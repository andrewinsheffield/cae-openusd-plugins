<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Third-Party Licenses

The repository keeps license texts and required notices for direct dependencies
and redistributed data under `tpl_licenses/`. Package builds install these files
under `PACKAGE-LICENSES`. Python runtime dependencies are installed separately
and are not redistributed by this project, but their license texts are included
with the project distribution for reference.

| Library or data | Used by | Distribution model | License reference |
| --- | --- | --- | --- |
| pugixml | VTK XML reader | External native dependency | [`tpl_licenses/pugixml-LICENSE.md`](../tpl_licenses/pugixml-LICENSE.md) |
| LZ4 | Compressed VTK XML payloads | External native dependency | [`tpl_licenses/lz4-LICENSE.txt`](../tpl_licenses/lz4-LICENSE.txt) |
| zlib | Compressed VTK XML payloads | External native dependency | [`tpl_licenses/zlib-LICENSE.txt`](../tpl_licenses/zlib-LICENSE.txt) |
| XZ Utils / liblzma | Compressed VTK XML payloads | External native dependency | [`tpl_licenses/xz-LICENSE.txt`](../tpl_licenses/xz-LICENSE.txt) |
| CGNS | CGNS reader | External native dependency | [`tpl_licenses/cgns-LICENSE.txt`](../tpl_licenses/cgns-LICENSE.txt) |
| HDF5 | EDEM, FLASH AMR, and commonly CGNS | External native dependency | [`tpl_licenses/hdf5-LICENSE.txt`](../tpl_licenses/hdf5-LICENSE.txt) |
| OpenUSD | All plugins and schema generation | External native runtime | [`tpl_licenses/openusd-25-LICENSE.txt`](../tpl_licenses/openusd-25-LICENSE.txt) for 25.x; [`tpl_licenses/openusd-26-LICENSE.txt`](../tpl_licenses/openusd-26-LICENSE.txt) for 26.x; [required notice](../tpl_licenses/openusd-NOTICE.txt) |
| oneTBB | OpenUSD/runtime parallelism where used | External runtime | [`tpl_licenses/onetbb-LICENSE.txt`](../tpl_licenses/onetbb-LICENSE.txt); [third-party program notices](../tpl_licenses/onetbb-third-party-programs.txt) |
| NumPy | NumPy, Trimesh, and NanoVDB adapters | Separate Python runtime | [`tpl_licenses/numpy-LICENSE.txt`](../tpl_licenses/numpy-LICENSE.txt) |
| Trimesh | Surface-mesh adapter | Separate Python runtime | [`tpl_licenses/trimesh-LICENSE.md`](../tpl_licenses/trimesh-LICENSE.md) |
| NVIDIA Warp | NanoVDB adapter | Separate Python runtime | [`tpl_licenses/warp-LICENSE.md`](../tpl_licenses/warp-LICENSE.md) |

The OpenUSD texts match the supported v25.02/v25.11 and v26.05 release
families. The other runtime reference copies come from oneTBB 2021.12.0,
NumPy 2.5.1, Trimesh 4.12.2, and NVIDIA Warp 1.15.0. Update the corresponding
license file whenever the validated dependency changes its licensing terms.

The project itself is distributed under the [Apache License 2.0](../LICENSE.md).
