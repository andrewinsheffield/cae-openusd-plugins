# EDEM Field Extension Analysis

## Change Summary

**Commit:** `60daf5b` — "extend EDEM field ingest"  
**File:** `source/file_formats/edem/src/OmniSciEdemFileFormat.cpp`  
**Function:** `ScanParticleFields`

### What Changed

**Before:** A hard-coded list of four fields was ingested:

```cpp
static const std::array<std::string, 4> kKnownFields = { "ids", "scale", "orientation", "velocity" };
for (const std::string& child : kKnownFields)
```

**After:** All datasets present in the HDF5 particle type group are discovered dynamically:

```cpp
// "position" is authored as the point cloud positions attribute, not as a generic field.
static const std::array<std::string, 1> kReservedFields = { "position" };

std::vector<std::string> children = ListChildNames(file.Get(), basePath);
std::sort(children.begin(), children.end());
for (const std::string& child : children)
{
    if (std::find(kReservedFields.begin(), kReservedFields.end(), child) != kReservedFields.end())
        continue;
    ...
}
```

`position` is excluded because it is already authored as the `OmniSciCaePointCloudAPI` positions
array, not as a generic field. All other datasets — including any custom or simulation-specific
fields in the file — are now ingested automatically.

---

## Full USD Authoring Pipeline

`ScanParticleFields` returns `std::vector<FieldInfo>`. Downstream, for every `FieldInfo` in
`cloud.fields`, the reader authors:

| What | USD Schema Applied | Attribute |
|---|---|---|
| Field descriptor | `OmniSciFieldAPI:<name>` | `omni:sci:field:<name>:name`, `association = "node"` |
| Array registration | `OmniSciArrayAPI:<name>` | `omni:sci:array:<name>:device = "cpu"` |
| Lazy value loader | — | Registered per time sample via `RegisterTimeSamples` / `RegisterLazySingleState` |

Values are **not** read from HDF5 at stage-open time. Each array is loaded on demand when
`UsdAttribute.Get()` is called at a specific `timeCode`.

---

## kit-cae Impact Assessment

**No kit-cae source code changes are required.**

### Why the pipeline is already generic

#### Field discovery — `omni.cae.core/python/usd_utils.py::get_instances`

```python
for applied_schema in prim.GetAppliedSchemas():
    schema_name, instance_name = registry.GetTypeNameAndInstance(applied_schema)
    if instance_name and schema_name == api_schema_name:
        instances.append(instance_name)
```

Iterates `prim.GetAppliedSchemas()` at runtime. Any `OmniSciFieldAPI:<name>` instance on the
prim is returned regardless of the field name.

#### Operator field enumeration — `omni.cae.viz/python/utils.py::get_available_fields`

```python
fields = {fi.name: fi for fi in await cae_simdata.GetAvailableFields.invoke(target)}
```

Calls `GetAvailableFields` on the dataset prim. The result set is intersected across all
selected targets and surfaced in the property panel's "Field Names" widget.

#### SimData adapter — `omni.cae.simdata/python/omnisci_commands.py`

```python
class OmniSciDatasetGetAvailableFields(GetAvailableFields):
    async def do(self):
        field_infos = simdata.usd.list_fields(self.dataset)
        return [FieldInfo(name=fi.name, label=fi.label, association=fi.association)
                for fi in field_infos]
```

`simdata.usd.list_fields` comes from the `warp_simdata` wheel and reads the applied
`OmniSciFieldAPI` instances from the prim's schema — no per-format allow-list.

### EDEM importer entry (no change needed)

```python
class EDEMAssetImporter(PayloadImporter):
    importer_name = "CAE EDEM Importer"
    file_extensions = (".dem",)
    importer_filter_descriptions = ["EDEM Deck Files (*.dem)"]
    schema_api = "OmniSciFileFormatArgsEdemAPI"
```

The importer only triggers the file-format reader. Field names are not mentioned.

---

## Deploying to kit-cae

Build and package `cae-openusd-plugins` (see [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md)), then
override kit-cae's dependency:

```bat
set KIT_CAE_OPENUSD_PLUGINS_PACKAGE=C:\Users\ahobbs\Documents\OV-Composer\cae-openusd-plugins\build\packages\cae_openusd_plugins@<version>.zip
repo.bat build -rx
```

---

## Verification

After rebuilding kit-cae, open an EDEM deck in a Kit session and confirm the fields are visible:

```python
from omni.cae.simdata import GetAvailableFields
from pxr import Usd

stage = Usd.Stage.Open("path/to/case.dem")
cloud_prim = stage.GetPrimAtPath("/World/ParticleClouds/<ParticleTypeName>")
fields = await GetAvailableFields.invoke(cloud_prim)
print([f.name for f in fields])
```

Expected: the printed list includes both the original four fields (`ids`, `scale`, `orientation`,
`velocity`) and any additional fields present in the EDEM file.

---

## Caveat: `warp_simdata` point-cloud adapter

If `simdata.usd.list_fields` does not return the new fields, the point-cloud adapter in
`warp_simdata` may need updating. Per [FormatOnboarding.md §5](https://github.com/andrewinsheffield/kit-cae-3.0/blob/main/docs/FormatOnboarding.md#5-add-simdata-conversion-when-needed),
a new or updated adapter would then need to be built and pointed at with:

```bat
set KIT_CAE_WARP_SIMDATA_PACKAGE=C:\path\to\warp_simdata@<version>.zip
repo.bat build -rx
```
