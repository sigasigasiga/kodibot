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
        std::unordered_set<td::td_api::int53> whitelist,
        td::td_api::int64 authorization_date
    );

private: // update handlers
    void on_update(const td::td_api::Object &update);
    void on_new_message(const td::td_api::message &message);

private:
    void process_video(const td::td_api::video &video);

private:
    hoster &m_hoster;
    player &m_player;
    telegram::client &m_client;
    util::scoped_connection m_update_connection;
    std::unordered_set<td::td_api::int53> m_whitelist;
    td::td_api::int64 m_start_time;
};

bot::bot(
    hoster &hoster,
    player &player,
    telegram::client &client,
    std::unordered_set<td::td_api::int53> whitelist,
    td::td_api::int64 authorization_date
)
    : m_hoster(hoster)
    , m_player(player)
    , m_client(client)
    , m_update_connection(m_client.subscribe(std::bind_front(&bot::on_update, this)))
    , m_whitelist(std::move(whitelist))
    , m_start_time(authorization_date)
{
    // what thE FUCK????
    // does tdlib really use a FUCKING SIGNED 32-BIT INTEGER for the FUCKING TIMESTAMP???????
    if constexpr (std::same_as<decltype(td::td_api::message::date_), td::td_api::int32>) {
        if (m_start_time > std::numeric_limits<td::td_api::int32>::max()) {
            spdlog::warn(
                "TDLib is prone to the Year 2038 problem, and the authorization date ({}) is "
                "too large to be represented as a signed 32-bit integer",
                m_start_time
            );

            spdlog::warn(
                "Setting the start time to 0 (epoch) to avoid dropping messages sent before "
                "the bot started. This may result in a flood of messages being processed."
            );

            m_start_time = 0;
        }
    }

#ifndef NDEBUG
    m_client.send_request(
        td::td_api::make_object<td::td_api::getAuthorizationState>(),
        [](td::td_api::object_ptr<td::td_api::Object> o) {
            assert(o->get_id() == td::td_api::authorizationStateReady::ID);
        }
    );
#endif // NDEBUG
}

void bot::on_update(const td::td_api::Object &update) {
    telegram::downcast_call(update, grace::fn::bind::overload{
        [this](const td::td_api::updateNewMessage &message) { on_new_message(*message.message_); },
        [](const td::td_api::Object &upd) { spdlog::trace("Unhandled update: {}", upd.get_id()); }
    });
}

void bot::on_new_message(const td::td_api::message &message) {
    if (message.is_outgoing_) {
        spdlog::trace("Ignoring outgoing message (chat_id={})", message.chat_id_);
        return;
    }

    auto sender = telegram::downcast<const td::td_api::messageSenderUser *>(message.sender_id_.get());
    if (!sender) {
        return spdlog::debug("Got message from non-user sender (sender_id={}). Skipping.", message.sender_id_->get_id());
    }

    const auto id = sender->user_id_;
    if (!m_whitelist.contains(id)) {
        return spdlog::warn(
            "Dropping message from non-whitelisted sender (user_id={}, chat_id={})",
            id,
            message.chat_id_
        );
    }

    if (static_cast<td::td_api::int64>(message.date_) < m_start_time) {
        return spdlog::debug("Dropping message sent while offline (date={}, started={})", message.date_, m_start_time);
    }

    spdlog::debug("Processing message from whitelisted user (user_id={}, chat_id={})...", id, message.chat_id_);

    telegram::downcast_call(*message.content_, grace::fn::bind::overload{
        [this](const td::td_api::messageVideo &m) {
            if (m.video_) {
                process_video(*m.video_);
            } else {
                spdlog::debug("Video message with null video object received");
            }
        },
        [](const td::td_api::MessageContent &m) {
            spdlog::trace("Got message content {}, ignoring...", m.get_id());
        },
    });
}

void bot::process_video(const td::td_api::video &video) {
    if (!video.video_) {
        spdlog::error("Video message with null video file object received");
        return;
    }

    const auto &file = *video.video_;
    auto size = file.size_;
    if (size == 0) {
        spdlog::debug("Video size is 0, using expected_size={}", file.expected_size_);
        size = file.expected_size_;
    }

    spdlog::info(
        "Processing video: file_id={}, size={}, mime_type={}, supports_streaming={}",
        file.id_,
        size,
        video.mime_type_,
        video.supports_streaming_
    );

    auto url = m_hoster.host_video(
        file.id_,
        size,
        video.mime_type_.empty() ? std::string{"video/mp4"} : video.mime_type_,
        video.supports_streaming_
    );

    m_player.play(std::move(url));
}

} // namespace kodibot::bot;
