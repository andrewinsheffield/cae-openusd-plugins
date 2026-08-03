# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciReservoir as OmniSciReservoir


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "reservoir" / "10_results"
_INIT = _DATA_DIR / "RESULTS.INIT"


def _init_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciInitFileFormat") is not None


def _open_init_stage(args=None):
    if not _INIT.exists():
        pytest.skip(f"INIT fixture not found: {_INIT}")
    if not _init_available():
        pytest.skip("OmniSciInitFileFormat plugin not registered")

    identifier = str(_INIT) if args is None else Sdf.Layer.CreateIdentifier(str(_INIT), args)
    stage = Usd.Stage.Open(identifier)
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _init_available(), "OmniSciInitFileFormat not registered"


@pytest.mark.integration
def test_init_whitelist_fields_load():
    stage = _open_init_stage()
    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path("/RESULTS")
    assert prim.IsA(OmniSci.Dataset)
    assert not prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)

    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "PORO")
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "FIPNUM")
    assert not prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "CUSTOM")

    poro_api = OmniSciReservoir.CellPropertyAPI(prim, "PORO")
    fipnum_api = OmniSciReservoir.CellPropertyAPI(prim, "FIPNUM")
    assert poro_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "activeCells"
    assert poro_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "PORO"
    assert fipnum_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
    assert fipnum_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "FIPNUM"

    poro = prim.GetAttribute("omni:sci:array:PORO:value").Get(Usd.TimeCode.EarliestTime())
    fipnum = prim.GetAttribute("omni:sci:array:FIPNUM:value").Get(Usd.TimeCode.EarliestTime())
    assert list(poro) == pytest.approx([0.2, 0.25, 0.3])
    assert list(fipnum) == [1, 1, 2, 2]


@pytest.mark.integration
def test_init_all_cell_sized_escape_hatch():
    stage = _open_init_stage({"reservoirKeywordMode": "allCellSized"})
    prim = stage.GetDefaultPrim()
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "CUSTOM")

    custom_api = OmniSciReservoir.CellPropertyAPI(prim, "CUSTOM")
    assert custom_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
    assert custom_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "CUSTOM"
    assert list(prim.GetAttribute("omni:sci:array:CUSTOM:value").Get(Usd.TimeCode.EarliestTime())) == pytest.approx([9.0, 8.0, 7.0, 6.0])
