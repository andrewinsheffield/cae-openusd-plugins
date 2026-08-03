# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciOpenFoam schema library."""

from pxr import Sdf, Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciOpenFoam as OmniSciOpenFoam


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciOpenFoamPolyMeshAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciOpenFoamBoundaryPatchAPI").isUnknown


class TestPolyMeshAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Volume").GetPrim()
        api = OmniSciOpenFoam.PolyMeshAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciOpenFoam.PolyMeshAPI)


class TestBoundaryPatchAPI:
    def test_apply(self, stage):
        patch = OmniSci.Dataset.Define(stage, "/Case/Boundaries/walls").GetPrim()
        api = OmniSciOpenFoam.BoundaryPatchAPI.Apply(patch)
        assert api
        assert patch.HasAPI(OmniSciOpenFoam.BoundaryPatchAPI)
        assert patch.IsA(OmniSci.Dataset)

    def test_patch_metadata(self, stage):
        patch = OmniSci.Dataset.Define(stage, "/Case/Boundaries/walls").GetPrim()
        api = OmniSciOpenFoam.BoundaryPatchAPI.Apply(patch)
        api.CreateMeshRel().SetTargets([Sdf.Path("/Case/Volume")])
        api.CreateNameAttr().Set("walls")
        api.CreateTypeAttr().Set("wall")
        api.CreateStartFaceAttr().Set(0)
        api.CreateNFacesAttr().Set(6)
        assert api.GetMeshRel().GetTargets() == [Sdf.Path("/Case/Volume")]
        assert api.GetNameAttr().Get() == "walls"
        assert api.GetTypeAttr().Get() == "wall"
        assert api.GetStartFaceAttr().Get() == 0
        assert api.GetNFacesAttr().Get() == 6


def test_roundtrip(tmp_path):
    path = str(tmp_path / "openfoam.usda")

    stage = Usd.Stage.CreateNew(path)
    volume = OmniSci.Dataset.Define(stage, "/Case/Volume").GetPrim()
    patch = OmniSci.Dataset.Define(stage, "/Case/Boundaries/walls").GetPrim()

    OmniSciOpenFoam.PolyMeshAPI.Apply(volume)

    patch_api = OmniSciOpenFoam.BoundaryPatchAPI.Apply(patch)
    patch_api.CreateMeshRel().SetTargets([volume.GetPath()])
    patch_api.CreateNameAttr().Set("walls")
    patch_api.CreateTypeAttr().Set("wall")
    patch_api.CreateStartFaceAttr().Set(0)
    patch_api.CreateNFacesAttr().Set(6)

    stage.GetRootLayer().Save()

    reopened = Usd.Stage.Open(path)
    patch_prim = reopened.GetPrimAtPath("/Case/Boundaries/walls")
    patch2 = OmniSciOpenFoam.BoundaryPatchAPI(patch_prim)

    assert patch_prim.IsA(OmniSci.Dataset)
    assert patch2.GetMeshRel().GetTargets() == [Sdf.Path("/Case/Volume")]
    assert patch2.GetNameAttr().Get() == "walls"
    assert patch2.GetTypeAttr().Get() == "wall"
    assert patch2.GetStartFaceAttr().Get() == 0
    assert patch2.GetNFacesAttr().Get() == 6
