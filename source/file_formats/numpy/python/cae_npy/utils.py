# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Shared utilities for the cae_npy reader."""

import os
from dataclasses import dataclass

import numpy as np
from pxr import OmniSci, Tf, Usd, Vt


@dataclass(frozen=True)
class NpyArrayInfo:
    name: str
    shape: tuple[int, ...]
    dtype: np.dtype


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


def read_npy_metadata(path: str, name: str = "array") -> NpyArrayInfo:
    """Read an NPY header without loading the array payload."""
    with open(path, "rb") as npy_file:
        shape, dtype = _read_npy_header(npy_file)
    return NpyArrayInfo(name, shape, dtype)


def load_npy_array(path: str, allow_pickle: bool = False):
    return np.load(path, allow_pickle=allow_pickle)


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
    d_name = dataset_name(path, args)
    d_path = dataset_path(d_name, args)

    dataset = OmniSci.Dataset.Define(stage, d_path)
    dataset_prim = dataset.GetPrim()
    stage.SetDefaultPrim(dataset_prim)

    return dataset_prim, d_path


def dataset_name(path: str, args: dict) -> str:
    return Tf.MakeValidIdentifier(os.path.splitext(os.path.basename(path))[0]) or "npy"


def dataset_path(d_name: str, args: dict) -> str:
    return f"/{d_name}"


def array_type_name(array_or_info) -> str | None:
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
    """Return ``(typeName, components)`` for a raw NumPy array."""
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
    base = Tf.MakeValidIdentifier(name) or "array"
    candidate = base
    suffix = 1
    while candidate in used:
        suffix += 1
        candidate = f"{base}_{suffix}"
    used.add(candidate)
    return candidate


def register_array(dataset_prim, prim_path: str, instance: str,
                   type_name: str, token: str, lazy_fields: list) -> None:
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
