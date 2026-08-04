# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# CaeExternalDependencies.cmake
#
# Package lookup helpers for optional non-Python file-format dependencies.

function(_cae_external_add_interface_alias ALIAS_TARGET REAL_TARGET)
    if(TARGET "${ALIAS_TARGET}" OR NOT TARGET "${REAL_TARGET}")
        return()
    endif()

    add_library("${ALIAS_TARGET}" INTERFACE IMPORTED)
    set_target_properties("${ALIAS_TARGET}" PROPERTIES
        INTERFACE_LINK_LIBRARIES "${REAL_TARGET}"
    )
endfunction()

function(_cae_external_normalize_pugixml_target)
    if(TARGET pugixml::pugixml)
        return()
    endif()

    if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
        set(_candidates
            pugixml::static
            pugixml
            pugixml::shared)
    elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
        set(_candidates
            pugixml::shared
            pugixml
            pugixml::static)
    else()
        set(_candidates
            pugixml
            pugixml::shared
            pugixml::static)
    endif()
    foreach(_candidate IN LISTS _candidates)
        if(TARGET "${_candidate}")
            _cae_external_add_interface_alias(pugixml::pugixml "${_candidate}")
            return()
        endif()
    endforeach()

    if(pugixml_LIBRARIES)
        add_library(pugixml::pugixml INTERFACE IMPORTED)
        set_target_properties(pugixml::pugixml PROPERTIES
            INTERFACE_LINK_LIBRARIES "${pugixml_LIBRARIES}"
        )
        if(pugixml_INCLUDE_DIRS)
            set_target_properties(pugixml::pugixml PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${pugixml_INCLUDE_DIRS}"
            )
        endif()
    endif()
endfunction()

function(_cae_external_normalize_lz4_target)
    if(TARGET LZ4::LZ4)
        return()
    endif()

    if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
        set(_candidates
            LZ4::lz4_static
            lz4_static
            LZ4::lz4
            lz4
            LZ4::lz4_shared
            lz4_shared)
    elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
        set(_candidates
            LZ4::lz4_shared
            lz4_shared
            LZ4::lz4
            lz4
            LZ4::lz4_static
            lz4_static)
    else()
        set(_candidates
            LZ4::lz4
            LZ4::lz4_shared
            LZ4::lz4_static
            lz4
            lz4_shared
            lz4_static)
    endif()
    foreach(_candidate IN LISTS _candidates)
        if(TARGET "${_candidate}")
            _cae_external_add_interface_alias(LZ4::LZ4 "${_candidate}")
            return()
        endif()
    endforeach()

    if(LZ4_LIBRARIES)
        add_library(LZ4::LZ4 INTERFACE IMPORTED)
        set_target_properties(LZ4::LZ4 PROPERTIES
            INTERFACE_LINK_LIBRARIES "${LZ4_LIBRARIES}"
        )
        if(LZ4_INCLUDE_DIRS)
            set_target_properties(LZ4::LZ4 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LZ4_INCLUDE_DIRS}"
            )
        endif()
    endif()
endfunction()

function(_cae_external_normalize_liblzma_target)
    if(NOT TARGET LibLZMA::LibLZMA)
        return()
    endif()

    if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static" AND WIN32)
        set_property(TARGET LibLZMA::LibLZMA APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS LZMA_API_STATIC)
    endif()
endfunction()

function(_cae_external_normalize_hdf5_target)
    if(NOT TARGET HDF5::HDF5)
        if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
            set(_candidates
                hdf5::hdf5-static
                hdf5-static
                hdf5::hdf5
                hdf5::hdf5-shared
                hdf5-shared)
        elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
            set(_candidates
                hdf5::hdf5-shared
                hdf5-shared
                hdf5::hdf5
                hdf5::hdf5-static
                hdf5-static)
        else()
            set(_candidates
                hdf5::hdf5
                hdf5::hdf5-shared
                hdf5-shared
                hdf5::hdf5-static
                hdf5-static)
        endif()
        foreach(_candidate IN LISTS _candidates)
            if(TARGET ${_candidate})
                add_library(HDF5::HDF5 INTERFACE IMPORTED)
                set_target_properties(HDF5::HDF5 PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_candidate}"
                )
                break()
            endif()
        endforeach()

        if(NOT TARGET HDF5::HDF5 AND HDF5_LIBRARIES)
            add_library(HDF5::HDF5 INTERFACE IMPORTED)
            set_target_properties(HDF5::HDF5 PROPERTIES
                INTERFACE_LINK_LIBRARIES "${HDF5_LIBRARIES}"
            )
            if(HDF5_INCLUDE_DIRS)
                set_target_properties(HDF5::HDF5 PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${HDF5_INCLUDE_DIRS}"
                )
            endif()
        endif()
    endif()

    if(TARGET HDF5::HDF5
            AND WIN32
            AND CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
        if(NOT TARGET ZLIB::ZLIB)
            find_package(ZLIB REQUIRED)
        endif()

        get_target_property(_hdf5_link_libraries
            HDF5::HDF5 INTERFACE_LINK_LIBRARIES)
        if(NOT _hdf5_link_libraries)
            set(_hdf5_link_libraries)
        endif()
        list(FIND _hdf5_link_libraries "Shlwapi.lib" _hdf5_shlwapi_index)
        if(_hdf5_shlwapi_index EQUAL -1)
            # Static HDF5 2.x calls StrStrIA on Windows.
            set_property(TARGET HDF5::HDF5 APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES Shlwapi.lib)
        endif()

        list(FIND _hdf5_link_libraries "ZLIB::ZLIB" _hdf5_zlib_index)
        if(_hdf5_zlib_index EQUAL -1)
            # Static HDF5 2.x does not reliably propagate its private zlib
            # dependency on Windows.
            set_property(TARGET HDF5::HDF5 APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
        endif()
    endif()
endfunction()

function(_cae_external_normalize_cgns_target)
    if(TARGET HDF5::HDF5)
        _cae_external_normalize_hdf5_target()
    endif()

    if(TARGET CGNS::CGNS)
        return()
    endif()

    if(CAE_FORMAT_DEPS_LINKAGE STREQUAL "static")
        set(_candidates
            CGNS::cgns_static
            CGNS::cgns-static
            cgns_static
            cgns
            CGNS::cgns_shared
            CGNS::cgns-shared
            cgns_shared)
    elseif(CAE_FORMAT_DEPS_LINKAGE STREQUAL "shared")
        set(_candidates
            CGNS::cgns_shared
            CGNS::cgns-shared
            cgns_shared
            cgns
            CGNS::cgns_static
            CGNS::cgns-static
            cgns_static)
    else()
        set(_candidates
            CGNS::cgns_shared
            CGNS::cgns_static
            CGNS::cgns-shared
            CGNS::cgns-static
            cgns_shared
            cgns_static
            cgns)
    endif()
    foreach(_candidate IN LISTS _candidates)
        if(TARGET ${_candidate})
            add_library(CGNS::CGNS INTERFACE IMPORTED)
            set_target_properties(CGNS::CGNS PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_candidate}"
            )
            return()
        endif()
    endforeach()

    if(CGNS_LIBRARIES)
        add_library(CGNS::CGNS INTERFACE IMPORTED)
        set_target_properties(CGNS::CGNS PROPERTIES
            INTERFACE_LINK_LIBRARIES "${CGNS_LIBRARIES}"
        )
        if(CGNS_INCLUDE_DIRS)
            set_target_properties(CGNS::CGNS PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${CGNS_INCLUDE_DIRS}"
            )
        endif()
    endif()
endfunction()

function(cae_resolve_package PACKAGE_NAME)
    find_package(${PACKAGE_NAME} ${ARGN} REQUIRED)

    string(TOLOWER "${PACKAGE_NAME}" _package_name_lower)

    if(_package_name_lower STREQUAL "pugixml")
        _cae_external_normalize_pugixml_target()
        if(NOT TARGET pugixml::pugixml)
            message(FATAL_ERROR
                "pugixml was found, but no usable pugixml::pugixml target could be created")
        endif()
    elseif(_package_name_lower STREQUAL "lz4")
        _cae_external_normalize_lz4_target()
        if(NOT TARGET LZ4::LZ4)
            message(FATAL_ERROR
                "LZ4 was found, but no usable LZ4::LZ4 target could be created")
        endif()
    elseif(PACKAGE_NAME STREQUAL "LibLZMA")
        _cae_external_normalize_liblzma_target()
        if(NOT TARGET LibLZMA::LibLZMA)
            message(FATAL_ERROR
                "LibLZMA was found, but no usable LibLZMA::LibLZMA target could be created")
        endif()
    elseif(PACKAGE_NAME STREQUAL "HDF5")
        _cae_external_normalize_hdf5_target()
        if(NOT TARGET HDF5::HDF5)
            message(FATAL_ERROR
                "HDF5 was found, but no usable HDF5::HDF5 target could be created")
        endif()
    elseif(PACKAGE_NAME STREQUAL "CGNS")
        _cae_external_normalize_cgns_target()
        if(NOT TARGET CGNS::CGNS)
            message(FATAL_ERROR
                "CGNS was found, but no usable CGNS::CGNS target could be created")
        endif()
    endif()

    unset(_package_name_lower)
endfunction()
