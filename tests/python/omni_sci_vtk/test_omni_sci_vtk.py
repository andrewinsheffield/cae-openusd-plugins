# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciVtk schema library."""

from pxr import Gf, Sdf, Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciVtk as OmniSciVtk


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciVtkUnstructuredGridAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciVtkStructuredGridAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciVtkImageDataAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciVtkRectilinearGridAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciVtkPolyDataAPI").isUnknown


class TestUnstructuredGridAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Unstructured").GetPrim()
        api = OmniSciVtk.UnstructuredGridAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciVtk.UnstructuredGridAPI)
        assert dataset.IsA(OmniSci.Dataset)


class TestStructuredGridAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Structured").GetPrim()
        api = OmniSciVtk.StructuredGridAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciVtk.StructuredGridAPI)

    def test_extents(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Structured").GetPrim()
        api = OmniSciVtk.StructuredGridAPI.Apply(dataset)
        api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
        api.CreateMaxExtentAttr().Set(Gf.Vec3i(9, 4, 2))
        assert tuple(api.GetMinExtentAttr().Get()) == (0, 0, 0)
        assert tuple(api.GetMaxExtentAttr().Get()) == (9, 4, 2)


class TestImageDataAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Image").GetPrim()
        api = OmniSciVtk.ImageDataAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciVtk.ImageDataAPI)

    def test_image_metadata(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Image").GetPrim()
        api = OmniSciVtk.ImageDataAPI.Apply(dataset)
        api.CreateOriginAttr().Set(Gf.Vec3f(1.0, 2.0, 3.0))
        api.CreateSpacingAttr().Set(Gf.Vec3f(0.5, 0.5, 1.0))
        api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
        api.CreateMaxExtentAttr().Set(Gf.Vec3i(31, 31, 15))
        assert tuple(api.GetOriginAttr().Get()) == (1.0, 2.0, 3.0)
        assert tuple(api.GetSpacingAttr().Get()) == (0.5, 0.5, 1.0)
        assert tuple(api.GetMinExtentAttr().Get()) == (0, 0, 0)
        assert tuple(api.GetMaxExtentAttr().Get()) == (31, 31, 15)


class TestRectilinearGridAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Rectilinear").GetPrim()
        api = OmniSciVtk.RectilinearGridAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciVtk.RectilinearGridAPI)

    def test_extents(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Rectilinear").GetPrim()
        api = OmniSciVtk.RectilinearGridAPI.Apply(dataset)
        api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
        api.CreateMaxExtentAttr().Set(Gf.Vec3i(4, 6, 8))
        assert tuple(api.GetMinExtentAttr().Get()) == (0, 0, 0)
        assert tuple(api.GetMaxExtentAttr().Get()) == (4, 6, 8)


class TestPolyDataAPI:
    def test_apply(self, stage):
        dataset = OmniSci.Dataset.Define(stage, "/Case/Surface").GetPrim()
        api = OmniSciVtk.PolyDataAPI.Apply(dataset)
        assert api
        assert dataset.HasAPI(OmniSciVtk.PolyDataAPI)
        assert dataset.IsA(OmniSci.Dataset)


def test_roundtrip(tmp_path):
    path = str(tmp_path / "vtk.usda")

    stage = Usd.Stage.CreateNew(path)
    unstructured = OmniSci.Dataset.Define(stage, "/Case/Volume").GetPrim()
    structured = OmniSci.Dataset.Define(stage, "/Case/Structured").GetPrim()
    image = OmniSci.Dataset.Define(stage, "/Case/Image").GetPrim()
    rectilinear = OmniSci.Dataset.Define(stage, "/Case/Rectilinear").GetPrim()
    poly = OmniSci.Dataset.Define(stage, "/Case/Surface").GetPrim()

    OmniSciVtk.UnstructuredGridAPI.Apply(unstructured)

    structured_api = OmniSciVtk.StructuredGridAPI.Apply(structured)
    structured_api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
    structured_api.CreateMaxExtentAttr().Set(Gf.Vec3i(3, 3, 3))

    image_api = OmniSciVtk.ImageDataAPI.Apply(image)
    image_api.CreateOriginAttr().Set(Gf.Vec3f(0.0, 1.0, 2.0))
    image_api.CreateSpacingAttr().Set(Gf.Vec3f(1.0, 2.0, 3.0))
    image_api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
    image_api.CreateMaxExtentAttr().Set(Gf.Vec3i(7, 7, 0))

    rect_api = OmniSciVtk.RectilinearGridAPI.Apply(rectilinear)
    rect_api.CreateMinExtentAttr().Set(Gf.Vec3i(0, 0, 0))
    rect_api.CreateMaxExtentAttr().Set(Gf.Vec3i(5, 4, 3))

    OmniSciVtk.PolyDataAPI.Apply(poly)

    stage.GetRootLayer().Save()

    reopened = Usd.Stage.Open(path)
    assert reopened.GetPrimAtPath("/Case/Volume").HasAPI(OmniSciVtk.UnstructuredGridAPI)

    structured2 = OmniSciVtk.StructuredGridAPI(reopened.GetPrimAtPath("/Case/Structured"))
    assert tuple(structured2.GetMaxExtentAttr().Get()) == (3, 3, 3)

    image2 = OmniSciVtk.ImageDataAPI(reopened.GetPrimAtPath("/Case/Image"))
    assert tuple(image2.GetOriginAttr().Get()) == (0.0, 1.0, 2.0)
    assert tuple(image2.GetSpacingAttr().Get()) == (1.0, 2.0, 3.0)

    rect2 = OmniSciVtk.RectilinearGridAPI(reopened.GetPrimAtPath("/Case/Rectilinear"))
    assert tuple(rect2.GetMaxExtentAttr().Get()) == (5, 4, 3)

    assert reopened.GetPrimAtPath("/Case/Surface").HasAPI(OmniSciVtk.PolyDataAPI)
