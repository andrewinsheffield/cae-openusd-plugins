# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_add_file_format(omniSciCgnsFileFormat
    ENABLE_VAR   CAE_ENABLE_CGNS
    DEFAULT      OFF
    FIND_PACKAGE CGNS
    SOURCES
        src/OmniSciCgnsFileFormat.cpp
        src/OmniSciCgnsFileFormatRegistration.cpp
        src/CGNSMutex.cpp
    USD_LIBRARIES
        pcp
        usdGeom
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciCgns
        omniSci
        CGNS::CGNS
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciCgns
        omniSci
    LICENSE_FILES
        "${CAE_SOURCE_ROOT}/tpl_licenses/cgns-LICENSE.txt"
        "${CAE_SOURCE_ROOT}/tpl_licenses/hdf5-LICENSE.txt"
)

if(TARGET omniSciCgnsFileFormat AND COMMAND cae_package_register_dependency_target)
    cae_package_register_dependency_target(CGNS::CGNS
        BASENAME_REGEX "^((lib)?cgns|cgnsdll)(\\.|$)")
endif()
if(TARGET omniSciCgnsFileFormat AND COMMAND cae_package_register_runtime_name_regex)
    cae_package_register_runtime_name_regex("^(lib)?hdf5(_.*)?(\\.|$)")
endif()
if(TARGET omniSciCgnsFileFormat AND CGNS_ROOT AND COMMAND cae_package_register_dependency_root)
    cae_package_register_dependency_root("${CGNS_ROOT}")
endif()
