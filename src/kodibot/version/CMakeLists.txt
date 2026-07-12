find_package(Git QUIET)

if (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE KODIBOT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if (NOT GIT_DESCRIBE_RESULT EQUAL 0)
        # TODO: log the warning
        set(KODIBOT_VERSION "0.0-UNKNOWN")
    endif()
else()
    # TODO: log the warning
    set(KODIBOT_VERSION "0.0-UNKNOWN")
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
