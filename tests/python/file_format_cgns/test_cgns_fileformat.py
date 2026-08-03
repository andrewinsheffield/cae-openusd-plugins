# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""
Integration tests for the CGNS file format plugin.

Tests open tests/data/StaticMixer.cgns -- a single-zone unstructured CFD mesh
with one flow solution.  These tests are marked 'integration' and skipped by
default; run them with:

    pytest -m integration tests/python/file_format_cgns/

Structure expected in StaticMixer.cgns:
    Base "Base" / Zone "StaticMixer" (Unstructured, 3-D)
        GridCoordinates: CoordinateX, CoordinateY, CoordinateZ
        Sections: B1.P3, StaticMixer Default, in1, in2, out
        Flow Solution "Flow Solution":
            Pressure, VelocityX, VelocityY, VelocityZ, Temperature, ...
"""

import pathlib
import textwrap

import pytest
from pxr import Sdf, Usd, UsdGeom

pytest.importorskip("pxr.OmniSci",  reason="omniSci plugin not available")
pytest.importorskip("pxr.OmniSciCgns", reason="omniSciCgns plugin not available")
pytest.importorskip("pxr.OmniSciFileFormatArgs", reason="omniSciFileFormatArgs plugin not available")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data"
_CGNS_FILE = _DATA_DIR / "StaticMixer.cgns"


def _cgns_available() -> bool:
    """True if the CGNS file format is registered with USD."""
    return Sdf.FileFormat.FindById("OmniSciCgnsFileFormat") is not None


def _root_path(stage: Usd.Stage) -> str:
    return "/StaticMixer"


def _cgns_structure_layers(stage: Usd.Stage):
    cgns_format = Sdf.FileFormat.FindById("OmniSciCgnsFileFormat")
    return [
        layer
        for layer in stage.GetUsedLayers()
        if layer.GetFileFormat() == cgns_format
    ]


# ---------------------------------------------------------------------------
# Session-scoped stage fixture -- opened once for all tests in this module.
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def static_mixer_stage():
    if not _CGNS_FILE.exists():
        pytest.skip(f"Test data not found: {_CGNS_FILE}")
    if not _cgns_available():
        pytest.skip("OmniSciCgnsFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_CGNS_FILE))
    assert stage, f"Failed to open {_CGNS_FILE}"
    return stage


# ---------------------------------------------------------------------------
# Basic structure
# ---------------------------------------------------------------------------

@pytest.mark.integration
def test_plugin_registered():
    assert _cgns_available(), "OmniSciCgnsFileFormat not registered"


@pytest.mark.integration
def test_stage_opens(static_mixer_stage):
    assert static_mixer_stage is not None


@pytest.mark.integration
def test_layer_authors_canonical_tcps(static_mixer_stage):
    """CGNS layers self-describe simulation seconds with TCPS=1.0."""
    assert static_mixer_stage.GetTimeCodesPerSecond() == pytest.approx(1.0)


@pytest.mark.integration
def test_stage_uses_one_cgns_layer_without_private_sublayers(static_mixer_stage):
    layers = _cgns_structure_layers(static_mixer_stage)
    assert len(layers) == 1
    assert layers[0].subLayerPaths == []


@pytest.mark.integration
def test_default_prim_is_root_scope(static_mixer_stage):
    default = static_mixer_stage.GetDefaultPrim()
    assert default.IsValid()
    assert default.GetPath() == Sdf.Path("/StaticMixer")
    assert not static_mixer_stage.GetPrimAtPath("/World").IsValid()


@pytest.mark.integration
def test_root_is_scope(static_mixer_stage):
    scope = UsdGeom.Scope(static_mixer_stage.GetPrimAtPath(_root_path(static_mixer_stage)))
    assert scope


@pytest.mark.integration
def test_payload_attribute_sugar_sets_dynamic_arguments():
    if not _CGNS_FILE.exists():
        pytest.skip(f"Test data not found: {_CGNS_FILE}")
    if not _cgns_available():
        pytest.skip("OmniSciCgnsFileFormat plugin not registered")
    from pxr import OmniSciFileFormatArgs

    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Cgns")
    cgns_args_api = OmniSciFileFormatArgs.CgnsAPI.Apply(prim)
    cgns_args_api.CreateBaseNameAttr().Set("Base")
    cgns_args_api.CreateZoneNameAttr().Set("StaticMixer")
    prim.GetPayloads().AddPayload(str(_CGNS_FILE))
    stage.Load(prim.GetPath())

    # The payload's default prim composes onto the host's payload prim, so the
    # CGNS Base appears one level under /Cgns (with no /UiRoot wrapper).
    assert stage.GetPrimAtPath("/Cgns/Base")
    assert prim.HasAPI(OmniSciFileFormatArgs.CgnsAPI)

    layers = _cgns_structure_layers(stage)
    assert len(layers) == 1
    assert layers[0].GetFileFormatArguments().get("baseName") == "Base"
    assert layers[0].GetFileFormatArguments().get("zoneName") == "StaticMixer"


@pytest.mark.integration
def test_payload_attribute_sugar_recomposes_after_ui_style_edit():
    if not _CGNS_FILE.exists():
        pytest.skip(f"Test data not found: {_CGNS_FILE}")
    if not _cgns_available():
        pytest.skip("OmniSciCgnsFileFormat plugin not registered")
    from pxr import OmniSciFileFormatArgs

    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Cgns")
    cgns_args_api = OmniSciFileFormatArgs.CgnsAPI.Apply(prim)
    base_attr = cgns_args_api.CreateBaseNameAttr()
    base_attr.Set("Base")
    prim.GetPayloads().AddPayload(str(_CGNS_FILE))
    stage.Load(prim.GetPath())

    assert stage.GetPrimAtPath("/Cgns/Base")

    # Setting baseName to a non-existent base should drop the Base prim from
    # the composed result (the structure layer matches no base).
    base_attr.Set("NoSuchBase")
    layers = _cgns_structure_layers(stage)
    assert len(layers) == 1
    assert layers[0].GetFileFormatArguments().get("baseName") == "NoSuchBase"


# ---------------------------------------------------------------------------
# Base and zone
# ---------------------------------------------------------------------------

@pytest.mark.integration
def test_base_prim_exists(static_mixer_stage):
    base = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base")
    assert base.IsValid(), "CGNSBase prim not found"
    assert base.GetTypeName() == "CGNSBase"


@pytest.mark.integration
def test_zone_prim_exists(static_mixer_stage):
    from pxr import OmniSci
    zone = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base/StaticMixer")
    assert zone.IsValid(), "Zone prim not found"
    assert zone.GetTypeName() == "CGNSZone"
    assert not zone.IsA(OmniSci.Dataset)


@pytest.mark.integration
def test_zone_has_cgns_zone_api(static_mixer_stage):
    from pxr import OmniSciCgns
    zone = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base/StaticMixer")
    assert OmniSciCgns.ZoneAPI(zone)


@pytest.mark.integration
def test_zone_sections_relationship(static_mixer_stage):
    from pxr import OmniSciCgns
    zone = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base/StaticMixer")
    api = OmniSciCgns.ZoneAPI(zone)
    rel = api.GetSectionsRel()
    targets = rel.GetTargets()
    assert len(targets) == 5, f"Expected 5 sections, got {len(targets)}"


@pytest.mark.integration
def test_zone_flow_solutions_relationship(static_mixer_stage):
    from pxr import OmniSciCgns
    zone = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base/StaticMixer")
    api = OmniSciCgns.ZoneAPI(zone)
    rel = api.GetFlowSolutionsRel()
    targets = rel.GetTargets()
    assert len(targets) >= 1, "Expected at least one flow solution"


@pytest.mark.integration
def test_zone_grid_coordinates_relationship(static_mixer_stage):
    from pxr import OmniSciCgns
    zone = static_mixer_stage.GetPrimAtPath(f"{_root_path(static_mixer_stage)}/Base/StaticMixer")
    api = OmniSciCgns.ZoneAPI(zone)
    rel = api.GetGridCoordinatesRel()
    targets = rel.GetTargets()
    assert targets == [Sdf.Path(f"{_root_path(static_mixer_stage)}/Base/StaticMixer/GridCoordinates")]


@pytest.mark.integration
def test_grid_coordinates_exists(static_mixer_stage):
    from pxr import OmniSciCgns
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/GridCoordinates")
    assert prim.IsValid(), "GridCoordinates prim not found"
    assert prim.GetTypeName() == "CGNSGridCoordinates"
    assert OmniSciCgns.GridCoordinatesAPI(prim)


# ---------------------------------------------------------------------------
# Sections / element sets
# ---------------------------------------------------------------------------

@pytest.mark.integration
@pytest.mark.parametrize("section_name", [
    "B1_P3",          # B1.P3  -> TfMakeValidIdentifier
    "StaticMixer_Default",  # "StaticMixer Default"
    "in1",
    "in2",
    "out",
])
def test_section_exists(static_mixer_stage, section_name):
    path = f"{_root_path(static_mixer_stage)}/Base/StaticMixer/{section_name}"
    prim = static_mixer_stage.GetPrimAtPath(path)
    assert prim.IsValid(), f"Section prim not found: {path}"


@pytest.mark.integration
def test_section_has_unstructured_elements_api(static_mixer_stage):
    from pxr import OmniSciCgns
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/StaticMixer_Default")
    assert OmniSciCgns.UnstructuredElementsAPI(prim)


@pytest.mark.integration
def test_section_element_type_is_set(static_mixer_stage):
    from pxr import OmniSciCgns
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/StaticMixer_Default")
    api = OmniSciCgns.UnstructuredElementsAPI(prim)
    elem_type = api.GetElementTypeAttr().Get(Usd.TimeCode.EarliestTime())
    assert elem_type is not None
    assert str(elem_type) != "ElementTypeNull"


@pytest.mark.integration
def test_section_element_range_is_set(static_mixer_stage):
    from pxr import OmniSciCgns
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/StaticMixer_Default")
    api = OmniSciCgns.UnstructuredElementsAPI(prim)
    elem_range = api.GetElementRangeAttr().Get(Usd.TimeCode.EarliestTime())
    assert elem_range is not None
    start, end = elem_range[0], elem_range[1]
    assert end >= start > 0


# ---------------------------------------------------------------------------
# Flow solution
# ---------------------------------------------------------------------------

@pytest.mark.integration
def test_flow_solution_exists(static_mixer_stage):
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/Flow_Solution")
    assert prim.IsValid(), "Flow Solution prim not found"
    assert prim.GetTypeName() == "CGNSFlowSolution"


@pytest.mark.integration
def test_flow_solution_grid_location(static_mixer_stage):
    from pxr import OmniSciCgns
    prim = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/Flow_Solution")
    api = OmniSciCgns.FlowSolutionAPI(prim)
    loc = api.GetGridLocationAttr().Get(Usd.TimeCode.EarliestTime())
    assert loc in ("Vertex", "CellCenter")


# ---------------------------------------------------------------------------
# Lazy-loaded array data
# ---------------------------------------------------------------------------

@pytest.mark.integration
def test_grid_coordinate_x_loads(static_mixer_stage):
    """Grid coordinate X should resolve to a non-empty float/double array."""
    coords = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/GridCoordinates")
    attr = coords.GetAttribute("omni:sci:array:gridCoordinatesX:value")
    assert attr.IsValid(), "gridCoordinatesX:value attribute not found"
    data = attr.Get(Usd.TimeCode.EarliestTime())
    assert data is not None, "gridCoordinatesX:value returned None"
    assert len(data) > 0, "gridCoordinatesX:value is empty"


@pytest.mark.integration
def test_single_state_array_requires_explicit_time(static_mixer_stage):
    attr = static_mixer_stage.GetAttributeAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/GridCoordinates"
        ".omni:sci:array:gridCoordinatesX:value")
    assert attr.GetTimeSamples() == [0.0]
    assert attr.Get() is None
    assert len(attr.Get(Usd.TimeCode.EarliestTime())) > 0


@pytest.mark.integration
def test_single_state_array_uses_transformed_time():
    layer = Sdf.Layer.FindOrOpen(
        str(_CGNS_FILE),
        {"timeScale": "3", "timeOffset": "7"},
    )
    stage = Usd.Stage.Open(layer)
    attr = stage.GetAttributeAtPath(
        "/StaticMixer/Base/StaticMixer/GridCoordinates"
        ".omni:sci:array:gridCoordinatesX:value")
    assert attr.GetTimeSamples() == [7.0]
    assert attr.Get() is None
    assert len(attr.Get(Usd.TimeCode(7))) > 0


@pytest.mark.integration
def test_cgns_layer_is_directly_usable_as_value_clip(tmp_path: pathlib.Path):
    manifest = tmp_path / "manifest.usda"
    manifest.write_text(
        textwrap.dedent(
            """\
            #usda 1.0

            def "StaticMixer"
            {
                def "Base"
                {
                    def "StaticMixer"
                    {
                        def "Flow_Solution"
                        {
                            float[] omni:sci:array:Pressure:value
                        }
                    }
                }
            }
            """
        ),
        encoding="utf-8",
    )

    root = tmp_path / "root.usda"
    root.write_text(
        textwrap.dedent(
            f"""\
            #usda 1.0
            (
                startTimeCode = 0
                endTimeCode = 0
                timeCodesPerSecond = 1.0
            )

            def "StaticMixer"
            {{
                def "Base"
                {{
                    def "StaticMixer"
                    {{
                        def "Flow_Solution" (
                            clips = {{
                                dictionary fields = {{
                                    asset[] assetPaths = [
                                        @{_CGNS_FILE.as_posix()}:SDF_FORMAT_ARGS:mountPath=/StaticMixer&timeOffset=7@
                                    ]
                                    double2[] active = [(0, 0)]
                                    double2[] times = [(0, 7)]
                                    asset manifestAssetPath = @{manifest.as_posix()}@
                                    string primPath = "/StaticMixer/Base/StaticMixer/Flow_Solution"
                                }}
                            }}
                            clipSets = ["fields"]
                        )
                        {{
                            float[] omni:sci:array:Pressure:value
                        }}
                    }}
                }}
            }}
            """
        ),
        encoding="utf-8",
    )

    stage = Usd.Stage.Open(str(root))
    attr = stage.GetAttributeAtPath(
        "/StaticMixer/Base/StaticMixer/Flow_Solution"
        ".omni:sci:array:Pressure:value")
    assert attr.Get() is None
    assert len(attr.Get(Usd.TimeCode(0))) > 0


@pytest.mark.integration
def test_pressure_field_loads(static_mixer_stage):
    """Pressure field should resolve to a non-empty float/double array."""
    fs = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/Flow_Solution")
    attr = fs.GetAttribute("omni:sci:array:Pressure:value")
    assert attr.IsValid(), "Pressure:value attribute not found"
    data = attr.Get(Usd.TimeCode.EarliestTime())
    assert data is not None, "Pressure:value returned None"
    assert len(data) > 0, "Pressure:value is empty"


@pytest.mark.integration
def test_element_connectivity_loads(static_mixer_stage):
    """Element connectivity for the volume section should be a non-empty int array."""
    section = static_mixer_stage.GetPrimAtPath(
        f"{_root_path(static_mixer_stage)}/Base/StaticMixer/StaticMixer_Default")
    attr = section.GetAttribute("omni:sci:array:elementConnectivity:value")
    assert attr.IsValid(), "elementConnectivity:value attribute not found"
    data = attr.Get(Usd.TimeCode.EarliestTime())
    assert data is not None, "elementConnectivity:value returned None"
    assert len(data) > 0, "elementConnectivity:value is empty"
