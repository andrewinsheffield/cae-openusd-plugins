# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cae_add_file_format(omniSciFlashFileFormat
    ENABLE_VAR        CAE_ENABLE_FLASH
    DEFAULT           OFF
    FIND_PACKAGE      HDF5
    FIND_PACKAGE_ARGS 1.10 COMPONENTS C
    SOURCES
        src/OmniSciFlashFileFormat.cpp
        src/OmniSciFlashFileFormatRegistration.cpp
    USD_LIBRARIES
        ar
        js
        pcp
        usd
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciFlash
        omniSci
        HDF5::HDF5
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciFlash
        omniSci
    LICENSE_FILES
        "${CAE_SOURCE_ROOT}/tpl_licenses/hdf5-LICENSE.txt"
)

if(TARGET omniSciFlashFileFormat AND COMMAND cae_package_register_dependency_target)
    cae_package_register_dependency_target(HDF5::HDF5
        BASENAME_REGEX "^(lib)?hdf5(_.*)?(\\.|$)")
endif()
if(TARGET omniSciFlashFileFormat AND HDF5_ROOT AND COMMAND cae_package_register_dependency_root)
    cae_package_register_dependency_root("${HDF5_ROOT}")
endif()
