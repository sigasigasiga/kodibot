module;

#include <td/telegram/Client.h>
#include <td/telegram/td_api.hpp>

export module kodibot.telegram:receiver;

export namespace kodibot::telegram {

class receiver
{
public:
    virtual ~receiver() = default;

public:
    virtual void on_response(
        td::ClientManager::RequestId id,
        td::td_api::object_ptr<td::td_api::Object> object
    ) = 0;

    virtual void on_update(td::td_api::object_ptr<td::td_api::Object> update) = 0;
};

} // namespace kodibot::telegram
