<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Asset Resolution

> **Status:** Implemented
>
> **Audience:** Users and application integrators

CAE file-format plugins use the active OpenUSD Asset Resolver to open native
assets. The plugins do not contain clients for particular storage services, so
resolver installation, authentication, caching, and transport remain policies
of the host application.

This allows a filename-oriented native or Python library to read an asset from
any resolver that can either expose a native backing file or provide the asset
bytes through `ArAsset`.

## Asset Identity and Local Access

A reader keeps three values distinct:

| Value | Purpose |
| --- | --- |
| Original identifier | USD composition, diagnostics, root naming, and relative child resolution |
| `ArResolvedPath` | Opaque result supplied by the active resolver |
| Lease-scoped local path | Native filename passed to a third-party reader |

The local path may be the source file, a resolver-managed cache file, or a
temporary materialization. It is an implementation detail: readers do not
author it into USD, and lazy value loaders retain its lease for as long as they
may need to reopen it.

When a resolver does not provide a native backing file, the shared reader
runtime copies the asset through `ArAsset::Read()` into a unique temporary
directory. The directory is removed when its final lease is released.

## Supported Dataset Layouts

Resolver support depends on how a native dataset identifies its files:

| Dataset layout | Resolver-backed support |
| --- | --- |
| One self-contained file | Supported |
| Root file with explicitly named children | Supported when the reader resolves each child relative to the root identifier |
| Deterministic child names requiring no directory scan | Supported when the reader can derive every name |
| Wildcard, directory, or timestep discovery | Local filesystem only unless an explicit manifest lists the assets |
| Unnamed sidecars opened internally by a third-party library | Depends on that library and is generally not portable |

OpenUSD's resolver API opens known assets but does not provide portable
directory enumeration. A remote resolver cache containing the root file also
does not imply that neighboring cache entries represent the source directory.
Readers that require adjacency therefore reject non-filesystem layouts with an
actionable error.

The [Conceptual Data Mapping index](../conceptual_data_mapping/README.md#asset-resolution-and-dataset-layout)
records the layout requirements of each reader.

## Relative Child Assets

Case files, manifests, and similar root assets may explicitly name child
files. Readers resolve those names relative to the original root identifier,
not relative to its localized cache path.

This distinction matters for identifiers such as:

```text
resolver://project/case/model.case
```

If `model.case` names `geometry.geo`, the resolver receives a child identifier
derived from the original resolver identifier. The reader must not concatenate
strings onto a temporary filename.

Every child used by a lazy loader retains its own asset lease.

## Stable Root Placement

Filename-oriented readers often derive a default root prim name from the
source filename. A resolver-generated cache filename would make that name
unstable, so the shared reader runtime derives the implicit `mountPath` from
the original identifier.

An explicit [`mountPath`](arguments.md#common-flat-arguments) always takes precedence. Use
it when several assets must expose the same clip-local or composition-local
prim path.

## Diagnostics

Enable resolver tracing when a reader cannot open an asset, locate a child, or
keep a lazy value source alive:

```console
TF_DEBUG=CAE_RESOLVER_ASSET <application> <arguments>
```

The trace reports:

- resolution results;
- the selected local-access strategy;
- materialization size and destination;
- explicit sibling resolution;
- adjacent-directory scan decisions;
- asset lease cleanup.

It logs high-level decisions rather than every `ArAsset::Read()` call or copy
chunk.

## Guarantees

The shared resolver boundary guarantees that:

- storage transport remains controlled by the application's resolver;
- filename-only libraries receive a readable native path;
- temporary materializations survive deferred reads while their leases live;
- original identifiers remain the USD composition identity;
- explicitly named child assets use resolver-relative semantics.

It does not guarantee that:

- the local path resembles the original identifier;
- the local path remains valid after the lease is destroyed;
- adjacent files are present beside a localized root;
- directories or wildcards can be enumerated;
- third-party libraries can discover unnamed remote sidecars.

## Maintainer Interface

Reader implementations use
`source/file_formats/shared/ResolverAsset.{h,cpp}`. The
[file-format development guide](../development/adding_a_file_format.md#5-implement-resolver-backed-access)
describes the integration sequence, and
[file-format performance patterns](../development/file_format_performance.md)
explains how asset leases interact with deferred loading.
