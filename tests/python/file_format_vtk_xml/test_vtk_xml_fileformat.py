# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import base64
import pathlib
import struct

import pytest
from pxr import Sdf, Usd

import pxr.OmniSci as OmniSci
import pxr.OmniSciVtk as OmniSciVtk


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "VTKXml"
_IMAGE_ASCII = _DATA_DIR / "simple_image_ascii.vti"
_IMAGE_BINARY = _DATA_DIR / "simple_image_binary.vti"
_IMAGE_APPENDED_BASE64 = _DATA_DIR / "simple_image_appended_base64.vti"
_RECT = _DATA_DIR / "simple_rect_ascii.vtr"
_STRUCTURED = _DATA_DIR / "simple_structured_ascii.vts"
_POLY_ASCII = _DATA_DIR / "simple_poly_ascii.vtp"
_POLY_BINARY = _DATA_DIR / "simple_poly_binary.vtp"
_UNSTRUCTURED = _DATA_DIR / "simple_unstructured_ascii.vtu"
_POLYHEDRA = _DATA_DIR / "multicomb_0_polyhedra.vtu"
_TOPOLOGY_REGULAR_SPLIT = _DATA_DIR / "topology_regular_split_ascii.vtu"
_TOPOLOGY_POLYHEDRON_SPLIT = _DATA_DIR / "topology_polyhedron_split_ascii.vtu"
_TOPOLOGY_POLYHEDRON_PACKED = _DATA_DIR / "topology_polyhedron_packed_ascii.vtu"

_REGULAR_CONNECTIVITY = [0, 1, 2, 3, 4, 5, 6, 7, 1, 8, 9, 10]
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
_POLYHEDRON_FACE_CONNECTIVITY = [
    0,
    3,
    2,
    1,
    4,
    5,
    6,
    7,
    0,
    1,
    5,
    4,
    1,
    2,
    6,
    5,
    2,
    3,
    7,
    6,
    3,
    0,
    4,
    7,
]
_RAW_PACKED_POLYHEDRON_FACE_STREAM = [
    6,
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
    3,
    7,
    4,
    4,
    1,
    2,
    6,
    5,
]


def _available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciVtkFileFormat") is not None


def _root_name(path: pathlib.Path) -> str:
    return path.stem.replace(".", "_")


def _open(path: pathlib.Path):
    if not path.exists():
        pytest.skip(f"VTK XML fixture not found: {path}")
    if not _available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(path))
    assert stage
    return stage


def _open_with_args(path: pathlib.Path, args: dict[str, str]):
    if not _available():
        pytest.skip("OmniSciVtkFileFormat plugin not registered")
    stage = Usd.Stage.Open(Sdf.Layer.CreateIdentifier(str(path), args))
    assert stage
    return stage


def _vtk_block(payload: bytes) -> bytes:
    return struct.pack("<I", len(payload)) + payload


def _vtk_base64_block(payload: bytes) -> bytes:
    return base64.b64encode(_vtk_block(payload))


def _write_base64_appended_encoded_offsets_image(path: pathlib.Path):
    density = struct.pack("<2f", 1.0, 2.0)
    temperature = struct.pack("<2f", 300.0, 350.0)

    blocks = [_vtk_base64_block(density), _vtk_base64_block(temperature)]
    density_offset = 0
    temperature_offset = len(blocks[0])

    prefix = f"""<?xml version="1.0"?>
<VTKFile type="ImageData" version="1.0" byte_order="LittleEndian" header_type="UInt32">
  <ImageData WholeExtent="0 1 0 0 0 0" Origin="0 0 0" Spacing="1 1 1">
    <Piece Extent="0 1 0 0 0 0">
      <PointData>
        <DataArray type="Float32" Name="density" format="appended" offset="{density_offset}"/>
        <DataArray type="Float32" Name="temperature" format="appended" offset="{temperature_offset}"/>
      </PointData>
      <CellData/>
    </Piece>
  </ImageData>
  <AppendedData encoding="base64">
_""".encode()
    suffix = b"\n  </AppendedData>\n</VTKFile>\n"
    path.write_bytes(prefix + b"".join(blocks) + suffix)


def _write_raw_appended_polydata(path: pathlib.Path):
    points = struct.pack(
        "<12f",
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        1.0,
        1.0,
        0.0,
        0.0,
        1.0,
        0.0,
    )
    connectivity = struct.pack("<4q", 0, 1, 2, 3)
    offsets = struct.pack("<q", 4)

    blocks = [_vtk_block(points), _vtk_block(connectivity), _vtk_block(offsets)]
    point_offset = 0
    connectivity_offset = len(blocks[0])
    offsets_offset = connectivity_offset + len(blocks[1])

    prefix = f"""<?xml version="1.0"?>
<VTKFile type="PolyData" version="1.0" byte_order="LittleEndian" header_type="UInt32">
  <PolyData>
    <Piece NumberOfPoints="4" NumberOfPolys="1">
      <PointData/>
      <CellData/>
      <Points>
        <DataArray type="Float32" NumberOfComponents="3" format="appended" offset="{point_offset}"/>
      </Points>
      <Polys>
        <DataArray type="Int64" Name="connectivity" format="appended" offset="{connectivity_offset}"/>
        <DataArray type="Int64" Name="offsets" format="appended" offset="{offsets_offset}"/>
      </Polys>
    </Piece>
  </PolyData>
  <AppendedData encoding="raw">
_""".encode()
    suffix = b"\n  </AppendedData>\n</VTKFile>\n"
    path.write_bytes(prefix + b"".join(blocks) + suffix)


def _write_raw_appended_packed_polyhedron(path: pathlib.Path):
    points = struct.pack(
        "<24f",
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        1.0,
        1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        1.0,
        1.0,
        0.0,
        1.0,
        1.0,
        1.0,
        1.0,
        0.0,
        1.0,
        1.0,
    )
    connectivity = struct.pack("<8q", 0, 1, 2, 3, 4, 5, 6, 7)
    offsets = struct.pack("<q", 8)
    types = struct.pack("<B", 42)
    faces_values = _RAW_PACKED_POLYHEDRON_FACE_STREAM
    faces = struct.pack("<31q", *faces_values)
    faceoffsets = struct.pack("<q", len(faces_values))

    blocks = [
        _vtk_block(points),
        _vtk_block(connectivity),
        _vtk_block(offsets),
        _vtk_block(types),
        _vtk_block(faces),
        _vtk_block(faceoffsets),
    ]
    point_offset = 0
    connectivity_offset = point_offset + len(blocks[0])
    offsets_offset = connectivity_offset + len(blocks[1])
    types_offset = offsets_offset + len(blocks[2])
    faces_offset = types_offset + len(blocks[3])
    faceoffsets_offset = faces_offset + len(blocks[4])

    prefix = f"""<?xml version="1.0"?>
<VTKFile type="UnstructuredGrid" version="1.0" byte_order="LittleEndian" header_type="UInt32">
  <UnstructuredGrid>
    <Piece NumberOfPoints="8" NumberOfCells="1">
      <PointData/>
      <CellData/>
      <Points>
        <DataArray type="Float32" NumberOfComponents="3" format="appended" offset="{point_offset}"/>
      </Points>
      <Cells>
        <DataArray type="Int64" Name="connectivity" format="appended" offset="{connectivity_offset}"/>
        <DataArray type="Int64" Name="offsets" format="appended" offset="{offsets_offset}"/>
        <DataArray type="UInt8" Name="types" format="appended" offset="{types_offset}"/>
        <DataArray type="Int64" Name="faces" format="appended" offset="{faces_offset}"/>
        <DataArray type="Int64" Name="faceoffsets" format="appended" offset="{faceoffsets_offset}"/>
      </Cells>
    </Piece>
  </UnstructuredGrid>
  <AppendedData encoding="raw">
_""".encode()
    suffix = b"\n  </AppendedData>\n</VTKFile>\n"
    path.write_bytes(prefix + b"".join(blocks) + suffix)


@pytest.mark.integration
def test_plugin_registered():
    assert _available()


@pytest.mark.integration
def test_image_ascii_and_binary():
    ascii_stage = _open(_IMAGE_ASCII)
    ascii_prim = ascii_stage.GetPrimAtPath(f"/{_root_name(_IMAGE_ASCII)}")
    assert ascii_prim.HasAPI(OmniSciVtk.ImageDataAPI)
    assert list(ascii_prim.GetAttribute("omni:sci:array:density:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.0, 4.0]

    binary_stage = _open(_IMAGE_BINARY)
    binary_prim = binary_stage.GetPrimAtPath(f"/{_root_name(_IMAGE_BINARY)}")
    assert binary_prim.HasAPI(OmniSciVtk.ImageDataAPI)
    assert list(binary_prim.GetAttribute("omni:sci:array:density:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.0, 4.0]

    appended_stage = _open(_IMAGE_APPENDED_BASE64)
    appended_prim = appended_stage.GetPrimAtPath(f"/{_root_name(_IMAGE_APPENDED_BASE64)}")
    assert appended_prim.HasAPI(OmniSciVtk.ImageDataAPI)
    assert list(appended_prim.GetAttribute("omni:sci:array:density:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.0, 4.0]


@pytest.mark.integration
def test_image_appended_base64_uses_encoded_offsets(tmp_path):
    path = tmp_path / "image_base64_encoded_offsets.vti"
    _write_base64_appended_encoded_offsets_image(path)

    stage = _open(path)
    prim = stage.GetPrimAtPath(f"/{_root_name(path)}")
    assert prim.HasAPI(OmniSciVtk.ImageDataAPI)
    assert list(prim.GetAttribute("omni:sci:array:density:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0]
    assert list(prim.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())) == [300.0, 350.0]


@pytest.mark.integration
def test_rectilinear_grid_arrays():
    stage = _open(_RECT)
    prim = stage.GetPrimAtPath(f"/{_root_name(_RECT)}")
    assert prim.IsA(OmniSci.Dataset)
    assert prim.HasAPI(OmniSciVtk.RectilinearGridAPI)
    assert list(prim.GetAttribute("omni:sci:array:xCoordinates:value").Get(Usd.TimeCode.EarliestTime())) == [0.0, 1.0]
    assert list(prim.GetAttribute("omni:sci:array:yCoordinates:value").Get(Usd.TimeCode.EarliestTime())) == [0.0, 2.0]
    assert list(prim.GetAttribute("omni:sci:array:zCoordinates:value").Get(Usd.TimeCode.EarliestTime())) == [0.0]


@pytest.mark.integration
def test_structured_grid_points_and_field():
    stage = _open(_STRUCTURED)
    prim = stage.GetPrimAtPath(f"/{_root_name(_STRUCTURED)}")
    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    values = prim.GetAttribute("omni:sci:array:temperature:value").Get(Usd.TimeCode.EarliestTime())
    assert prim.HasAPI(OmniSciVtk.StructuredGridAPI)
    assert len(points) == 4
    assert tuple(points[2]) == pytest.approx((0.0, 1.0, 0.0))
    assert list(values) == [10.0, 20.0, 30.0, 40.0]


@pytest.mark.integration
def test_polydata_ascii_and_binary():
    ascii_stage = _open(_POLY_ASCII)
    ascii_prim = ascii_stage.GetPrimAtPath(f"/{_root_name(_POLY_ASCII)}")
    assert ascii_prim.HasAPI(OmniSciVtk.PolyDataAPI)
    assert list(ascii_prim.GetAttribute("omni:sci:array:polysConnectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 4]
    assert list(ascii_prim.GetAttribute("omni:sci:array:polysConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == [0, 1, 2, 3]

    binary_stage = _open(_POLY_BINARY)
    binary_prim = binary_stage.GetPrimAtPath(f"/{_root_name(_POLY_BINARY)}")
    assert binary_prim.HasAPI(OmniSciVtk.PolyDataAPI)
    assert list(binary_prim.GetAttribute("omni:sci:array:polysConnectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 4]
    assert list(binary_prim.GetAttribute("omni:sci:array:polysConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == [0, 1, 2, 3]


@pytest.mark.integration
def test_polydata_raw_appended_uncompressed(tmp_path):
    path = tmp_path / "simple_poly_raw_appended.vtp"
    _write_raw_appended_polydata(path)

    stage = _open_with_args(path, {"ioThreads": "16"})
    prim = stage.GetPrimAtPath(f"/{_root_name(path)}")
    assert prim.HasAPI(OmniSciVtk.PolyDataAPI)

    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    assert len(points) == 4
    assert tuple(points[2]) == pytest.approx((1.0, 1.0, 0.0))
    assert list(prim.GetAttribute("omni:sci:array:polysConnectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 4]
    assert list(prim.GetAttribute("omni:sci:array:polysConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == [0, 1, 2, 3]


@pytest.mark.integration
def test_unstructured_grid():
    stage = _open(_UNSTRUCTURED)
    prim = stage.GetPrimAtPath(f"/{_root_name(_UNSTRUCTURED)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)
    assert list(prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 4]
    assert list(prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == [0, 1, 2, 3]
    assert list(prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())) == [9]
    assert list(prim.GetAttribute("omni:sci:array:pval:value").Get(Usd.TimeCode.EarliestTime())) == [1.0, 2.0, 3.0, 4.0]
    assert list(prim.GetAttribute("omni:sci:array:cval:value").Get(Usd.TimeCode.EarliestTime())) == [9.0]


@pytest.mark.integration
def test_vtu_regular_cells_split_arrays():
    stage = _open(_TOPOLOGY_REGULAR_SPLIT)
    prim = stage.GetPrimAtPath(f"/{_root_name(_TOPOLOGY_REGULAR_SPLIT)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    assert list(prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 8, 12]
    assert list(prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == _REGULAR_CONNECTIVITY
    assert list(prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())) == [12, 10]
    assert not prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").IsValid()


@pytest.mark.integration
def test_vtu_polyhedron_split_arrays():
    stage = _open(_TOPOLOGY_POLYHEDRON_SPLIT)
    prim = stage.GetPrimAtPath(f"/{_root_name(_TOPOLOGY_POLYHEDRON_SPLIT)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    # The Points DataArray carries an <InformationKey> child after its numeric
    # data; materializing it confirms the inline-ASCII scan stops at the nested
    # element rather than counting the InformationKey values as point data.
    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    assert len(points) == 12
    assert tuple(points[0]) == pytest.approx((0.0, 0.0, 0.0))
    assert tuple(points[11]) == pytest.approx((2.0, 1.0, 1.0))

    assert list(prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 8, 12]
    assert list(prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == _REGULAR_CONNECTIVITY
    assert list(prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())) == [42, 10]
    assert list(prim.GetAttribute("omni:sci:array:polyhedronFacesOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [
        0,
        4,
        8,
        12,
        16,
        20,
        24,
    ]
    assert (
        list(prim.GetAttribute("omni:sci:array:polyhedronFacesConnectivityArray:value").Get(Usd.TimeCode.EarliestTime()))
        == _POLYHEDRON_FACE_CONNECTIVITY
    )
    assert list(prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 6, 6]
    assert list(prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == [
        0,
        1,
        2,
        3,
        4,
        5,
    ]
    assert not prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronPackedFaceOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronPackedFaces:value").IsValid()


@pytest.mark.integration
def test_vtu_polyhedron_packed_arrays():
    stage = _open(_TOPOLOGY_POLYHEDRON_PACKED)
    prim = stage.GetPrimAtPath(f"/{_root_name(_TOPOLOGY_POLYHEDRON_PACKED)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    assert list(prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 8, 12]
    assert list(prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == _REGULAR_CONNECTIVITY
    assert list(prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())) == [42, 10]
    assert list(prim.GetAttribute("omni:sci:array:polyhedronPackedFaceOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [31, -1]
    assert list(prim.GetAttribute("omni:sci:array:polyhedronPackedFaces:value").Get(Usd.TimeCode.EarliestTime())) == _POLYHEDRON_FACE_STREAM
    assert not prim.GetAttribute("omni:sci:array:connectivityPackedArray:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronFacesOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronFacesConnectivityArray:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsConnectivityArray:value").IsValid()


@pytest.mark.integration
def test_unstructured_grid_packed_polyhedron_raw_appended(tmp_path):
    path = tmp_path / "packed_polyhedron_raw_appended.vtu"
    _write_raw_appended_packed_polyhedron(path)

    stage = _open(path)
    prim = stage.GetPrimAtPath(f"/{_root_name(path)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    assert list(prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [0, 8]
    assert list(prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())) == list(range(8))
    assert list(prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())) == [42]
    assert list(prim.GetAttribute("omni:sci:array:polyhedronPackedFaceOffsets:value").Get(Usd.TimeCode.EarliestTime())) == [31]
    assert (
        list(prim.GetAttribute("omni:sci:array:polyhedronPackedFaces:value").Get(Usd.TimeCode.EarliestTime()))
        == _RAW_PACKED_POLYHEDRON_FACE_STREAM
    )
    assert not prim.GetAttribute("omni:sci:array:polyhedronFacesOffsets:value").IsValid()
    assert not prim.GetAttribute("omni:sci:array:polyhedronFacesConnectivityArray:value").IsValid()


@pytest.mark.integration
def test_unstructured_grid_polyhedra_appended_lz4():
    stage = _open(_POLYHEDRA)
    prim = stage.GetPrimAtPath(f"/{_root_name(_POLYHEDRA)}")
    assert prim.HasAPI(OmniSciVtk.UnstructuredGridAPI)

    points = prim.GetAttribute("omni:sci:array:points:value").Get(Usd.TimeCode.EarliestTime())
    offsets = prim.GetAttribute("omni:sci:array:connectivityOffsets:value").Get(Usd.TimeCode.EarliestTime())
    conn = prim.GetAttribute("omni:sci:array:connectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    cell_types = prim.GetAttribute("omni:sci:array:cellTypes:value").Get(Usd.TimeCode.EarliestTime())
    face_offsets = prim.GetAttribute("omni:sci:array:polyhedronFacesOffsets:value").Get(Usd.TimeCode.EarliestTime())
    face_conn = prim.GetAttribute("omni:sci:array:polyhedronFacesConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    poly_offsets = prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsOffsets:value").Get(Usd.TimeCode.EarliestTime())
    poly_conn = prim.GetAttribute("omni:sci:array:polyhedronFaceLocationsConnectivityArray:value").Get(Usd.TimeCode.EarliestTime())
    density = prim.GetAttribute("omni:sci:array:Density:value").Get(Usd.TimeCode.EarliestTime())

    assert len(points) == 13200
    assert len(offsets) == 11521
    assert offsets[0] == 0
    assert len(conn) > 0
    assert len(cell_types) == 11520
    assert len(face_offsets) > 1
    assert face_offsets[0] == 0
    assert len(face_conn) > 0
    assert len(poly_offsets) > 1
    assert poly_offsets[0] == 0
    assert len(poly_conn) > 0
    assert len(density) == 13200
