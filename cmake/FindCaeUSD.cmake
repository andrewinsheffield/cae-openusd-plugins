# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

#[=======================================================================[.rst:
FindCaeUSD
----------

Provider-neutral USD normalization layer for ``cae-openusd-plugins``.

OpenUSD's own ``pxrConfig.cmake`` exposes too much of the build's internal
dependency graph: it eagerly ``find_dependency()``-s things a plugin may never
use (MaterialX, OpenSubdiv, ...), and its raw targets publish broad
``INTERFACE_LINK_LIBRARIES`` (Python, TBB, Hydra, GL, ...). This module
sidesteps that by importing ``pxrTargets.cmake`` *directly* and treating the
raw USD targets as inventory data only. It then synthesizes cleaned
``CaeUSD::<usd-library-name>`` imported targets that carry just the library
location, include dirs, and compile definitions -- not the over-wide interface
link closure. See ``docs/development/openusd_integration.md`` for the public
design and integration contract.

This file is the shared implementation behind both module-mode
``find_package(CaeUSD)`` and the generated ``CaeUSDConfig.cmake`` (config mode).

Module layout
^^^^^^^^^^^^^

The implementation is split across two files so that re-running
``find_package(CaeUSD)`` stays correct:

``CaeUSDImpl.cmake``
  All function *definitions* (the ``_cae_usd_*`` helpers and the public
  ``cae_usd_resolve_libraries()`` command). Guarded by ``include_guard(GLOBAL)``
  so it parses exactly once, even when this module is pulled in through both
  module mode and ``CaeUSDConfig.cmake``.
``FindCaeUSD.cmake`` (this file)
  The discovery and synthesis logic that actually runs. Deliberately **not**
  guarded: it re-runs on every ``find_package(CaeUSD)`` call so a later call
  with a different ``COMPONENTS`` set re-evaluates the per-component ``_FOUND``
  variables. Every target-creating statement is individually idempotent
  (``if(TARGET ...)`` / ``if(NOT TARGET ...)``), so re-running is safe; even the
  ``include(pxrTargets.cmake)`` is a no-op on re-entry (CMake's generated export
  files early-return once all their targets exist).

Input / hint variables
^^^^^^^^^^^^^^^^^^^^^^^^

``USD_ROOT``
  Root of the USD install/export (cache ``PATH``). Falls back to the
  ``USD_ROOT`` environment variable. If unset, derived from the discovered
  include directory.
``pxr_DIR``
  Alternative hint searched for ``pxrTargets.cmake`` when ``USD_ROOT`` does not
  resolve.
``TBB_DIR``
  Hint for locating a TBB CMake package (Windows import-library support only).
``Python3_ROOT_DIR`` / ``Python3_EXECUTABLE``
  Provider hints used to synthesize ``Python3::Python`` when
  ``find_package(Python3)`` cannot resolve it (e.g. Packman builds).
``CAE_USDGENSCHEMA_PYTHON_PACKAGES`` / ``CAE_USDGENSCHEMA_PYTHON_IMPORTS``
  pip package specs / import names the ``usdGenSchema`` tool runtime needs
  (cache ``STRING``; consumed by the tool trampoline).
``CAE_USD_TOOL_TRAMPOLINE`` / ``CAE_USD_TOOL_STATE_DIR``
  Path to the script-mode tool launcher and its build-local state directory.

Components
^^^^^^^^^^

Components are *capability gates*, not USD-library bundles -- they answer "can
this project build this class of USD integration?". The concrete link interface
still comes from explicit ``CaeUSD::*`` targets or the helper macros'
``USD_LIBRARIES`` argument.

``Schema``
  C++ schema authoring: a schema runtime library (``CaeUSD::usd`` or a
  monolithic target) plus a usable ``usdGenSchema``.
``FileFormat``
  ``SdfFileFormat`` plugin support: the core file-format USD libraries
  (``CaeUSD::sdf``/``CaeUSD::pcp``/``CaeUSD::usd`` or a monolithic target).
``Python``
  A Python-enabled USD install with a resolvable ``Python3::Python``.

``Schema`` does not imply ``Python`` and vice versa; they compose freely.

Result variables
^^^^^^^^^^^^^^^^^

``CaeUSD_FOUND``
  True if the required components and variables were satisfied.
``CaeUSD_<Component>_FOUND``
  Per-component capability result (``Schema`` / ``FileFormat`` / ``Python``).
``USD_INCLUDE_DIRS``
  USD include directories (each contains ``pxr/``).
``USD_LIBRARY_DIR``
  Directory containing the resolved USD shared libraries.
``USD_PLUGIN_DIR``
  ``<USD_ROOT>/plugin/usd`` -- conventional plugin install/lookup root.
``USD_VERSION``
  USD version string, parsed from ``pxrConfig.cmake`` (read as text, never
  ``include()``-d).
``USDGENSCHEMA_EXECUTABLE``
  Path to the ``usdGenSchema`` code generator, if found.
``CaeUSD_MONOLITHIC_TARGET``
  ``CaeUSD::usd_ms`` / ``CaeUSD::usd_m`` for monolithic builds; empty for
  componentized builds.
``CaeUSD_HAS_PYTHON`` / ``CaeUSD_HAS_TBB``
  Whether optional Python / TBB support was wired up.

Imported targets
^^^^^^^^^^^^^^^^^

``CaeUSD::Headers``
  ``INTERFACE`` target carrying only USD include dirs and compile definitions.
``CaeUSD::<usd-library-name>``
  One cleaned imported target per exported USD library (``CaeUSD::sdf``,
  ``CaeUSD::usdGeom``, ...). For monolithic builds these resolve through
  ``CaeUSD_MONOLITHIC_TARGET``.

Provided commands
^^^^^^^^^^^^^^^^^

``cae_usd_resolve_libraries(<out-var> <usd-lib-name>...)``
  Map plain USD library names (``sdf``, ``usdGeom``, ...) to the cleaned
  ``CaeUSD::*`` targets, expanding each name's USD-only dependency closure.
  Rejects namespaced / non-USD targets. This module also ``include()``-s
  ``USDPluginHelpers.cmake``, which layers ``cae_add_schema()`` /
  ``cae_add_file_format()`` / ``cae_usd_gen_schema()`` on top of this resolver.
#]=======================================================================]

# Function definitions live in the guarded implementation file so they are
# parsed once; the discovery/synthesis below re-runs on every find_package().
include("${CMAKE_CURRENT_LIST_DIR}/CaeUSDImpl.cmake")

set(_CAE_USD_KNOWN_COMPONENTS Schema FileFormat Python)

set(CAE_USD_TOOL_TRAMPOLINE
    "${CMAKE_CURRENT_LIST_DIR}/CaeUSDToolTrampoline.cmake"
    CACHE FILEPATH "CaeUSD CMake script-mode tool trampoline")
set(CAE_USD_TOOL_STATE_DIR
    "${CMAKE_BINARY_DIR}/_cae-usd-tools"
    CACHE PATH "Build-local state for CaeUSD tool runtimes")
set(CAE_USDGENSCHEMA_PYTHON_PACKAGES
    "Jinja2==3.1.6;MarkupSafe==3.0.3"
    CACHE STRING "Python packages required by the usdGenSchema tool")
set(CAE_USDGENSCHEMA_PYTHON_IMPORTS
    "jinja2;markupsafe"
    CACHE STRING "Python imports used to verify the usdGenSchema tool environment")
mark_as_advanced(
    CAE_USD_TOOL_TRAMPOLINE
    CAE_USD_TOOL_STATE_DIR
    CAE_USDGENSCHEMA_PYTHON_PACKAGES
    CAE_USDGENSCHEMA_PYTHON_IMPORTS)

if(NOT USD_ROOT AND DEFINED ENV{USD_ROOT})
    set(USD_ROOT "$ENV{USD_ROOT}" CACHE PATH "Root of USD installation")
endif()

# --- Provider import: locate and directly include pxrTargets.cmake ----------

_cae_usd_find_pxr_targets(_CAE_USD_PXR_TARGETS_FILE)

if(NOT _CAE_USD_PXR_TARGETS_FILE)
    message(FATAL_ERROR
        "CaeUSD requires direct import of pxrTargets.cmake. "
        "Set USD_ROOT or pxr_DIR to a USD install/export directory containing "
        "pxrTargets.cmake; pxrConfig.cmake is intentionally not used because it "
        "can force unrelated dependency discovery before clean CaeUSD targets "
        "are synthesized.")
endif()

include("${_CAE_USD_PXR_TARGETS_FILE}")
_cae_usd_parse_exported_targets("${_CAE_USD_PXR_TARGETS_FILE}" _CAE_USD_RAW_TARGETS)
_cae_usd_status("Imported USD targets directly: ${_CAE_USD_PXR_TARGETS_FILE}")

# --- USD inventory: include dirs and derived USD_ROOT -----------------------

_cae_usd_collect_include_dirs(USD_INCLUDE_DIRS)

if(NOT USD_INCLUDE_DIRS)
    message(FATAL_ERROR
        "CaeUSD could not find a usable USD include root from ${_CAE_USD_PXR_TARGETS_FILE}. "
        "Expected an include directory containing pxr/.")
endif()

if(NOT USD_ROOT AND USD_INCLUDE_DIRS)
    list(GET USD_INCLUDE_DIRS 0 _cae_usd_first_include_dir)
    get_filename_component(_cae_usd_root "${_cae_usd_first_include_dir}/.." ABSOLUTE)
    set(USD_ROOT "${_cae_usd_root}" CACHE PATH "USD install root (derived from USD include dirs)" FORCE)
endif()

# --- Clean target synthesis -------------------------------------------------

foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
    _cae_usd_create_clean_target("${_raw_target}")
endforeach()

if(NOT TARGET CaeUSD::Headers)
    add_library(CaeUSD::Headers INTERFACE IMPORTED GLOBAL)
    if(USD_INCLUDE_DIRS)
        set_target_properties(CaeUSD::Headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${USD_INCLUDE_DIRS}")
    endif()
endif()

set(USD_LIBRARY_DIR "${USD_ROOT}/lib")
foreach(_cae_usd_location_target IN ITEMS usd usd_m usd_ms sdf arch)
    if(TARGET "${_cae_usd_location_target}")
        _cae_usd_target_location("${_cae_usd_location_target}" _cae_usd_location)
        if(_cae_usd_location)
            get_filename_component(USD_LIBRARY_DIR "${_cae_usd_location}" DIRECTORY)
            break()
        endif()
    endif()
endforeach()

# Windows USD import libraries can carry /DEFAULTLIB entries for sibling
# dependency import libraries such as tbb12.lib. The clean CaeUSD targets link
# USD libraries by absolute path, so MSVC still needs the provider lib directory
# on the link search path for those default-library records to resolve.
if(WIN32 AND USD_LIBRARY_DIR)
    foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
        if(TARGET "CaeUSD::${_raw_target}")
            set_property(TARGET "CaeUSD::${_raw_target}" APPEND PROPERTY
                INTERFACE_LINK_DIRECTORIES "${USD_LIBRARY_DIR}")
        endif()
    endforeach()
endif()

set(USD_PLUGIN_DIR "${USD_ROOT}/plugin/usd")

if(TARGET python)
    _cae_usd_append_python_support()
endif()
_cae_usd_append_windows_import_dependencies()

# Python-enabled USD headers can include pySafePython.h from ordinary C++ USD
# headers such as VtValue and SdfLayer. Treat Python's include directory as a
# provider include path so helper libraries that include USD headers directly
# do not need to know whether the provider was Python-enabled.
if(CaeUSD_HAS_PYTHON AND Python3_INCLUDE_DIRS)
    list(APPEND USD_INCLUDE_DIRS ${Python3_INCLUDE_DIRS})
    list(REMOVE_DUPLICATES USD_INCLUDE_DIRS)
    set(_cae_usd_python_link_dir "")
    if(WIN32 AND Python3_LIBRARIES)
        list(GET Python3_LIBRARIES 0 _cae_usd_python_library)
        get_filename_component(_cae_usd_python_link_dir "${_cae_usd_python_library}" DIRECTORY)
    elseif(WIN32 AND TARGET Python3::Python)
        foreach(_cae_usd_python_property IN ITEMS
                IMPORTED_IMPLIB
                IMPORTED_IMPLIB_RELEASE
                IMPORTED_IMPLIB_RELWITHDEBINFO
                IMPORTED_IMPLIB_MINSIZEREL
                IMPORTED_LOCATION)
            get_target_property(_cae_usd_python_library Python3::Python "${_cae_usd_python_property}")
            if(_cae_usd_python_library)
                get_filename_component(_cae_usd_python_link_dir "${_cae_usd_python_library}" DIRECTORY)
                break()
            endif()
        endforeach()
    endif()
    if(TARGET CaeUSD::Headers)
        set_property(TARGET CaeUSD::Headers APPEND PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}")
    endif()
    foreach(_raw_target IN LISTS _CAE_USD_RAW_TARGETS)
        if(TARGET "CaeUSD::${_raw_target}")
            set_property(TARGET "CaeUSD::${_raw_target}" APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}")
            if(_cae_usd_python_link_dir)
                set_property(TARGET "CaeUSD::${_raw_target}" APPEND PROPERTY
                    INTERFACE_LINK_DIRECTORIES "${_cae_usd_python_link_dir}")
            endif()
        endif()
    endforeach()
endif()

set(CaeUSD_MONOLITHIC_TARGET "")
if(TARGET CaeUSD::usd_ms)
    set(CaeUSD_MONOLITHIC_TARGET CaeUSD::usd_ms)
elseif(TARGET CaeUSD::usd_m)
    set(CaeUSD_MONOLITHIC_TARGET CaeUSD::usd_m)
endif()

# --- USD version (parsed from pxrConfig.cmake text, never include()-d) ------

_cae_usd_read_pxr_config_value(PXR_MAJOR_VERSION _cae_usd_pxr_major_version)
_cae_usd_read_pxr_config_value(PXR_MINOR_VERSION _cae_usd_pxr_minor_version)
_cae_usd_read_pxr_config_value(PXR_PATCH_VERSION _cae_usd_pxr_patch_version)
_cae_usd_read_pxr_config_value(PXR_VERSION _cae_usd_pxr_version)

if(_cae_usd_pxr_major_version OR _cae_usd_pxr_minor_version OR _cae_usd_pxr_patch_version)
    set(USD_VERSION
        "${_cae_usd_pxr_major_version}.${_cae_usd_pxr_minor_version}.${_cae_usd_pxr_patch_version}")
elseif(_cae_usd_pxr_version)
    set(USD_VERSION "${_cae_usd_pxr_version}")
else()
    set(USD_VERSION "")
endif()

# --- usdGenSchema tool discovery --------------------------------------------

if(USDGENSCHEMA_EXECUTABLE AND NOT EXISTS "${USDGENSCHEMA_EXECUTABLE}")
    unset(USDGENSCHEMA_EXECUTABLE CACHE)
    unset(USDGENSCHEMA_EXECUTABLE)
endif()

# Restrict discovery to USD_ROOT/bin (NO_DEFAULT_PATH): the tool must match the
# USD build being imported, so a mismatched usdGenSchema on the system PATH must
# not be picked up.
find_file(USDGENSCHEMA_EXECUTABLE
    NAMES usdGenSchema usdGenSchema.py usdGenSchema.bat usdGenSchema.cmd
    HINTS "${USD_ROOT}/bin"
    NO_DEFAULT_PATH
    DOC "Path to usdGenSchema code generator"
)
mark_as_advanced(USDGENSCHEMA_EXECUTABLE)
if(USDGENSCHEMA_EXECUTABLE)
    _cae_usd_status("Found usdGenSchema: ${USDGENSCHEMA_EXECUTABLE}")
    if(NOT Python3_EXECUTABLE)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    if(NOT Python3_EXECUTABLE)
        _cae_usd_define_python_from_hints()
    endif()
else()
    message(WARNING
        "usdGenSchema not found in ${USD_ROOT}/bin -- "
        "cae_usd_gen_schema() will not be available.")
endif()

# --- Capability components --------------------------------------------------

set(CaeUSD_Schema_FOUND FALSE)
set(_cae_usd_has_schema_library FALSE)
if(CaeUSD_MONOLITHIC_TARGET OR TARGET CaeUSD::usd)
    set(_cae_usd_has_schema_library TRUE)
endif()
if(_cae_usd_has_schema_library AND USDGENSCHEMA_EXECUTABLE)
    set(CaeUSD_Schema_FOUND TRUE)
endif()

set(CaeUSD_FileFormat_FOUND FALSE)
set(_cae_usd_has_file_format_libraries FALSE)
if(CaeUSD_MONOLITHIC_TARGET)
    set(_cae_usd_has_file_format_libraries TRUE)
elseif(TARGET CaeUSD::pcp AND TARGET CaeUSD::usd)
    set(_cae_usd_has_file_format_libraries TRUE)
endif()
if(_cae_usd_has_file_format_libraries)
    set(CaeUSD_FileFormat_FOUND TRUE)
endif()

set(CaeUSD_Python_FOUND FALSE)
if(CaeUSD_HAS_PYTHON AND TARGET Python3::Python)
    set(CaeUSD_Python_FOUND TRUE)
endif()

foreach(_component IN LISTS CaeUSD_FIND_COMPONENTS)
    if(NOT "${_component}" IN_LIST _CAE_USD_KNOWN_COMPONENTS)
        set(CaeUSD_${_component}_FOUND FALSE)
    endif()
endforeach()

find_package_handle_standard_args(CaeUSD
    REQUIRED_VARS USD_INCLUDE_DIRS USD_LIBRARY_DIR
    VERSION_VAR USD_VERSION
    HANDLE_COMPONENTS
)

_cae_usd_status("Found USD ${USD_VERSION} at ${USD_ROOT}")

# Expose the packaged plugin helper macros (cae_add_schema / cae_add_file_format
# / cae_usd_gen_schema) alongside the clean targets.
include("${CMAKE_CURRENT_LIST_DIR}/USDPluginHelpers.cmake")
