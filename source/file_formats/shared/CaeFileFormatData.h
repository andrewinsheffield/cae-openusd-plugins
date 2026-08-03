// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"
#include "OmniSciFileFormatSharedAPI.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/data.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/path.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @class CaeFileFormatData
 *
 * Combined file-format data backend. Ordinary scene structure is stored by
 * SdfData while registered heavy attributes are resolved lazily through the
 * overrides below.
 *
 * Callers register every potentially time-varying value as one or more time
 * samples. Each Loader is a self-contained callable that fetches the data and
 * returns a VtValue.
 *
 * CacheMode controls persistent value retention: All caches time samples,
 * while Static and None retain no sampled values. Metadata probes such as
 * QueryTimeSampleTypeid() must stay loader-free so OpenUSD's value-resolution
 * bookkeeping does not trigger heavy I/O before the actual value fetch.
 */
class OMNI_SCI_FILE_FORMAT_SHARED_TYPE CaeFileFormatData : public SdfData
{
public:
    enum class CacheMode
    {
        All,
        Static,
        None,
    };

    OMNI_SCI_FILE_FORMAT_SHARED_API explicit CaeFileFormatData(CacheMode cacheMode = CacheMode::All);
    OMNI_SCI_FILE_FORMAT_SHARED_API ~CaeFileFormatData() override;

    /// Self-contained callable that loads and returns the array value.
    using Loader = std::function<VtValue()>;

    static OMNI_SCI_FILE_FORMAT_SHARED_API const char* CacheModeArgName();
    static OMNI_SCI_FILE_FORMAT_SHARED_API CacheMode ParseCacheMode(const std::string& value);
    static OMNI_SCI_FILE_FORMAT_SHARED_API CacheMode ParseCacheMode(const SdfFileFormat::FileFormatArguments& args);

    // -------------------------------------------------------------------------
    // Registration API -- called while building the USD hierarchy.
    // -------------------------------------------------------------------------

    /// Register one source value as a time sample.
    ///
    /// Callers must request an explicit UsdTimeCode (or EarliestTime); USD
    /// default-time access intentionally remains unauthored.
    OMNI_SCI_FILE_FORMAT_SHARED_API void RegisterLazySingleState(
        const SdfPath& primPath, const TfToken& attrName, const TfToken& typeName, double time, Loader loader);

    OMNI_SCI_FILE_FORMAT_SHARED_API void RegisterLazyTimeSamples(const SdfPath& primPath,
                                                                 const TfToken& attrName,
                                                                 const TfToken& typeName,
                                                                 std::vector<std::pair<double, Loader>> samples);

    OMNI_SCI_FILE_FORMAT_SHARED_API void RegisterLazyTimeSamplesSorted(const SdfPath& primPath,
                                                                       const TfToken& attrName,
                                                                       const TfToken& typeName,
                                                                       std::vector<std::pair<double, Loader>> samples);

    /// Retain an external resource for as long as deferred loaders may use it.
    OMNI_SCI_FILE_FORMAT_SHARED_API void KeepAlive(std::shared_ptr<void> resource);

    // -------------------------------------------------------------------------
    // SdfAbstractData overrides
    // -------------------------------------------------------------------------

    OMNI_SCI_FILE_FORMAT_SHARED_API bool StreamsData() const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool IsDetached() const override;

    OMNI_SCI_FILE_FORMAT_SHARED_API void CreateSpec(const SdfPath& path, SdfSpecType specType) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool HasSpec(const SdfPath& path) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void EraseSpec(const SdfPath& path) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void MoveSpec(const SdfPath& oldPath, const SdfPath& newPath) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API SdfSpecType GetSpecType(const SdfPath& path) const override;

    OMNI_SCI_FILE_FORMAT_SHARED_API bool Has(const SdfPath& path,
                                             const TfToken& fieldName,
                                             SdfAbstractDataValue* value) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool Has(const SdfPath& path,
                                             const TfToken& fieldName,
                                             VtValue* value = nullptr) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool HasSpecAndField(const SdfPath& path,
                                                         const TfToken& fieldName,
                                                         SdfAbstractDataValue* value,
                                                         SdfSpecType* specType) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool HasSpecAndField(const SdfPath& path,
                                                         const TfToken& fieldName,
                                                         VtValue* value,
                                                         SdfSpecType* specType) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API VtValue Get(const SdfPath& path, const TfToken& fieldName) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API std::type_info const& GetTypeid(const SdfPath& path,
                                                                    const TfToken& fieldName) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void Set(const SdfPath& path, const TfToken& fieldName, const VtValue& value) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void Set(const SdfPath& path,
                                             const TfToken& fieldName,
                                             const SdfAbstractDataConstValue& value) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void Erase(const SdfPath& path, const TfToken& fieldName) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API std::vector<TfToken> List(const SdfPath& path) const override;

    OMNI_SCI_FILE_FORMAT_SHARED_API std::set<double> ListAllTimeSamples() const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API std::set<double> ListTimeSamplesForPath(const SdfPath& path) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool GetBracketingTimeSamples(double time,
                                                                  double* tLower,
                                                                  double* tUpper) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API size_t GetNumTimeSamplesForPath(const SdfPath& path) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool GetBracketingTimeSamplesForPath(const SdfPath& path,
                                                                         double time,
                                                                         double* tLower,
                                                                         double* tUpper) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool QueryTimeSample(const SdfPath& path,
                                                         double time,
                                                         VtValue* optionalValue = nullptr) const override;
    OMNI_SCI_FILE_FORMAT_SHARED_API bool QueryTimeSample(const SdfPath& path,
                                                         double time,
                                                         SdfAbstractDataValue* optionalValue) const override;
#if PXR_VERSION >= 2603
    /// Return registered sample type metadata without invoking the lazy loader.
    OMNI_SCI_FILE_FORMAT_SHARED_API const std::type_info& QueryTimeSampleTypeid(const SdfPath& path,
                                                                                double time) const override;
#endif
    OMNI_SCI_FILE_FORMAT_SHARED_API void SetTimeSample(const SdfPath& path, double time, const VtValue& value) override;
    OMNI_SCI_FILE_FORMAT_SHARED_API void EraseTimeSample(const SdfPath& path, double time) override;

protected:
    OMNI_SCI_FILE_FORMAT_SHARED_API void _VisitSpecs(SdfAbstractDataSpecVisitor* visitor) const override;

private:
    OMNI_SCI_FILE_FORMAT_SHARED_LOCAL VtValue _LoadLazyField(const Loader& loader) const;
    OMNI_SCI_FILE_FORMAT_SHARED_LOCAL bool _ShouldCacheDefaults() const;
    OMNI_SCI_FILE_FORMAT_SHARED_LOCAL bool _ShouldCacheTimeSamples() const;
    OMNI_SCI_FILE_FORMAT_SHARED_LOCAL std::type_info const& _GetFieldTypeid(const SdfPath& path,
                                                                            const TfToken& fieldName) const;
    OMNI_SCI_FILE_FORMAT_SHARED_LOCAL void _RegisterLazyTimeSamples(const SdfPath& primPath,
                                                                    const TfToken& attrName,
                                                                    const TfToken& typeName,
                                                                    std::vector<std::pair<double, Loader>> samples,
                                                                    bool samplesAreSorted);

    struct AttrInfo
    {
        TfToken typeName;
    };

    // All "over" prim paths, sorted for stable _VisitSpecs traversal.
    std::set<SdfPath> _overPaths;
    // Parent prim path -> ordered child name tokens.
    std::unordered_map<SdfPath, std::vector<TfToken>, SdfPath::Hash> _childrenMap;
    // Prim path -> ordered property name tokens.
    std::unordered_map<SdfPath, std::vector<TfToken>, SdfPath::Hash> _propertyChildren;
    // Attr path -> type metadata.
    std::unordered_map<SdfPath, AttrInfo, SdfPath::Hash> _attrInfos;

    using _LazyKey = SdfPath;
    std::unordered_map<_LazyKey, Loader, SdfPath::Hash> _lazyFields;
    std::unordered_map<_LazyKey, std::vector<std::pair<double, Loader>>, SdfPath::Hash> _lazyTimeSamples;
    std::vector<std::shared_ptr<void>> _retainedResources;

    CacheMode _cacheMode = CacheMode::All;

    mutable std::shared_mutex _cacheMutex;
    mutable std::unordered_map<_LazyKey, VtValue, SdfPath::Hash> _arrayCache;
    mutable std::unordered_map<_LazyKey, std::unordered_map<double, VtValue>, SdfPath::Hash> _timeSampleCache;
};

TF_DECLARE_WEAK_AND_REF_PTRS(CaeFileFormatData);

/// Creates a reference-counted lazy-data backend.
///
/// This centralizes the raw-pointer adoption required by OpenUSD's intrusive
/// TfRefPtr API so file-format readers do not manage ownership directly.
OMNI_SCI_FILE_FORMAT_SHARED_API CaeFileFormatDataRefPtr
CreateCaeFileFormatData(CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All);

PXR_NAMESPACE_CLOSE_SCOPE
