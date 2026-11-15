# Git has to run inside the submodule: from the parent repository it silently skips every path
# that belongs to a submodule and reports success.
function(toffee_apply_patch directory patch)
    find_package(Git REQUIRED)
    get_filename_component(_patch_name "${patch}" NAME)
    if(NOT EXISTS "${directory}/.git")
        message(FATAL_ERROR
            "${directory} is not a git checkout, so ${_patch_name} cannot be applied. "
            "Initialise it with: git submodule update --init --recursive")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-space-change "${patch}"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE _already
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_already EQUAL 0)
        message(STATUS "${_patch_name} is already applied")
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check --ignore-space-change "${patch}"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE _check
        ERROR_VARIABLE _error
    )
    if(NOT _check EQUAL 0)
        message(FATAL_ERROR
            "Cannot apply ${_patch_name}. Check the pinned submodule revision and local edits.\n${_error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-space-change "${patch}"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE _result
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${_patch_name}:\n${_error}")
    endif()
    message(STATUS "Applied ${_patch_name}")
endfunction()
