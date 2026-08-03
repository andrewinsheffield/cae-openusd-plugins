# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Shared utilities for cae_npz schema readers."""

import os
import zipfile
from dataclasses import dataclass
from typing import Iterable

import numpy as np
from pxr import OmniSci, Tf, Usd, Vt


@dataclass(frozen=True)
class NpzArrayInfo:
    """Metadata for one array inside an NPZ archive.

    The schema readers need names, shapes and dtypes to author USD structure,
    but they should not load the actual array payload during stage open.
    """

    name: str
    shape: tuple[int, ...]
    dtype: np.dtype

    @property
    def ndim(self) -> int:
        return len(self.shape)


def _array_name_from_zip_member(member_name: str) -> str | None:
    if not member_name.endswith(".npy"):
        return None
    if member_name.startswith("__MACOSX/"):
        return None
    return member_name[:-4]


def _read_npy_header(file_obj) -> tuple[tuple[int, ...], np.dtype]:
    version = np.lib.format.read_magic(file_obj)
    if version == (1, 0):
        shape, _fortran_order, dtype = np.lib.format.read_array_header_1_0(file_obj)
    elif version == (2, 0):
        shape, _fortran_order, dtype = np.lib.format.read_array_header_2_0(file_obj)
    elif version == (3, 0):
        shape, _fortran_order, dtype = np.lib.format.read_array_header_2_0(file_obj)
    else:
        raise RuntimeError(f"Unsupported NPY header version {version}")
    return tuple(shape), np.dtype(dtype)


def read_npz_metadata(path: str) -> dict[str, NpzArrayInfo]:
    """Read NPZ member headers without loading array payloads."""
    metadata: dict[str, NpzArrayInfo] = {}
    with zipfile.ZipFile(path) as archive:
        for member_name in archive.namelist():
            array_name = _array_name_from_zip_member(member_name)
            if array_name is None:
                continue
            with archive.open(member_name) as member:
                shape, dtype = _read_npy_header(member)
            metadata[array_name] = NpzArrayInfo(array_name, shape, dtype)

    if not metadata:
        raise RuntimeError(f"Unsupported NPZ payload in '{path}'")
    return metadata


def load_npz_array(path: str, name: str, allow_pickle: bool = False):
    with np.load(path, allow_pickle=allow_pickle) as np_file:
        if not hasattr(np_file, "files"):
            raise RuntimeError(f"Unsupported NPZ payload in '{path}'")
        return np_file[name]


def numpy_to_vt_array(array):
    """Convert a NumPy array to a USD VtArray without building Python lists."""
    arr = np.asarray(array)
    dtype = np.dtype(arr.dtype)
    vector_components = arr.shape[-1] if arr.ndim > 1 and arr.shape[-1] in (2, 3, 4) else None

    if vector_components is not None:
        arr = arr.reshape((-1, vector_components))
        if dtype.kind == "f":
            if dtype.itemsize <= 4:
                classes = {2: Vt.Vec2fArray, 3: Vt.Vec3fArray, 4: Vt.Vec4fArray}
                return classes[vector_components].FromNumpy(np.ascontiguousarray(arr, dtype=np.float32))
            classes = {2: Vt.Vec2dArray, 3: Vt.Vec3dArray, 4: Vt.Vec4dArray}
            return classes[vector_components].FromNumpy(np.ascontiguousarray(arr, dtype=np.float64))
        if dtype.kind in {"i", "u"} and dtype.itemsize <= 4:
            classes = {2: Vt.Vec2iArray, 3: Vt.Vec3iArray, 4: Vt.Vec4iArray}
            return classes[vector_components].FromNumpy(np.ascontiguousarray(arr, dtype=np.int32))

    arr = arr.reshape(-1)
    if dtype.kind == "f":
        if dtype.itemsize <= 4:
            return Vt.FloatArray.FromNumpy(np.ascontiguousarray(arr, dtype=np.float32))
        return Vt.DoubleArray.FromNumpy(np.ascontiguousarray(arr, dtype=np.float64))
    if dtype.kind in {"i", "u"}:
        if dtype.itemsize <= 4:
            return Vt.IntArray.FromNumpy(np.ascontiguousarray(arr, dtype=np.int32))
        return Vt.Int64Array.FromNumpy(np.ascontiguousarray(arr, dtype=np.int64))

    return arr.tolist()


def make_dataset_prim(stage: Usd.Stage, path: str, args: dict):
    """Populate *stage* with an OmniSciDataset prim at the layer root.

    The dataset prim is the layer's default prim directly: there is no
    ``/World`` wrapper, and the prim is always named after the input filename
    stem. Returns ``(dataset_prim, dataset_path)``.
    """
    d_name = dataset_name(path, args)
    d_path = dataset_path(d_name, args)

    dataset = OmniSci.Dataset.Define(stage, d_path)
    dataset_prim = dataset.GetPrim()
    stage.SetDefaultPrim(dataset_prim)

    return dataset_prim, d_path


def dataset_name(path: str, args: dict) -> str:
    return Tf.MakeValidIdentifier(os.path.splitext(os.path.basename(path))[0]) or "npz"


def dataset_path(d_name: str, args: dict) -> str:
    return f"/{d_name}"


def array_type_name(array_or_info, prefer_float: bool = False) -> str | None:
    """Return the USD type name string for *array_or_info*, or ``None`` if unsupported."""
    if prefer_float:
        return "double[]"
    dtype = getattr(array_or_info, "dtype", None)
    if dtype is None:
        dtype = np.asarray(array_or_info).dtype
    dtype = np.dtype(dtype)
    if dtype.kind == "f":
        return "float[]" if dtype.itemsize <= 4 else "double[]"
    if dtype.kind in {"i", "u"}:
        return "int[]" if dtype.itemsize <= 4 else "int64[]"
    return None


def shaped_array_type_name(array_or_info) -> tuple[str | None, int | None]:
    """Return ``(typeName, components)`` for scalar or vector-shaped arrays."""
    scalar_type = array_type_name(array_or_info)
    if scalar_type is None:
        return None, None

    shape = getattr(array_or_info, "shape", ())
    components = shape[-1] if len(shape) > 1 and shape[-1] in (2, 3, 4) else None
    if components is None:
        return scalar_type, None

    dtype = getattr(array_or_info, "dtype", None)
    if dtype is None:
        dtype = np.asarray(array_or_info).dtype
    dtype = np.dtype(dtype)
    if dtype.kind == "f":
        prefix = "float" if dtype.itemsize <= 4 else "double"
        return f"{prefix}{components}[]", components
    if dtype.kind in {"i", "u"} and dtype.itemsize <= 4:
        return f"int{components}[]", components

    return scalar_type, None


def unique_identifier(name: str, used: set[str]) -> str:
    """Return a USD-valid identifier derived from *name* that is not in *used*,
    then add it to *used*."""
    base = Tf.MakeValidIdentifier(name) or "field"
    candidate = base
    suffix = 1
    while candidate in used:
        suffix += 1
        candidate = f"{base}_{suffix}"
    used.add(candidate)
    return candidate


def register_array(dataset_prim, prim_path: str, instance: str,
                   type_name: str, token: str, lazy_fields: list) -> None:
    """Author the ``omni:sci:array:<instance>:device`` attribute on
    *dataset_prim* and append the lazy-field manifest entry for ``:value``."""
    if dataset_prim is not None:
        OmniSci.ArrayAPI(dataset_prim, instance).CreateDeviceAttr("cpu")
    lazy_fields.append(
        {
            "primPath": prim_path,
            "attrName": f"omni:sci:array:{instance}:value",
            "typeName": type_name,
            "token": token,
        }
    )


def resolve_coordinates(names: Iterable[str], args: dict) -> dict | None:
    """Determine which NPZ arrays carry spatial coordinates.

    Returns a dict with key ``"kind"`` equal to ``"interleaved"`` or
    ``"split"``, or ``None`` when no coordinates can be resolved.

    Resolution order:
    1. Explicit ``coordsArrayX/Y/Z`` format args (split layout).
    2. Explicit ``coordsArray`` format arg (interleaved Nx3 layout).
    3. Auto-detection by common name (``coords``, ``points``, ``xyz``, ...).
    4. Arrays named ``x``, ``y``, ``z``.
    5. Paired names ``coordsX/Y/Z``, ``pointsX/Y/Z``, ``gridCoordinatesX/Y/Z``.
    """
    if args.get("coordsArrayX") and args.get("coordsArrayY") and args.get("coordsArrayZ"):
        return {
            "kind": "split",
            "x": args["coordsArrayX"],
            "y": args["coordsArrayY"],
            "z": args["coordsArrayZ"],
        }

    if args.get("coordsArray"):
        return {"kind": "interleaved", "array": args["coordsArray"]}

    lowered = {name.lower(): name for name in names}
    for candidate in ("coords", "points", "coordinates", "xyz", "gridcoordinates"):
        if candidate in lowered:
            return {"kind": "interleaved", "array": lowered[candidate]}

    if all(key in lowered for key in ("x", "y", "z")):
        return {"kind": "split", "x": lowered["x"], "y": lowered["y"], "z": lowered["z"]}

    for x_key, y_key, z_key in (
        ("coordsx", "coordsy", "coordsz"),
        ("pointsx", "pointsy", "pointsz"),
        ("gridcoordinatesx", "gridcoordinatesy", "gridcoordinatesz"),
    ):
        if x_key in lowered and y_key in lowered and z_key in lowered:
            return {"kind": "split", "x": lowered[x_key], "y": lowered[y_key], "z": lowered[z_key]}

    return None
