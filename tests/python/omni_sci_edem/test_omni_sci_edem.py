# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciEdem schema library."""

from pxr import Sdf, Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciEdem as OmniSciEdem


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciEdemParticleCloudAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciEdemParticleTypeAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciEdemGeometryGroupAPI").isUnknown


class TestParticleCloudAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Particles/Pebble").GetPrim()
        api = OmniSciEdem.ParticleCloudAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciEdem.ParticleCloudAPI)

    def test_properties(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Particles/Pebble").GetPrim()
        api = OmniSciEdem.ParticleCloudAPI.Apply(dataset)
        api.CreatePrototypeRel().SetTargets([Sdf.Path("/Case/ParticleTypes/Pebble")])
        api.CreateNameAttr().Set("Pebble")
        api.CreateSourceNodeAttr().Set("type0")
        assert api.GetPrototypeRel().GetTargets() == [Sdf.Path("/Case/ParticleTypes/Pebble")]
        assert api.GetNameAttr().Get() == "Pebble"
        assert api.GetSourceNodeAttr().Get() == "type0"


class TestParticleTypeAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/Case/ParticleTypes/Pebble", "Xform")
        api = OmniSciEdem.ParticleTypeAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciEdem.ParticleTypeAPI)

    def test_properties(self, stage):
        prim = stage.DefinePrim("/Case/ParticleTypes/Pebble", "Xform")
        api = OmniSciEdem.ParticleTypeAPI.Apply(prim)
        api.CreateNameAttr().Set("Pebble")
        api.CreateSourceNodeAttr().Set("type0")
        api.CreateShapeKindAttr().Set("polyhedral")
        assert api.GetNameAttr().Get() == "Pebble"
        assert api.GetSourceNodeAttr().Get() == "type0"
        assert api.GetShapeKindAttr().Get() == "polyhedral"


class TestGeometryGroupAPI:
    def test_apply(self, stage):
        prim = stage.DefinePrim("/Case/GeometryGroups/Drum", "Mesh")
        api = OmniSciEdem.GeometryGroupAPI.Apply(prim)
        assert api
        assert prim.HasAPI(OmniSciEdem.GeometryGroupAPI)

    def test_properties(self, stage):
        prim = stage.DefinePrim("/Case/GeometryGroups/Drum", "Mesh")
        api = OmniSciEdem.GeometryGroupAPI.Apply(prim)
        api.CreateNameAttr().Set("Drum")
        api.CreateSourceNodeAttr().Set("wall0")
        assert api.GetNameAttr().Get() == "Drum"
        assert api.GetSourceNodeAttr().Get() == "wall0"


def test_roundtrip(tmp_path):
    path = str(tmp_path / "edem.usda")

    stage = Usd.Stage.CreateNew(path)
    particle = OmniSci.Dataset.Define(stage, "/Case/Particles/Pebble").GetPrim()
    prototype = stage.DefinePrim("/Case/ParticleTypes/Pebble", "Xform")
    geometry = stage.DefinePrim("/Case/GeometryGroups/Drum", "Mesh")

    cloud_api = OmniSciEdem.ParticleCloudAPI.Apply(particle)
    cloud_api.CreatePrototypeRel().SetTargets([prototype.GetPath()])
    cloud_api.CreateNameAttr().Set("Pebble")
    cloud_api.CreateSourceNodeAttr().Set("type0")

    type_api = OmniSciEdem.ParticleTypeAPI.Apply(prototype)
    type_api.CreateNameAttr().Set("Pebble")
    type_api.CreateSourceNodeAttr().Set("type0")
    type_api.CreateShapeKindAttr().Set("polyhedral")

    group_api = OmniSciEdem.GeometryGroupAPI.Apply(geometry)
    group_api.CreateNameAttr().Set("Drum")
    group_api.CreateSourceNodeAttr().Set("wall0")

    stage.GetRootLayer().Save()

    reopened = Usd.Stage.Open(path)
    particle2 = OmniSciEdem.ParticleCloudAPI(reopened.GetPrimAtPath("/Case/Particles/Pebble"))
    prototype2 = OmniSciEdem.ParticleTypeAPI(reopened.GetPrimAtPath("/Case/ParticleTypes/Pebble"))
    geometry2 = OmniSciEdem.GeometryGroupAPI(reopened.GetPrimAtPath("/Case/GeometryGroups/Drum"))

    assert particle2.GetPrototypeRel().GetTargets() == [Sdf.Path("/Case/ParticleTypes/Pebble")]
    assert particle2.GetNameAttr().Get() == "Pebble"
    assert prototype2.GetShapeKindAttr().Get() == "polyhedral"
    assert geometry2.GetNameAttr().Get() == "Drum"
