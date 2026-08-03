# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# CaeUSDToolTrampoline.cmake
#
# Script-mode launcher for USD tools that need a prepared Python/runtime
# environment. Invoke with:
#
#   cmake -DCAE_USD_TOOL_EXECUTABLE=...
#         -DCAE_USD_TOOL_PYTHON_EXECUTABLE=...
#         -DCAE_USD_TOOL_ARGS_FILE=...
#         -P CaeUSDToolTrampoline.cmake

function(_cae_require_var VAR_NAME)
    if(NOT DEFINED ${VAR_NAME} OR "${${VAR_NAME}}" STREQUAL "")
        message(FATAL_ERROR "CaeUSD tool trampoline requires ${VAR_NAME}")
    endif()
endfunction()

function(_cae_decode_list VALUE OUT)
    if("${VALUE}" STREQUAL "")
        set(${OUT} "" PARENT_SCOPE)
    else()
        string(REPLACE "|" ";" _items "${VALUE}")
        set(${OUT} ${_items} PARENT_SCOPE)
    endif()
endfunction()

function(_cae_append_existing_path OUT)
    set(_paths "${${OUT}}")
    foreach(_path IN LISTS ARGN)
        if(_path AND EXISTS "${_path}")
            list(APPEND _paths "${_path}")
        endif()
    endforeach()
    if(_paths)
        list(REMOVE_DUPLICATES _paths)
    endif()
    set(${OUT} ${_paths} PARENT_SCOPE)
endfunction()

function(_cae_join_env_path OUT)
    if(WIN32)
        set(_separator ";")
    else()
        set(_separator ":")
    endif()

    set(_paths "")
    foreach(_path IN LISTS ARGN)
        if(_path)
            list(APPEND _paths "${_path}")
        endif()
    endforeach()

    if(_paths)
        list(JOIN _paths "${_separator}" _joined)
    else()
        set(_joined "")
    endif()
    set(${OUT} "${_joined}" PARENT_SCOPE)
endfunction()

function(_cae_check_python_imports PYTHON_EXE PYTHONPATH IMPORTS OUT)
    if(NOT IMPORTS)
        set(${OUT} TRUE PARENT_SCOPE)
        return()
    endif()

    set(_check_dir "${CAE_USD_TOOL_PYTHON_TARGET}")
    if(NOT _check_dir)
        set(_check_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    file(MAKE_DIRECTORY "${_check_dir}")

    set(_check_script "${_check_dir}/cae_usd_check_imports.py")
    set(_check_code "import importlib\n")
    foreach(_import_name IN LISTS IMPORTS)
        string(APPEND _check_code "importlib.import_module('${_import_name}')\n")
    endforeach()
    file(WRITE "${_check_script}" "${_check_code}")

    if(NOT "${PYTHONPATH}" STREQUAL "")
        set(ENV{PYTHONPATH} "${PYTHONPATH}")
    endif()
    execute_process(
        COMMAND "${PYTHON_EXE}" "${_check_script}"
        RESULT_VARIABLE _import_result
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(_import_result EQUAL 0)
        set(${OUT} TRUE PARENT_SCOPE)
    else()
        set(${OUT} FALSE PARENT_SCOPE)
    endif()
endfunction()

_cae_require_var(CAE_USD_TOOL_NAME)
_cae_require_var(CAE_USD_TOOL_EXECUTABLE)
_cae_require_var(CAE_USD_TOOL_ARGS_FILE)

if(NOT EXISTS "${CAE_USD_TOOL_EXECUTABLE}")
    message(FATAL_ERROR
        "CaeUSD tool '${CAE_USD_TOOL_NAME}' does not exist: ${CAE_USD_TOOL_EXECUTABLE}")
endif()

if(NOT EXISTS "${CAE_USD_TOOL_ARGS_FILE}")
    message(FATAL_ERROR
        "CaeUSD tool '${CAE_USD_TOOL_NAME}' args file does not exist: ${CAE_USD_TOOL_ARGS_FILE}")
endif()

_cae_decode_list("${CAE_USD_TOOL_PYTHONPATH}" _tool_pythonpath)
_cae_decode_list("${CAE_USD_TOOL_LIBRARY_PATH}" _tool_library_path)
_cae_decode_list("${CAE_USD_TOOL_PATH}" _tool_path)
_cae_decode_list("${CAE_USD_TOOL_PYTHON_PACKAGES}" _tool_python_packages)
_cae_decode_list("${CAE_USD_TOOL_PYTHON_IMPORTS}" _tool_python_imports)

if(NOT DEFINED CAE_USD_TOOL_USE_PYTHON OR "${CAE_USD_TOOL_USE_PYTHON}" STREQUAL "")
    set(CAE_USD_TOOL_USE_PYTHON TRUE)
endif()

if(CAE_USD_TOOL_USE_PYTHON OR _tool_python_packages)
    _cae_require_var(CAE_USD_TOOL_PYTHON_EXECUTABLE)
    if(NOT EXISTS "${CAE_USD_TOOL_PYTHON_EXECUTABLE}")
        message(FATAL_ERROR
            "CaeUSD tool '${CAE_USD_TOOL_NAME}' Python executable does not exist: "
            "${CAE_USD_TOOL_PYTHON_EXECUTABLE}")
    endif()
endif()

if(_tool_python_packages)
    _cae_require_var(CAE_USD_TOOL_PYTHON_TARGET)
    file(MAKE_DIRECTORY "${CAE_USD_TOOL_PYTHON_TARGET}")

    set(_state_text "${CAE_USD_TOOL_PYTHON_EXECUTABLE}\n${_tool_python_packages}\n")
    string(SHA256 _expected_state "${_state_text}")
    set(_state_file "${CAE_USD_TOOL_PYTHON_TARGET}/.cae-usd-tool-pip-state")
    if(EXISTS "${_state_file}")
        file(READ "${_state_file}" _current_state)
        string(STRIP "${_current_state}" _current_state)
    else()
        set(_current_state "")
    endif()

    set(_package_pythonpath "${CAE_USD_TOOL_PYTHON_TARGET}")
    list(APPEND _package_pythonpath ${_tool_pythonpath})
    if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
        list(APPEND _package_pythonpath "$ENV{PYTHONPATH}")
    endif()
    _cae_join_env_path(_package_pythonpath_env ${_package_pythonpath})
    _cae_check_python_imports(
        "${CAE_USD_TOOL_PYTHON_EXECUTABLE}"
        "${_package_pythonpath_env}"
        "${_tool_python_imports}"
        _imports_available)

    if(NOT _imports_available OR NOT _current_state STREQUAL _expected_state)
        message(STATUS
            "[cae] Installing Python packages for ${CAE_USD_TOOL_NAME}: "
            "${_tool_python_packages}")
        file(REMOVE_RECURSE "${CAE_USD_TOOL_PYTHON_TARGET}")
        file(MAKE_DIRECTORY "${CAE_USD_TOOL_PYTHON_TARGET}")
        execute_process(
            COMMAND
                "${CAE_USD_TOOL_PYTHON_EXECUTABLE}" -m pip install
                --disable-pip-version-check
                --upgrade
                --no-deps
                --target "${CAE_USD_TOOL_PYTHON_TARGET}"
                ${_tool_python_packages}
            RESULT_VARIABLE _pip_result
            OUTPUT_VARIABLE _pip_output
            ERROR_VARIABLE _pip_error
        )
        if(NOT _pip_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to install Python packages for ${CAE_USD_TOOL_NAME} "
                "(exit ${_pip_result}):\n${_pip_output}\n${_pip_error}")
        endif()
        file(WRITE "${_state_file}" "${_expected_state}\n")
    endif()
endif()

set(_pythonpath_entries "")
_cae_append_existing_path(_pythonpath_entries "${CAE_USD_TOOL_PYTHON_TARGET}" ${_tool_pythonpath})
if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
    list(APPEND _pythonpath_entries "$ENV{PYTHONPATH}")
endif()
_cae_join_env_path(_pythonpath_env ${_pythonpath_entries})

set(_path_entries "")
_cae_append_existing_path(_path_entries ${_tool_path})
if(DEFINED ENV{PATH} AND NOT "$ENV{PATH}" STREQUAL "")
    list(APPEND _path_entries "$ENV{PATH}")
endif()
_cae_join_env_path(_path_env ${_path_entries})

set(_library_path_entries "")
_cae_append_existing_path(_library_path_entries ${_tool_library_path})

if(WIN32)
    # DLL directories are carried by PATH on Windows.
elseif(APPLE)
    if(DEFINED ENV{DYLD_LIBRARY_PATH} AND NOT "$ENV{DYLD_LIBRARY_PATH}" STREQUAL "")
        list(APPEND _library_path_entries "$ENV{DYLD_LIBRARY_PATH}")
    endif()
    _cae_join_env_path(_library_path_env ${_library_path_entries})
else()
    if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
        list(APPEND _library_path_entries "$ENV{LD_LIBRARY_PATH}")
    endif()
    _cae_join_env_path(_library_path_env ${_library_path_entries})
endif()

if(NOT "${_pythonpath_env}" STREQUAL "")
    set(ENV{PYTHONPATH} "${_pythonpath_env}")
endif()

if(NOT "${_path_env}" STREQUAL "")
    set(ENV{PATH} "${_path_env}")
endif()

if(NOT "${_library_path_env}" STREQUAL "")
    if(APPLE)
        set(ENV{DYLD_LIBRARY_PATH} "${_library_path_env}")
    elseif(NOT WIN32)
        set(ENV{LD_LIBRARY_PATH} "${_library_path_env}")
    endif()
endif()

file(STRINGS "${CAE_USD_TOOL_ARGS_FILE}" _tool_args)

if(CAE_USD_TOOL_USE_PYTHON)
    set(_command "${CAE_USD_TOOL_PYTHON_EXECUTABLE}" "${CAE_USD_TOOL_EXECUTABLE}" ${_tool_args})
else()
    set(_command "${CAE_USD_TOOL_EXECUTABLE}" ${_tool_args})
endif()

if(CAE_USD_TOOL_WORKING_DIRECTORY)
    set(_working_directory "${CAE_USD_TOOL_WORKING_DIRECTORY}")
else()
    set(_working_directory "${CMAKE_CURRENT_LIST_DIR}")
endif()

execute_process(
    COMMAND ${_command}
    RESULT_VARIABLE _tool_result
    OUTPUT_VARIABLE _tool_output
    ERROR_VARIABLE _tool_error
    WORKING_DIRECTORY "${_working_directory}"
)

if(NOT _tool_result EQUAL 0)
    message(FATAL_ERROR
        "CaeUSD tool '${CAE_USD_TOOL_NAME}' failed (exit ${_tool_result}):\n"
        "${_tool_output}\n${_tool_error}")
endif()

if(_tool_output)
    message(STATUS "${_tool_output}")
endif()
