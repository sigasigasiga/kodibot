#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <boost/program_options.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <unistd.h>

import grace;
import kodibot.bot;
import kodibot.kodi;
import kodibot.telegram;
import kodibot.util;

namespace td_api = td::td_api;

namespace {

auto make_auth_params(td_api::int32 api_id, std::string api_hash)
{
    auto request = td_api::make_object<td_api::setTdlibParameters>();
    request->database_directory_ = "tdlib_db";
    request->use_message_database_ = true;
    request->use_secret_chats_ = false;
    request->api_id_ = api_id;
    request->api_hash_ = std::move(api_hash);
    request->system_language_code_ = "en";
    request->device_model_ = "Server";
    request->application_version_ = "1.0";
    return request;
}


class kodibot_app : private kodibot::bot::bot::hoster
                  , private kodibot::bot::bot::player
{
public:
    kodibot_app(
        td_api::int32 api_id,
        std::string api_hash,
        std::string bot_token,
        int http_port,
        std::unordered_set<std::int64_t> user_whitelist,
        kodibot::kodi::connection kodi_conn,
        std::string public_host
    )
        : m_client(m_client_manager.make_client())
        , m_state(
              std::in_place_index<0>,
              m_client,
              make_auth_params(api_id, std::move(api_hash)),
              std::move(bot_token)
          )
        , m_http_port(http_port)
        , m_kodi_enabled(!kodi_conn.host.empty())
        , m_kodi(std::move(kodi_conn))
        , m_public_host(std::move(public_host))
    {
        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        setup_http_routes();

        get<0>(m_state).start(
            [this, whitelist = std::move(user_whitelist)]
            (kodibot::telegram::client &client, td::td_api::object_ptr<td::td_api::error> error) mutable {
                if (error) {
                    spdlog::error("Authentication failed: {}", to_string(*error));
                    m_client_manager.stop();
                } else {
                    spdlog::info("Bot is online and ready.");
                    m_state.emplace<1>(
                        static_cast<kodibot::bot::bot::hoster &>(*this),
                        static_cast<kodibot::bot::bot::player &>(*this),
                        client,
                        std::move(whitelist)
                    );
                }
            }
        );
    }

    void run() {
        m_http_thread = std::thread([this] {
            spdlog::info("HTTP server listening on 0.0.0.0:{}", m_http_port);
            if (!m_server.listen("0.0.0.0", m_http_port)) {
                spdlog::error("HTTP server failed to bind to port {}", m_http_port);
                m_client_manager.stop();
            }
        });

        // Drives the TDLib receive loop and dispatches responses/updates back
        // to the client (and therefore to our subscribed handlers). Returns once
        // a shutdown has been requested (signal or auth failure).
        m_client_manager.run();

        spdlog::info("Shutting down...");
        m_server.stop();
        if (m_http_thread.joinable()) {
            m_http_thread.join();
        }
    }

    void stop() {
        m_client_manager.stop();
        m_server.stop();
    }

private:
    struct video_info {
        td_api::int32 file_id{0};
        std::int64_t size{0};
        std::string mime_type;
        bool supports_streaming{false};
    };

    // Synchronous wrapper around the asynchronous client::send_request, used by
    // the HTTP worker thread. The callback runs on the client_manager::run()
    // thread; we block here until it fires.
    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f) {
        auto promise = std::make_shared<std::promise<td_api::object_ptr<td_api::Object>>>();
        auto future = promise->get_future();
        m_client.send_request(std::move(f), [promise](td_api::object_ptr<td_api::Object> obj) mutable {
            promise->set_value(std::move(obj));
        });
        return future.get();
    }

    // kodibot::bot::bot::hoster
    std::string host_video(
        td::td_api::int32 file_id,
        td::td_api::int53 size,
        std::string mime_type,
        bool supports_streaming
    ) final {
        video_info info{
            .file_id = file_id,
            .size = size,
            .mime_type = std::move(mime_type),
            .supports_streaming = supports_streaming,
        };

        {
            std::lock_guard lock(m_videos_mutex);
            m_videos[file_id] = info;
        }

        spdlog::info(
            "Registered video at /videos/{} (size={}, mime={}, streaming={})",
            file_id, size, info.mime_type, info.supports_streaming
        );

        return std::format("http://{}:{}/videos/{}", m_public_host, m_http_port, file_id);
    }

    // kodibot::bot::bot::player
    void play(std::string url) final {
        if (!m_kodi_enabled) {
            return;
        }

        // The request runs on a detached thread: this method is called from the client_manager::run()
        // receive loop, but Player.Open can block until Kodi opens the stream, which
        // it does by fetching from our HTTP server thread, which in turn relies on
        // the receive loop to fulfil downloadFile. Blocking here would deadlock.
        std::thread([this, url = std::move(url)] {
            spdlog::info("Asking Kodi to play {}", url);
            if (auto result = m_kodi.play(url); !result) {
                spdlog::error("Kodi playback failed: {}", result.error());
            }
        }).detach();
    }

    void setup_http_routes() {
        m_server.Get(R"(/videos/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
            td_api::int32 file_id = 0;
            try {
                file_id = static_cast<td_api::int32>(std::stoi(req.matches[1].str()));
            } catch (...) {
                res.status = 400;
                return;
            }

            std::optional<video_info> info;
            {
                std::lock_guard lock(m_videos_mutex);
                if (auto it = m_videos.find(file_id); it != m_videos.end()) {
                    info = it->second;
                }
            }
            if (!info) {
                res.status = 404;
                return;
            }

            if (info->supports_streaming && info->size > 0) {
                serve_seekable(res, *info);
            } else {
                serve_non_seekable(res, *info);
            }
        });
    }

    void serve_seekable(httplib::Response &res, video_info info) {
        res.set_content_provider(
            static_cast<size_t>(info.size),
            info.mime_type,
            [this, info](size_t offset, size_t length, httplib::DataSink &sink) -> bool {
                constexpr size_t kChunkSize = 256 * 1024;
                const auto to_fetch = static_cast<td_api::int53>(std::min(length, kChunkSize));

                auto dl_req = td_api::make_object<td_api::downloadFile>();
                dl_req->file_id_ = info.file_id;
                dl_req->priority_ = 1;
                dl_req->offset_ = static_cast<td_api::int53>(offset);
                dl_req->limit_ = to_fetch;
                dl_req->synchronous_ = true;
                auto dl_result = send_query_sync(std::move(dl_req));
                if (!dl_result || dl_result->get_id() != td_api::file::ID) {
                    spdlog::warn("downloadFile failed for file_id={} at offset={}",
                                 info.file_id, offset);
                    return false;
                }

                auto file = td::move_tl_object_as<td_api::file>(dl_result);
                if (!file->local_ || file->local_->path_.empty()) {
                    spdlog::warn("no local path for file_id={} at offset={}",
                                 info.file_id, offset);
                    return false;
                }

                std::ifstream in(file->local_->path_, std::ios::binary);
                if (!in) {
                    spdlog::warn("failed to open {} for file_id={}",
                                 file->local_->path_, info.file_id);
                    return false;
                }
                in.seekg(static_cast<std::streamoff>(offset));

                std::array<char, kChunkSize> buf{};
                in.read(buf.data(), to_fetch);
                const auto n = in.gcount();
                if (n <= 0) {
                    return false;
                }
                return sink.write(buf.data(), static_cast<size_t>(n));
            });
    }

    void serve_non_seekable(httplib::Response &res, video_info info) {
        auto dl_req = td_api::make_object<td_api::downloadFile>();
        dl_req->file_id_ = info.file_id;
        dl_req->priority_ = 1;
        dl_req->offset_ = 0;
        dl_req->limit_ = 0;
        dl_req->synchronous_ = true;
        auto dl_result = send_query_sync(std::move(dl_req));
        if (!dl_result || dl_result->get_id() != td_api::file::ID) {
            res.status = 500;
            return;
        }
        auto file = td::move_tl_object_as<td_api::file>(dl_result);
        if (!file->local_ || file->local_->path_.empty()) {
            res.status = 500;
            return;
        }

        auto in = std::make_shared<std::ifstream>(file->local_->path_, std::ios::binary);
        if (!*in) {
            res.status = 500;
            return;
        }

        res.set_header("Accept-Ranges", "none");
        res.set_chunked_content_provider(
            info.mime_type,
            [in](size_t /*offset*/, httplib::DataSink &sink) -> bool {
                std::array<char, 64 * 1024> buf{};
                in->read(buf.data(), buf.size());
                const auto n = in->gcount();
                if (n > 0 && !sink.write(buf.data(), static_cast<size_t>(n))) {
                    return false;
                }
                if (n < static_cast<std::streamsize>(buf.size())) {
                    sink.done();
                }
                return true;
            });
    }

private:
    using state_type = std::variant<
        kodibot::telegram::bot_auth,
        kodibot::bot::bot
    >;

private:
    kodibot::telegram::client_manager m_client_manager;
    kodibot::telegram::client &m_client;
    state_type m_state;

    int m_http_port;
    httplib::Server m_server;
    std::thread m_http_thread;

    std::mutex m_videos_mutex;
    std::map<td_api::int32, video_info> m_videos;

    bool m_kodi_enabled;
    kodibot::kodi::client m_kodi;
    std::string m_public_host;
};

// Name of the credentials file looked up inside $CREDENTIALS_DIRECTORY. With
// systemd this is the credential id, e.g. configured via
// `LoadCredential=kodibot.conf:/etc/kodibot.conf` in the unit file.
constexpr const char *kCredentialsFileName = "kodibot.conf";

// Loads options from the systemd credentials store, if available. systemd
// exposes credentials as files inside the directory named by
// $CREDENTIALS_DIRECTORY; we parse kCredentialsFileName there as a
// program_options config file (one `key = value` per line, using the same
// long option names as the command line).
//
// Values already present in `vm` (i.e. those passed on the command line) take
// precedence: program_options keeps the first stored, non-default value for
// each option, so this must be called after the command line has been stored.
void load_systemd_credentials(const boost::program_options::options_description &options,
                              boost::program_options::variables_map &vm) {
    namespace po = boost::program_options;

    const char *creds_dir = std::getenv("CREDENTIALS_DIRECTORY");
    if (!creds_dir || *creds_dir == '\0') {
        return;
    }

    const std::filesystem::path config_path =
        std::filesystem::path(creds_dir) / kCredentialsFileName;
    std::ifstream config_file(config_path);
    if (!config_file) {
        spdlog::warn(
            "CREDENTIALS_DIRECTORY is set but '{}' could not be opened; "
            "relying on command-line options only.",
            config_path.string());
        return;
    }

    spdlog::info("Loading configuration from systemd credential '{}'.", config_path.string());
    po::store(po::parse_config_file(config_file, options), vm);
}

// Parses a comma-separated list of Telegram user IDs (e.g. "123,456,789") into
// a set. Whitespace around entries is ignored; invalid entries are skipped with
// a warning.
std::unordered_set<std::int64_t> parse_user_whitelist(const std::string &spec) {
    std::unordered_set<std::int64_t> whitelist;
    std::stringstream stream(spec);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        const auto begin = entry.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            continue;  // empty / whitespace-only entry
        }
        const auto end = entry.find_last_not_of(" \t");
        const auto trimmed = entry.substr(begin, end - begin + 1);
        try {
            whitelist.insert(std::stoll(trimmed));
        } catch (const std::exception &e) {
            spdlog::warn("Ignoring invalid whitelist user id '{}': {}", trimmed, e.what());
        }
    }
    return whitelist;
}

// Set while kodibot_app::run() is executing so the signal handler can request a
// graceful shutdown. request_stop() on libstdc++/libc++ is lock-free, which is
// good enough for use from a signal handler here.
kodibot_app *g_app = nullptr;

extern "C" void handle_termination_signal(int /*signum*/) {
    if (g_app) {
        g_app->stop();
    }
}

}  // namespace

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::debug);

    namespace po = boost::program_options;

    td_api::int32 api_id = 0;
    std::string api_hash;
    std::string token;
    std::string whitelist_str;
    int http_port = 9988;
    std::string kodi_host;
    int kodi_port = 8080;
    std::string kodi_username;
    std::string kodi_password;
    std::string public_host;

    po::options_description options("Options");
    options.add_options()
        ("help,h", "Show this help message and exit.")
        ("telegram-api-id", po::value<td_api::int32>(&api_id)->required(),
         "Telegram API id.")
        ("telegram-api-hash", po::value<std::string>(&api_hash)->required(),
         "Telegram API hash.")
        ("telegram-bot-token", po::value<std::string>(&token)->required(),
         "Telegram bot token.")
        ("telegram-user-whitelist", po::value<std::string>(&whitelist_str),
         "Comma-separated list of allowed Telegram user IDs, "
         "e.g. \"123456789,987654321\".")
        ("http-port", po::value<int>(&http_port)->default_value(http_port),
         "Port the bot's HTTP server listens on.")
        ("kodi-host", po::value<std::string>(&kodi_host),
         "Kodi host. Enables playback: received videos are sent to the Kodi "
         "JSON-RPC interface at kodi-host:kodi-port.")
        ("kodi-port", po::value<int>(&kodi_port)->default_value(kodi_port),
         "Kodi JSON-RPC port.")
        ("kodi-username", po::value<std::string>(&kodi_username),
         "Kodi JSON-RPC username.")
        ("kodi-password", po::value<std::string>(&kodi_password),
         "Kodi JSON-RPC password.")
        ("public-host", po::value<std::string>(&public_host),
         "Address Kodi uses to reach this bot's HTTP server "
         "(defaults to the local hostname).");

    po::variables_map vm;
    try {
        // Store the command line first so it takes precedence over the
        // credentials file: program_options keeps the first stored value for
        // each option.
        po::store(po::parse_command_line(argc, argv, options), vm);
        if (vm.count("help")) {
            std::ostringstream usage;
            usage << options;
            spdlog::info("Usage: {} [options]\n{}",
                         argc > 0 ? argv[0] : "kodibot", usage.str());
            return 0;
        }
        load_systemd_credentials(options, vm);
        po::notify(vm);
    } catch (const po::error &e) {
        spdlog::error("{}", e.what());
        std::ostringstream usage;
        usage << options;
        spdlog::error("Usage: {} [options]\n{}",
                      argc > 0 ? argv[0] : "kodibot", usage.str());
        return 1;
    }

    auto user_whitelist = parse_user_whitelist(whitelist_str);
    if (user_whitelist.empty()) {
        spdlog::warn(
            "User whitelist is empty; no incoming messages will be processed. "
            "Pass a comma-separated list of allowed user IDs to enable the bot.");
    } else {
        spdlog::info("Loaded user whitelist with {} entries.", user_whitelist.size());
    }

    kodibot::kodi::connection kodi_conn;
    kodi_conn.host = kodi_host;
    kodi_conn.port = kodi_port;
    kodi_conn.username = std::move(kodi_username);
    kodi_conn.password = std::move(kodi_password);

    if (kodi_conn.host.empty()) {
        spdlog::warn(
            "KODI_HOST is not set; Kodi playback is disabled. Videos will still be "
            "served over HTTP. Pass a Kodi host to enable playback.");
    } else {
        if (public_host.empty()) {
            std::array<char, 256> hostname{};
            if (gethostname(hostname.data(), hostname.size() - 1) == 0) {
                public_host = hostname.data();
            }
            if (public_host.empty()) {
                public_host = "localhost";
            }
            spdlog::warn(
                "KODIBOT_PUBLIC_HOST is not set; defaulting to '{}'. Kodi must be "
                "able to reach this bot's HTTP server at that address.",
                public_host);
        }
        spdlog::info("Kodi playback enabled (target {}:{}).", kodi_conn.host, kodi_conn.port);
    }

    kodibot_app bot(api_id, std::move(api_hash), std::move(token), http_port,
                    std::move(user_whitelist), std::move(kodi_conn), std::move(public_host));

    g_app = &bot;
    std::signal(SIGINT, handle_termination_signal);
    std::signal(SIGTERM, handle_termination_signal);

    bot.run();

    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    g_app = nullptr;

    return 0;
}
