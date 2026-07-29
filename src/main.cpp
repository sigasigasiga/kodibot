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
import kodibot.os;
import kodibot.telegram;
import kodibot.util;
import kodibot.version;

namespace td_api = td::td_api;

namespace boost {

void validate(
    boost::any &v,
    const std::vector<std::string>& values,
    spdlog::level::level_enum *target,
    int
) {
    namespace po = boost::program_options;

    auto val = po::validators::get_single_string(values);
    std::ranges::transform(val, val.begin(), &grace::utility::to_lower);

    constexpr std::array log_levels = SPDLOG_LEVEL_NAMES;

    if (std::ranges::contains(log_levels, val)) {
        v = boost::any(spdlog::level::from_str(val));
    } else {
        throw po::validation_error(
            po::validation_error::invalid_option_value,
            "log-level",
            val
        );
    }
}

} // namespace boost

namespace {

auto make_auth_params(td_api::int32 api_id, std::string api_hash, std::string db_path)
{
    auto request = td_api::make_object<td_api::setTdlibParameters>();
    request->database_directory_ = std::move(db_path);
    request->use_message_database_ = true;
    request->use_secret_chats_ = false;
    request->api_id_ = api_id;
    request->api_hash_ = std::move(api_hash);
    request->system_language_code_ = "en";
    request->device_model_ = "Server";
    request->application_version_ = "1.0";
    return request;
}

void print_help(const boost::program_options::options_description &options) {
    std::cout << "Usage: kodibot [options]\n" << options << std::endl;
}

void set_db_cleanup_options(kodibot::telegram::client &client, td_api::int64 db_size_kb) {
    spdlog::trace("Enabling storage_optimizer...");

    client.send_request(
        td_api::make_object<td_api::setOption>(
            "storage_max_files_size", /* in kilobytes */
            td_api::make_object<td_api::optionValueInteger>(db_size_kb)
        ),
        nullptr
    );

    // if we make this value configurable, some may fuck the bot up by setting it
    // too low and then complaining that the playback crashes arbitrarily
    client.send_request(
        td_api::make_object<td_api::setOption>(
            "storage_immunity_delay", /* in seconds */
            td_api::make_object<td_api::optionValueInteger>(60 * 60 * 24 * 1) /* 1 day */
        ),
        nullptr
    );

    client.send_request(
        td_api::make_object<td_api::setOption>(
            "use_storage_optimizer",
            td_api::make_object<td_api::optionValueBoolean>(true)
        ),
        nullptr
    );
}

class kodibot_app : private kodibot::bot::bot::hoster
                  , private kodibot::bot::bot::player
{
public:
    kodibot_app(
        std::string db_path,
        td_api::int64 db_size_kb,
        td_api::int32 api_id,
        std::string api_hash,
        std::string bot_token,
        std::string http_server_address,
        std::uint16_t http_server_port,
        std::unordered_set<std::int64_t> user_whitelist,
        kodibot::kodi::connection kodi_conn
    )
        : m_signal_monitor{SIGINT, SIGTERM}
        , m_client(m_client_manager.make_client())
        , m_state(
              std::in_place_index<0>,
              m_client,
              make_auth_params(api_id, std::move(api_hash), std::move(db_path)),
              std::move(bot_token)
          )
        , m_http_server_address(std::move(http_server_address))
        , m_http_server_port(http_server_port)
        , m_kodi_enabled(!kodi_conn.host.empty())
        , m_kodi(std::move(kodi_conn))
    {
        spdlog::debug("Initializing kodibot application");
        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        set_db_cleanup_options(m_client, db_size_kb);

        setup_http_routes();

        get<0>(m_state).start(
            [this, whitelist = std::move(user_whitelist)]
            (kodibot::telegram::client &client, td::td_api::object_ptr<td::td_api::error> error) mutable {
                if (error) {
                    spdlog::error("Authentication failed: {}", to_string(*error));
                    m_client_manager.stop();
                } else {
                    spdlog::info("Bot authenticated successfully");
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
        // TODO: these will `std::terminate` on exception
        m_http_thread = std::thread([this] {
            spdlog::info("HTTP server starting, listening on {}:{}", m_http_server_address, m_http_server_port);
            if (!m_server.listen(m_http_server_address, m_http_server_port)) {
                spdlog::error("HTTP server failed to bind to {}:{}", m_http_server_address, m_http_server_port);
            }

            spdlog::info("HTTP server thread is shutting down...");
            stop();
        });

        m_telegram_thread = std::thread([this] {
            spdlog::info("TDLib client manager starting");
            m_client_manager.run();

            spdlog::info("TDLib client manager thread is shutting down...");

            stop();
        });

        if (auto sig = m_signal_monitor.wait()) {
            spdlog::info("Received signal {}, shutting down", *sig);
            stop();
        } else {
            spdlog::info("Signal monitor got stopped, shutting down");

            // NB: no need to call `stop()` here because the signal monitor
            // was stopped by `stop()` itself, so the other threads are already shutting down
        }

        m_http_thread.join();
        m_telegram_thread.join();

        spdlog::info("Shutdown");
    }

    void stop() {
        spdlog::info("Stopping the bot");
        m_signal_monitor.stop();
        m_client_manager.stop();

        // it fails on an internal `assert` when it is stopped multiple times simultaneously
        std::call_once(m_server_stop_flag, [this] { m_server.stop(); });

        spdlog::info("The bot has been stopped");
    }

private:
    struct video_info {
        td_api::int32 file_id{0};
        std::int64_t size{0};
        std::string mime_type;
        bool supports_streaming{false};
    };

    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f) {
        auto func_id = f->get_id();
        spdlog::trace("Sending synchronous request: {}", func_id);
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

        return std::format("http://{}:{}/videos/{}", m_http_server_address, m_http_server_port, file_id);
    }

    // kodibot::bot::bot::player
    void play(std::string url) final {
        if (!m_kodi_enabled) {
            spdlog::trace("Kodi is not enabled, skipping playback of {}", url);
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
            } else {
                spdlog::info("Kodi playback initiated successfully");
            }
        }).detach();
    }

    void setup_http_routes() {
        m_server.Get(R"(/videos/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
            td_api::int32 file_id = 0;
            try {
                file_id = static_cast<td_api::int32>(std::stoi(req.matches[1].str()));
            } catch (...) {
                spdlog::warn("Invalid file_id in request: {}", req.matches[1].str());
                res.status = 400;
                return;
            }

            spdlog::trace("Got HTTP request for video {}", file_id);

            std::optional<video_info> info;
            {
                std::lock_guard lock(m_videos_mutex);
                if (auto it = m_videos.find(file_id); it != m_videos.end()) {
                    info = it->second;
                }
            }

            if (!info) {
                spdlog::warn("Video {} not found in registry", file_id);
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
        spdlog::debug("Setting up seekable streaming for file_id={}, total_size={}", info.file_id, info.size);

        res.set_content_provider(
            static_cast<size_t>(info.size),
            info.mime_type,
            [this, info](size_t offset, size_t length, httplib::DataSink &sink) -> bool {
                constexpr size_t kChunkSize = 256 * 1024;
                const auto to_fetch = static_cast<td_api::int53>(std::min(length, kChunkSize));

                spdlog::trace("Fetching chunk: offset={}, length={}, to_fetch={}", offset, length, to_fetch);

                auto dl_req = td_api::make_object<td_api::downloadFile>();
                dl_req->file_id_ = info.file_id;
                dl_req->priority_ = 1;
                dl_req->offset_ = static_cast<td_api::int53>(offset);
                dl_req->limit_ = to_fetch;
                dl_req->synchronous_ = true;
                auto dl_result = send_query_sync(std::move(dl_req));
                if (!dl_result || dl_result->get_id() != td_api::file::ID) {
                    spdlog::warn(
                        "downloadFile failed for file_id={} at offset={}",
                        info.file_id, offset
                    );
                    return false;
                }

                auto file = td::move_tl_object_as<td_api::file>(dl_result);
                if (!file->local_ || file->local_->path_.empty()) {
                    spdlog::error(
                        "no local path for file_id={} at offset={}",
                        info.file_id, offset
                    );
                    return false;
                }

                spdlog::trace("Opening file for reading: {}", file->local_->path_);
                std::ifstream in(file->local_->path_, std::ios::binary);
                if (!in) {
                    spdlog::error(
                        "failed to open {} for file_id={}",
                        file->local_->path_, info.file_id
                    );
                    return false;
                }
                in.seekg(static_cast<std::streamoff>(offset));

                std::vector<char> buf(to_fetch);
                in.read(buf.data(), to_fetch);
                const auto n = in.gcount();
                if (n <= 0) {
                    spdlog::warn("Read returned {} bytes at offset={}", n, offset);
                    return false;
                }

                spdlog::trace("Sending {} bytes for file_id={}", n, info.file_id);
                return sink.write(buf.data(), static_cast<size_t>(n));
            });
    }

    void serve_non_seekable(httplib::Response &res, video_info info) {
        spdlog::debug("Setting up non-seekable streaming for file_id={}", info.file_id);

        auto dl_req = td_api::make_object<td_api::downloadFile>();
        dl_req->file_id_ = info.file_id;
        dl_req->priority_ = 1;
        dl_req->offset_ = 0;
        dl_req->limit_ = 0;
        dl_req->synchronous_ = true;

        spdlog::trace("Downloading complete file for file_id={}", info.file_id);
        auto dl_result = send_query_sync(std::move(dl_req));
        if (!dl_result || dl_result->get_id() != td_api::file::ID) {
            spdlog::error("downloadFile failed for file_id={}", info.file_id);
            res.status = 500;
            return;
        }

        auto file = td::move_tl_object_as<td_api::file>(dl_result);
        if (!file->local_ || file->local_->path_.empty()) {
            spdlog::error("no local path for file_id={}", info.file_id);
            res.status = 500;
            return;
        }

        spdlog::debug("Opening file for non-seekable streaming: {}", file->local_->path_);
        auto in = std::make_shared<std::ifstream>(file->local_->path_, std::ios::binary);
        if (!*in) {
            spdlog::error("failed to open {} for file_id={}", file->local_->path_, info.file_id);
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
    // it is important that `m_signal_monitor` is created before any other threads
    kodibot::os::signal_monitor m_signal_monitor;

    kodibot::telegram::client_manager m_client_manager;
    kodibot::telegram::client &m_client;
    state_type m_state;
    std::thread m_telegram_thread;

    std::string m_http_server_address;
    std::uint16_t m_http_server_port;
    std::once_flag m_server_stop_flag;
    httplib::Server m_server;
    std::thread m_http_thread;

    std::mutex m_videos_mutex;
    std::map<td_api::int32, video_info> m_videos;

    bool m_kodi_enabled;
    kodibot::kodi::client m_kodi;
};

constexpr const char *k_credentials_filename = "kodibot.conf";

// https://systemd.io/CREDENTIALS/
//
// NB: Values already present in `vm` (i.e. those passed on the command line)
// take precedence: program_options keeps the first stored, non-default value for
// each option, so this must be called after the command line has been stored.
void load_systemd_credentials(
    const boost::program_options::options_description &options,
    boost::program_options::variables_map &vm
) {
    namespace po = boost::program_options;

    const char *creds_dir = std::getenv("CREDENTIALS_DIRECTORY");
    if (!creds_dir || *creds_dir == '\0') {
        return;
    }

    const auto config_path = std::filesystem::path(creds_dir) / k_credentials_filename;
    std::ifstream config_file(config_path);
    if (!config_file) {
        spdlog::warn(
            "CREDENTIALS_DIRECTORY is set but '{}' could not be opened; "
            "relying on command-line options only.",
            config_path.string()
        );
        return;
    }

    spdlog::info("Loading configuration from systemd credential '{}'.", config_path.string());
    po::store(po::parse_config_file(config_file, options), vm);
}

std::unordered_set<std::int64_t> parse_user_whitelist(const std::string &spec) {
    std::unordered_set<std::int64_t> whitelist;
    std::stringstream stream(spec);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        const auto begin = entry.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            continue;
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

}  // namespace

int main(int argc, char **argv) {
    namespace po = boost::program_options;

    spdlog::level::level_enum log_level = spdlog::level::info;
    std::string db_path;
    td_api::int64 db_size_kb = 0;
    td_api::int32 api_id = 0;
    std::string api_hash;
    std::string token;
    std::string whitelist_str;
    std::string http_server_address;
    std::uint16_t http_server_port = 0;
    std::string kodi_address;
    std::uint16_t kodi_port = 0;
    std::string kodi_username;
    std::string kodi_password;

    using namespace std::string_view_literals;

    constexpr std::array log_levels_arr = SPDLOG_LEVEL_NAMES;
    std::string log_levels_msg = log_levels_arr | std::views::join_with(", "sv) | std::ranges::to<std::string>();
    auto log_level_option_msg = std::format("Log level: {}.", log_levels_msg);

    po::options_description options("Options");
    options.add_options()
        ("help,h", "Show this help message and exit.")
        ("version,v", "Show version and exit.")
        ("log-level", po::value<spdlog::level::level_enum>(&log_level)->default_value(spdlog::level::info, "info"),
         log_level_option_msg.c_str())

        ("telegram-db-path", po::value<std::string>(&db_path)->required(),
         "Telegram database path")
        ("telegram-db-size", po::value<td_api::int64>(&db_size_kb)->default_value(10 * 1024 * 1024), /* 10 GB */
         "Telegram database size in kilobytes")

        ("telegram-api-id", po::value<td_api::int32>(&api_id)->required(),
         "Telegram API id.")
        ("telegram-api-hash", po::value<std::string>(&api_hash)->required(),
         "Telegram API hash.")
        ("telegram-bot-token", po::value<std::string>(&token)->required(),
         "Telegram bot token.")
        ("telegram-user-whitelist", po::value<std::string>(&whitelist_str),
         "Comma-separated list of allowed Telegram user IDs, "
         "e.g. \"123456789,987654321\".")

        ("http-server-address", po::value<std::string>(&http_server_address)->default_value("127.0.0.1"),
         "Address Kodi uses to reach this bot's HTTP server ")
        ("http-server-port", po::value<std::uint16_t>(&http_server_port)->default_value(9988),
         "Port the bot's HTTP server listens on.")

        ("kodi-address", po::value<std::string>(&kodi_address)->default_value("127.0.0.1"),
         "Kodi host. Enables playback: received videos are sent to the Kodi "
         "JSON-RPC interface at kodi-host:kodi-port.")
        ("kodi-port", po::value<std::uint16_t>(&kodi_port)->default_value(8080),
         "Kodi JSON-RPC port.")
        ("kodi-username", po::value<std::string>(&kodi_username),
         "Kodi JSON-RPC username.")
        ("kodi-password", po::value<std::string>(&kodi_password),
         "Kodi JSON-RPC password.")
    ;

    const std::string_view exe_name = argc > 0 ? argv[0] : "kodibot";

    po::variables_map vm;
    try {
        // Store the command line first so it takes precedence over the
        // credentials file: program_options keeps the first stored value for
        // each option.
        po::store(po::parse_command_line(argc, argv, options), vm);
        if (vm.count("help")) {
            print_help(options);
            return 0;
        }
        if (vm.count("version")) {
            std::cout << "kodibot " << kodibot::version::version_string << std::endl;
            return 0;
        }

        spdlog::set_level(log_level);

        spdlog::info("Starting kodibot {}...", kodibot::version::version_string);

        load_systemd_credentials(options, vm);
        po::notify(vm);
    } catch (const po::error &e) {
        spdlog::error("Cannot parse arguments: {}", e.what());
        print_help(options);
        return 1;
    }

    auto user_whitelist = parse_user_whitelist(whitelist_str);
    if (user_whitelist.empty()) {
        spdlog::warn(
            "User whitelist is empty; no incoming messages will be processed. "
            "Pass a comma-separated list of allowed user IDs to enable the bot."
        );
    } else {
        spdlog::info("Loaded user whitelist with {} entries.", user_whitelist.size());
    }

    kodibot::kodi::connection kodi_conn;
    kodi_conn.host = kodi_address;
    kodi_conn.port = kodi_port;
    kodi_conn.username = std::move(kodi_username);
    kodi_conn.password = std::move(kodi_password);

    if (kodi_conn.host.empty()) {
        spdlog::warn(
            "Kodi address is not set; Kodi playback is disabled. "
            "Videos will still be served over HTTP. Pass a Kodi address to enable playback."
        );
    } else {
        spdlog::info("Kodi playback enabled (target {}:{}).", kodi_conn.host, kodi_conn.port);
    }

    kodibot_app bot(
        std::move(db_path),
        db_size_kb,
        api_id,
        std::move(api_hash),
        std::move(token),
        std::move(http_server_address),
        http_server_port,
        std::move(user_whitelist),
        std::move(kodi_conn)
    );

    bot.run();

    return 0;
}
