add_library(kodibot.os)

target_sources(
    kodibot.os

    PUBLIC

    FILE_SET
    CXX_MODULES FILES

    eintr.cppm
    signal_monitor.cppm
    unique_fd.cppm

    module.cppm
)
