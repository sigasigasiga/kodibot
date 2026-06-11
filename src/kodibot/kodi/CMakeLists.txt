add_library(kodibot.kodi)

target_sources(
    kodibot.kodi

    PUBLIC

    FILE_SET
    CXX_MODULES FILES

    client.cppm

    module.cppm
)

target_link_libraries(
    kodibot.kodi

    PRIVATE

    spdlog::spdlog
    httplib::httplib
)
