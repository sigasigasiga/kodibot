cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(kodibot
  VERSION 0.1.0
  DESCRIPTION "Telegram echo bot built on TDLib"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TDLib is vendored as a git submodule under third_party/tdlib.
add_subdirectory(third_party/tdlib EXCLUDE_FROM_ALL)

find_package(spdlog REQUIRED)
find_package(httplib REQUIRED)

add_executable(kodibot src/main.cpp)
target_link_libraries(kodibot PRIVATE Td::TdStatic spdlog::spdlog httplib::httplib)
target_compile_features(kodibot PRIVATE cxx_std_26)
