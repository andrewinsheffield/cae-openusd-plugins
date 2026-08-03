# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciReservoir as OmniSciReservoir


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "GRDECL"
_DECK = _DATA_DIR / "minimal.DATA"
_GRID = _DATA_DIR / "minimal_grid.GRDECL"
_RESERVOIR_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "reservoir"
_RESERVOIR_CASES = [
    pytest.param("01_single_cell", "SINGLE_CELL", (1, 1, 1), 1, id="single_cell"),
    pytest.param("02_tiny", "TINY", (3, 3, 2), 18, id="tiny"),
    pytest.param("03_small_cubic", "SMALL_CUBIC", (10, 10, 10), 1000, id="small_cubic"),
    pytest.param("04_anisotropic", "ANISOTROPIC", (10, 8, 5), 400, id="anisotropic"),
    pytest.param("05_with_inactive", "WITH_INACTIVE", (10, 10, 5), 428, id="with_inactive"),
    pytest.param("06_with_properties", "WITH_PROPERTIES", (10, 10, 5), 500, id="with_properties"),
    pytest.param("07_thin_slice", "THIN_SLICE", (20, 20, 1), 400, id="thin_slice"),
    pytest.param("08_column", "COLUMN", (1, 1, 20), 20, id="column"),
    pytest.param("09_medium", "MEDIUM", (30, 30, 10), 9000, id="medium"),
]


def _grdecl_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciGrdeclFileFormat") is not None


def _reservoir_case_path(subdir, name):
    return _RESERVOIR_DIR / subdir / f"{name}.GRDECL"


def _open_grdecl_stage(path):
    if not path.exists():
        pytest.skip(f"GRDECL fixture not found: {path}")
    if not _grdecl_available():
        pytest.skip("OmniSciGrdeclFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(path))
    assert stage
    return stage


def _assert_logical_to_active_matches_actnum(prim, actnum, active_count):
    lookup = prim.GetAttribute("omni:sci:array:logicalCellToActiveCell:value").Get(Usd.TimeCode.EarliestTime())
    assert lookup is not None and len(lookup) == len(actnum)

    next_active = 0
    for active_flag, active_index in zip(actnum, lookup, strict=True):
        if active_flag != 0:
            assert active_index == next_active
            next_active += 1
        else:
            assert active_index == -1
    assert next_active == active_count


@pytest.fixture(scope="module")
def deck_stage():
    if not _DECK.exists():
        pytest.skip("GRDECL deck fixture not found")
    if not _grdecl_available():
        pytest.skip("OmniSciGrdeclFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_DECK))
    assert stage
    return stage


@pytest.fixture(scope="module")
def grid_stage():
    if not _GRID.exists():
        pytest.skip("GRDECL grid fixture not found")
    if not _grdecl_available():
        pytest.skip("OmniSciGrdeclFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_GRID))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _grdecl_available(), "OmniSciGrdeclFileFormat not registered"


@pytest.mark.integration
def test_deck_default_prim_and_schema(deck_stage):
    prim = deck_stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path("/minimal")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert "OmniSciCaeMeshAPI" not in prim.GetAppliedSchemas()

    api = OmniSciReservoir.CornerPointGridAPI(prim)
    assert api.GetLogicalCellDimsAttr().Get(Usd.TimeCode.EarliestTime()) == (2, 1, 1)
    assert api.GetSourceFormatAttr().Get(Usd.TimeCode.EarliestTime()) == "grdecl"
    assert api.GetIndexOrderAttr().Get(Usd.TimeCode.EarliestTime()) == "eclipse"
    assert api.GetDepthDirectionAttr().Get(Usd.TimeCode.EarliestTime()) == "zDown"
    assert api.GetLengthUnitAttr().Get(Usd.TimeCode.EarliestTime()) == "m"
    assert list(api.GetMapAxesAttr().Get(Usd.TimeCode.EarliestTime())) == [0.0, 100.0, 0.0, 0.0, 100.0, 0.0]


@pytest.mark.integration
def test_deck_grid_arrays_load(deck_stage):
    prim = deck_stage.GetDefaultPrim()
    coord = prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())
    zcorn = prim.GetAttribute("omni:sci:array:zcorn:value").Get(Usd.TimeCode.EarliestTime())
    actnum = prim.GetAttribute("omni:sci:array:actnum:value").Get(Usd.TimeCode.EarliestTime())

    assert coord is not None and len(coord) == 36
    assert list(coord[:6]) == pytest.approx([0, 0, 0, 0, 0, 10])
    assert zcorn is not None and len(zcorn) == 16
    assert list(zcorn[:8]) == pytest.approx([0, 0, 0, 0, 10, 10, 10, 10])
    assert actnum is not None and list(actnum) == [1, 0]
    _assert_logical_to_active_matches_actnum(prim, actnum, 1)


@pytest.mark.integration
def test_deck_cell_properties_load(deck_stage):
    prim = deck_stage.GetDefaultPrim()
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "PORO")
    assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, "FIPNUM")

    poro_api = OmniSciReservoir.CellPropertyAPI(prim, "PORO")
    fipnum_api = OmniSciReservoir.CellPropertyAPI(prim, "FIPNUM")
    assert poro_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
    assert poro_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "PORO"
    assert fipnum_api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
    assert fipnum_api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == "FIPNUM"

    poro = prim.GetAttribute("omni:sci:array:PORO:value").Get(Usd.TimeCode.EarliestTime())
    fipnum = prim.GetAttribute("omni:sci:array:FIPNUM:value").Get(Usd.TimeCode.EarliestTime())
    assert list(poro) == pytest.approx([0.25, 0.35])
    assert list(fipnum) == [1, 2]


@pytest.mark.integration
def test_direct_grdecl_grid_loads(grid_stage):
    prim = grid_stage.GetDefaultPrim()
    assert prim.GetPath() == Sdf.Path("/minimal_grid")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert OmniSciReservoir.CornerPointGridAPI(prim).GetLogicalCellDimsAttr().Get(Usd.TimeCode.EarliestTime()) == (2, 1, 1)
    assert len(prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())) == 36
    assert len(prim.GetAttribute("omni:sci:array:zcorn:value").Get(Usd.TimeCode.EarliestTime())) == 16
    assert not prim.GetAttribute("omni:sci:array:actnum:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:logicalCellToActiveCell:value").IsValid()


@pytest.mark.integration
def test_mount_path_argument():
    if not _DECK.exists():
        pytest.skip("GRDECL deck fixture not found")
    if not _grdecl_available():
        pytest.skip("OmniSciGrdeclFileFormat plugin not registered")

    identifier = Sdf.Layer.CreateIdentifier(str(_DECK), {"mountPath": "/Reservoir/Norne"})
    stage = Usd.Stage.Open(identifier)
    assert stage
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Reservoir")
    prim = stage.GetPrimAtPath("/Reservoir/Norne")
    assert prim
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert len(prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())) == 36


@pytest.mark.integration
@pytest.mark.parametrize("subdir,name,dims,active_count", _RESERVOIR_CASES)
def test_generated_grdecl_fixtures_load(subdir, name, dims, active_count):
    stage = _open_grdecl_stage(_reservoir_case_path(subdir, name))
    prim = stage.GetDefaultPrim()
    assert prim
    assert prim.GetPath() == Sdf.Path(f"/{name}")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciReservoir.CornerPointGridAPI)
    assert "OmniSciCaeMeshAPI" not in prim.GetAppliedSchemas()

    api = OmniSciReservoir.CornerPointGridAPI(prim)
    assert api.GetLogicalCellDimsAttr().Get(Usd.TimeCode.EarliestTime()) == dims
    assert api.GetSourceFormatAttr().Get(Usd.TimeCode.EarliestTime()) == "grdecl"
    assert api.GetLengthUnitAttr().Get(Usd.TimeCode.EarliestTime()) == "m"

    coord = prim.GetAttribute("omni:sci:array:coord:value").Get(Usd.TimeCode.EarliestTime())
    zcorn = prim.GetAttribute("omni:sci:array:zcorn:value").Get(Usd.TimeCode.EarliestTime())
    actnum = prim.GetAttribute("omni:sci:array:actnum:value").Get(Usd.TimeCode.EarliestTime())

    nx, ny, nz = dims
    logical_cell_count = nx * ny * nz
    assert coord is not None and len(coord) == 6 * (nx + 1) * (ny + 1)
    assert zcorn is not None and len(zcorn) == 8 * logical_cell_count
    assert actnum is not None and len(actnum) == logical_cell_count
    assert sum(1 for value in actnum if value != 0) == active_count
    _assert_logical_to_active_matches_actnum(prim, actnum, active_count)


@pytest.mark.integration
def test_generated_grdecl_properties_load():
    stage = _open_grdecl_stage(_reservoir_case_path("06_with_properties", "WITH_PROPERTIES"))
    prim = stage.GetDefaultPrim()
    for keyword in ["PORO", "PERMX", "PERMY", "PERMZ", "NTG"]:
        assert prim.HasAPI(OmniSciReservoir.CellPropertyAPI, keyword)
        api = OmniSciReservoir.CellPropertyAPI(prim, keyword)
        assert api.GetIndexSpaceAttr().Get(Usd.TimeCode.EarliestTime()) == "logicalCells"
        assert api.GetSourceKeywordAttr().Get(Usd.TimeCode.EarliestTime()) == keyword
        values = prim.GetAttribute(f"omni:sci:array:{keyword}:value").Get(Usd.TimeCode.EarliestTime())
        assert values is not None and len(values) == 500

    assert prim.GetAttribute("omni:sci:array:PORO:value").Get(Usd.TimeCode.EarliestTime())[0] == pytest.approx(0.05)
    assert prim.GetAttribute("omni:sci:array:PORO:value").Get(Usd.TimeCode.EarliestTime())[-1] == pytest.approx(0.228)
    assert prim.GetAttribute("omni:sci:array:PERMX:value").Get(Usd.TimeCode.EarliestTime())[0] == pytest.approx(50.0)
    assert prim.GetAttribute("omni:sci:array:PERMZ:value").Get(Usd.TimeCode.EarliestTime())[-1] == pytest.approx(37.0)
    assert prim.GetAttribute("omni:sci:array:NTG:value").Get(Usd.TimeCode.EarliestTime())[0] == pytest.approx(0.85)
