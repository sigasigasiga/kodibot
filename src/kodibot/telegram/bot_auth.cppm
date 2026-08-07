module;

#include <expected>
#include <functional>

#include <td/telegram/Client.h>
#include <spdlog/spdlog.h>

export module kodibot.telegram:bot_auth;

import :client;
import :downcast;

export namespace kodibot::telegram {

class bot_auth
{
public:
    using callback_type = std::move_only_function<void(client &, td::td_api::object_ptr<td::td_api::error>) &&>;

public:
    bot_auth(
        client &client,
        td::td_api::object_ptr<td::td_api::setTdlibParameters> auth,
        std::string bot_token
    );

public:
    void start(callback_type cb);

private:
    void on_update(const td::td_api::Object &update);

    auto make_auth_handler() {
        return [this, id = ++m_authentication_query_id](td::td_api::object_ptr<td::td_api::Object> object) {
            if (id == m_authentication_query_id && object->get_id() == td::td_api::error::ID) {
                std::invoke(std::move(m_callback), m_client, td::move_tl_object_as<td::td_api::error>(object));
            }
        };
    }

private:
    client &m_client;
    td::td_api::object_ptr<td::td_api::setTdlibParameters> m_auth;
    std::string m_bot_token;
    util::scoped_connection m_connection;
    std::uint64_t m_authentication_query_id; // don't know why it's needed but it exists in the official cpp example

    callback_type m_callback;
};

bot_auth::bot_auth(
    client &client,
    td::td_api::object_ptr<td::td_api::setTdlibParameters> auth,
    std::string bot_token
)
    : m_client(client)
    , m_auth(std::move(auth))
    , m_bot_token(std::move(bot_token))
    , m_connection(client.subscribe(std::bind_front(&bot_auth::on_update, this)))
    , m_authentication_query_id(0)
{
}

void bot_auth::start(callback_type cb) {
    spdlog::info("Starting Telegram bot authentication");
    m_callback = std::move(cb);
    m_client.send_request(td::td_api::make_object<td::td_api::getOption>("version"), nullptr);
}

void bot_auth::on_update(const td::td_api::Object &update) {
    if (update.get_id() != td::td_api::updateAuthorizationState::ID) {
        return;
    }

    auto &auth_state = static_cast<const td::td_api::updateAuthorizationState &>(update);

    (downcast_call)(
        *auth_state.authorization_state_,
        grace::fn::bind::overload{
            [this](const td::td_api::authorizationStateReady &) {
                spdlog::trace("Authorization state: READY");
                m_client.post(std::bind_front(std::move(m_callback), std::ref(m_client), nullptr));
            },
            [](const td::td_api::authorizationStateLoggingOut &) {
                spdlog::trace("Authorization state: LOGGING_OUT");
            },
            [](const td::td_api::authorizationStateClosing &) {
                spdlog::trace("Authorization state: CLOSING");
            },
            [](const td::td_api::authorizationStateClosed &) {
                spdlog::trace("Authorization state: CLOSED");
            },
            [this](const td::td_api::authorizationStateWaitPhoneNumber &) {
                spdlog::trace("Authorization state: WAIT_PHONE_NUMBER");
                m_client.send_request(
                    td::td_api::make_object<td::td_api::checkAuthenticationBotToken>(m_bot_token),
                    make_auth_handler()
                );
            },
            [this](const td::td_api::authorizationStateWaitTdlibParameters &) {
                spdlog::trace("Authorization state: WAIT_TDLIB_PARAMETERS");
                m_client.send_request(std::move(m_auth), make_auth_handler());
            },
            [](const td::td_api::AuthorizationState &upd) {
                spdlog::warn("Unexpected authorization state: {}", upd.get_id());
            },
        });
}

} // namespace kodibot::telegram
