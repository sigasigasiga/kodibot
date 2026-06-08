module;

#include <functional>
#include <map>
#include <mutex>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>

export module kodibot.telegram:client;

export import :client_interface;

import kodibot.util;

export namespace kodibot::telegram {

class client : public client_interface
{
public:
    using callback_type = std::move_only_function<void(td::td_api::object_ptr<td::td_api::Object>) &&>;
    using signal_type = util::signal<void(td::td_api::Object &)>;

public:
    class delegate
    {
    public:
        virtual ~delegate() = default;

    public:
        virtual void send_request(
            td::ClientManager::ClientId id,
            td::ClientManager::RequestId request_id,
            td::td_api::object_ptr<td::td_api::Function> f
        ) = 0;
    };

public:
    client(delegate &delegate, td::ClientManager::ClientId id);

public:
    void send_request(td::td_api::object_ptr<td::td_api::Function> f, callback_type cb);
    util::scoped_connection subscribe(signal_type::slot_function_type callback);

private: // client_interface
    void on_response(
        td::ClientManager::RequestId id,
        td::td_api::object_ptr<td::td_api::Object> object
    ) final;

    void on_update(td::td_api::object_ptr<td::td_api::Object> update) final;

private:
    delegate &m_delegate;
    td::ClientManager::ClientId m_id;

    // Guards m_request_id and m_callbacks, which are accessed both from the
    // client_manager receive loop (on_response) and from any thread calling
    // send_request (e.g. the HTTP worker thread).
    std::mutex m_mutex;
    td::ClientManager::RequestId m_request_id;
    std::map<td::ClientManager::RequestId, callback_type> m_callbacks;

    signal_type m_update_signal;
};

client::client(delegate &delegate, td::ClientManager::ClientId id)
    : m_delegate(delegate)
    , m_id(id)
    , m_request_id(0)
{
}

void client::send_request(td::td_api::object_ptr<td::td_api::Function> f, callback_type cb) {
    td::ClientManager::RequestId req_id;
    {
        std::lock_guard lock(m_mutex);
        req_id = ++m_request_id;
        if (cb) {
            m_callbacks.emplace(req_id, std::move(cb));
        }
    }

    m_delegate.send_request(m_id, req_id, std::move(f));
}

util::scoped_connection client::subscribe(signal_type::slot_function_type callback) {
    return m_update_signal.connect(std::move(callback));
}

// client_interface
void client::on_response(
    td::ClientManager::RequestId id,
    td::td_api::object_ptr<td::td_api::Object> object
) {
    // Extract the callback under the lock, but invoke it without holding the
    // mutex: the callback may itself call send_request (re-entrant locking).
    callback_type cb;
    {
        std::lock_guard lock(m_mutex);
        if (auto node = m_callbacks.extract(id)) {
            cb = std::move(node.mapped());
        }
    }

    if (cb) {
        std::invoke(std::move(cb), std::move(object));
    }
}

void client::on_update(td::td_api::object_ptr<td::td_api::Object> update) {
    m_update_signal(*update);
}

} // namespace kodibot::telegram
