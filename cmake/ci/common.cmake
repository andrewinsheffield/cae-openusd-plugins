# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Shared helpers for CMake script-mode CI drivers.
#
# These scripts are meant to replace ad hoc shell/PowerShell orchestration with
# a small cross-platform CMake dialect. They intentionally do not define build
# policy themselves; each entry point decides which environment variables it
# accepts and which CMake targets it runs.
#
# Conventions:
#   - Environment variables are the CI matrix handoff.
#   - CMake cache variables are the build-system handoff.
#   - Commands are echoed before execution so GitLab logs remain debuggable.
#   - Failures are fatal immediately; downstream stages should rely on artifacts
#     from completed stages rather than partial local state.
#
function(cae_ci_repo_root out_var)
    # Resolve from cmake/ci/common.cmake back to the repository root.
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set("${out_var}" "${_root}" PARENT_SCOPE)
endfunction()

function(cae_ci_get_env out_var name default_value)
    # Read one matrix/env value with a default. Empty environment values are
    # treated as unset so CI can omit optional dimensions cleanly.
    if(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
        set("${out_var}" "$ENV{${name}}" PARENT_SCOPE)
    else()
        set("${out_var}" "${default_value}" PARENT_SCOPE)
    endif()
endfunction()

function(cae_ci_append_env_option list_var env_name cmake_name)
    # Forward an environment value as a -D cache option when present.
    # Example:
    #   CAE_OPENUSD_TAG=v25.11 ->
    #   -DCAE_SUPERBUILD_OPENUSD_TAG=v25.11
    if(DEFINED ENV{${env_name}} AND NOT "$ENV{${env_name}}" STREQUAL "")
        set(_options "${${list_var}}")
        list(APPEND _options "-D${cmake_name}=$ENV{${env_name}}")
        set("${list_var}" "${_options}" PARENT_SCOPE)
    endif()
endfunction()

function(_cae_ci_prepend_path_env out_var env_name)
    set(_entries ${ARGN})
    if(NOT _entries)
        set("${out_var}" "" PARENT_SCOPE)
        return()
    endif()

    cmake_path(CONVERT "${_entries}" TO_NATIVE_PATH_LIST _prefix)
    if(CMAKE_HOST_WIN32)
        set(_separator "\;")
    else()
        set(_separator ":")
    endif()

    if(DEFINED ENV{${env_name}} AND NOT "$ENV{${env_name}}" STREQUAL "")
        set(_value "${_prefix}${_separator}$ENV{${env_name}}")
    else()
        set(_value "${_prefix}")
    endif()
    if(CMAKE_HOST_WIN32)
        # Windows path-list values use semicolons, which are also CMake's list
        # separator. Escape them so `cmake -E env PATH=a;b ...` remains one
        # command argument instead of making `b` the executable.
        string(REPLACE ";" "\\;" _value "${_value}")
    endif()
    set("${out_var}" "${env_name}=${_value}" PARENT_SCOPE)
endfunction()

function(cae_ci_run label)
    # Execute a native command with a readable label. Existing calls may pass the
    # command directly after the label. New calls can add runtime path options:
    #
    #   cae_ci_run("Run pytest"
    #       RUNTIME_PYTHONPATH /path/to/usd/lib/python
    #       RUNTIME_PATH /path/to/usd/bin /path/to/usd/lib
    #       COMMAND python -m pytest ...)
    #
    # RUNTIME_PATH is prepended to PATH on every platform. On Unix-like hosts it
    # is also prepended to the native library search variable, which is what the
    # Packman OpenUSD Python modules need when imported from a plain venv.
    set(_runtime_pythonpath)
    set(_runtime_path)
    set(_command)
    set(_mode COMMAND)

    foreach(_arg IN LISTS ARGN)
        if(_arg STREQUAL "RUNTIME_PYTHONPATH")
            set(_mode RUNTIME_PYTHONPATH)
        elseif(_arg STREQUAL "RUNTIME_PATH")
            set(_mode RUNTIME_PATH)
        elseif(_arg STREQUAL "COMMAND")
            set(_mode COMMAND)
        elseif(_mode STREQUAL "RUNTIME_PYTHONPATH")
            list(APPEND _runtime_pythonpath "${_arg}")
        elseif(_mode STREQUAL "RUNTIME_PATH")
            list(APPEND _runtime_path "${_arg}")
        else()
            list(APPEND _command "${_arg}")
        endif()
    endforeach()

    if(NOT _command)
        message(FATAL_ERROR "${label} has no command")
    endif()

    set(_has_env FALSE)
    _cae_ci_prepend_path_env(_pythonpath_env PYTHONPATH ${_runtime_pythonpath})
    if(_pythonpath_env)
        set(_has_env TRUE)
    endif()

    _cae_ci_prepend_path_env(_path_env PATH ${_runtime_path})
    if(_path_env)
        set(_has_env TRUE)
    endif()

    set(_library_path_env)
    if(_runtime_path AND NOT CMAKE_HOST_WIN32)
        if(APPLE)
            set(_library_env_name DYLD_LIBRARY_PATH)
        else()
            set(_library_env_name LD_LIBRARY_PATH)
        endif()
        _cae_ci_prepend_path_env(_library_path_env ${_library_env_name} ${_runtime_path})
        set(_has_env TRUE)
    endif()

    if(_has_env)
        set(_execute_command "${CMAKE_COMMAND}" -E env)
        # Environment values such as Windows PATH contain semicolons. Append
        # each assignment as a quoted list element directly; storing these in
        # another list and iterating it would split the value before
        # `cmake -E env` sees it.
        if(_pythonpath_env)
            list(APPEND _execute_command "${_pythonpath_env}")
        endif()
        if(_path_env)
            list(APPEND _execute_command "${_path_env}")
        endif()
        if(_library_path_env)
            list(APPEND _execute_command "${_library_path_env}")
        endif()
        foreach(_command_entry IN LISTS _command)
            list(APPEND _execute_command "${_command_entry}")
        endforeach()
    else()
        set(_execute_command ${_command})
    endif()

    message(STATUS "${label}")
    message(STATUS "+ ${_execute_command}")
    execute_process(
        COMMAND ${_execute_command}
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${label} failed with exit code ${_result}")
    endif()
endfunction()

function(cae_ci_split_words out_var value)
    if(value)
        separate_arguments(_values NATIVE_COMMAND "${value}")
    else()
        set(_values)
    endif()
    set("${out_var}" "${_values}" PARENT_SCOPE)
endfunction()

function(cae_ci_native_path_list out_var)
    set(_entries)
    foreach(_entry IN LISTS ARGN)
        if(_entry)
            cmake_path(NORMAL_PATH _entry OUTPUT_VARIABLE _normalized)
            list(APPEND _entries "${_normalized}")
        endif()
    endforeach()

    if(_entries)
        cmake_path(CONVERT "${_entries}" TO_NATIVE_PATH_LIST _joined)
    else()
        set(_joined "")
    endif()
    set("${out_var}" "${_joined}" PARENT_SCOPE)
endfunction()

function(cae_ci_find_one_file out_var directory pattern)
    file(GLOB _matches "${directory}/${pattern}")
    list(LENGTH _matches _match_count)
    if(NOT _match_count EQUAL 1)
        message(FATAL_ERROR
            "Expected one file matching '${pattern}' under '${directory}', "
            "found ${_match_count}: ${_matches}")
    endif()
    list(GET _matches 0 _match)
    set("${out_var}" "${_match}" PARENT_SCOPE)
endfunction()

function(cae_ci_venv_python out_var venv_dir)
    if(CMAKE_HOST_WIN32)
        set(_python "${venv_dir}/Scripts/python.exe")
    else()
        set(_python "${venv_dir}/bin/python")
    endif()
    set("${out_var}" "${_python}" PARENT_SCOPE)
endfunction()
