<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CAE OpenUSD Plugins Superbuild

This directory owns the optional dependency SDK builder. The main project stays
plain CMake and continues to consume dependencies through `find_package()`.

The superbuild is for CI and developer convenience when a matrix row needs a
known SDK prefix. The normal top-level project remains the public build entry
point; this directory only prepares dependency roots and CMake initial-cache
handoff files.

```bash
cmake -P cmake/ci/setup.cmake
cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/superbuild.cmake
cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/build.cmake
```

By default, `superbuild.cmake` keeps build scratch under `_build/superbuild`,
writes file-format dependencies under `_build/sdk`, and writes the USD provider
under `_build/sdk_usd`. The generated cache files live with the prefixes they
describe:

- `_build/sdk/cae-format-sdk-cache.cmake`
- `_build/sdk_usd/cae-usd-sdk-cache.cmake`

The CI scripts always configure with Ninja. Build steps normally let Ninja
choose its own parallelism. Set `OMNI_REPO_BUILD_JOBS` only for matrix rows that
need a memory cap; `superbuild.cmake` forwards it to
`CMAKE_BUILD_PARALLEL_LEVEL` and to source OpenUSD's `build_usd.py --jobs`.
`build.cmake` emits one package and one wheel from the generated SDK caches.
Static file-format dependencies are the default, so package identity does not
encode dependency bundling.

## File Map

- `CMakeLists.txt`: declares the dependency graph and writes the format/USD
  cache files into their SDK prefixes.
- `scripts/build_openusd_sdk.cmake`: adapts the source OpenUSD matrix row to
  `build_usd.py`.
- `scripts/ensure_python_packages.cmake`: installs isolated Python packages
  used by OpenUSD build steps and by the generated test SDK.
- `../ci/setup.cmake`: downloads the CI Python profile and installs CMake/Ninja.
- `../ci/superbuild.cmake`: CMake script-mode entry point for dependency SDK
  artifacts.
- `../ci/build.cmake`: CMake script-mode entry point for consuming an SDK cache
  and building the project, CPack package, and wheel artifacts.
- `../ci/test_wheel.cmake`: CMake script-mode entry point for testing a wheel
  in a fresh virtual environment with normal pip dependency resolution.
- `../ci/common.cmake`: shared helpers for CMake script-mode drivers.

## USD Flavor

`CAE_SUPERBUILD_USD_FLAVOR` selects the USD provider shape when
`CAE_SUPERBUILD_ENABLE_USD` is enabled. It does not select split vs.
monolithic linkage by itself; that should be a separate OpenUSD build option if
we need source-built monolithic OpenUSD later.

- `openusd`: build an OpenUSD SDK from source with `build_usd.py`. The current
  implementation builds the split-library flavor used by the Kit/Packman
  runtime compatibility experiments.
- `usd-core`: prepare the build-only SDK shim for PyPI `usd-core`. This targets
  the runtime layout and ABI of the `usd-core` wheel; it is not just a generic
  "monolithic OpenUSD" source build.

To build only the file-format dependency SDK and leave USD to the normal
top-level consumer workflow, configure the superbuild with
`CAE_SUPERBUILD_ENABLE_USD=OFF`.

The CI matrix currently supports:

| Flavor | Versions | Notes |
|---|---|---|
| `openusd` | `25.02`, `25.11` | Source-built split SDK, wheel-tested against the matching Kit/Packman runtime. |
| `usd-core` | `25.11`, `26.05` | Build-only compile shim, wheel-tested in a fresh venv that installs matching PyPI `usd-core`. |

`usd-core` `25.02` is intentionally not in the supported matrix because the
current shim path targets the monolithic runtime layout covered by the rows
above.

Format dependencies are built when `CAE_SUPERBUILD_ENABLE_FORMAT_DEPS` is on.
That includes pugixml, LZ4, zlib, liblzma, HDF5, and CGNS. The generated cache
enables the consuming plugins, enables compressed VTK XML support, and seeds
`CAE_PACKAGE_DEPENDENCY_ROOTS`. These dependencies install into the common
`CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX`, and the generated format cache
appends that SDK prefix to the `CMAKE_PREFIX_PATH` environment during configure.

The superbuild also installs Python packages used by the top-level CTest suite
under `${CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX}/python`. That directory
contains `pytest`, `numpy`, `trimesh`, and `warp-lang`; `usd-core` rows also
install the matching PyPI `usd-core` runtime there. The generated format cache
exposes this path through `CAE_TEST_RUNTIME_PYTHONPATH`.

`CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE` selects `shared` or `static` linkage for
those file-format dependencies only. It does not affect OpenUSD. The default is
`static`, which links the generated non-USD dependency libraries into the
plugins. Shared format-dependency SDKs turn on `CAE_PACKAGE_BUNDLE_DIRECT_DEPS`
so package/wheel payloads include generated dependency runtimes without changing
the artifact name.

Upstream support is not perfectly uniform. pugixml, LZ4, liblzma, and HDF5
honor the selected linkage directly. CGNS always creates a static library and
uses `CGNS_BUILD_SHARED` to decide whether to add the shared library. zlib's
CMake project defines and installs both `zlib` and `zlibstatic`; making zlib
strictly shared-only or static-only would require a local patch. The generated
format cache still forces the consuming plugin build to prefer the requested
linkage, including `ZLIB_USE_STATIC_LIBS` and exact static zlib/liblzma library
hints for static SDKs.

The default top-level project is still a normal CMake consumer. If you do not
use the superbuild cache, `find_package()` and the top-level feature options
control whether external/system dependencies are consumed.

The generated cache files are the handoff to the main project. The format cache
seeds `CAE_PACKAGE_DEPENDENCY_ROOTS`, `CAE_TEST_RUNTIME_LIBRARY_DIRS`,
`CAE_TEST_RUNTIME_PYTHONPATH`, and the feature toggles implied by the generated
dependency SDK. The USD cache seeds `USD_ROOT` and any USD-provider-specific
packaging metadata.

Each cache file appends its own prefix to the `CMAKE_PREFIX_PATH` environment
variable during configure. `CMAKE_PREFIX_PATH` is intentionally not written as a
cache variable.

`CAE_TEST_RUNTIME_LIBRARY_DIRS` is consumed by the normal CTest helpers. It
keeps build-tree tests from relying on absolute install RPATHs: Linux and macOS
tests add these entries to the native loader path, while Windows tests add them
to `PATH`. Developers using their own dependency SDK can pass the same variable
when configuring the top-level project.

For `usd-core` rows, the format cache also adds the native runtime directory
inside the pip-installed `usd-core` package. On Windows, the normal CTest helper
uses that entry as `PXR_USD_WINDOWS_DLL_PATH` so `pxr.Tf` imports modules such
as `_tf.pyd` without searching unrelated dependency directories.

`CAE_TEST_RUNTIME_PYTHONPATH` is also consumed by the normal CTest helpers. It
keeps test-only Python packages out of the CI bootstrap environment and makes
the generated SDK describe the full local test runtime.

The CTest helpers prepend the staged install paths and these cache-provided
runtime paths, then append any existing `PYTHONPATH` or native loader path from
the process environment captured during CMake configure.

The CTest helpers do not infer external USD or Python runtime paths from
`USD_ROOT` or `Python3_ROOT_DIR`. The superbuild writes those paths into the
test runtime variables when they are part of the generated SDK; custom SDK
users should pass the same variables explicitly.

## Example Matrix Rows

Source OpenUSD SDK for a Packman-runtime validation lane:

```bash
cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/superbuild.cmake
```

PyPI `usd-core` compile shim:

```bash
cmake -DCAE_USD_FLAVOR=usd-core -DCAE_USD_VERSION=26.05 \
  -P cmake/ci/superbuild.cmake
```

Format dependency SDK only, with USD supplied later by the normal top-level
consumer build:

```bash
cmake -S cmake/superbuild -B _build/format-deps-sdk -G Ninja \
  -DCAE_SUPERBUILD_ENABLE_USD=OFF \
  -DCAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX=$PWD/_build/sdk
cmake --build _build/format-deps-sdk --target cae-sdk
```

## Review Notes

The main project should not be included from this directory. Keeping the SDK
builder separate preserves the default external-dependency workflow and keeps CI
USD matrix rows explicit.

The generated cache file is intentionally small. If a new dependency needs a
main-build option, add it to the cache handoff rather than making the top-level
project infer that it came from the superbuild.

Packman compatibility should be validated in runtime test jobs, not encoded as
a Packman build dependency here.
