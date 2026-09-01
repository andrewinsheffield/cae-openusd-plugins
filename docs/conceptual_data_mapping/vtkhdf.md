<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# VTKHDF DEM and OpenUSD Conceptual Data Mapping

The `OmniSciVtkHdfFileFormat` reader consumes VTKHDF v2.5 `MultiBlockDataSet`
files as produced by DEM simulators. A "case" is a directory of frames named
`<stem>_t_<N>.vtkhdf` (one file per timestep) plus, optionally, a sibling
`particle_templates/` directory holding the prototype particle meshes.

**Document Version:** 1.0.0
**Last Update:** 2026-09-01

## Introduction

Opening any single `<stem>_t_<N>.vtkhdf` file causes the reader to scan the
containing directory for the full series and author a single USD layer whose
particle-cloud arrays are time-sampled across the whole series. Opening the
same case through the kit-cae *CAE VTKHDF Importer* deduplicates a
multi-selection down to one file per group so the same layer is reused.

The mapping is **one-way (VTKHDF → OpenUSD)**. The plugin is read-only.

### Schema plugins involved

Because a VTKHDF DEM dataset is semantically equivalent to an EDEM particle
simulation, the reader reuses the EDEM domain schemas instead of introducing
its own:

- **OmniSci** (`omni:sci:` namespace) — format-agnostic base schemas for
  scientific datasets, fields, and arrays.
- **OmniSciCae** (`omni:cae:` namespace) — shared CAE-oriented schemas
  (`OmniSciCaePointCloudAPI` on particle clouds).
- **OmniSciEdem** (`omni:edem:` namespace) — particle types, particle clouds,
  and geometry groups.
- **OmniSciFileFormatArgs** — the dedicated `OmniSciFileFormatArgsVtkHdfAPI`
  carries payload arguments (`cacheMode`, `timeScale`, `timeOffset`,
  `timeSource`, `ioThreads`, `mountPath`) for kit-cae's importer to apply on
  the payload prim.

## Source-file structure

Each `<stem>_t_<N>.vtkhdf` is an HDF5 file with a `/VTKHDF` root group of
`Type=MultiBlockDataSet`, `Version=[2,5]`. The file contains:

- `/VTKHDF/Assembly/` — a semantic map, populated with soft links, using the
  categories `ParticleTemplates`, `Particles`, `Contacts`, `Bonds`,
  `Geometries`. Each link points at a block under `/VTKHDF/block_XXX`.
- `/VTKHDF/block_XXX/` — the actual `PolyData` blocks holding `Points`,
  `PointData`, `CellData`, `Polygons/{Connectivity,Offsets}`, and
  `FieldData/TimeValue`.

The standalone `particle_templates_t_0.vtkhdf` file omits the
`ParticleTemplates` subgroup and places the prototype links directly under
`/VTKHDF/Assembly/{Paired,Tri,Rod,...}`. The reader detects both layouts.

At `t = 0` the simulation typically has no particles yet, so
`/VTKHDF/Assembly/Particles/` is empty. The reader scans later samples to
discover per-cloud field metadata.

## USD stage layout

For a case opened via `<parent>/simulation_t_0.vtkhdf`, the reader authors:

```
/simulation                        # UsdGeomScope (mount path — see below)
├── ParticleTypes/                 # UsdGeomScope
│   ├── Paired  (Mesh, OmniSciEdemParticleTypeAPI, shapeKind="polyhedral")
│   ├── Tri     (Mesh, OmniSciEdemParticleTypeAPI, shapeKind="polyhedral")
│   └── Rod     (Mesh, OmniSciEdemParticleTypeAPI, shapeKind="polyhedral")
├── Particles/                     # UsdGeomScope
│   ├── Paired  (OmniSciDataset, OmniSciCaePointCloudAPI,
│   │            OmniSciEdemParticleCloudAPI)
│   ├── Tri     (OmniSciDataset, …)
│   └── Rod     (OmniSciDataset, …)
└── GeometryGroups/                # UsdGeomScope
    ├── Box     (Mesh, OmniSciEdemGeometryGroupAPI)
    └── Factory (Mesh, OmniSciEdemGeometryGroupAPI)
```

### Mount path

The default mount path strips the `_t_<N>` suffix from the filename stem, so
every frame in a `<stem>_t_<N>.vtkhdf` series composes onto the same root
prim. Opening `simulation_t_0.vtkhdf` or `simulation_t_100.vtkhdf` both use
the default prim `/simulation`. Passing an explicit `mountPath=<absolute
SdfPath>` overrides this.

### Particle types

Each entry under `/VTKHDF/Assembly/ParticleTemplates/*` (or under
`/VTKHDF/Assembly/*` in the standalone template file) becomes a `UsdGeomMesh`
under `<root>/ParticleTypes/<name>` with:

- `points`, `faceVertexCounts`, `faceVertexIndices` from `Points` and
  `Polygons/{Connectivity,Offsets}`.
- `OmniSciEdemParticleTypeAPI` with `name`, `sourceNode` (the block path),
  and `shapeKind="polyhedral"`.

### Particle clouds

Each `/VTKHDF/Assembly/Particles/<name>` link becomes an `OmniSciDataset`
prim under `<root>/Particles/<name>` with `OmniSciCaePointCloudAPI` and
`OmniSciEdemParticleCloudAPI` applied. Its `prototype` relationship targets
`<root>/ParticleTypes/<name>`.

Positions are exposed as `omni:sci:array:points:value` with `float3[]`
value-type. Each supported `PointData/<field>` becomes a lazy
`omni:sci:array:<field>:value` attribute with `OmniSciFieldAPI` metadata
(`association="node"`) and `OmniSciArrayAPI` (`device="cpu"`).

Supported field value types:

| VTKHDF dataset shape / class | USD value type |
| --- | --- |
| Nx3 float | `float3[]` |
| Nx4 float | `float4[]` |
| N or Nx1 float | `float[]` |
| any integer | `int[]` |

Both positions and fields are registered as lazy time samples through
`CaeFileFormatData`. Sample times come from either the timestep index
(default) or the per-file `FieldData/TimeValue` (when
`timeSource=TimeValue`).

### Geometry groups

Each `/VTKHDF/Assembly/Geometries/<name>` link becomes a `UsdGeomMesh` under
`<root>/GeometryGroups/<name>` with `OmniSciEdemGeometryGroupAPI` applied.
Topology is loaded from the first sample (assumed static). Per-cell field
data on geometries is not exposed in this release.

## File-format arguments

| Argument | Default | Description |
| --- | --- | --- |
| `mountPath` | derived from `<stem>` with the `_t_<N>` suffix stripped | Absolute `SdfPath` where the plugin authors its typed root prim. |
| `cacheMode` | `all` | Lazy-array cache policy. `all` retains loaded values; `static`/`none` do not. |
| `timeScale` | `1.0` | Multiplier applied to the selected time source. |
| `timeOffset` | `0.0` | Offset added after `timeScale`. |
| `timeSource` | `TimeStep` | `TimeStep` uses the parsed frame index; `TimeValue` uses each file's `FieldData/TimeValue`. |
| `ioThreads` | `1` | Reserved; the current reader does not schedule threaded reads. |

## Discovery, time-scan, and lazy loading

1. **Discovery.** `CanRead` opens the file, confirms the HDF5 magic bytes,
   and checks that `/VTKHDF` exists.
2. **Frame scan.** The stem is parsed with the regex `<base>_t_<N>`. Every
   sibling matching `<base>_t_<int>.vtkhdf` becomes a `TimeSampleInfo` sorted
   by `N`.
3. **Prototype source.** If `<parent>/particle_templates/*.vtkhdf` exists it
   is preferred as the template source. Otherwise the first frame is used.
4. **Structure vs data.** Structure (prims, applied schemas, static
   topology) is authored eagerly. Per-frame positions and field arrays are
   registered as lazy time samples on a `CaeFileFormatData` backend.
5. **Field discovery.** Because `<stem>_t_0.vtkhdf` typically has no
   particles yet, field metadata is discovered from the first frame whose
   `PointData` has at least one row.

## Unsupported / open items

| Concept | Status |
| --- | --- |
| Contacts (`/VTKHDF/Assembly/Contacts/*`) | Not authored in this release. |
| Bonds (`/VTKHDF/Assembly/Bonds/*`) | Not authored in this release. |
| Time-varying geometry topology | Assumed static; only the first frame's topology is authored. |
| Per-cell field data on geometry meshes | Not authored. |
| Writing | Read-only; write support is out of scope. |
