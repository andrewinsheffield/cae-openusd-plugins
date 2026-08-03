// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PythonFileFormatBase.h"

#include "DisablePXRWarnings.h"
#include "MountPath.h"
#include "PythonDebugCodes.h"
#include "ResolverAsset.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/pyInterpreter.h>
#include <pxr/base/tf/pyInvoke.h>
#include <pxr/base/tf/pyLock.h>
#include <pxr/base/tf/pyUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>
#include <pxr/external/boost/python/dict.hpp>
#include <pxr/external/boost/python/extract.hpp>
#include <pxr/external/boost/python/import.hpp>
#include <pxr/external/boost/python/list.hpp>
#include <pxr/external/boost/python/object.hpp>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/pyConversions.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE
TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_PYTHON_FILEFORMAT, "Python-backed file-format diagnostics");
}
namespace
{

namespace bp = pxr_boost::python;

// ---------------------------------------------------------------------------
// Format argument keys recognised by all Python-backed file format plugins.
// These match the keys documented in plugInfo.json FileFormatArguments.
// ---------------------------------------------------------------------------
constexpr const char* kArgPythonModule = "pythonModule";
constexpr const char* kArgPythonPath = "pythonPath";
constexpr const char* kArgReadFunction = "pythonReadFunction";
constexpr const char* kArgCanReadFunction = "pythonCanReadFunction";
constexpr const char* kArgLoadArrayFunction = "pythonLoadArrayFunction";

// ---------------------------------------------------------------------------
// Internal data structures
// ---------------------------------------------------------------------------

/// Python call parameters resolved from PythonFileFormatConfig defaults and
/// per-open SdfLayer::FileFormatArguments overrides.  Constructed once per
/// Read / CanRead call so each open is self-contained.
struct PythonCallConfig
{
    std::string moduleName;
    std::string pythonPath;
    std::string readFunctionName;
    std::string canReadFunctionName;
    std::string loadArrayFunctionName;
};

/// Descriptor for one lazy array attribute declared by the Python `read`
/// callback.  Corresponds to a single dict in the lazy-field manifest.
struct LazyFieldSpec
{
    SdfPath primPath;
    TfToken attrName;
    /// One of the scalar/vector array type names supported by IsSupportedLazyType().
    TfToken typeName;
    /// Opaque token passed back to `load_array` for a single-state sample at
    /// time 0. Empty when `timeSamples` is non-empty.
    std::string token;
    /// Time-sample pairs: (timeCode, opaque token) for animated attributes.
    /// Empty when `token` is non-empty.
    std::vector<std::pair<double, std::string>> timeSamples;
};

/// Aggregate return value from the Python `read` callback after
/// Boost.Python extraction.
struct PythonReadResult
{
    std::vector<LazyFieldSpec> lazyFields;
};

// ---------------------------------------------------------------------------
// Helpers: format-argument resolution
// ---------------------------------------------------------------------------

/// Returns the value for `key` in `args`, or `fallback` when absent or empty.
std::string GetArgOrDefault(const SdfLayer::FileFormatArguments& args, const char* key, const char* fallback)
{
    const auto it = args.find(key);
    if (it != args.end() && !it->second.empty())
        return it->second;
    return fallback ? std::string(fallback) : std::string();
}

bool ExtensionMatches(const std::string& extension, const PythonFileFormatConfig& config)
{
    std::string lowerExtension = extension;
    std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerExtension == config.extension)
        return true;

    std::string aliases = config.extensionAliases ? config.extensionAliases : "";
    size_t first = 0;
    while (first < aliases.size())
    {
        size_t last = aliases.find(',', first);
        if (last == std::string::npos)
            last = aliases.size();
        if (lowerExtension == aliases.substr(first, last - first))
            return true;
        first = last + 1;
    }
    return false;
}

/// Locates the plugin's Python directory by looking up the plugin in the
/// PlugRegistry (first by `ownerType`, then by `config.pluginName`) and
/// returning `<resourcePath>/../python`.  Returns an empty string when the
/// directory does not exist or the plugin cannot be found.
std::string ResolvePluginPythonPath(const PythonFileFormatConfig& config, const TfType& ownerType)
{
    PlugPluginPtr plugin;
    if (!ownerType.IsUnknown())
        plugin = PlugRegistry::GetInstance().GetPluginForType(ownerType);
    if (!plugin && config.pluginName && *config.pluginName)
        plugin = PlugRegistry::GetInstance().GetPluginWithName(config.pluginName);
    if (!plugin)
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] no plugin found for python path resolution (pluginName='%s')\n",
                 config.pluginName ? config.pluginName : "");
        return {};
    }

    const std::filesystem::path pluginPythonPath =
        std::filesystem::path(plugin->GetResourcePath()).parent_path() / "python";
    if (!std::filesystem::exists(pluginPythonPath))
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] plugin python path does not exist: '%s'\n", pluginPythonPath.string().c_str());
        return {};
    }

    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] resolved plugin python path '%s'\n", pluginPythonPath.string().c_str());
    return pluginPythonPath.string();
}

/// Merges PythonFileFormatConfig defaults with per-open format arguments to
/// produce the PythonCallConfig for one Read / CanRead invocation.
/// `args` may be nullptr (treated as an empty map).
PythonCallConfig ResolvePythonCallConfig(const PythonFileFormatConfig& config,
                                         const TfType& ownerType,
                                         const SdfLayer::FileFormatArguments* args)
{
    const SdfLayer::FileFormatArguments emptyArgs;
    const SdfLayer::FileFormatArguments& fmtArgs = args ? *args : emptyArgs;
    const std::string defaultPythonPath = ResolvePluginPythonPath(config, ownerType);

    PythonCallConfig result;
    result.moduleName = GetArgOrDefault(fmtArgs, kArgPythonModule, config.defaultModule);
    result.pythonPath = GetArgOrDefault(fmtArgs, kArgPythonPath, defaultPythonPath.c_str());
    result.readFunctionName = GetArgOrDefault(fmtArgs, kArgReadFunction, config.defaultReadFunction);
    result.canReadFunctionName = GetArgOrDefault(fmtArgs, kArgCanReadFunction, config.defaultCanReadFunction);
    result.loadArrayFunctionName = GetArgOrDefault(fmtArgs, kArgLoadArrayFunction, config.defaultLoadArrayFunction);
    return result;
}

// ---------------------------------------------------------------------------
// Helpers: Python interpreter management
// ---------------------------------------------------------------------------

/// Ensures `pythonPath` is present at index 0 of `sys.path`, initialising
/// the interpreter if needed.  Does nothing when `pythonPath` is empty or
/// already present.
void EnsurePythonPath(const std::string& pythonPath)
{
    if (pythonPath.empty())
        return;

    TfPyInitialize();
    TfPyLock lock;

    bp::object sys = bp::import("sys");
    bp::list sysPath = bp::extract<bp::list>(sys.attr("path"));
    const Py_ssize_t n = bp::len(sysPath);
    for (Py_ssize_t i = 0; i < n; ++i)
    {
        bp::extract<std::string> ex(sysPath[i]);
        if (ex.check() && ex() == pythonPath)
            return;
    }
    sysPath.insert(0, pythonPath);
    TF_DEBUG(CAE_PYTHON_FILEFORMAT).Msg("[PythonFileFormat] inserted '%s' into sys.path\n", pythonPath.c_str());
}

/// Returns true when `moduleName` is importable and exposes an attribute
/// named `functionName`.  Used to make `can_read` truly optional -- if the
/// Python module does not define it, CanRead returns true unconditionally.
bool ModuleHasCallable(const std::string& moduleName, const std::string& functionName)
{
    if (moduleName.empty() || functionName.empty())
        return false;

    TfPyInitialize();
    TfPyLock lock;
    try
    {
        bp::object pythonModule = bp::import(moduleName.c_str());
        pythonModule.attr(functionName.c_str());
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] module '%s' has callable '%s'\n", moduleName.c_str(), functionName.c_str());
        return true;
    }
    catch (bp::error_already_set const&)
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] module '%s' has no callable '%s'\n", moduleName.c_str(), functionName.c_str());
        return false;
    }
}

/// Converts SdfLayer::FileFormatArguments into a Python dict so callbacks
/// receive the full set of format arguments as a str->str mapping.
bp::dict BuildArgsDict(const SdfLayer::FileFormatArguments& args)
{
    bp::dict result;
    for (const auto& [key, value] : args)
        result[key] = value;
    return result;
}

// ---------------------------------------------------------------------------
// Helpers: type mapping (lazy-field manifest -> USD value types)
// ---------------------------------------------------------------------------

/// Returns true for array type name strings that PythonFileFormatBase can
/// handle as lazy fields.  Extend this (and GetSdfValueTypeName below) to
/// support additional types.
bool IsSupportedLazyType(const TfToken& typeName)
{
    return typeName == TfToken("float[]") || typeName == TfToken("double[]") || typeName == TfToken("int[]") ||
           typeName == TfToken("uint[]") || typeName == TfToken("int64[]") || typeName == TfToken("uint64[]") ||
           typeName == TfToken("float2[]") || typeName == TfToken("float3[]") || typeName == TfToken("float4[]") ||
           typeName == TfToken("double2[]") || typeName == TfToken("double3[]") || typeName == TfToken("double4[]") ||
           typeName == TfToken("int2[]") || typeName == TfToken("int3[]") || typeName == TfToken("int4[]");
}

/// Maps a supported lazy-type token to the corresponding SdfValueTypeName.
/// Callers must check IsSupportedLazyType() first; unrecognised tokens fall
/// through to Int64Array.
const SdfValueTypeName& GetSdfValueTypeName(const TfToken& typeName)
{
    if (typeName == TfToken("float[]"))
        return SdfValueTypeNames->FloatArray;
    if (typeName == TfToken("double[]"))
        return SdfValueTypeNames->DoubleArray;
    if (typeName == TfToken("int[]"))
        return SdfValueTypeNames->IntArray;
    if (typeName == TfToken("uint[]"))
        return SdfValueTypeNames->UIntArray;
    if (typeName == TfToken("float2[]"))
        return SdfValueTypeNames->Float2Array;
    if (typeName == TfToken("float3[]"))
        return SdfValueTypeNames->Float3Array;
    if (typeName == TfToken("float4[]"))
        return SdfValueTypeNames->Float4Array;
    if (typeName == TfToken("double2[]"))
        return SdfValueTypeNames->Double2Array;
    if (typeName == TfToken("double3[]"))
        return SdfValueTypeNames->Double3Array;
    if (typeName == TfToken("double4[]"))
        return SdfValueTypeNames->Double4Array;
    if (typeName == TfToken("int2[]"))
        return SdfValueTypeNames->Int2Array;
    if (typeName == TfToken("int3[]"))
        return SdfValueTypeNames->Int3Array;
    if (typeName == TfToken("int4[]"))
        return SdfValueTypeNames->Int4Array;
    if (typeName == TfToken("uint64[]"))
        return SdfValueTypeNames->UInt64Array;
    return SdfValueTypeNames->Int64Array;
}

bool TimeSamplesAreSorted(const std::vector<std::pair<double, std::string>>& samples)
{
    return std::is_sorted(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
}

const char* CacheModeName(CaeFileFormatData::CacheMode mode)
{
    switch (mode)
    {
    case CaeFileFormatData::CacheMode::All:
        return "all";
    case CaeFileFormatData::CacheMode::Static:
        return "static";
    case CaeFileFormatData::CacheMode::None:
        return "none";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Helpers: Boost.Python dict / result parsing
// ---------------------------------------------------------------------------

/// Extracts the string value for `key` from `dictObj` into `*out`.
/// Returns false when the key is absent or the value is not a string.
bool ExtractStringDictItem(const bp::dict& dictObj, const char* key, std::string* out)
{
    if (!dictObj.has_key(key) || !out)
        return false;
    bp::extract<std::string> extractor(dictObj[key]);
    if (!extractor.check())
        return false;
    *out = extractor();
    return true;
}

/// Parses one lazy-field dict from the Python read result into `*field`.
/// Required keys: "primPath", "attrName", "typeName".
/// Exactly one of "token" (static) or "timeSamples" (animated) must be set.
/// Returns false on malformed input or an unsupported typeName.
bool ParseLazyField(const bp::dict& dictObj, LazyFieldSpec* field)
{
    if (!field)
        return false;

    std::string primPath;
    std::string attrName;
    std::string typeName;
    if (!ExtractStringDictItem(dictObj, "primPath", &primPath) ||
        !ExtractStringDictItem(dictObj, "attrName", &attrName) || !ExtractStringDictItem(dictObj, "typeName", &typeName))
    {
        return false;
    }

    field->primPath = SdfPath(primPath);
    field->attrName = TfToken(attrName);
    field->typeName = TfToken(typeName);
    if (!field->primPath.IsPrimPath() || field->attrName.IsEmpty() || !IsSupportedLazyType(field->typeName))
        return false;

    if (dictObj.has_key("token"))
    {
        bp::extract<std::string> tokenExtractor(dictObj["token"]);
        if (!tokenExtractor.check())
            return false;
        field->token = tokenExtractor();
    }

    if (dictObj.has_key("timeSamples"))
    {
        bp::list samples = bp::extract<bp::list>(dictObj["timeSamples"]);
        const Py_ssize_t n = bp::len(samples);
        field->timeSamples.reserve(static_cast<size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i)
        {
            bp::dict sampleDict = bp::extract<bp::dict>(samples[i]);
            bp::extract<double> timeExtractor(sampleDict["time"]);
            bp::extract<std::string> tokenExtractor(sampleDict["token"]);
            if (!timeExtractor.check() || !tokenExtractor.check())
                return false;
            field->timeSamples.emplace_back(timeExtractor(), tokenExtractor());
        }
    }

    // A valid lazy field must carry either a static token or at least one
    // time sample; otherwise there is nothing for load_array to return.
    return !field->token.empty() || !field->timeSamples.empty();
}

/// Parses the return value from the Python `read` callback into
/// `*result`.  Accepts three shapes:
///   - None -> empty result (success)
///   - list  -> treated directly as the lazy-field list
///   - dict with key "lazyFields" -> same list, nested under the key
/// Returns false on type mismatches or invalid lazy-field dicts.
bool ParseReadResult(const bp::object& resultObj, PythonReadResult* result)
{
    if (!result)
        return false;

    if (TfPyIsNone(resultObj))
        return true;

    bp::object lazyFieldObj = resultObj;
    bp::extract<bp::dict> dictExtractor(resultObj);
    if (dictExtractor.check())
    {
        bp::dict resultDict = dictExtractor();
        if (!resultDict.has_key("lazyFields"))
            return true;
        lazyFieldObj = resultDict["lazyFields"];
    }

    bp::list lazyFields = bp::extract<bp::list>(lazyFieldObj);
    const Py_ssize_t n = bp::len(lazyFields);
    result->lazyFields.reserve(static_cast<size_t>(n));
    for (Py_ssize_t i = 0; i < n; ++i)
    {
        LazyFieldSpec field;
        if (!ParseLazyField(bp::extract<bp::dict>(lazyFields[i]), &field))
            return false;
        result->lazyFields.push_back(std::move(field));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Python callback invocation
// ---------------------------------------------------------------------------

/// Invokes the `can_read` Python callback for `filePath`.
/// Returns nullopt when the callback is not present in the module (caller
/// should treat this as "undecided" and fall back to the extension check).
/// Returns false on invocation failure or a non-bool return value.
std::optional<bool> CallCanRead(const PythonFileFormatConfig& config, const TfType& ownerType, const std::string& filePath)
{
    const PythonCallConfig callConfig = ResolvePythonCallConfig(config, ownerType, nullptr);
    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] CanRead probe path='%s' module='%s' function='%s' pythonPath='%s'\n", filePath.c_str(),
             callConfig.moduleName.c_str(), callConfig.canReadFunctionName.c_str(), callConfig.pythonPath.c_str());
    EnsurePythonPath(callConfig.pythonPath);
    if (!ModuleHasCallable(callConfig.moduleName, callConfig.canReadFunctionName))
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] CanRead has no Python probe for '%s'; falling back to extension\n",
                 filePath.c_str());
        return std::nullopt;
    }

    TfPyInitialize();
    TfPyLock lock;

    // Keep Python argument construction and invocation within one explicit
    // GIL scope. Do not build Boost.Python objects for calls before TfPyLock.
    bool canRead = false;
    bp::list posArgs;
    bp::dict kwArgs;
    posArgs.append(filePath);

    bp::object resultObj;
    if (!Tf_PyInvokeImpl(callConfig.moduleName, callConfig.canReadFunctionName, posArgs, kwArgs, &resultObj))
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT).Msg("[PythonFileFormat] CanRead Python probe failed for '%s'\n", filePath.c_str());
        return false;
    }

    bp::extract<bool> extractor(resultObj);
    if (!extractor.check())
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] CanRead Python probe returned non-bool for '%s'\n", filePath.c_str());
        return false;
    }
    canRead = extractor();
    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] CanRead Python probe returned %d for '%s'\n", canRead ? 1 : 0, filePath.c_str());
    return canRead;
}

/// Invokes the `read` Python callback.  On success `*ok` is set to true and
/// the returned `PythonReadResult` carries the parsed lazy-field manifest.
/// On any failure `*ok` is false and the result is empty; a TF_WARN is
/// emitted to aid debugging.
PythonReadResult CallRead(const PythonFileFormatConfig& config,
                          const TfType& ownerType,
                          SdfLayer* layer,
                          const SdfLayer::FileFormatArguments& fmtArgs,
                          const std::string& resolvedPath,
                          bool metadataOnly,
                          bool* ok)
{
    PythonReadResult readResult;
    *ok = false;

    const PythonCallConfig callConfig = ResolvePythonCallConfig(config, ownerType, &fmtArgs);
    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] Read path='%s' metadataOnly=%d module='%s' read='%s' load='%s' args=%zu\n",
             resolvedPath.c_str(), metadataOnly ? 1 : 0, callConfig.moduleName.c_str(),
             callConfig.readFunctionName.c_str(), callConfig.loadArrayFunctionName.c_str(), fmtArgs.size());
    for (const auto& [key, value] : fmtArgs)
        TF_DEBUG(CAE_PYTHON_FILEFORMAT).Msg("[PythonFileFormat]   arg %s='%s'\n", key.c_str(), value.c_str());
    if (callConfig.moduleName.empty())
    {
        TF_WARN("PythonFileFormatBase: no Python module configured for '%s'", resolvedPath.c_str());
        return readResult;
    }

    EnsurePythonPath(callConfig.pythonPath);

    TfPyInitialize();
    TfPyLock lock;

    // File format callbacks must not touch Python state outside TfPyLock.
    bp::list posArgs;
    bp::dict kwArgs;
    posArgs.append(TfPyObject(SdfLayerHandle(layer)));
    posArgs.append(resolvedPath);
    posArgs.append(metadataOnly);
    posArgs.append(BuildArgsDict(fmtArgs));

    bp::object resultObj;
    if (!Tf_PyInvokeImpl(callConfig.moduleName, callConfig.readFunctionName, posArgs, kwArgs, &resultObj))
    {
        TF_WARN("PythonFileFormatBase: read failed for '%s'", resolvedPath.c_str());
        return readResult;
    }

    if (!ParseReadResult(resultObj, &readResult))
    {
        TF_WARN("PythonFileFormatBase: Python read result for '%s' has an invalid shape", resolvedPath.c_str());
        return readResult;
    }

    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] Read returned %zu lazy field(s) for '%s'\n", readResult.lazyFields.size(),
             resolvedPath.c_str());
    for (const LazyFieldSpec& field : readResult.lazyFields)
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat]   lazy prim=%s attr=%s type=%s staticToken=%d timeSamples=%zu\n",
                 field.primPath.GetText(), field.attrName.GetText(), field.typeName.GetText(),
                 field.token.empty() ? 0 : 1, field.timeSamples.size());
    }

    *ok = true;
    return readResult;
}

/// Invokes the `load_array` Python callback for a single lazy attribute value.
/// `token` is the opaque key returned by the `read` callback; `time` is set
/// for time-sampled attributes and nullopt for static ones.
/// Returns an empty VtValue and emits TF_RUNTIME_ERROR on failure.
VtValue CallLoadArray(const PythonCallConfig& callConfig,
                      const SdfLayer::FileFormatArguments& fmtArgs,
                      const std::string& resolvedPath,
                      const std::string& token,
                      const std::optional<double>& time,
                      const TfToken& typeName)
{
    EnsurePythonPath(callConfig.pythonPath);

    TfPyInitialize();
    TfPyLock lock;

    if (time.has_value())
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] load_array path='%s' token='%s' time=%g type=%s function='%s.%s'\n",
                 resolvedPath.c_str(), token.c_str(), *time, typeName.GetText(), callConfig.moduleName.c_str(),
                 callConfig.loadArrayFunctionName.c_str());
    }
    else
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] load_array path='%s' token='%s' static type=%s function='%s.%s'\n",
                 resolvedPath.c_str(), token.c_str(), typeName.GetText(), callConfig.moduleName.c_str(),
                 callConfig.loadArrayFunctionName.c_str());
    }

    // Lazy loads may happen later and on demand; keep the entire Python call
    // and conversion inside the lock scope.
    bp::list posArgs;
    bp::dict kwArgs;
    posArgs.append(resolvedPath);
    posArgs.append(token);
    posArgs.append(time ? bp::object(*time) : bp::object());
    posArgs.append(BuildArgsDict(fmtArgs));

    bp::object resultObj;
    if (!Tf_PyInvokeImpl(callConfig.moduleName, callConfig.loadArrayFunctionName, posArgs, kwArgs, &resultObj))
    {
        TF_RUNTIME_ERROR(
            "PythonFileFormatBase: load_array failed for '%s' token '%s'", resolvedPath.c_str(), token.c_str());
        return {};
    }

    return UsdPythonToSdfType(TfPyObjWrapper(resultObj), GetSdfValueTypeName(typeName));
}

bool RebasePythonReadResult(SdfLayer* layer,
                            const std::string& identifier,
                            const SdfLayer::FileFormatArguments& arguments,
                            PythonReadResult* readResult)
{
    if (const auto mount = arguments.find(CaeMountPathArgName()); mount == arguments.end() || mount->second.empty())
        return true;

    SdfPath rootPath;
    try
    {
        rootPath = CaeResolveRootPrimPath(identifier, arguments);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("PythonFileFormatBase: %s", ex.what());
        return false;
    }

    SdfPath pythonLeaf;
    if (const TfToken pythonDefault = layer->GetDefaultPrim(); !pythonDefault.IsEmpty())
        pythonLeaf = SdfPath::AbsoluteRootPath().AppendChild(pythonDefault);
    else if (!readResult->lazyFields.empty())
    {
        const auto prefixes = readResult->lazyFields.front().primPath.GetPrefixes();
        if (!prefixes.empty())
            pythonLeaf = prefixes.front();
    }

    if (!pythonLeaf.IsEmpty() && pythonLeaf != rootPath)
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] rebasing Python-authored leaf %s to mount root %s\n", pythonLeaf.GetText(),
                 rootPath.GetText());
        if (SdfPrimSpecHandle source = layer->GetPrimAtPath(pythonLeaf))
        {
            if (rootPath.GetPathElementCount() > 1)
                SdfCreatePrimInLayer(SdfLayerHandle(layer), rootPath.GetParentPath());
            SdfCopySpec(SdfLayerHandle(layer), pythonLeaf, SdfLayerHandle(layer), rootPath);
            if (SdfPrimSpecHandle stale = layer->GetPrimAtPath(pythonLeaf))
                layer->ScheduleRemoveIfInert(stale.GetSpec());
        }
        for (LazyFieldSpec& field : readResult->lazyFields)
        {
            if (field.primPath.HasPrefix(pythonLeaf))
                field.primPath = field.primPath.ReplacePrefix(pythonLeaf, rootPath);
        }
    }
    else
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] mount root %s requires no subtree rebase (pythonLeaf=%s)\n", rootPath.GetText(),
                 pythonLeaf.GetText());
    }
    CaeAuthorMountPathOvers(layer, rootPath);
    return true;
}

CaeFileFormatDataRefPtr RegisterPythonLazyFields(const PythonReadResult& readResult,
                                                 const PythonCallConfig& callConfig,
                                                 const SdfLayer::FileFormatArguments& arguments,
                                                 const std::string& localPath)
{
    const CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::ParseCacheMode(arguments);
    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] creating CaeFileFormatData cacheMode=%s fields=%zu\n", CacheModeName(cacheMode),
             readResult.lazyFields.size());
    CaeFileFormatDataRefPtr fileData = CreateCaeFileFormatData(cacheMode);
    for (const LazyFieldSpec& field : readResult.lazyFields)
    {
        if (field.timeSamples.empty())
        {
            TF_DEBUG(CAE_PYTHON_FILEFORMAT)
                .Msg("[PythonFileFormat] registering static lazy attr prim=%s attr=%s type=%s\n",
                     field.primPath.GetText(), field.attrName.GetText(), field.typeName.GetText());
            fileData->RegisterLazySingleState(
                field.primPath, field.attrName, field.typeName, 0.0,
                [callConfig, arguments, localPath, token = field.token, type = field.typeName]()
                { return CallLoadArray(callConfig, arguments, localPath, token, 0.0, type); });
            continue;
        }

        std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
        samples.reserve(field.timeSamples.size());
        for (const auto& [time, token] : field.timeSamples)
        {
            samples.emplace_back(time, [callConfig, arguments, localPath, token, time, type = field.typeName]()
                                 { return CallLoadArray(callConfig, arguments, localPath, token, time, type); });
        }
        if (TimeSamplesAreSorted(field.timeSamples))
        {
            TF_DEBUG(CAE_PYTHON_FILEFORMAT)
                .Msg("[PythonFileFormat] registering sorted time samples prim=%s attr=%s count=%zu\n",
                     field.primPath.GetText(), field.attrName.GetText(), field.timeSamples.size());
            fileData->RegisterLazyTimeSamplesSorted(field.primPath, field.attrName, field.typeName, std::move(samples));
        }
        else
        {
            TF_DEBUG(CAE_PYTHON_FILEFORMAT)
                .Msg("[PythonFileFormat] registering unsorted time samples prim=%s attr=%s count=%zu\n",
                     field.primPath.GetText(), field.attrName.GetText(), field.timeSamples.size());
            fileData->RegisterLazyTimeSamples(field.primPath, field.attrName, field.typeName, std::move(samples));
        }
    }
    return fileData;
}

} // namespace

// ---------------------------------------------------------------------------
// PythonFileFormatBase -- SdfFileFormat implementation
// ---------------------------------------------------------------------------

PythonFileFormatBase::PythonFileFormatBase(const PythonFileFormatConfig& config)
    : SdfFileFormat(
          TfToken(config.formatId), TfToken(config.version), TfToken(config.target), std::string(config.extension)),
      _config(config)
{
}

PythonFileFormatBase::~PythonFileFormatBase() = default;

bool PythonFileFormatBase::CanRead(const std::string& filePath) const
{
    if (!ExtensionMatches(TfGetExtension(filePath), _config))
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] CanRead('%s') -> false (extension mismatch for '%s')\n", filePath.c_str(),
                 _config.extension);
        return false;
    }

    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        const std::optional<bool> pythonResult = CallCanRead(_config, TfType::Find(*this), asset->LocalPath());
        const bool result = pythonResult.value_or(true);
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] CanRead('%s') -> %d (%s)\n", filePath.c_str(), result ? 1 : 0,
                 pythonResult.has_value() ? "python" : "extension");
        return result;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_DEBUG(CAE_PYTHON_FILEFORMAT)
            .Msg("[PythonFileFormat] CanRead('%s') resolver failure: %s\n", filePath.c_str(), ex.what());
        return false;
    }
}

bool PythonFileFormatBase::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    if (!TF_VERIFY(layer))
        return false;

    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    CaeResolverAssetPtr asset;
    SdfLayer::FileFormatArguments fmtArgs;
    try
    {
        asset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        fmtArgs = _config.deriveMountPathFromIdentifier ?
                      CaePrepareResolverArguments(identifier, layer->GetFileFormatArguments()) :
                      layer->GetFileFormatArguments();
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("PythonFileFormatBase: %s", ex.what());
        return false;
    }
    const std::string& localPath = asset->LocalPath();
    const TfType ownerType = TfType::Find(*this);
    TF_DEBUG(CAE_PYTHON_FILEFORMAT)
        .Msg("[PythonFileFormat] Read('%s') metadataOnly=%d\n", resolvedPath.c_str(), metadataOnly ? 1 : 0);

    // --- invoke Python read callback to author the structure layer ----------
    bool readOk = false;
    PythonReadResult readResult = CallRead(_config, ownerType, layer, fmtArgs, localPath, metadataOnly, &readOk);
    if (!readOk)
        return false;

    // --- Rebase the python-authored subtree to mountPath if set ------------
    // When mountPath is unset we leave the python module's authored layout
    // alone. When it is set, we relocate the python's default-prim subtree to
    // the requested leaf path, rewrite lazy-field primPaths, and author the
    // ancestor `over` chain so the layer composes cleanly via sublayer use.
    //
    if (!RebasePythonReadResult(layer, identifier, fmtArgs, &readResult))
        return false;

    // --- wire lazy loaders for any declared lazy fields ---------------------
    const PythonCallConfig callConfig = ResolvePythonCallConfig(_config, ownerType, &fmtArgs);
    CaeFileFormatDataRefPtr fileData = RegisterPythonLazyFields(readResult, callConfig, fmtArgs, localPath);

    // Every CAE file-format layer self-describes the canonical simulation-
    // seconds unit.
    layer->SetTimeCodesPerSecond(1.0);

    fileData->KeepAlive(asset);
    fileData->CopyFrom(_GetLayerData(*layer));
    SdfAbstractDataRefPtr data = fileData;
    _SetLayerData(layer, data);
    return true;
}

bool PythonFileFormatBase::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("PythonFileFormatBase is read-only; WriteToString is not supported.");
    return false;
}

bool PythonFileFormatBase::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("PythonFileFormatBase is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
