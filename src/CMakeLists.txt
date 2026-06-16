add_subdirectory(kodibot)

add_executable(kodibot.x main.cpp)

target_link_libraries(
    kodibot.x

    PRIVATE

    Td::TdStatic
    spdlog::spdlog
    httplib::httplib
    Boost::headers
    Boost::program_options

    kodibot.bot
    kodibot.kodi
    kodibot.telegram
    kodibot.util
)
