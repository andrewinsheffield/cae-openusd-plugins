<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Usage

## Register the Plugins

Register the installed plugin bundle before importing its generated schema
modules:

```python
import cae_openusd_plugins

# Validate that this wheel matches the active OpenUSD runtime, register the
# bundled plug-ins, and make generated modules such as pxr.OmniSci importable.
plugin_root = cae_openusd_plugins.register_usd_plugins()
```

`register_usd_plugins()` raises an exception if the wheel and the active
OpenUSD runtime are incompatible. Applications that do not initialize the
plugins from Python can use the environment variables described in
[Installation](installation.md#use-a-cmake-install).

## Open a Native Asset

Once registered, a supported native file can be opened like any other USD
layer. VTK readers are enabled in the default build:

```python
from pxr import Usd

stage = Usd.Stage.Open("/data/simulation.vtu")
if not stage:
    raise RuntimeError("Could not open /data/simulation.vtu")

root = stage.GetDefaultPrim()
print(root.GetPath())  # /simulation
```

The native file remains the source of truth. Opening it creates the USD
hierarchy and metadata; array values are read from the backing file when they
are requested. By default, the root prim name is derived from the filename and
made safe for use as a USD identifier.

Asset identifiers are resolved through the application's active
`ArResolver`, so the same code can also work with resolver-supported,
non-filesystem identifiers.

The exact prim hierarchy and applied schemas depend on the reader; see the
[Conceptual Data Mappings](conceptual_data_mapping/README.md).

## Read Lazy Array Values

Native readers expose array data through the OmniSci schema. Requesting an
array attribute causes the reader to load its backing value:

```python
from pxr import Usd

points_attr = root.GetAttribute("omni:sci:array:points:value")
points = points_attr.Get(Usd.TimeCode.EarliestTime())
print(f"Loaded {len(points)} points")
```

The reader's `cacheMode` argument controls whether loaded values are retained.
See [File-format arguments](file_formats/arguments.md) for the supported cache
policies and time-selection options.

## Compose a Native Asset as a Payload

Use a payload when the native dataset should be composed into a larger stage.
Format-specific arguments can be authored with the generated API:

```python
from pxr import OmniSciFileFormatArgs, Usd, UsdGeom

stage = Usd.Stage.CreateInMemory()
payload_prim = UsdGeom.Xform.Define(stage, "/World/Simulation").GetPrim()

# Author typed CGNS reader settings on the prim that carries the payload.
reader_api = OmniSciFileFormatArgs.CgnsAPI.Apply(payload_prim)
base_name_attr = reader_api.CreateBaseNameAttr()
zone_name_attr = reader_api.CreateZoneNameAttr()
base_name_attr.Set("Base")
zone_name_attr.Set("Zone")

# The native asset's default prim is composed at /World/Simulation.
payload_prim.GetPayloads().AddPayload("/data/simulation.cgns")
stage.Load(payload_prim.GetPath())

# Later, change an argument using the same attribute. OpenUSD automatically
# recomposes the dynamic payload and rereads it using the new arguments.
zone_name_attr.Set("AlternateZone")
```

This example requires a build with the optional CGNS reader enabled. Editing a
composed file-format argument causes the payload to recompose and its
structure to be read again with the new settings. The payload does not need to
be removed, re-added, or explicitly reloaded. Array values requested after the
edit come from the newly selected data.

## Pass Flat Layer Arguments

For tools that cannot author the generated API, pass arguments directly in the
asset identifier:

```python
from pxr import Sdf

identifier = Sdf.Layer.CreateIdentifier(
    "/data/simulation.vtu",
    {"cacheMode": "all", "ioThreads": "8"},
)
layer = Sdf.Layer.FindOrOpen(identifier)
```

Flat argument values are strings. The available keys, value syntax, and typed
API equivalents are documented in
[File-format arguments](file_formats/arguments.md).

Use `mountPath` to choose where the reader authors the native hierarchy:

```python
identifier = Sdf.Layer.CreateIdentifier(
    "/data/simulation.vtu",
    {"mountPath": "/World/Simulation"},
)
layer = Sdf.Layer.FindOrOpen(identifier)
```

`mountPath` must be an absolute prim path. The reader authors any missing
ancestors and makes the top-level component the layer's default prim.

## Author an OmniSci Dataset

The same schema used by native readers can also describe arrays authored
directly in USD:

```python
from pxr import OmniSci, Sdf, Usd, Vt

stage = Usd.Stage.CreateInMemory()
dataset = OmniSci.Dataset.Define(stage, "/World/Dataset")

# Apply FieldAPI to describe what the array represents.
field = OmniSci.FieldAPI.Apply(dataset.GetPrim(), "pressure")
field.CreateAssociationAttr().Set(OmniSci.Tokens.element)

# Apply ArrayAPI to describe where the array lives and author its value.
array = OmniSci.ArrayAPI.Apply(dataset.GetPrim(), "pressure")
array.CreateDeviceAttr().Set("cpu")
value_attr = dataset.GetPrim().CreateAttribute(
    "omni:sci:array:pressure:value",
    Sdf.ValueTypeNames.FloatArray,
)
value_attr.Set(Vt.FloatArray([101.2, 99.8, 100.5]))
```

See [OmniSci schemas](schemas/omni_sci.md) for the complete schema reference
and [Time handling](file_formats/time.md) for time-sampled datasets.
