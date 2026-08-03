# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

# Minimal host-tool bootstrap used by GitLab before the SDK superbuild.
#
# Run with:
#
#   cmake -DCAE_TOOLS_DIR=$PWD/_build/tools -P cmake/ci/setup.cmake
#
# This intentionally keeps setup narrow:
#   1. pull the Packman Python profile used by the CMake script drivers
#   2. optionally pull a Packman USD runtime profile for openusd wheel tests
#   3. install CMake/Ninja into CAE_TOOLS_DIR with the setup Python

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(_cae_get out_var name default_value)
    if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
        set("${out_var}" "${${name}}" PARENT_SCOPE)
    elseif(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
        set("${out_var}" "$ENV{${name}}" PARENT_SCOPE)
    else()
        set("${out_var}" "${default_value}" PARENT_SCOPE)
    endif()
endfunction()

if(DEFINED CAE_TOOLS_DIR AND NOT "${CAE_TOOLS_DIR}" STREQUAL "")
    set(_tools_dir "${CAE_TOOLS_DIR}")
    if(NOT IS_ABSOLUTE "${_tools_dir}")
        get_filename_component(_tools_dir "${_repo_root}/${_tools_dir}" ABSOLUTE)
    endif()
else()
    set(_tools_dir "${_repo_root}/_build/tools")
endif()

if(CMAKE_HOST_WIN32)
    set(_packman "${_repo_root}/tools/packman/packman.cmd")
    set(_packman_platform "windows-x86_64")
    set(_platform_abi "windows-x86_64")
    set(_python "${_repo_root}/_build/host-deps/python/python.exe")
else()
    set(_packman "${_repo_root}/tools/packman/packman")
    set(_packman_platform "linux-x86_64")
    set(_platform_abi "manylinux_2_35_x86_64")
    set(_python "${_repo_root}/_build/host-deps/python/bin/python3")
endif()

set(_packman_profile "${_repo_root}/tools/deps/packman/py312.packman.xml")
set(_pip_cache "${_repo_root}/_build/pip-cache")
_cae_get(_pip_cmake_version CAE_PIP_CMAKE_VERSION "4.3.2")
_cae_get(_packman_variant CAE_SETUP_PACKMAN_VARIANT "python")

file(MAKE_DIRECTORY "${_pip_cache}")

function(_cae_packman_pull label profile)
    execute_process(
        COMMAND "${_packman}" pull
        -p "${_packman_platform}"
        -t "root=${_repo_root}"
        -t "platform_abi=${_platform_abi}"
        -t "cae_packman_platform_abi=${_platform_abi}"
        "${profile}"
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${label} failed with exit code ${_result}")
    endif()
endfunction()

_cae_packman_pull("Packman Python setup" "${_packman_profile}")

if(_packman_variant STREQUAL "python")
    set(_usd_profile "")
elseif(_packman_variant STREQUAL "usd")
    _cae_get(_usd_version CAE_USD_VERSION "")
    if(NOT _usd_version MATCHES "^(25\\.02|25\\.11)$")
        message(FATAL_ERROR
            "CAE_SETUP_PACKMAN_VARIANT=usd requires CAE_USD_VERSION 25.02 or 25.11")
    endif()
    set(_usd_profile
        "${_repo_root}/tools/deps/packman/usd-${_usd_version}-py312-runtime.packman.xml")
    if(NOT EXISTS "${_usd_profile}")
        message(FATAL_ERROR "Packman USD runtime profile does not exist: ${_usd_profile}")
    endif()
    _cae_packman_pull("Packman USD runtime setup" "${_usd_profile}")
else()
    message(FATAL_ERROR
        "Unsupported CAE_SETUP_PACKMAN_VARIANT='${_packman_variant}'. Use python or usd.")
endif()

execute_process(
    COMMAND "${_python}" -m pip install
        --disable-pip-version-check
        --upgrade
        --cache-dir "${_pip_cache}"
        --target "${_tools_dir}"
        "cmake==${_pip_cmake_version}"
        "scikit-build-core>=0.12"
        build
        ninja
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "CMake/Ninja setup failed with exit code ${_result}")
endif()

message(STATUS "CAE setup Python: ${_python}")
message(STATUS "CAE setup tools: ${_tools_dir}")
message(STATUS "CAE setup Packman variant: ${_packman_variant}")
if(_usd_profile)
    message(STATUS "CAE setup USD profile: ${_usd_profile}")
endif()
message(STATUS "CAE setup CMake version: ${_pip_cmake_version}")
