<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Build

## Prerequisites

The top-level project is a CMake 3.20+ C++17 project. It requires an OpenUSD
installation that includes headers, libraries, CMake target exports, and
`usdGenSchema`. Python 3.10+ is required for generated Python schema bindings,
Python-backed file formats, and wheels.

Native file-format plugins without additional dependencies are enabled by
default: EnSight, OpenFOAM, and Eclipse reservoir formats. VTK is also enabled
by default and requires pugixml, LZ4, zlib, and liblzma. Readers that require
CGNS or HDF5 are disabled by default.

Python-backed readers are enabled when a Python development runtime is found.
Their Python packages are runtime dependencies and are not installed by a
normal CMake build.

| Reader | Additional requirements |
| --- | --- |
| VTK legacy and XML | pugixml, LZ4, zlib, and liblzma |
| CGNS | CGNS and its configured backend, commonly HDF5 |
| EDEM and FLASH AMR | HDF5 C library 1.10+ |
| Python proxy | Python development runtime; adapter-specific packages at runtime |
| NumPy | Python development runtime; NumPy at runtime |
| Trimesh | Python development runtime; NumPy and Trimesh at runtime |
| NanoVDB | Python development runtime; NumPy and Warp at runtime |

## Configure and Build

Point `CMAKE_PREFIX_PATH` at OpenUSD and the native dependencies for every
reader you enable. The default build therefore needs the VTK dependencies:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/usd;/path/to/dependencies"
cmake --build build --parallel
```

`USD_ROOT` and `pxr_DIR` are also accepted when a more explicit OpenUSD hint is
needed. The selected OpenUSD installation must provide `usdGenSchema`.

To build only the schema plugins and readers without external native
dependencies, disable VTK. CGNS, EDEM, and FLASH AMR are already off by
default:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/usd \
  -DCAE_ENABLE_VTK=OFF
```

## Reader Options

| Option | Default | Reader |
| --- | --- | --- |
| `CAE_ENABLE_ENSIGHT` | `ON` | EnSight Gold |
| `CAE_ENABLE_OPENFOAM` | `ON` | OpenFOAM |
| `CAE_ENABLE_ECLIPSE` | `ON` | GRDECL, EGRID, INIT, UNRST |
| `CAE_ENABLE_VTK` | `ON` | Legacy and serial XML VTK |
| `CAE_ENABLE_EDEM` | `OFF` | EDEM |
| `CAE_ENABLE_VTKHDF` | `OFF` | VTKHDF DEM particle series |
| `CAE_ENABLE_FLASH` | `OFF` | FLASH AMR |
| `CAE_ENABLE_CGNS` | `OFF` | CGNS |
| `CAE_ENABLE_PYTHON_PROXY` | `ON` when Python is found | Python proxy |
| `CAE_ENABLE_NUMPY` | `ON` when Python is found | NPY and NPZ |
| `CAE_ENABLE_TRIMESH` | `ON` when Python is found | STL, PLY, 3MF |
| `CAE_ENABLE_NVDB` | `ON` when Python is found | NanoVDB |

Runtime Python packages such as NumPy, Trimesh, and Warp are not installed by a
normal CMake build. Install only the packages required by the readers you use.

OpenUSD distributions are normalized through the repository's `CaeUSD` CMake
package. See [OpenUSD CMake integration](development/openusd_integration.md)
for its targets, capability components, tool runtime, and limitations.

## Project Options

| Option | Default | Purpose |
| --- | --- | --- |
| `CAE_BUILD_TESTS` | `ON` | Register CTest and pytest tests. |
| `CAE_BUILD_SCHEMA_PYTHON_BINDINGS` | `ON` | Build generated `pxr.OmniSci*` modules. |
| `CAE_ENABLE_STRICT_WARNINGS` | `ON` | Enable strict warnings for repository-owned code. |
| `CAE_WARNINGS_AS_ERRORS` | `ON` | Treat those warnings as errors. |
| `CAE_ENABLE_CPACK` | `ON` | Add CPack package targets. |
| `CAE_INSTALL_RPATH_USE_LINK_PATH` | `ON` | Include linked-library directories in installed runtime search paths. |
| `CAE_PACKAGE_BUNDLE_DIRECT_DEPS` | `OFF` | Copy known direct dependency runtimes into packages. |

## Dependency Superbuild

`cmake/superbuild/` can build a pinned dependency SDK and produce CMake initial
cache files. It is a convenience for CI and reproducible developer builds; the
top-level project remains the public build entry point.

```sh
cmake -P cmake/ci/setup.cmake
cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/superbuild.cmake

cmake -S . -B build-sdk -G Ninja \
  -C _build/sdk/cae-format-sdk-cache.cmake \
  -C _build/sdk_usd/cae-usd-sdk-cache.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdk --parallel
```

The maintained matrix covers source OpenUSD 25.02 and 25.11 plus PyPI
`usd-core` 25.11 and 26.05. See
[`cmake/superbuild/README.md`](../cmake/superbuild/README.md) for the generated
SDK layout and advanced options.

## Next Steps

- [Install or package the build](installation.md).
- [Run the test suite](testing.md).
- [Add a schema](development/adding_a_schema.md) or
  [add a reader](development/adding_a_file_format.md).
