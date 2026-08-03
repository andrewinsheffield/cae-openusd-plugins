# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# FindLZ4.cmake
#
# Provides:
#   LZ4_FOUND
#   LZ4_VERSION
#   LZ4_INCLUDE_DIRS
#   LZ4_LIBRARIES
#   LZ4::LZ4

cmake_policy(PUSH)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
find_package(lz4 CONFIG QUIET)
cmake_policy(POP)

set(_LZ4_CONFIG_TARGET)
if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
    set(_LZ4_CONFIG_TARGET_CANDIDATES
        LZ4::lz4_static
        lz4_static
        LZ4::lz4
        lz4
        LZ4::lz4_shared
        lz4_shared)
elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
    set(_LZ4_CONFIG_TARGET_CANDIDATES
        LZ4::lz4_shared
        lz4_shared
        LZ4::lz4
        lz4
        LZ4::lz4_static
        lz4_static)
else()
    set(_LZ4_CONFIG_TARGET_CANDIDATES
        LZ4::lz4
        LZ4::lz4_shared
        LZ4::lz4_static
        lz4
        lz4_shared
        lz4_static)
endif()
foreach(_candidate IN LISTS _LZ4_CONFIG_TARGET_CANDIDATES)
    if(TARGET "${_candidate}")
        set(_LZ4_CONFIG_TARGET "${_candidate}")
        break()
    endif()
endforeach()

if(lz4_VERSION AND NOT LZ4_VERSION)
    set(LZ4_VERSION "${lz4_VERSION}")
endif()

include(FindPackageHandleStandardArgs)

if(_LZ4_CONFIG_TARGET)
    find_package_handle_standard_args(LZ4
        REQUIRED_VARS _LZ4_CONFIG_TARGET
        VERSION_VAR LZ4_VERSION)

    if(LZ4_FOUND)
        set(LZ4_LIBRARIES "${_LZ4_CONFIG_TARGET}")
        if(NOT TARGET LZ4::LZ4)
            add_library(LZ4::LZ4 INTERFACE IMPORTED)
            set_target_properties(LZ4::LZ4 PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_LZ4_CONFIG_TARGET}"
            )
        endif()
    endif()
else()
    find_path(LZ4_INCLUDE_DIR
        NAMES lz4.h
        HINTS "${LZ4_ROOT}" "$ENV{LZ4_ROOT}"
        PATH_SUFFIXES include
        DOC "LZ4 include directory"
    )
    find_library(LZ4_LIBRARY
        NAMES lz4 liblz4
        HINTS "${LZ4_ROOT}" "$ENV{LZ4_ROOT}"
        PATH_SUFFIXES lib lib64
        DOC "LZ4 library"
    )

    if(LZ4_INCLUDE_DIR AND EXISTS "${LZ4_INCLUDE_DIR}/lz4.h")
        file(STRINGS "${LZ4_INCLUDE_DIR}/lz4.h" _LZ4_VERSION_LINES
            REGEX "^#define[ \t]+LZ4_VERSION_(MAJOR|MINOR|RELEASE)[ \t]+[0-9]+")
        foreach(_version_type IN ITEMS MAJOR MINOR RELEASE)
            foreach(_line IN LISTS _LZ4_VERSION_LINES)
                if(_line MATCHES "^#define[ \t]+LZ4_VERSION_${_version_type}[ \t]+([0-9]+)")
                    set("_LZ4_VERSION_${_version_type}" "${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endforeach()
        if(DEFINED _LZ4_VERSION_MAJOR
                AND DEFINED _LZ4_VERSION_MINOR
                AND DEFINED _LZ4_VERSION_RELEASE)
            set(LZ4_VERSION
                "${_LZ4_VERSION_MAJOR}.${_LZ4_VERSION_MINOR}.${_LZ4_VERSION_RELEASE}")
        endif()
    endif()

    find_package_handle_standard_args(LZ4
        REQUIRED_VARS LZ4_INCLUDE_DIR LZ4_LIBRARY
        VERSION_VAR LZ4_VERSION)

    if(LZ4_FOUND)
        set(LZ4_INCLUDE_DIRS "${LZ4_INCLUDE_DIR}")
        set(LZ4_LIBRARIES "${LZ4_LIBRARY}")
        if(NOT TARGET LZ4::LZ4)
            add_library(LZ4::LZ4 UNKNOWN IMPORTED)
            set_target_properties(LZ4::LZ4 PROPERTIES
                IMPORTED_LOCATION "${LZ4_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LZ4_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

mark_as_advanced(LZ4_INCLUDE_DIR LZ4_LIBRARY)

unset(_LZ4_CONFIG_TARGET)
unset(_LZ4_CONFIG_TARGET_CANDIDATES)
unset(_LZ4_VERSION_LINES)
unset(_LZ4_VERSION_MAJOR)
unset(_LZ4_VERSION_MINOR)
unset(_LZ4_VERSION_RELEASE)
