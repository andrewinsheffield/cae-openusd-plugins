# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciVtk as OmniSciVtk

pytest.importorskip("pxr.OmniSciFileFormatArgs", reason="omniSciFileFormatArgs plugin not available")


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "VTK"
_POLY_ASCII = _DATA_DIR / "simple_poly_ascii.vtk"
_POLY_BINARY = _DATA_DIR / "simple_poly_bin.vtk"
_UNSTRUCTURED_ASCII = _DATA_DIR / "simple_unstructured_ascii.vtk"
_UNSTRUCTURED_BINARY_V5 = _DATA_DIR / "simple_unstructured_bin_v5.vtk"
_STRUCTURED_ASCII = _DATA_DIR / "simple_structured_ascii.vtk"
_STRUCTURED_POINTS_ASCII = _DATA_DIR / "simple_structured_points_ascii.vtk"
_STRUCTURED_POINTS_BINARY = _DATA_DIR / "simple_structured_points_bin.vtk"
_STRUCTURED_POINTS_STRINGS_ASCII = _DATA_DIR / "simple_structured_points_strings_ascii.vtk"
_STRUCTURED_POINTS_STRINGS_BINARY = _DATA_DIR / "simple_structured_points_strings_bin.vtk"
_RECTILINEAR_ASCII = _DATA_DIR / "simple_rectilinear1_ascii.vtk"
_TOPOLOGY_REGULAR_PACKED_V42 = _DATA_DIR / "topology_regular_packed_v42_ascii.vtk"
_TOPOLOGY_REGULAR_SPLIT_V51 = _DATA_DIR / "topology_regular_split_v51_ascii.vtk"
_TOPOLOGY_POLYHEDRON_PACKED_V42 = _DATA_DIR / "topology_polyhedron_packed_v42_ascii.vtk"
_TOPOLOGY_POLYHEDRON_SPLIT_V51 = _DATA_DIR / "topology_polyhedron_split_v51_ascii.vtk"
_VALUE_CLIP_CAN_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "value_clip_can"
_VALUE_CLIP_CAN_MOVING_ROOT = _VALUE_CLIP_CAN_DIR / "moving_points_root.usda"
_VALUE_CLIP_CAN_MOVING_STEP_1 = _VALUE_CLIP_CAN_DIR / "moving_points_001.vtk"

_REGULAR_CONNECTIVITY = [0, 1, 2, 3, 4, 5, 6, 7, 1, 8, 9, 10]
_REGULAR_PACKED_CELLS = [8, *_REGULAR_CONNECTIVITY[:8], 4, *_REGULAR_CONNECTIVITY[8:]]
_POLYHEDRON_FACE_STREAM = [
    6,
    4,
    0,
    3,
    2,
    1,
    4,
    4,
    5,
    6,
    7,
    4,
    0,
    1,
    5,
    4,
    4,
    1,
    2,
    6,
    5,
    4,
    2,
    3,
    7,
    6,
    4,
    3,
    0,
    4,
    7,
]
_POLYHEDRON_PACKED_CELLS = [
    len(_POLYHEDRON_FACE_STREAM),
    *_POLYHEDRON_FACE_STREAM,
    4,
    1,
    8,
    9,
    10,
]
_POLYHEDRON_SPLIT_CONNECTIVITY = [*_POLYHEDRON_FACE_STREAM, 1, 8, 9, 10]


def _vtk_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciVtkFileFormat") is not None


def _root_name(path: pathlib.Path) -> str:
    return path.stem.replace(".", "_")


def _vtk_structure_layers(stage: Usd.Stage):
    vtk_format = Sdf.FileFormat.FindById("OmniSciVtkFileFormat")
    return [
        layer
        for layer in stage.GetUsedLayers()
        if layer.GetFileFormat() == vtk_format
    ]


def _open_prim(path: pathlib.Path):
    if not path.exists():
        pytest.skip(f"VTK fixture not found: {path}")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(path))
    assert stage
    prim = stage.GetPrimAtPath(f"/{_root_name(path)}")
    assert prim
    return stage, prim


def _identifier(path: pathlib.Path, **kwargs) -> str:
    return Sdf.Layer.CreateIdentifier(str(path), kwargs)


@pytest.fixture(scope="module")
def poly_ascii_stage():
    if not _POLY_ASCII.exists():
        pytest.skip("VTK polydata fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_POLY_ASCII))
    assert stage
    return stage


@pytest.fixture(scope="module")
def poly_binary_stage():
    if not _POLY_BINARY.exists():
        pytest.skip("VTK binary polydata fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_POLY_BINARY))
    assert stage
    return stage


@pytest.fixture(scope="module")
def unstructured_ascii_stage():
    if not _UNSTRUCTURED_ASCII.exists():
        pytest.skip("VTK unstructured fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_UNSTRUCTURED_ASCII))
    assert stage
    return stage


@pytest.fixture(scope="module")
def unstructured_binary_stage():
    if not _UNSTRUCTURED_BINARY_V5.exists():
        pytest.skip("VTK binary unstructured fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_UNSTRUCTURED_BINARY_V5))
    assert stage
    return stage


@pytest.fixture(scope="module")
def structured_stage():
    if not _STRUCTURED_ASCII.exists():
        pytest.skip("VTK structured grid fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_STRUCTURED_ASCII))
    assert stage
    return stage


@pytest.fixture(scope="module")
def image_ascii_stage():
    if not _STRUCTURED_POINTS_ASCII.exists():
        pytest.skip("VTK structured points fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_STRUCTURED_POINTS_ASCII))
    assert stage
    return stage


@pytest.fixture(scope="module")
def image_binary_stage():
    if not _STRUCTURED_POINTS_BINARY.exists():
        pytest.skip("VTK binary structured points fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_STRUCTURED_POINTS_BINARY))
    assert stage
    return stage


@pytest.fixture(scope="module")
def image_strings_ascii_stage():
    if not _STRUCTURED_POINTS_STRINGS_ASCII.exists():
        pytest.skip("VTK structured points string fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_STRUCTURED_POINTS_STRINGS_ASCII))
    assert stage
    return stage


@pytest.fixture(scope="module")
def image_strings_binary_stage():
    if not _STRUCTURED_POINTS_STRINGS_BINARY.exists():
        pytest.skip("VTK binary structured points string fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_STRUCTURED_POINTS_STRINGS_BINARY))
    assert stage
    return stage


@pytest.fixture(scope="module")
def rectilinear_stage():
    if not _RECTILINEAR_ASCII.exists():
        pytest.skip("VTK rectilinear fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_RECTILINEAR_ASCII))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _vtk_available(), "OmniSciVtkFileFormat not registered"


@pytest.mark.integration
def test_payload_attribute_sugar_sets_dynamic_arguments():
    if not _POLY_ASCII.exists():
        pytest.skip("VTK polydata fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    from pxr import OmniSciFileFormatArgs

    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Vtk")
    streaming_api = OmniSciFileFormatArgs.StreamingAPI.Apply(prim)
    streaming_api.CreateIoThreadsAttr().Set(2)
    prim.GetPayloads().AddPayload(str(_POLY_ASCII))
    stage.Load(prim.GetPath())

    # The payload's default prim composes onto /Vtk directly; the layer's
    # default prim is the dataset rather than an additional wrapper.
    assert prim.HasAPI(OmniSciFileFormatArgs.StreamingAPI)

    layers = _vtk_structure_layers(stage)
    assert len(layers) == 1
    assert layers[0].GetFileFormatArguments().get("ioThreads") == "2"


@pytest.mark.integration
def test_default_prim_is_filename_stem(poly_ascii_stage):
    assert poly_ascii_stage.GetDefaultPrim().GetPath() == Sdf.Path(f"/{_root_name(_POLY_ASCII)}")
    assert not poly_ascii_stage.GetPrimAtPath("/World").IsValid()


@pytest.mark.integration
def test_polydata_ascii_topology(poly_ascii_stage):
    prim = poly_ascii_stage.GetPrimAtPath(f"/{_root_name(_POLY_ASCII)}")
    assert prim
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciVtk.PolyDataAPI)
    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    packed_polys = prim.GetAttribute("omni:sci:array:polysPackedConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) == 8
    assert list(packed_polys) == [
        4,
        0,
        1,
        2,
        3,
        4,
        4,
        5,
        6,
        7,
        4,
        0,
        1,
        5,
        4,
        4,
        2,
        3,
        7,
        6,
        4,
        0,
        4,
        7,
        3,
        4,
        1,
        2,
        6,
        5,
    ]
    assert not prim.GetAttribute("omni:sci:array:polysConnectivityOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polysConnectivityArray:value").IsValid()


@pytest.mark.integration
def test_polydata_binary_matches_ascii(poly_ascii_stage, poly_binary_stage):
    ascii_prim = poly_ascii_stage.GetPrimAtPath(f"/{_root_name(_POLY_ASCII)}")
    binary_prim = poly_binary_stage.GetPrimAtPath(f"/{_root_name(_POLY_BINARY)}")
    assert ascii_prim and binary_prim
    ascii_points = ascii_prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    binary_points = binary_prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    ascii_polys = ascii_prim.GetAttribute("omni:sci:array:polysPackedConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    binary_polys = binary_prim.GetAttribute("omni:sci:array:polysPackedConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    assert len(ascii_points) == len(binary_points)
    assert tuple(ascii_points[0]) == pytest.approx(tuple(binary_points[0]))
    assert tuple(ascii_points[-1]) == pytest.approx(tuple(binary_points[-1]))
    assert list(ascii_polys) == list(binary_polys)


@pytest.mark.integration
def test_values_are_authored_as_time_samples():
    if not _VALUE_CLIP_CAN_MOVING_STEP_1.exists():
        pytest.skip("value_clip_can moving VTK fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")

    stage = Usd.Stage.Open(_identifier(_VALUE_CLIP_CAN_MOVING_STEP_1, mountPath="/Can"))
    prim = stage.GetPrimAtPath("/Can")
    points = prim.GetAttribute("omni:sci:array:points:value")

    assert points.GetTimeSamples() == [0.0]
    values = points.Get(Usd.TimeCode(0))
    assert str(points.GetTypeName()) == "double3[]"
    assert len(values) == 10088
    assert tuple(values[0]) == pytest.approx((3.65343904495, -0.556061923504, -5.35889479518))
    assert prim.GetAttribute("omni:sci:array:points:device").Get() == "cpu"


@pytest.mark.integration
def test_value_clip_can_moving_points_fixture_supplies_time_varying_points():
    if not _VALUE_CLIP_CAN_MOVING_ROOT.exists():
        pytest.skip("value_clip_can moving VTK root fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")

    stage = Usd.Stage.Open(str(_VALUE_CLIP_CAN_MOVING_ROOT))
    prim = stage.GetPrimAtPath("/Can")

    points = prim.GetAttribute("omni:sci:array:points:value")
    assert points.GetTimeSamples() == [0.0, 1.0, 2.0, 3.0]
    points_t0 = points.Get(Usd.TimeCode(0))
    points_t3 = points.Get(Usd.TimeCode(3))
    assert len(points_t0) == 10088
    assert len(points_t3) == 10088
    assert tuple(points_t0[0]) != tuple(points_t3[0])

    connectivity = prim.GetAttribute("omni:sci:array:connectivityArray:value")
    assert connectivity.GetTimeSamples() == [0.0]
    assert len(connectivity.Get(Usd.TimeCode.EarliestTime())) == 57216
    assert prim.GetAttribute("omni:sci:array:points:device").Get(Usd.TimeCode.EarliestTime()) == "cpu"


@pytest.mark.integration
def test_structured_grid_extents_and_fields(structured_stage):
    prim = structured_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_ASCII)}")
    assert prim
    api = OmniSciVtk.StructuredGridAPI(prim)
    assert tuple(api.GetMaxExtentAttr().Get(Usd.TimeCode.EarliestTime())) == (2, 1, 0)
    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    pointvar = prim.GetAttribute("omni:sci:array:pointvar:value").Get(Usd.TimeCode.EarliestTime())
    cellvar = prim.GetAttribute("omni:sci:array:cellvar:value").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) == 6
    assert pointvar == pytest.approx([10.1, 20.1, 30.1, 40.1, 50.1, 60.1])
    assert cellvar == pytest.approx([100.1, 200.1])


@pytest.mark.integration
def test_image_data_ascii_metadata_and_scalar_field(image_ascii_stage):
    prim = image_ascii_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_POINTS_ASCII)}")
    assert prim
    api = OmniSciVtk.ImageDataAPI(prim)
    assert tuple(api.GetOriginAttr().Get(Usd.TimeCode.EarliestTime())) == pytest.approx((0.0, 0.0, 0.0))
    assert tuple(api.GetSpacingAttr().Get(Usd.TimeCode.EarliestTime())) == pytest.approx((1.0, 1.0, 1.0))
    assert tuple(api.GetMaxExtentAttr().Get(Usd.TimeCode.EarliestTime())) == (2, 3, 5)
    values = prim.GetAttribute("omni:sci:array:volume_scalars:value").Get(Usd.TimeCode.EarliestTime())
    assert values is not None
    assert len(values) == 72
    assert values[13] == 5


@pytest.mark.integration
def test_image_data_binary_matches_ascii(image_ascii_stage, image_binary_stage):
    ascii_prim = image_ascii_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_POINTS_ASCII)}")
    binary_prim = image_binary_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_POINTS_BINARY)}")
    ascii_values = ascii_prim.GetAttribute("omni:sci:array:volume_scalars:value").Get(Usd.TimeCode.EarliestTime())
    binary_values = binary_prim.GetAttribute("omni:sci:array:volume_scalars:value").Get(Usd.TimeCode.EarliestTime())
    assert list(ascii_values) == list(binary_values)


@pytest.mark.integration
def test_image_data_string_field_data_ascii(image_strings_ascii_stage):
    prim = image_strings_ascii_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_POINTS_STRINGS_ASCII)}")
    strings = prim.GetAttribute("omni:sci:array:Presidents:value").Get(Usd.TimeCode.EarliestTime())
    assert strings is None


@pytest.mark.integration
def test_image_data_string_field_data_binary(image_strings_binary_stage):
    prim = image_strings_binary_stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED_POINTS_STRINGS_BINARY)}")
    strings = prim.GetAttribute("omni:sci:array:Presidents:value").Get(Usd.TimeCode.EarliestTime())
    assert strings is None


@pytest.mark.integration
def test_rectilinear_grid_coordinates(rectilinear_stage):
    prim = rectilinear_stage.GetPrimAtPath(f"/{_root_name(_RECTILINEAR_ASCII)}")
    assert prim
    api = OmniSciVtk.RectilinearGridAPI(prim)
    assert tuple(api.GetMaxExtentAttr().Get(Usd.TimeCode.EarliestTime())) == (4, 4, 4)
    x = prim.GetAttribute("omni:sci:array:xCoordinates:value").Get(Usd.TimeCode.EarliestTime())
    y = prim.GetAttribute("omni:sci:array:yCoordinates:value").Get(Usd.TimeCode.EarliestTime())
    z = prim.GetAttribute("omni:sci:array:zCoordinates:value").Get(Usd.TimeCode.EarliestTime())
    assert list(x) == pytest.approx([-10.0, -5.0, 0.0, 5.0, 10.0])
    assert list(y) == pytest.approx([-10.0, -5.0, 0.0, 5.0, 10.0])
    assert list(z) == pytest.approx([-10.0, -5.0, 0.0, 5.0, 10.0])


@pytest.mark.integration
def test_vtk_regular_cells_packed_legacy_array():
    _stage, prim = _open_prim(_TOPOLOGY_REGULAR_PACKED_V42)
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    packed = prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())

    assert len(points) == 12
    assert list(packed) == _REGULAR_PACKED_CELLS
    assert list(cell_types) == [12, 10]
    assert not prim.GetAttribute("omni:sci:array:connectivityOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:connectivityArray:value").IsValid()


@pytest.mark.integration
def test_vtk_regular_cells_split_v51_arrays():
    _stage, prim = _open_prim(_TOPOLOGY_REGULAR_SPLIT_V51)
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    offsets = prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    connectivity = prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())

    assert list(offsets) == [0, 8, 12]
    assert list(connectivity) == _REGULAR_CONNECTIVITY
    assert list(cell_types) == [12, 10]
    assert not prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").IsValid()


@pytest.mark.integration
def test_vtk_polyhedron_cells_packed_legacy_array():
    _stage, prim = _open_prim(_TOPOLOGY_POLYHEDRON_PACKED_V42)
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    packed = prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())

    assert list(packed) == _POLYHEDRON_PACKED_CELLS
    assert list(cell_types) == [42, 10]
    assert not prim.GetAttribute("omni:sci:array:connectivityOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:connectivityArray:value").IsValid()


@pytest.mark.integration
def test_vtk_polyhedron_cells_split_v51_arrays():
    _stage, prim = _open_prim(_TOPOLOGY_POLYHEDRON_SPLIT_V51)
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    offsets = prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    connectivity = prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())

    assert list(offsets) == [0, 31, 35]
    assert list(connectivity) == _POLYHEDRON_SPLIT_CONNECTIVITY
    assert list(cell_types) == [42, 10]
    assert not prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").IsValid()


@pytest.mark.integration
def test_unstructured_grid_ascii_legacy_cells(unstructured_ascii_stage):
    prim = unstructured_ascii_stage.GetPrimAtPath(f"/{_root_name(_UNSTRUCTURED_ASCII)}")
    assert prim
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)
    packed_connectivity = prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())
    scalars = prim.GetAttribute("omni:sci:array:scalars:value").Get(Usd.TimeCode.EarliestTime())
    vectors = prim.GetAttribute("omni:sci:array:vectors:value").Get(Usd.TimeCode.EarliestTime())
    assert packed_connectivity is not None and len(packed_connectivity) == 64
    assert list(packed_connectivity[:9]) == [8, 0, 1, 4, 3, 6, 7, 10, 9]
    assert not prim.GetAttribute("omni:sci:array:connectivityOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:connectivityArray:value").IsValid()
    assert list(cell_types) == [12, 13, 13, 14, 10, 10, 6, 9, 5, 5, 3, 1]
    assert scalars is not None and len(scalars) == 26
    assert vectors is not None and len(vectors) == 26


@pytest.mark.integration
def test_unstructured_grid_binary_v5(unstructured_binary_stage):
    prim = unstructured_binary_stage.GetPrimAtPath(f"/{_root_name(_UNSTRUCTURED_BINARY_V5)}")
    assert prim
    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    offsets = prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    connectivity = prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())
    assert points is not None and len(points) == 26
    assert offsets is not None and len(offsets) == 13
    assert connectivity is not None and len(connectivity) == 52
    assert list(cell_types) == [12, 13, 13, 14, 10, 10, 6, 9, 5, 5, 3, 1]


@pytest.mark.integration
def test_binary_parallel_args_match_default():
    if not _UNSTRUCTURED_BINARY_V5.exists():
        pytest.skip("VTK binary unstructured fixture not found")
    if not _vtk_available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")

    default_stage = Usd.Stage.Open(str(_UNSTRUCTURED_BINARY_V5))
    threaded_id = Sdf.Layer.CreateIdentifier(str(_UNSTRUCTURED_BINARY_V5), {"ioThreads": "4"})
    threaded_stage = Usd.Stage.Open(threaded_id)
    default_prim = default_stage.GetPrimAtPath(f"/{_root_name(_UNSTRUCTURED_BINARY_V5)}")
    threaded_prim = threaded_stage.GetPrimAtPath(f"/{_root_name(_UNSTRUCTURED_BINARY_V5)}")
    default_points = default_prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    threaded_points = threaded_prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    default_offsets = default_prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    threaded_offsets = threaded_prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    default_connectivity = default_prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    threaded_connectivity = threaded_prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    assert list(default_offsets) == list(threaded_offsets)
    assert len(default_points) == len(threaded_points)
    assert tuple(default_points[0]) == pytest.approx(tuple(threaded_points[0]))
    assert tuple(default_points[-1]) == pytest.approx(tuple(threaded_points[-1]))
    assert list(default_connectivity) == list(threaded_connectivity)
