# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# CaeTestHelpers.cmake
#
# Helpers for registering CAE USD plugin tests with CTest.
#
# Functions:
#   cae_add_pytest(name ...)  -- register a pytest test suite with CTest

include(CMakeParseArguments)

set(CAE_TEST_RUNTIME_PYTHONPATH "" CACHE STRING
    "Extra Python package directories used by CTest tests")
set(CAE_TEST_RUNTIME_LIBRARY_DIRS "" CACHE STRING
    "Extra native runtime library directories used by CTest tests")

function(_cae_join_env_paths OUT_VAR)
    set(_path_entries ${ARGN})
    if(WIN32)
        set(_path_sep "\\;")
    else()
        set(_path_sep ":")
    endif()
    list(FILTER _path_entries EXCLUDE REGEX "^$")
    if(_path_entries)
        list(REMOVE_DUPLICATES _path_entries)
    endif()
    list(JOIN _path_entries "${_path_sep}" _joined_paths)
    set(${OUT_VAR} "${_joined_paths}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------
# cae_add_pytest
#
# Registers one or more pytest test paths as a named CTest test.
# Tests are automatically run against the staging install produced by
# the cae_test_setup fixture -- no manual cmake --install step needed.
#
# Usage:
#   cae_add_pytest(test_omni_sci
#       TESTS   tests/python/omni_sci
#       PLUGINS omniSci omniSciCgns   # names of schema libs under plugin/usd/
#       LABELS  unit
#   )
#
# PLUGINS -- list of plugin library names required by the test. Kept in the
#   API for readability at call sites.
#
# Required variables (set before calling, all set by root CMakeLists.txt):
#   CAE_SOURCE_ROOT        -- repo root (source code lives here)
#   CAE_TEST_INSTALL_DIR   -- staging install dir (build/test_install)
#   Python3_EXECUTABLE     -- Python interpreter
# -----------------------------------------------------------------------
function(cae_add_pytest NAME)
    cmake_parse_arguments(ARG "" "" "TESTS;PLUGINS;LABELS;PYTEST_ARGS;ENV_VARS" ${ARGN})

    if(NOT Python3_FOUND)
        message(STATUS "[cae] Skipping pytest ${NAME}: Python3 not found")
        return()
    endif()

    if(NOT ARG_TESTS)
        message(FATAL_ERROR "cae_add_pytest(${NAME}): TESTS argument is required")
    endif()

    # Resolve test paths relative to the project source root
    set(_test_paths "")
    foreach(_t IN LISTS ARG_TESTS)
        if(IS_ABSOLUTE "${_t}")
            list(APPEND _test_paths "${_t}")
        else()
            list(APPEND _test_paths "${CAE_SOURCE_ROOT}/${_t}")
        endif()
    endforeach()

    add_test(
        NAME    ${NAME}
        COMMAND ${Python3_EXECUTABLE} -m pytest ${_test_paths} -v --tb=short ${ARG_PYTEST_ARGS}
    )

    # These are the staging install paths produced by the cae_install fixture.
    # External USD/Python/dependency runtime paths are supplied explicitly via
    # CAE_TEST_RUNTIME_PYTHONPATH and CAE_TEST_RUNTIME_LIBRARY_DIRS.
    set(_plugin_dir "${CAE_TEST_INSTALL_DIR}/plugin/usd")
    set(_py_dir     "${CAE_TEST_INSTALL_DIR}/lib/python")

    set(_python_path_entries
        "${_py_dir}"
        ${CAE_TEST_RUNTIME_PYTHONPATH}
    )
    if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
        list(APPEND _python_path_entries "$ENV{PYTHONPATH}")
    endif()
    _cae_join_env_paths(_python_path ${_python_path_entries})

    if(WIN32)
        set(_runtime_path_var "PATH")
    elseif(APPLE)
        set(_runtime_path_var "DYLD_LIBRARY_PATH")
    else()
        set(_runtime_path_var "LD_LIBRARY_PATH")
    endif()

    set(_runtime_path_entries
        "${_plugin_dir}"
        ${CAE_TEST_RUNTIME_LIBRARY_DIRS}
    )
    if(WIN32)
        # Keep PyPI usd-core's own runtime directory first. Its pxr.Tf import
        # wrapper adds every PXR_USD_WINDOWS_DLL_PATH entry with
        # AddDllDirectory; mixing plugin/dependency SDK dirs into that list can
        # make `_tf.pyd` bind against an incompatible duplicate DLL. Plugin and
        # dependency dirs still remain on PATH for the rest of the test.
        set(_pxr_windows_primary_dll_path_entries)
        foreach(_entry IN LISTS CAE_TEST_RUNTIME_LIBRARY_DIRS)
            if(_entry MATCHES "[/\\\\]python[/\\\\]pxr$")
                list(APPEND _pxr_windows_primary_dll_path_entries "${_entry}")
            endif()
        endforeach()
        set(_pxr_windows_dll_path_entries
            ${_pxr_windows_primary_dll_path_entries})
    endif()
    if(DEFINED ENV{${_runtime_path_var}} AND NOT "$ENV{${_runtime_path_var}}" STREQUAL "")
        list(APPEND _runtime_path_entries "$ENV{${_runtime_path_var}}")
    endif()
    _cae_join_env_paths(_runtime_path ${_runtime_path_entries})

    set(_test_env
        "PXR_PLUGINPATH_NAME=${_plugin_dir}"
        "PYTHONPATH=${_python_path}"
        "${_runtime_path_var}=${_runtime_path}"
    )
    if(WIN32)
        if(DEFINED ENV{PXR_USD_WINDOWS_DLL_PATH}
                AND NOT "$ENV{PXR_USD_WINDOWS_DLL_PATH}" STREQUAL "")
            list(APPEND _pxr_windows_dll_path_entries
                "$ENV{PXR_USD_WINDOWS_DLL_PATH}")
        endif()
        if(_pxr_windows_dll_path_entries)
            _cae_join_env_paths(_pxr_windows_dll_path
                ${_pxr_windows_dll_path_entries})
            list(APPEND _test_env
                "PXR_USD_WINDOWS_DLL_PATH=${_pxr_windows_dll_path}")
        endif()
    endif()
    if(ARG_ENV_VARS)
        list(APPEND _test_env ${ARG_ENV_VARS})
    endif()

    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "${_test_env}"
        FIXTURES_REQUIRED cae_install
        LABELS "${ARG_LABELS}"
    )
endfunction()
