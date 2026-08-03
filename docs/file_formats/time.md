<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Time Handling

## Canonical Unit

Every reader emits USD time codes in **simulation seconds** and sets
`timeCodesPerSecond = 1.0` on its native-format layer.

The common transform is:

```text
timeCode = sourceValue * timeScale + timeOffset
```

Both the result and `timeOffset` are in seconds. `timeScale` converts the
selected source into seconds.

## Time Sources

| Token | Meaning |
| --- | --- |
| `TimeStep` | Zero-based sample index. Set `timeScale` to the duration of one step when real-time playback is required. |
| `TimeValue` | Physical time stored by the source. Set `timeScale` to convert the source unit to seconds. |
| `IterationValue` | Solver iteration or report counter. It is not physical time unless `timeScale` supplies a duration per iteration. |

## Reader Support

| Reader | Default | Other supported sources |
| --- | --- | --- |
| CGNS | `TimeStep`, scale `1` | `TimeValue` from `TimeValues`; `IterationValue` from `IterationValues` |
| EDEM | `TimeStep`, scale `1` | `TimeValue` from the timestep `time` attribute |
| EnSight | `TimeStep`, scale `1` | `TimeValue` from the case-file `time values` block |
| OpenFOAM | `TimeStep`, scale `1` | `TimeValue` from numeric time-directory names |
| UNRST | `TimeValue`, scale `86400` | `TimeStep`; `IterationValue` from `SEQNUM` |

Other readers expose potentially varying values as a single sample at time
`0`. This applies even when the current source file is known to contain one
state.

## Composition Into a Host Stage

A file-format layer self-describes its unit with `timeCodesPerSecond = 1.0`.
When composing it into a host stage with another TCPS value, the author of the
payload or reference must decide how host time should map to simulation time.
To preserve real-time duration when the host uses `stageTcps`, apply:

```python
from pxr import Sdf

payload = Sdf.Payload(
    assetPath="simulation.cgns",
    layerOffset=Sdf.LayerOffset(scale=stage.GetTimeCodesPerSecond(), offset=0),
)
prim.GetPayloads().AddPayload(payload)
```

The file-format plugin cannot author that offset because it does not own the
composition arc or the host stage's playback policy.

## File Series and Value Clips

Every native-format layer can participate directly in USD value clips because
potentially varying values are always time sampled. Single-state assets
normally use local time `0`; time-aware readers use their transformed source
times. Clip metadata maps host times to those local samples.

See [File series and USD value clips](file_series.md).
