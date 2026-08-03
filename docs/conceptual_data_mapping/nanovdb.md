<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# NanoVDB and OpenUSD Conceptual Data Mapping

Resolver-backed identifiers are supported for self-contained NanoVDB files.

The `omniSciNvdbFileFormat` plugin exposes a `.nvdb` file as one raw NanoVDB
word array. It preserves the serialized NanoVDB payload rather than deriving a
USD volume-field hierarchy.

```text
/<filenameStem>  OmniSciDataset (default prim)
  OmniSciArrayAPI:nanovdb
  omni:sci:array:nanovdb:value  uint[]
```

The value is loaded through `warp.Volume.load_from_nvdb()` and returned as the
volume's uint32 word buffer. The `warp-lang` package is therefore required when
the value is requested, but not for initial structure discovery.

`cacheMode` and `mountPath` have their common meanings. The word array is a
single sample at time 0.

Grid names, transforms, value types, active topology, and voxel values are not
split into separate USD properties. Consumers interpret the serialized buffer
using NanoVDB-aware tooling.
