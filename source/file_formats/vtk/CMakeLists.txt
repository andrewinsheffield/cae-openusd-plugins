# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

option(CAE_ENABLE_VTK "Build the omniSciVtkFileFormat plugin" ON)

if(CAE_ENABLE_VTK)
    cae_resolve_package(pugixml)
    cae_resolve_package(LZ4)
    find_package(ZLIB REQUIRED)
    cae_resolve_package(LibLZMA)
endif()

cae_add_file_format(omniSciVtkFileFormat
    ENABLE_VAR CAE_ENABLE_VTK
    DEFAULT    ON
    SOURCES
        src/ArrayEvaluator.cpp
        src/DatasetSpecCache.cpp
        src/FileFormatCore.cpp
        src/FileHandle.cpp
        src/LegacyParser.cpp
        src/OmniSciVtkFileFormat.cpp
        src/OmniSciVtkFileFormatRegistration.cpp
        src/Parser.cpp
        src/ParserUtils.cpp
        src/ScalarPayloadReader.cpp
        src/Types.cpp
        src/XmlParser.cpp
    USD_LIBRARIES
        pcp
        usd
        work
    LIBRARIES
        omniSciFileFormatShared
        pugixml::pugixml
        LZ4::LZ4
        ZLIB::ZLIB
        LibLZMA::LibLZMA
        omniSciFileFormatArgs
        omniSciVtk
        omniSci
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciVtk
        omniSci
    LICENSE_FILES
        "${CAE_SOURCE_ROOT}/tpl_licenses/lz4-LICENSE.txt"
        "${CAE_SOURCE_ROOT}/tpl_licenses/pugixml-LICENSE.md"
        "${CAE_SOURCE_ROOT}/tpl_licenses/zlib-LICENSE.txt"
        "${CAE_SOURCE_ROOT}/tpl_licenses/xz-LICENSE.txt"
)

if(TARGET omniSciVtkFileFormat AND COMMAND cae_package_register_dependency_target)
    cae_package_register_dependency_target(pugixml::pugixml
        BASENAME_REGEX "^(lib)?pugixml(\\.|$)")
    cae_package_register_dependency_target(LZ4::LZ4
        BASENAME_REGEX "^(lib)?lz4(\\.|$)")
    cae_package_register_dependency_target(ZLIB::ZLIB
        BASENAME_REGEX "^(libz|zlib|zlib1)(\\.|$)")
    cae_package_register_dependency_target(LibLZMA::LibLZMA
        BASENAME_REGEX "^(lib)?lzma(\\.|$)")
endif()

if(TARGET omniSciVtkFileFormat AND COMMAND cae_package_register_dependency_root)
    foreach(_root IN ITEMS
            "${pugixml_ROOT}"
            "$ENV{pugixml_ROOT}"
            "${PUGIXML_ROOT}"
            "$ENV{PUGIXML_ROOT}"
            "${LZ4_ROOT}"
            "$ENV{LZ4_ROOT}"
            "${ZLIB_ROOT}"
            "$ENV{ZLIB_ROOT}"
            "${LibLZMA_ROOT}"
            "$ENV{LibLZMA_ROOT}")
        if(_root AND IS_DIRECTORY "${_root}")
            cae_package_register_dependency_root("${_root}")
        endif()
    endforeach()
endif()
