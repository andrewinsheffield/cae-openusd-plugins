<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Troubleshooting

This page covers the common boundaries between plugin discovery, OpenUSD
runtime compatibility, asset resolution, reader selection, and lazy value
loading.

## Confirm Runtime Compatibility

Native plugins must use an OpenUSD runtime compatible with the SDK used to
build them. A wheel must also match the OpenUSD flavor, version, Python ABI,
platform, and C++ ABI encoded by its artifact profile.

Python applications can inspect compatibility before registering anything:

```python
import cae_openusd_plugins

result = cae_openusd_plugins.check_runtime()
print(result.message)
```

`register_usd_plugins()` performs the same check and raises an exception when
the active runtime is incompatible. Importing `cae_openusd_plugins` by itself
does not register plugins.

## Confirm Plugin Discovery

For a CMake install or extracted archive, set `PXR_PLUGINPATH_NAME` before
starting the application:

```sh
export PXR_PLUGINPATH_NAME="/path/to/install/plugin/usd${PXR_PLUGINPATH_NAME:+:$PXR_PLUGINPATH_NAME}"
```

The named directory must contain the installed top-level `plugInfo.json`.
Changing this variable after an application has already populated OpenUSD's
plugin registry may be too late; set it before launch.

Python applications should normally use:

```python
import cae_openusd_plugins

plugin_root = cae_openusd_plugins.register_usd_plugins()
print(plugin_root)
```

That helper validates the runtime, registers the plugin tree, extends the
active `pxr` namespace with generated schema modules, and updates
`PXR_PLUGINPATH_NAME` for child processes.

If an extension is not recognized, also confirm that its reader was enabled in
the build. The [file-format index](file_formats/README.md) lists each build
option and default.

## Distinguish Open Failure From Lazy-Value Failure

Opening a native asset normally discovers structure and registers lazy array
loaders. It does not load every array.

- If `Usd.Stage.Open()` or `Sdf.Layer.FindOrOpen()` fails, investigate plugin
  discovery, reader selection, asset resolution, and metadata parsing.
- If the stage opens but `UsdAttribute.Get()` fails, investigate the selected
  time code, source payload, cache lifetime, and reader-specific diagnostics.
- If an attribute exists but returns no value at the default time, request
  `Usd.TimeCode.EarliestTime()` or an explicit sample. Potentially varying
  native values are represented as time samples, including single-state values
  at local time `0`.

```python
from pxr import Usd

attr = prim.GetAttribute("omni:sci:array:points:value")
print(attr.GetTimeSamples())
value = attr.Get(Usd.TimeCode.EarliestTime())
```

See [Time handling](file_formats/time.md) for reader time conventions.

## Trace Asset Resolution

Enable the shared resolver trace when an asset cannot be localized, a child
file cannot be found, or a deferred load loses access to its source:

```console
TF_DEBUG=CAE_RESOLVER_ASSET <application> <arguments>
```

The trace identifies the original asset, resolved path, local-access strategy,
explicit child resolution, directory-scan decision, and lease cleanup.

Remember that OpenUSD's Asset Resolver can open a known child identifier but
cannot portably enumerate directories. Wildcard and adjacency-based datasets
may require a local filesystem. See
[Asset resolution](file_formats/asset_resolution.md).

## Enable Reader Diagnostics

Readers register the following `TF_DEBUG` symbols:

| Symbol | Area |
| --- | --- |
| `CAE_CGNS_FILEFORMAT` | CGNS |
| `CAE_ECLIPSE_FILEFORMAT` | GRDECL, EGRID, INIT, and UNRST |
| `CAE_EDEM_FILEFORMAT` | EDEM |
| `CAE_ENSIGHT_FILEFORMAT` | EnSight |
| `CAE_FLASH_FILEFORMAT` | FLASH |
| `CAE_OPENFOAM_FILEFORMAT` | OpenFOAM |
| `CAE_PYTHON_FILEFORMAT` | Python proxy and Python-backed readers |
| `CAE_VTK_FILEFORMAT` | Legacy and XML VTK |
| `CAE_FILE_FORMAT_DATA` | Lazy sample registration, loading, and retention |
| `CAE_RESOLVER_ASSET` | Resolver-backed asset access |

For example:

```console
TF_DEBUG="CAE_VTK_FILEFORMAT CAE_FILE_FORMAT_DATA" usdview dataset.vtu
```

Start with the format-specific symbol and add `CAE_FILE_FORMAT_DATA` only when
the problem occurs during value access. Debug output can be verbose and should
not normally be enabled in production.

## Check File-Format Arguments

Flat layer arguments are string-valued and form part of the layer identifier.
Use `Sdf.Layer.CreateIdentifier()` instead of constructing
`:SDF_FORMAT_ARGS:` syntax manually:

```python
from pxr import Sdf

identifier = Sdf.Layer.CreateIdentifier(
    "/data/simulation.vtu",
    {"mountPath": "/World/Simulation", "cacheMode": "none"},
)
layer = Sdf.Layer.FindOrOpen(identifier)
```

Typed arguments on a payload-bearing prim are dynamic. Changing a composed
argument recomposes and rereads the payload with the new structure settings;
the payload does not need to be removed and re-added.

See [File-format arguments](file_formats/arguments.md) for accepted values and
reader-specific keys.

## Collect a Useful Problem Report

Include:

- plugin package or commit version;
- OpenUSD distribution and version;
- operating system, architecture, and Python version when applicable;
- enabled reader and relevant build options;
- asset extension and whether its layout uses child files or directory scans;
- the exact layer identifier or payload arguments, with credentials removed;
- whether failure occurs during open, traversal, or `UsdAttribute.Get()`;
- relevant `TF_DEBUG` output;
- a minimal shareable input when possible.

Follow [SUPPORT.md](../SUPPORT.md) for the project's support channels and scope.
