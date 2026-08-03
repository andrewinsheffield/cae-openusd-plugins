# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# tests.cmake
#
# CTest registration for CAE USD plugin tests.
#
# The cae_install fixture refreshes build/test_install before any test runs,
# so no separate cmake --install step is needed:
#
#   cmake --build build --parallel
#   ctest --test-dir build -V
#
# Labels:
#   unit        -- fast, in-memory only, no file I/O
#   integration -- loads .usda fixtures from tests/data/

# -----------------------------------------------------------------------
# Fixture: clean and install to staging dir before any test runs
# -----------------------------------------------------------------------
add_test(
    NAME    cae_install
    COMMAND ${CMAKE_COMMAND}
                "-DCAE_INSTALL_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DCAE_INSTALL_PREFIX=${CAE_TEST_INSTALL_DIR}"
                -P "${CAE_SOURCE_ROOT}/cmake/CaeCleanInstall.cmake"
)
set_tests_properties(cae_install PROPERTIES
    FIXTURES_SETUP cae_install
)

# -----------------------------------------------------------------------
# Test suites
# -----------------------------------------------------------------------
cae_add_pytest(test_omni_sci
    TESTS   tests/python/omni_sci
    PLUGINS omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_cgns
    TESTS   tests/python/omni_sci_cgns
    PLUGINS omniSciCgns
    LABELS  unit
)

cae_add_pytest(test_omni_sci_file_format_args
    TESTS   tests/python/omni_sci_file_format_args
    PLUGINS omniSciFileFormatArgs
    LABELS  unit
)

cae_add_pytest(test_omni_sci_ensight
    TESTS   tests/python/omni_sci_ensight
    PLUGINS omniSciEnSight omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_openfoam
    TESTS   tests/python/omni_sci_openfoam
    PLUGINS omniSciOpenFoam omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_vtk
    TESTS   tests/python/omni_sci_vtk
    PLUGINS omniSciVtk omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_edem
    TESTS   tests/python/omni_sci_edem
    PLUGINS omniSciEdem omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_flash
    TESTS   tests/python/omni_sci_flash
    PLUGINS omniSciFlash omniSci
    LABELS  unit
)

cae_add_pytest(test_omni_sci_reservoir
    TESTS   tests/python/omni_sci_reservoir
    PLUGINS omniSciReservoir omniSci
    LABELS  unit
)

if(CAE_ENABLE_ENSIGHT)
    cae_add_pytest(test_ensight_fileformat
        TESTS   tests/python/file_format_ensight
        PLUGINS omniSciEnSightFileFormat omniSciFileFormatArgs omniSciEnSight omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_OPENFOAM)
    cae_add_pytest(test_openfoam_fileformat
        TESTS   tests/python/file_format_openfoam
        PLUGINS omniSciOpenFoamFileFormat omniSciFileFormatArgs omniSciOpenFoam omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_ECLIPSE)
    cae_add_pytest(test_grdecl_fileformat
        TESTS   tests/python/file_format_grdecl
        PLUGINS omniSciEclipseFileFormat omniSciFileFormatArgs omniSciReservoir omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )

    cae_add_pytest(test_egrid_fileformat
        TESTS   tests/python/file_format_egrid
        PLUGINS omniSciEclipseFileFormat omniSciFileFormatArgs omniSciReservoir omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )

    cae_add_pytest(test_init_fileformat
        TESTS   tests/python/file_format_init
        PLUGINS omniSciEclipseFileFormat omniSciFileFormatArgs omniSciReservoir omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )

    cae_add_pytest(test_unrst_fileformat
        TESTS   tests/python/file_format_unrst
        PLUGINS omniSciEclipseFileFormat omniSciFileFormatArgs omniSciReservoir omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_VTK)
    cae_add_pytest(test_vtk_fileformat
        TESTS   tests/python/file_format_vtk
        PLUGINS omniSciVtkFileFormat omniSciFileFormatArgs omniSciVtk omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_VTK)
    cae_add_pytest(test_vtk_xml_fileformat
        TESTS   tests/python/file_format_vtk_xml
        PLUGINS omniSciVtkFileFormat omniSciFileFormatArgs omniSciVtk omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_EDEM)
    cae_add_pytest(test_edem_fileformat
        TESTS   tests/python/file_format_edem
        PLUGINS omniSciEdemFileFormat omniSciFileFormatArgs omniSciEdem omniSciCae omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_FLASH)
    if(COMMAND cae_resolve_package)
        cae_resolve_package(HDF5 1.10 COMPONENTS C)
    else()
        find_package(HDF5 1.10 COMPONENTS C REQUIRED)
    endif()
    add_executable(cae_generate_flash_test_data
        "${CAE_SOURCE_ROOT}/tests/data/FLASH/generate_minimal_flash.cpp")
    target_link_libraries(cae_generate_flash_test_data PRIVATE HDF5::HDF5 cae_project_warnings)

    set(_cae_flash_test_data_dir "${CMAKE_BINARY_DIR}/test_data/FLASH")
    set(_cae_flash_test_outputs
        "${_cae_flash_test_data_dir}/hdf5_plt_cnt_0000"
        "${_cae_flash_test_data_dir}/hdf5_plt_cnt_0001"
        "${_cae_flash_test_data_dir}/minimal.flash"
        "${_cae_flash_test_data_dir}/series.flash")
    add_custom_command(
        OUTPUT ${_cae_flash_test_outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_cae_flash_test_data_dir}"
        COMMAND $<TARGET_FILE:cae_generate_flash_test_data> "${_cae_flash_test_data_dir}"
        DEPENDS cae_generate_flash_test_data
        VERBATIM)
    add_custom_target(cae_flash_test_data ALL DEPENDS ${_cae_flash_test_outputs})

    cae_add_pytest(test_flash_fileformat
        TESTS   tests/python/file_format_flash
        PLUGINS omniSciFlashFileFormat omniSciFileFormatArgs omniSciFlash omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
        ENV_VARS "CAE_FLASH_TEST_DATA_DIR=${_cae_flash_test_data_dir}"
    )
endif()

# -----------------------------------------------------------------------
# CGNS file format integration tests -- only registered when the plugin
# is enabled (CAE_ENABLE_CGNS=ON) so the omniSciCgnsFileFormat target exists.
# -----------------------------------------------------------------------
if(CAE_ENABLE_CGNS)
    cae_add_pytest(test_cgns_fileformat
        TESTS   tests/python/file_format_cgns
        PLUGINS omniSciCgnsFileFormat omniSciFileFormatArgs omniSciCgns omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_PYTHON_PROXY)
    cae_add_pytest(test_python_proxy_fileformat
        TESTS   tests/python/file_format_python_proxy
        PLUGINS omniSciPythonProxyFileFormat omniSciFileFormatArgs
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_NUMPY)
    cae_add_pytest(test_npz_fileformat
        TESTS   tests/python/file_format_npz
        PLUGINS omniSciNumpyFileFormat omniSciFileFormatArgs omniSciCae omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_TRIMESH)
    cae_add_pytest(test_trimesh_fileformat
        TESTS   tests/python/file_format_trimesh
        PLUGINS omniSciTrimeshFileFormat omniSciFileFormatArgs omniSciCae omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()

if(CAE_ENABLE_NVDB)
    cae_add_pytest(test_nvdb_fileformat
        TESTS   tests/python/file_format_nvdb
        PLUGINS omniSciNvdbFileFormat omniSciFileFormatArgs omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()
