// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/// @file PythonFileFormatBase.h
///
/// Reusable SdfFileFormat base class that delegates Read / CanRead / lazy-array
/// loading to Python callbacks, eliminating per-format C++ boilerplate for
/// formats that can be fully implemented in Python.
///
/// ## Combined data model
///
/// When a layer is opened and the Python `read` callback returns lazy fields,
/// `PythonFileFormatBase::Read` copies the Python-authored structure and lazy
/// value registrations into one `CaeFileFormatData` backend.
///
/// ## Python callback contract
///
/// The Python module must expose at minimum a `read` function.  All function
/// names are configurable via format arguments or `PythonFileFormatConfig`.
///
/// ```python
/// def read(layer: Sdf.Layer,
///          path: str,
///          metadata_only: bool,
///          args: dict[str, str]) -> list[dict] | dict | None:
///     """Author prims/attrs into `layer` and return an optional lazy-field
///     manifest.
///
///     Return value can be:
///     - None or absent: no lazy fields.
///     - A list of lazy-field dicts (see below).
///     - A dict with key ``"lazyFields"`` whose value is that list.
///     """
///
/// def can_read(path: str) -> bool:   # optional
///     """Return True if this format can handle the file at `path`."""
///
/// def load_array(path: str,
///                token: str,
///                time: float | None,
///                args: dict[str, str]):   # optional, required when lazy fields are returned
///     """Return the array for the given opaque `token` (and `time` for
///     time-sampled attributes).  The return type must be compatible with
///     the declared scalar or vector ``typeName``."""
/// ```
///
/// ### Lazy-field dict schema
/// Each entry in the lazy-field list is a dict with the following keys:
/// ```python
/// {
///     "primPath":  str,           # SdfPath string, e.g. "/World/data"
///     "attrName":  str,           # attribute name token
///     "typeName":  str,           # scalar or vector Sdf value type name
///     # For a single-state attribute sampled at time 0:
///     "token":     str,           # opaque key passed back to load_array
///     # For a time-sampled attribute (mutually exclusive with "token"):
///     "timeSamples": [{"time": float, "token": str}, ...]
/// }
/// ```

#pragma once

#include "CaeFileFormatData.h"
#include "DisablePXRWarnings.h"
#include "OmniSciFileFormatPythonSharedAPI.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @struct PythonFileFormatConfig
 *
 * Static configuration supplied by a concrete plugin's constructor.  All
 * string fields must remain valid for the lifetime of the format object
 * (use string literals or tokens owned by the plugin).
 *
 * The `defaultModule` / `defaultRead/CanRead/LoadArrayFunction` fields set
 * fallback values that are used when the corresponding format argument is not
 * present in `SdfLayer::FileFormatArguments`.  A concrete plugin that always
 * uses the same module and function names should fill these in; a generic
 * escape-hatch plugin (like `PythonProxyFileFormat`) leaves them empty and
 * relies on callers to pass format arguments at open time.
 */
struct PythonFileFormatConfig
{
    /// USD plugin name as registered in plugInfo.json, used to locate the
    /// plugin's resource directory for automatic `pythonPath` resolution.
    const char* pluginName = "";

    /// SdfFileFormat format identifier (e.g. "OmniSciPythonProxyFileFormat").
    const char* formatId = "";

    /// Format version string passed to SdfFileFormat (default "1.0").
    const char* version = "1.0";

    /// USD composition target (always "usd" for layer-level formats).
    const char* target = "usd";

    /// File extension handled by this format (without leading dot).
    const char* extension = "";

    /// Optional comma-separated aliases handled by the same format.
    const char* extensionAliases = "";

    /// Default Python module to import.  May be overridden at open time via
    /// the `pythonModule` format argument.
    const char* defaultModule = "";

    /// Default name of the `read` callback in the Python module.
    const char* defaultReadFunction = "read";

    /// Default name of the `can_read` callback in the Python module.
    const char* defaultCanReadFunction = "can_read";

    /// Default name of the `load_array` callback in the Python module.
    const char* defaultLoadArrayFunction = "load_array";

    /// Derive an implicit mount path from the original asset identifier.
    ///
    /// Filename-oriented formats use this to keep their root prim stable when
    /// the resolver exposes an unrelated cache filename. Generic proxy modules
    /// own their hierarchy and should disable it.
    bool deriveMountPathFromIdentifier = true;
};

/**
 * @class PythonFileFormatBase
 *
 * `SdfFileFormat` base class that delegates all file I/O to Python callbacks,
 * centralising GIL management, Boost.Python type conversions, and the
 * combined structural/lazy data pattern in one place.
 *
 * Concrete plugins inherit from this class, pass a filled-in
 * `PythonFileFormatConfig` to the protected constructor, and add only the
 * SDF_FILE_FORMAT_FACTORY_ACCESS / TF_DEFINE_PUBLIC_TOKENS boilerplate
 * required by the USD plugin registry.
 *
 * The format is **read-only**; `WriteToString` and `WriteToStream` are
 * implemented as no-ops that emit a `TF_CODING_ERROR`.
 *
 * ### Supported lazy array types
 * float[], double[], int[], int64[], float2/3/4[], double2/3/4[], int2/3/4[]
 *
 * ### Format arguments recognised at open time
 * | Key                      | Default (from config)           | Description |
 * |--------------------------|----------------------------------|-------------|
 * | `pythonModule`           | `config.defaultModule`          | Python module to import |
 * | `pythonPath`             | plugin resource dir / python/    | Path prepended to sys.path |
 * | `pythonReadFunction`     | `config.defaultReadFunction`    | `read` callback name |
 * | `pythonCanReadFunction`  | `config.defaultCanReadFunction` | `can_read` callback name |
 * | `pythonLoadArrayFunction`| `config.defaultLoadArrayFunction`| `load_array` callback name |
 * | `cacheMode`              | `all`                           | Lazy-array value retention: `all`, `static`, or `none`
 * |
 *
 * @see PythonFileFormatConfig
 * @see CaeFileFormatData
 */
class OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_TYPE PythonFileFormatBase : public SdfFileFormat
{
public:
    /// Returns true when the file extension matches and the optional Python
    /// `can_read` probe (if present in the module) approves the path.
    /// Falls back to `true` when no `can_read` function is found.
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API bool CanRead(const std::string& filePath) const override;

    /// Invokes the Python `read` callback to author the structure layer, then
    /// wires lazy-array loaders via `CaeFileFormatData` for any fields declared in
    /// the returned lazy-field manifest.
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API bool Read(SdfLayer* layer,
                                                     const std::string& resolvedPath,
                                                     bool metadataOnly) const override;

    /// Not supported -- emits TF_CODING_ERROR and returns false.
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API bool WriteToString(const SdfLayer& layer,
                                                              std::string* str,
                                                              const std::string& comment = std::string()) const override;

    /// Not supported -- emits TF_CODING_ERROR and returns false.
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API bool WriteToStream(const SdfSpecHandle& spec,
                                                              std::ostream& out,
                                                              size_t indent) const override;

protected:
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API explicit PythonFileFormatBase(const PythonFileFormatConfig& config);
    OMNI_SCI_FILE_FORMAT_PYTHON_SHARED_API ~PythonFileFormatBase() override;

    /// Accessor for the static config, available to subclasses that need to
    /// inspect or extend the plugin configuration.
    const PythonFileFormatConfig& GetPythonConfig() const
    {
        return _config;
    }

private:
    PythonFileFormatConfig _config;
};

PXR_NAMESPACE_CLOSE_SCOPE
