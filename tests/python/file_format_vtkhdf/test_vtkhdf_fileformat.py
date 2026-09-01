# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Integration tests for the OmniSciVtkHdfFileFormat plugin.

Requires a directory of `<stem>_t_<N>.vtkhdf` files. The location is resolved
from ``CAE_VTKHDF_TEST_DATA_DIR`` (env var) or, as a fallback, from the
``JP_VTKHDF/`` folder at the repository root.
"""

from __future__ import annotations

import os
import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciCae as OmniSciCae
import pxr.OmniSciEdem as OmniSciEdem


_REPO_ROOT = pathlib.Path(__file__).parent.parent.parent.parent


def _resolve_data_dir() -> pathlib.Path | None:
    env = os.environ.get("CAE_VTKHDF_TEST_DATA_DIR")
    if env:
        p = pathlib.Path(env)
        if p.is_dir():
            return p
    fallback = _REPO_ROOT / "JP_VTKHDF"
    if fallback.is_dir():
        return fallback
    return None


_DATA_DIR = _resolve_data_dir()
_CASE = _DATA_DIR / "simulation_t_0.vtkhdf" if _DATA_DIR else None


def _looks_like_hdf5(path: pathlib.Path) -> bool:
    try:
        return path.read_bytes()[:8] == b"\x89HDF\r\n\x1a\n"
    except OSError:
        return False


def _plugin_available() -> bool:
    try:
        return Sdf.FileFormat.FindById("OmniSciVtkHdfFileFormat") is not None
    except Exception:
        # Windows LoadLibrary or plugin dependency failures show up as
        # exceptions here; report as "not registered" so most tests skip
        # instead of erroring. The dedicated registration test surfaces
        # the underlying failure.
        return False


def _plugin_load_error() -> str | None:
    try:
        Sdf.FileFormat.FindById("OmniSciVtkHdfFileFormat")
        return None
    except Exception as ex:
        return str(ex)


@pytest.fixture(scope="module")
def stage():
    if _CASE is None or not _CASE.exists():
        pytest.skip("VTKHDF sample data not found; set CAE_VTKHDF_TEST_DATA_DIR")
    if not _looks_like_hdf5(_CASE):
        pytest.skip("VTKHDF fixture is not a valid HDF5 file")
    if not _plugin_available():
        pytest.skip("OmniSciVtkHdfFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_CASE))
    assert stage is not None
    return stage


@pytest.mark.integration
def test_plugin_registered():
    err = _plugin_load_error()
    assert err is None, f"OmniSciVtkHdfFileFormat failed to load: {err}"
    assert Sdf.FileFormat.FindById("OmniSciVtkHdfFileFormat") is not None


@pytest.mark.integration
def test_layer_authors_canonical_tcps(stage):
    """VTKHDF layers self-describe simulation seconds with TCPS=1.0."""
    assert stage.GetTimeCodesPerSecond() == pytest.approx(1.0)


@pytest.mark.integration
def test_default_prim_is_filename_stem(stage):
    default_prim = stage.GetDefaultPrim()
    assert default_prim
    # The filename stem drops the "_t_0" suffix, giving "simulation".
    assert default_prim.GetPath() == Sdf.Path("/simulation")


@pytest.mark.integration
def test_root_scope_hierarchy(stage):
    for child in ("ParticleTypes", "Particles", "GeometryGroups"):
        prim = stage.GetPrimAtPath(f"/simulation/{child}")
        assert prim, f"missing /simulation/{child}"


@pytest.mark.integration
@pytest.mark.parametrize("template_name", ["Paired", "Tri", "Rod"])
def test_particle_types_are_meshes(stage, template_name):
    proto = stage.GetPrimAtPath(f"/simulation/ParticleTypes/{template_name}")
    assert proto, f"missing prototype {template_name}"
    assert proto.GetTypeName() == "Mesh"
    api = OmniSciEdem.ParticleTypeAPI(proto)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == template_name
    assert api.GetShapeKindAttr().Get(Usd.TimeCode.EarliestTime()) == "polyhedral"

    points = proto.GetAttribute("points").Get(Usd.TimeCode.EarliestTime())
    counts = proto.GetAttribute("faceVertexCounts").Get(Usd.TimeCode.EarliestTime())
    indices = proto.GetAttribute("faceVertexIndices").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) > 0
    assert counts is not None and len(counts) > 0
    assert indices is not None and len(indices) == sum(counts)


@pytest.mark.integration
@pytest.mark.parametrize("template_name", ["Paired", "Tri", "Rod"])
def test_particle_clouds_are_datasets(stage, template_name):
    cloud = stage.GetPrimAtPath(f"/simulation/Particles/{template_name}")
    assert cloud, f"missing cloud {template_name}"
    assert cloud.IsA(OmniSci.Dataset)
    assert cloud.HasAPI(OmniSciCae.PointCloudAPI)
    assert cloud.HasAPI(OmniSciEdem.ParticleCloudAPI)

    api = OmniSciEdem.ParticleCloudAPI(cloud)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == template_name
    assert api.GetPrototypeRel().GetTargets() == [
        Sdf.Path(f"/simulation/ParticleTypes/{template_name}")
    ]


@pytest.mark.integration
def test_particle_positions_change_over_time(stage):
    cloud = stage.GetPrimAtPath("/simulation/Particles/Paired")
    points_attr = cloud.GetAttribute("omni:sci:array:points:value")
    assert points_attr.IsValid()

    time_samples = points_attr.GetTimeSamples()
    assert len(time_samples) > 1, "expected multiple time samples"

    early = points_attr.Get(Usd.TimeCode(time_samples[0]))
    late = points_attr.Get(Usd.TimeCode(time_samples[-1]))
    assert early is not None
    assert late is not None
    # Particle counts grow as the simulation injects new particles.
    assert len(late) >= len(early)


@pytest.mark.integration
@pytest.mark.parametrize(
    "field_name,value_type",
    [
        ("velocity", "float3[]"),
        ("angular_velocity", "float3[]"),
        ("quaternions", "float4[]"),
        ("mass", "float[]"),
        ("density", "float[]"),
        ("ids", "int[]"),
        ("types", "int[]"),
    ],
)
def test_expected_particle_fields_are_present(stage, field_name, value_type):
    cloud = stage.GetPrimAtPath("/simulation/Particles/Paired")
    attr = cloud.GetAttribute(f"omni:sci:array:{field_name}:value")
    assert attr.IsValid(), f"missing field {field_name}"
    assert attr.GetTypeName() == value_type

    time_samples = attr.GetTimeSamples()
    if not time_samples:
        pytest.skip(f"no time samples for {field_name}")
    values = attr.Get(Usd.TimeCode(time_samples[-1]))
    assert values is not None
    assert len(values) > 0


@pytest.mark.integration
def test_geometry_groups_are_static_meshes(stage):
    for name in ("Box", "Factory"):
        mesh = stage.GetPrimAtPath(f"/simulation/GeometryGroups/{name}")
        assert mesh, f"missing geometry {name}"
        assert mesh.GetTypeName() == "Mesh"
        api = OmniSciEdem.GeometryGroupAPI(mesh)
        assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == name
