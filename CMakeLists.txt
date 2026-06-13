cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(kodibot
  VERSION 0.1.0
  DESCRIPTION "Telegram echo bot built on TDLib"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TDLib is vendored as a git submodule under third_party/tdlib.
add_subdirectory(third_party/tdlib EXCLUDE_FROM_ALL)

add_subdirectory(third_party/cpp-httplib EXCLUDE_FROM_ALL)

find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)

add_subdirectory(src)
