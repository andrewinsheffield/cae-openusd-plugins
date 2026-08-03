# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# USDPluginHelpers.cmake
#
# Convenience functions for building USD schema libraries and file format plugins.
#
# Low-level helpers:
#   cae_usd_run_tool(...)             -- run a USD tool through the CMake trampoline
#   cae_usd_gen_schema(name ...)       -- run usdGenSchema and return generated files
#   cae_usd_runtime_library(name ...)  -- build/install shared libs beside USD plugins
#   cae_usd_plugin(name ...)           -- build and install a USD plugin library
#
# High-level (one call registers everything):
#   cae_add_schema(name ...)           -- register a schema from its source directory
#   cae_add_file_format(name ...)      -- register a file format plugin (option + build + install)
#
# This file is pure definitions (no top-level side effects), so it is guarded:
# FindCaeUSD.cmake include()-s it on every find_package(CaeUSD) call, and the
# guard keeps the functions from being re-parsed each time.

include_guard(GLOBAL)
include(CMakeParseArguments)


function(cae_apply_usd_msvc_options NAME)
    if(NOT MSVC)
        return()
    endif()

    # Flags for compiling Packman USD headers with MSVC.  CMake's SYSTEM
    # includes do not reliably map to /external:I, so the USD/Python include
    # roots are added with the external switches explicitly (below) and the
    # warnings they emit are quieted via /external:W0.
    #
    # NOTE: the Windows schema build was fixed by include-path hygiene (keeping
    # the flat usdGenSchema output -- e.g. timeAPI.h -- off the include path so
    # it cannot shadow SDK headers like <timeapi.h>), NOT by relaxing
    # conformance.  Do not drop /permissive- to chase a USD/Python build error.
    target_compile_definitions(${NAME} PRIVATE NOMINMAX)
    target_compile_options(${NAME} PRIVATE
        /utf-8
        /bigobj
        /permissive-
        /MP
        /GS
        /sdl
        /Zc:wchar_t
        /Zc:inline-
        /external:W0
        /wd4003
        /wd4005
        /wd4100
        /wd4127
        /wd4201
        /wd4244
        /wd4251
        /wd4267
        /wd4305
        /wd4996
    )
    target_link_options(${NAME} PRIVATE /NOEXP)

    foreach(_external_include_dir IN LISTS USD_INCLUDE_DIRS Python3_INCLUDE_DIRS)
        if(_external_include_dir)
            target_compile_options(${NAME} PRIVATE "/external:I${_external_include_dir}")
        endif()
    endforeach()
endfunction()

# MFB_PACKAGE_NAME / MFB_ALT_PACKAGE_NAME tag this library's
# TF_REGISTRY_FUNCTION registrations (see tf/registryManager.h) and, for a
# Python module, drive the PyInit_ symbol in tf/pyModule.h.  Every plugin
# library wants a unique name here; left undefined, USD stringizes the literal
# "MFB_ALT_PACKAGE_NAME" so all such libraries would share one bogus tag.
#
# PACKAGE_MODULE (optional) is the pxr Python package name and is consumed only
# by the Python binding headers (pyModule.h / pyUtils.h).  Pass it only for the
# Python module target so plain C++ plugins don't carry a meaningless define.
function(cae_apply_usd_plugin_definitions TARGET_NAME PACKAGE_NAME)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        "MFB_PACKAGE_NAME=${PACKAGE_NAME}"
        "MFB_ALT_PACKAGE_NAME=${PACKAGE_NAME}"
    )
    set(_package_module "${ARGN}")
    if(_package_module)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            "MFB_PACKAGE_MODULE=${_package_module}"
        )
    endif()
endfunction()

# Uppercase the first character of IN_STR (leaving the rest unchanged) and
# return it in OUT_VAR.  Used to derive the pxr-style package module name
# (e.g. omniSci -> OmniSci) from a plugin target name.
function(cae_capitalize_first IN_STR OUT_VAR)
    string(SUBSTRING "${IN_STR}" 0 1 _first)
    string(TOUPPER   "${_first}" _first)
    string(SUBSTRING "${IN_STR}" 1 -1 _rest)
    set(${OUT_VAR} "${_first}${_rest}" PARENT_SCOPE)
endfunction()


function(_cae_usd_encode_tool_list OUT)
    set(_value ${ARGN})
    string(REPLACE ";" "|" _encoded "${_value}")
    set(${OUT} "${_encoded}" PARENT_SCOPE)
endfunction()


# -----------------------------------------------------------------------
# cae_usd_run_tool
#
# Runs a USD tool through CaeUSDToolTrampoline.cmake.  This keeps Python
# package setup and platform-specific runtime environment handling in one
# reusable path instead of each helper assembling its own execute_process().
#
# Usage:
#   cae_usd_run_tool(
#       NAME usdGenSchema
#       EXECUTABLE "${USDGENSCHEMA_EXECUTABLE}"
#       ARGS_FILE "${args_file}"
#       WORKING_DIRECTORY "${source_dir}"
#       USE_PYTHON TRUE
#       PYTHON_EXECUTABLE "${Python3_EXECUTABLE}"
#       PYTHON_TARGET "${CAE_USD_TOOL_STATE_DIR}/python/usdGenSchema"
#       PYTHON_PACKAGES ${CAE_USDGENSCHEMA_PYTHON_PACKAGES}
#       PYTHON_IMPORTS ${CAE_USDGENSCHEMA_PYTHON_IMPORTS}
#       PYTHONPATH ${pythonpath_entries}
#       LIBRARY_PATH ${library_path_entries}
#       PATH ${path_entries}
#   )
# -----------------------------------------------------------------------
function(cae_usd_run_tool)
    cmake_parse_arguments(ARG ""
        "NAME;EXECUTABLE;PYTHON_EXECUTABLE;PYTHON_TARGET;ARGS_FILE;WORKING_DIRECTORY;USE_PYTHON;OUT_OUTPUT;OUT_ERROR"
        "PYTHON_PACKAGES;PYTHON_IMPORTS;PYTHONPATH;LIBRARY_PATH;PATH"
        ${ARGN}
    )

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cae_usd_run_tool(): unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    foreach(_required_arg IN ITEMS NAME EXECUTABLE ARGS_FILE)
        if(NOT ARG_${_required_arg})
            message(FATAL_ERROR "cae_usd_run_tool(): ${_required_arg} is required")
        endif()
    endforeach()

    if("${ARG_USE_PYTHON}" STREQUAL "")
        set(ARG_USE_PYTHON TRUE)
    endif()

    set(_requires_python FALSE)
    if(ARG_USE_PYTHON OR ARG_PYTHON_PACKAGES)
        set(_requires_python TRUE)
    endif()

    if(_requires_python AND NOT ARG_PYTHON_EXECUTABLE)
        message(FATAL_ERROR
            "cae_usd_run_tool(${ARG_NAME}): PYTHON_EXECUTABLE is required")
    endif()
    if(ARG_PYTHON_PACKAGES AND NOT ARG_PYTHON_TARGET)
        message(FATAL_ERROR
            "cae_usd_run_tool(${ARG_NAME}): PYTHON_TARGET is required when "
            "PYTHON_PACKAGES is set")
    endif()
    if(NOT CAE_USD_TOOL_TRAMPOLINE OR NOT EXISTS "${CAE_USD_TOOL_TRAMPOLINE}")
        message(FATAL_ERROR
            "cae_usd_run_tool(${ARG_NAME}): CAE_USD_TOOL_TRAMPOLINE does not exist.\n"
            "Ensure CaeUSD was found from a package that includes "
            "CaeUSDToolTrampoline.cmake.")
    endif()

    _cae_usd_encode_tool_list(_tool_python_packages_arg ${ARG_PYTHON_PACKAGES})
    _cae_usd_encode_tool_list(_tool_python_imports_arg ${ARG_PYTHON_IMPORTS})
    _cae_usd_encode_tool_list(_tool_pythonpath_arg ${ARG_PYTHONPATH})
    _cae_usd_encode_tool_list(_tool_library_path_arg ${ARG_LIBRARY_PATH})
    _cae_usd_encode_tool_list(_tool_path_arg ${ARG_PATH})

    set(_working_directory_arg "")
    if(ARG_WORKING_DIRECTORY)
        list(APPEND _working_directory_arg
            WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DCAE_USD_TOOL_NAME=${ARG_NAME}"
            "-DCAE_USD_TOOL_EXECUTABLE=${ARG_EXECUTABLE}"
            "-DCAE_USD_TOOL_PYTHON_EXECUTABLE=${ARG_PYTHON_EXECUTABLE}"
            "-DCAE_USD_TOOL_PYTHON_TARGET=${ARG_PYTHON_TARGET}"
            "-DCAE_USD_TOOL_PYTHON_PACKAGES=${_tool_python_packages_arg}"
            "-DCAE_USD_TOOL_PYTHON_IMPORTS=${_tool_python_imports_arg}"
            "-DCAE_USD_TOOL_PYTHONPATH=${_tool_pythonpath_arg}"
            "-DCAE_USD_TOOL_LIBRARY_PATH=${_tool_library_path_arg}"
            "-DCAE_USD_TOOL_PATH=${_tool_path_arg}"
            "-DCAE_USD_TOOL_USE_PYTHON=${ARG_USE_PYTHON}"
            "-DCAE_USD_TOOL_ARGS_FILE=${ARG_ARGS_FILE}"
            "-DCAE_USD_TOOL_WORKING_DIRECTORY=${ARG_WORKING_DIRECTORY}"
            -P "${CAE_USD_TOOL_TRAMPOLINE}"
        RESULT_VARIABLE _tool_result
        OUTPUT_VARIABLE _tool_output
        ERROR_VARIABLE  _tool_error
        ${_working_directory_arg}
    )
    if(NOT _tool_result STREQUAL "0")
        message(FATAL_ERROR
            "CaeUSD tool '${ARG_NAME}' failed (exit ${_tool_result}):\n"
            "${_tool_output}\n${_tool_error}")
    endif()

    if(ARG_OUT_OUTPUT)
        set(${ARG_OUT_OUTPUT} "${_tool_output}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_ERROR)
        set(${ARG_OUT_ERROR} "${_tool_error}" PARENT_SCOPE)
    endif()
endfunction()


# -----------------------------------------------------------------------
# cae_usd_gen_schema
#
# Runs usdGenSchema at CMake configure time on schema.usda and returns the
# generated source, resource, and include paths to the caller.  Building and
# installing the plugin target is handled by cae_usd_plugin().
#
# Usage:
#   cae_usd_gen_schema(omniSci
#       SCHEMA_USDA schema.usda   # default: "schema.usda"
#       SOURCE_DIR  /path/to/schema/source  # default: CMAKE_CURRENT_SOURCE_DIR
#       OUT_SOURCES <var>
#       OUT_PYTHON_SOURCES <var>
#       OUT_GENERATED_DIR <var>
#       OUT_INCLUDE_DIR <var>
#       OUT_HEADER_DIR <var>
#       OUT_SCHEMA_FILE <var>
#       OUT_PLUG_INFO_TEMPLATE <var>
#       OUT_RESOURCE_FILES <var>
#   )
# -----------------------------------------------------------------------
function(cae_usd_gen_schema NAME)
    cmake_parse_arguments(ARG ""
        "SCHEMA_USDA;SOURCE_DIR;OUT_SOURCES;OUT_PYTHON_SOURCES;OUT_GENERATED_DIR;OUT_INCLUDE_DIR;OUT_HEADER_DIR;OUT_SCHEMA_FILE;OUT_PLUG_INFO_TEMPLATE;OUT_RESOURCE_FILES"
        ""
        ${ARGN}
    )

    if(NOT ARG_SCHEMA_USDA)
        set(ARG_SCHEMA_USDA "schema.usda")
    endif()

    if(ARG_SOURCE_DIR)
        set(_source_dir "${ARG_SOURCE_DIR}")
    else()
        set(_source_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT USDGENSCHEMA_EXECUTABLE)
        message(FATAL_ERROR
            "cae_usd_gen_schema(${NAME}): USDGENSCHEMA_EXECUTABLE not set.\n"
            "Ensure USD_ROOT points to a USD installation that includes usdGenSchema.")
    endif()
    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR
            "cae_usd_gen_schema(${NAME}): Python3_EXECUTABLE not set.\n"
            "usdGenSchema is launched through Python so tool packages can be prepared.")
    endif()

    set(_schema_file "${_source_dir}/${ARG_SCHEMA_USDA}")
    set(_gen_dir     "${CMAKE_CURRENT_BINARY_DIR}/generated/${NAME}")
    set(_include_dir "${_gen_dir}/include")

    # usdGenSchema overwrites current outputs but does not remove files for
    # deleted schema classes. Clear this schema's output when its source
    # changes, while preserving incremental builds across unrelated reconfigures.
    file(SHA256 "${_schema_file}" _schema_hash)
    set(_schema_hash_file
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${NAME}.schema.sha256")
    set(_previous_schema_hash "")
    if(EXISTS "${_schema_hash_file}")
        file(READ "${_schema_hash_file}" _previous_schema_hash)
        string(STRIP "${_previous_schema_hash}" _previous_schema_hash)
    endif()
    if(NOT _previous_schema_hash STREQUAL _schema_hash)
        file(REMOVE_RECURSE "${_gen_dir}")
    endif()
    file(MAKE_DIRECTORY "${_gen_dir}")

    # ------------------------------------------------------------------
    # Run usdGenSchema at configure time
    # ------------------------------------------------------------------
    set(_tool_args_file "${_gen_dir}/usdGenSchema.args")
    file(WRITE "${_tool_args_file}" "${_schema_file}\n${_gen_dir}\n")

    set(_tool_pythonpath "${USD_ROOT}/lib/python")
    if(CAE_USDGENSCHEMA_PYTHONPATH)
        list(APPEND _tool_pythonpath ${CAE_USDGENSCHEMA_PYTHONPATH})
    endif()
    if(_tool_pythonpath)
        list(REMOVE_DUPLICATES _tool_pythonpath)
    endif()

    set(_tool_library_path "${USD_LIBRARY_DIR}")
    foreach(_candidate IN ITEMS
            "${Python3_ROOT_DIR}/lib"
            "${Python3_ROOT_DIR}/libs")
        if(_candidate AND EXISTS "${_candidate}")
            list(APPEND _tool_library_path "${_candidate}")
        endif()
    endforeach()
    if(_tool_library_path)
        list(REMOVE_DUPLICATES _tool_library_path)
    endif()

    set(_tool_path "")
    foreach(_candidate IN ITEMS
            "${USD_ROOT}/bin"
            "${USD_LIBRARY_DIR}"
            "${Python3_ROOT_DIR}"
            "${Python3_ROOT_DIR}/bin"
            "${Python3_ROOT_DIR}/DLLs"
            "${Python3_ROOT_DIR}/Library/bin")
        if(_candidate AND EXISTS "${_candidate}")
            list(APPEND _tool_path "${_candidate}")
        endif()
    endforeach()
    if(_tool_path)
        list(REMOVE_DUPLICATES _tool_path)
    endif()

    set(_usdgenschema_uses_python TRUE)
    if(USDGENSCHEMA_EXECUTABLE MATCHES "\\.(bat|cmd|exe)$")
        set(_usdgenschema_uses_python FALSE)
    endif()

    set(_tool_python_target "${CAE_USD_TOOL_STATE_DIR}/python/usdGenSchema")

    message(STATUS "[cae] Running usdGenSchema for ${NAME}...")
    cae_usd_run_tool(
        NAME usdGenSchema
        EXECUTABLE "${USDGENSCHEMA_EXECUTABLE}"
        PYTHON_EXECUTABLE "${Python3_EXECUTABLE}"
        PYTHON_TARGET "${_tool_python_target}"
        PYTHON_PACKAGES ${CAE_USDGENSCHEMA_PYTHON_PACKAGES}
        PYTHON_IMPORTS ${CAE_USDGENSCHEMA_PYTHON_IMPORTS}
        PYTHONPATH ${_tool_pythonpath}
        LIBRARY_PATH ${_tool_library_path}
        PATH ${_tool_path}
        USE_PYTHON "${_usdgenschema_uses_python}"
        ARGS_FILE "${_tool_args_file}"
        WORKING_DIRECTORY "${_source_dir}"
    )
    file(WRITE "${_schema_hash_file}" "${_schema_hash}\n")

    # ------------------------------------------------------------------
    # Mirror headers into a dedicated include root so generated .cpp files
    # can include them as "omniSci/tokens.h" etc.  Keep the flat generator
    # output off the compiler include path; on Windows, names like timeAPI.h
    # can otherwise shadow SDK headers such as <timeapi.h>.
    # ------------------------------------------------------------------
    file(GLOB _gen_headers "${_gen_dir}/*.h")
    file(MAKE_DIRECTORY "${_include_dir}/${NAME}")
    file(COPY ${_gen_headers} DESTINATION "${_include_dir}/${NAME}/")

    # ------------------------------------------------------------------
    # Separate core sources from Python binding sources.
    #
    # Newer usdGenSchema emits generatedSchema.module.h (TF_WRAP declarations)
    # but not module.cpp (PyInit entry point).  Write it if absent.
    # ------------------------------------------------------------------
    file(GLOB _wrap_cpp "${_gen_dir}/wrap*.cpp")
    set(_module_cpp "${_gen_dir}/module.cpp")

    if(NOT EXISTS "${_module_cpp}")
        configure_file(
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/usd_schema_module.cpp.in"
            "${_module_cpp}"
            @ONLY
        )
    endif()

    file(GLOB _all_cpp "${_gen_dir}/*.cpp")
    set(_py_cpp ${_wrap_cpp} "${_module_cpp}")

    # Core sources: everything except Python binding files
    set(_core_cpp ${_all_cpp})
    list(REMOVE_ITEM _core_cpp ${_wrap_cpp} "${_module_cpp}")

    set(_resource_files
        "${_gen_dir}/generatedSchema.usda"
        "${_schema_file}"
    )

    if(ARG_OUT_SOURCES)
        set(${ARG_OUT_SOURCES} ${_core_cpp} PARENT_SCOPE)
    endif()
    if(ARG_OUT_PYTHON_SOURCES)
        set(${ARG_OUT_PYTHON_SOURCES} ${_py_cpp} PARENT_SCOPE)
    endif()
    if(ARG_OUT_GENERATED_DIR)
        set(${ARG_OUT_GENERATED_DIR} "${_gen_dir}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_INCLUDE_DIR)
        set(${ARG_OUT_INCLUDE_DIR} "${_include_dir}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_HEADER_DIR)
        set(${ARG_OUT_HEADER_DIR} "${_include_dir}/${NAME}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_SCHEMA_FILE)
        set(${ARG_OUT_SCHEMA_FILE} "${_schema_file}" PARENT_SCOPE)
    endif()
    if(ARG_OUT_PLUG_INFO_TEMPLATE)
        set(${ARG_OUT_PLUG_INFO_TEMPLATE} "${_gen_dir}/plugInfo.json" PARENT_SCOPE)
    endif()
    if(ARG_OUT_RESOURCE_FILES)
        set(${ARG_OUT_RESOURCE_FILES} ${_resource_files} PARENT_SCOPE)
    endif()
endfunction()


# -----------------------------------------------------------------------
# cae_usd_runtime_library
#
# Build and install a non-plugin shared library beside the USD plugin targets.
# These support libraries are linked by plugin modules but do not have
# plugInfo.json resources of their own.
#
# Usage:
#   cae_usd_runtime_library(omniSciFileFormatShared
#       SOURCES source/file_formats/shared/CaeFileFormatData.cpp
#       EXPORT_DEFINE OMNI_SCI_FILE_FORMAT_SHARED_EXPORTS
#       PUBLIC_INCLUDE_DIRS source/file_formats/shared
#       SYSTEM_PRIVATE_INCLUDE_DIRS ${USD_INCLUDE_DIRS}
#       SYSTEM_INTERFACE_INCLUDE_DIRS ${USD_INCLUDE_DIRS}
#       USD_LIBRARIES sdf
#   )
# -----------------------------------------------------------------------
function(cae_usd_runtime_library NAME)
    cmake_parse_arguments(ARG ""
        "EXPORT_DEFINE"
        "SOURCES;PUBLIC_INCLUDE_DIRS;PRIVATE_INCLUDE_DIRS;SYSTEM_PRIVATE_INCLUDE_DIRS;SYSTEM_INTERFACE_INCLUDE_DIRS;USD_LIBRARIES;LIBRARIES;COMPILE_OPTIONS"
        ${ARGN}
    )

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cae_usd_runtime_library(${NAME}): SOURCES is required")
    endif()

    add_library(${NAME} SHARED ${ARG_SOURCES})

    set_target_properties(${NAME} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        INSTALL_RPATH "$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH "${CAE_INSTALL_RPATH_USE_LINK_PATH}"
    )
    if(ARG_EXPORT_DEFINE)
        set_target_properties(${NAME} PROPERTIES
            DEFINE_SYMBOL "${ARG_EXPORT_DEFINE}"
        )
    endif()

    if(ARG_PUBLIC_INCLUDE_DIRS)
        target_include_directories(${NAME} PUBLIC ${ARG_PUBLIC_INCLUDE_DIRS})
    endif()
    if(ARG_PRIVATE_INCLUDE_DIRS)
        target_include_directories(${NAME} PRIVATE ${ARG_PRIVATE_INCLUDE_DIRS})
    endif()
    if(ARG_SYSTEM_PRIVATE_INCLUDE_DIRS)
        target_include_directories(${NAME} SYSTEM PRIVATE ${ARG_SYSTEM_PRIVATE_INCLUDE_DIRS})
    endif()
    if(ARG_SYSTEM_INTERFACE_INCLUDE_DIRS)
        target_include_directories(${NAME} SYSTEM INTERFACE ${ARG_SYSTEM_INTERFACE_INCLUDE_DIRS})
    endif()

    set(_usd_link_libraries "")
    if(ARG_USD_LIBRARIES)
        cae_usd_resolve_libraries(_usd_link_libraries ${ARG_USD_LIBRARIES})
    endif()

    target_link_libraries(${NAME}
        PRIVATE
            ${_usd_link_libraries}
            ${ARG_LIBRARIES}
            cae_project_warnings
    )

    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${NAME} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()
    cae_apply_usd_msvc_options(${NAME})

    install(TARGETS ${NAME}
        LIBRARY DESTINATION "${CAE_PLUGIN_INSTALL_DIR}"
        RUNTIME DESTINATION "${CAE_PLUGIN_INSTALL_DIR}"
    )
endfunction()


# -----------------------------------------------------------------------
# cae_usd_plugin
#
# Build and install a USD plugin target using the OpenUSD plugin layout.
# Optional Python bindings are for pxr-style generated schema modules.
#
# Usage:
#   cae_usd_plugin(omniSciCgnsFileFormat
#       MODULE                         # build as a dlopen-style plugin
#       SOURCES    src/fileFormat.cpp src/reader.cpp ...   # absolute paths
#       PLUG_INFO_TEMPLATE resources/plugInfo.json.in
#       USD_LIBRARIES pcp usdGeom
#       LIBRARIES  cgns hdf5 ...
#       SOURCE_DIR /path/to/plugin/source  # for include/ dir
#       RESOURCE_FILES extra_resource.usda
#       RESOURCE_DIRS  data
#       PYTHON_BINDINGS              # pxr-style generated schema bindings
#       PYTHON_SOURCES wrap*.cpp module.cpp
#   )
# -----------------------------------------------------------------------
function(cae_usd_plugin NAME)
    cmake_parse_arguments(ARG
        "MODULE;PUBLIC_USD_LIBRARIES;PYTHON_BINDINGS"
        "PLUG_INFO;PLUG_INFO_TEMPLATE;SOURCE_DIR;INSTALL_HEADER_DIR;PYTHON_MODULE;PYTHON_PACKAGE;EXPORT_DEFINE"
        "SOURCES;USD_LIBRARIES;LIBRARIES;PLUGIN_DEPS;PUBLIC_INCLUDE_DIRS;PRIVATE_INCLUDE_DIRS;RESOURCE_FILES;RESOURCE_DIRS;COMPILE_OPTIONS;PYTHON_SOURCES;PYTHON_INCLUDE_DIRS;PYTHON_USD_LIBRARIES;PYTHON_LIBRARIES;PYTHON_COMPILE_OPTIONS"
        ${ARGN}
    )

    if(ARG_SOURCE_DIR)
        set(_source_dir "${ARG_SOURCE_DIR}")
    else()
        set(_source_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    set(_plugin_library_type SHARED)
    set(_plugin_library_suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(ARG_MODULE)
        set(_plugin_library_type MODULE)
        if(DEFINED CMAKE_SHARED_MODULE_SUFFIX)
            set(_plugin_library_suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
        endif()
    endif()

    add_library(${NAME} ${_plugin_library_type} ${ARG_SOURCES})

    if(ARG_EXPORT_DEFINE)
        set_target_properties(${NAME} PROPERTIES
            DEFINE_SYMBOL "${ARG_EXPORT_DEFINE}"
        )
    endif()

    if(ARG_PUBLIC_INCLUDE_DIRS)
        set(_public_build_includes "")
        foreach(_include_dir IN LISTS ARG_PUBLIC_INCLUDE_DIRS)
            list(APPEND _public_build_includes "$<BUILD_INTERFACE:${_include_dir}>")
        endforeach()
        target_include_directories(${NAME}
            PUBLIC
                ${_public_build_includes}
                $<INSTALL_INTERFACE:include/${NAME}>
        )
    endif()

    set(_private_include_dirs ${ARG_PRIVATE_INCLUDE_DIRS})
    if(EXISTS "${_source_dir}/include")
        list(APPEND _private_include_dirs "${_source_dir}/include")
    endif()
    if(_private_include_dirs)
        target_include_directories(${NAME} PRIVATE ${_private_include_dirs})
    endif()
    target_include_directories(${NAME}
        SYSTEM PRIVATE
            ${USD_INCLUDE_DIRS}
    )
    if(NOT ARG_USD_LIBRARIES)
        message(FATAL_ERROR
            "cae_usd_plugin(${NAME}): USD_LIBRARIES is required. "
            "Pass explicit USD library names such as 'usd', 'pcp usd', or "
            "'pcp usdGeom work'.")
    endif()
    cae_usd_resolve_libraries(_usd_link_libraries ${ARG_USD_LIBRARIES})

    if(ARG_PUBLIC_USD_LIBRARIES)
        target_link_libraries(${NAME} PUBLIC ${_usd_link_libraries})
        target_link_libraries(${NAME} PRIVATE ${ARG_LIBRARIES} cae_project_warnings)
    else()
        target_link_libraries(${NAME} PRIVATE
            ${_usd_link_libraries}
            ${ARG_LIBRARIES}
            cae_project_warnings
        )
    endif()

    cae_apply_usd_plugin_definitions(${NAME} ${NAME})

    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${NAME} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()
    cae_apply_usd_msvc_options(${NAME})

    # Build INSTALL_RPATH so sibling plugin/runtime .so files under plugin/usd
    # are found without LD_LIBRARY_PATH.
    set(_rpath "")
    set(_needs_plugin_root_rpath FALSE)
    if(ARG_PLUGIN_DEPS)
        set(_needs_plugin_root_rpath TRUE)
    endif()
    foreach(_linked_library IN LISTS ARG_LIBRARIES)
        if(TARGET "${_linked_library}")
            get_target_property(_linked_library_type "${_linked_library}" TYPE)
            if(_linked_library_type STREQUAL "SHARED_LIBRARY" OR
                    _linked_library_type STREQUAL "MODULE_LIBRARY" OR
                    _linked_library_type STREQUAL "UNKNOWN_LIBRARY")
                set(_needs_plugin_root_rpath TRUE)
            endif()
        endif()
    endforeach()
    if(_needs_plugin_root_rpath)
        list(APPEND _rpath "$ORIGIN")
    endif()
    unset(_needs_plugin_root_rpath)
    unset(_linked_library)
    unset(_linked_library_type)

    set_target_properties(${NAME} PROPERTIES
        PREFIX ""
        INSTALL_RPATH "${_rpath}"
        # Keep non-system linked library directories, such as CGNS or HDF5
        # installs, discoverable when tests run with a controlled environment.
        INSTALL_RPATH_USE_LINK_PATH "${CAE_INSTALL_RPATH_USE_LINK_PATH}"
    )

    set(_plugin_dir "${CAE_PLUGIN_INSTALL_DIR}/${NAME}")
    set(_resource_dir "${_plugin_dir}/resources")

    if(ARG_PLUG_INFO AND ARG_PLUG_INFO_TEMPLATE)
        message(FATAL_ERROR
            "cae_usd_plugin(${NAME}): specify only one of PLUG_INFO or PLUG_INFO_TEMPLATE")
    endif()
    if(ARG_PLUG_INFO_TEMPLATE)
        set(_plug_info_out "${CMAKE_BINARY_DIR}/plugins/${NAME}/resources/plugInfo.json")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${NAME}/resources")
        set(PLUG_INFO_ROOT          "..")
        set(PLUG_INFO_LIBRARY_PATH  "../${NAME}${_plugin_library_suffix}")
        set(PLUG_INFO_RESOURCE_PATH "resources")
        configure_file(
            "${ARG_PLUG_INFO_TEMPLATE}"
            "${_plug_info_out}"
            @ONLY
        )
    else()
        set(_plug_info_out "${ARG_PLUG_INFO}")
    endif()

    install(TARGETS ${NAME}
        LIBRARY DESTINATION "${CAE_PLUGIN_INSTALL_DIR}"
        RUNTIME DESTINATION "${CAE_PLUGIN_INSTALL_DIR}"
    )
    if(_plug_info_out)
        install(FILES "${_plug_info_out}"
            DESTINATION "${_resource_dir}"
        )
    endif()
    if(ARG_RESOURCE_FILES)
        install(FILES ${ARG_RESOURCE_FILES}
            DESTINATION "${_resource_dir}"
        )
    endif()
    foreach(_resource_dir_in IN LISTS ARG_RESOURCE_DIRS)
        install(DIRECTORY "${_resource_dir_in}/"
            DESTINATION "${_resource_dir}"
        )
    endforeach()
    if(ARG_INSTALL_HEADER_DIR)
        install(DIRECTORY "${ARG_INSTALL_HEADER_DIR}"
            DESTINATION "include"
        )
    endif()

    if(ARG_PYTHON_BINDINGS AND Python3_FOUND AND ARG_PYTHON_SOURCES)
        if(ARG_PYTHON_MODULE)
            set(_py_module "${ARG_PYTHON_MODULE}")
        else()
            set(_py_module "_${NAME}")
        endif()
        if(ARG_PYTHON_PACKAGE)
            set(_py_pkg "${ARG_PYTHON_PACKAGE}")
        else()
            cae_capitalize_first("${NAME}" _py_pkg)
        endif()
        set(_py_pkg_dir "${CAE_PYTHON_INSTALL_DIR}/pxr/${_py_pkg}")
        set(_pxr_init  "${CMAKE_BINARY_DIR}/plugins/${NAME}/python/pxr/__init__.py")
        set(_pkg_init  "${CMAKE_BINARY_DIR}/plugins/${NAME}/python/${_py_pkg}/__init__.py")
        file(MAKE_DIRECTORY
            "${CMAKE_BINARY_DIR}/plugins/${NAME}/python/pxr"
            "${CMAKE_BINARY_DIR}/plugins/${NAME}/python/${_py_pkg}"
        )

        add_library(${_py_module} SHARED ${ARG_PYTHON_SOURCES})
        if(ARG_PYTHON_INCLUDE_DIRS)
            target_include_directories(${_py_module} PRIVATE ${ARG_PYTHON_INCLUDE_DIRS})
        endif()
        target_include_directories(${_py_module}
            SYSTEM PRIVATE
                ${USD_INCLUDE_DIRS}
                ${Python3_INCLUDE_DIRS}
        )
        set(_python_usd_link_libraries "")
        if(ARG_PYTHON_USD_LIBRARIES)
            cae_usd_resolve_libraries(_python_usd_link_libraries ${ARG_PYTHON_USD_LIBRARIES})
        endif()
        target_link_libraries(${_py_module} PRIVATE
            ${NAME}
            ${_python_usd_link_libraries}
            ${ARG_PYTHON_LIBRARIES}
            Python3::Python
        )
        target_compile_options(${_py_module} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-cpp>
            ${ARG_PYTHON_COMPILE_OPTIONS}
        )
        cae_apply_usd_msvc_options(${_py_module})
        # MFB_PACKAGE_NAME drives the PyInit__<name> symbol in pyModule.h.
        # Without it the .so exports PyInit__MFB_PACKAGE_NAME literally and
        # Python can't find the entry point.
        cae_apply_usd_plugin_definitions(${_py_module} ${NAME} "${_py_pkg}")
        # Installed layout: <runtime>/lib/python/pxr/<Pkg>/_<name><ext> links against
        # plugin/usd/<name><ext>. Set rpath so the dynamic linker finds the
        # C++ library without LD_LIBRARY_PATH.
        set_target_properties(${_py_module} PROPERTIES
            PREFIX ""
            INSTALL_RPATH "$ORIGIN/../../../../${CAE_RUNTIME_PLUGIN_INSTALL_DIR}"
        )
        if(WIN32)
            set_target_properties(${_py_module} PROPERTIES
                SUFFIX ".pyd"
            )
        endif()

        install(TARGETS ${_py_module}
            LIBRARY DESTINATION "${_py_pkg_dir}"
            RUNTIME DESTINATION "${_py_pkg_dir}"
        )

        # Provide a pxr package shim in our install tree that extends the
        # module search path to include the real USD pxr package as well.
        configure_file(
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/pxr_init.py.in"
            "${_pxr_init}"
            @ONLY
        )
        install(FILES "${_pxr_init}"
            DESTINATION "${CAE_PYTHON_INSTALL_DIR}/pxr"
        )

        # Write the package init explicitly so schema bindings can be imported
        # via `from pxr import <SchemaPkg>` in a fresh interpreter.
        configure_file(
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/usd_schema_package_init.py.in"
            "${_pkg_init}"
            @ONLY
        )
        install(FILES "${_pkg_init}"
            DESTINATION "${_py_pkg_dir}"
        )
    endif()
endfunction()


# -----------------------------------------------------------------------
# cae_add_schema
#
# High-level helper to register a USD schema plugin from its component
# directory. Wraps cae_usd_gen_schema() and cae_usd_plugin().
#
# Usage:
#   cae_add_schema(omniSci)
#
# DIR defaults to CMAKE_CURRENT_SOURCE_DIR, and SCHEMA_USDA defaults to
# schema.usda.
# -----------------------------------------------------------------------
function(cae_add_schema NAME)
    cmake_parse_arguments(ARG "" "DIR;SCHEMA_USDA" "" ${ARGN})

    if(NOT ARG_DIR)
        set(ARG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT ARG_SCHEMA_USDA)
        set(ARG_SCHEMA_USDA "schema.usda")
    endif()

    cae_usd_gen_schema(${NAME}
        SCHEMA_USDA "${ARG_SCHEMA_USDA}"
        SOURCE_DIR  "${ARG_DIR}"
        OUT_SOURCES            _schema_sources
        OUT_PYTHON_SOURCES     _schema_python_sources
        OUT_INCLUDE_DIR        _schema_include_dir
        OUT_HEADER_DIR         _schema_header_dir
        OUT_PLUG_INFO_TEMPLATE _schema_plug_info_template
        OUT_RESOURCE_FILES     _schema_resource_files
    )

    string(TOUPPER "${NAME}" _schema_export_define)
    set(_schema_export_define "${_schema_export_define}_EXPORTS")

    set(_schema_python_args "")
    if(CAE_BUILD_SCHEMA_PYTHON_BINDINGS AND Python3_FOUND)
        list(APPEND _schema_python_args
            PYTHON_BINDINGS
            PYTHON_SOURCES ${_schema_python_sources}
            PYTHON_INCLUDE_DIRS "${_schema_include_dir}"
            PYTHON_USD_LIBRARIES usd
        )
    endif()

    cae_usd_plugin(${NAME}
        SOURCE_DIR "${ARG_DIR}"
        SOURCES ${_schema_sources}
        PLUG_INFO_TEMPLATE "${_schema_plug_info_template}"
        EXPORT_DEFINE "${_schema_export_define}"
        USD_LIBRARIES usd
        PUBLIC_USD_LIBRARIES
        PUBLIC_INCLUDE_DIRS "${_schema_include_dir}"
        INSTALL_HEADER_DIR "${_schema_header_dir}"
        RESOURCE_FILES ${_schema_resource_files}
        COMPILE_OPTIONS $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-cpp>
        ${_schema_python_args}
    )
endfunction()


# -----------------------------------------------------------------------
# cae_add_file_format
#
# High-level helper to register a USD file format plugin.
# Handles option creation, optional find_package, plugInfo.json
# configuration, MODULE target build, and data directory installation.
#
# Required:
#   NAME              CMake target name (e.g. omniSciEnSightFileFormat)
#   ENABLE_VAR        CMake option variable (e.g. CAE_ENABLE_ENSIGHT)
#   SOURCES           Source files relative to DIR (or absolute)
#   USD_LIBRARIES     Explicit USD library names, e.g. `pcp usdGeom work`
#
# Optional:
#   DIR               Plugin source directory (defaults to current source dir)
#   DEFAULT           Option default: ON or OFF (default: ON)
#   FIND_PACKAGE      Package name to find_package(REQUIRED) when enabled
#   FIND_PACKAGE_ARGS Extra args passed to find_package (e.g. "1.10 COMPONENTS C")
#   LIBRARIES         Link libraries
#   PLUGIN_DEPS       Sibling plugin names for RPATH (see cae_usd_plugin)
#   DATA_DIR          Subdirectory under DIR to install as resources
#                     (auto-detected as "data" if the directory exists)
#   LICENSE_FILES     License files for third-party code/data used by this format.
#                     Relative paths resolve from DIR.
#   PYTHON_MODULES    Sidecar Python package dirs installed under the plugin's
#                     python/ directory. Relative paths resolve from DIR/python.
# Usage:
#   cae_add_file_format(omniSciEnSightFileFormat
#       ENABLE_VAR CAE_ENABLE_ENSIGHT
#       DEFAULT    ON
#       SOURCES    src/OmniSciEnSightFileFormat.cpp
#                  src/OmniSciEnSightFileFormatRegistration.cpp
#       LIBRARIES  omniSciFileFormatShared
#   )
# -----------------------------------------------------------------------
function(cae_add_file_format NAME)
    cmake_parse_arguments(ARG ""
        "DIR;ENABLE_VAR;DEFAULT;FIND_PACKAGE;DATA_DIR"
        "SOURCES;USD_LIBRARIES;LIBRARIES;PLUGIN_DEPS;FIND_PACKAGE_ARGS;LICENSE_FILES;PYTHON_MODULES"
        ${ARGN}
    )

    if(NOT ARG_DIR)
        set(ARG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT ARG_ENABLE_VAR)
        message(FATAL_ERROR "cae_add_file_format(${NAME}): ENABLE_VAR is required")
    endif()
    if(NOT DEFINED ARG_DEFAULT)
        set(ARG_DEFAULT ON)
    endif()
    if(NOT ARG_USD_LIBRARIES)
        message(FATAL_ERROR
            "cae_add_file_format(${NAME}): USD_LIBRARIES is required. "
            "Pass explicit USD library names such as 'pcp usd' or 'pcp usdGeom'.")
    endif()

    option(${ARG_ENABLE_VAR} "Build the ${NAME} plugin" ${ARG_DEFAULT})
    if(NOT ${ARG_ENABLE_VAR})
        return()
    endif()

    # Optional external dependency -- only resolved when the plugin is enabled.
    # cae_resolve_package() preserves normal find_package() behavior while
    # normalizing imported target names for dependencies such as HDF5 and CGNS.
    if(ARG_FIND_PACKAGE)
        if(COMMAND cae_resolve_package)
            cae_resolve_package(${ARG_FIND_PACKAGE} ${ARG_FIND_PACKAGE_ARGS})
        else()
            find_package(${ARG_FIND_PACKAGE} ${ARG_FIND_PACKAGE_ARGS} REQUIRED)
        endif()
    endif()

    # Resolve sources to absolute paths
    set(_abs_sources "")
    foreach(_s IN LISTS ARG_SOURCES)
        if(IS_ABSOLUTE "${_s}")
            list(APPEND _abs_sources "${_s}")
        else()
            list(APPEND _abs_sources "${ARG_DIR}/${_s}")
        endif()
    endforeach()

    # Install bundled data files if present
    set(_resource_dirs "")
    if(ARG_DATA_DIR)
        list(APPEND _resource_dirs "${ARG_DIR}/${ARG_DATA_DIR}")
    elseif(EXISTS "${ARG_DIR}/data")
        list(APPEND _resource_dirs "${ARG_DIR}/data")
    endif()

    cae_usd_plugin(${NAME}
        MODULE
        SOURCE_DIR "${ARG_DIR}"
        SOURCES ${_abs_sources}
        PLUG_INFO_TEMPLATE "${ARG_DIR}/resources/plugInfo.json.in"
        USD_LIBRARIES ${ARG_USD_LIBRARIES}
        LIBRARIES ${ARG_LIBRARIES}
        PLUGIN_DEPS ${ARG_PLUGIN_DEPS}
        PRIVATE_INCLUDE_DIRS "${CAE_SOURCE_ROOT}/source/file_formats/shared"
        RESOURCE_DIRS ${_resource_dirs}
    )

    foreach(_license_file IN LISTS ARG_LICENSE_FILES)
        if(IS_ABSOLUTE "${_license_file}")
            set(_license_file_path "${_license_file}")
        else()
            set(_license_file_path "${ARG_DIR}/${_license_file}")
        endif()

        if(COMMAND cae_package_register_license_file)
            cae_package_register_license_file("${_license_file_path}")
        endif()
    endforeach()

    foreach(_python_module IN LISTS ARG_PYTHON_MODULES)
        if(IS_ABSOLUTE "${_python_module}")
            set(_python_module_dir "${_python_module}")
        else()
            set(_python_module_dir "${ARG_DIR}/python/${_python_module}")
        endif()

        if(NOT IS_DIRECTORY "${_python_module_dir}")
            message(FATAL_ERROR
                "cae_add_file_format(${NAME}): Python module directory not found: "
                "${_python_module_dir}")
        endif()

        install(DIRECTORY "${_python_module_dir}"
            DESTINATION "${CAE_PLUGIN_INSTALL_DIR}/${NAME}/python"
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
        )
    endforeach()
endfunction()
