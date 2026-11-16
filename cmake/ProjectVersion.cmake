set(TOFFEE_VERSION "" CACHE STRING "Release version such as v1.2.3 or v1.2.3-rc1 (default: newest tag + -dev)")

if(TOFFEE_VERSION)
    string(REGEX REPLACE "^v" "" TOFFEE_VERSION_STRING "${TOFFEE_VERSION}")
else()
    find_package(Git QUIET)
    set(_tag "")
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "v[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _tag
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    string(REGEX REPLACE "^v" "" _tag "${_tag}")
    string(REGEX REPLACE "[-+].*$" "" _tag "${_tag}")
    if(NOT _tag)
        set(_tag "0.0.0")
    endif()
    set(TOFFEE_VERSION_STRING "${_tag}-dev")
endif()

if(NOT TOFFEE_VERSION_STRING MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)([-+][0-9A-Za-z.+-]+)?$")
    message(FATAL_ERROR "TOFFEE_VERSION '${TOFFEE_VERSION}' must look like v1.2.3 or v1.2.3-rc1.")
endif()
set(TOFFEE_PROJECT_VERSION "${CMAKE_MATCH_1}")
