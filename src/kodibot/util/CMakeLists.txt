add_library(kodibot.util)

target_sources(
    kodibot.util

    PUBLIC

    FILE_SET
    CXX_MODULES FILES

    functional.cppm
    signal.cppm

    module.cppm
)

target_link_libraries(
    kodibot.util

    PRIVATE

    Boost::headers
)
