# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""NPZ schema reader: Point Cloud."""

import json

from pxr import OmniSci, OmniSciCae, Usd

from .utils import (
    array_type_name,
    dataset_name,
    dataset_path as make_dataset_path,
    make_dataset_prim,
    read_npz_metadata,
    register_array,
    resolve_coordinates,
    shaped_array_type_name,
    unique_identifier,
)


def read(stage: Usd.Stage | None, path: str, args: dict) -> list[dict]:
    """Populate *stage* from an NPZ file using the Point Cloud schema.

    Applies ``OmniSciCaePointCloudAPI`` to the dataset prim and registers
    the spatial coordinates as split ``OmniSciArrayAPI`` instances
    (``pointsX`` / ``pointsY`` / ``pointsZ``).  Every remaining 1-D scalar
    or Nx2/3/4 vector array with a supported dtype is registered as an
    ``OmniSciFieldAPI`` + ``OmniSciArrayAPI`` pair with
    ``association = "node"``.

    Coordinate arrays are resolved in the following order:

    1. Explicit ``coordsArrayX/Y/Z`` format args (split layout).
    2. Explicit ``coordsArray`` format arg (interleaved Nx3 layout).
    3. Auto-detection by common name (``coords``, ``points``, ``xyz``, ...).
    4. Arrays named ``x``, ``y``, ``z``.

    All array data is registered as lazy fields -- no array values are loaded
    at open time.

    Args:
        stage: In-memory ``Usd.Stage`` to populate.
        path: Resolved path to the ``.npz`` file.
        args: Format arguments dict from the layer identifier.

    Returns:
        list[dict]: Lazy-field manifest consumed by ``PythonFileFormatBase``.

    Raises:
        RuntimeError: If no coordinate arrays can be resolved, or if the
            coordinate array shape is not Nx>=3.
    """
    array_info = read_npz_metadata(path)
    names = list(array_info)
    coord_spec = resolve_coordinates(names, args)
    if coord_spec is None:
        raise RuntimeError(
            f"Could not determine coordinates for '{path}'. "
            "Provide coordsArray or coordsArrayX/Y/Z format args."
        )

    dataset_prim = None
    if stage is not None:
        dataset_prim, dataset_path = make_dataset_prim(stage, path, args)
        OmniSciCae.PointCloudAPI.Apply(dataset_prim)
    else:
        dataset_path = make_dataset_path(dataset_name(path, args), args)
    lazy_fields = []

    # --- register coordinate arrays ------------------------------------------
    if coord_spec["kind"] == "interleaved":
        coords_name = coord_spec["array"]
        coords_info = array_info[coords_name]
        if coords_info.ndim != 2 or coords_info.shape[1] < 3:
            raise RuntimeError(f"Coordinate array '{coords_name}' must be Nx3")

        coord_type = array_type_name(coords_info, prefer_float=True)
        for axis_name, column in (("pointsX", 0), ("pointsY", 1), ("pointsZ", 2)):
            if dataset_prim is not None:
                OmniSci.ArrayAPI.Apply(dataset_prim, axis_name)
            register_array(
                dataset_prim, dataset_path, axis_name, coord_type,
                json.dumps({"array": coords_name, "column": column, "preferFloat": True}),
                lazy_fields,
            )
    else:
        for axis_name, array_key in (
            ("pointsX", coord_spec["x"]),
            ("pointsY", coord_spec["y"]),
            ("pointsZ", coord_spec["z"]),
        ):
            coord_info = array_info[array_key]
            if dataset_prim is not None:
                OmniSci.ArrayAPI.Apply(dataset_prim, axis_name)
            register_array(
                dataset_prim, dataset_path, axis_name,
                array_type_name(coord_info, prefer_float=True),
                json.dumps({"array": array_key, "preferFloat": True}),
                lazy_fields,
            )

    # --- register scalar and vector field arrays -----------------------------
    reserved = (
        {coord_spec["array"]}
        if coord_spec["kind"] == "interleaved"
        else {coord_spec["x"], coord_spec["y"], coord_spec["z"]}
    )
    used_instances = {"pointsX", "pointsY", "pointsZ"}

    for name in names:
        if name in reserved:
            continue
        info = array_info[name]
        if info.ndim > 2:
            continue
        type_name, components = shaped_array_type_name(info)
        if type_name is None or (info.ndim == 2 and components is None):
            continue

        inst = unique_identifier(name, used_instances)
        if dataset_prim is not None:
            OmniSci.FieldAPI.Apply(dataset_prim, inst)
            OmniSci.ArrayAPI.Apply(dataset_prim, inst)
            OmniSci.FieldAPI(dataset_prim, inst).CreateNameAttr(name)
            OmniSci.FieldAPI(dataset_prim, inst).CreateAssociationAttr("node")
        token = {"array": name}
        if components is not None:
            token["components"] = components
        register_array(dataset_prim, dataset_path, inst, type_name, json.dumps(token), lazy_fields)

    return lazy_fields
