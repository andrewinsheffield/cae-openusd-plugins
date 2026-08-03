# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Findpugixml.cmake
#
# Provides:
#   pugixml_FOUND
#   pugixml_VERSION
#   pugixml_INCLUDE_DIRS
#   pugixml_LIBRARIES
#   pugixml::pugixml

find_package(pugixml CONFIG QUIET)

set(_pugixml_CONFIG_TARGET)
if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
    set(_pugixml_CONFIG_TARGET_CANDIDATES
        pugixml::static
        pugixml::pugixml
        pugixml
        pugixml::shared)
elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
    set(_pugixml_CONFIG_TARGET_CANDIDATES
        pugixml::shared
        pugixml::pugixml
        pugixml
        pugixml::static)
else()
    set(_pugixml_CONFIG_TARGET_CANDIDATES
        pugixml::pugixml
        pugixml::shared
        pugixml::static
        pugixml)
endif()
foreach(_candidate IN LISTS _pugixml_CONFIG_TARGET_CANDIDATES)
    if(TARGET "${_candidate}")
        set(_pugixml_CONFIG_TARGET "${_candidate}")
        break()
    endif()
endforeach()

include(FindPackageHandleStandardArgs)

if(_pugixml_CONFIG_TARGET)
    find_package_handle_standard_args(pugixml
        REQUIRED_VARS _pugixml_CONFIG_TARGET
        VERSION_VAR pugixml_VERSION)

    if(pugixml_FOUND)
        set(pugixml_LIBRARIES "${_pugixml_CONFIG_TARGET}")
        if(NOT TARGET pugixml::pugixml)
            add_library(pugixml::pugixml INTERFACE IMPORTED)
            set_target_properties(pugixml::pugixml PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_pugixml_CONFIG_TARGET}"
            )
        endif()
    endif()
else()
    find_path(pugixml_INCLUDE_DIR
        NAMES pugixml.hpp
        HINTS
            "${pugixml_ROOT}"
            "$ENV{pugixml_ROOT}"
            "${PUGIXML_ROOT}"
            "$ENV{PUGIXML_ROOT}"
        PATH_SUFFIXES include include/pugixml
        DOC "pugixml include directory"
    )
    find_library(pugixml_LIBRARY
        NAMES pugixml
        HINTS
            "${pugixml_ROOT}"
            "$ENV{pugixml_ROOT}"
            "${PUGIXML_ROOT}"
            "$ENV{PUGIXML_ROOT}"
        PATH_SUFFIXES lib lib64
        DOC "pugixml library"
    )

    if(pugixml_INCLUDE_DIR AND EXISTS "${pugixml_INCLUDE_DIR}/pugixml.hpp")
        file(STRINGS "${pugixml_INCLUDE_DIR}/pugixml.hpp" _pugixml_VERSION_LINE
            REGEX "^#define[ \t]+PUGIXML_VERSION[ \t]+[0-9]+")
        if(_pugixml_VERSION_LINE)
            string(REGEX REPLACE ".*PUGIXML_VERSION[ \t]+([0-9]+).*" "\\1"
                _pugixml_VERSION_NUMBER "${_pugixml_VERSION_LINE}")
            math(EXPR _pugixml_VERSION_MAJOR "${_pugixml_VERSION_NUMBER} / 1000")
            math(EXPR _pugixml_VERSION_MINOR "(${_pugixml_VERSION_NUMBER} / 10) % 100")
            math(EXPR _pugixml_VERSION_PATCH "${_pugixml_VERSION_NUMBER} % 10")
            set(pugixml_VERSION
                "${_pugixml_VERSION_MAJOR}.${_pugixml_VERSION_MINOR}.${_pugixml_VERSION_PATCH}")
        endif()
    endif()

    find_package_handle_standard_args(pugixml
        REQUIRED_VARS pugixml_INCLUDE_DIR pugixml_LIBRARY
        VERSION_VAR pugixml_VERSION)

    if(pugixml_FOUND)
        set(pugixml_INCLUDE_DIRS "${pugixml_INCLUDE_DIR}")
        set(pugixml_LIBRARIES "${pugixml_LIBRARY}")
        if(NOT TARGET pugixml::pugixml)
            add_library(pugixml::pugixml UNKNOWN IMPORTED)
            set_target_properties(pugixml::pugixml PROPERTIES
                IMPORTED_LOCATION "${pugixml_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${pugixml_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

mark_as_advanced(pugixml_INCLUDE_DIR pugixml_LIBRARY)

unset(_pugixml_CONFIG_TARGET)
unset(_pugixml_CONFIG_TARGET_CANDIDATES)
unset(_pugixml_VERSION_LINE)
unset(_pugixml_VERSION_NUMBER)
unset(_pugixml_VERSION_MAJOR)
unset(_pugixml_VERSION_MINOR)
unset(_pugixml_VERSION_PATCH)
