cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(
    kodibot

    VERSION 0.1.0
    LANGUAGES CXX
)

# Generate a .gitignore in the build directory to ignore all its contents
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  file(WRITE "${CMAKE_BINARY_DIR}/.gitignore" "*\n")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Td CONFIG QUIET)
if (Td_FOUND)
    message(STATUS "Using installed Td package")
else()
    message(STATUS "Td package not found — building bundled third_party/tdlib")
    block(SCOPE_FOR VARIABLES)
        # I don't get why they didn't specify C++ standard in thier own `CMakeLists.txt`
        set(CMAKE_CXX_STANDARD 17)
        add_subdirectory(third_party/tdlib EXCLUDE_FROM_ALL)
    endblock()
endif()

add_subdirectory(third_party/cpp-httplib EXCLUDE_FROM_ALL)
add_subdirectory(third_party/grace EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

add_subdirectory(src)
