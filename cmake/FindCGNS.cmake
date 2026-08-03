# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# FindCGNS.cmake
#
# Locates the CGNS (CFD General Notation System) library.
#
# Search hints:
#   CGNS_ROOT   -- root of the CGNS installation, as a CMake cache variable
#                  or environment variable
#
# Result variables:
#   CGNS_FOUND
#   CGNS_VERSION
#   CGNS_INCLUDE_DIR
#   CGNS_INCLUDE_DIRS
#   CGNS_LIBRARY
#   CGNS_LIBRARIES
#
# Imported target created on success:
#   CGNS::CGNS

function(_cgns_get_imported_location target out_var)
    set(_location)

    get_target_property(_generic_location "${target}" IMPORTED_LOCATION)
    if(_generic_location)
        set(_location "${_generic_location}")
    endif()

    if(NOT _location)
        get_target_property(_generic_implib "${target}" IMPORTED_IMPLIB)
        if(_generic_implib)
            set(_location "${_generic_implib}")
        endif()
    endif()

    if(NOT _location)
        get_target_property(_configs "${target}" IMPORTED_CONFIGURATIONS)
        foreach(_config IN LISTS _configs)
            foreach(_property IN ITEMS IMPORTED_IMPLIB IMPORTED_LOCATION)
                get_target_property(_config_location "${target}" "${_property}_${_config}")
                if(_config_location)
                    set(_location "${_config_location}")
                    break()
                endif()
            endforeach()
            if(_location)
                break()
            endif()
        endforeach()
    endif()

    if(NOT _location)
        foreach(_config IN ITEMS RELEASE RELWITHDEBINFO MINSIZEREL DEBUG NOCONFIG)
            foreach(_property IN ITEMS IMPORTED_IMPLIB IMPORTED_LOCATION)
                get_target_property(_config_location "${target}" "${_property}_${_config}")
                if(_config_location)
                    set(_location "${_config_location}")
                    break()
                endif()
            endforeach()
            if(_location)
                break()
            endif()
        endforeach()
    endif()

    set("${out_var}" "${_location}" PARENT_SCOPE)
endfunction()

function(_cgns_get_target_include_dir target out_var)
    set(_include_dir)

    get_target_property(_include_dirs "${target}" INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_dir IN LISTS _include_dirs)
        if(_dir MATCHES "^\\$<")
            continue()
        endif()
        if(EXISTS "${_dir}/cgnslib.h")
            set(_include_dir "${_dir}")
            break()
        endif()
    endforeach()

    set("${out_var}" "${_include_dir}" PARENT_SCOPE)
endfunction()

function(_cgns_normalize_link_libraries out_var)
    set(_normalized_libraries)
    foreach(_link_library IN LISTS ARGN)
        if(_link_library MATCHES "hdf5-(static|shared)")
            if(NOT TARGET HDF5::HDF5)
                find_package(HDF5 COMPONENTS C QUIET)
            endif()
            if(TARGET HDF5::HDF5)
                list(APPEND _normalized_libraries HDF5::HDF5)
            else()
                list(APPEND _normalized_libraries "${_link_library}")
            endif()
        else()
            list(APPEND _normalized_libraries "${_link_library}")
        endif()
    endforeach()
    if(_normalized_libraries)
        list(REMOVE_DUPLICATES _normalized_libraries)
    endif()
    set("${out_var}" "${_normalized_libraries}" PARENT_SCOPE)
endfunction()

# Prefer the upstream package config when it exists, but do not assume it
# exports the same target name this project consumes.
if(CGNS_FIND_VERSION)
    if(CGNS_FIND_VERSION_EXACT)
        find_package(CGNS ${CGNS_FIND_VERSION} EXACT CONFIG QUIET)
    else()
        find_package(CGNS ${CGNS_FIND_VERSION} CONFIG QUIET)
    endif()
else()
    find_package(CGNS CONFIG QUIET)
endif()

set(_CGNS_CONFIG_TARGET)
if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
    set(_CGNS_CONFIG_TARGET_CANDIDATES
        CGNS::cgns_static
        CGNS::cgns-static
        cgns_static
        cgns
        CGNS::CGNS
        CGNS::cgns_shared
        CGNS::cgns-shared
        cgns_shared)
elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
    set(_CGNS_CONFIG_TARGET_CANDIDATES
        CGNS::cgns_shared
        CGNS::cgns-shared
        cgns_shared
        cgns
        CGNS::CGNS
        CGNS::cgns_static
        CGNS::cgns-static
        cgns_static)
else()
    set(_CGNS_CONFIG_TARGET_CANDIDATES
        CGNS::CGNS
        CGNS::cgns_shared
        CGNS::cgns_static
        CGNS::cgns-shared
        CGNS::cgns-static
        cgns_shared
        cgns_static
        cgns)
endif()
foreach(_candidate IN LISTS _CGNS_CONFIG_TARGET_CANDIDATES)
    if(TARGET "${_candidate}")
        set(_CGNS_CONFIG_TARGET "${_candidate}")
        break()
    endif()
endforeach()

if(_CGNS_CONFIG_TARGET)
    _cgns_get_target_include_dir("${_CGNS_CONFIG_TARGET}" _CGNS_CONFIG_INCLUDE_DIR)
    if(_CGNS_CONFIG_INCLUDE_DIR AND NOT CGNS_INCLUDE_DIR)
        set(CGNS_INCLUDE_DIR "${_CGNS_CONFIG_INCLUDE_DIR}" CACHE PATH
            "CGNS include directory")
    endif()

    _cgns_get_imported_location("${_CGNS_CONFIG_TARGET}" _CGNS_CONFIG_LIBRARY)
    if(_CGNS_CONFIG_LIBRARY AND NOT CGNS_LIBRARY)
        set(CGNS_LIBRARY "${_CGNS_CONFIG_LIBRARY}" CACHE FILEPATH
            "CGNS library")
    endif()
endif()

find_path(CGNS_INCLUDE_DIR
    NAMES cgnslib.h
    HINTS "${CGNS_ROOT}" "$ENV{CGNS_ROOT}"
    PATH_SUFFIXES include
    DOC "CGNS include directory"
)

find_library(CGNS_LIBRARY
    NAMES cgns libcgns cgnsdll
    HINTS "${CGNS_ROOT}" "$ENV{CGNS_ROOT}"
    PATH_SUFFIXES lib lib64
    DOC "CGNS library"
)

if(CGNS_INCLUDE_DIR AND EXISTS "${CGNS_INCLUDE_DIR}/cgnslib.h")
    file(STRINGS "${CGNS_INCLUDE_DIR}/cgnslib.h" _CGNS_VERSION_LINE
        REGEX "^#define[ \t]+CGNS_DOTVERS[ \t]+[0-9.]+")
    string(REGEX REPLACE ".*CGNS_DOTVERS[ \t]+([0-9.]+).*" "\\1"
        CGNS_VERSION "${_CGNS_VERSION_LINE}")
else()
    set(CGNS_VERSION CGNS_VERSION-NOTFOUND)
endif()

include(FindPackageHandleStandardArgs)

set(_CGNS_REQUIRED_VARS CGNS_INCLUDE_DIR)
if(_CGNS_CONFIG_TARGET)
    set(_CGNS_TARGET "${_CGNS_CONFIG_TARGET}")
    list(APPEND _CGNS_REQUIRED_VARS _CGNS_TARGET)
else()
    list(APPEND _CGNS_REQUIRED_VARS CGNS_LIBRARY)
endif()

find_package_handle_standard_args(CGNS
    REQUIRED_VARS ${_CGNS_REQUIRED_VARS}
    VERSION_VAR CGNS_VERSION
    FAIL_MESSAGE "CGNS not found. Install it (e.g. 'apt install libcgns-dev') or set CGNS_ROOT to the CGNS installation directory."
)

if(CGNS_FOUND)
    set(CGNS_INCLUDE_DIRS "${CGNS_INCLUDE_DIR}")
    if(CGNS_LIBRARY)
        set(CGNS_LIBRARIES "${CGNS_LIBRARY}")
    else()
        set(CGNS_LIBRARIES "${_CGNS_CONFIG_TARGET}")
    endif()

    if(NOT TARGET CGNS::CGNS)
        if(CGNS_LIBRARY)
            add_library(CGNS::CGNS UNKNOWN IMPORTED)
            set_target_properties(CGNS::CGNS PROPERTIES
                IMPORTED_LOCATION "${CGNS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${CGNS_INCLUDE_DIR}")

            if(_CGNS_CONFIG_TARGET)
                get_target_property(_CGNS_CONFIG_TYPE "${_CGNS_CONFIG_TARGET}" TYPE)
                if(_CGNS_CONFIG_TYPE STREQUAL "STATIC_LIBRARY")
                    get_target_property(_CGNS_CONFIG_LINK_LIBRARIES
                        "${_CGNS_CONFIG_TARGET}" INTERFACE_LINK_LIBRARIES)
                    if(_CGNS_CONFIG_LINK_LIBRARIES)
                        _cgns_normalize_link_libraries(
                            _CGNS_CONFIG_LINK_LIBRARIES
                            ${_CGNS_CONFIG_LINK_LIBRARIES})
                        set_target_properties(CGNS::CGNS PROPERTIES
                            INTERFACE_LINK_LIBRARIES
                                "${_CGNS_CONFIG_LINK_LIBRARIES}")
                    endif()
                endif()
            endif()
        elseif(_CGNS_CONFIG_TARGET)
            add_library(CGNS::CGNS INTERFACE IMPORTED)
            set_target_properties(CGNS::CGNS PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_CGNS_CONFIG_TARGET}")
        endif()
    endif()
endif()

mark_as_advanced(CGNS_INCLUDE_DIR CGNS_LIBRARY)

unset(_CGNS_CONFIG_INCLUDE_DIR)
unset(_CGNS_CONFIG_LIBRARY)
unset(_CGNS_CONFIG_LINK_LIBRARIES)
unset(_CGNS_CONFIG_TARGET)
unset(_CGNS_CONFIG_TARGET_CANDIDATES)
unset(_CGNS_CONFIG_TYPE)
unset(_CGNS_REQUIRED_VARS)
unset(_CGNS_TARGET)
unset(_CGNS_VERSION_LINE)
