# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciOpenFoam as OmniSciOpenFoam


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "OpenFOAM"
_CASE = _DATA_DIR / "minimal.foam"
_BINARY_CASE = _DATA_DIR / "squareBend" / "squareBend.foam"


def _openfoam_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciOpenFoamFileFormat") is not None


@pytest.fixture(scope="module")
def stage():
    if not _CASE.exists():
        pytest.skip("OpenFOAM test data not found")
    if not _openfoam_available():
        pytest.skip("OmniSciOpenFoamFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_CASE))
    assert stage
    return stage


@pytest.fixture(scope="module")
def binary_stage():
    if not _BINARY_CASE.exists():
        pytest.skip("Binary OpenFOAM test data not found")
    if not _openfoam_available():
        pytest.skip("OmniSciOpenFoamFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_BINARY_CASE))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _openfoam_available(), "OmniSciOpenFoamFileFormat not registered"


@pytest.mark.integration
def test_layer_authors_canonical_tcps(stage):
    """OpenFOAM layers self-describe simulation seconds with TCPS=1.0."""
    assert stage.GetTimeCodesPerSecond() == pytest.approx(1.0)


@pytest.mark.integration
def test_default_prim_is_filename_stem(stage):
    default_prim = stage.GetDefaultPrim()
    assert default_prim
    assert default_prim.GetPath() == Sdf.Path("/minimal")
    assert not stage.GetPrimAtPath("/World").IsValid()


@pytest.mark.integration
def test_volume_dataset_exists(stage):
    volume = stage.GetPrimAtPath("/minimal/Volume")
    assert volume
    assert volume.IsA(OmniSci.Dataset)
    assert volume.HasAPI(OmniSciOpenFoam.PolyMeshAPI)


@pytest.mark.integration
def test_mesh_arrays_load(stage):
    volume = stage.GetPrimAtPath("/minimal/Volume")
    points = volume.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    face_indices = volume.GetAttribute("omni:sci:array:faces:value").Get(Usd.TimeCode.EarliestTime())
    face_counts = volume.GetAttribute("omni:sci:array:facesOffsets:value").Get(Usd.TimeCode.EarliestTime())
    owner = volume.GetAttribute("omni:sci:array:owner:value").Get(Usd.TimeCode.EarliestTime())
    neighbour = volume.GetAttribute("omni:sci:array:neighbour:value").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) == 8
    assert face_indices is not None and len(face_indices) == 24
    assert face_counts is not None and list(face_counts) == [0, 4, 8, 12, 16, 20, 24]
    assert owner is not None and list(owner) == [0, 0, 0, 0, 0, 0]
    assert neighbour is not None and len(neighbour) == 0


@pytest.mark.integration
def test_boundary_patch_metadata(stage):
    patch = stage.GetPrimAtPath("/minimal/Boundaries/walls")
    assert patch
    assert patch.IsA(OmniSci.Dataset)
    patch_api = OmniSciOpenFoam.BoundaryPatchAPI(patch)
    assert patch_api.GetMeshRel().GetTargets() == [Sdf.Path("/minimal/Volume")]
    assert patch_api.GetNameAttr().Get(Usd.TimeCode.EarliestTime()) == "walls"
    assert patch_api.GetTypeAttr().Get(Usd.TimeCode.EarliestTime()) == "wall"
    assert patch_api.GetStartFaceAttr().Get(Usd.TimeCode.EarliestTime()) == 0
    assert patch_api.GetNFacesAttr().Get(Usd.TimeCode.EarliestTime()) == 6


@pytest.mark.integration
def test_internal_fields_are_time_sampled(stage):
    volume = stage.GetPrimAtPath("/minimal/Volume")
    t_attr = volume.GetAttribute("omni:sci:array:T:value")
    u_attr = volume.GetAttribute("omni:sci:array:U:value")
    assert t_attr.IsValid()
    assert u_attr.IsValid()
    assert t_attr.Get(Usd.TimeCode(0)) == pytest.approx([300.0])
    assert t_attr.Get(Usd.TimeCode(1)) == pytest.approx([350.0])
    u0 = u_attr.Get(Usd.TimeCode(0))
    u1 = u_attr.Get(Usd.TimeCode(1))
    assert u0 is not None and len(u0) == 1
    assert u1 is not None and len(u1) == 1
    assert tuple(u0[0]) == pytest.approx((1.0, 0.0, 0.0))
    assert tuple(u1[0]) == pytest.approx((0.0, 1.0, 0.0))


@pytest.mark.integration
def test_binary_case_volume_dataset_exists(binary_stage):
    volume = binary_stage.GetPrimAtPath("/squareBend/Volume")
    assert volume
    assert volume.IsA(OmniSci.Dataset)
    assert volume.HasAPI(OmniSciOpenFoam.PolyMeshAPI)


@pytest.mark.integration
def test_binary_mesh_arrays_load(binary_stage):
    volume = binary_stage.GetPrimAtPath("/squareBend/Volume")
    points = volume.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    faces = volume.GetAttribute("omni:sci:array:faces:value").Get(Usd.TimeCode.EarliestTime())
    offsets = volume.GetAttribute("omni:sci:array:facesOffsets:value").Get(Usd.TimeCode.EarliestTime())
    owner = volume.GetAttribute("omni:sci:array:owner:value").Get(Usd.TimeCode.EarliestTime())
    neighbour = volume.GetAttribute("omni:sci:array:neighbour:value").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) > 100000
    assert faces is not None and len(faces) > 300000
    assert offsets is not None and len(offsets) > 300000
    assert owner is not None and len(owner) > 300000
    assert neighbour is not None and len(neighbour) > 300000
    assert list(offsets[:4]) == [0, 4, 8, 12]


@pytest.mark.integration
def test_binary_fields_are_time_sampled(binary_stage):
    volume = binary_stage.GetPrimAtPath("/squareBend/Volume")
    t_attr = volume.GetAttribute("omni:sci:array:T:value")
    u_attr = volume.GetAttribute("omni:sci:array:U:value")
    assert t_attr.IsValid()
    assert u_attr.IsValid()
    t0 = t_attr.Get(Usd.TimeCode(0))
    t1 = t_attr.Get(Usd.TimeCode(1))
    u0 = u_attr.Get(Usd.TimeCode(0))
    u1 = u_attr.Get(Usd.TimeCode(1))
    assert t0 is not None and len(t0) == 112000
    assert t1 is not None and len(t1) == 112000
    assert u0 is not None and len(u0) == 112000
    assert u1 is not None and len(u1) == 112000


@pytest.mark.integration
def test_binary_arrays_match_with_parallel_read_args():
    if not _BINARY_CASE.exists():
        pytest.skip("Binary OpenFOAM test data not found")
    if not _openfoam_available():
        pytest.skip("OmniSciOpenFoamFileFormat plugin not registered")

    threaded_id = Sdf.Layer.CreateIdentifier(str(_BINARY_CASE), {"ioThreads": "4"})
    threaded_stage = Usd.Stage.Open(threaded_id)
    assert threaded_stage

    default_stage = Usd.Stage.Open(str(_BINARY_CASE))
    assert default_stage
    default_volume = default_stage.GetPrimAtPath("/squareBend/Volume")
    threaded_volume = threaded_stage.GetPrimAtPath("/squareBend/Volume")
    default_points = default_volume.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    threaded_points = threaded_volume.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    default_owner = default_volume.GetAttribute("omni:sci:array:owner:value").Get(Usd.TimeCode.EarliestTime())
    threaded_owner = threaded_volume.GetAttribute("omni:sci:array:owner:value").Get(Usd.TimeCode.EarliestTime())
    assert default_points is not None and threaded_points is not None
    assert default_owner is not None and threaded_owner is not None
    assert len(default_points) == len(threaded_points)
    assert len(default_owner) == len(threaded_owner)
    assert tuple(default_points[0]) == pytest.approx(tuple(threaded_points[0]))
    assert tuple(default_points[-1]) == pytest.approx(tuple(threaded_points[-1]))
    assert list(default_owner[:64]) == list(threaded_owner[:64])
