<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File Series and USD Value Clips

> **Status:** Implemented, with the limitations described below
>
> **Audience:** Users and authoring-tool developers

Many simulation pipelines write one native file per state, such as
`step_000.npz`, `step_001.npz`, and `step_002.npz`. CAE file-format plugins
expose each file as an ordinary USD layer. Standard USD value clips then map
those layers onto a composed stage timeline.

The reader opens one asset at a time; it does not invent a CAE-specific series
container or scan a directory for timesteps. The authored USD stage owns the
file list, time mapping, stable structure, and clip manifest.

## How It Works

- The main layer owns the stable hierarchy and applies `UsdClipsAPI` metadata
  to the prim whose values vary.
- Each clip asset is a normal native-format asset opened by its registered
  `SdfFileFormat` plugin.
- Readers expose potentially varying lazy attributes as time samples. A
  single-state asset normally exposes its values at local time `0`.
- Clip metadata maps host-stage times to those clip-local samples.
- Static metadata, invariant topology, and non-varying values remain outside
  the clips.

This keeps the series in ordinary USD composition. Explicit clip assets support
irregular names and times; template clips support regular filename patterns.

## Explicit File Series

The following stage maps three single-state NPZ files onto stage times `0`,
`1`, and `2`:

```usda
#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 2
    timeCodesPerSecond = 1.0
)

def OmniSciDataset "SimResult" (
    add references = @./sim_topology.usda@</SimResult>
    clips = {
        dictionary default = {
            asset[] assetPaths = [
                @./frame_000.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@,
                @./frame_001.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@,
                @./frame_002.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@
            ]
            double2[] active = [(0, 0), (1, 1), (2, 2)]
            double2[] times = [(0, 0), (1, 0), (2, 0)]
            asset manifestAssetPath = @./sim_clip_manifest.usda@
            string primPath = "/SimResult"
        }
    }
)
{
}
```

Each file exposes its values at clip-local time `0`. The `active` entries select
a clip asset at each stage time, and `times` maps every stage time to local time
`0` in the selected clip. A time-aware source can instead map stage time to one
of its native sample times.

Use `mountPath=/SimResult` to keep the clip-local prim path stable when source
filenames differ.

## Template File Series

Regularly named files can use USD template clip metadata:

```usda
#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 999
    timeCodesPerSecond = 1.0
)

def OmniSciDataset "SimResult" (
    add references = @./sim_topology.usda@</SimResult>
    clips = {
        dictionary default = {
            string templateAssetPath = "./frame_###.npz:SDF_FORMAT_ARGS:mountPath=/SimResult"
            double templateStartTime = 0
            double templateEndTime = 999
            double templateStride = 1
            asset manifestAssetPath = @./sim_clip_manifest.usda@
            string primPath = "/SimResult"
        }
    }
)
{
}
```

Explicit `assetPaths`, `active`, and `times` are preferable when filenames are
irregular, steps are missing, physical time spacing is non-uniform, or the
author needs explicit looping or hold behavior.

## Clip Manifest

The manifest is a small USD layer declaring the attributes whose values may
come from clips. USD ignores clip values for properties absent from the
manifest.

```usda
#usda 1.0

def "SimResult"
{
    float[] omni:sci:array:temperature:value
    float[] omni:sci:array:pressure:value
}
```

Keep the manifest cheap: declare prim and property specs with the correct value
types, but do not load large arrays. USD can generate a manifest when one is
not supplied, but doing so may require opening every heavy source asset.

## Static Structure and Values

Value clips provide time-varying attribute values; they do not define the
stage's prim hierarchy or make non-time-varying opinions appear.

The main or topology layer should own:

- prim hierarchy, typed prims, and applied API schemas;
- relationships, field names, associations, dimensions, units, and other
  lightweight metadata;
- invariant coordinates and topology;
- empty typed specs for clipped heavy-value attributes.

Declaring the clipped attributes in the stable layer is important for consumers
that call `UsdPrim::GetAttribute()` as a validity check. The manifest indexes
clip values, but should not be the only declaration of the public attribute.

Clip layers should own only the selected time samples of heavy-value
attributes. If a value is static from the application's point of view, author
it outside the clips instead of relying on a default value inside a clip asset.

## Mixed Formats and Split Sources

A clip set may mix extensions because USD sees every clip asset as an
`SdfLayer`. Each asset must expose compatible properties at the same
clip-local paths and with compatible Sdf value types.

It is often better to keep a static mesh outside a field clip set:

```usda
def OmniSciDataset "SimResult" (
    add references = @./mesh.vtu:SDF_FORMAT_ARGS:mountPath=/SimResult@</SimResult>
    clips = {
        dictionary fields = {
            asset[] assetPaths = [
                @./fields_000.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@,
                @./fields_001.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@,
                @./fields_002.npz:SDF_FORMAT_ARGS:mountPath=/SimResult@
            ]
            double2[] active = [(0, 0), (1, 1), (2, 2)]
            double2[] times = [(0, 0), (1, 0), (2, 0)]
            asset manifestAssetPath = @./sim_fields_manifest.usda@
            string primPath = "/SimResult"
        }
    }
    clipSets = ["fields"]
)
{
}
```

Here, `mesh.vtu` supplies stable geometry and topology while the NPZ clip set
supplies field values. A mesh-only file should not be used as the first member
of the field clip set because it cannot answer the clipped field attributes.

If geometry and fields both vary, use separate named clip sets with disjoint
manifests, such as `mesh` and `fields`. Clip-set ordering matters when multiple
sets can provide the same attribute.

## Reader Contract

All readers follow the same clip-compatible behavior:

- potentially varying values are time sampled on the native-format layer;
- single-state readers register values at local time `0`;
- time-aware readers retain their transformed native sample times;
- direct and clipped use expose identical attribute names and Sdf value types;
- structure discovery remains separate from lazy value materialization.

The clip manifest selects which properties participate. No clip-specific
file-format argument is required.

## Authoring Responsibilities

Authoring code is responsible for:

- enumerating the assets or selecting a template pattern;
- choosing stage time codes and mapping them to clip-local times;
- authoring explicit or template clip metadata;
- providing a manifest;
- selecting the clip-local `primPath`;
- using `mountPath` when stable prim paths require it;
- keeping invariant topology and metadata outside the clips.

This deliberately keeps series policy out of readers and keeps each reader
focused on opening one asset efficiently.

## Example Dataset

`tests/data/value_clip_can/` contains two complete examples derived from
ParaView's `can.ex2` dataset:

- `root.usda` composes a static VTK mesh with an NPZ field series;
- `moving_points_root.usda` composes static topology with a VTK series of
  time-varying points.

The fixture README explains the files and can be used as a starting point for
authoring similar stages.

## Known Limitations

- Readers do not discover file series by scanning directories.
- There is no typed CAE authoring schema for file lists or clip metadata.
- There is no bundled utility that generates the topology layer, manifest, and
  root clip layer.
- Topology-changing series require deliberate authoring of the varying topology
  arrays and have not been generalized into a higher-level workflow.
- All clips contributing a property must agree on its path and value type.

These limitations do not change the underlying USD behavior. Authoring tools
can generate the required standard value-clip metadata without modifying the
file-format plugins.
