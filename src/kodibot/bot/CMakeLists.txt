add_library(kodibot.bot)

target_sources(
    kodibot.bot

    PUBLIC

    FILE_SET
    CXX_MODULES FILES

    bot.cppm

    module.cppm
)

target_link_libraries(
    kodibot.bot

    PRIVATE

    httplib::httplib
    Td::TdStatic
    spdlog::spdlog

    kodibot.kodi
    kodibot.telegram
    kodibot.util
)
