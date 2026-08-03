<!--
SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# Versioning and releases

The `main` branch carries ongoing development. Long-lived release trains use
`release/v<major>`, beginning with `release/v0`. CI publishes the filtered
public snapshot of each branch to its matching `github/<branch>` branch, so
`release/v0` updates `github/release/v0`.

Development builds use PEP 440 development versions and retain branch and Git
provenance. Release tags are immutable, annotated tags and are the source of
truth for published release versions.

Accepted release tags are:

```text
vX.Y.Z
vX.Y.Z(a|b|rc)N
vX.Y.Z.postN
```

The tag's `X.Y.Z` portion must match the `project(... VERSION X.Y.Z)` value in
the tagged `CMakeLists.txt`. Configure `v*` as a protected tag pattern in
GitLab and allow only release maintainers to create it.

Tag pipelines remove branch names, pipeline IDs, development suffixes, and Git
hashes from published package versions. Native archives retain the OpenUSD,
Python, and platform dimensions needed to select a compatible binary:

```text
cae_openusd_plugins@0.1.0+openusd.usd-0.25.11.py312.linux-x86_64.zip
```

Wheels likewise retain their required runtime dimensions, but omit source
provenance:

```text
cae_openusd_plugins-0.1.0+usd25.11.py312.usdcore-cp312-cp312-linux_x86_64.whl
```

The protected tag pipeline builds and tests every supported matrix entry before
publishing native packages to Packman and `usd-core` wheels to the internal
Python index. Both publishers download the published artifact and verify it
against the build output.

Never delete, recreate, or force-move a published release tag. Use a patch
release for source changes and reserve `.postN` releases for packaging or
metadata corrections.
