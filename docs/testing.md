<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Testing

## CTest

Tests are enabled by default. Build the project, then run the configured suite:

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Every registered pytest suite requires the `cae_install` fixture. CTest runs
that fixture once for the selected test set, replacing `build/test_install`
with a fresh install of the current build. A separate manual install is not
required.

Tests use two labels:

- `unit`: schema and in-memory behavior;
- `integration`: file-backed reader behavior.

Run either group with:

```sh
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
```

Integration tests are registered only for readers enabled in the build. List
the configured tests or run a focused test by name with:

```sh
ctest --test-dir build -N
ctest --test-dir build -R '^test_vtk_xml_fileformat$' --output-on-failure
```

Use `-V` when the full command and generated test environment are needed for
diagnosis.

## CTest Runtime Environment

The test helper configures each pytest process with:

- `PXR_PLUGINPATH_NAME` pointing at the staged plugin registry;
- `PYTHONPATH` containing the staged generated modules, followed by
  `CAE_TEST_RUNTIME_PYTHONPATH` and the inherited `PYTHONPATH`;
- `PATH`, `LD_LIBRARY_PATH`, or `DYLD_LIBRARY_PATH`, depending on the platform,
  containing the staged plugin directory, followed by
  `CAE_TEST_RUNTIME_LIBRARY_DIRS` and the inherited loader path.

The generated superbuild caches provide the extra Python and native runtime
directories for their selected OpenUSD and dependency profiles. Custom SDK
users can set the same CMake cache variables when configuring the project.

## Direct pytest

CTest is the preferred entry point because it stages an install tree and
constructs the complete runtime environment. To run pytest against a completed
install tree, first activate its compatible OpenUSD runtime and any shared
dependencies, then expose the installed Python modules and plugin registry:

```sh
export PYTHONPATH="/path/to/install/lib/python${PYTHONPATH:+:$PYTHONPATH}"
export PXR_PLUGINPATH_NAME="/path/to/install/plugin/usd${PXR_PLUGINPATH_NAME:+:$PXR_PLUGINPATH_NAME}"

python -m pytest tests/python/omni_sci -v
python -m pytest -m integration tests/python/file_format_vtk -v
```

Run only paths supported by the plugins and runtime packages in that install.
With the repository's Python test dependencies installed, the complete
in-memory subset is:

```sh
python -m pytest -m "not integration" tests/python
```

## Wheel Smoke Tests

`cmake/ci/test_wheel.cmake` creates a fresh virtual environment, installs a
selected wheel with normal dependency resolution, validates its private runtime
layout and dependency metadata, and runs both the repository and benchmark
pytest suites.

After building a `usd-core` wheel as described in the
[installation guide](installation.md), run:

```sh
cmake -P cmake/ci/setup.cmake
cmake -DCAE_USD_FLAVOR=usd-core \
  -DCAE_USD_VERSION=26.05 \
  -DCAE_WHEEL_PATTERN="wheels/*usdcore*.whl" \
  -P cmake/ci/test_wheel.cmake
```

The default artifact root is `ci-artifacts/build/`. Override it with
`CAE_ARTIFACT_DIR` when the wheel is elsewhere. The pattern must select exactly
one wheel.

An `openusd` wheel is tested against the matching Packman runtime. Pull that
runtime before running the wheel test:

```sh
cmake -DCAE_SETUP_PACKMAN_VARIANT=usd \
  -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/setup.cmake
cmake -DCAE_USD_FLAVOR=openusd \
  -DCAE_USD_VERSION=25.11 \
  -DCAE_WHEEL_PATTERN="wheels/*openusd*.whl" \
  -P cmake/ci/test_wheel.cmake
```

## Documentation Validation

Examples and support tables should be checked against:

- `source/schemas/*/schema.usda` for schema classes and properties;
- `source/file_formats/*/resources/plugInfo.json.in` for registered extensions;
- reader headers and parsing code for flat arguments;
- `tests.cmake` for the enabled test matrix and `tests/python/` paths it
  registers for observable behavior;
- CMake install and packaging rules for build artifacts.

Existing prose and historical decision records are not authoritative when they
disagree with those sources.
