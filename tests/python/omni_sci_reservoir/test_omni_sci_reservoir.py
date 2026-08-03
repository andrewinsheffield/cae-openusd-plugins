# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciReservoir schema library."""

from pxr import Gf, Tf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciReservoir as OmniSciReservoir


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciReservoirCornerPointGridAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciReservoirCellPropertyAPI").isUnknown


def test_corner_point_grid_api(stage):
    prim = OmniSci.Dataset.Define(stage, "/Grid").GetPrim()
    api = OmniSciReservoir.CornerPointGridAPI.Apply(prim)
    api.CreateLogicalCellDimsAttr().Set(Gf.Vec3i(2, 1, 1))
    api.CreateSourceFormatAttr().Set("grdecl")
    api.CreateIndexOrderAttr().Set("eclipse")
    api.CreateDepthDirectionAttr().Set("zDown")
    api.CreateLengthUnitAttr().Set("m")
    api.CreateMapAxesAttr().Set([0.0, 100.0, 0.0, 0.0, 100.0, 0.0])

    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert api.GetLogicalCellDimsAttr().Get() == Gf.Vec3i(2, 1, 1)
    assert api.GetSourceFormatAttr().Get() == "grdecl"
    assert api.GetLengthUnitAttr().Get() == "m"
    assert list(api.GetMapAxesAttr().Get()) == [0.0, 100.0, 0.0, 0.0, 100.0, 0.0]


def test_cell_property_api(stage):
    prim = OmniSci.Dataset.Define(stage, "/Grid").GetPrim()
    api = OmniSciReservoir.CellPropertyAPI.Apply(prim, "poro")
    api.CreateIndexSpaceAttr().Set("logicalCells")
    api.CreateSourceKeywordAttr().Set("PORO")

    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "poro")
    assert api.GetIndexSpaceAttr().Get() == "logicalCells"
    assert api.GetSourceKeywordAttr().Get() == "PORO"


def test_roundtrip(tmp_path):
    path = str(tmp_path / "reservoir.usda")
    stage = Usd.Stage.CreateNew(path)
    prim = OmniSci.Dataset.Define(stage, "/Grid").GetPrim()
    grid_api = OmniSciReservoir.CornerPointGridAPI.Apply(prim)
    grid_api.CreateLogicalCellDimsAttr().Set(Gf.Vec3i(2, 1, 1))
    grid_api.CreateSourceFormatAttr().Set("grdecl")
    property_api = OmniSciReservoir.CellPropertyAPI.Apply(prim, "poro")
    property_api.CreateIndexSpaceAttr().Set("logicalCells")
    property_api.CreateSourceKeywordAttr().Set("PORO")
    stage.GetRootLayer().Save()

    reopened = Usd.Stage.Open(path)
    prim2 = reopened.GetPrimAtPath("/Grid")
    assert prim2.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert prim2.HasAPI(OmniSciReservoir.CellPropertyAPI, "poro")
    assert OmniSciReservoir.CornerPointGridAPI(prim2).GetLogicalCellDimsAttr().Get() == Gf.Vec3i(2, 1, 1)
    assert OmniSciReservoir.CellPropertyAPI(prim2, "poro").GetSourceKeywordAttr().Get() == "PORO"
