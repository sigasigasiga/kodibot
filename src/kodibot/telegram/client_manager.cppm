module;

#include <cassert>
#include <map>
#include <stop_token>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>

export module kodibot.telegram:client_manager;

import :client;

export namespace kodibot::telegram {

class client_manager : private client::delegate
{
public:
    client &make_client();

    // Drives the receive loop until the stop token is triggered. receive() uses
    // a short timeout so a stop request is observed promptly.
    void run(std::stop_token token);

private: // client::delegate
    void send_request(
        td::ClientManager::ClientId id,
        td::ClientManager::RequestId request_id,
        td::td_api::object_ptr<td::td_api::Function> f
    ) final;

private:
    td::ClientManager m_client_manager;

    std::map<td::ClientManager::ClientId, std::unique_ptr<receiver>> m_clients;
};

client &client_manager::make_client() {
    auto const id = m_client_manager.create_client_id();

    auto c = std::make_unique<client>(
        static_cast<client::delegate &>(*this),
        id // FIXME: we store the `id` two times: in the `map` and inside the `client`
    );

    auto &c_ref = *c;

    m_clients.emplace(id, std::move(c));

    return c_ref;
}

void client_manager::run(std::stop_token token) {
    while (!token.stop_requested()) {
        auto resp = m_client_manager.receive(1);
        if (!resp.object) {
            continue;
        }

        auto it = m_clients.find(resp.client_id);
        if (it == m_clients.end()) {
            assert(false);
            continue;
        }

        if (resp.request_id == 0) {
            it->second->on_update(std::move(resp).object);
        } else {
            it->second->on_response(resp.request_id, std::move(resp).object);
        }
    }
}

// client::delegate
void client_manager::send_request(
    td::ClientManager::ClientId id,
    td::ClientManager::RequestId request_id,
    td::td_api::object_ptr<td::td_api::Function> f
) {
    m_client_manager.send(id, request_id, std::move(f));
}

} // namespace kodibot::telegram
