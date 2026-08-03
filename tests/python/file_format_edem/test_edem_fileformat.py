# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciCae as OmniSciCae
import pxr.OmniSciEdem as OmniSciEdem


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "EDEM"
_CASE = _DATA_DIR / "minimal.dem"
_FIRST_SAMPLE = _DATA_DIR / "minimal_data" / "0.h5"


def _looks_like_hdf5(path: pathlib.Path) -> bool:
    try:
        return path.read_bytes().startswith(b"\x89HDF\r\n\x1a\n")
    except OSError:
        return False


def _edem_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciEdemFileFormat") is not None


@pytest.fixture(scope="module")
def stage():
    if not _CASE.exists():
        pytest.skip("EDEM test data not found")
    if not _looks_like_hdf5(_CASE) or not _looks_like_hdf5(_FIRST_SAMPLE):
        pytest.skip("EDEM binary fixtures are not available in this checkout")
    if not _edem_available():
        pytest.skip("OmniSciEdemFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_CASE))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _edem_available(), "OmniSciEdemFileFormat not registered"


@pytest.mark.integration
def test_layer_authors_canonical_tcps(stage):
    """EDEM layers self-describe simulation seconds with TCPS=1.0."""
    assert stage.GetTimeCodesPerSecond() == pytest.approx(1.0)


@pytest.mark.integration
def test_default_prim_is_filename_stem(stage):
    default_prim = stage.GetDefaultPrim()
    assert default_prim
    assert default_prim.GetPath() == Sdf.Path("/minimal")
    assert not stage.GetPrimAtPath("/World").IsValid()


@pytest.mark.integration
def test_particle_cloud_dataset_exists(stage):
    cloud = stage.GetPrimAtPath("/minimal/Particles/Pebble")
    assert cloud
    assert cloud.IsA(OmniSci.Dataset)
    assert cloud.HasAPI(OmniSciCae.PointCloudAPI)
    assert cloud.HasAPI(OmniSciEdem.ParticleCloudAPI)


@pytest.mark.integration
def test_particle_cloud_metadata(stage):
    cloud = stage.GetPrimAtPath("/minimal/Particles/Pebble")
    api = OmniSciEdem.ParticleCloudAPI(cloud)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == "Pebble"
    assert api.GetSourceNodeAttr().Get(Usd.TimeCode.EarliestTime()) == "type0"
    assert api.GetPrototypeRel().GetTargets() == [Sdf.Path("/minimal/ParticleTypes/Pebble")]


@pytest.mark.integration
def test_particle_positions_and_fields_are_time_sampled(stage):
    cloud = stage.GetPrimAtPath("/minimal/Particles/Pebble")
    points_attr = cloud.GetAttribute("omni:sci:array:points:value")
    ids_attr = cloud.GetAttribute("omni:sci:array:ids:value")
    velocity_attr = cloud.GetAttribute("omni:sci:array:velocity:value")
    assert points_attr.IsValid()
    assert ids_attr.IsValid()
    assert velocity_attr.IsValid()

    p0 = points_attr.Get(Usd.TimeCode(0))
    p1 = points_attr.Get(Usd.TimeCode(1))
    ids0 = ids_attr.Get(Usd.TimeCode(0))
    vel1 = velocity_attr.Get(Usd.TimeCode(1))
    assert p0 is not None and len(p0) == 2
    assert p1 is not None and len(p1) == 2
    assert ids0 is not None and list(ids0) == [10, 20]
    assert vel1 is not None and len(vel1) == 2
    assert tuple(p0[0]) == pytest.approx((0.0, 0.0, 0.0))
    assert tuple(p1[1]) == pytest.approx((2.0, 1.0, 0.0))
    assert tuple(vel1[0]) == pytest.approx((0.0, 2.0, 0.0))


@pytest.mark.integration
def test_particle_type_prototype_exists(stage):
    prototype = stage.GetPrimAtPath("/minimal/ParticleTypes/Pebble")
    assert prototype
    assert prototype.GetTypeName() == "Mesh"
    api = OmniSciEdem.ParticleTypeAPI(prototype)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == "Pebble"
    assert api.GetShapeKindAttr().Get(Usd.TimeCode.EarliestTime()) == "polyhedral"


@pytest.mark.integration
def test_sphere_cluster_particle_type_expands_to_sphere_children(stage):
    prototype = stage.GetPrimAtPath("/minimal/ParticleTypes/Dumbbell")
    assert prototype
    assert prototype.GetTypeName() == "Xform"
    api = OmniSciEdem.ParticleTypeAPI(prototype)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == "Dumbbell"
    assert api.GetShapeKindAttr().Get(Usd.TimeCode.EarliestTime()) == "sphereCluster"

    left = stage.GetPrimAtPath("/minimal/ParticleTypes/Dumbbell/Left")
    right = stage.GetPrimAtPath("/minimal/ParticleTypes/Dumbbell/Right")
    assert left and right
    assert left.GetTypeName() == "Sphere"
    assert right.GetTypeName() == "Sphere"
    assert left.GetAttribute("radius").Get(Usd.TimeCode.EarliestTime()) == pytest.approx(0.25)
    assert right.GetAttribute("radius").Get(Usd.TimeCode.EarliestTime()) == pytest.approx(0.25)


@pytest.mark.integration
def test_sphere_cluster_particle_cloud_exists(stage):
    cloud = stage.GetPrimAtPath("/minimal/Particles/Dumbbell")
    assert cloud
    assert cloud.IsA(OmniSci.Dataset)
    api = OmniSciEdem.ParticleCloudAPI(cloud)
    assert api.GetPrototypeRel().GetTargets() == [Sdf.Path("/minimal/ParticleTypes/Dumbbell")]


@pytest.mark.integration
def test_geometry_group_exists_and_is_transformed(stage):
    mesh = stage.GetPrimAtPath("/minimal/GeometryGroups/Drum")
    assert mesh
    assert mesh.GetTypeName() == "Mesh"
    api = OmniSciEdem.GeometryGroupAPI(mesh)
    assert api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == "Drum"
    xform_op = mesh.GetAttribute("xformOp:transform")
    assert xform_op.IsValid()
    matrix = xform_op.Get(Usd.TimeCode(1))
    assert matrix is not None


@pytest.mark.integration
def test_time_value_argument(stage):
    layer_id = Sdf.Layer.CreateIdentifier(str(_CASE), {"timeSource": "TimeValue"})
    time_stage = Usd.Stage.Open(layer_id)
    assert time_stage
    cloud = time_stage.GetPrimAtPath("/minimal/Particles/Pebble")
    points_attr = cloud.GetAttribute("omni:sci:array:points:value")
    value = points_attr.Get(Usd.TimeCode(0.5))
    assert value is not None
    assert len(value) == 2
