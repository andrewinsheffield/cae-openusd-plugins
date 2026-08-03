# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the OmniSciEnSight schema library."""

import pytest
from pxr import OmniSci, OmniSciEnSight, Sdf, Tf, Usd


def test_plugin_registered():
    assert not Tf.Type.FindByName("OmniSciEnSightPiece").isUnknown
    assert not Tf.Type.FindByName("OmniSciEnSightUnstructuredPartAPI").isUnknown
    assert not Tf.Type.FindByName("OmniSciEnSightUnstructuredPieceAPI").isUnknown


class TestPieceSchema:
    def test_define(self, stage):
        piece = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_0").GetPrim()
        assert piece
        assert piece.GetTypeName() == "OmniSciEnSightPiece"
        assert piece.IsA(OmniSciEnSight.Piece)


class TestUnstructuredPartAPI:
    def test_apply(self, stage):
        part = OmniSci.Dataset.Define(stage, "/Part").GetPrim()
        api = OmniSciEnSight.UnstructuredPartAPI.Apply(part)
        assert api
        assert part.HasAPI(OmniSciEnSight.UnstructuredPartAPI)

    def test_part_id(self, stage):
        part = OmniSci.Dataset.Define(stage, "/Part").GetPrim()
        api = OmniSciEnSight.UnstructuredPartAPI.Apply(part)
        api.CreateIdAttr().Set(42)
        assert api.GetIdAttr().Get() == 42

    def test_pieces_rel(self, stage):
        part = OmniSci.Dataset.Define(stage, "/Part").GetPrim()
        piece0 = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_0").GetPrim()
        piece1 = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_1").GetPrim()
        api = OmniSciEnSight.UnstructuredPartAPI.Apply(part)
        api.GetPiecesRel().AddTarget(piece0.GetPath())
        api.GetPiecesRel().AddTarget(piece1.GetPath())
        targets = api.GetPiecesRel().GetTargets()
        assert Sdf.Path("/Part/Piece_0") in targets
        assert Sdf.Path("/Part/Piece_1") in targets


class TestUnstructuredPieceAPI:
    def test_apply(self, stage):
        piece = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_0").GetPrim()
        api = OmniSciEnSight.UnstructuredPieceAPI.Apply(piece)
        assert api
        assert piece.HasAPI(OmniSciEnSight.UnstructuredPieceAPI)
        assert piece.GetTypeName() == "OmniSciEnSightPiece"

    @pytest.mark.parametrize("etype", ["tria3", "hexa8", "nsided", "nfaced"])
    def test_element_type_values(self, stage, etype):
        piece = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_0").GetPrim()
        api = OmniSciEnSight.UnstructuredPieceAPI.Apply(piece)
        api.CreateElementTypeAttr().Set(etype)
        assert api.GetElementTypeAttr().Get() == etype


def test_roundtrip(tmp_path):
    path = str(tmp_path / "ensight_part.usda")

    stage = Usd.Stage.CreateNew(path)
    part = OmniSci.Dataset.Define(stage, "/Part").GetPrim()
    piece = OmniSciEnSight.Piece.Define(stage, "/Part/Piece_0").GetPrim()

    part_api = OmniSciEnSight.UnstructuredPartAPI.Apply(part)
    part_api.CreateIdAttr().Set(7)
    part_api.GetPiecesRel().AddTarget(piece.GetPath())

    piece_api = OmniSciEnSight.UnstructuredPieceAPI.Apply(piece)
    piece_api.CreateElementTypeAttr().Set("nfaced")

    stage.GetRootLayer().Save()

    reopened = Usd.Stage.Open(path)
    part2 = OmniSciEnSight.UnstructuredPartAPI(reopened.GetPrimAtPath("/Part"))
    piece_prim = reopened.GetPrimAtPath("/Part/Piece_0")
    piece2 = OmniSciEnSight.UnstructuredPieceAPI(reopened.GetPrimAtPath("/Part/Piece_0"))

    assert part2.GetIdAttr().Get() == 7
    assert Sdf.Path("/Part/Piece_0") in part2.GetPiecesRel().GetTargets()
    assert piece_prim.GetTypeName() == "OmniSciEnSightPiece"
    assert piece_prim.IsA(OmniSciEnSight.Piece)
    assert piece2.GetElementTypeAttr().Get() == "nfaced"
