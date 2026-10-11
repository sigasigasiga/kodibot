module;

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <mutex>
#include <ranges>
#include <stop_token>
#include <vector>

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

    // NB: `cancel()` invokes the callbacks of the pending requests, and those may call back
    // into the client manager, so it must not be called under `m_clients_mutex`.
    // The pointers stay valid after the unlock because a client cannot be deleted
    std::vector<receiver *> clients;
    {
        auto _ = std::scoped_lock(m_clients_mutex);
        clients = m_clients
            | std::views::values
            | std::views::transform([](const auto &c) { return c.get(); })
            | std::ranges::to<std::vector>();

        // funny way to wake up the event loop :)
        // NB: the request goes to an existing client: creating a new one just for that would start
        // a TDLib instance right before `td::ClientManager` gets destroyed, and TDLib sometimes aborts
        // in its destructor when that happens while another instance is still being initialized.
        // Nobody waits for this request id, so the response is ignored
        if (!m_clients.empty()) {
            send_request(
                m_clients.begin()->first,
                std::numeric_limits<td::ClientManager::RequestId>::max(),
                td::td_api::make_object<td::td_api::testCallEmpty>()
            );
        }
    }

    std::ranges::for_each(clients, &receiver::cancel);
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
