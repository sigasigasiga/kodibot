add_library(kodibot.telegram)

target_sources(
    kodibot.telegram

    PUBLIC

    FILE_SET
    CXX_MODULES FILES

    bot_auth.cppm
    client.cppm
    client_interface.cppm
    client_manager.cppm

    module.cppm
)

target_link_libraries(
    kodibot.telegram

    PRIVATE

    Td::TdStatic
    spdlog::spdlog

    kodibot.util
)
