<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File Formats

The repository registers read-only `SdfFileFormat` adapters. Each adapter
exposes native data through a USD stage without rewriting the source asset.

| Source | Extensions | Plugin | Build option | Default | Conceptual Data Mapping |
| --- | --- | --- | --- | --- | --- |
| EnSight Gold | `.case`, `.encas` | `omniSciEnSightFileFormat` | `CAE_ENABLE_ENSIGHT` | On | [Mapping](../conceptual_data_mapping/ensight.md) |
| OpenFOAM | `.foam` | `omniSciOpenFoamFileFormat` | `CAE_ENABLE_OPENFOAM` | On | [Mapping](../conceptual_data_mapping/openfoam.md) |
| Eclipse reservoir | `.grdecl`, `.data`, `.egrid`, `.init`, `.unrst` | `omniSciEclipseFileFormat` | `CAE_ENABLE_ECLIPSE` | On | [Mapping](../conceptual_data_mapping/grdecl_egrid.md) |
| VTK legacy and serial XML | `.vtk`, `.vti`, `.vtr`, `.vts`, `.vtp`, `.vtu` | `omniSciVtkFileFormat` | `CAE_ENABLE_VTK` | On | [Mapping](../conceptual_data_mapping/vtk.md) |
| EDEM | `.dem` | `omniSciEdemFileFormat` | `CAE_ENABLE_EDEM` | Off | [Mapping](../conceptual_data_mapping/edem.md) |
| VTKHDF DEM particle series | `.vtkhdf` | `omniSciVtkHdfFileFormat` | `CAE_ENABLE_VTKHDF` | Off | – |
| FLASH AMR | `.flash` descriptor | `omniSciFlashFileFormat` | `CAE_ENABLE_FLASH` | Off | [Mapping](../conceptual_data_mapping/flash.md) |
| CGNS | `.cgns` | `omniSciCgnsFileFormat` | `CAE_ENABLE_CGNS` | Off | [Mapping](../conceptual_data_mapping/cgns.md) |
| NumPy | `.npy`, `.npz` | `omniSciNumpyFileFormat` | `CAE_ENABLE_NUMPY` | On when Python is found | [Mapping](../conceptual_data_mapping/numpy.md) |
| Trimesh surface meshes | `.stl`, `.ply`, `.3mf` | `omniSciTrimeshFileFormat` | `CAE_ENABLE_TRIMESH` | On when Python is found | [Mapping](../conceptual_data_mapping/trimesh.md) |
| NanoVDB | `.nvdb` | `omniSciNvdbFileFormat` | `CAE_ENABLE_NVDB` | On when Python is found | [Mapping](../conceptual_data_mapping/nanovdb.md) |
| Python adapter proxy | `.pydf` | `omniSciPythonProxyFileFormat` | `CAE_ENABLE_PYTHON_PROXY` | On when Python is found | Adapter-defined |

The registered extensions are defined by each plugin's
`resources/plugInfo.json.in`. A build may omit any optional reader.

## Reader Documentation

- [Architecture](architecture.md)
- [File-format arguments](arguments.md)
- [Time handling](time.md)
- [Asset resolution](asset_resolution.md)
- [File series and USD value clips](file_series.md)
- [Python proxy contract](python_proxy.md)
- [Performance patterns for reader developers](../development/file_format_performance.md)

Conceptual Data Mapping documents define the USD representation. This section
defines the mechanics shared by the readers.
