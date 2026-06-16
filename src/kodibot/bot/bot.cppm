module;

#include <cassert>
#include <functional>
#include <unordered_set>

#include <td/telegram/td_api.hpp>
#include <spdlog/spdlog.h>

export module kodibot.bot:bot;

import grace;

import kodibot.kodi;
import kodibot.telegram;
import kodibot.util;

export namespace kodibot::bot {

class bot
{
public:
    class hoster
    {
    public:
        virtual ~hoster() = default;

    public:
        virtual std::string host_video(
            td::td_api::int32 file_id,
            td::td_api::int53 size,
            std::string mime_type,
            bool supports_streaming
        ) = 0;
    };

    class player
    {
    public:
        virtual ~player() = default;

    public:
        virtual void play(std::string url) = 0;
    };

public:
    bot(
        hoster &hoster,
        player &player,
        telegram::client &client,
        std::unordered_set<td::td_api::int53> whitelist
    );

private: // update handlers
    void on_update(td::td_api::Object &update);
    void on_new_message(td::td_api::message &message);

private:
    void process_video(td::td_api::video &video);

private:
    hoster &m_hoster;
    player &m_player;
    telegram::client &m_client;
    util::scoped_connection m_update_connection;
    std::unordered_set<td::td_api::int53> m_whitelist;
    td::td_api::int32 m_start_time;
};

bot::bot(
    hoster &hoster,
    player &player,
    telegram::client &client,
    std::unordered_set<td::td_api::int53> whitelist
)
    : m_hoster(hoster)
    , m_player(player)
    , m_client(client)
    , m_update_connection(m_client.subscribe(std::bind_front(&bot::on_update, this)))
    , m_whitelist(std::move(whitelist))
    , m_start_time(static_cast<td::td_api::int32>(std::time(nullptr)))
{
#ifndef NDEBUG
    m_client.send_request(
        td::td_api::make_object<td::td_api::getAuthorizationState>(),
        [](td::td_api::object_ptr<td::td_api::Object> o) {
            assert(o->get_id() == td::td_api::authorizationStateReady::ID);
        }
    );
#endif // NDEBUG
}

void bot::on_update(td::td_api::Object &update) {
    td::td_api::downcast_call(update, grace::fn::bind::overload{
        [this](td::td_api::updateNewMessage &message) { on_new_message(*message.message_); },
        [](td::td_api::Object &upd) { spdlog::trace("Unhandled update: {}", upd.get_id()); }
    });
}

void bot::on_new_message(td::td_api::message &message) {
    if (message.is_outgoing_) {
        return;
    }

    if (message.sender_id_->get_id() != td::td_api::messageSenderUser::ID) {
    }

    if (auto sender = telegram::downcast<td::td_api::messageSenderUser *>(message.sender_id_.get())) {
        auto const id = sender->user_id_;
        if (!m_whitelist.contains(id)) {
            spdlog::warn(
                "Dropping message from non-whitelisted sender (user_id={}, chat_id={})",
                id,
                message.chat_id_
            );
            return;
        }
    } else {
        spdlog::trace("Got message from something other than a user. Skipping.");
        return;
    }

    if (message.date_ < m_start_time) {
        spdlog::debug("Dropping message sent while offline (date={}, started={})", message.date_, m_start_time);
        return;
    }

    td::td_api::downcast_call(*message.content_, grace::fn::bind::overload{
        [this](td::td_api::messageVideo &m) {
            if (m.video_) {
                process_video(*m.video_);
            }
        },
        [](td::td_api::MessageContent &m) {
            spdlog::trace("Got message content {}, ignoring...", m.get_id());
        },
    });
}

void bot::process_video(td::td_api::video &video) {
    auto size = video.video_->size_;
    if (size == 0) {
        size = video.video_->expected_size_;
    }

    auto url = m_hoster.host_video(
        video.video_->id_,
        size,
        video.mime_type_.empty() ? std::string{"video/mp4"} : video.mime_type_,
        video.supports_streaming_
    );

    m_player.play(std::move(url));
}

} // namespace kodibot::bot;
