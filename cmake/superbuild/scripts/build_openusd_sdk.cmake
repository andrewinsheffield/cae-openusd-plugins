# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Build one source-based OpenUSD SDK.
#
# This script runs in CMake script mode from the `cae-sb-openusd`
# ExternalProject build step. OpenUSD's `build_usd.py` remains the authority for
# its own third-party dependency graph and install layout; this wrapper only
# maps our CI/superbuild matrix variables onto stable build_usd.py arguments.
#
# Required inputs:
#   CAE_OPENUSD_SOURCE_DIR        checked-out OpenUSD source tree
#   CAE_OPENUSD_SDK_ROOT          final OpenUSD install prefix
#   CAE_OPENUSD_BUILD_DIR         build_usd.py build directory
#   CAE_OPENUSD_SRC_DIR           build_usd.py third-party source directory
#   CAE_OPENUSD_BUILD_TYPE        release/debug/etc. for build_usd.py
#   CAE_PYTHON_EXECUTABLE         Python used for build_usd.py
#
# Optional inputs:
#   CAE_OPENUSD_GENERATOR        CMake generator selected by the superbuild
#   CAE_CMAKE_C_COMPILER         C compiler selected by the superbuild
#   CAE_CMAKE_CXX_COMPILER       C++ compiler selected by the superbuild
#   CAE_OPENUSD_CXX11_ABI         0 or 1 for libstdc++ ABI compatibility
#   CAE_OPENUSD_USE_ONETBB        request oneTBB when build_usd.py supports it
#   CAE_OPENUSD_BUILD_JOBS        positive integer job cap for build_usd.py
#   CAE_OPENUSD_PYTHON_BUILD_DEPS isolated PYTHONPATH, usually Jinja2
#   CAE_PYTHON_INCLUDE_DIR        Python include dir for --build-python-info
#   CAE_PYTHON_LIBRARY            Python library for --build-python-info
#   CAE_PYTHON_VERSION            major.minor for --build-python-info
#
# Policy:
#   - Use the generator and compiler selected by the top-level superbuild.
#   - Do not pass --jobs by default. CI profiles may set
#     CAE_OPENUSD_BUILD_JOBS when runner memory needs a cap.
#   - Build a compact SDK: shared libraries, Python, and tools only.
#
foreach(_required IN ITEMS
        CAE_OPENUSD_SOURCE_DIR
        CAE_OPENUSD_SDK_ROOT
        CAE_OPENUSD_BUILD_DIR
        CAE_OPENUSD_SRC_DIR
        CAE_OPENUSD_BUILD_TYPE
        CAE_PYTHON_EXECUTABLE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

# ExternalProject handles checkout; this script handles the build invocation.
set(_build_usd_py "${CAE_OPENUSD_SOURCE_DIR}/build_scripts/build_usd.py")
if(NOT EXISTS "${_build_usd_py}")
    message(FATAL_ERROR "OpenUSD build script was not found: ${_build_usd_py}")
endif()

if(NOT "${CAE_OPENUSD_CXX11_ABI}" STREQUAL ""
        AND NOT CAE_OPENUSD_CXX11_ABI MATCHES "^[01]$")
    message(FATAL_ERROR
        "CAE_OPENUSD_CXX11_ABI must be empty, 0, or 1; got '${CAE_OPENUSD_CXX11_ABI}'")
endif()
if(CAE_OPENUSD_BUILD_JOBS AND NOT CAE_OPENUSD_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "CAE_OPENUSD_BUILD_JOBS must be a positive integer; got '${CAE_OPENUSD_BUILD_JOBS}'")
endif()

string(TOLOWER "${CAE_OPENUSD_BUILD_TYPE}" _openusd_build_variant)
if(_openusd_build_variant STREQUAL "relwithdebinfo")
    set(_openusd_build_variant "relwithdebuginfo")
endif()

set(_openusd_generator "Ninja")
if(DEFINED CAE_OPENUSD_GENERATOR AND NOT "${CAE_OPENUSD_GENERATOR}" STREQUAL "")
    set(_openusd_generator "${CAE_OPENUSD_GENERATOR}")
endif()
if(NOT _openusd_build_variant MATCHES "^(debug|release|relwithdebuginfo)$")
    message(FATAL_ERROR
        "CAE_OPENUSD_BUILD_TYPE must map to debug, release, or relwithdebuginfo; "
        "got '${CAE_OPENUSD_BUILD_TYPE}'")
endif()

# A previous run may already have produced a usable SDK. Keep this practical:
# CMake package, schema generator, and at least one split Sdf library are enough
# for the CAE plugin build to proceed.
set(_has_complete_sdk OFF)
if(EXISTS "${CAE_OPENUSD_SDK_ROOT}/pxrConfig.cmake"
        AND EXISTS "${CAE_OPENUSD_SDK_ROOT}/bin/usdGenSchema")
    file(GLOB _sdf_libs
        "${CAE_OPENUSD_SDK_ROOT}/lib/*usd_sdf*"
        "${CAE_OPENUSD_SDK_ROOT}/bin/*usd_sdf*")
    if(_sdf_libs)
        set(_has_complete_sdk ON)
    endif()
endif()

if(_has_complete_sdk)
    message(STATUS "OpenUSD SDK already exists: ${CAE_OPENUSD_SDK_ROOT}")
    return()
endif()

# Guard against reusing the failed classic-TBB experiment. The curated Kit
# runtimes use oneTBB, so a source SDK with libtbb.so.2 is not suitable for the
# packman-runtime-compatible wheel lane.
if(EXISTS "${CAE_OPENUSD_SDK_ROOT}/lib/libtbb.so.2")
    message(STATUS
        "Existing OpenUSD SDK uses classic TBB; rebuilding with oneTBB-compatible settings")
    file(REMOVE_RECURSE "${CAE_OPENUSD_SDK_ROOT}" "${CAE_OPENUSD_BUILD_DIR}")
endif()

# Introspect build_usd.py instead of keying only on OpenUSD versions. That keeps
# future tags flexible if options are added, removed, or renamed.
execute_process(
    COMMAND "${CAE_PYTHON_EXECUTABLE}" "${_build_usd_py}" --help
    OUTPUT_VARIABLE _build_usd_help
    RESULT_VARIABLE _build_usd_help_result
)
if(NOT _build_usd_help_result EQUAL 0)
    message(FATAL_ERROR "Failed to query build_usd.py help")
endif()

set(_build_usd_supports_onetbb OFF)
set(_build_usd_supports_usdvalidation OFF)
set(_build_usd_onetbb_needs_cmake_policy_floor OFF)
if(_build_usd_help MATCHES "--onetbb")
    set(_build_usd_supports_onetbb ON)
endif()
if(_build_usd_help MATCHES "--no-usdValidation")
    set(_build_usd_supports_usdvalidation ON)
endif()
file(STRINGS "${_build_usd_py}" _onetbb_2021_9_line
    REGEX "oneTBB/.*/v2021\\.9\\.0\\.zip")
if(_onetbb_2021_9_line)
    set(_build_usd_onetbb_needs_cmake_policy_floor ON)
endif()

# Keep the OpenUSD SDK intentionally narrow. Imaging, MaterialX, Alembic, Draco,
# tests, and docs are not needed to compile or run this plugin package.
set(_build_args
    "${CAE_OPENUSD_SDK_ROOT}"
    --src "${CAE_OPENUSD_SRC_DIR}"
    --inst "${CAE_OPENUSD_SDK_ROOT}"
    --build "${CAE_OPENUSD_BUILD_DIR}"
    --build-variant "${_openusd_build_variant}"
    --generator "${_openusd_generator}"
    --build-shared
    --python
    --tools
    --no-tests
    --no-examples
    --no-tutorials
    --no-docs
    --no-python-docs
    --no-imaging
    --no-usdview
    --no-materialx
    --no-alembic
    --no-draco
)

if(NOT "${CAE_OPENUSD_CXX11_ABI}" STREQUAL "")
    list(APPEND _build_args --use-cxx11-abi "${CAE_OPENUSD_CXX11_ABI}")
endif()

if(CAE_OPENUSD_BUILD_JOBS)
    list(APPEND _build_args --jobs "${CAE_OPENUSD_BUILD_JOBS}")
endif()

if(CAE_PYTHON_INCLUDE_DIR AND CAE_PYTHON_LIBRARY AND CAE_PYTHON_VERSION)
    # Prevent build_usd.py from discovering a different host Python than the
    # interpreter selected by the CI matrix.
    list(APPEND _build_args
        --build-python-info
            "${CAE_PYTHON_EXECUTABLE}"
            "${CAE_PYTHON_INCLUDE_DIR}"
            "${CAE_PYTHON_LIBRARY}"
            "${CAE_PYTHON_VERSION}")
endif()

if(CAE_OPENUSD_USE_ONETBB AND _build_usd_supports_onetbb)
    list(APPEND _build_args --onetbb)
    if(_build_usd_onetbb_needs_cmake_policy_floor)
        # CMake 4 rejects oneTBB 2021.9's old policy range unless this floor is
        # supplied. Newer OpenUSD tags, such as v25.11's oneTBB 2021.12, do not
        # receive this workaround.
        list(APPEND _build_args
            --build-args oneTBB,-DCMAKE_POLICY_VERSION_MINIMUM=3.5)
    endif()
endif()

if(_build_usd_supports_usdvalidation)
    list(APPEND _build_args --no-usdValidation)
endif()

if(CMAKE_HOST_WIN32)
    set(_path_separator ";")
else()
    set(_path_separator ":")
endif()

# Preserve the caller's PYTHONPATH but prepend the isolated build packages so
# build_usd.py can import Jinja2 without installing into the selected Python.
set(_pythonpath "${CAE_OPENUSD_PYTHON_BUILD_DEPS}")
if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
    if(_pythonpath)
        string(APPEND _pythonpath "${_path_separator}$ENV{PYTHONPATH}")
    else()
        set(_pythonpath "$ENV{PYTHONPATH}")
    endif()
endif()

message(STATUS "Building OpenUSD SDK at ${CAE_OPENUSD_SDK_ROOT}")
set(_build_env "PYTHONPATH=${_pythonpath}")
if(DEFINED CAE_CMAKE_C_COMPILER AND NOT "${CAE_CMAKE_C_COMPILER}" STREQUAL "")
    list(APPEND _build_env "CC=${CAE_CMAKE_C_COMPILER}")
endif()
if(DEFINED CAE_CMAKE_CXX_COMPILER AND NOT "${CAE_CMAKE_CXX_COMPILER}" STREQUAL "")
    list(APPEND _build_env "CXX=${CAE_CMAKE_CXX_COMPILER}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_build_env}
        "${CAE_PYTHON_EXECUTABLE}" "${_build_usd_py}" ${_build_args}
    RESULT_VARIABLE _build_result
)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "build_usd.py failed with exit code ${_build_result}")
endif()
