# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib
import textwrap

import numpy as np
import pytest
from pxr import Sdf, Usd

pytest.importorskip("pxr.OmniSciFileFormatArgs", reason="omniSciFileFormatArgs plugin not available")


def _plugin_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciNpzFileFormat") is not None


def _vtk_plugin_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciVtkFileFormat") is not None


@pytest.fixture
def npz_path(tmp_path: pathlib.Path) -> pathlib.Path:
    path = tmp_path / "points.npz"
    np.savez(
        path,
        coords=np.array([[0.0, 1.0, 2.0], [3.0, 4.0, 5.0]], dtype=np.float32),
        temperature=np.array([300.0, 325.0], dtype=np.float32),
        velocity=np.array([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]], dtype=np.float32),
        ids=np.array([10, 20], dtype=np.int32),
    )
    return path


def _identifier(path: pathlib.Path, **kwargs) -> str:
    args = {"schema": "Point Cloud"}
    args.update(kwargs)
    return Sdf.Layer.CreateIdentifier(str(path), args)


def _npz_structure_layers(stage: Usd.Stage):
    npz_format = Sdf.FileFormat.FindById("OmniSciNpzFileFormat")
    return [
        layer
        for layer in stage.GetUsedLayers()
        if layer.GetFileFormat() == npz_format
    ]


@pytest.mark.integration
def test_plugin_registered():
    assert _plugin_available(), "OmniSciNpzFileFormat not registered"


@pytest.mark.integration
def test_payload_attribute_sugar_sets_dynamic_arguments(npz_path: pathlib.Path):
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")
    from pxr import OmniSciFileFormatArgs

    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Npz")
    npz_api = OmniSciFileFormatArgs.NpzAPI.Apply(prim)
    npz_api.CreateSchemaAttr().Set("Point Cloud")
    npz_api.CreateCoordsArrayAttr().Set("coords")
    prim.GetPayloads().AddPayload(str(npz_path))
    stage.Load(prim.GetPath())

    # The payload's default prim composes onto /Npz directly, with no
    # additional wrapper prim.
    assert prim.HasAPI(OmniSciFileFormatArgs.NpzAPI)

    layers = _npz_structure_layers(stage)
    assert len(layers) == 1
    assert layers[0].GetFileFormatArguments().get("schema") == "Point Cloud"
    assert layers[0].GetFileFormatArguments().get("coordsArray") == "coords"


@pytest.mark.integration
def test_npz_opens_as_point_cloud(npz_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npz_path))
    assert stage

    dataset = stage.GetPrimAtPath("/points")
    assert dataset
    assert dataset.GetTypeName() == "OmniSciDataset"
    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciCaePointCloudAPI" in api_schemas
    assert "OmniSciArrayAPI:pointsX" in api_schemas
    assert "OmniSciArrayAPI:pointsY" in api_schemas
    assert "OmniSciArrayAPI:pointsZ" in api_schemas


@pytest.mark.integration
def test_coordinates_default_detection(npz_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npz_path))
    dataset = stage.GetPrimAtPath("/points")
    assert list(dataset.GetAttribute("omni:sci:array:pointsX:value").Get(Usd.TimeCode.EarliestTime())) == [0.0, 3.0]
    assert list(dataset.GetAttribute("omni:sci:array:pointsY:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 4.0]
    assert list(dataset.GetAttribute("omni:sci:array:pointsZ:value").Get(Usd.TimeCode.EarliestTime())) == [2.0, 5.0]


@pytest.mark.integration
def test_scalar_fields_are_exposed(npz_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npz_path))
    dataset = stage.GetPrimAtPath("/points")
    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciFieldAPI:temperature" in api_schemas
    assert "OmniSciArrayAPI:temperature" in api_schemas
    assert "OmniSciFieldAPI:ids" in api_schemas
    assert list(dataset.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())) == [300.0, 325.0]
    assert list(dataset.GetAttribute("omni:sci:array:ids:value").Get(Usd.TimeCode.EarliestTime())) == [10, 20]


@pytest.mark.integration
def test_vector_fields_are_exposed(npz_path: pathlib.Path):
    stage = Usd.Stage.Open(_identifier(npz_path))
    dataset = stage.GetPrimAtPath("/points")
    api_schemas = set(dataset.GetAppliedSchemas())
    assert "OmniSciFieldAPI:velocity" in api_schemas
    assert "OmniSciArrayAPI:velocity" in api_schemas

    velocity = dataset.GetAttribute("omni:sci:array:velocity:value")
    assert str(velocity.GetTypeName()) == "float3[]"
    assert list(velocity.Get(Usd.TimeCode.EarliestTime())) == [
        (1.0, 2.0, 3.0),
        (4.0, 5.0, 6.0),
    ]


@pytest.mark.integration
def test_explicit_coords_array_override(tmp_path: pathlib.Path):
    path = tmp_path / "custom_coords.npz"
    np.savez(
        path,
        xyz=np.array([[1.0, 2.0, 3.0], [6.0, 5.0, 4.0]], dtype=np.float64),
        pressure=np.array([1.5, 2.5], dtype=np.float64),
    )

    stage = Usd.Stage.Open(_identifier(path, coordsArray="xyz"))
    dataset = stage.GetPrimAtPath("/custom_coords")
    assert dataset
    assert list(dataset.GetAttribute("omni:sci:array:pointsX:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 6.0]
    assert list(dataset.GetAttribute("omni:sci:array:pressure:value").Get(Usd.TimeCode.EarliestTime())) == [1.5, 2.5]


# ---------------------------------------------------------------------------
# schema=None tests
# ---------------------------------------------------------------------------

@pytest.fixture
def npz_path_no_coords(tmp_path: pathlib.Path) -> pathlib.Path:
    path = tmp_path / "raw.npz"
    np.savez(
        path,
        temperature=np.array([300.0, 325.0], dtype=np.float32),
        ids=np.array([10, 20], dtype=np.int32),
        matrix=np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float32),  # 2-D, should be skipped
    )
    return path


def _none_identifier(path: pathlib.Path, **kwargs) -> str:
    args = {"schema": "None"}
    args.update(kwargs)
    return Sdf.Layer.CreateIdentifier(str(path), args)


@pytest.mark.integration
def test_none_schema_creates_dataset(npz_path_no_coords: pathlib.Path):
    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    assert stage

    dataset = stage.GetPrimAtPath("/raw")
    assert dataset
    assert dataset.GetTypeName() == "OmniSciDataset"


@pytest.mark.integration
def test_none_schema_registers_arrays(npz_path_no_coords: pathlib.Path):
    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    dataset = stage.GetPrimAtPath("/raw")
    api_schemas = set(dataset.GetAppliedSchemas())

    assert "OmniSciArrayAPI:temperature" in api_schemas
    assert "OmniSciArrayAPI:ids" in api_schemas


@pytest.mark.integration
def test_none_schema_no_field_or_point_cloud_schemas(npz_path_no_coords: pathlib.Path):
    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    dataset = stage.GetPrimAtPath("/raw")
    api_schemas = set(dataset.GetAppliedSchemas())

    assert "OmniSciCaePointCloudAPI" not in api_schemas
    assert not any(s.startswith("OmniSciFieldAPI") for s in api_schemas)


@pytest.mark.integration
def test_none_schema_skips_multidimensional_arrays(npz_path_no_coords: pathlib.Path):
    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    dataset = stage.GetPrimAtPath("/raw")
    api_schemas = set(dataset.GetAppliedSchemas())

    assert "OmniSciArrayAPI:matrix" not in api_schemas


@pytest.mark.integration
def test_none_schema_array_values(npz_path_no_coords: pathlib.Path):
    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    dataset = stage.GetPrimAtPath("/raw")

    assert list(dataset.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())) == [300.0, 325.0]
    assert list(dataset.GetAttribute("omni:sci:array:ids:value").Get(Usd.TimeCode.EarliestTime())) == [10, 20]


@pytest.mark.integration
def test_none_schema_values_are_time_sampled(npz_path_no_coords: pathlib.Path):
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")

    stage = Usd.Stage.Open(_none_identifier(npz_path_no_coords))
    dataset = stage.GetPrimAtPath("/raw")

    values = dataset.GetAttribute("omni:sci:array:temperature:value")
    assert values.GetTimeSamples() == [0.0]
    assert list(values.Get(Usd.TimeCode(0))) == [300.0, 325.0]

    assert dataset.GetAttribute("omni:sci:array:temperature:device").Get() == "cpu"


def _write_npz_value_clip_stage(tmp_path: pathlib.Path) -> pathlib.Path:
    for index, value in enumerate((10.0, 20.0, 30.0)):
        np.savez(
            tmp_path / f"fields_{index:03d}.npz",
            temperature=np.array([value, value + 0.5], dtype=np.float32),
        )

    (tmp_path / "fields_manifest.usda").write_text(
        textwrap.dedent(
            """\
            #usda 1.0

            def "SimResult"
            {
                float[] omni:sci:array:temperature:value
            }
            """
        ),
        encoding="utf-8",
    )

    root_path = tmp_path / "root.usda"
    root_path.write_text(
        textwrap.dedent(
            """\
            #usda 1.0
            (
                startTimeCode = 0
                endTimeCode = 2
                timeCodesPerSecond = 1.0
            )

            def OmniSciDataset "SimResult" (
                prepend apiSchemas = ["OmniSciArrayAPI:temperature"]
                clips = {
                    dictionary fields = {
                        asset[] assetPaths = [
                            @./fields_000.npz:SDF_FORMAT_ARGS:schema=None&mountPath=/SimResult@,
                            @./fields_001.npz:SDF_FORMAT_ARGS:schema=None&mountPath=/SimResult@,
                            @./fields_002.npz:SDF_FORMAT_ARGS:schema=None&mountPath=/SimResult@
                        ]
                        double2[] active = [(0, 0), (1, 1), (2, 2)]
                        double2[] times = [(0, 0), (1, 0), (2, 0)]
                        asset manifestAssetPath = @./fields_manifest.usda@
                        string primPath = "/SimResult"
                    }
                }
                clipSets = ["fields"]
            )
            {
                token omni:sci:array:temperature:device = "cpu"
            }
            """
        ),
        encoding="utf-8",
    )
    return root_path


@pytest.mark.integration
def test_npz_value_clip_stage_switches_files_by_stage_time(tmp_path: pathlib.Path):
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")

    root_path = _write_npz_value_clip_stage(tmp_path)
    stage = Usd.Stage.Open(str(root_path))
    dataset = stage.GetPrimAtPath("/SimResult")

    values = dataset.GetAttribute("omni:sci:array:temperature:value")
    assert values.GetTimeSamples() == [0.0, 1.0, 2.0]
    assert list(values.Get(Usd.TimeCode(0))) == [10.0, 10.5]
    assert list(values.Get(Usd.TimeCode(1))) == [20.0, 20.5]
    assert list(values.Get(Usd.TimeCode(2))) == [30.0, 30.5]

    device = dataset.GetAttribute("omni:sci:array:temperature:device")
    assert device.Get(Usd.TimeCode.EarliestTime()) == "cpu"
    assert device.GetTimeSamples() == []


@pytest.mark.integration
def test_value_clip_can_fixture_supplies_npz_field_time_samples():
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")
    if not _vtk_plugin_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")

    root_path = pathlib.Path(__file__).parent.parent.parent / "data" / "value_clip_can" / "root.usda"
    if not root_path.exists():
        pytest.skip("value_clip_can fixture not found")

    stage = Usd.Stage.Open(str(root_path))
    dataset = stage.GetPrimAtPath("/Can")

    expected_fields = {
        "cell_eqps": "element",
        "point_accl_x": "node",
        "point_accl_y": "node",
        "point_accl_z": "node",
        "point_displ_x": "node",
        "point_displ_y": "node",
        "point_displ_z": "node",
        "point_vel_x": "node",
        "point_vel_y": "node",
        "point_vel_z": "node",
    }
    schemas = set(dataset.GetAppliedSchemas())
    for name, association in expected_fields.items():
        assert f"OmniSciFieldAPI:{name}" in schemas
        assert f"OmniSciArrayAPI:{name}" in schemas
        assert dataset.GetAttribute(f"omni:sci:field:{name}:name").Get(Usd.TimeCode.EarliestTime()) == name
        assert dataset.GetAttribute(f"omni:sci:field:{name}:association").Get(Usd.TimeCode.EarliestTime()) == association
        value_attr = dataset.GetAttribute(f"omni:sci:array:{name}:value")
        assert value_attr
        assert str(value_attr.GetTypeName()) == "float[]"

    point_velocity = dataset.GetAttribute("omni:sci:array:point_vel_x:value")
    assert point_velocity.GetTimeSamples() == [0.0, 1.0, 2.0, 3.0]
    assert len(point_velocity.Get(Usd.TimeCode(0))) == 10088
    assert len(point_velocity.Get(Usd.TimeCode(3))) == 10088

    cell_eqps = dataset.GetAttribute("omni:sci:array:cell_eqps:value")
    assert len(cell_eqps.Get(Usd.TimeCode(0))) == 7152


@pytest.mark.integration
def test_none_schema_works_without_coordinates(tmp_path: pathlib.Path):
    """None schema must not require coordinate arrays -- no RuntimeError."""
    path = tmp_path / "scalars_only.npz"
    np.savez(path, pressure=np.array([1.0, 2.0, 3.0], dtype=np.float64))

    stage = Usd.Stage.Open(_none_identifier(path))
    assert stage
    dataset = stage.GetPrimAtPath("/scalars_only")
    assert dataset
    assert list(dataset.GetAttribute("omni:sci:array:pressure:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.0]


# ---------------------------------------------------------------------------
# schema=CGNS tests  (disk_out_ref.npz)
#   8499 nodes, 7472 HEXA_8 elements, element_connectivity shape (59776,)
#   scalar fields: ids, AsH3, CH4, GaMe3, H2, Pres, Temp
#   V is Nx3 -> registered as a 3-component vector field
# ---------------------------------------------------------------------------

_DISK_OUT_REF = pathlib.Path(__file__).parent.parent.parent / "data" / "disk_out_ref.npz"
_CGNS_ROOT = "/disk_out_ref"
_CGNS_BASE = f"{_CGNS_ROOT}/Base"
_CGNS_ZONE = f"{_CGNS_BASE}/Zone"
_CGNS_GRID_COORDINATES = f"{_CGNS_ZONE}/GridCoordinates"
_CGNS_FLOW_SOLUTION = f"{_CGNS_ZONE}/FlowSolution"
_CGNS_SECTION = f"{_CGNS_ZONE}/Section"


def _cgns_identifier(path: pathlib.Path, **kwargs) -> str:
    args = {"schema": "CGNS", "coordsArray": "coords"}
    args.update(kwargs)
    return Sdf.Layer.CreateIdentifier(str(path), args)


@pytest.fixture(scope="module")
def cgns_stage():
    if not _DISK_OUT_REF.exists():
        pytest.skip("disk_out_ref.npz not found")
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")
    return Usd.Stage.Open(_cgns_identifier(_DISK_OUT_REF))


@pytest.mark.integration
def test_cgns_root_scope(cgns_stage):
    root = cgns_stage.GetPrimAtPath(_CGNS_ROOT)
    assert root
    assert root.GetTypeName() == "Scope"
    assert cgns_stage.GetDefaultPrim().GetPath() == Sdf.Path(_CGNS_ROOT)


@pytest.mark.integration
def test_cgns_base_prim_type(cgns_stage):
    base = cgns_stage.GetPrimAtPath(_CGNS_BASE)
    assert base
    assert base.GetTypeName() == "CGNSBase"


@pytest.mark.integration
def test_cgns_zone_prim_type(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    assert zone
    assert zone.GetTypeName() == "CGNSZone"


@pytest.mark.integration
def test_cgns_zone_api_schemas(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    schemas = set(zone.GetAppliedSchemas())
    assert "OmniSciCgnsZoneAPI" in schemas


@pytest.mark.integration
def test_cgns_grid_coordinates_prim(cgns_stage):
    coords = cgns_stage.GetPrimAtPath(_CGNS_GRID_COORDINATES)
    assert coords
    assert coords.GetTypeName() == "CGNSGridCoordinates"
    schemas = set(coords.GetAppliedSchemas())
    assert "OmniSciCgnsGridCoordinatesAPI" in schemas
    assert "OmniSciArrayAPI:gridCoordinatesX" in schemas
    assert "OmniSciArrayAPI:gridCoordinatesY" in schemas
    assert "OmniSciArrayAPI:gridCoordinatesZ" in schemas


@pytest.mark.integration
def test_cgns_coordinate_values(cgns_stage):
    coords = cgns_stage.GetPrimAtPath(_CGNS_GRID_COORDINATES)
    # coords[0] = [0.0, -3.80999994, 10.15999985]
    xs = coords.GetAttribute("omni:sci:array:gridCoordinatesX:value").Get(Usd.TimeCode.EarliestTime())
    ys = coords.GetAttribute("omni:sci:array:gridCoordinatesY:value").Get(Usd.TimeCode.EarliestTime())
    zs = coords.GetAttribute("omni:sci:array:gridCoordinatesZ:value").Get(Usd.TimeCode.EarliestTime())
    assert len(xs) == 8499
    assert abs(xs[0]) < 1e-6
    assert abs(ys[0] - (-3.80999994)) < 1e-4
    assert abs(zs[0] - 10.15999985) < 1e-4


@pytest.mark.integration
def test_cgns_scalar_fields_on_flow_solution(cgns_stage):
    flow_solution = cgns_stage.GetPrimAtPath(_CGNS_FLOW_SOLUTION)
    assert flow_solution
    assert flow_solution.GetTypeName() == "CGNSFlowSolution"
    schemas = set(flow_solution.GetAppliedSchemas())
    assert "OmniSciCgnsFlowSolutionAPI" in schemas
    assert flow_solution.GetAttribute("omni:cgns:flow_solution:gridLocation").Get(Usd.TimeCode.EarliestTime()) == "Vertex"
    for field in ("Pres", "Temp", "H2", "AsH3", "CH4", "GaMe3"):
        inst = field  # identifier is the field name directly
        assert f"OmniSciFieldAPI:{inst}" in schemas, f"missing OmniSciFieldAPI:{inst}"
        assert f"OmniSciArrayAPI:{inst}" in schemas, f"missing OmniSciArrayAPI:{inst}"
        vals = flow_solution.GetAttribute(f"omni:sci:array:{inst}:value").Get(Usd.TimeCode.EarliestTime())
        assert vals is not None and len(vals) == 8499


@pytest.mark.integration
def test_cgns_vector_field_on_flow_solution(cgns_stage):
    """V is shape (8499, 3) -- registered as a 3-component vector field."""
    flow_solution = cgns_stage.GetPrimAtPath(_CGNS_FLOW_SOLUTION)
    schemas = set(flow_solution.GetAppliedSchemas())
    assert "OmniSciFieldAPI:V" in schemas
    assert "OmniSciArrayAPI:V" in schemas
    vals = flow_solution.GetAttribute("omni:sci:array:V:value").Get(Usd.TimeCode.EarliestTime())
    assert vals is not None and len(vals) == 8499
    assert len(vals[0]) == 3


@pytest.mark.integration
def test_cgns_section_prim(cgns_stage):
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    assert section
    assert section.GetTypeName() == "OmniSciDataset"
    schemas = set(section.GetAppliedSchemas())
    assert "OmniSciCgnsUnstructuredElementsAPI" in schemas
    assert "OmniSciArrayAPI:elementConnectivity" in schemas


@pytest.mark.integration
def test_cgns_element_type(cgns_stage):
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    assert section.GetAttribute("omni:cgns:unstructured_elements:elementType").Get(Usd.TimeCode.EarliestTime()) == "HEXA_8"


@pytest.mark.integration
def test_cgns_element_range(cgns_stage):
    from pxr import Gf
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    r = section.GetAttribute("omni:cgns:unstructured_elements:elementRange").Get(Usd.TimeCode.EarliestTime())
    assert r == Gf.Vec2i(1, 7472)


@pytest.mark.integration
def test_cgns_connectivity_values(cgns_stage):
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    conn = section.GetAttribute("omni:sci:array:elementConnectivity:value").Get(Usd.TimeCode.EarliestTime())
    assert len(conn) == 59776  # 7472 HEXA_8 x 8 nodes
    assert conn[0] == 143      # first node index from the file


@pytest.mark.integration
def test_cgns_zone_grid_coordinates_relationship(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    targets = zone.GetRelationship("omni:cgns:zone:gridCoordinates").GetTargets()
    assert targets == [Sdf.Path(_CGNS_GRID_COORDINATES)]


@pytest.mark.integration
def test_cgns_zone_flow_solutions_relationship(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    targets = zone.GetRelationship("omni:cgns:zone:flowSolutions").GetTargets()
    assert targets == [Sdf.Path(_CGNS_FLOW_SOLUTION)]


@pytest.mark.integration
def test_cgns_zone_sections_relationship(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    targets = zone.GetRelationship("omni:cgns:zone:sections").GetTargets()
    assert section.GetPath() in targets


@pytest.mark.integration
def test_cgns_section_zone_relationship(cgns_stage):
    zone = cgns_stage.GetPrimAtPath(_CGNS_ZONE)
    section = cgns_stage.GetPrimAtPath(_CGNS_SECTION)
    targets = section.GetRelationship("omni:cgns:unstructured_elements:zone").GetTargets()
    assert zone.GetPath() in targets


# ---------------------------------------------------------------------------
# mountPath -- combined structure and lazy data
# ---------------------------------------------------------------------------


@pytest.mark.integration
def test_mountpath_applies_to_combined_data(npz_path: pathlib.Path):
    if not _plugin_available():
        pytest.skip("OmniSciNpzFileFormat plugin not registered")

    layer_id = Sdf.Layer.CreateIdentifier(
        str(npz_path),
        {"schema": "Point Cloud", "mountPath": "/Mounted/Under/Here"},
    )
    stage = Usd.Stage.Open(layer_id)
    assert stage

    # Default prim is the topmost component of mountPath.
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Mounted")

    # Leaf carries the dataset prim with its API schemas applied.
    leaf = stage.GetPrimAtPath("/Mounted/Under/Here")
    assert leaf.IsValid()
    assert leaf.GetTypeName() == "OmniSciDataset"

    values = leaf.GetAttribute("omni:sci:array:pointsX:value").Get(Usd.TimeCode.EarliestTime())
    assert list(values) == [0.0, 3.0]

    npz_format = Sdf.FileFormat.FindById("OmniSciNpzFileFormat")
    npz_layers = [layer for layer in stage.GetUsedLayers() if layer.GetFileFormat() == npz_format]
    assert len(npz_layers) == 1
    assert npz_layers[0].GetFileFormatArguments().get("mountPath") == "/Mounted/Under/Here"
    assert npz_layers[0].subLayerPaths == []
