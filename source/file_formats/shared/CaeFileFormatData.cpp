// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CaeFileFormatData.h"

#include "CaeFileFormatDataDebugCodes.h"
#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/schema.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <cctype>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

PXR_NAMESPACE_OPEN_SCOPE

CaeFileFormatDataRefPtr CreateCaeFileFormatData(CaeFileFormatData::CacheMode cacheMode)
{
    // TfCreateRefPtr adopts a newly allocated TfRefBase instance. OpenUSD does
    // not provide a make-style alternative for this intrusive pointer API.
    return TfCreateRefPtr(new CaeFileFormatData(cacheMode)); // NOSONAR
}

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_FILE_FORMAT_DATA, "Lazy array data cache and loader diagnostics");
}

static const char* CacheModeName(CaeFileFormatData::CacheMode mode)
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

static size_t DebugThreadId()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

static std::string DebugValueTypeName(const VtValue& value)
{
    return value.IsEmpty() ? std::string("<empty>") : value.GetTypeName();
}

// ---------------------------------------------------------------------------
// Canonical cache path: normalise :chunk_N suffixes to :value so that all
// chunk paths share a single cache entry.
// ---------------------------------------------------------------------------
static SdfPath CanonicalCachePath(const SdfPath& path)
{
    if (!path.IsPropertyPath())
        return path;
    const std::string& name = path.GetName();
    const auto pos = name.rfind(":chunk_");
    if (pos != std::string::npos)
        return path.GetParentPath().AppendProperty(TfToken(name.substr(0, pos) + ":value"));
    return path;
}

// ---------------------------------------------------------------------------
// Ctor / dtor
// ---------------------------------------------------------------------------

CaeFileFormatData::CaeFileFormatData(CacheMode cacheMode) : _cacheMode(cacheMode)
{
    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] construct cacheMode=%s tid=%zu\n", static_cast<const void*>(this),
             CacheModeName(_cacheMode), DebugThreadId());
}

CaeFileFormatData::~CaeFileFormatData()
{
    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] destruct cacheMode=%s cachedDefaults=%zu cachedTimeSamplePaths=%zu tid=%zu\n",
             static_cast<const void*>(this), CacheModeName(_cacheMode), _arrayCache.size(), _timeSampleCache.size(),
             DebugThreadId());
}

void CaeFileFormatData::KeepAlive(std::shared_ptr<void> resource)
{
    if (resource)
        _retainedResources.push_back(std::move(resource));
}

const char* CaeFileFormatData::CacheModeArgName()
{
    return "cacheMode";
}

CaeFileFormatData::CacheMode CaeFileFormatData::ParseCacheMode(const std::string& value)
{
    std::string mode = value;
    std::transform(
        mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (mode.empty() || mode == "all")
        return CacheMode::All;
    if (mode == "static")
        return CacheMode::Static;
    if (mode == "none")
        return CacheMode::None;

    TF_WARN(
        "CaeFileFormatData: invalid cacheMode '%s'; expected 'all', 'static', or 'none'. Using 'all'.", value.c_str());
    return CacheMode::All;
}

CaeFileFormatData::CacheMode CaeFileFormatData::ParseCacheMode(const SdfFileFormat::FileFormatArguments& args)
{
    const auto it = args.find(CacheModeArgName());
    return ParseCacheMode(it == args.end() ? std::string() : it->second);
}

// ---------------------------------------------------------------------------
// RegisterLazyTimeSamples
//
// Lazy attrs are intentionally NOT added to _propertyChildren so that
// prim.GetAuthoredProperties() will not see them.  USD value resolution
// still works because HasSpec / GetSpecType / Has / Get do not consult
// PropertyChildren.
// ---------------------------------------------------------------------------

static void _AddUnique(std::vector<TfToken>& vec, const TfToken& tok)
{
    if (std::find(vec.begin(), vec.end(), tok) == vec.end())
        vec.push_back(tok);
}

using TimeSampleList = std::vector<std::pair<double, CaeFileFormatData::Loader>>;

static TimeSampleList::const_iterator FindTimeSample(const TimeSampleList& samples, double time)
{
    auto it = std::lower_bound(
        samples.begin(), samples.end(), time, [](const auto& sample, double t) { return sample.first < t; });
    return (it != samples.end() && it->first == time) ? it : samples.end();
}

void CaeFileFormatData::RegisterLazySingleState(
    const SdfPath& primPath, const TfToken& attrName, const TfToken& typeName, double time, Loader loader)
{
    std::vector<std::pair<double, Loader>> samples;
    samples.emplace_back(time, std::move(loader));
    RegisterLazyTimeSamplesSorted(primPath, attrName, typeName, std::move(samples));
}

void CaeFileFormatData::RegisterLazyTimeSamples(const SdfPath& primPath,
                                                const TfToken& attrName,
                                                const TfToken& typeName,
                                                std::vector<std::pair<double, Loader>> samples)
{
    _RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples), false);
}

void CaeFileFormatData::RegisterLazyTimeSamplesSorted(const SdfPath& primPath,
                                                      const TfToken& attrName,
                                                      const TfToken& typeName,
                                                      std::vector<std::pair<double, Loader>> samples)
{
    _RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples), true);
}

void CaeFileFormatData::_RegisterLazyTimeSamples(const SdfPath& primPath,
                                                 const TfToken& attrName,
                                                 const TfToken& typeName,
                                                 std::vector<std::pair<double, Loader>> samples,
                                                 bool samplesAreSorted)
{
    for (SdfPath p = primPath; p != SdfPath::AbsoluteRootPath(); p = p.GetParentPath())
    {
        if (_overPaths.insert(p).second)
            _AddUnique(_childrenMap[p.GetParentPath()], p.GetNameToken());
    }

    SdfPath attrPath = primPath.AppendProperty(attrName);
    const SdfPath canonical = CanonicalCachePath(attrPath);
    _attrInfos[attrPath] = { typeName };

    if (!samplesAreSorted)
        std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const size_t sampleCount = samples.size();
    _lazyTimeSamples[canonical] = std::move(samples);

    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] register time samples prim=%s attr=%s type=%s canonical=%s samples=%zu lazyTimeSamples=%zu overPaths=%zu tid=%zu\n",
             static_cast<const void*>(this), primPath.GetText(), attrName.GetText(), typeName.GetText(),
             canonical.GetText(), sampleCount, _lazyTimeSamples.size(), _overPaths.size(), DebugThreadId());
}

// ---------------------------------------------------------------------------
// _LoadLazyField
// ---------------------------------------------------------------------------

VtValue CaeFileFormatData::_LoadLazyField(const Loader& loader) const
{
    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] load begin tid=%zu\n", static_cast<const void*>(this), DebugThreadId());

    // Release GIL before potentially blocking call to read heavy data.
    TF_PY_ALLOW_THREADS_IN_SCOPE();
    VtValue value = std::invoke(loader);

    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] load end type=%s tid=%zu\n", static_cast<const void*>(this),
             DebugValueTypeName(value).c_str(), DebugThreadId());
    return value;
}

bool CaeFileFormatData::_ShouldCacheDefaults() const
{
    return _cacheMode == CacheMode::All || _cacheMode == CacheMode::Static;
}

bool CaeFileFormatData::_ShouldCacheTimeSamples() const
{
    return _cacheMode == CacheMode::All;
}

// ---------------------------------------------------------------------------
// SdfAbstractData overrides
// ---------------------------------------------------------------------------

bool CaeFileFormatData::StreamsData() const
{
    return false;
}
bool CaeFileFormatData::IsDetached() const
{
    return true;
}

void CaeFileFormatData::CreateSpec(const SdfPath& path, SdfSpecType specType)
{
    SdfData::CreateSpec(path, specType);
}
void CaeFileFormatData::EraseSpec(const SdfPath& path)
{
    SdfData::EraseSpec(path);
}
void CaeFileFormatData::MoveSpec(const SdfPath& oldPath, const SdfPath& newPath)
{
    SdfData::MoveSpec(oldPath, newPath);
}
void CaeFileFormatData::Set(const SdfPath& path, const TfToken& fieldName, const VtValue& value)
{
    SdfData::Set(path, fieldName, value);
}
void CaeFileFormatData::Set(const SdfPath& path, const TfToken& fieldName, const SdfAbstractDataConstValue& value)
{
    SdfData::Set(path, fieldName, value);
}
void CaeFileFormatData::Erase(const SdfPath& path, const TfToken& fieldName)
{
    SdfData::Erase(path, fieldName);
}
void CaeFileFormatData::SetTimeSample(const SdfPath& path, double time, const VtValue& value)
{
    SdfData::SetTimeSample(path, time, value);
}
void CaeFileFormatData::EraseTimeSample(const SdfPath& path, double time)
{
    SdfData::EraseTimeSample(path, time);
}

static const std::vector<TfToken> _emptyTokenVec;

bool CaeFileFormatData::HasSpec(const SdfPath& path) const
{
    if (SdfData::HasSpec(path))
        return true;
    if (path == SdfPath::AbsoluteRootPath())
        return true;
    return _overPaths.count(path) || _attrInfos.count(path);
}

SdfSpecType CaeFileFormatData::GetSpecType(const SdfPath& path) const
{
    const SdfSpecType storedType = SdfData::GetSpecType(path);
    if (storedType != SdfSpecTypeUnknown)
        return storedType;
    if (path == SdfPath::AbsoluteRootPath())
        return SdfSpecTypePseudoRoot;
    if (_attrInfos.count(path))
        return SdfSpecTypeAttribute;
    if (_overPaths.count(path))
        return SdfSpecTypePrim;
    return SdfSpecTypeUnknown;
}

bool CaeFileFormatData::Has(const SdfPath& path, const TfToken& fieldName, SdfAbstractDataValue* value) const
{
    const bool lazyValueField =
        _attrInfos.count(path) && (fieldName == SdfFieldKeys->Default || fieldName == SdfFieldKeys->TimeSamples);
    if (!lazyValueField && SdfData::Has(path, fieldName, value))
        return true;

    if (path == SdfPath::AbsoluteRootPath())
    {
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            std::vector<TfToken> children = it != _childrenMap.end() ? it->second : std::vector<TfToken>{};
            if (value)
                return value->StoreValue(VtValue(children));
            return true;
        }
        // Every CAE file-format layer self-describes the canonical simulation-
        // seconds unit. Report TCPS=1.0 so USD's per-arc rescaling does not
        // treat the layer as the default 24 fps and divide its samples by 24.
        if (fieldName == SdfFieldKeys->TimeCodesPerSecond)
        {
            if (value)
                return value->StoreValue(VtValue(1.0));
            return true;
        }
        return false;
    }

    if (_overPaths.count(path) && !_attrInfos.count(path))
    {
        if (fieldName == SdfFieldKeys->Specifier)
        {
            if (value)
                return value->StoreValue(VtValue(SdfSpecifierOver));
            return true;
        }
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            if (it != _childrenMap.end())
            {
                if (value)
                    return value->StoreValue(VtValue(it->second));
                return true;
            }
            return false;
        }
        if (fieldName == SdfChildrenKeys->PropertyChildren)
        {
            auto it = _propertyChildren.find(path);
            if (it != _propertyChildren.end() && !it->second.empty())
            {
                if (value)
                    return value->StoreValue(VtValue(it->second));
                return true;
            }
            return false;
        }
        return false;
    }

    auto infoIt = _attrInfos.find(path);
    if (infoIt != _attrInfos.end())
    {
        if (fieldName == SdfFieldKeys->TypeName)
        {
            if (value)
                return value->StoreValue(VtValue(infoIt->second.typeName));
            return true;
        }
        if (fieldName == SdfFieldKeys->Variability)
        {
            if (value)
                return value->StoreValue(VtValue(SdfVariabilityVarying));
            return true;
        }
        if (fieldName == SdfFieldKeys->Custom)
        {
            if (value)
                return value->StoreValue(VtValue(false));
            return true;
        }
        if (fieldName == SdfFieldKeys->TimeSamples)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            if (_lazyTimeSamples.count(canonical))
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) timeSamples path=%s canonical=%s value=%p tid=%zu\n",
                         static_cast<const void*>(this), path.GetText(), canonical.GetText(), static_cast<void*>(value),
                         DebugThreadId());
                if (value)
                    return value->StoreValue(VtValue(SdfTimeSampleMap{}));
                return true;
            }
            return false;
        }
        if (fieldName == SdfFieldKeys->Default)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            const bool cacheDefaults = _ShouldCacheDefaults();
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default begin path=%s canonical=%s value=%p cacheDefaults=%d tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), static_cast<void*>(value),
                     cacheDefaults ? 1 : 0, DebugThreadId());
            if (cacheDefaults)
            {
                std::shared_lock lk(_cacheMutex);
                auto it = _arrayCache.find(canonical);
                if (it != _arrayCache.end())
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default cache hit canonical=%s type=%s cacheSize=%zu tid=%zu\n",
                             static_cast<const void*>(this), canonical.GetText(),
                             DebugValueTypeName(it->second).c_str(), _arrayCache.size(), DebugThreadId());
                    if (value)
                        return value->StoreValue(it->second);
                    return true;
                }
            }
            auto lazyIt = _lazyFields.find(canonical);
            if (lazyIt != _lazyFields.end())
            {
                if (value)
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default load path=%s canonical=%s tid=%zu\n",
                             static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                    VtValue v = _LoadLazyField(lazyIt->second);
                    if (cacheDefaults)
                    {
                        std::unique_lock lk(_cacheMutex);
                        TF_DEBUG(CAE_FILE_FORMAT_DATA)
                            .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default cache insert begin canonical=%s loadedType=%s cacheSize=%zu tid=%zu\n",
                                 static_cast<const void*>(this), canonical.GetText(), DebugValueTypeName(v).c_str(),
                                 _arrayCache.size(), DebugThreadId());
                        const auto inserted = _arrayCache.emplace(canonical, std::move(v));
                        TF_DEBUG(CAE_FILE_FORMAT_DATA)
                            .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default cache insert end canonical=%s inserted=%d storedType=%s cacheSize=%zu tid=%zu\n",
                                 static_cast<const void*>(this), canonical.GetText(), inserted.second ? 1 : 0,
                                 DebugValueTypeName(inserted.first->second).c_str(), _arrayCache.size(), DebugThreadId());
                        return value->StoreValue(inserted.first->second);
                    }
                    return value->StoreValue(v);
                }
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default loader exists path=%s canonical=%s value=null tid=%zu\n",
                         static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                return true;
            }
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Has(SdfAbstractDataValue*) default no loader path=%s canonical=%s tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
        }
    }
    return false;
}

bool CaeFileFormatData::Has(const SdfPath& path, const TfToken& fieldName, VtValue* value) const
{
    const bool lazyValueField =
        _attrInfos.count(path) && (fieldName == SdfFieldKeys->Default || fieldName == SdfFieldKeys->TimeSamples);
    if (!lazyValueField && SdfData::Has(path, fieldName, value))
        return true;

    if (path == SdfPath::AbsoluteRootPath())
    {
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            std::vector<TfToken> children = it != _childrenMap.end() ? it->second : std::vector<TfToken>{};
            if (value)
                *value = VtValue(children);
            return true;
        }
        if (fieldName == SdfFieldKeys->TimeCodesPerSecond)
        {
            if (value)
                *value = VtValue(1.0);
            return true;
        }
        return false;
    }

    if (_overPaths.count(path) && !_attrInfos.count(path))
    {
        if (fieldName == SdfFieldKeys->Specifier)
        {
            if (value)
                *value = VtValue(SdfSpecifierOver);
            return true;
        }
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            if (it != _childrenMap.end())
            {
                if (value)
                    *value = VtValue(it->second);
                return true;
            }
            return false;
        }
        if (fieldName == SdfChildrenKeys->PropertyChildren)
        {
            auto it = _propertyChildren.find(path);
            if (it != _propertyChildren.end() && !it->second.empty())
            {
                if (value)
                    *value = VtValue(it->second);
                return true;
            }
            return false;
        }
        return false;
    }

    auto infoIt = _attrInfos.find(path);
    if (infoIt != _attrInfos.end())
    {
        if (fieldName == SdfFieldKeys->TypeName)
        {
            if (value)
                *value = VtValue(infoIt->second.typeName);
            return true;
        }
        if (fieldName == SdfFieldKeys->Variability)
        {
            if (value)
                *value = VtValue(SdfVariabilityVarying);
            return true;
        }
        if (fieldName == SdfFieldKeys->Custom)
        {
            if (value)
                *value = VtValue(false);
            return true;
        }
        if (fieldName == SdfFieldKeys->TimeSamples)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            if (_lazyTimeSamples.count(canonical))
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] Has(VtValue*) timeSamples path=%s canonical=%s value=%p tid=%zu\n",
                         static_cast<const void*>(this), path.GetText(), canonical.GetText(), static_cast<void*>(value),
                         DebugThreadId());
                if (value)
                    *value = VtValue(SdfTimeSampleMap{});
                return true;
            }
            return false;
        }
        if (fieldName == SdfFieldKeys->Default)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            const bool cacheDefaults = _ShouldCacheDefaults();
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Has(VtValue*) default begin path=%s canonical=%s value=%p cacheDefaults=%d tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), static_cast<void*>(value),
                     cacheDefaults ? 1 : 0, DebugThreadId());
            if (cacheDefaults)
            {
                std::shared_lock lk(_cacheMutex);
                auto it = _arrayCache.find(canonical);
                if (it != _arrayCache.end())
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Has(VtValue*) default cache hit canonical=%s type=%s cacheSize=%zu tid=%zu\n",
                             static_cast<const void*>(this), canonical.GetText(),
                             DebugValueTypeName(it->second).c_str(), _arrayCache.size(), DebugThreadId());
                    if (value)
                        *value = it->second;
                    return true;
                }
            }
            auto lazyIt = _lazyFields.find(canonical);
            if (lazyIt != _lazyFields.end())
            {
                if (value)
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Has(VtValue*) default load path=%s canonical=%s tid=%zu\n",
                             static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                    VtValue v = _LoadLazyField(lazyIt->second);
                    if (cacheDefaults)
                    {
                        std::unique_lock lk(_cacheMutex);
                        TF_DEBUG(CAE_FILE_FORMAT_DATA)
                            .Msg("[CaeFileFormatData:%p] Has(VtValue*) default cache insert begin canonical=%s loadedType=%s cacheSize=%zu tid=%zu\n",
                                 static_cast<const void*>(this), canonical.GetText(), DebugValueTypeName(v).c_str(),
                                 _arrayCache.size(), DebugThreadId());
                        const auto inserted = _arrayCache.emplace(canonical, std::move(v));
                        TF_DEBUG(CAE_FILE_FORMAT_DATA)
                            .Msg("[CaeFileFormatData:%p] Has(VtValue*) default cache insert end canonical=%s inserted=%d storedType=%s cacheSize=%zu tid=%zu\n",
                                 static_cast<const void*>(this), canonical.GetText(), inserted.second ? 1 : 0,
                                 DebugValueTypeName(inserted.first->second).c_str(), _arrayCache.size(), DebugThreadId());
                        *value = inserted.first->second;
                    }
                    else
                    {
                        *value = std::move(v);
                    }
                }
                else
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Has(VtValue*) default loader exists path=%s canonical=%s value=null tid=%zu\n",
                             static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                }
                return true;
            }
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Has(VtValue*) default no loader path=%s canonical=%s tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
        }
    }
    return false;
}

bool CaeFileFormatData::HasSpecAndField(const SdfPath& path,
                                        const TfToken& fieldName,
                                        SdfAbstractDataValue* value,
                                        SdfSpecType* specType) const
{
    const SdfSpecType resolvedSpecType = GetSpecType(path);
    if (specType)
        *specType = resolvedSpecType;
    return resolvedSpecType != SdfSpecTypeUnknown && Has(path, fieldName, value);
}

bool CaeFileFormatData::HasSpecAndField(const SdfPath& path,
                                        const TfToken& fieldName,
                                        VtValue* value,
                                        SdfSpecType* specType) const
{
    const SdfSpecType resolvedSpecType = GetSpecType(path);
    if (specType)
        *specType = resolvedSpecType;
    return resolvedSpecType != SdfSpecTypeUnknown && Has(path, fieldName, value);
}

VtValue CaeFileFormatData::Get(const SdfPath& path, const TfToken& fieldName) const
{
    VtValue storedValue;
    const bool lazyValueField =
        _attrInfos.count(path) && (fieldName == SdfFieldKeys->Default || fieldName == SdfFieldKeys->TimeSamples);
    if (!lazyValueField && SdfData::Has(path, fieldName, &storedValue))
        return storedValue;

    if (path == SdfPath::AbsoluteRootPath())
    {
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            const std::vector<TfToken>& children = it != _childrenMap.end() ? it->second : _emptyTokenVec;
            return VtValue(children);
        }
        if (fieldName == SdfFieldKeys->TimeCodesPerSecond)
            return VtValue(1.0);
        return VtValue();
    }

    if (_overPaths.count(path) && !_attrInfos.count(path))
    {
        if (fieldName == SdfFieldKeys->Specifier)
            return VtValue(SdfSpecifierOver);
        if (fieldName == SdfChildrenKeys->PrimChildren)
        {
            auto it = _childrenMap.find(path);
            if (it != _childrenMap.end())
                return VtValue(it->second);
            return VtValue();
        }
        if (fieldName == SdfChildrenKeys->PropertyChildren)
        {
            auto it = _propertyChildren.find(path);
            if (it != _propertyChildren.end())
                return VtValue(it->second);
            return VtValue();
        }
        return VtValue();
    }

    auto infoIt = _attrInfos.find(path);
    if (infoIt != _attrInfos.end())
    {
        if (fieldName == SdfFieldKeys->TypeName)
            return VtValue(infoIt->second.typeName);
        if (fieldName == SdfFieldKeys->Variability)
            return VtValue(SdfVariabilityVarying);
        if (fieldName == SdfFieldKeys->Custom)
            return VtValue(false);
        if (fieldName == SdfFieldKeys->TimeSamples)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            if (_lazyTimeSamples.count(canonical))
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] Get timeSamples path=%s canonical=%s tid=%zu\n",
                         static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                return VtValue(SdfTimeSampleMap{});
            }
        }
        if (fieldName == SdfFieldKeys->Default)
        {
            const SdfPath canonical = CanonicalCachePath(path);
            const bool cacheDefaults = _ShouldCacheDefaults();
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Get default begin path=%s canonical=%s cacheDefaults=%d tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), cacheDefaults ? 1 : 0,
                     DebugThreadId());
            if (cacheDefaults)
            {
                std::shared_lock lk(_cacheMutex);
                auto it = _arrayCache.find(canonical);
                if (it != _arrayCache.end())
                {
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Get default cache hit canonical=%s type=%s cacheSize=%zu tid=%zu\n",
                             static_cast<const void*>(this), canonical.GetText(),
                             DebugValueTypeName(it->second).c_str(), _arrayCache.size(), DebugThreadId());
                    return it->second;
                }
            }
            auto lazyIt = _lazyFields.find(canonical);
            if (lazyIt != _lazyFields.end())
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] Get default load path=%s canonical=%s tid=%zu\n",
                         static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
                VtValue v = _LoadLazyField(lazyIt->second);
                if (cacheDefaults)
                {
                    std::unique_lock lk(_cacheMutex);
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Get default cache insert begin canonical=%s loadedType=%s cacheSize=%zu tid=%zu\n",
                             static_cast<const void*>(this), canonical.GetText(), DebugValueTypeName(v).c_str(),
                             _arrayCache.size(), DebugThreadId());
                    const auto inserted = _arrayCache.emplace(canonical, std::move(v));
                    TF_DEBUG(CAE_FILE_FORMAT_DATA)
                        .Msg("[CaeFileFormatData:%p] Get default cache insert end canonical=%s inserted=%d storedType=%s cacheSize=%zu tid=%zu\n",
                             static_cast<const void*>(this), canonical.GetText(), inserted.second ? 1 : 0,
                             DebugValueTypeName(inserted.first->second).c_str(), _arrayCache.size(), DebugThreadId());
                    return inserted.first->second;
                }
                return v;
            }
            TF_DEBUG(CAE_FILE_FORMAT_DATA)
                .Msg("[CaeFileFormatData:%p] Get default no loader path=%s canonical=%s tid=%zu\n",
                     static_cast<const void*>(this), path.GetText(), canonical.GetText(), DebugThreadId());
        }
    }
    return VtValue();
}

std::type_info const& CaeFileFormatData::GetTypeid(const SdfPath& path, const TfToken& fieldName) const
{
    if (_attrInfos.count(path) && fieldName == SdfFieldKeys->Default)
        return _GetFieldTypeid(path, fieldName);
    return SdfAbstractData::GetTypeid(path, fieldName);
}

std::type_info const& CaeFileFormatData::_GetFieldTypeid(const SdfPath& path, const TfToken& fieldName) const
{
    if (fieldName == SdfFieldKeys->Default)
    {
        auto checkType = [](const TfToken& sdfTypeName) -> std::type_info const&
        {
            if (sdfTypeName == TfToken("float[]"))
                return typeid(VtArray<float>);
            if (sdfTypeName == TfToken("double[]"))
                return typeid(VtArray<double>);
            if (sdfTypeName == TfToken("int[]"))
                return typeid(VtArray<int>);
            if (sdfTypeName == TfToken("uint[]"))
                return typeid(VtArray<unsigned int>);
            if (sdfTypeName == TfToken("uchar[]"))
                return typeid(VtArray<unsigned char>);
            if (sdfTypeName == TfToken("float2[]"))
                return typeid(VtArray<GfVec2f>);
            if (sdfTypeName == TfToken("float3[]"))
                return typeid(VtArray<GfVec3f>);
            if (sdfTypeName == TfToken("float4[]"))
                return typeid(VtArray<GfVec4f>);
            if (sdfTypeName == TfToken("double2[]"))
                return typeid(VtArray<GfVec2d>);
            if (sdfTypeName == TfToken("double3[]"))
                return typeid(VtArray<GfVec3d>);
            if (sdfTypeName == TfToken("double4[]"))
                return typeid(VtArray<GfVec4d>);
            if (sdfTypeName == TfToken("int2[]"))
                return typeid(VtArray<GfVec2i>);
            if (sdfTypeName == TfToken("int3[]"))
                return typeid(VtArray<GfVec3i>);
            if (sdfTypeName == TfToken("int4[]"))
                return typeid(VtArray<GfVec4i>);
            if (sdfTypeName == TfToken("uint64[]"))
                return typeid(VtArray<uint64_t>);
            return typeid(VtArray<int64_t>);
        };
        auto attrIt = _attrInfos.find(path);
        if (attrIt != _attrInfos.end())
            return checkType(attrIt->second.typeName);
    }
    return typeid(void);
}

std::vector<TfToken> CaeFileFormatData::List(const SdfPath& path) const
{
    std::vector<TfToken> storedFields = SdfData::List(path);
    const auto addStoredUnique = [&storedFields](const TfToken& field)
    {
        if (std::find(storedFields.begin(), storedFields.end(), field) == storedFields.end())
            storedFields.push_back(field);
    };

    if (path == SdfPath::AbsoluteRootPath())
    {
        addStoredUnique(SdfFieldKeys->TimeCodesPerSecond);
        if (_childrenMap.count(path))
            addStoredUnique(SdfChildrenKeys->PrimChildren);
        return storedFields;
    }
    if (_attrInfos.count(path))
    {
        addStoredUnique(SdfFieldKeys->TypeName);
        addStoredUnique(SdfFieldKeys->Variability);
        addStoredUnique(SdfFieldKeys->Custom);
        const SdfPath canonical = CanonicalCachePath(path);
        if (_lazyTimeSamples.count(canonical))
            addStoredUnique(SdfFieldKeys->TimeSamples);
        if (_lazyFields.count(canonical))
            addStoredUnique(SdfFieldKeys->Default);
        return storedFields;
    }
    if (_overPaths.count(path))
    {
        addStoredUnique(SdfFieldKeys->Specifier);
        if (_childrenMap.count(path))
            addStoredUnique(SdfChildrenKeys->PrimChildren);
        auto it = _propertyChildren.find(path);
        if (it != _propertyChildren.end() && !it->second.empty())
            addStoredUnique(SdfChildrenKeys->PropertyChildren);
        return storedFields;
    }
    return storedFields;
}

// ---------------------------------------------------------------------------
// Time-sample overrides
// ---------------------------------------------------------------------------

std::set<double> CaeFileFormatData::ListAllTimeSamples() const
{
    std::set<double> times = SdfData::ListAllTimeSamples();
    for (const auto& kv : _lazyTimeSamples)
        for (const auto& [t, _] : kv.second)
            times.insert(t);
    return times;
}

std::set<double> CaeFileFormatData::ListTimeSamplesForPath(const SdfPath& path) const
{
    std::set<double> times = SdfData::ListTimeSamplesForPath(path);
    auto it = _lazyTimeSamples.find(CanonicalCachePath(path));
    if (it != _lazyTimeSamples.end())
    {
        for (const auto& [t, _] : it->second)
            times.insert(t);
    }
    return times;
}

bool CaeFileFormatData::GetBracketingTimeSamples(double time, double* tLower, double* tUpper) const
{
    const std::set<double> samples = ListAllTimeSamples();
    if (samples.empty())
        return false;

    auto upper = samples.lower_bound(time);
    if (upper == samples.end())
        upper = std::prev(samples.end());
    auto lower = upper;
    if (*upper > time && upper != samples.begin())
        lower = std::prev(upper);
    if (tLower)
        *tLower = *lower;
    if (tUpper)
        *tUpper = *upper;
    return true;
}

size_t CaeFileFormatData::GetNumTimeSamplesForPath(const SdfPath& path) const
{
    return ListTimeSamplesForPath(path).size();
}

bool CaeFileFormatData::GetBracketingTimeSamplesForPath(const SdfPath& path, double time, double* tLower, double* tUpper) const
{
    auto it = _lazyTimeSamples.find(CanonicalCachePath(path));
    if (it != _lazyTimeSamples.end() && !it->second.empty())
    {
        const auto& samples = it->second;
        auto upper =
            std::lower_bound(samples.begin(), samples.end(), time, [](const auto& s, double t) { return s.first < t; });
        if (upper == samples.end())
        {
            if (tLower)
                *tLower = samples.back().first;
            if (tUpper)
                *tUpper = samples.back().first;
        }
        else if (upper == samples.begin())
        {
            if (tLower)
                *tLower = samples.front().first;
            if (tUpper)
                *tUpper = samples.front().first;
        }
        else if (upper->first == time)
        {
            if (tLower)
                *tLower = upper->first;
            if (tUpper)
                *tUpper = upper->first;
        }
        else
        {
            auto lower = std::prev(upper);
            if (tLower)
                *tLower = lower->first;
            if (tUpper)
                *tUpper = upper->first;
        }
        return true;
    }
    if (tLower)
        *tLower = 0.0;
    if (tUpper)
        *tUpper = 0.0;
    return SdfData::GetBracketingTimeSamplesForPath(path, time, tLower, tUpper);
}

bool CaeFileFormatData::QueryTimeSample(const SdfPath& path, double time, VtValue* optionalValue) const
{
    const SdfPath canonical = CanonicalCachePath(path);
    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) begin path=%s canonical=%s time=%.17g value=%p tid=%zu\n",
             static_cast<const void*>(this), path.GetText(), canonical.GetText(), time,
             static_cast<void*>(optionalValue), DebugThreadId());
    auto tsIt = _lazyTimeSamples.find(canonical);
    if (tsIt == _lazyTimeSamples.end())
    {
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) no samples canonical=%s tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), DebugThreadId());
        return SdfData::QueryTimeSample(path, time, optionalValue);
    }

    auto sampleIt = FindTimeSample(tsIt->second, time);
    if (sampleIt == tsIt->second.end())
    {
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) missing time canonical=%s time=%.17g sampleCount=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, tsIt->second.size(), DebugThreadId());
        return false;
    }

    const bool cacheTimeSamples = _ShouldCacheTimeSamples();
    if (cacheTimeSamples)
    {
        std::shared_lock lk(_cacheMutex);
        auto cIt = _timeSampleCache.find(canonical);
        if (cIt != _timeSampleCache.end())
        {
            auto vIt = cIt->second.find(time);
            if (vIt != cIt->second.end())
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) cache hit canonical=%s time=%.17g type=%s cachedTimes=%zu tid=%zu\n",
                         static_cast<const void*>(this), canonical.GetText(), time,
                         DebugValueTypeName(vIt->second).c_str(), cIt->second.size(), DebugThreadId());
                if (optionalValue)
                    *optionalValue = vIt->second;
                return true;
            }
        }
    }

    VtValue v = _LoadLazyField(sampleIt->second);
    if (cacheTimeSamples)
    {
        std::unique_lock lk(_cacheMutex);
        auto& samplesForPath = _timeSampleCache[canonical];
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) cache write begin canonical=%s time=%.17g type=%s cachedTimes=%zu cachedPaths=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, DebugValueTypeName(v).c_str(),
                 samplesForPath.size(), _timeSampleCache.size(), DebugThreadId());
        samplesForPath[time] = v;
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(VtValue*) cache write end canonical=%s time=%.17g cachedTimes=%zu cachedPaths=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, samplesForPath.size(),
                 _timeSampleCache.size(), DebugThreadId());
    }
    if (optionalValue)
        *optionalValue = v;
    return true;
}

bool CaeFileFormatData::QueryTimeSample(const SdfPath& path, double time, SdfAbstractDataValue* optionalValue) const
{
    const SdfPath canonical = CanonicalCachePath(path);
    TF_DEBUG(CAE_FILE_FORMAT_DATA)
        .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) begin path=%s canonical=%s time=%.17g value=%p tid=%zu\n",
             static_cast<const void*>(this), path.GetText(), canonical.GetText(), time,
             static_cast<void*>(optionalValue), DebugThreadId());
    auto tsIt = _lazyTimeSamples.find(canonical);
    if (tsIt == _lazyTimeSamples.end())
    {
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) no samples canonical=%s tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), DebugThreadId());
        return SdfData::QueryTimeSample(path, time, optionalValue);
    }

    auto sampleIt = FindTimeSample(tsIt->second, time);
    if (sampleIt == tsIt->second.end())
    {
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) missing time canonical=%s time=%.17g sampleCount=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, tsIt->second.size(), DebugThreadId());
        return false;
    }

    const bool cacheTimeSamples = _ShouldCacheTimeSamples();
    if (cacheTimeSamples)
    {
        std::shared_lock lk(_cacheMutex);
        auto cIt = _timeSampleCache.find(canonical);
        if (cIt != _timeSampleCache.end())
        {
            auto vIt = cIt->second.find(time);
            if (vIt != cIt->second.end())
            {
                TF_DEBUG(CAE_FILE_FORMAT_DATA)
                    .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) cache hit canonical=%s time=%.17g type=%s cachedTimes=%zu tid=%zu\n",
                         static_cast<const void*>(this), canonical.GetText(), time,
                         DebugValueTypeName(vIt->second).c_str(), cIt->second.size(), DebugThreadId());
                if (optionalValue)
                    return optionalValue->StoreValue(vIt->second);
                return true;
            }
        }
    }

    VtValue v = _LoadLazyField(sampleIt->second);
    if (cacheTimeSamples)
    {
        std::unique_lock lk(_cacheMutex);
        auto& samplesForPath = _timeSampleCache[canonical];
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) cache write begin canonical=%s time=%.17g type=%s cachedTimes=%zu cachedPaths=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, DebugValueTypeName(v).c_str(),
                 samplesForPath.size(), _timeSampleCache.size(), DebugThreadId());
        samplesForPath[time] = v;
        TF_DEBUG(CAE_FILE_FORMAT_DATA)
            .Msg("[CaeFileFormatData:%p] QueryTimeSample(SdfAbstractDataValue*) cache write end canonical=%s time=%.17g cachedTimes=%zu cachedPaths=%zu tid=%zu\n",
                 static_cast<const void*>(this), canonical.GetText(), time, samplesForPath.size(),
                 _timeSampleCache.size(), DebugThreadId());
    }
    if (optionalValue)
        return optionalValue->StoreValue(v);
    return true;
}

#if PXR_VERSION >= 2603
const std::type_info& CaeFileFormatData::QueryTimeSampleTypeid(const SdfPath& path, double time) const
{
    // OpenUSD can ask for a time sample's type more than once while resolving
    // one UsdAttribute::Get(). SdfAbstractData's default implementation loads
    // the VtValue and returns its type, which would make cacheMode=static/none
    // perform duplicate heavy I/O. We already know the registered USD type, so
    // answer the probe from metadata and leave QueryTimeSample() as the only
    // path that materializes the array.
    const SdfPath canonical = CanonicalCachePath(path);
    auto tsIt = _lazyTimeSamples.find(canonical);
    if (tsIt == _lazyTimeSamples.end())
        return SdfAbstractData::QueryTimeSampleTypeid(path, time);

    auto sampleIt = FindTimeSample(tsIt->second, time);
    if (sampleIt == tsIt->second.end())
        return typeid(void);

    return _GetFieldTypeid(path, SdfFieldKeys->Default);
}
#endif

// ---------------------------------------------------------------------------
// _VisitSpecs
// ---------------------------------------------------------------------------

void CaeFileFormatData::_VisitSpecs(SdfAbstractDataSpecVisitor* visitor) const
{
    class UnionVisitor final : public SdfAbstractDataSpecVisitor
    {
    public:
        UnionVisitor(const CaeFileFormatData& owner, SdfAbstractDataSpecVisitor* target)
            : _owner(owner), _target(target)
        {
        }

        bool VisitSpec(const SdfAbstractData&, const SdfPath& path) override
        {
            if (!_continue || !_visited.insert(path).second)
                return _continue;
            _continue = _target->VisitSpec(_owner, path);
            return _continue;
        }

        void Done(const SdfAbstractData&) override
        {
            // This adapter is passed only to SdfData::_VisitSpecs(), which
            // does not call Done(). The outer SdfAbstractData::VisitSpecs()
            // call notifies the target once after the union is complete.
        }

        bool ShouldContinue() const
        {
            return _continue;
        }

    private:
        const CaeFileFormatData& _owner;
        SdfAbstractDataSpecVisitor* _target;
        std::set<SdfPath> _visited;
        bool _continue = true;
    };

    UnionVisitor unionVisitor(*this, visitor);
    SdfData::_VisitSpecs(&unionVisitor);
    if (!unionVisitor.ShouldContinue())
        return;

    if (!unionVisitor.VisitSpec(*this, SdfPath::AbsoluteRootPath()))
        return;
    for (const SdfPath& path : _overPaths)
    {
        if (!unionVisitor.VisitSpec(*this, path))
            return;
    }
    for (const auto& [path, _] : _attrInfos)
        if (!unionVisitor.VisitSpec(*this, path))
            return;
}

PXR_NAMESPACE_CLOSE_SCOPE
