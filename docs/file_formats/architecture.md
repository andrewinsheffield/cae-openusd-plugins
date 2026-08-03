<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File-Format Architecture

> **Status:** Implemented
>
> **Audience:** Users, application integrators, and reader developers

## OpenUSD Integration

Each reader is an `SdfFileFormat` registered for one or more native extensions.
Registrations use the normal `usd` target and set `primary = false`. Opening a
native asset therefore uses ordinary OpenUSD APIs:

```python
stage = Usd.Stage.Open("dataset.vtu")
```

Readers are read-only. Their `WriteToFile`, `WriteToString`, and
`WriteToStream` paths reject writes rather than pretending to round-trip a
native format.

## Root Prim and Composition

Without arguments, a reader authors its dataset at a root path derived from the
sanitized filename stem and makes that top-level prim the layer's default prim.
There is no implicit `/World` wrapper.

The flat `mountPath` argument replaces that root with an absolute prim path.
The reader authors `over` specs for ancestors and a `def` at the dataset leaf,
and sets the layer's default prim to the path's top-level component. This makes
the layer suitable for sublayer composition into an existing host hierarchy.
Payloads and references normally use their composition arc to choose placement,
so `mountPath` is not exposed as a typed payload attribute.

## Combined Structure and Lazy Values

Each reader installs one `CaeFileFormatData` backend on its native-format
layer. Ordinary `SdfData` fields hold hierarchy, schemas, relationships,
declarations, and metadata. The same backend holds loader callbacks for large
values, without decoded buffers or permanently open handles. There are no
private self-sublayers or internal read-mode arguments.

Every attribute that could vary over time is exposed as time samples, even
when a particular file contains only one state. Consumers must request an
explicit time code or `Usd.TimeCode.EarliestTime()`:

```python
values = prim.GetAttribute("omni:sci:array:points:value").Get(
    Usd.TimeCode.EarliestTime()
)
```

## Array Convention

Readers apply `OmniSciArrayAPI:<instance>` to identify a logical array. The
schema supplies storage metadata such as `device`, while the reader declares a
custom, concretely typed value attribute named:

```text
omni:sci:array:<instance>:value
```

The value property is not declared in `OmniSciArrayAPI` because a source array
may be scalar, vector, integer, floating point, or another supported USD array
type. The reader selects the concrete type from source metadata. A matching
`OmniSciFieldAPI:<instance>` is applied when the array represents a physical
field rather than topology or coordinates.

## Caching

`CaeFileFormatData` supports three per-layer value-retention modes:

- `all` caches loaded time-sampled values;
- `static` does not retain time-sampled values;
- `none` retains no loaded values between requests.

The cache policy does not change source parsing or USD identity. Readers may
also cache immutable parse/index metadata by resolved path and file signature.

## Native and Python-Backed Readers

Native readers implement parsing and array loading in C++. Python-backed
readers share `PythonFileFormatBase`, which invokes a module for structure
authoring and receives a manifest of lazy fields. The C++ layer installs the
same `CaeFileFormatData` interface used by native readers and calls the module's
array loader when values are requested.

The generic `.pydf` plugin exposes that protocol directly. NPY, NPZ, Trimesh,
and NanoVDB provide fixed modules and format registrations on top of it.

## Source Fidelity

Readers preserve source type, shape, topology, indexing, and units unless a
Conceptual Data Mapping explicitly documents a conversion. For example,
reservoir readers expose native `COORD`, `ZCORN`, and `ACTNUM` arrays, and the
VTK reader preserves the topology representation stored by the source rather
than forcing every input through one derived mesh encoding.

Per-format details and capability gaps belong in the
[Conceptual Data Mappings](../conceptual_data_mapping/README.md).
