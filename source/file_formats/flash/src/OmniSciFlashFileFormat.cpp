// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciFlashFileFormat.h"

#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
#include "DisablePXRWarnings.h"
#include "DynamicFileFormatArguments.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSciFileFormatArgs/tokens.h>
#include <omniSciFlash/amrAPI.h>
#include <omniSciFlash/tokens.h>
CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/stage.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <hdf5.h>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace fs = std::filesystem;

namespace detail
{

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 4> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciFlashFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciFlashFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciFlashFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciFlashFileFormatTokens->ArgTimeSource },
        } };

    return DynamicFileFormatArgs;
}

class H5Handle
{
public:
    using Closer = herr_t (*)(hid_t);

    H5Handle() = default;
    H5Handle(hid_t id, Closer closer) : _id(id), _closer(closer)
    {
    }
    H5Handle(const H5Handle&) = delete;
    H5Handle& operator=(const H5Handle&) = delete;
    H5Handle(H5Handle&& other) noexcept : _id(other._id), _closer(other._closer)
    {
        other._id = -1;
        other._closer = nullptr;
    }
    H5Handle& operator=(H5Handle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _id = other._id;
            _closer = other._closer;
            other._id = -1;
            other._closer = nullptr;
        }
        return *this;
    }
    ~H5Handle()
    {
        Reset();
    }

    explicit operator bool() const
    {
        return _id >= 0;
    }
    operator hid_t() const
    {
        return _id;
    }

private:
    void Reset()
    {
        if (_id >= 0 && _closer)
            _closer(_id);
        _id = -1;
        _closer = nullptr;
    }

    hid_t _id = -1;
    Closer _closer = nullptr;
};

static std::mutex& GetHdf5Mutex()
{
    static std::mutex mutex;
    return mutex;
}

static H5Handle OpenFile(const std::string& path)
{
    hid_t id = -1;
    H5E_BEGIN_TRY
    {
        id = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    }
    H5E_END_TRY;
    if (id < 0)
        throw cae::FileFormatError("Failed to open FLASH HDF5 file: " + path);
    return H5Handle(id, H5Fclose);
}

static H5Handle OpenDataset(hid_t file, const std::string& name)
{
    hid_t id = -1;
    H5E_BEGIN_TRY
    {
        id = H5Dopen2(file, name.c_str(), H5P_DEFAULT);
    }
    H5E_END_TRY;
    if (id < 0)
        throw cae::FileFormatError("Missing FLASH HDF5 dataset: " + name);
    return H5Handle(id, H5Dclose);
}

static bool DatasetExists(hid_t file, const std::string& name)
{
    htri_t exists = 0;
    H5E_BEGIN_TRY
    {
        exists = H5Lexists(file, name.c_str(), H5P_DEFAULT);
    }
    H5E_END_TRY;
    return exists > 0;
}

static std::vector<hsize_t> GetDatasetDims(hid_t dataset)
{
    H5Handle space(H5Dget_space(dataset), H5Sclose);
    if (!space)
        throw cae::FileFormatError("Failed to inspect HDF5 dataspace");
    const int rank = H5Sget_simple_extent_ndims(space);
    if (rank < 0)
        throw cae::FileFormatError("Failed to inspect HDF5 rank");
    std::vector<hsize_t> dims(static_cast<size_t>(rank));
    if (rank > 0 && H5Sget_simple_extent_dims(space, dims.data(), nullptr) < 0)
        throw cae::FileFormatError("Failed to inspect HDF5 dimensions");
    return dims;
}

static size_t ElementCount(const std::vector<hsize_t>& dims)
{
    size_t count = 1;
    for (hsize_t dim : dims)
    {
        if (dim > 0 && count > std::numeric_limits<size_t>::max() / dim)
            throw cae::FileFormatError("HDF5 dataset is too large to address");
        count *= dim;
    }
    return count;
}

enum class NumericKind
{
    Int32,
    Int64,
    UInt32,
    UInt64,
    Float32,
    Float64
};

static const char* NumericKindName(NumericKind kind)
{
    switch (kind)
    {
    case NumericKind::Int32:
        return "int32";
    case NumericKind::Int64:
        return "int64";
    case NumericKind::UInt32:
        return "uint32";
    case NumericKind::UInt64:
        return "uint64";
    case NumericKind::Float32:
        return "float32";
    case NumericKind::Float64:
        return "float64";
    }
    return "unknown";
}

static NumericKind GetNumericKind(hid_t type)
{
    const H5T_class_t cls = H5Tget_class(type);
    const size_t size = H5Tget_size(type);
    if (cls == H5T_FLOAT)
    {
        if (size == 4)
            return NumericKind::Float32;
        if (size == 8)
            return NumericKind::Float64;
    }
    if (cls == H5T_INTEGER)
    {
        const bool isUnsigned = H5Tget_sign(type) == H5T_SGN_NONE;
        if (size == 4)
            return isUnsigned ? NumericKind::UInt32 : NumericKind::Int32;
        if (size == 8)
            return isUnsigned ? NumericKind::UInt64 : NumericKind::Int64;
    }
    throw cae::FileFormatError("Unsupported FLASH numeric HDF5 type");
}

static TfToken SdfTypeFor(NumericKind kind)
{
    switch (kind)
    {
    case NumericKind::Int32:
        return TfToken("int[]");
    case NumericKind::Int64:
        return TfToken("int64[]");
    case NumericKind::UInt32:
        return TfToken("uint[]");
    case NumericKind::UInt64:
        return TfToken("uint64[]");
    case NumericKind::Float32:
        return TfToken("float[]");
    case NumericKind::Float64:
        return TfToken("double[]");
    }
    throw cae::FileFormatError("Unsupported FLASH numeric kind");
}

template <typename T>
static VtValue ReadNumericArrayAs(
    hid_t dataset, hid_t memoryType, size_t count, const std::string& filePath, const std::string& datasetName)
{
    VtArray<T> values(count);
    if (count > 0 && H5Dread(dataset, memoryType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
        throw cae::FileFormatError("Failed to read FLASH dataset '" + datasetName + "' from '" + filePath + "'");
    return VtValue::Take(values);
}

static VtValue ReadNumericArray(const std::string& filePath, const std::string& datasetName, NumericKind kind)
{
    std::scoped_lock lock(GetHdf5Mutex());
    H5Handle file = OpenFile(filePath);
    H5Handle dataset = OpenDataset(file, datasetName);
    const size_t count = ElementCount(GetDatasetDims(dataset));
    TF_DEBUG(CAE_FLASH_FILEFORMAT)
        .Msg("[FLASH] load dataset='%s' type='%s' elements=%zu file='%s'\n", datasetName.c_str(), NumericKindName(kind),
             count, filePath.c_str());
    switch (kind)
    {
    case NumericKind::Int32:
        return ReadNumericArrayAs<int32_t>(dataset, H5T_NATIVE_INT32, count, filePath, datasetName);
    case NumericKind::Int64:
        return ReadNumericArrayAs<int64_t>(dataset, H5T_NATIVE_INT64, count, filePath, datasetName);
    case NumericKind::UInt32:
        return ReadNumericArrayAs<uint32_t>(dataset, H5T_NATIVE_UINT32, count, filePath, datasetName);
    case NumericKind::UInt64:
        return ReadNumericArrayAs<uint64_t>(dataset, H5T_NATIVE_UINT64, count, filePath, datasetName);
    case NumericKind::Float32:
        return ReadNumericArrayAs<float>(dataset, H5T_NATIVE_FLOAT, count, filePath, datasetName);
    case NumericKind::Float64:
        return ReadNumericArrayAs<double>(dataset, H5T_NATIVE_DOUBLE, count, filePath, datasetName);
    }
    return VtValue();
}

static std::string TrimFixedString(std::string value)
{
    while (!value.empty() && (value.back() == '\0' || std::isspace(static_cast<unsigned char>(value.back()))))
        value.pop_back();
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    return value.substr(begin);
}

// Strips only embedded and trailing null bytes, preserving spaces.  Used to
// recover the exact HDF5 dataset name stored in "unknown names", which FLASH
// may pad with spaces rather than nulls (e.g. "c12 ", "he4 ").
static std::string StripNullsFromFixedString(std::string value)
{
    while (!value.empty() && value.back() == '\0')
        value.pop_back();
    return value;
}

static std::vector<char> ReadFixedStringBytes(hid_t file, const std::string& name, size_t& outWidth)
{
    H5Handle dataset = OpenDataset(file, name);
    H5Handle type(H5Dget_type(dataset), H5Tclose);
    if (!type || H5Tget_class(type) != H5T_STRING || H5Tis_variable_str(type) > 0)
        throw cae::FileFormatError("Expected fixed-string FLASH dataset: " + name);
    outWidth = H5Tget_size(type);
    if (outWidth == 0)
        throw cae::FileFormatError("FLASH string dataset has zero-width elements: " + name);
    const size_t count = ElementCount(GetDatasetDims(dataset));
    if (count > std::numeric_limits<size_t>::max() / outWidth)
        throw cae::FileFormatError("FLASH string dataset is too large to address: " + name);
    std::vector<char> bytes(count * outWidth, '\0');
    if (!bytes.empty() && H5Dread(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data()) < 0)
        throw cae::FileFormatError("Failed to read FLASH string dataset: " + name);
    return bytes;
}

static std::vector<std::string> ReadFixedStringDataset(hid_t file, const std::string& name)
{
    size_t width = 0;
    const std::vector<char> bytes = ReadFixedStringBytes(file, name, width);
    const size_t count = bytes.size() / width;
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(TrimFixedString(std::string(bytes.data() + i * width, width)));
    return result;
}

// Returns the raw HDF5-key form of each entry: null bytes stripped but spaces
// preserved.  Used to look up field datasets whose names may end in spaces.
static std::vector<std::string> ReadRawStringDataset(hid_t file, const std::string& name)
{
    size_t width = 0;
    const std::vector<char> bytes = ReadFixedStringBytes(file, name, width);
    const size_t count = bytes.size() / width;
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(StripNullsFromFixedString(std::string(bytes.data() + i * width, width)));
    return result;
}

using ScalarValue = std::variant<int32_t, int64_t, uint32_t, uint64_t, float, double, bool, std::string>;

static std::vector<std::string> ReadCompoundStringMember(hid_t dataset, const std::string& memberName)
{
    H5Handle fileType(H5Dget_type(dataset), H5Tclose);
    if (!fileType || H5Tget_class(fileType) != H5T_COMPOUND)
        throw cae::FileFormatError("Expected FLASH compound dataset for member: " + memberName);
    const int memberIndex = H5Tget_member_index(fileType, memberName.c_str());
    if (memberIndex < 0)
        throw cae::FileFormatError("Missing compound member: " + memberName);
    H5Handle memberType(H5Tget_member_type(fileType, static_cast<unsigned int>(memberIndex)), H5Tclose);
    if (!memberType || H5Tget_class(memberType) != H5T_STRING || H5Tis_variable_str(memberType) > 0)
        throw cae::FileFormatError("Expected fixed-string compound member: " + memberName);

    const size_t width = H5Tget_size(memberType);
    if (width == 0)
        throw cae::FileFormatError("FLASH compound string member has zero width: " + memberName);
    const size_t count = ElementCount(GetDatasetDims(dataset));
    if (width > 0 && count > std::numeric_limits<size_t>::max() / width)
        throw cae::FileFormatError("FLASH compound string member is too large to address: " + memberName);
    H5Handle memoryString(H5Tcopy(H5T_C_S1), H5Tclose);
    if (!memoryString || H5Tset_size(memoryString, width) < 0 || H5Tset_strpad(memoryString, H5T_STR_NULLPAD) < 0)
        throw cae::FileFormatError("Failed to construct compound string memory type: " + memberName);
    H5Handle memoryType(H5Tcreate(H5T_COMPOUND, width), H5Tclose);
    if (!memoryType || H5Tinsert(memoryType, memberName.c_str(), 0, memoryString) < 0)
        throw cae::FileFormatError("Failed to construct compound string memory type");

    std::vector<char> bytes(count * width, '\0');
    if (!bytes.empty() && H5Dread(dataset, memoryType, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data()) < 0)
        throw cae::FileFormatError("Failed to read compound string member: " + memberName);
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(TrimFixedString(std::string(bytes.data() + i * width, width)));
    return result;
}

template <typename T>
static std::vector<T> ReadCompoundNumericMemberAs(hid_t dataset, const std::string& memberName, hid_t memoryScalar)
{
    const size_t count = ElementCount(GetDatasetDims(dataset));
    H5Handle memoryType(H5Tcreate(H5T_COMPOUND, sizeof(T)), H5Tclose);
    if (!memoryType || H5Tinsert(memoryType, memberName.c_str(), 0, memoryScalar) < 0)
        throw cae::FileFormatError("Failed to construct compound numeric memory type");
    std::vector<T> values(count);
    if (!values.empty() && H5Dread(dataset, memoryType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
        throw cae::FileFormatError("Failed to read compound numeric member: " + memberName);
    return values;
}

static std::vector<ScalarValue> ReadCompoundValues(hid_t dataset, const std::string& memberName)
{
    H5Handle fileType(H5Dget_type(dataset), H5Tclose);
    if (!fileType || H5Tget_class(fileType) != H5T_COMPOUND)
        throw cae::FileFormatError("Expected FLASH compound dataset for member: " + memberName);
    const int memberIndex = H5Tget_member_index(fileType, memberName.c_str());
    if (memberIndex < 0)
        throw cae::FileFormatError("Missing compound member: " + memberName);
    H5Handle memberType(H5Tget_member_type(fileType, static_cast<unsigned int>(memberIndex)), H5Tclose);
    if (H5Tget_class(memberType) == H5T_STRING)
    {
        std::vector<ScalarValue> out;
        for (const std::string& value : ReadCompoundStringMember(dataset, memberName))
            out.emplace_back(value);
        return out;
    }

    const NumericKind kind = GetNumericKind(memberType);
    std::vector<ScalarValue> out;
    switch (kind)
    {
    case NumericKind::Int32:
        for (int32_t value : ReadCompoundNumericMemberAs<int32_t>(dataset, memberName, H5T_NATIVE_INT32))
            out.emplace_back(value);
        break;
    case NumericKind::Int64:
        for (int64_t value : ReadCompoundNumericMemberAs<int64_t>(dataset, memberName, H5T_NATIVE_INT64))
            out.emplace_back(value);
        break;
    case NumericKind::UInt32:
        for (uint32_t value : ReadCompoundNumericMemberAs<uint32_t>(dataset, memberName, H5T_NATIVE_UINT32))
            out.emplace_back(value);
        break;
    case NumericKind::UInt64:
        for (uint64_t value : ReadCompoundNumericMemberAs<uint64_t>(dataset, memberName, H5T_NATIVE_UINT64))
            out.emplace_back(value);
        break;
    case NumericKind::Float32:
        for (float value : ReadCompoundNumericMemberAs<float>(dataset, memberName, H5T_NATIVE_FLOAT))
            out.emplace_back(value);
        break;
    case NumericKind::Float64:
        for (double value : ReadCompoundNumericMemberAs<double>(dataset, memberName, H5T_NATIVE_DOUBLE))
            out.emplace_back(value);
        break;
    }
    return out;
}

static cae::StringMap<ScalarValue> ReadNamedScalarTable(hid_t file, const std::string& datasetName, bool logicalValues)
{
    cae::StringMap<ScalarValue> result;
    if (!DatasetExists(file, datasetName))
        return result;
    H5Handle dataset = OpenDataset(file, datasetName);
    const std::vector<std::string> names = ReadCompoundStringMember(dataset, "name");
    std::vector<ScalarValue> values = ReadCompoundValues(dataset, "value");
    if (names.size() != values.size())
        throw cae::FileFormatError("FLASH scalar name/value count mismatch in " + datasetName);
    for (size_t i = 0; i < names.size(); ++i)
    {
        ScalarValue value = std::move(values[i]);
        if (logicalValues)
        {
            bool logical = false;
            bool converted = false;
            std::visit(
                [&](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::is_arithmetic_v<T>)
                    {
                        logical = typed != 0;
                        converted = true;
                    }
                },
                value);
            if (!converted)
                throw cae::FileFormatError("FLASH logical scalar is not numeric: " + names[i]);
            value = logical;
        }
        if (!result.try_emplace(names[i], std::move(value)).second)
            throw cae::FileFormatError("Duplicate FLASH scalar name: " + names[i]);
    }
    return result;
}

static cae::StringMap<ScalarValue> ReadSimInfo(hid_t file)
{
    cae::StringMap<ScalarValue> result;
    if (!DatasetExists(file, "sim info"))
        return result;
    H5Handle dataset = OpenDataset(file, "sim info");
    H5Handle type(H5Dget_type(dataset), H5Tclose);
    if (!type || H5Tget_class(type) != H5T_COMPOUND)
        throw cae::FileFormatError("FLASH sim info is not a compound dataset");
    const int memberCount = H5Tget_nmembers(type);
    if (memberCount < 0)
        throw cae::FileFormatError("Failed to enumerate FLASH sim info members");
    for (int i = 0; i < memberCount; ++i)
    {
        char* rawName = H5Tget_member_name(type, static_cast<unsigned int>(i));
        if (!rawName)
            throw cae::FileFormatError("Failed to read FLASH sim info member name");
        const std::string name(rawName);
        H5free_memory(rawName);
        std::vector<ScalarValue> values = ReadCompoundValues(dataset, name);
        if (values.size() != 1)
            throw cae::FileFormatError("FLASH sim info must contain exactly one record");
        result.try_emplace(name, std::move(values.front()));
    }
    return result;
}

static std::optional<double> ScalarAsDouble(const cae::StringMap<ScalarValue>& scalars, const std::string& name)
{
    const auto it = scalars.find(name);
    if (it == scalars.end())
        return std::nullopt;
    std::optional<double> result;
    std::visit(
        [&](const auto& value)
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
                result = static_cast<double>(value);
        },
        it->second);
    return result;
}

#if H5_VERSION_GE(1, 12, 0)
using H5LinkInfo = H5L_info2_t;
#else
using H5LinkInfo = H5L_info_t;
#endif

static herr_t ListRootCallback(hid_t, const char* name, const H5LinkInfo*, void* userData)
{
    static_cast<std::vector<std::string>*>(userData)->emplace_back(name);
    return 0;
}

static std::vector<std::string> ListRootNames(hid_t file)
{
    std::vector<std::string> names;
    hsize_t index = 0;
    if (H5Literate(file, H5_INDEX_NAME, H5_ITER_NATIVE, &index, ListRootCallback, &names) < 0)
        throw cae::FileFormatError("Failed to enumerate FLASH HDF5 root");
    return names;
}

static bool WildcardMatch(const std::string& pattern, const std::string& value)
{
    size_t p = 0;
    size_t v = 0;
    size_t star = std::string::npos;
    size_t retry = 0;
    while (v < value.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v]))
        {
            ++p;
            ++v;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            retry = v;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            v = ++retry;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

static bool NaturalLess(const fs::path& lhsPath, const fs::path& rhsPath)
{
    const std::string lhs = lhsPath.filename().string();
    const std::string rhs = rhsPath.filename().string();
    size_t i = 0;
    size_t j = 0;
    while (i < lhs.size() && j < rhs.size())
    {
        if (std::isdigit(static_cast<unsigned char>(lhs[i])) && std::isdigit(static_cast<unsigned char>(rhs[j])))
        {
            size_t ie = i;
            size_t je = j;
            while (ie < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[ie])))
                ++ie;
            while (je < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[je])))
                ++je;
            const std::string ln = lhs.substr(i, ie - i);
            const std::string rn = rhs.substr(j, je - j);
            const std::string ltrim = ln.substr(std::min(ln.find_first_not_of('0'), ln.size() - 1));
            const std::string rtrim = rn.substr(std::min(rn.find_first_not_of('0'), rn.size() - 1));
            if (ltrim.size() != rtrim.size())
                return ltrim.size() < rtrim.size();
            if (ltrim != rtrim)
                return ltrim < rtrim;
            if (ln.size() != rn.size())
                return ln.size() < rn.size();
            i = ie;
            j = je;
            continue;
        }
        const auto lc = static_cast<unsigned char>(lhs[i]);
        if (const auto rc = static_cast<unsigned char>(rhs[j]); lc != rc)
            return lc < rc;
        ++i;
        ++j;
    }
    return lhs.size() < rhs.size();
}

struct Manifest
{
    std::vector<std::string> files;
    std::vector<CaeResolverAssetPtr> assetLeases;
};

static JsValue RequireObjectMember(const JsObject& object, const std::string& name)
{
    const auto it = object.find(name);
    if (it == object.end())
        throw cae::FileFormatError("FLASH descriptor is missing '" + name + "'");
    return it->second;
}

// Validates that a descriptor file has the expected JSON structure for a FLASH dataset.
// Does not resolve or access any of the referenced data files.  Use this in CanRead()
// so that probing secondary HDF5 files — which may live on remote storage — is not
// required before the layer is accepted for loading.
static void ValidateDescriptorFormat(const std::string& descriptorPath)
{
    std::ifstream input(descriptorPath);
    if (!input)
        throw cae::FileFormatError("Failed to open FLASH descriptor: " + descriptorPath);
    JsParseError error;
    const JsValue root = JsParseStream(input, &error);
    if (!root.IsObject())
        throw cae::FileFormatError("Invalid FLASH descriptor JSON at line " + std::to_string(error.line) + ": " +
                                   error.reason);
    const JsObject& object = root.GetJsObject();
    const JsValue format = RequireObjectMember(object, "format");
    if (!format.IsString() || format.GetString() != "flash-paramesh-hdf5")
        throw cae::FileFormatError("FLASH descriptor format must be 'flash-paramesh-hdf5'");
    const auto versionIt = object.find("version");
    if (versionIt != object.end() && (!versionIt->second.IsInt() || versionIt->second.GetInt() != 1))
        throw cae::FileFormatError("Unsupported FLASH descriptor version");
    const bool hasFile = object.count("file") != 0;
    const bool hasFiles = object.count("files") != 0;
    const bool hasPattern = object.count("pattern") != 0;
    if (static_cast<int>(hasFile) + static_cast<int>(hasFiles) + static_cast<int>(hasPattern) != 1)
        throw cae::FileFormatError("FLASH descriptor must contain exactly one of 'file', 'files', or 'pattern'");
}

// Parses the descriptor at `descriptorPath` (a resolved local path) and returns the
// list of local paths for the referenced HDF5 snapshot files.
//
// `anchorIdentifier` is the *original* identifier of the .flash asset — for example
// "omniverse://server/project/sim.flash" when the file lives on Nucleus.  It is used
// as the anchor for the USD Asset Resolver so that relative paths in the descriptor are
// resolved through the same resolver that fetched the descriptor itself, enabling
// transparent access to files on Nucleus or any other AR-backed storage.
//
// The 'pattern' key is resolved against the local filesystem only (directory iteration
// is not supported for remote storage).  Use 'file' or 'files' when the data lives on
// Nucleus or another remote location.
static Manifest ParseManifest(const std::string& descriptorPath, const std::string& anchorIdentifier)
{
    std::ifstream input(descriptorPath);
    if (!input)
        throw cae::FileFormatError("Failed to open FLASH descriptor: " + descriptorPath);
    JsParseError error;
    const JsValue root = JsParseStream(input, &error);
    if (!root.IsObject())
        throw cae::FileFormatError("Invalid FLASH descriptor JSON at line " + std::to_string(error.line) + ": " +
                                   error.reason);
    const JsObject& object = root.GetJsObject();
    const JsValue format = RequireObjectMember(object, "format");
    if (!format.IsString() || format.GetString() != "flash-paramesh-hdf5")
        throw cae::FileFormatError("FLASH descriptor format must be 'flash-paramesh-hdf5'");

    const auto versionIt = object.find("version");
    if (versionIt != object.end() && (!versionIt->second.IsInt() || versionIt->second.GetInt() != 1))
        throw cae::FileFormatError("Unsupported FLASH descriptor version");

    const bool hasFile = object.count("file") != 0;
    const bool hasFiles = object.count("files") != 0;
    const bool hasPattern = object.count("pattern") != 0;
    if (static_cast<int>(hasFile) + static_cast<int>(hasFiles) + static_cast<int>(hasPattern) != 1)
        throw cae::FileFormatError("FLASH descriptor must contain exactly one of 'file', 'files', or 'pattern'");

    Manifest manifest;

    if (hasFile || hasFiles)
    {
        std::vector<std::string> referencedPaths;
        if (hasFile)
        {
            const JsValue& value = object.at("file");
            if (!value.IsString() || value.GetString().empty())
                throw cae::FileFormatError("FLASH descriptor 'file' must be a non-empty string");
            referencedPaths.push_back(value.GetString());
        }
        else
        {
            const JsValue& value = object.at("files");
            if (!value.IsArray() || value.GetJsArray().empty())
                throw cae::FileFormatError("FLASH descriptor 'files' must be a non-empty string array");
            for (const JsValue& item : value.GetJsArray())
            {
                if (!item.IsString() || item.GetString().empty())
                    throw cae::FileFormatError("FLASH descriptor 'files' must contain only non-empty strings");
                referencedPaths.push_back(item.GetString());
            }
        }

        cae::StringSet seen;
        for (const std::string& path : referencedPaths)
        {
            CaeResolverAssetPtr asset = CaeResolveSiblingAsset(anchorIdentifier, path);
            if (!seen.insert(asset->Identifier()).second)
                throw cae::FileFormatError("FLASH descriptor contains duplicate file: " + path);
            manifest.files.push_back(asset->LocalPath());
            manifest.assetLeases.push_back(std::move(asset));
        }
    }
    else
    {
        CaeRequireAdjacentFileScanning(
            "FLASH pattern manifests", anchorIdentifier, ArGetResolver().Resolve(anchorIdentifier));
        // 'pattern' uses local filesystem directory iteration.  It is not supported
        // when the descriptor lives on remote storage (e.g. Nucleus); use 'file' or
        // 'files' in that case.
        const JsValue& value = object.at("pattern");
        if (!value.IsString() || value.GetString().empty())
            throw cae::FileFormatError("FLASH descriptor 'pattern' must be a non-empty string");
        const fs::path relativePattern(value.GetString());
        const fs::path base = fs::absolute(fs::path(descriptorPath)).parent_path();
        const fs::path directory = base / relativePattern.parent_path();
        const std::string filenamePattern = relativePattern.filename().string();
        if (!fs::is_directory(directory))
            throw cae::FileFormatError(
                "FLASH descriptor pattern directory does not exist: " + directory.string() +
                " (note: 'pattern' requires local filesystem access; use 'file' or 'files' for remote storage)");
        std::vector<fs::path> paths;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory))
        {
            if (entry.is_regular_file() && WildcardMatch(filenamePattern, entry.path().filename().string()))
                paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end(), NaturalLess);
        if (paths.empty())
            throw cae::FileFormatError("FLASH descriptor pattern matched no files: " + value.GetString());

        std::set<fs::path> seen;
        for (const fs::path& path : paths)
        {
            const fs::path normalized = fs::absolute(path).lexically_normal();
            if (!seen.insert(normalized).second)
                throw cae::FileFormatError("FLASH descriptor contains duplicate file: " + normalized.string());
            manifest.files.push_back(normalized.string());
        }
    }

    TF_DEBUG(CAE_FLASH_FILEFORMAT)
        .Msg("[FLASH] descriptor='%s' resolvedSnapshots=%zu\n", descriptorPath.c_str(), manifest.files.size());
    for (const std::string& file : manifest.files)
        TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH]   source='%s'\n", file.c_str());
    return manifest;
}

struct DatasetInfo
{
    std::string sourceName;
    TfToken instanceName;
    NumericKind kind = NumericKind::Float32;
    TfToken sdfType;
    std::vector<hsize_t> dims;
    bool field = false;
};

struct Snapshot
{
    std::string filePath;
    size_t globalBlockCount = 0;
    int spatialDimension = 0;
    std::vector<std::string> fieldNames;
    std::vector<DatasetInfo> arrays;
    cae::StringMap<ScalarValue> scalars;
    cae::StringMap<ScalarValue> simInfo;
    std::optional<int64_t> formatVersion;
    double rawTime = 0.0;
    double sampleTime = 0.0;
};

static const DatasetInfo* FindArray(const Snapshot& snapshot, const std::string& sourceName)
{
    const auto it = std::find_if(snapshot.arrays.begin(), snapshot.arrays.end(),
                                 [&](const DatasetInfo& info) { return info.sourceName == sourceName; });
    return it == snapshot.arrays.end() ? nullptr : &*it;
}

static DatasetInfo InspectNumericDataset(hid_t file, const std::string& sourceName, const TfToken& instanceName, bool field)
{
    H5Handle dataset = OpenDataset(file, sourceName);
    H5Handle type(H5Dget_type(dataset), H5Tclose);
    if (!type)
        throw cae::FileFormatError("Failed to inspect FLASH HDF5 type for dataset: " + sourceName);
    DatasetInfo info;
    info.sourceName = sourceName;
    info.instanceName = instanceName;
    info.kind = GetNumericKind(type);
    info.sdfType = SdfTypeFor(info.kind);
    info.dims = GetDatasetDims(dataset);
    info.field = field;
    return info;
}

static int SpatialDimensionFromGidWidth(hsize_t width)
{
    if (width == 5)
        return 1;
    if (width == 9)
        return 2;
    if (width == 15)
        return 3;
    throw cae::FileFormatError("Unsupported PARAMESH GID row width: " + std::to_string(width));
}

static bool SameTrailingDims(const std::vector<hsize_t>& lhs, const std::vector<hsize_t>& rhs)
{
    return lhs.size() == rhs.size() && lhs.size() >= 1 && std::equal(lhs.begin() + 1, lhs.end(), rhs.begin() + 1);
}

static bool IsIntegerKind(NumericKind kind)
{
    return kind == NumericKind::Int32 || kind == NumericKind::Int64 || kind == NumericKind::UInt32 ||
           kind == NumericKind::UInt64;
}

static bool IsFloatingPointKind(NumericKind kind)
{
    return kind == NumericKind::Float32 || kind == NumericKind::Float64;
}

template <typename Range>
static std::string ShapeString(const Range& dimensions)
{
    std::string result = "[";
    bool first = true;
    for (hsize_t dimension : dimensions)
    {
        if (!first)
            result += ", ";
        result += std::to_string(dimension);
        first = false;
    }
    result += "]";
    return result;
}

static void RequireTrailingShape(const DatasetInfo& info, std::initializer_list<hsize_t> trailingShape)
{
    if (info.dims.size() != trailingShape.size() + 1 ||
        !std::equal(trailingShape.begin(), trailingShape.end(), info.dims.begin() + 1))
    {
        throw cae::FileFormatError("FLASH dataset '" + info.sourceName + "' has shape " + ShapeString(info.dims) +
                                   "; expected trailing shape " + ShapeString(trailingShape));
    }
}

static void ReadSnapshotMetadata(const H5Handle& file, Snapshot* snapshot)
{
    snapshot->fieldNames = ReadFixedStringDataset(file, "unknown names");
    snapshot->scalars = ReadNamedScalarTable(file, "integer scalars", false);
    const auto realScalars = ReadNamedScalarTable(file, "real scalars", false);
    const auto logicalScalars = ReadNamedScalarTable(file, "logical scalars", true);
    const auto stringScalars = ReadNamedScalarTable(file, "string scalars", false);
    for (const auto* table : { &realScalars, &logicalScalars, &stringScalars })
    {
        for (const auto& item : *table)
        {
            if (!snapshot->scalars.try_emplace(item.first, item.second).second)
                throw cae::FileFormatError("Duplicate FLASH scalar name across scalar tables: " + item.first);
        }
    }

    snapshot->simInfo = ReadSimInfo(file);
    const auto version = snapshot->simInfo.find("file format version");
    if (version == snapshot->simInfo.end())
        return;
    std::visit(
        [&](const auto& value)
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
                snapshot->formatVersion = static_cast<int64_t>(value);
        },
        version->second);
}

static void AddStructuralArrays(const H5Handle& file,
                                Snapshot* snapshot,
                                cae::StringSet* knownNames,
                                cae::StringSet* instances)
{
    const std::array<std::tuple<const char*, TfToken, bool>, 7> structural = {
        std::make_tuple("gid", OmniSciFlashTokens->gid, true),
        std::make_tuple("node type", OmniSciFlashTokens->nodeType, true),
        std::make_tuple("refine level", OmniSciFlashTokens->refinementLevel, true),
        std::make_tuple("bounding box", OmniSciFlashTokens->boundingBox, true),
        std::make_tuple("coordinates", OmniSciFlashTokens->coordinates, false),
        std::make_tuple("block size", OmniSciFlashTokens->blockSize, false),
        std::make_tuple("processor number", OmniSciFlashTokens->processorNumber, false),
    };

    for (const auto& [sourceNameValue, instanceName, required] : structural)
    {
        const std::string sourceName = sourceNameValue;
        if (!DatasetExists(file, sourceName))
        {
            if (required)
                throw cae::FileFormatError("Required FLASH dataset is missing: " + sourceName);
            continue;
        }
        snapshot->arrays.push_back(InspectNumericDataset(file, sourceName, instanceName, false));
        knownNames->insert(sourceName);
        instances->insert(instanceName.GetString());
    }
}

static void ValidateStructuralArrays(Snapshot* snapshot)
{
    const DatasetInfo& nodeType = *FindArray(*snapshot, "node type");
    if (nodeType.dims.size() != 1 || !IsIntegerKind(nodeType.kind))
        throw cae::FileFormatError("FLASH node type must have shape [G]");
    snapshot->globalBlockCount = nodeType.dims[0];
    if (snapshot->globalBlockCount == 0)
        throw cae::FileFormatError("FLASH plotfile contains no AMR blocks");

    const DatasetInfo& gid = *FindArray(*snapshot, "gid");
    if (gid.dims.size() != 2 || gid.dims[0] != snapshot->globalBlockCount || !IsIntegerKind(gid.kind))
        throw cae::FileFormatError("FLASH gid must have shape [G, W]");
    snapshot->spatialDimension = SpatialDimensionFromGidWidth(gid.dims[1]);

    if (const DatasetInfo& refinementLevel = *FindArray(*snapshot, "refine level");
        refinementLevel.dims.size() != 1 || !IsIntegerKind(refinementLevel.kind))
        throw cae::FileFormatError("FLASH refine level must be an integer array with shape [G]");

    const DatasetInfo& boundingBox = *FindArray(*snapshot, "bounding box");
    RequireTrailingShape(boundingBox, { 3, 2 });
    if (!IsFloatingPointKind(boundingBox.kind))
        throw cae::FileFormatError("FLASH bounding box must use a floating-point type");

    for (const char* name : { "coordinates", "block size" })
    {
        if (const DatasetInfo* array = FindArray(*snapshot, name))
        {
            RequireTrailingShape(*array, { 3 });
            if (!IsFloatingPointKind(array->kind))
                throw cae::FileFormatError("FLASH " + std::string(name) + " must use a floating-point type");
        }
    }
    if (const DatasetInfo* processorNumber = FindArray(*snapshot, "processor number");
        processorNumber && (processorNumber->dims.size() != 1 || !IsIntegerKind(processorNumber->kind)))
        throw cae::FileFormatError("FLASH processor number must be an integer array with shape [G]");
}

static void AddFieldArrays(const H5Handle& file,
                           const std::vector<std::string>& rawFieldNames,
                           Snapshot* snapshot,
                           cae::StringSet* knownNames,
                           cae::StringSet* instances)
{
    std::vector<hsize_t> fieldDimensions;
    for (size_t index = 0; index < snapshot->fieldNames.size(); ++index)
    {
        const std::string& fieldName = snapshot->fieldNames[index];
        const std::string& hdf5Name = rawFieldNames[index];
        if (fieldName.empty())
            throw cae::FileFormatError("FLASH unknown names contains an empty field name");
        if (!DatasetExists(file, hdf5Name))
            throw cae::FileFormatError("FLASH field named by unknown names is missing: " + hdf5Name);

        const std::string validName = TfMakeValidIdentifier(fieldName);
        if (validName.empty() || !instances->insert(validName).second)
            throw cae::FileFormatError("FLASH field name collision after USD sanitization: " + fieldName);
        DatasetInfo info = InspectNumericDataset(file, hdf5Name, TfToken(validName), true);
        if (info.dims.size() != 4 || info.dims[0] != snapshot->globalBlockCount)
            throw cae::FileFormatError("FLASH field must have shape [G, nzb, nyb, nxb]: " + hdf5Name);
        if (fieldDimensions.empty())
            fieldDimensions = info.dims;
        else if (!SameTrailingDims(fieldDimensions, info.dims))
            throw cae::FileFormatError("FLASH fields do not share one trailing shape");
        snapshot->arrays.push_back(std::move(info));
        knownNames->insert(hdf5Name);
    }
}

static void ValidateSnapshotArrayCounts(const Snapshot& snapshot)
{
    for (const DatasetInfo& info : snapshot.arrays)
    {
        if (info.dims.empty() || static_cast<size_t>(info.dims[0]) != snapshot.globalBlockCount)
            throw cae::FileFormatError("FLASH array leading dimension does not equal G: " + info.sourceName);
    }
    if (const std::optional<double> declared = ScalarAsDouble(snapshot.scalars, "globalnumblocks");
        declared.has_value() && *declared != static_cast<double>(snapshot.globalBlockCount))
        throw cae::FileFormatError("FLASH globalnumblocks does not match dataset leading dimensions");
}

static Snapshot ScanSnapshot(const std::string& filePath, bool warnUnsupported)
{
    std::scoped_lock lock(GetHdf5Mutex());
    H5Handle file = OpenFile(filePath);
    for (const char* required : { "unknown names", "gid", "node type", "refine level", "bounding box" })
    {
        if (!DatasetExists(file, required))
            throw cae::FileFormatError("Required FLASH dataset is missing from '" + filePath + "': " + required);
    }

    Snapshot snapshot;
    snapshot.filePath = filePath;
    // Raw names preserve trailing spaces so they match the actual HDF5 dataset keys.
    const std::vector<std::string> rawFieldNames = ReadRawStringDataset(file, "unknown names");
    ReadSnapshotMetadata(file, &snapshot);

    cae::StringSet knownNames = { "unknown names",   "integer scalars", "real scalars",
                                  "logical scalars", "string scalars",  "sim info" };
    cae::StringSet instances;
    AddStructuralArrays(file, &snapshot, &knownNames, &instances);
    ValidateStructuralArrays(&snapshot);
    AddFieldArrays(file, rawFieldNames, &snapshot, &knownNames, &instances);
    ValidateSnapshotArrayCounts(snapshot);

    if (warnUnsupported)
    {
        for (const std::string& name : ListRootNames(file))
        {
            if (!cae::Contains(knownNames, name))
                TF_WARN("OmniSciFlashFileFormat: unsupported root HDF5 dataset '%s' in '%s'", name.c_str(),
                        filePath.c_str());
        }
    }
    TF_DEBUG(CAE_FLASH_FILEFORMAT)
        .Msg("[FLASH] snapshot='%s' blocks=%zu dimension=%d fields=%zu arrays=%zu scalars=%zu simInfo=%zu\n",
             filePath.c_str(), snapshot.globalBlockCount, snapshot.spatialDimension, snapshot.fieldNames.size(),
             snapshot.arrays.size(), snapshot.scalars.size(), snapshot.simInfo.size());
    return snapshot;
}

struct ReadOptions
{
    SdfPath rootPath;
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    std::string timeSource = "TimeStep";
    double timeScale = 1.0;
    double timeOffset = 0.0;
};

static double ParseFiniteArgument(const std::string& text, const char* name)
{
    size_t parsed = 0;
    double value = 0.0;
    try
    {
        value = std::stod(text, &parsed);
    }
    catch (const std::exception&)
    {
        throw cae::FileFormatError(std::string("FLASH ") + name + " must be a finite number: " + text);
    }
    while (parsed < text.size() && std::isspace(static_cast<unsigned char>(text[parsed])))
        ++parsed;
    if (parsed != text.size() || !std::isfinite(value))
        throw cae::FileFormatError(std::string("FLASH ") + name + " must be a finite number: " + text);
    return value;
}

static ReadOptions ParseReadOptions(const std::string& descriptorPath, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key) -> std::string
    {
        const auto it = args.find(key.GetString());
        return it == args.end() ? std::string() : it->second;
    };
    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(descriptorPath, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    const std::string timeSource = getArg(OmniSciFlashFileFormatTokens->ArgTimeSource);
    const std::string timeScale = getArg(OmniSciFlashFileFormatTokens->ArgTimeScale);
    const std::string timeOffset = getArg(OmniSciFlashFileFormatTokens->ArgTimeOffset);
    const std::string cacheMode = getArg(OmniSciFlashFileFormatTokens->ArgCacheMode);
    if (!timeSource.empty())
        options.timeSource = timeSource;
    if (!timeScale.empty())
        options.timeScale = ParseFiniteArgument(timeScale, "timeScale");
    if (!timeOffset.empty())
        options.timeOffset = ParseFiniteArgument(timeOffset, "timeOffset");
    if (options.timeSource != "TimeValue" && options.timeSource != "TimeStep" && options.timeSource != "IterationValue")
        throw cae::FileFormatError("FLASH timeSource must be TimeValue, TimeStep, or IterationValue");
    TF_DEBUG(CAE_FLASH_FILEFORMAT)
        .Msg("[FLASH] options root='%s' cacheMode='%s' timeSource='%s' timeScale=%.17g timeOffset=%.17g\n",
             options.rootPath.GetText(), cacheMode.empty() ? "all" : cacheMode.c_str(), options.timeSource.c_str(),
             options.timeScale, options.timeOffset);
    return options;
}

static bool ScalarSchemaMatches(const cae::StringMap<ScalarValue>& lhs, const cae::StringMap<ScalarValue>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    auto li = lhs.begin();
    auto ri = rhs.begin();
    for (; li != lhs.end(); ++li, ++ri)
    {
        if (li->first != ri->first || li->second.index() != ri->second.index())
            return false;
    }
    return true;
}

static void ValidateSnapshotCompatibility(const Snapshot& reference, const Snapshot& candidate)
{
    if (reference.fieldNames != candidate.fieldNames)
        throw cae::FileFormatError("FLASH series field names or ordering changed: " + candidate.filePath);
    if (reference.spatialDimension != candidate.spatialDimension)
        throw cae::FileFormatError("FLASH series spatial dimension changed: " + candidate.filePath);
    if (reference.formatVersion != candidate.formatVersion)
        throw cae::FileFormatError("FLASH series file-format version changed: " + candidate.filePath);
    if (!ScalarSchemaMatches(reference.scalars, candidate.scalars))
        throw cae::FileFormatError("FLASH series scalar names or types changed: " + candidate.filePath);
    if (!ScalarSchemaMatches(reference.simInfo, candidate.simInfo))
        throw cae::FileFormatError("FLASH series sim info members or types changed: " + candidate.filePath);
    if (reference.arrays.size() != candidate.arrays.size())
        throw cae::FileFormatError("FLASH series optional arrays changed: " + candidate.filePath);
    for (size_t i = 0; i < reference.arrays.size(); ++i)
    {
        const DatasetInfo& a = reference.arrays[i];
        const DatasetInfo& b = candidate.arrays[i];
        if (a.sourceName != b.sourceName || a.instanceName != b.instanceName || a.kind != b.kind ||
            a.field != b.field || !SameTrailingDims(a.dims, b.dims))
            throw cae::FileFormatError("FLASH series array schema changed for '" + a.sourceName +
                                       "': " + candidate.filePath);
    }
}

struct SeriesInfo
{
    ReadOptions options;
    std::vector<Snapshot> snapshots;
    std::vector<CaeResolverAssetPtr> assetLeases;
};

static SeriesInfo ScanSeries(const std::string& descriptorPath,
                             const std::string& anchorIdentifier,
                             const SdfLayer::FileFormatArguments& args,
                             bool warnUnsupported)
{
    SeriesInfo series;
    series.options = ParseReadOptions(descriptorPath, args);
    Manifest manifest = ParseManifest(descriptorPath, anchorIdentifier);
    series.assetLeases = std::move(manifest.assetLeases);
    for (const std::string& file : manifest.files)
    {
        try
        {
            series.snapshots.push_back(ScanSnapshot(file, warnUnsupported));
        }
        catch (const std::exception& ex)
        {
            throw cae::FileFormatError("Failed to scan FLASH snapshot '" + file + "': " + ex.what());
        }
    }
    if (series.snapshots.empty())
        throw cae::FileFormatError("FLASH descriptor contains no snapshots");
    for (size_t i = 1; i < series.snapshots.size(); ++i)
        ValidateSnapshotCompatibility(series.snapshots.front(), series.snapshots[i]);

    if (series.snapshots.size() == 1)
        return series;

    if (series.options.timeSource == "TimeStep")
    {
        for (size_t i = 0; i < series.snapshots.size(); ++i)
            series.snapshots[i].rawTime = static_cast<double>(i);
    }
    else
    {
        const std::string scalarName = series.options.timeSource == "TimeValue" ? "time" : "nstep";
        for (Snapshot& snapshot : series.snapshots)
        {
            const std::optional<double> value = ScalarAsDouble(snapshot.scalars, scalarName);
            if (!value.has_value())
                throw cae::FileFormatError("FLASH timeSource=" + series.options.timeSource + " requires scalar '" +
                                           scalarName + "' in " + snapshot.filePath);
            snapshot.rawTime = *value;
        }
    }

    std::set<double> resolvedTimes;
    for (Snapshot& snapshot : series.snapshots)
    {
        snapshot.sampleTime = series.options.timeOffset + series.options.timeScale * snapshot.rawTime;
        if (!std::isfinite(snapshot.sampleTime))
            throw cae::FileFormatError("FLASH series resolved a non-finite time code for " + snapshot.filePath);
        if (!resolvedTimes.insert(snapshot.sampleTime).second)
            throw cae::FileFormatError("FLASH series resolves multiple snapshots to time code " +
                                       TfStringify(snapshot.sampleTime));
        TF_DEBUG(CAE_FLASH_FILEFORMAT)
            .Msg("[FLASH] sample source='%s' rawTime=%.17g sampleTime=%.17g\n", snapshot.filePath.c_str(),
                 snapshot.rawTime, snapshot.sampleTime);
    }
    std::stable_sort(series.snapshots.begin(), series.snapshots.end(),
                     [](const Snapshot& a, const Snapshot& b) { return a.sampleTime < b.sampleTime; });
    return series;
}

static TfToken ArrayValueAttrName(const TfToken& instanceName)
{
    return TfToken("omni:sci:array:" + instanceName.GetString() + ":value");
}

static VtIntArray TrailingShape(const std::vector<hsize_t>& dims)
{
    VtIntArray result;
    for (size_t i = 1; i < dims.size(); ++i)
    {
        if (dims[i] > static_cast<hsize_t>(std::numeric_limits<int>::max()))
            throw cae::FileFormatError("FLASH trailing dimension exceeds USD int range");
        result.push_back(static_cast<int>(dims[i]));
    }
    return result;
}

static UsdAttribute CreateScalarAttribute(const UsdPrim& prim, const TfToken& name, const ScalarValue& value)
{
    switch (value.index())
    {
    case 0:
        return prim.CreateAttribute(name, SdfValueTypeNames->Int, true);
    case 1:
        return prim.CreateAttribute(name, SdfValueTypeNames->Int64, true);
    case 2:
        return prim.CreateAttribute(name, SdfValueTypeNames->UInt, true);
    case 3:
        return prim.CreateAttribute(name, SdfValueTypeNames->UInt64, true);
    case 4:
        return prim.CreateAttribute(name, SdfValueTypeNames->Float, true);
    case 5:
        return prim.CreateAttribute(name, SdfValueTypeNames->Double, true);
    case 6:
        return prim.CreateAttribute(name, SdfValueTypeNames->Bool, true);
    case 7:
        return prim.CreateAttribute(name, SdfValueTypeNames->String, true);
    default:
        break;
    }
    throw cae::FileFormatError("Unsupported FLASH scalar value type");
}

static void SetScalarValue(const UsdAttribute& attr, const ScalarValue& value, const UsdTimeCode& time)
{
    std::visit([&](const auto& typed) { attr.Set(typed, time); }, value);
}

static void AuthorScalarMap(const SeriesInfo& series,
                            const UsdPrim& prim,
                            const cae::StringMap<ScalarValue> Snapshot::*member,
                            const std::string& propertyPrefix)
{
    const auto& firstMap = series.snapshots.front().*member;
    cae::StringSet propertyNames;
    for (const auto& [sourceName, sourceValue] : firstMap)
    {
        const std::string validName = TfMakeValidIdentifier(sourceName);
        if (validName.empty())
            throw cae::FileFormatError("FLASH metadata name cannot be converted to a USD identifier: " + sourceName);
        const std::string propertyName = propertyPrefix + validName;
        if (!propertyNames.insert(propertyName).second)
            throw cae::FileFormatError("FLASH metadata name collision after USD sanitization: " + sourceName);
        UsdAttribute attr = CreateScalarAttribute(prim, TfToken(propertyName), sourceValue);
        attr.SetCustomDataByKey(TfToken("flashSourceName"), VtValue(sourceName));
        for (const Snapshot& snapshot : series.snapshots)
            SetScalarValue(attr, (snapshot.*member).at(sourceName), UsdTimeCode(snapshot.sampleTime));
    }
}

using ReadFlashResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

static ReadFlashResult ReadFlash(const std::string& descriptorPath,
                                 const std::string& anchorIdentifier,
                                 const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH] read descriptor='%s' args=%zu\n", descriptorPath.c_str(), args.size());
    const SeriesInfo series = ScanSeries(descriptorPath, anchorIdentifier, args, true);

    SdfLayerRefPtr structure = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(structure);
    stage->SetTimeCodesPerSecond(1.0);
    UsdPrim datasetPrim = OmniSciDataset::Define(stage, series.options.rootPath).GetPrim();
    if (!datasetPrim)
        throw cae::FileFormatError("Failed to define FLASH dataset prim");
    OmniSciFlashAmrAPI flashApi = OmniSciFlashAmrAPI::Apply(datasetPrim);
    flashApi.CreateSpatialDimensionAttr().Set(series.snapshots.front().spatialDimension);
    if (const DatasetInfo* info = FindArray(series.snapshots.front(), "gid"))
        flashApi.CreateGidShapeAttr().Set(TrailingShape(info->dims));
    if (const DatasetInfo* info = FindArray(series.snapshots.front(), "coordinates"))
        flashApi.CreateCoordinatesShapeAttr().Set(TrailingShape(info->dims));
    if (const DatasetInfo* info = FindArray(series.snapshots.front(), "bounding box"))
        flashApi.CreateBoundingBoxShapeAttr().Set(TrailingShape(info->dims));
    if (const DatasetInfo* info = FindArray(series.snapshots.front(), "block size"))
        flashApi.CreateBlockSizeShapeAttr().Set(TrailingShape(info->dims));
    const DatasetInfo* firstField = nullptr;
    for (const DatasetInfo& info : series.snapshots.front().arrays)
    {
        if (info.field)
        {
            firstField = &info;
            break;
        }
    }
    if (firstField)
        flashApi.CreateFieldShapeAttr().Set(TrailingShape(firstField->dims));

    for (const DatasetInfo& info : series.snapshots.front().arrays)
    {
        OmniSciArrayAPI arrayApi = OmniSciArrayAPI::Apply(datasetPrim, info.instanceName);
        arrayApi.CreateDeviceAttr().Set(TfToken("cpu"));
        if (info.field)
        {
            OmniSciFieldAPI fieldApi = OmniSciFieldAPI::Apply(datasetPrim, info.instanceName);
            fieldApi.CreateNameAttr().Set(info.sourceName);
            fieldApi.CreateAssociationAttr().Set(TfToken("element"));
        }
    }
    AuthorScalarMap(series, datasetPrim, &Snapshot::scalars, "omni:flash:scalar:");
    AuthorScalarMap(series, datasetPrim, &Snapshot::simInfo, "omni:flash:simInfo:");
    stage->SetDefaultPrim(datasetPrim);

    CaeFileFormatDataRefPtr fileData = CreateCaeFileFormatData(series.options.cacheMode);
    for (const CaeResolverAssetPtr& asset : series.assetLeases)
        fileData->KeepAlive(asset);
    for (size_t arrayIndex = 0; arrayIndex < series.snapshots.front().arrays.size(); ++arrayIndex)
    {
        const DatasetInfo& reference = series.snapshots.front().arrays[arrayIndex];
        const TfToken attrName = ArrayValueAttrName(reference.instanceName);
        if (series.snapshots.size() == 1)
        {
            const Snapshot& snapshot = series.snapshots.front();
            fileData->RegisterLazySingleState(series.options.rootPath, attrName, reference.sdfType, snapshot.sampleTime,
                                              [path = snapshot.filePath, name = reference.sourceName,
                                               kind = reference.kind]() { return ReadNumericArray(path, name, kind); });
        }
        else
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(series.snapshots.size());
            for (const Snapshot& snapshot : series.snapshots)
            {
                const DatasetInfo& info = snapshot.arrays[arrayIndex];
                samples.emplace_back(snapshot.sampleTime,
                                     [path = snapshot.filePath, name = info.sourceName, kind = info.kind]()
                                     { return ReadNumericArray(path, name, kind); });
            }
            fileData->RegisterLazyTimeSamplesSorted(
                series.options.rootPath, attrName, reference.sdfType, std::move(samples));
        }
    }
    return { structure, fileData };
}

} // namespace detail

OmniSciFlashFileFormat::OmniSciFlashFileFormat()
    : SdfFileFormat(OmniSciFlashFileFormatTokens->Id,
                    OmniSciFlashFileFormatTokens->Version,
                    OmniSciFlashFileFormatTokens->Target,
                    OmniSciFlashFileFormatTokens->Extension)
{
}

OmniSciFlashFileFormat::~OmniSciFlashFileFormat() = default;

bool OmniSciFlashFileFormat::CanRead(const std::string& filePath) const
{
    if (TfGetExtension(filePath) != OmniSciFlashFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH] CanRead('%s') -> false (extension mismatch)\n", filePath.c_str());
        return false;
    }
    // Only validate the descriptor. Referenced assets are resolved in Read(),
    // where the descriptor's original identifier is available as their anchor.
    try
    {
        const CaeResolverAssetPtr asset = CaeResolveAsset(filePath);
        detail::ValidateDescriptorFormat(asset->LocalPath());
        TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH] CanRead('%s') -> true\n", filePath.c_str());
        return true;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH] CanRead('%s') -> false: %s\n", filePath.c_str(), ex.what());
        return false;
    }
}

bool OmniSciFlashFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;
    try
    {
        const auto& args = layer->GetFileFormatArguments();
        TF_DEBUG(CAE_FLASH_FILEFORMAT).Msg("[FLASH] Read('%s')\n", resolvedPath.c_str());

        const std::string anchorIdentifier = CaeGetLayerAssetIdentifier(*layer);
        const CaeResolverAssetPtr descriptorAsset = CaeOpenResolverAsset(anchorIdentifier, ArResolvedPath(resolvedPath));
        const auto readArgs = CaePrepareResolverArguments(anchorIdentifier, args);

        detail::ReadFlashResult result = detail::ReadFlash(descriptorAsset->LocalPath(), anchorIdentifier, readArgs);
        if (!result.first || !result.second)
            return false;

        result.second->KeepAlive(descriptorAsset);
        result.second->CopyFrom(_GetLayerData(*result.first));
        SdfAbstractDataRefPtr fileData = result.second;
        _SetLayerData(layer, fileData);

        const SdfPath rootPath = CaeResolveRootPrimPath(anchorIdentifier, args);
        CaeAuthorMountPathOvers(layer, rootPath);
        return true;
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciFlashFileFormat: %s", ex.what());
        return false;
    }
}

void OmniSciFlashFileFormat::ComposeFieldsForFileFormatArguments(const std::string&,
                                                                 const PcpDynamicFileFormatContext& context,
                                                                 FileFormatArguments* args,
                                                                 VtValue*) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciFlashFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(const TfToken& attributeName,
                                                                                     const VtValue& oldValue,
                                                                                     const VtValue& newValue,
                                                                                     const VtValue&) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciFlashFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciFlashFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciFlashFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciFlashFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
