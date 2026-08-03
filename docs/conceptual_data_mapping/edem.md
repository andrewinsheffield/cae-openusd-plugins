<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# EDEM and OpenUSD Conceptual Data Mapping

EDEM loading scans the sibling `<case>_data` directory for timestep HDF5
files. It is therefore supported only on a local filesystem. Opening a
resolver-backed deck reports this layout as unsupported.

**Document Version:** 1.0.0
**Last Update:** 2026-04-19

## Introduction

### Overview

This document describes how EDEM deck and timestep data are translated into
OpenUSD prims and properties by the `OmniSciEdemFileFormat` plugin. It is the
reference for developers working on the EDEM reader and for consumers reading
the OpenUSD representation of EDEM datasets in this repository.

The mapping is **one-way (EDEM → OpenUSD)**. The plugin is read-only;
round-trip write support is not in scope. The document also notes capability
gaps where EDEM concepts are not surfaced in the current implementation.

An EDEM dataset is anchored by a `.dem` deck HDF5 file with sibling timestep
files under `<deck>_data/*.h5`. OpenUSD organises the result as a scenegraph of
typed prims plus composable API schemas. The mapping uses three custom schema
plugins:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciCae** (`omni:cae:` namespace) — shared CAE-oriented schemas, used
  here for point-cloud semantics.
- **OmniSciEdem** (`omni:edem:` namespace) — EDEM-specific API schemas for
  particle clouds, particle types, and geometry groups.

Large particle arrays (positions and particle fields) are not loaded eagerly.
One `CaeFileFormatData` backend stores structure and resolves sampled
`omni:sci:array:<name>:value` attributes on demand.

### References

This document has been prepared in reference to the software and file-layout
conventions exercised by the EDEM datasets targeted by this repository.

#### EDEM Reference

| Version | Reference Documents |
|---------|---------------------|
| HDF5-backed EDEM decks and timestep files | EDEM `.dem` + `<deck>_data/*.h5` layout implemented by the reader and exercised by `tests/data/EDEM/` |

#### OpenUSD Reference

| Version | Reference Documents |
|---------|---------------------|
| 24.08 | [OpenUSD C++ and Schema Documentation](https://openusd.org/release/api/index.html), [OpenUSD GitHub Repository](https://github.com/PixarAnimationStudios/OpenUSD), [USD Terms and Concepts](https://openusd.org/release/glossary.html) |

### General Assumptions and Constraints

**One-way mapping (EDEM → OpenUSD).** The plugin provides read-only access.
Writing OpenUSD back to EDEM is not supported.

**`.dem` deck as the entry point.** The plugin registers the `dem` extension.
The deck is an HDF5 file containing deck metadata, including the number of
timesteps. Timestep files are discovered under the sibling directory
`<deckStem>_data/`.

**HDF5 C API implementation.** The reader uses the HDF5 C library directly
and supports HDF5 1.10 or newer. Calls that changed signature across HDF5 API
generations — currently `H5Oget_info_by_name`, which is the 4-arg form with
`H5O_info_t` on 1.10 and the 5-arg form with `H5O_info2_t` + `H5O_INFO_BASIC`
fields mask on 1.12+ — are handled with `H5_VERSION_GE` guards. Python-specific
HDF5 helpers are not part of this implementation.

**Prototype geometry comes from timestep 0 creator data.** Particle types and
geometry groups are read from `/CreatorData/0/...` in the first timestep file.

**Particle clouds are point clouds.** Each EDEM particle type becomes one
`OmniSciDataset` point-cloud prim carrying positions and supported particle
fields as lazy arrays. The dataset points back to a particle-type prototype via
`OmniSciEdemParticleCloudAPI`.

**Particle-type prototype shape is shape-kind dependent.**

- polyhedral particle types become `UsdGeomMesh` prims
- sphere-cluster particle types become `UsdGeomXform` prims with child
  `UsdGeomSphere` prims
- unknown particle types currently fall back to an empty `UsdGeomXform`

**Known particle fields only in v1.** The reader currently follows the old
`kit-cae` logic and only probes these timestep particle fields:

- `position`
- `ids`
- `scale`
- `orientation`
- `velocity`

Nested groups such as `CustomProperties` are intentionally ignored in v1.

**Geometry groups are eager meshes.** Geometry-group meshes are authored
directly as `UsdGeomMesh` prims using creator data from timestep 0. Optional
per-timestep transforms from `GeometryGroups/<group>/Kinematics/0/global transform`
are authored as time samples on `xformOp:transform`.

**Time handling is file-format-arg driven; results are canonical simulation
seconds.** The reader supports `timeSource`, `timeScale`, and `timeOffset`
through the shared `OmniSciFileFormatArgsTimeAPI`. `timeSource=TimeStep` uses
sample indices by default; `timeSource=TimeValue` uses the per-timestep `time`
HDF5 attribute. The resolved time code is `source_value * timeScale +
timeOffset` interpreted as simulation seconds. Pick `timeScale = 1.0` when the
file's `time` attribute is already in seconds (the EDEM convention) and the
per-step `dt` in seconds when `timeSource=TimeStep`. The emitted layer sets
`timeCodesPerSecond = 1.0`; host pipelines that compose into a stage whose
TCPS is not 1 are responsible for authoring an
`Sdf.LayerOffset(scale=stage.GetTimeCodesPerSecond(), 0)` on the payload /
sublayer / reference arc -- the plugin does not ship a helper API for this.

**Root layout.** The default stage layout is `/<deckName>`, matching the
current repository conventions for CAE readers.

**Prim name sanitisation.** Deck names, particle-type names, geometry-group
names, and sphere names are passed through `TfMakeValidIdentifier()` before use
as prim names.

### Definitions, Acronyms, Abbreviations

| Term or Abbreviation | Description |
|----------------------|-------------|
| EDEM | Discrete Element Method simulation data stored here as an HDF5-backed deck plus timestep files. |
| `.dem` | EDEM deck file used as the USD file-format entry point. |
| Creator data | Static prototype and geometry-group description data stored under `/CreatorData/0/...` in timestep files. |
| Particle type | EDEM prototype definition for one particle family. |
| Particle cloud | The time-varying point set representing all simulated particles of a given type. |
| Geometry group | Static or animated boundary/fixture geometry in the simulation. |
| Sphere cluster | A particle type represented as multiple spheres with offsets and radii. |
| OmniSci | Format-agnostic USD schema plugin (`omni:sci:` namespace) providing base types for scientific datasets. |
| OmniSciCae | Shared CAE-oriented USD schema plugin (`omni:cae:` namespace) providing point-cloud semantics. |
| OmniSciEdem | EDEM-specific USD schema plugin (`omni:edem:` namespace) providing EDEM API schemas. |
| Lazy attribute | An attribute whose value is resolved on demand from the lazy heavy-data layer rather than authored eagerly in the primary structure layer. |

---

## Concepts

The table below lists the EDEM concepts consumed by the current reader and
their OpenUSD equivalents.

| EDEM | OpenUSD | Description |
|------|---------|-------------|
| [Deck (`.dem`)](#deck-root) | `UsdGeomScope` at `/<deckName>` (default prim) | Entry layer and root for the imported EDEM dataset. |
| [Particle type](#particle-type) | `UsdGeomMesh` or `UsdGeomXform` + `OmniSciEdemParticleTypeAPI` | Static prototype for one EDEM particle family. |
| [Particle cloud](#particle-cloud) | `OmniSciDataset` + `OmniSciCaePointCloudAPI` + `OmniSciEdemParticleCloudAPI` | Time-varying simulated particles for one particle type. |
| [Geometry group](#geometry-group) | `UsdGeomMesh` + `OmniSciEdemGeometryGroupAPI` | Static mesh with optional time-sampled transform. |
| [Particle fields](#particle-fields) | `OmniSciFieldAPI:<name>` + `OmniSciArrayAPI:<name>` | Lazy particle arrays authored on the owning particle-cloud dataset. |
| [Time samples](#time-varying-data) | USD time samples on `omni:sci:array:<name>:value` and `xformOp:transform` | Per-timestep positions, fields, and geometry transforms. |

### Deck Root

An EDEM dataset is opened through a `.dem` deck file. The reader derives the
sibling data directory from the deck stem:

```text
<caseDir>/
    <deckName>.dem
    <deckName>_data/
        0.h5
        1.h5
        ...
```

In OpenUSD the imported content is rooted under `/<deckName>`.

**Stage path:** `/<deckName>`

#### Composition

```text
/<deckName>                         UsdGeomScope  (default prim, Z-up)
/<deckName>/ParticleTypes           UsdGeomScope
/<deckName>/Particles               UsdGeomScope
/<deckName>/GeometryGroups          UsdGeomScope
```

`/<deckName>` is the layer's default prim.

---

### Particle Type

Each EDEM particle type from `/CreatorData/0/ParticleTypes/<node>` becomes one
prototype prim under `/<deckName>/ParticleTypes/`.

**Stage path:** `/<deckName>/ParticleTypes/<typeName>`

#### Properties

| EDEM | OpenUSD | Description |
|------|---------|-------------|
| particle-type name attr | Prim name + `omni:edem:particleType:name` | Sanitised prim name plus preserved original string value. |
| creator node name | `omni:edem:particleType:sourceNode` | Original HDF5 node identifier under `ParticleTypes`. |
| inferred shape kind | `omni:edem:particleType:shapeKind` | `polyhedral`, `sphereCluster`, or `unknown`. |
| `coords` + `triangle nodes` | `UsdGeomMesh` | Polyhedral prototype mesh. |
| `spheres` | `UsdGeomXform` with child `UsdGeomSphere` prims | Sphere-cluster prototype expansion. |

#### Shape-Kind Mapping

| EDEM creator data | OpenUSD | Notes |
|-------------------|---------|-------|
| `coords` + `triangle nodes` | `UsdGeomMesh` | Mesh points and triangles are authored eagerly. |
| `spheres` | `UsdGeomXform` + child spheres | Each row in the `spheres` dataset becomes one child `UsdGeomSphere`. |
| neither | `UsdGeomXform` | Placeholder prototype for unsupported/unknown particle type layouts. |

#### Sphere Cluster Expansion

When a particle type contains a `spheres` dataset, the reader expands the
prototype as follows:

- the prototype prim is a `UsdGeomXform`
- each sphere record becomes a child `UsdGeomSphere`
- radius comes from `physicalRadius`
- translate comes from `pos`

The current implementation does not author custom metadata attributes such as
`edem:physicalRadius` and `edem:contactRadius`.

---

### Particle Cloud

Each particle type also becomes one particle-cloud dataset under
`/<deckName>/Particles/`.

**Stage path:** `/<deckName>/Particles/<typeName>`

#### Composition

```text
/<deckName>/Particles/<typeName>    OmniSciDataset
    apiSchemas = [
        "OmniSciCaePointCloudAPI",
        "OmniSciEdemParticleCloudAPI",
        "OmniSciArrayAPI:points",
        "OmniSciArrayAPI:<fieldName>",
        "OmniSciFieldAPI:<fieldName>"
    ]
```

#### Properties

| EDEM | OpenUSD | Description |
|------|---------|-------------|
| particle-type name | `omni:edem:particleCloud:name` | Original EDEM particle-type name. |
| creator node name | `omni:edem:particleCloud:sourceNode` | Original HDF5 node identifier. |
| prototype reference | `omni:edem:particleCloud:prototype` (rel) | Relationship to the particle-type prototype prim. |
| `position` | `OmniSciArrayAPI:points` + lazy `omni:sci:array:points:value` | Time-varying point positions. |

Particle positions are interpreted as point-cloud coordinates and the particle
cloud prim has `OmniSciCaePointCloudAPI` applied.

---

### Geometry Group

Each EDEM geometry group from `/CreatorData/0/GeometryGroups/<node>` becomes a
mesh prim under `/<deckName>/GeometryGroups/`.

**Stage path:** `/<deckName>/GeometryGroups/<groupName>`

#### Properties

| EDEM | OpenUSD | Description |
|------|---------|-------------|
| geometry-group name attr | Prim name + `omni:edem:geometryGroup:name` | Sanitised prim name plus preserved original name string. |
| creator node name | `omni:edem:geometryGroup:sourceNode` | Original HDF5 node identifier. |
| `coords` + `triangle nodes` | `UsdGeomMesh` | Mesh points and triangles authored eagerly. |
| `Kinematics/0/global transform` | `xformOp:transform` | Optional per-timestep transform samples. |

Geometry-group topology is currently authored eagerly rather than lazily.

---

### Particle Fields

Supported particle fields are authored on the particle-cloud dataset as
`OmniSciFieldAPI:<name>` and `OmniSciArrayAPI:<name>` pairs.

#### Supported Field Set in v1

| EDEM timestep dataset | OpenUSD | Notes |
|-----------------------|---------|-------|
| `ids` | `int[]` | Particle ids. |
| `scale` | `float[]` or `floatN[]` if encountered | Only surfaced if present and dataset-shaped compatibly. |
| `orientation` | `float4[]` when shaped as quaternion tuples | Only surfaced if present and dataset-shaped compatibly. |
| `velocity` | `float3[]` | Particle velocity vectors. |

The reader does **not** recursively scan arbitrary nested groups for fields.
This is deliberate and follows the old `kit-cae` importer’s narrower particle
field logic.

#### Field Association

Particle-cloud fields are authored with:

```text
omni:sci:field:<name>:association = "node"
```

because each field value is attached to one particle point.

---

### Time-Varying Data

Each timestep file under `<deckName>_data/` contributes one sample under
`/TimestepData/<node>`. The reader records the timestep `time` attribute and
maps it to USD time ordinates according to file-format args.

#### Time Arguments

| File-format arg | Meaning |
|-----------------|---------|
| `timeSource` | `TimeStep` (zero-based sample indices) or `TimeValue` (per-timestep `time` HDF5 attribute). |
| `timeScale` | Multiplier that converts the chosen source into simulation seconds. `1.0` when the source is already in seconds; per-step `dt` for `TimeStep`. |
| `timeOffset` | Additive offset (simulation seconds) applied after `timeScale`. |

#### Time-Sampled Properties

| Property | Sampled? |
|----------|----------|
| `omni:sci:array:points:value` on particle clouds | Yes |
| `omni:sci:array:<field>:value` on particle clouds | Yes, when the field exists |
| `xformOp:transform` on geometry groups | Yes, when the kinematics path exists |

Particle-type prototype geometry is not time varying in the current reader.

---

## File-Format Arguments

| Argument | Default | Mapping effect |
| --- | --- | --- |
| `mountPath` | filename-stem root | Places the dataset at an absolute sublayer path. |
| `cacheMode` | `all` | Controls lazy value retention (`all`, `static`, or `none`). |
| `timeSource` | `TimeStep` | Selects step index or the stored `TimeValue`. |
| `timeScale` | `1` | Converts the selected source to simulation seconds. |
| `timeOffset` | `0` | Shifts the resulting seconds-domain time code. |
| `ioThreads` | `1` | Accepted streaming hint; threaded HDF5 reads are not currently implemented. |

---

## Capability Gaps

The current EDEM mapping intentionally leaves several things out:

| EDEM concept | Status |
|--------------|--------|
| `CustomProperties` nested groups | Not implemented |
| Arbitrary nested particle-field groups | Not implemented |
| Preservation of old sphere custom attrs (`edem:physicalRadius`, `edem:contactRadius`, etc.) | Not implemented |
| Writing OpenUSD back to `.dem` / `.h5` | Not implemented |
| Threaded HDF5 array reads | Format args exist, but behavior is not implemented |

## Summary

The EDEM reader maps one deck to one CAE-oriented USD subtree rooted under
`/<deckName>`. Particle types are represented as reusable prototypes,
particle clouds as `OmniSciDataset` point clouds with lazy arrays, and geometry
groups as meshes with optional animated transforms. This mirrors the old
`kit-cae` importer’s key structural choices while aligning the data model with
the `OmniSci`, `OmniSciCae`, and `OmniSciEdem` schema libraries used in this
repository.
