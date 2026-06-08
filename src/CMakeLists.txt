add_subdirectory(kodibot)

add_executable(kodibot.x main.cpp)

target_link_libraries(
    kodibot.x

    PRIVATE

    Td::TdStatic
    spdlog::spdlog
    httplib::httplib
    Boost::headers

    kodibot.telegram
    kodibot.util
)
