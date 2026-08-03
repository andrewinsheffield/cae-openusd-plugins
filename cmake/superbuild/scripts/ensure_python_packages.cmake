# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Ensure isolated Python packages exist.
#
# This script is used by the superbuild before invoking tools such as
# OpenUSD's build_usd.py and before running the top-level CTest suite. It
# installs Python modules into a target directory under the generated SDK or
# superbuild tree, then writes a stamp file for Ninja.
#
# Required inputs:
#   CAE_PYTHON_EXECUTABLE     interpreter to query and install with
#   CAE_PYTHON_TARGET_DIR     directory added to PYTHONPATH
#   CAE_PYTHON_IMPORT_NAME    import used to decide whether install is needed
#   CAE_PYTHON_PACKAGES       pip package specs to install if import fails
#   CAE_PYTHON_ALWAYS_INSTALL reinstall packages when this script is invoked
#                             (optional; defaults to OFF)
#   CAE_PYTHON_STAMP          output stamp written on success
#
# The selected interpreter is not modified globally. All installed modules live
# under CAE_PYTHON_TARGET_DIR and are consumed by prepending PYTHONPATH.
#
if(NOT DEFINED CAE_PYTHON_EXECUTABLE OR CAE_PYTHON_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "CAE_PYTHON_EXECUTABLE is required")
endif()
if(NOT DEFINED CAE_PYTHON_TARGET_DIR OR CAE_PYTHON_TARGET_DIR STREQUAL "")
    message(FATAL_ERROR "CAE_PYTHON_TARGET_DIR is required")
endif()
if(NOT DEFINED CAE_PYTHON_IMPORT_NAME OR CAE_PYTHON_IMPORT_NAME STREQUAL "")
    message(FATAL_ERROR "CAE_PYTHON_IMPORT_NAME is required")
endif()
if(NOT DEFINED CAE_PYTHON_PACKAGES OR CAE_PYTHON_PACKAGES STREQUAL "")
    message(FATAL_ERROR "CAE_PYTHON_PACKAGES is required")
endif()
if(NOT DEFINED CAE_PYTHON_STAMP OR CAE_PYTHON_STAMP STREQUAL "")
    message(FATAL_ERROR "CAE_PYTHON_STAMP is required")
endif()

if(CMAKE_HOST_WIN32)
    set(_path_separator ";")
else()
    set(_path_separator ":")
endif()

set(_pythonpath "${CAE_PYTHON_TARGET_DIR}")
if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
    string(APPEND _pythonpath "${_path_separator}$ENV{PYTHONPATH}")
endif()

set(_install_packages "${CAE_PYTHON_ALWAYS_INSTALL}")
if(NOT _install_packages)
    # Import first so rebuilds are fast and do not keep reinstalling packages
    # into an already-valid target directory.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${_pythonpath}"
            "${CAE_PYTHON_EXECUTABLE}" -c "import ${CAE_PYTHON_IMPORT_NAME}"
        RESULT_VARIABLE _import_result
    )
    if(NOT _import_result EQUAL 0)
        set(_install_packages ON)
    endif()
endif()

if(_install_packages)
    file(MAKE_DIRECTORY "${CAE_PYTHON_TARGET_DIR}")
    # CAE_PYTHON_PACKAGES is a command-line string so CI can pass multiple
    # package specs through one -D variable.
    separate_arguments(_python_packages NATIVE_COMMAND "${CAE_PYTHON_PACKAGES}")
    execute_process(
        COMMAND "${CAE_PYTHON_EXECUTABLE}" -m pip install
            --disable-pip-version-check
            --upgrade
            --target "${CAE_PYTHON_TARGET_DIR}"
            ${_python_packages}
        RESULT_VARIABLE _pip_result
    )
    if(NOT _pip_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to install Python build packages: ${CAE_PYTHON_PACKAGES}")
    endif()
endif()

# Stamp files let add_custom_command() model this as a normal build step.
file(MAKE_DIRECTORY "${CAE_PYTHON_TARGET_DIR}")
file(WRITE "${CAE_PYTHON_STAMP}" "ok\n")
