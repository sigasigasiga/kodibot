cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(
    kodibot

    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

block(SCOPE_FOR VARIABLES)
    # I don't get why they didn't specify C++ standard in thier own `CMakeLists.txt`
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(third_party/tdlib EXCLUDE_FROM_ALL)
endblock()

add_subdirectory(third_party/cpp-httplib EXCLUDE_FROM_ALL)
add_subdirectory(third_party/grace EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

add_subdirectory(src)
