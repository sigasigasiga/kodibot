find_package(Git QUIET)

if (DEFINED KODIBOT_VERSION)
    message(STATUS "Using predefined KODIBOT_VERSION: ${KODIBOT_VERSION}")
elseif (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE KODIBOT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if (NOT GIT_DESCRIBE_RESULT EQUAL 0)
        message(WARNING "`git describe` command failed with exit code ${GIT_DESCRIBE_RESULT}")
    endif()
else()
    message(WARNING "Unable to determine version number")
endif()

if (NOT KODIBOT_VERSION)
    message(WARNING "Version number is not available. Using default version: v0.0.0-UNKNOWN")
    set(KODIBOT_VERSION "v0.0.0-UNKNOWN")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.cppm.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.cppm"
    @ONLY
)

add_library(kodibot.version)

target_sources(
    kodibot.version

    PUBLIC

    FILE_SET

    CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    FILES

    "${CMAKE_CURRENT_BINARY_DIR}/version.cppm"

    module.cppm
)
