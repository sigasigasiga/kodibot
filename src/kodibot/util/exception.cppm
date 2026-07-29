module;

#include <stdexcept>

#include <spdlog/spdlog.h>

export module kodibot.util:exception;

export namespace kodibot::util {

void log_exception(std::string_view prefix) {
    // TODO: handle nested exceptions
    try {
        throw;
    } catch(const std::exception &ex) {
        spdlog::error("{}: {}", prefix, ex.what());
    } catch(...) {
        spdlog::error("{}: {}", prefix, "unknown exception");
    }
}

} // namespace kodibot::util
