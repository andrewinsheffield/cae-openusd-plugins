<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Core OmniSci Schemas

The `omniSci` schema library defines the format-independent contract shared by
the scientific file-format readers. It deliberately contains only three
schemas: a typed dataset anchor and multiple-apply APIs for named fields and
arrays.

The source of truth is
[`source/schemas/omni_sci/schema.usda`](../../source/schemas/omni_sci/schema.usda).

| USD schema | Kind | Generated Python class |
| --- | --- | --- |
| `OmniSciDataset` | Concrete typed prim | `OmniSci.Dataset` |
| `OmniSciFieldAPI:<instance>` | Multiple-apply API | `OmniSci.FieldAPI` |
| `OmniSciArrayAPI:<instance>` | Multiple-apply API | `OmniSci.ArrayAPI` |

## `OmniSciDataset`

`OmniSciDataset` is a concrete typed prim that marks one logical scientific
dataset. It does not prescribe a mesh topology or store values by itself.
Readers and authors apply domain and array APIs to the dataset or its
descendants.

A format may use one dataset prim or a hierarchy of datasets. The relevant
[Conceptual Data Mapping](../conceptual_data_mapping/README.md) defines that
choice.

## `OmniSciFieldAPI:<instance>`

`OmniSciFieldAPI` is a multiple-apply API for the semantic identity of a
physical or computed field.

| Property | Type | Meaning |
| --- | --- | --- |
| `omni:sci:field:<instance>:name` | uniform `string` | Original name in the source dataset. |
| `omni:sci:field:<instance>:association` | uniform `token` | `none`, `node`, or `element`. |

The API instance name is a USD-safe identifier. It may differ from the source
name, which is why the original string is preserved separately.

Associations mean:

- `none`: no topological association;
- `node`: one tuple per mesh node or point;
- `element`: one tuple per cell, element, face, or particle as defined by the
  domain mapping.

## `OmniSciArrayAPI:<instance>`

`OmniSciArrayAPI` is a multiple-apply API for a logical array.

| Property | Type | Meaning |
| --- | --- | --- |
| `omni:sci:array:<instance>:device` | `token` | Advisory location such as `cpu`, `cuda`, or `gpu`. |

The optional device token describes residence; authoring it does not move data.
It is a free token with no fixed allowed values, so consumers must tolerate
device kinds they do not recognize.

### Value Attribute Convention

The array value is exposed as:

```text
omni:sci:array:<instance>:value
```

This property is intentionally not declared by `OmniSciArrayAPI`. Its concrete
USD type depends on the source array and may be `float[]`, `double3[]`,
`int64[]`, or another supported type. File-format readers declare the typed
attribute from source metadata and may resolve its values lazily.

Potentially varying values supplied by native readers are time samples rather
than defaults. This includes single-state values, which normally use time `0`.
Consumers should request an explicit time or `Usd.TimeCode.EarliestTime()`;
see [Using the plugins](../usage.md#read-lazy-array-values).

Topology and coordinate arrays normally use `OmniSciArrayAPI` without a field
API. A physical field normally uses a field and array instance with the same
name:

```text
OmniSciFieldAPI:pressure
OmniSciArrayAPI:pressure
omni:sci:array:pressure:value
```

The matching name is a composition convention rather than a relationship. It
keeps field metadata and storage metadata separable while giving consumers one
stable key.

## Python Authoring Example

```python
from pxr import OmniSci, Sdf, Usd, Vt

stage = Usd.Stage.CreateInMemory()
dataset = OmniSci.Dataset.Define(stage, "/Simulation")
prim = dataset.GetPrim()

field = OmniSci.FieldAPI.Apply(prim, "pressure")
field.CreateNameAttr().Set("p")
field.CreateAssociationAttr().Set(OmniSci.Tokens.element)

array = OmniSci.ArrayAPI.Apply(prim, "pressure")
array.CreateDeviceAttr().Set("cpu")

value = prim.CreateAttribute(
    "omni:sci:array:pressure:value",
    Sdf.ValueTypeNames.FloatArray,
)
value.Set(Vt.FloatArray([101325.0, 101100.0]))
```

File-format plugins use the same USD shape but can provide `value` from a
deferred backing layer instead of authoring the array into the structure layer.
The example above authors a default value directly and therefore does not need
an explicit time code.

## USDA Shape

```usda
def OmniSciDataset "Simulation" (
    prepend apiSchemas = [
        "OmniSciFieldAPI:pressure",
        "OmniSciArrayAPI:pressure"
    ]
)
{
    uniform string omni:sci:field:pressure:name = "p"
    uniform token omni:sci:field:pressure:association = "element"
    token omni:sci:array:pressure:device = "cpu"
    float[] omni:sci:array:pressure:value = [101325, 101100]
}
```

## Domain Composition

Domain-specific schemas are composed as APIs alongside the core schemas rather
than creating a deep inheritance tree. For example, an OpenFOAM volume is an
`OmniSciDataset` with `OmniSciOpenFoamPolyMeshAPI` plus named array APIs;
reservoir properties additionally apply
`OmniSciReservoirCellPropertyAPI:<instance>` to describe their index space.

This keeps `omniSci` independent of any file format while allowing each
Conceptual Data Mapping to state the additional semantics required to interpret
its arrays.
