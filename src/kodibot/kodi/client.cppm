module;

#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <httplib.h>
#include <spdlog/spdlog.h>

export module kodibot.kodi:client;

namespace kodibot::kodi {

std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += spdlog::fmt_lib::format("\\u{:04x}", static_cast<unsigned char>(c));
            } else {
                out += c;
            }
        }
    }
    return out;
}

} // namespace kodibot::kodi

export namespace kodibot::kodi {

struct connection
{
    std::string host;
    int port{8080};
    std::string username;
    std::string password;
};

class client
{
public:
    explicit client(connection conn);

    std::expected<void, std::string> play(std::string_view url) const;

private:
    connection m_conn;
};

client::client(connection conn)
    : m_conn(std::move(conn))
{
}

std::expected<void, std::string> client::play(std::string_view url) const {
    const std::string body = spdlog::fmt_lib::format(
        R"({{"jsonrpc":"2.0","id":1,"method":"Player.Open","params":{{"item":{{"file":"{}"}}}}}})",
        json_escape(url));

    spdlog::trace("Kodi request body: {}", body);

    httplib::Client http(m_conn.host, m_conn.port);
    if (!m_conn.username.empty()) {
        http.set_basic_auth(m_conn.username, m_conn.password);
    }

    spdlog::trace("Connecting to Kodi at {}:{}", m_conn.host, m_conn.port);
    auto res = http.Post("/jsonrpc", body, "application/json");
    if (!res) {
        return std::unexpected(spdlog::fmt_lib::format(
            "no response from Kodi at {}:{} ({})",
            m_conn.host, m_conn.port, httplib::to_string(res.error())));
    }

    spdlog::trace("Kodi HTTP response: {} - {}", res->status, res->body);

    if (res->status != 200) {
        return std::unexpected(spdlog::fmt_lib::format(
            "Kodi returned HTTP {}: {}", res->status, res->body));
    }
    if (res->body.find("\"error\"") != std::string::npos) {
        return std::unexpected(spdlog::fmt_lib::format("Kodi JSON-RPC error: {}", res->body));
    }
    return {};
}

} // namespace kodibot::kodi
