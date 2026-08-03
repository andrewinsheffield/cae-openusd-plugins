# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""NPZ schema reader: CGNS (single zone, single element section).

Prim layout
-----------
::

    /<filename-stem>                Scope (default prim)
    /<filename-stem>/Base           CGNSBase
    /<filename-stem>/Base/Zone      CGNSZone + OmniSciCgnsZoneAPI
        omni:cgns:zone:gridCoordinates -> [.../GridCoordinates]
        omni:cgns:zone:flowSolutions -> [.../FlowSolution]
        omni:cgns:zone:sections -> [.../Section]

    /<filename-stem>/Base/Zone/GridCoordinates
        CGNSGridCoordinates + OmniSciCgnsGridCoordinatesAPI
        OmniSciArrayAPI:gridCoordinatesX/Y/Z

    /<filename-stem>/Base/Zone/FlowSolution
        CGNSFlowSolution + OmniSciCgnsFlowSolutionAPI
        OmniSciFieldAPI:<name> + OmniSciArrayAPI:<name>  (scalar fields)

    /<filename-stem>/Base/Zone/Section
        OmniSciDataset + OmniSciCgnsUnstructuredElementsAPI
        omni:cgns:unstructured_elements:elementType
        omni:cgns:unstructured_elements:elementRange
        omni:cgns:unstructured_elements:elementSizeBoundary = 0
        omni:cgns:unstructured_elements:zone -> /<filename-stem>/Base/Zone
        OmniSciArrayAPI:elementConnectivity    (lazy)
        OmniSciArrayAPI:elementStartOffset     (lazy, optional)

Array role detection (case-insensitive)
---------------------------------------
- ``element_type`` / ``elementtype``           -> elementType metadata (shape [1])
- ``element_range`` / ``elementrange``         -> elementRange metadata (shape [2])
- ``elementconnectivity`` / ``connectivity`` / ``conn`` / ``con``  -> connectivity
- ``elementstartoffset`` / ``startoffsets`` / ``startoffset``      -> startOffset
- Coordinate arrays: resolved via ``utils.resolve_coordinates``
- Everything else (1-D or Nx2/3/4, supported dtype) -> field on FlowSolution prim
"""

import json

from pxr import Gf, OmniSci, OmniSciCgns, Tf, Usd

from .utils import (
    array_type_name,
    dataset_name,
    dataset_path as make_dataset_path,
    load_npz_array,
    read_npz_metadata,
    register_array,
    resolve_coordinates,
    shaped_array_type_name,
    unique_identifier,
)

# Maps CGNS integer element-type codes (as stored in NPZ) to schema token strings.
_ELEMENT_TYPE_TOKEN: dict[int, str] = {
    2: "NODE",
    3: "BAR_2",
    5: "TRI_3",
    7: "QUAD_4",
    10: "TETRA_4",
    12: "PYRA_5",
    14: "PENTA_6",
    17: "HEXA_8",
    20: "MIXED",
}

_CONNECTIVITY_NAMES = frozenset({"elementconnectivity", "connectivity", "conn", "con"})
_START_OFFSET_NAMES = frozenset({"elementstartoffset", "startoffsets", "startoffset"})


def _array_role(name: str) -> str | None:
    """Classify *name* as a CGNS-specific role or ``None`` for ordinary fields.

    Returns one of ``"connectivity"``, ``"startOffset"``, ``"elementType"``,
    ``"elementRange"``, or ``None``.
    """
    lower = name.lower()
    if lower in _CONNECTIVITY_NAMES or lower.replace("_", "") in _CONNECTIVITY_NAMES:
        return "connectivity"
    if lower in _START_OFFSET_NAMES or lower.replace("_", "") in _START_OFFSET_NAMES:
        return "startOffset"
    if lower in {"element_type", "elementtype"}:
        return "elementType"
    if lower in {"element_range", "elementrange"}:
        return "elementRange"
    return None


def _child_name(args: dict, key: str, default: str) -> str:
    name = str(args.get(key) or default)
    return Tf.MakeValidIdentifier(name) or default


def read(stage: Usd.Stage | None, path: str, args: dict) -> list[dict]:
    """Populate *stage* from an NPZ file using the CGNS single-zone schema.

    Creates a CGNS-like ``Scope/Base/Zone`` hierarchy. Coordinates live on a
    ``GridCoordinates`` child prim, scalar fields live on a ``FlowSolution``
    child prim, and connectivity data lives on a section prim. All array data
    is registered as lazy fields.

    The following special arrays are consumed as CGNS metadata rather than
    registered as lazy fields:

    - ``element_type`` (shape ``[1]``): integer CGNS element-type code.
    - ``element_range`` (shape ``[2]``): ``[firstElement, lastElement]``.

    Args:
        stage: In-memory ``Usd.Stage`` to populate.
        path: Resolved path to the ``.npz`` file.
        args: Format arguments dict from the layer identifier.

    Returns:
        list[dict]: Lazy-field manifest consumed by ``PythonFileFormatBase``.

    Raises:
        RuntimeError: If no coordinate arrays can be resolved.
    """
    allow_pickle = str(args.get("allowPickle", "")).lower() == "true"
    array_info = read_npz_metadata(path)
    names = list(array_info)

    # --- resolve coordinates -------------------------------------------------
    coord_spec = resolve_coordinates(names, args)
    if coord_spec is None:
        raise RuntimeError(
            f"Could not determine coordinates for '{path}'. "
            "Provide coordsArray or coordsArrayX/Y/Z format args."
        )

    # --- scan for CGNS-specific arrays and metadata --------------------------
    connectivity_name: str | None = None
    start_offset_name: str | None = None
    element_type_token: str = "ElementTypeNull"
    element_range: tuple[int, int] | None = None

    for name in names:
        role = _array_role(name)
        if role == "connectivity" and connectivity_name is None:
            connectivity_name = name
        elif role == "startOffset" and start_offset_name is None:
            start_offset_name = name
        elif role == "elementType":
            if stage is None:
                continue
            info = array_info[name]
            if info.ndim == 1 and info.shape[0] == 1:
                arr = load_npz_array(path, name, allow_pickle=allow_pickle)
                element_type_token = _ELEMENT_TYPE_TOKEN.get(int(arr[0]), "MIXED")
            else:
                element_type_token = "MIXED"
        elif role == "elementRange":
            if stage is None:
                continue
            info = array_info[name]
            if info.ndim == 1 and info.shape[0] == 2:
                arr = load_npz_array(path, name, allow_pickle=allow_pickle)
                element_range = (int(arr[0]), int(arr[1]))

    # Names consumed by coordinates or CGNS metadata -- not registered as fields
    reserved: set[str] = set()
    if coord_spec["kind"] == "interleaved":
        reserved.add(coord_spec["array"])
    else:
        reserved.update({coord_spec["x"], coord_spec["y"], coord_spec["z"]})
    for name in names:
        if _array_role(name) is not None:
            reserved.add(name)

    # --- CGNS prim hierarchy -------------------------------------------------
    root_name = dataset_name(path, args)
    root_path = make_dataset_path(root_name, args)
    base_path = f"{root_path}/{_child_name(args, 'baseName', 'Base')}"
    zone_path = f"{base_path}/{_child_name(args, 'zoneName', 'Zone')}"
    grid_coordinates_path = f"{zone_path}/{_child_name(args, 'gridCoordinatesName', 'GridCoordinates')}"
    flow_solution_path = f"{zone_path}/{_child_name(args, 'flowSolutionName', 'FlowSolution')}"
    section_path = f"{zone_path}/{_child_name(args, 'sectionName', 'Section')}"

    root_prim = None
    zone_prim = None
    grid_coordinates_prim = None
    if stage is not None:
        root_prim = stage.DefinePrim(root_path, "Scope")
        stage.SetDefaultPrim(root_prim)
        stage.DefinePrim(base_path, "CGNSBase")

        zone_prim = stage.DefinePrim(zone_path, "CGNSZone")
        OmniSciCgns.ZoneAPI.Apply(zone_prim)

        grid_coordinates_prim = stage.DefinePrim(grid_coordinates_path, "CGNSGridCoordinates")
        OmniSciCgns.GridCoordinatesAPI.Apply(grid_coordinates_prim)
        OmniSciCgns.ZoneAPI(zone_prim).CreateGridCoordinatesRel().SetTargets([grid_coordinates_prim.GetPath()])
    lazy_fields: list[dict] = []

    # --- coordinate arrays on GridCoordinates prim ---------------------------
    if coord_spec["kind"] == "interleaved":
        coords_name = coord_spec["array"]
        coords_info = array_info[coords_name]
        if coords_info.ndim != 2 or coords_info.shape[1] < 3:
            raise RuntimeError(f"Coordinate array '{coords_name}' must be Nx3")
        coord_type = array_type_name(coords_info, prefer_float=True)
        for inst, col in (("gridCoordinatesX", 0), ("gridCoordinatesY", 1), ("gridCoordinatesZ", 2)):
            if grid_coordinates_prim is not None:
                OmniSci.ArrayAPI.Apply(grid_coordinates_prim, inst)
            register_array(
                grid_coordinates_prim, grid_coordinates_path, inst, coord_type,
                json.dumps({"array": coords_name, "column": col, "preferFloat": True}),
                lazy_fields,
            )
    else:
        for inst, array_key in (
            ("gridCoordinatesX", coord_spec["x"]),
            ("gridCoordinatesY", coord_spec["y"]),
            ("gridCoordinatesZ", coord_spec["z"]),
        ):
            if grid_coordinates_prim is not None:
                OmniSci.ArrayAPI.Apply(grid_coordinates_prim, inst)
            register_array(
                grid_coordinates_prim, grid_coordinates_path, inst,
                array_type_name(array_info[array_key], prefer_float=True),
                json.dumps({"array": array_key, "preferFloat": True}),
                lazy_fields,
            )

    # --- field arrays on FlowSolution prim -----------------------------------
    field_specs = []
    used_instances = set()
    for name in names:
        if name in reserved:
            continue
        info = array_info[name]
        if info.ndim > 2:
            continue
        type_name, components = shaped_array_type_name(info)
        if type_name is None:
            continue
        inst = unique_identifier(name, used_instances)
        field_specs.append((name, inst, type_name, components))

    flow_solution_prim = None
    if stage is not None and field_specs:
        flow_solution_prim = stage.DefinePrim(flow_solution_path, "CGNSFlowSolution")
        flow_solution_api = OmniSciCgns.FlowSolutionAPI.Apply(flow_solution_prim)
        flow_solution_api.CreateGridLocationAttr().Set("Vertex")
        OmniSciCgns.ZoneAPI(zone_prim).CreateFlowSolutionsRel().SetTargets([flow_solution_prim.GetPath()])

    for name, inst, type_name, components in field_specs:
        if flow_solution_prim is not None:
            OmniSci.FieldAPI.Apply(flow_solution_prim, inst)
            OmniSci.ArrayAPI.Apply(flow_solution_prim, inst)
            OmniSci.FieldAPI(flow_solution_prim, inst).CreateNameAttr(name)
            OmniSci.FieldAPI(flow_solution_prim, inst).CreateAssociationAttr("node")
        token = {"array": name}
        if components is None:
            token["flatten"] = True
        else:
            token["components"] = components
        register_array(
            flow_solution_prim, flow_solution_path, inst,
            type_name, json.dumps(token), lazy_fields,
        )

    # --- section prim (child of zone) ----------------------------------------
    section_prim = None
    if stage is not None:
        section_prim = OmniSci.Dataset.Define(stage, section_path).GetPrim()

        elem_api = OmniSciCgns.UnstructuredElementsAPI.Apply(section_prim)
        elem_api.CreateElementTypeAttr().Set(element_type_token)
        elem_api.CreateElementSizeBoundaryAttr().Set(0)
        if element_range is not None:
            elem_api.CreateElementRangeAttr().Set(Gf.Vec2i(*element_range))
        elem_api.CreateZoneRel().SetTargets([zone_prim.GetPath()])

    # --- connectivity array on section prim ----------------------------------
    if connectivity_name is not None:
        conn_info = array_info[connectivity_name]
        if section_prim is not None:
            OmniSci.ArrayAPI.Apply(section_prim, "elementConnectivity")
        register_array(
            section_prim, section_path, "elementConnectivity",
            array_type_name(conn_info) or "int[]",
            json.dumps({"array": connectivity_name}),
            lazy_fields,
        )

    # --- start-offset array on section prim (optional) -----------------------
    if start_offset_name is not None:
        offset_info = array_info[start_offset_name]
        if section_prim is not None:
            OmniSci.ArrayAPI.Apply(section_prim, "elementStartOffset")
        register_array(
            section_prim, section_path, "elementStartOffset",
            array_type_name(offset_info) or "int[]",
            json.dumps({"array": start_offset_name}),
            lazy_fields,
        )

    # --- wire zone -> section relationship ------------------------------------
    if zone_prim is not None and section_prim is not None:
        OmniSciCgns.ZoneAPI(zone_prim).CreateSectionsRel().SetTargets([section_prim.GetPath()])

    return lazy_fields
