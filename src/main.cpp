#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace td_api = td::td_api;

namespace {

template <class... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};

template <class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

class kodibot {
public:
    using callback_t = std::move_only_function<void(td_api::object_ptr<td_api::Object>) &&>;

public:
    kodibot(td_api::int32 api_id, std::string api_hash, std::string bot_token, int http_port)
        : m_api_id(api_id)
        , m_api_hash(std::move(api_hash))
        , m_bot_token(std::move(bot_token))
        , m_client_id(m_client_manager.create_client_id())
        , m_http_port(http_port)
    {
        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        send_query(td_api::make_object<td_api::getOption>("version"), nullptr);
        setup_http_routes();
    }

    void run() {
        m_http_thread = std::thread([this] {
            spdlog::info("HTTP server listening on 0.0.0.0:{}", m_http_port);
            if (!m_server.listen("0.0.0.0", m_http_port)) {
                spdlog::error("HTTP server failed to bind to port {}", m_http_port);
            }
        });

        while (!m_should_exit) {
            process_response(m_client_manager.receive(10.0));
        }

        m_server.stop();
        if (m_http_thread.joinable()) {
            m_http_thread.join();
        }
    }

private:
    struct video_info {
        td_api::int32 file_id{0};
        std::int64_t size{0};
        std::string mime_type;
        bool supports_streaming{false};
    };

    std::uint64_t next_query_id() { return ++m_current_query_id; }

    void send_query(td_api::object_ptr<td_api::Function> f, callback_t handler) {
        auto query_id = next_query_id();
        if (handler) {
            std::lock_guard lock(m_handlers_mutex);
            m_handlers.emplace(query_id, std::move(handler));
        }
        m_client_manager.send(m_client_id, query_id, std::move(f));
    }

    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f) {
        auto promise = std::make_shared<std::promise<td_api::object_ptr<td_api::Object>>>();
        auto future = promise->get_future();
        send_query(std::move(f), [promise](td_api::object_ptr<td_api::Object> obj) mutable {
            promise->set_value(std::move(obj));
        });
        return future.get();
    }

    void process_response(td::ClientManager::Response response) {
        if (!response.object) {
            return;
        }

        if (response.request_id == 0) {
            process_update(std::move(response.object));
            return;
        }

        callback_t handler;
        {
            std::lock_guard lock(m_handlers_mutex);
            if (auto node = m_handlers.extract(response.request_id)) {
                handler = std::move(node.mapped());
            }
        }
        if (handler) {
            std::invoke(std::move(handler), std::move(response.object));
        }
    }

    void process_update(td_api::object_ptr<td_api::Object> update) {
        td_api::downcast_call(
            *update,
            overloaded{
                [this](td_api::updateAuthorizationState &state) {
                    on_authorization_state_update(state);
                },
                [this](td_api::updateNewMessage &update_new_message) {
                    on_new_message(*update_new_message.message_);
                },
                [](td_api::Object &upd) {
                    spdlog::trace("unexpected update: {}", upd.get_id());
                },
            });
    }

    void on_new_message(td_api::message &message) {
        if (message.is_outgoing_) {
            return;
        }

        td_api::downcast_call(*message.content_, overloaded{
            [this](td_api::messageVideo &m) {
                if (m.video_) {
                    register_video(*m.video_);
                }
            },
            [](td_api::MessageContent &m) {
                spdlog::trace("Got message content {}, ignoring...", m.get_id());
            },
        });
    }

    void register_video(const td_api::video &video) {
        if (!video.video_) {
            return;
        }
        const auto file_id = video.video_->id_;
        auto size = video.video_->size_;
        if (size == 0) {
            size = video.video_->expected_size_;
        }
        video_info info{
            .file_id = file_id,
            .size = size,
            .mime_type = video.mime_type_.empty() ? std::string{"video/mp4"} : video.mime_type_,
            .supports_streaming = video.supports_streaming_,
        };
        {
            std::lock_guard lock(m_videos_mutex);
            m_videos[file_id] = info;
        }
        spdlog::info(
            "Registered video at /videos/{} (size={}, mime={}, streaming={})",
            file_id, size, info.mime_type, info.supports_streaming);
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

                auto rd_req = td_api::make_object<td_api::readFilePart>();
                rd_req->file_id_ = info.file_id;
                rd_req->offset_ = static_cast<td_api::int53>(offset);
                rd_req->count_ = to_fetch;
                auto rd_result = send_query_sync(std::move(rd_req));
                if (!rd_result || rd_result->get_id() != td_api::data::ID) {
                    spdlog::warn("readFilePart failed for file_id={} at offset={}",
                                 info.file_id, offset);
                    return false;
                }

                auto part = td::move_tl_object_as<td_api::data>(rd_result);
                if (part->data_.empty()) {
                    return false;
                }
                return sink.write(part->data_.data(), part->data_.size());
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

        std::shared_ptr<std::FILE> fp(
            std::fopen(file->local_->path_.c_str(), "rb"),
            [](std::FILE *f) { if (f) std::fclose(f); });
        if (!fp) {
            res.status = 500;
            return;
        }

        res.set_header("Accept-Ranges", "none");
        res.set_chunked_content_provider(
            info.mime_type,
            [fp](size_t /*offset*/, httplib::DataSink &sink) -> bool {
                std::array<char, 64 * 1024> buf{};
                auto n = std::fread(buf.data(), 1, buf.size(), fp.get());
                if (n > 0 && !sink.write(buf.data(), n)) {
                    return false;
                }
                if (n < buf.size()) {
                    sink.done();
                }
                return true;
            });
    }

    auto make_auth_handler() {
        return [this, id = m_authentication_query_id](td_api::object_ptr<td_api::Object> object) {
            if (id == m_authentication_query_id && object->get_id() == td_api::error::ID) {
                auto error = td::move_tl_object_as<td_api::error>(object);
                spdlog::error("Authentication error: {}", to_string(error));
                m_should_exit = true;
            }
        };
    }

    void on_authorization_state_update(td_api::updateAuthorizationState &auth_state) {
        ++m_authentication_query_id;

        td_api::downcast_call(
            *auth_state.authorization_state_,
            overloaded{
                [this](td_api::authorizationStateReady &) {
                    m_is_authorized = true;
                    spdlog::info("Bot is online and ready to echo messages.");
                },
                [this](td_api::authorizationStateLoggingOut &) {
                    m_is_authorized = false;
                    spdlog::info("Logging out...");
                },
                [](td_api::authorizationStateClosing &) {
                    spdlog::info("Closing...");
                },
                [this](td_api::authorizationStateClosed &) {
                    m_is_authorized = false;
                    m_should_exit = true;
                    spdlog::info("TDLib instance terminated.");
                },
                [this](td_api::authorizationStateWaitPhoneNumber &) {
                    send_query(
                        td_api::make_object<td_api::checkAuthenticationBotToken>(m_bot_token),
                        make_auth_handler()
                    );
                    spdlog::info("Bot logged in");
                },
                [this](td_api::authorizationStateWaitTdlibParameters &) {
                    auto request = td_api::make_object<td_api::setTdlibParameters>();
                    request->database_directory_ = "tdlib_db";
                    request->use_message_database_ = true;
                    request->use_secret_chats_ = false;
                    request->api_id_ = m_api_id;
                    request->api_hash_ = m_api_hash;
                    request->system_language_code_ = "en";
                    request->device_model_ = "Server";
                    request->application_version_ = "1.0";
                    send_query(std::move(request), make_auth_handler());
                    spdlog::info("Auth parameters set");
                },
                [](td_api::AuthorizationState &upd) {
                    spdlog::warn("unexpected auth state: {}", upd.get_id());
                },
            });
    }

private:
    td_api::int32 m_api_id;
    std::string m_api_hash;
    std::string m_bot_token;

    td::ClientManager m_client_manager;
    td::ClientManager::ClientId m_client_id{0};

    bool m_is_authorized{false};

    bool m_should_exit{false};
    std::atomic<std::uint64_t> m_current_query_id{0};
    std::uint64_t m_authentication_query_id{0}; // don't know why it's needed but it exists in the official cpp example

    std::mutex m_handlers_mutex;
    std::map<std::uint64_t, callback_t> m_handlers;

    int m_http_port;
    httplib::Server m_server;
    std::thread m_http_thread;

    std::mutex m_videos_mutex;
    std::map<td_api::int32, video_info> m_videos;
};

}  // namespace

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::debug);

    auto arg_or_env = [&](int index, const char *env_name) -> std::string {
        if (argc > index) {
            return argv[index];
        }
        if (const char *env = std::getenv(env_name)) {
            return env;
        }
        return {};
    };

    std::string api_id_str = arg_or_env(1, "TELEGRAM_API_ID");
    std::string api_hash = arg_or_env(2, "TELEGRAM_API_HASH");
    std::string token = arg_or_env(3, "TELEGRAM_BOT_TOKEN");

    if (api_id_str.empty() || api_hash.empty() || token.empty()) {
        spdlog::error(
            "Usage: {} <api_id> <api_hash> <bot_token>\n"
            "Or set the TELEGRAM_API_ID, TELEGRAM_API_HASH, and TELEGRAM_BOT_TOKEN "
            "environment variables.",
            argc > 0 ? argv[0] : "kodibot");
        return 1;
    }

    td_api::int32 api_id = 0;
    try {
        api_id = std::stoi(api_id_str);
    } catch (const std::exception &e) {
        spdlog::error("Invalid api_id '{}': {}", api_id_str, e.what());
        return 1;
    }

    int http_port = 8080;
    if (const char *p = std::getenv("KODIBOT_HTTP_PORT")) {
        try {
            http_port = std::stoi(p);
        } catch (...) {
            spdlog::warn("Invalid KODIBOT_HTTP_PORT='{}', falling back to {}", p, http_port);
        }
    }

    kodibot bot(api_id, std::move(api_hash), std::move(token), http_port);
    bot.run();
    return 0;
}
