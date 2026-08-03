# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

#[=======================================================================[.rst:
CaeUSDImpl
----------

Internal implementation detail of :module:`FindCaeUSD`. **Not a public entry
point** -- do not ``include()`` or ``find_package()`` this directly.

This file holds only *definitions*: the ``_cae_usd_*`` helper functions, the
``_cae_usd_status`` QUIET-aware logger, and the public
``cae_usd_resolve_libraries()`` command. It is guarded by
``include_guard(GLOBAL)`` so the definitions are parsed exactly once per CMake
run, even when ``FindCaeUSD.cmake`` is pulled in through both module-mode
``find_package(CaeUSD)`` and the generated ``CaeUSDConfig.cmake``.

The *discovery and synthesis* logic that actually runs (locating
``pxrTargets.cmake``, creating the ``CaeUSD::*`` targets, evaluating components)
lives in ``FindCaeUSD.cmake`` and is intentionally **not** guarded, so it
re-runs on every ``find_package(CaeUSD)`` call -- that is what lets a later call
with a different ``COMPONENTS`` set re-evaluate the per-component ``_FOUND``
variables. All target-creating statements there are individually idempotent
(``if(TARGET ...)`` / ``if(NOT TARGET ...)`` guards), so re-running is safe.
#]=======================================================================]

include_guard(GLOBAL)
include(FindPackageHandleStandardArgs)

# _cae_usd_status(<message>)
#
# Emit a STATUS message, honoring a QUIET find_package() request
# (find_package(CaeUSD ... QUIET) sets CaeUSD_FIND_QUIETLY). Warnings and fatal
# diagnostics are deliberately NOT routed through this helper -- they must
# surface regardless of QUIET.
function(_cae_usd_status _message)
    if(NOT CaeUSD_FIND_QUIETLY)
        message(STATUS "${_message}")
    endif()
endfunction()

# _cae_usd_find_pxr_targets(<out-var>)
#
# Locate the raw `pxrTargets.cmake` export. Searches the common install/export
# layouts under USD_ROOT and pxr_DIR first, then falls back to a find_file over
# the same path suffixes. Sets <out-var> to the resolved path, or "" if none.
# Deliberately never resolves pxrConfig.cmake -- importing the targets file
# directly is what keeps unrelated find_dependency() calls from firing.
function(_cae_usd_find_pxr_targets OUT)
    set(_candidate_roots "")
    if(USD_ROOT)
        list(APPEND _candidate_roots "${USD_ROOT}")
    endif()
    if(pxr_DIR)
        list(APPEND _candidate_roots "${pxr_DIR}")
    endif()

    foreach(_root IN LISTS _candidate_roots)
        foreach(_candidate IN ITEMS
                "${_root}/pxrTargets.cmake"
                "${_root}/cmake/pxrTargets.cmake"
                "${_root}/lib/cmake/pxr/pxrTargets.cmake"
                "${_root}/lib64/cmake/pxr/pxrTargets.cmake"
                "${_root}/share/cmake/pxr/pxrTargets.cmake"
                "${_root}/build/src/pxrTargets.cmake")
            if(EXISTS "${_candidate}")
                set(${OUT} "${_candidate}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()

    # find_file() requires a result variable that it caches; use a local name
    # and clear it from the cache afterwards so the result is recomputed on
    # reconfigure and does not pollute the cache/ccmake UI. NO_DEFAULT_PATH is
    # intentionally NOT used here: this is the discovery fallback that lets a
    # USD install on CMAKE_PREFIX_PATH be found when USD_ROOT/pxr_DIR are unset.
    find_file(_cae_usd_pxr_targets_file
        NAMES pxrTargets.cmake
        PATH_SUFFIXES
            cmake
            lib/cmake/pxr
            lib64/cmake/pxr
            share/cmake/pxr
            build/src
    )
    if(_cae_usd_pxr_targets_file)
        set(${OUT} "${_cae_usd_pxr_targets_file}" PARENT_SCOPE)
    else()
        set(${OUT} "" PARENT_SCOPE)
    endif()
    unset(_cae_usd_pxr_targets_file CACHE)
endfunction()

# _cae_usd_parse_exported_targets(<pxr-targets-file> <out-var>)
#
# Scan the targets file text for `add_library(<name>` declarations and return
# the de-duplicated list of names that resolved to real imported targets after
# the file was include()-d. This is the raw USD inventory every later stage
# treats as data rather than as link dependencies.
#
# NOTE: This couples to the textual shape of CMake's generated *Targets.cmake
# (one `add_library(<name> <type> IMPORTED)` per exported target). It degrades
# safely -- every parsed name is cross-checked with if(TARGET ...) -- but if a
# future USD release changes how the export file is generated, revisit this.
function(_cae_usd_parse_exported_targets PXR_TARGETS_FILE OUT)
    file(READ "${PXR_TARGETS_FILE}" _cae_usd_targets_text)
    string(REGEX MATCHALL "add_library\\([A-Za-z0-9_]+" _cae_usd_target_matches "${_cae_usd_targets_text}")

    set(_targets "")
    foreach(_match IN LISTS _cae_usd_target_matches)
        string(REGEX REPLACE "^add_library\\(" "" _target "${_match}")
        if(TARGET "${_target}")
            list(APPEND _targets "${_target}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    set(${OUT} ${_targets} PARENT_SCOPE)
endfunction()

# _cae_usd_target_location(<target> <out-var>)
#
# Resolve a usable on-disk library path for an imported target. On Windows,
# prefer IMPORTED_IMPLIB because that directory is the link-time search path;
# otherwise prefer IMPORTED_LOCATION. Falls back to config-less properties.
function(_cae_usd_target_location TARGET_NAME OUT)
    set(_location "")
    if(WIN32)
        set(_location_property_order IMPORTED_IMPLIB IMPORTED_LOCATION)
    else()
        set(_location_property_order IMPORTED_LOCATION IMPORTED_IMPLIB)
    endif()

    get_target_property(_configs "${TARGET_NAME}" IMPORTED_CONFIGURATIONS)
    if(_configs)
        foreach(_config IN LISTS _configs)
            foreach(_property IN LISTS _location_property_order)
                get_target_property(_candidate "${TARGET_NAME}" "${_property}_${_config}")
                if(_candidate)
                    set(_location "${_candidate}")
                    break()
                endif()
            endforeach()
            if(_location)
                break()
            endif()
        endforeach()
    endif()

    if(NOT _location)
        foreach(_property IN LISTS _location_property_order)
            get_target_property(_candidate "${TARGET_NAME}" "${_property}")
            if(_candidate)
                set(_location "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT _location)
        get_target_property(_candidate "${TARGET_NAME}" LOCATION)
        if(_candidate)
            set(_location "${_candidate}")
        endif()
    endif()

    set(${OUT} "${_location}" PARENT_SCOPE)
endfunction()

# _cae_usd_copy_target_property(<raw-target> <clean-target> <property>)
#
# Copy a single property from a raw USD target to its cleaned CaeUSD:: twin,
# but only when the source property is set (avoids writing "<prop>-NOTFOUND").
function(_cae_usd_copy_target_property RAW_TARGET CLEAN_TARGET PROPERTY_NAME)
    get_target_property(_value "${RAW_TARGET}" "${PROPERTY_NAME}")
    if(_value)
        set_target_properties("${CLEAN_TARGET}" PROPERTIES "${PROPERTY_NAME}" "${_value}")
    endif()
endfunction()

# _cae_usd_is_usd_include_dir(<include-dir> <out-var>)
#
# True iff <include-dir> is a concrete USD include root (contains pxr/).
# Generator expressions ($<...>) and empty inputs are rejected up front since
# they cannot be probed on disk at configure time.
function(_cae_usd_is_usd_include_dir INCLUDE_DIR OUT)
    if(NOT INCLUDE_DIR OR INCLUDE_DIR MATCHES "^\\$<")
        set(${OUT} FALSE PARENT_SCOPE)
        return()
    endif()

    get_filename_component(_abs_include_dir "${INCLUDE_DIR}" ABSOLUTE)
    if(EXISTS "${_abs_include_dir}/pxr")
        set(${OUT} TRUE PARENT_SCOPE)
    else()
        set(${OUT} FALSE PARENT_SCOPE)
    endif()
endfunction()

# _cae_usd_collect_include_dirs(<out-var>)
#
# Gather the USD include roots: USD_ROOT/include when present, plus every
# INTERFACE_INCLUDE_DIRECTORIES entry of the raw targets that looks like a USD
# include root. Returns absolute, de-duplicated paths.
function(_cae_usd_collect_include_dirs OUT)
    set(_include_dirs "")

    if(USD_ROOT AND EXISTS "${USD_ROOT}/include/pxr")
        list(APPEND _include_dirs "${USD_ROOT}/include")
    endif()

    foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
        if(TARGET "${_raw_target}")
            get_target_property(_includes "${_raw_target}" INTERFACE_INCLUDE_DIRECTORIES)
            if(_includes)
                foreach(_include IN LISTS _includes)
                    _cae_usd_is_usd_include_dir("${_include}" _is_usd_include_dir)
                    if(_is_usd_include_dir)
                        get_filename_component(_abs_include_dir "${_include}" ABSOLUTE)
                        list(APPEND _include_dirs "${_abs_include_dir}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()

    if(_include_dirs)
        list(REMOVE_DUPLICATES _include_dirs)
    endif()
    set(${OUT} ${_include_dirs} PARENT_SCOPE)
endfunction()

# _cae_usd_create_clean_target(<raw-target>)
#
# Synthesize the cleaned CaeUSD::<raw-target> imported target. Mirrors the raw
# target's library type and copies its location / implib / soname (per-config
# and config-less) plus compile definitions and options -- but NOT its broad
# INTERFACE_LINK_LIBRARIES, which is the whole point of the clean layer. The
# USD include dirs are attached from USD_INCLUDE_DIRS. Missing consumer build
# configs are mapped to the first available imported config so Debug consumers
# can link a RelWithDebInfo-only USD export.
function(_cae_usd_create_clean_target RAW_TARGET)
    if(NOT TARGET "${RAW_TARGET}")
        return()
    endif()

    set(_clean_target "CaeUSD::${RAW_TARGET}")
    if(TARGET "${_clean_target}")
        return()
    endif()

    get_target_property(_type "${RAW_TARGET}" TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
        add_library("${_clean_target}" STATIC IMPORTED GLOBAL)
    elseif(_type STREQUAL "MODULE_LIBRARY")
        add_library("${_clean_target}" MODULE IMPORTED GLOBAL)
    elseif(_type STREQUAL "INTERFACE_LIBRARY")
        add_library("${_clean_target}" INTERFACE IMPORTED GLOBAL)
    elseif(_type STREQUAL "UNKNOWN_LIBRARY")
        add_library("${_clean_target}" UNKNOWN IMPORTED GLOBAL)
    else()
        add_library("${_clean_target}" SHARED IMPORTED GLOBAL)
    endif()

    foreach(_property IN ITEMS
            INTERFACE_COMPILE_DEFINITIONS
            INTERFACE_COMPILE_OPTIONS
            IMPORTED_CONFIGURATIONS
            IMPORTED_LOCATION
            IMPORTED_IMPLIB
            IMPORTED_SONAME
            INTERFACE_LINK_DIRECTORIES)
        _cae_usd_copy_target_property("${RAW_TARGET}" "${_clean_target}" "${_property}")
    endforeach()

    if(USD_INCLUDE_DIRS)
        set_target_properties("${_clean_target}" PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${USD_INCLUDE_DIRS}")
    endif()

    get_target_property(_configs "${RAW_TARGET}" IMPORTED_CONFIGURATIONS)
    if(_configs)
        foreach(_config IN LISTS _configs)
            foreach(_property_base IN ITEMS
                    IMPORTED_LOCATION
                    IMPORTED_IMPLIB
                    IMPORTED_SONAME
                    IMPORTED_NO_SONAME)
                _cae_usd_copy_target_property(
                    "${RAW_TARGET}" "${_clean_target}" "${_property_base}_${_config}")
            endforeach()
        endforeach()

        list(GET _configs 0 _first_config)
        foreach(_consumer_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            list(FIND _configs "${_consumer_config}" _has_consumer_config)
            if(_has_consumer_config EQUAL -1)
                set_target_properties("${_clean_target}" PROPERTIES
                    "MAP_IMPORTED_CONFIG_${_consumer_config}" "${_first_config}")
            endif()
        endforeach()
    endif()
endfunction()

# _cae_usd_append_python_support()
#
# When the USD export includes a `python` target, resolve Python3::Python (via
# find_package(Python3), then provider hints) and attach it to CaeUSD::python.
# Propagates the resolved Python3_* variables to the caller and sets
# CaeUSD_HAS_PYTHON. USD's common headers include pySafePython.h, so Python
# support is provider-neutral, not a Packman-only concern.
function(_cae_usd_append_python_support)
    if(NOT TARGET CaeUSD::python)
        return()
    endif()

    if(NOT TARGET Python3::Python)
        find_package(Python3 COMPONENTS Interpreter Development QUIET)
    endif()

    if(NOT TARGET Python3::Python)
        _cae_usd_define_python_from_hints()
    endif()

    if(TARGET Python3::Python)
        set(CaeUSD_HAS_PYTHON TRUE PARENT_SCOPE)
        foreach(_python_var IN ITEMS
                Python3_FOUND
                Python3_EXECUTABLE
                Python3_VERSION
                Python3_VERSION_MAJOR
                Python3_VERSION_MINOR
                Python3_VERSION_PATCH
                Python3_INCLUDE_DIRS
                Python3_LIBRARIES
                Python3_ROOT_DIR)
            if(DEFINED ${_python_var})
                set(${_python_var} "${${_python_var}}" PARENT_SCOPE)
            endif()
        endforeach()
        target_link_libraries(CaeUSD::python INTERFACE Python3::Python)
        if(Python3_INCLUDE_DIRS)
            set_property(TARGET CaeUSD::python APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}")
        endif()
        _cae_usd_status("Found Python3 ${Python3_VERSION}: ${Python3_EXECUTABLE}")
    else()
        set(CaeUSD_HAS_PYTHON FALSE PARENT_SCOPE)
        message(WARNING
            "USD exports a Python target, but Python3::Python could not be resolved. "
            "Python-enabled USD consumers may fail to compile or link.")
    endif()
endfunction()

# _cae_usd_define_python_from_hints()
#
# Last-resort Python3 resolution from Python3_ROOT_DIR / Python3_EXECUTABLE
# hints when find_package(Python3) failed (common for relocatable Packman
# Python). Globs for the interpreter, Python.h, and the import/shared library,
# then synthesizes Python3::Interpreter / Python3::Python imported targets.
function(_cae_usd_define_python_from_hints)
    if(NOT Python3_ROOT_DIR AND Python3_EXECUTABLE)
        get_filename_component(Python3_ROOT_DIR "${Python3_EXECUTABLE}/.." DIRECTORY)
    endif()
    if(NOT Python3_ROOT_DIR)
        return()
    endif()

    if(NOT Python3_EXECUTABLE)
        foreach(_candidate IN ITEMS
                "${Python3_ROOT_DIR}/bin/python3"
                "${Python3_ROOT_DIR}/bin/python"
                "${Python3_ROOT_DIR}/python.exe"
                "${Python3_ROOT_DIR}/python")
            if(EXISTS "${_candidate}")
                set(Python3_EXECUTABLE "${_candidate}" CACHE FILEPATH "Python executable")
                break()
            endif()
        endforeach()
    endif()

    if(NOT Python3_INCLUDE_DIRS)
        file(GLOB _include_candidates
            "${Python3_ROOT_DIR}/include/python*"
            "${Python3_ROOT_DIR}/include")
        foreach(_candidate IN LISTS _include_candidates)
            if(EXISTS "${_candidate}/Python.h")
                set(Python3_INCLUDE_DIRS "${_candidate}" CACHE PATH "Python include directory")
                break()
            endif()
        endforeach()
    endif()

    if(NOT Python3_LIBRARIES)
        if(WIN32)
            file(GLOB _library_candidates
                "${Python3_ROOT_DIR}/libs/python*.lib"
                "${Python3_ROOT_DIR}/lib/python*.lib"
                "${Python3_ROOT_DIR}/python*.lib")
        elseif(APPLE)
            file(GLOB _library_candidates
                "${Python3_ROOT_DIR}/lib/libpython*.dylib"
                "${Python3_ROOT_DIR}/lib/libpython*.a")
        else()
            file(GLOB _library_candidates
                "${Python3_ROOT_DIR}/lib/libpython*.so"
                "${Python3_ROOT_DIR}/lib64/libpython*.so"
                "${Python3_ROOT_DIR}/lib/libpython*.a")
        endif()
        if(_library_candidates)
            list(SORT _library_candidates)
            list(GET _library_candidates 0 _python_library)
            set(Python3_LIBRARIES "${_python_library}" CACHE FILEPATH "Python library")
        endif()
    endif()

    if(WIN32)
        file(GLOB _python_dll_candidates
            "${Python3_ROOT_DIR}/python*.dll"
            "${Python3_ROOT_DIR}/bin/python*.dll"
            "${Python3_ROOT_DIR}/DLLs/python*.dll")
        if(_python_dll_candidates)
            list(SORT _python_dll_candidates)
            list(GET _python_dll_candidates 0 _python_dll)
        endif()
    endif()

    if(Python3_EXECUTABLE AND Python3_INCLUDE_DIRS AND Python3_LIBRARIES)
        set(Python3_FOUND TRUE CACHE BOOL "Python3 found")
        set(Python3_Development_FOUND TRUE CACHE BOOL "Python3 Development found")
        if(NOT TARGET Python3::Interpreter)
            add_executable(Python3::Interpreter IMPORTED GLOBAL)
            set_target_properties(Python3::Interpreter PROPERTIES
                IMPORTED_LOCATION "${Python3_EXECUTABLE}")
        endif()

        if(NOT TARGET Python3::Python)
            add_library(Python3::Python SHARED IMPORTED GLOBAL)
            set_target_properties(Python3::Python PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}")
            if(WIN32)
                set_target_properties(Python3::Python PROPERTIES
                    IMPORTED_IMPLIB "${Python3_LIBRARIES}")
                if(_python_dll)
                    set_target_properties(Python3::Python PROPERTIES
                        IMPORTED_LOCATION "${_python_dll}")
                endif()
            else()
                set_target_properties(Python3::Python PROPERTIES
                    IMPORTED_LOCATION "${Python3_LIBRARIES}")
            endif()
        endif()
    endif()
endfunction()

# _cae_usd_raw_target_links_to(<raw-target> <dependency> <out-var>)
#
# True iff <raw-target>'s INTERFACE_LINK_LIBRARIES names <dependency>, unwrapping
# $<LINK_ONLY:...> generator-expression entries. Used to decide which clean
# targets actually need TBB on Windows.
function(_cae_usd_raw_target_links_to RAW_TARGET DEPENDENCY OUT)
    set(_found FALSE)
    if(TARGET "${RAW_TARGET}")
        get_target_property(_deps "${RAW_TARGET}" INTERFACE_LINK_LIBRARIES)
        if(_deps)
            foreach(_dep IN LISTS _deps)
                set(_dep_name "${_dep}")
                if(_dep_name MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
                    set(_dep_name "${CMAKE_MATCH_1}")
                endif()
                if(_dep_name STREQUAL "${DEPENDENCY}")
                    set(_found TRUE)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    set(${OUT} "${_found}" PARENT_SCOPE)
endfunction()

# _cae_usd_define_tbb_from_hints()
#
# Synthesize a TBB::tbb imported target from libraries/headers found near
# USD_ROOT or the raw USD include dirs, when no TBB CMake package is available.
# Windows-only support concern (MSVC import libs may carry /DEFAULTLIB:tbb).
function(_cae_usd_define_tbb_from_hints)
    if(TARGET TBB::tbb)
        return()
    endif()

    set(_cae_tbb_roots "")
    if(USD_ROOT)
        list(APPEND _cae_tbb_roots "${USD_ROOT}")
    endif()

    foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
        if(TARGET "${_raw_target}")
            get_target_property(_includes "${_raw_target}" INTERFACE_INCLUDE_DIRECTORIES)
            if(_includes)
                foreach(_include IN LISTS _includes)
                    if(EXISTS "${_include}/tbb/tbb.h" OR EXISTS "${_include}/oneapi/tbb.h")
                        get_filename_component(_root "${_include}/.." ABSOLUTE)
                        list(APPEND _cae_tbb_roots "${_root}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()

    if(_cae_tbb_roots)
        list(REMOVE_DUPLICATES _cae_tbb_roots)
    endif()

    find_library(_cae_tbb_implib
        NAMES tbb12 tbb
        PATHS ${_cae_tbb_roots}
        PATH_SUFFIXES lib bin
        NO_DEFAULT_PATH)
    find_file(_cae_tbb_dll
        NAMES tbb12.dll tbb.dll
        PATHS ${_cae_tbb_roots}
        PATH_SUFFIXES bin lib
        NO_DEFAULT_PATH)
    find_path(_cae_tbb_include_dir
        NAMES tbb/tbb.h oneapi/tbb.h
        PATHS ${_cae_tbb_roots}
        PATH_SUFFIXES include
        NO_DEFAULT_PATH)

    if(_cae_tbb_implib OR _cae_tbb_dll)
        add_library(TBB::tbb SHARED IMPORTED GLOBAL)
        if(_cae_tbb_implib)
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_IMPLIB "${_cae_tbb_implib}")
        endif()
        if(_cae_tbb_dll)
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_LOCATION "${_cae_tbb_dll}")
        elseif(_cae_tbb_implib)
            set_target_properties(TBB::tbb PROPERTIES
                IMPORTED_LOCATION "${_cae_tbb_implib}")
        endif()
        if(_cae_tbb_include_dir)
            set_target_properties(TBB::tbb PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_cae_tbb_include_dir}")
        endif()
    endif()

    unset(_cae_tbb_dll CACHE)
    unset(_cae_tbb_implib CACHE)
    unset(_cae_tbb_include_dir CACHE)
endfunction()

# _cae_usd_append_windows_import_dependencies()
#
# Windows-only: if any raw USD target references TBB::tbb, resolve it (TBB CMake
# package, then hint-based synthesis) and attach it to the clean targets that
# need it. Prevents MSVC "cannot open file 'tbb.lib'" link failures driven by
# /DEFAULTLIB directives in USD import libraries. No-op elsewhere.
function(_cae_usd_append_windows_import_dependencies)
    if(NOT WIN32)
        return()
    endif()

    set(_needs_tbb FALSE)
    foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
        _cae_usd_raw_target_links_to("${_raw_target}" TBB::tbb _target_needs_tbb)
        if(_target_needs_tbb)
            set(_needs_tbb TRUE)
            break()
        endif()
    endforeach()

    if(NOT _needs_tbb)
        return()
    endif()

    if(NOT TARGET TBB::tbb)
        set(_tbb_package_hints "")
        if(TBB_DIR)
            list(APPEND _tbb_package_hints "${TBB_DIR}")
        endif()
        if(pxr_DIR)
            list(APPEND _tbb_package_hints
                "${pxr_DIR}/cmake-packages/TBB"
                "${pxr_DIR}/cmake-packages")
        endif()
        if(USD_ROOT)
            list(APPEND _tbb_package_hints
                "${USD_ROOT}/lib/cmake/TBB"
                "${USD_ROOT}/cmake/TBB"
                "${USD_ROOT}")
        endif()

        if(_tbb_package_hints)
            find_package(TBB CONFIG QUIET PATHS ${_tbb_package_hints})
        else()
            find_package(TBB CONFIG QUIET)
        endif()
    endif()

    if(NOT TARGET TBB::tbb)
        _cae_usd_define_tbb_from_hints()
    endif()

    if(TARGET TBB::tbb)
        foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
            _cae_usd_raw_target_links_to("${_raw_target}" TBB::tbb _target_needs_tbb)
            if(_target_needs_tbb AND TARGET "CaeUSD::${_raw_target}")
                target_link_libraries("CaeUSD::${_raw_target}" INTERFACE TBB::tbb)
            endif()
        endforeach()
        set(CaeUSD_HAS_TBB TRUE PARENT_SCOPE)
        _cae_usd_status("Found TBB support for Windows USD import libraries")
    else()
        set(CaeUSD_HAS_TBB FALSE PARENT_SCOPE)
        message(WARNING
            "USD import libraries reference TBB, but TBB::tbb could not be resolved. "
            "MSVC links may fail with 'cannot open file tbb.lib'.")
    endif()
endfunction()

# _cae_usd_collect_closure(<out-var> <raw-target>...)
#
# Return the CaeUSD:: targets forming the USD-only dependency closure of the
# given raw target roots, walking INTERFACE_LINK_LIBRARIES but following only
# edges to other raw USD targets (non-USD deps such as TBB / Python are pruned).
function(_cae_usd_collect_closure OUT)
    set(_seen "")
    set(_result "")
    foreach(_root IN LISTS ARGN)
        _cae_usd_collect_closure_impl("${_root}" _seen _result)
    endforeach()
    set(${OUT} ${_result} PARENT_SCOPE)
endfunction()

# _cae_usd_collect_closure_impl(<raw-target> <seen-var> <result-var>)
#
# Recursive worker for _cae_usd_collect_closure(). SEEN_VAR (visited set, cycle
# guard) and RESULT_VAR (accumulated CaeUSD:: targets) are passed by name and
# threaded through PARENT_SCOPE so recursion accumulates across branches.
function(_cae_usd_collect_closure_impl RAW_TARGET SEEN_VAR RESULT_VAR)
    set(_seen "${${SEEN_VAR}}")
    set(_result "${${RESULT_VAR}}")

    if(NOT "${RAW_TARGET}" IN_LIST _CAE_USD_RAW_TARGETS)
        set(${SEEN_VAR} ${_seen} PARENT_SCOPE)
        set(${RESULT_VAR} ${_result} PARENT_SCOPE)
        return()
    endif()
    if(NOT TARGET "${RAW_TARGET}")
        set(${SEEN_VAR} ${_seen} PARENT_SCOPE)
        set(${RESULT_VAR} ${_result} PARENT_SCOPE)
        return()
    endif()

    list(FIND _seen "${RAW_TARGET}" _already_seen)
    if(NOT _already_seen EQUAL -1)
        set(${SEEN_VAR} ${_seen} PARENT_SCOPE)
        set(${RESULT_VAR} ${_result} PARENT_SCOPE)
        return()
    endif()

    list(APPEND _seen "${RAW_TARGET}")
    if(TARGET "CaeUSD::${RAW_TARGET}")
        list(APPEND _result "CaeUSD::${RAW_TARGET}")
    endif()

    get_target_property(_deps "${RAW_TARGET}" INTERFACE_LINK_LIBRARIES)
    if(_deps)
        foreach(_dep IN LISTS _deps)
            set(_dep_name "${_dep}")
            if(_dep_name MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
                set(_dep_name "${CMAKE_MATCH_1}")
            endif()
            if(_dep_name IN_LIST _CAE_USD_RAW_TARGETS)
                set(${SEEN_VAR} ${_seen})
                set(${RESULT_VAR} ${_result})
                _cae_usd_collect_closure_impl("${_dep_name}" ${SEEN_VAR} ${RESULT_VAR})
                set(_seen "${${SEEN_VAR}}")
                set(_result "${${RESULT_VAR}}")
            endif()
        endforeach()
    endif()

    set(${SEEN_VAR} ${_seen} PARENT_SCOPE)
    set(${RESULT_VAR} ${_result} PARENT_SCOPE)
endfunction()

# _cae_usd_read_pxr_config_value(<pxr-var-name> <out-var>)
#
# Extract a `set(<pxr-var-name> ...)` value from pxrConfig.cmake by reading it
# as text (file(STRINGS) + regex) -- it is never include()-d, so no
# find_dependency() side effects fire. Used only to recover the USD version.
function(_cae_usd_read_pxr_config_value VAR_NAME OUT)
    set(_config_candidates "")
    if(USD_ROOT)
        list(APPEND _config_candidates
            "${USD_ROOT}/pxrConfig.cmake"
            "${USD_ROOT}/lib/cmake/pxr/pxrConfig.cmake"
            "${USD_ROOT}/lib64/cmake/pxr/pxrConfig.cmake")
    endif()
    if(_CAE_USD_PXR_TARGETS_FILE)
        get_filename_component(_targets_dir "${_CAE_USD_PXR_TARGETS_FILE}" DIRECTORY)
        list(APPEND _config_candidates
            "${_targets_dir}/pxrConfig.cmake"
            "${_targets_dir}/../pxrConfig.cmake"
            "${_targets_dir}/../../pxrConfig.cmake"
            "${_targets_dir}/../../../pxrConfig.cmake")
    endif()

    foreach(_config IN LISTS _config_candidates)
        if(EXISTS "${_config}")
            file(STRINGS "${_config}" _matches REGEX "^[ \t]*set\\(${VAR_NAME}[ \t]+\"?[^\")]+\"?\\)")
            if(_matches)
                list(GET _matches 0 _line)
                string(REGEX REPLACE "^[ \t]*set\\(${VAR_NAME}[ \t]+\"?([^\"\\)]+)\"?\\).*" "\\1" _value "${_line}")
                set(${OUT} "${_value}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()

    set(${OUT} "" PARENT_SCOPE)
endfunction()

# cae_usd_resolve_libraries(<out-var> <usd-lib-name>...)
#
# PUBLIC. Map plain USD library names (or already-namespaced CaeUSD::<name>) to
# the cleaned CaeUSD:: link targets, expanding each name's USD-only closure and
# de-duplicating. For monolithic builds every name resolves to
# CaeUSD_MONOLITHIC_TARGET. Fails with a clear diagnostic on a non-USD namespaced
# target (those belong in the helper macros' LIBRARIES argument) or an unknown
# USD library name. This is the single chokepoint the helper macros use to turn
# USD_LIBRARIES arguments into link dependencies.
#
# Reads _CAE_USD_RAW_TARGETS and CaeUSD_MONOLITHIC_TARGET at call time; these are
# set by FindCaeUSD.cmake's discovery pass in the find_package() caller scope.
function(cae_usd_resolve_libraries OUT)
    set(_resolved "")
    foreach(_library IN LISTS ARGN)
        if(NOT _library)
            continue()
        endif()

        set(_raw_library "${_library}")
        if(_raw_library MATCHES "^CaeUSD::(.+)$")
            set(_raw_library "${CMAKE_MATCH_1}")
        elseif(_raw_library MATCHES "::")
            message(FATAL_ERROR
                "cae_usd_resolve_libraries(${OUT}): '${_library}' is not a USD "
                "library name. Pass plain USD library names such as 'sdf' or "
                "'usdGeom' to USD_LIBRARIES and pass non-USD targets through "
                "the helper's LIBRARIES argument.")
        endif()

        if(CaeUSD_MONOLITHIC_TARGET)
            list(APPEND _resolved "${CaeUSD_MONOLITHIC_TARGET}")
            continue()
        endif()

        if(NOT "${_raw_library}" IN_LIST _CAE_USD_RAW_TARGETS)
            message(FATAL_ERROR
                "cae_usd_resolve_libraries(${OUT}): unknown USD library "
                "'${_raw_library}'. Available USD targets: ${_CAE_USD_RAW_TARGETS}")
        endif()

        _cae_usd_collect_closure(_library_closure "${_raw_library}")
        list(APPEND _resolved ${_library_closure})
    endforeach()

    if(_resolved)
        list(REMOVE_DUPLICATES _resolved)
    endif()
    set(${OUT} ${_resolved} PARENT_SCOPE)
endfunction()
