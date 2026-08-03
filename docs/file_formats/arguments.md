<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File-Format Arguments

OpenUSD file-format arguments are strings attached to a layer identifier. This
project also provides typed API schemas whose uniform attribute defaults are
composed into those strings for payloads through
`PcpDynamicFileFormatInterface`.

## Typed Payload Arguments

Apply the format-specific schema to the payload-bearing prim:

```python
from pxr import OmniSciFileFormatArgs

api = OmniSciFileFormatArgs.CgnsAPI.Apply(prim)
api.CreateBaseNameAttr().Set("Base")
api.CreateZoneNameAttr().Set("Zone")
prim.GetPayloads().AddPayload("simulation.cgns")
```

The per-format API prepends the shared APIs it supports. All argument
attributes are `uniform`: they configure layer creation and are not animatable
signals.

| API | Attributes | Used by |
| --- | --- | --- |
| `ArgsAPI` | `cacheMode` | All lazy readers |
| `TimeAPI` | `source`, `scale`, `offset` | CGNS, EDEM, FLASH, EnSight, OpenFOAM, UNRST |
| `StreamingAPI` | `ioThreads` | EnSight, OpenFOAM, and VTK consume it; EDEM currently treats it as a reserved hint. |
| `CgnsAPI` | `baseName`, `zoneName`, `intSize`, `floatSize` | CGNS |
| `FlashAPI` | Shared cache and time arguments | FLASH AMR |
| `NpzAPI` | `schema`, coordinate-array selectors, `allowPickle` | NPZ |
| `NpyAPI` | `arrayName`, `allowPickle` | NPY |
| `ReservoirResultsAPI` | `reservoirKeywordMode` | INIT and UNRST |
| `PythonAPI` | module, path, and callback names | Python proxy |

`EdemAPI`, `EnSightAPI`, `OpenFoamAPI`, `GrdeclAPI`, `EgridAPI`, `InitAPI`,
`UnrstAPI`, and `VtkAPI` are stable format-specific entry points. Some add no
format-only attributes but prepend the shared APIs appropriate to that reader.

## Common Flat Arguments

Flat arguments can be passed to `Sdf.Layer.CreateIdentifier`:

```python
identifier = Sdf.Layer.CreateIdentifier(
    "dataset.vtu",
    {"cacheMode": "static", "ioThreads": "8"},
)
```

| Argument | Values | Meaning |
| --- | --- | --- |
| `cacheMode` | `all`, `static`, `none` | Lazy value-retention policy. `all` caches sampled values; `static` and `none` do not. Default: `all`. |
| `mountPath` | Absolute prim path | Author the dataset at an explicit sublayer path. Available to every reader; not a typed payload attribute. |

## Time Arguments

`timeSource`, `timeScale`, and `timeOffset` select and transform a reader's
native ordinate:

```text
timeCode = sourceValue * timeScale + timeOffset
```

The result is interpreted as simulation seconds. See [Time handling](time.md).

## Format-Specific Flat Arguments

| Reader | Arguments |
| --- | --- |
| CGNS | `baseName`, `zoneName`, `intSize`, `floatSize` |
| EDEM | `timeSource`, `timeScale`, `timeOffset`, `ioThreads` |
| FLASH AMR | `timeSource`, `timeScale`, `timeOffset` |
| EnSight | `timeSource`, `timeScale`, `timeOffset`, `ioThreads` |
| OpenFOAM | `timeSource`, `timeScale`, `timeOffset`, `ioThreads` |
| INIT/UNRST | `reservoirKeywordMode`; UNRST also accepts time arguments |
| VTK | `ioThreads` |
| NPY | `arrayName`, `allowPickle` |
| NPZ | `schema`, `coordsArray`, `coordsArrayX/Y/Z`, `allowPickle` |
| Python proxy | `pythonModule`, `pythonPath`, `pythonReadFunction`, `pythonCanReadFunction`, `pythonLoadArrayFunction` |
| Trimesh, NanoVDB | No format-specific arguments |

For defaults and semantic effects on the USD representation, use the relevant
[Conceptual Data Mapping](../conceptual_data_mapping/README.md). The schema
source of truth for typed arguments is
`source/schemas/omni_sci_file_format_args/schema.usda`.
