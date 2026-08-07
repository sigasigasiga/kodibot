module;

#include <algorithm>
#include <cassert>
#include <map>
#include <mutex>
#include <stop_token>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>

#include <spdlog/spdlog.h>

export module kodibot.telegram:client_manager;

import :client;

export namespace kodibot::telegram {

class client_manager : private client::delegate
{
public:
    client &make_client();

    void run();
    void stop();

private: // client::delegate
    void send_request(
        td::ClientManager::ClientId id,
        td::ClientManager::RequestId request_id,
        td::td_api::object_ptr<td::td_api::Function> f
    ) final;

private:
    td::ClientManager m_client_manager;
    std::stop_source m_stop;

    std::mutex m_clients_mutex;
    std::map<td::ClientManager::ClientId, std::unique_ptr<receiver>> m_clients;
};

client &client_manager::make_client() {
    auto const id = m_client_manager.create_client_id();

    auto c = std::make_unique<client>(
        static_cast<client::delegate &>(*this),
        id // FIXME: we store the `id` two times: in the `map` and inside the `client`
    );

    auto &c_ref = *c;

    {
        std::lock_guard lock(m_clients_mutex);
        m_clients.emplace(id, std::move(c));
    }

    return c_ref;
}

void client_manager::run() {
    spdlog::debug("TDLib event loop started");
    while (!m_stop.stop_requested()) {
        auto resp = m_client_manager.receive(1);
        if (!resp.object) {
            continue;
        }

        receiver *recv = nullptr;
        {
            std::lock_guard lock(m_clients_mutex);
            auto it = m_clients.find(resp.client_id);
            if (it == m_clients.end()) {
                spdlog::error("Received event for unknown client_id: {}", resp.client_id);
                assert(false);
                continue;
            }

            // `recv` will be valid because a client cannot be deleted
            recv = it->second.get();
        }

        if (resp.request_id == 0) {
            recv->on_update(std::move(resp).object);
        } else {
            recv->on_response(resp.request_id, std::move(resp).object);
        }
    }
    spdlog::info("TDLib event loop stopped");
}

void client_manager::stop() {
    spdlog::info("Stopping TDLib client manager...");
    m_stop.request_stop();

    // funny way to wake up the event loop :)
    make_client().post([] {});

    auto _ = std::scoped_lock(m_clients_mutex);
    std::ranges::for_each(m_clients, &receiver::cancel, grace::fn::op::get_value());
}

// client::delegate
void client_manager::send_request(
    td::ClientManager::ClientId id,
    td::ClientManager::RequestId request_id,
    td::td_api::object_ptr<td::td_api::Function> f
) {
    spdlog::trace("Sending request: client_id={}, request_id={}, function_id={}", id, request_id, f->get_id());
    m_client_manager.send(id, request_id, std::move(f));
}

} // namespace kodibot::telegram
