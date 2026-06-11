module;

#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <httplib.h>
#include <spdlog/spdlog.h>

export module kodibot.kodi:client;

namespace kodibot::kodi {

// Escapes `in` so it can be embedded as a JSON string value.
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

// Connection parameters for a Kodi instance exposing the JSON-RPC HTTP
// interface (Settings -> Services -> Control -> "Allow remote control via
// HTTP"). username/password are optional and only used when username is set.
struct connection
{
    std::string host;
    int port{8080};
    std::string username;
    std::string password;
};

// Minimal Kodi JSON-RPC client. A new HTTP connection is opened per request, so
// instances are safe to use from any thread.
class client
{
public:
    explicit client(connection conn);

    // Asks Kodi to start playing the media at `url` via Player.Open. Blocking
    // HTTP call; returns an error description on failure.
    std::expected<void, std::string> play(const std::string &url);

private:
    connection m_conn;
};

client::client(connection conn)
    : m_conn(std::move(conn))
{
}

std::expected<void, std::string> client::play(const std::string &url) {
    const std::string body = spdlog::fmt_lib::format(
        R"({{"jsonrpc":"2.0","id":1,"method":"Player.Open","params":{{"item":{{"file":"{}"}}}}}})",
        json_escape(url));

    httplib::Client http(m_conn.host, m_conn.port);
    if (!m_conn.username.empty()) {
        http.set_basic_auth(m_conn.username, m_conn.password);
    }

    auto res = http.Post("/jsonrpc", body, "application/json");
    if (!res) {
        return std::unexpected(spdlog::fmt_lib::format(
            "no response from Kodi at {}:{} ({})",
            m_conn.host, m_conn.port, httplib::to_string(res.error())));
    }
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
