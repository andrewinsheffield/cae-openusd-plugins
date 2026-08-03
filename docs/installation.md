<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Installation and Packaging

## CMake Install

Install a configured build with:

```sh
cmake --install build --prefix /path/to/install
```

Without `--prefix`, the project defaults to `<build>/install`.

The normal install layout is:

```text
<prefix>/
  plugin/usd/
    plugInfo.json
    <schema and file-format plugin libraries>
    <PluginName>/resources/
    <PluginName>/python/          # Python-backed reader modules when enabled
  include/<SchemaPlugin>/
  lib/python/
    cae_openusd_plugins/
    pxr/<GeneratedSchemaModule>/
  PACKAGE-LICENSES/
  LICENSE.md
  requirements.txt
  cae-package-metadata.env
```

Only enabled plugins are installed. Generated `pxr` modules are present only
when schema Python bindings are built, and plugin-local `python/` directories
are present only for Python-backed readers.

## Use a CMake Install

Compatible OpenUSD applications can discover the installed plugins without
using Python. Add the plugin registry root to `PXR_PLUGINPATH_NAME` before
launching the application:

```sh
export PXR_PLUGINPATH_NAME="/path/to/install/plugin/usd${PXR_PLUGINPATH_NAME:+:$PXR_PLUGINPATH_NAME}"
```

The application must use an OpenUSD runtime compatible with the one used to
build the plugins.

For Python applications, add the installed package directory to `PYTHONPATH`
and use the registration helper:

```sh
export PYTHONPATH="/path/to/install/lib/python${PYTHONPATH:+:$PYTHONPATH}"
```

```python
import cae_openusd_plugins

# Validate the active OpenUSD runtime, extend the pxr namespace with the
# generated schemas, and register the installed plugin tree.
plugin_root = cae_openusd_plugins.register_usd_plugins()
print(plugin_root)
```

The helper also prepends the plugin root to `PXR_PLUGINPATH_NAME` for child
processes. Call `check_runtime()` separately only when an application needs to
inspect or display the diagnostic result without registering plugins.

## CPack Archives

CPack is enabled by default:

```sh
cmake --build build --target package
```

The default generator writes a ZIP archive and SHA-256 checksum under
`build/packages/`. The archive contains a top-level directory named from the
project version, OpenUSD version, Python ABI, platform, any build variant, and
Git revision.

Archives normally contain this project's install tree only. Dependencies linked
statically are already part of the plugin libraries; shared dependencies must
otherwise be available to the application at runtime. To copy known direct
shared-library dependencies into `plugin/usd`, configure with:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/usd;/path/to/dependencies" \
  -DCAE_PACKAGE_BUNDLE_DIRECT_DEPS=ON \
  -DCAE_PACKAGE_DEPENDENCY_ROOTS="/path/to/pugixml;/path/to/lz4;/path/to/zlib;/path/to/xz"
```

The dependency superbuild seeds these roots automatically. Its default static
format-dependency SDK does not need runtime bundling; a shared dependency SDK
enables bundling in its generated cache. The OpenUSD runtime is never bundled,
so consumers must provide the matching runtime.

## Python Wheels

Wheels use scikit-build-core and store the native install tree privately under
`cae_openusd_plugins/_runtime`. After generating the SDK caches described in
the [build guide](build.md), the validated artifact driver builds and tests the
project, then produces both the CPack archive and wheel:

```sh
cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
  -P cmake/ci/build.cmake
```

The wheel is written under `ci-artifacts/build/wheels/`. The driver derives the
wheel version, runtime dependency metadata, OpenUSD flavor marker, Python ABI,
platform tag, and Git revision from the selected build profile.

The wheel contains the plugin libraries, resources, generated schema modules,
and runtime registration helper. A `usd-core` wheel declares the matching
`usd-core` package as a dependency; an `openusd` wheel expects the compatible
OpenUSD runtime to be supplied by the application environment.

Install the wheel selected for the active OpenUSD flavor, version, Python ABI,
platform, and C++ ABI:

```sh
python -m pip install /path/to/cae_openusd_plugins-wheel.whl
```

After installation:

```python
import cae_openusd_plugins

# Registration validates the active OpenUSD runtime before loading plugins.
cae_openusd_plugins.register_usd_plugins()

from pxr import OmniSci, Usd
```

Importing `cae_openusd_plugins` alone intentionally has no registration side
effect. Set `CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT=warn` or
`CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT=error` only when an application wants
opt-in import-time diagnostics.

See [Troubleshooting](troubleshooting.md) for plugin discovery, runtime
compatibility, resolver tracing, and reader-specific diagnostics.

## Dependency Licenses

Install trees, archives, and wheels copy registered dependency notices under
`PACKAGE-LICENSES`; reader-specific notices are registered only when that
reader is enabled. Wheels also include the project license in their standard
distribution metadata. See [Third-party licenses](third_party_licenses.md).
