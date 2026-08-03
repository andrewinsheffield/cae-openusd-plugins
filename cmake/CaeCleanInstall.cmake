# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# CaeCleanInstall.cmake
#
# CTest helper used by the cae_install fixture.  It removes the staging
# install prefix before invoking `cmake --install`, which keeps test runs from
# accidentally seeing stale plugins, Python files, or resources left behind by
# earlier builds.
#
# Required variables:
#   CAE_INSTALL_BUILD_DIR -- configured CMake build directory to install from
#   CAE_INSTALL_PREFIX    -- staging install prefix to remove and recreate
#
# Intended usage:
#   cmake
#       -DCAE_INSTALL_BUILD_DIR=<build-dir>
#       -DCAE_INSTALL_PREFIX=<install-prefix>
#       -P cmake/CaeCleanInstall.cmake
#
if(NOT DEFINED CAE_INSTALL_BUILD_DIR)
    message(FATAL_ERROR "CAE_INSTALL_BUILD_DIR is required")
endif()

if(NOT DEFINED CAE_INSTALL_PREFIX)
    message(FATAL_ERROR "CAE_INSTALL_PREFIX is required")
endif()

get_filename_component(_install_prefix "${CAE_INSTALL_PREFIX}" ABSOLUTE)
if(_install_prefix STREQUAL "/" OR _install_prefix STREQUAL "")
    message(FATAL_ERROR "Refusing to remove unsafe install prefix: ${_install_prefix}")
endif()

message(STATUS "[cae] Removing test install prefix: ${_install_prefix}")
file(REMOVE_RECURSE "${_install_prefix}")

message(STATUS "[cae] Installing ${CAE_INSTALL_BUILD_DIR} to ${_install_prefix}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            --install "${CAE_INSTALL_BUILD_DIR}"
            --prefix "${_install_prefix}"
    RESULT_VARIABLE _install_result
)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed with exit code ${_install_result}")
endif()
