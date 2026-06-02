#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <string>
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
    kodibot(td_api::int32 api_id, std::string api_hash, std::string bot_token)
        : m_api_id(api_id)
        , m_api_hash(std::move(api_hash))
        , m_bot_token(std::move(bot_token))
        , m_client_id(m_client_manager.create_client_id())
    {
        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        send_query(td_api::make_object<td_api::getOption>("version"), nullptr);
    }

    void run() {
        while (!m_should_exit) {
            process_response(m_client_manager.receive(10.0));
        }
    }

private:
    std::uint64_t next_query_id() { return ++m_current_query_id; }

    void send_query(td_api::object_ptr<td_api::Function> f, callback_t handler) {
        auto query_id = next_query_id();
        if (handler) {
            m_handlers.emplace(query_id, std::move(handler));
        }
        m_client_manager.send(m_client_id, query_id, std::move(f));
    }

    void process_response(td::ClientManager::Response response) {
        if (!response.object) {
            return;
        }

        if (response.request_id == 0) {
            process_update(std::move(response.object));
        } else if (auto node = m_handlers.extract(response.request_id)) {
            std::invoke(std::move(node.mapped()), std::move(response.object));
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

        td_api::int32 file_id = 0;
        td_api::downcast_call(*message.content_, overloaded{
            [&](td_api::messageVideo &m) {
                file_id = m.video_->video_->id_;
            },
            [](td_api::MessageContent &m) {
                spdlog::trace("Got message content {}, ignoring...", m.get_id());
            },
        });
        if (file_id == 0) return;

        auto req = td_api::make_object<td_api::downloadFile>();
        req->file_id_ = file_id;
        req->priority_ = 1;
        req->synchronous_ = true;
        send_query(std::move(req), [](td_api::object_ptr<td_api::Object> obj) {
            if (obj->get_id() != td_api::file::ID) return;
            auto file = td::move_tl_object_as<td_api::file>(obj);
            spdlog::info("downloaded to {}", file->local_->path_);
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
    std::uint64_t m_current_query_id{0};
    std::uint64_t m_authentication_query_id{0}; // don't know why it's needed but it exists in the official cpp example

    std::map<std::uint64_t, callback_t> m_handlers;
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

    kodibot bot(api_id, std::move(api_hash), std::move(token));
    bot.run();
    return 0;
}
