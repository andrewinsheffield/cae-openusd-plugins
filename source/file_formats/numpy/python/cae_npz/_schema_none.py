# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""NPZ schema reader: None (raw arrays, no field metadata)."""

import json

from pxr import OmniSci, Usd

from .utils import (
    array_type_name,
    dataset_name,
    dataset_path as make_dataset_path,
    make_dataset_prim,
    read_npz_metadata,
    register_array,
    unique_identifier,
)


def read(stage: Usd.Stage | None, path: str, args: dict) -> list[dict]:
    """Populate *stage* from an NPZ file with no geometry schema.

    Registers all 1-D arrays with a supported dtype as ``OmniSciArrayAPI``
    instances.  No geometry marker schema (``OmniSciCaePointCloudAPI``,
    ``OmniSciCaeMeshAPI``, etc.) is applied and no ``OmniSciFieldAPI``
    entries are created -- the dataset prim carries only raw array data.

    Coordinate arrays are not required and are treated as ordinary arrays.
    Multi-dimensional arrays are silently skipped.

    Args:
        stage: In-memory ``Usd.Stage`` to populate.
        path: Resolved path to the ``.npz`` file.
        args: Format arguments dict from the layer identifier.

    Returns:
        list[dict]: Lazy-field manifest consumed by ``PythonFileFormatBase``.
    """
    array_info = read_npz_metadata(path)
    dataset_prim = None
    if stage is not None:
        dataset_prim, dataset_path = make_dataset_prim(stage, path, args)
    else:
        dataset_path = make_dataset_path(dataset_name(path, args), args)
    lazy_fields = []
    used_instances: set[str] = set()

    for name, info in array_info.items():
        if info.ndim != 1:
            continue
        type_name = array_type_name(info)
        if type_name is None:
            continue

        inst = unique_identifier(name, used_instances)
        if dataset_prim is not None:
            OmniSci.ArrayAPI.Apply(dataset_prim, inst)
        register_array(dataset_prim, dataset_path, inst, type_name, json.dumps({"array": name}), lazy_fields)

    return lazy_fields
