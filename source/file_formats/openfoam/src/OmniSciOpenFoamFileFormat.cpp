// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciOpenFoamFileFormat.h"

#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
#include "DisablePXRWarnings.h"
#include "DynamicFileFormatArguments.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "UninitializedVtArray.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSci/tokens.h>
#include <omniSciFileFormatArgs/tokens.h>
#include <omniSciOpenFoam/boundaryPatchAPI.h>
#include <omniSciOpenFoam/polyMeshAPI.h>
#include <omniSciOpenFoam/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/work/loops.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciOpenFoamFileFormatTokens, OMNI_SCI_OPEN_FOAM_FILE_FORMAT_TOKENS);

namespace fs = std::filesystem;

namespace detail
{

constexpr size_t ReadGrainBytes = 1u << 20;

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 5> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciOpenFoamFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciOpenFoamFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciOpenFoamFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciOpenFoamFileFormatTokens->ArgTimeSource },
            { OmniSciFileFormatArgsTokens->omniCaeFormatStreamingIoThreads, OmniSciOpenFoamFileFormatTokens->ArgIoThreads },
        } };

    return DynamicFileFormatArgs;
}

struct ReadOptions
{
    SdfPath rootPath;
    double timeScale = 1.0;
    double timeOffset = 0.0;
    std::string timeSource = "TimeStep";
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    int ioThreads = 1;
};

enum class FieldKind
{
    Scalar,
    Vector
};

enum class ReadMode
{
    StructureOnly,
    FileDataOnly,
    StructureAndFileData
};

static const char* ReadModeName(ReadMode mode)
{
    switch (mode)
    {
    case ReadMode::StructureOnly:
        return "structureOnly";
    case ReadMode::FileDataOnly:
        return "fileDataOnly";
    case ReadMode::StructureAndFileData:
        return "structureAndFileData";
    }
    return "unknown";
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

static const char* FieldKindName(FieldKind kind)
{
    switch (kind)
    {
    case FieldKind::Scalar:
        return "scalar";
    case FieldKind::Vector:
        return "vector";
    }
    return "unknown";
}

struct BoundaryPatchInfo
{
    std::string name;
    std::string type;
    int startFace = 0;
    int nFaces = 0;
};

struct FieldInfo
{
    std::string sourceName;
    TfToken instanceName;
    FieldKind kind = FieldKind::Scalar;
    std::vector<std::string> samplePaths;
};

struct CaseInfo
{
    std::string casePath;
    std::string caseDir;
    std::string meshDir;
    ReadOptions options;
    int numCells = 0;
    std::vector<BoundaryPatchInfo> patches;
    std::vector<std::string> timeDirs;
    std::vector<double> timeValues;
    std::vector<double> sampleTimes;
    bool sampleTimesAreSorted = true;
    std::vector<FieldInfo> fields;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    CaseInfo caseInfo;
    ReadMode mode = ReadMode::StructureAndFileData;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
};

using ReadOpenFoamResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;

struct FoamHeaderInfo
{
    cae::StringMap<std::string> entries;
    bool binary = false;
    bool littleEndian = true;
    int labelBits = 32;
    int scalarBits = 32;
    std::string foamClass;
};

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static bool ParseDoubleString(const std::string& text, double* value)
{
    if (!value)
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

static void SkipWsAndComments(std::istream& input)
{
    bool lineComment = false;
    bool blockComment = false;
    char c = '\0';
    while (input.get(c))
    {
        if (blockComment)
        {
            if (c == '*' && input.peek() == '/')
            {
                input.get(c);
                blockComment = false;
            }
            continue;
        }
        if (lineComment)
        {
            if (c == '\n' || c == '\r')
                lineComment = false;
            continue;
        }
        if (c == '/' && input.peek() == '/')
        {
            input.get(c);
            lineComment = true;
            continue;
        }
        if (c == '/' && input.peek() == '*')
        {
            input.get(c);
            blockComment = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        input.unget();
        return;
    }
}

static std::string ReadToken(std::istream& input)
{
    SkipWsAndComments(input);
    std::string token;
    char c = '\0';
    if (!input.get(c))
        return token;

    if (c == '"' || c == '\'')
    {
        const char quote = c;
        while (input.get(c) && c != quote)
            token.push_back(c);
        return token;
    }

    if (c == '{' || c == '}' || c == '(' || c == ')' || c == ';')
    {
        token.push_back(c);
        return token;
    }

    token.push_back(c);
    while (input.get(c))
    {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '(' || c == ')' || c == ';')
        {
            input.unget();
            break;
        }
        if (c == '/' && (input.peek() == '/' || input.peek() == '*'))
        {
            input.unget();
            break;
        }
        token.push_back(c);
    }
    return token;
}

static void ExpectToken(std::istream& input, const std::string& expected)
{
    const std::string token = ReadToken(input);
    if (token != expected)
        throw cae::FileFormatError("Expected token '" + expected + "', got '" + token + "'");
}

static int ReadIntToken(std::istream& input)
{
    const std::string token = ReadToken(input);
    if (token.empty())
        throw cae::FileFormatError("Expected integer token");
    return std::stoi(token);
}

static float ReadFloatToken(std::istream& input)
{
    const std::string token = ReadToken(input);
    if (token.empty())
        throw cae::FileFormatError("Expected float token");
    return std::stof(token);
}

static cae::StringMap<std::string> ReadHeader(std::istream& input)
{
    cae::StringMap<std::string> header;
    ExpectToken(input, "FoamFile");
    ExpectToken(input, "{");
    while (input)
    {
        const std::string key = ReadToken(input);
        if (key.empty())
            break;
        if (key == "}")
            break;
        const std::string value = ReadToken(input);
        ExpectToken(input, ";");
        header[key] = value;
    }
    return header;
}

static FoamHeaderInfo ParseHeaderInfo(cae::StringMap<std::string> header, const std::string& path)
{
    FoamHeaderInfo info;
    info.entries = std::move(header);

    const auto formatIt = info.entries.find("format");
    if (formatIt == info.entries.end())
        throw cae::FileFormatError("OpenFOAM header missing format entry: " + path);

    if (formatIt->second == "binary")
    {
        info.binary = true;

        const auto archIt = info.entries.find("arch");
        if (archIt == info.entries.end())
            throw cae::FileFormatError("Binary OpenFOAM header missing arch entry: " + path);

        std::string arch = archIt->second;
        arch.erase(
            std::remove_if(arch.begin(), arch.end(), [](unsigned char c) { return std::isspace(c) != 0; }), arch.end());
        info.littleEndian = cae::StringContains(arch, "LSB");

        const auto labelPos = arch.find("label=");
        if (labelPos != std::string::npos)
            info.labelBits = std::stoi(arch.substr(labelPos + 6));

        const auto scalarPos = arch.find("scalar=");
        if (scalarPos != std::string::npos)
            info.scalarBits = std::stoi(arch.substr(scalarPos + 7));
    }
    else if (formatIt->second != "ascii")
    {
        throw cae::FileFormatError("Unsupported OpenFOAM format '" + formatIt->second + "' in: " + path);
    }

    const auto classIt = info.entries.find("class");
    if (classIt != info.entries.end())
        info.foamClass = classIt->second;

    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] header path='%s' format=%s class='%s' endian=%s labelBits=%d scalarBits=%d\n", path.c_str(),
             info.binary ? "binary" : "ascii", info.foamClass.c_str(), info.littleEndian ? "little" : "big",
             info.labelBits, info.scalarBits);

    return info;
}

static uint64_t ReadUIntToken(std::istream& input)
{
    const std::string token = ReadToken(input);
    if (token.empty())
        throw cae::FileFormatError("Expected unsigned integer token");
    return static_cast<uint64_t>(std::stoull(token));
}

template <typename T>
static void ByteSwapInPlace(T* data, size_t count)
{
    static_assert(std::is_trivially_copyable_v<T>);
    auto* bytes = reinterpret_cast<unsigned char*>(data);
    for (size_t i = 0; i < count; ++i)
        std::reverse(bytes + i * sizeof(T), bytes + (i + 1) * sizeof(T));
}

template <typename T>
static bool ReadContiguousBinary(const std::string& filePath, uint64_t offset, T* data, size_t count, bool swapEndian)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        return false;
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(count * sizeof(T)));
    const bool ok = input.good() || static_cast<size_t>(input.gcount()) == count * sizeof(T);
    if (ok && swapEndian)
        ByteSwapInPlace(data, count);
    return ok;
}

template <typename T>
static VtArray<T> ReadChunkedBinaryArray(
    const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options, bool swapEndian)
{
    UninitializedVtArray<T> values = MakeUninitializedVtArray<T>(count);
    if (count == 0)
        return values.array;

    const size_t elementsPerChunk = std::max<size_t>(1, ReadGrainBytes / sizeof(T));
    const size_t chunkCount = (count + elementsPerChunk - 1) / elementsPerChunk;
    const size_t taskCount = (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;

    std::atomic failed(false);
    WorkParallelForN(taskCount,
                     [&failed, chunkCount, taskCount, elementsPerChunk, count, offset, &filePath, &values, swapEndian](
                         size_t begin, size_t end)
                     {
                         for (size_t task = begin; task < end; ++task)
                         {
                             for (size_t chunk = task; chunk < chunkCount; chunk += taskCount)
                             {
                                 if (failed.load())
                                     return;
                                 const size_t first = chunk * elementsPerChunk;
                                 const size_t n = std::min(elementsPerChunk, count - first);
                                 const uint64_t chunkOffset = offset + first * sizeof(T);
                                 if (!ReadContiguousBinary(filePath, chunkOffset, values.data + first, n, swapEndian))
                                 {
                                     failed.store(true);
                                     return;
                                 }
                             }
                         }
                     });

    if (failed.load())
        throw cae::FileFormatError("Failed to read OpenFOAM binary array data from: " + filePath);

    return std::move(values.array);
}

template <typename T>
static VtIntArray ReadChunkedBinaryIntArrayAsInt(
    const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options, bool swapEndian)
{
    if constexpr (std::is_same_v<T, int>)
    {
        return ReadChunkedBinaryArray<int>(filePath, offset, count, options, swapEndian);
    }
    else
    {
        UninitializedVtArray<int> values = MakeUninitializedVtArray<int>(count);
        if (count == 0)
            return values.array;

        const size_t elementsPerChunk = std::max<size_t>(1, ReadGrainBytes / sizeof(T));
        const size_t chunkCount = (count + elementsPerChunk - 1) / elementsPerChunk;
        const size_t taskCount =
            (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;

        std::atomic failed(false);
        WorkParallelForN(taskCount,
                         [&failed, chunkCount, taskCount, elementsPerChunk, count, offset, &filePath, &values,
                          swapEndian](size_t begin, size_t end)
                         {
                             std::unique_ptr<T[]> scratch;
                             size_t scratchCapacity = 0;
                             for (size_t task = begin; task < end; ++task)
                             {
                                 for (size_t chunk = task; chunk < chunkCount; chunk += taskCount)
                                 {
                                     if (failed.load())
                                         return;
                                     const size_t first = chunk * elementsPerChunk;
                                     const size_t n = std::min(elementsPerChunk, count - first);
                                     const uint64_t chunkOffset = offset + first * sizeof(T);
                                     if (scratchCapacity < n)
                                     {
                                         scratch.reset(new T[n]);
                                         scratchCapacity = n;
                                     }
                                     if (!ReadContiguousBinary(filePath, chunkOffset, scratch.get(), n, swapEndian))
                                     {
                                         failed.store(true);
                                         return;
                                     }
                                     for (size_t i = 0; i < n; ++i)
                                         values.data[first + i] = static_cast<int>(scratch[i]);
                                 }
                             }
                         });

        if (failed.load())
            throw cae::FileFormatError("Failed to read OpenFOAM binary label data from: " + filePath);
        return std::move(values.array);
    }
}

template <typename T>
static VtVec3fArray ReadChunkedBinaryVec3fArray(
    const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options, bool swapEndian)
{
    UninitializedVtArray<GfVec3f> values = MakeUninitializedVtArray<GfVec3f>(count);
    if (count == 0)
        return values.array;

    const size_t tupleStride = sizeof(T) * 3u;
    const size_t tuplesPerChunk = std::max<size_t>(1, ReadGrainBytes / tupleStride);
    const size_t chunkCount = (count + tuplesPerChunk - 1) / tuplesPerChunk;
    const size_t taskCount = (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;
    const bool directOutput = std::is_same_v<T, float> && !swapEndian && std::is_trivially_copyable_v<GfVec3f> &&
                              sizeof(GfVec3f) == sizeof(float) * 3u;

    std::atomic failed(false);
    WorkParallelForN(
        taskCount,
        [&failed, chunkCount, taskCount, tuplesPerChunk, count, offset, tupleStride, directOutput, &filePath, &values,
         swapEndian](size_t begin, size_t end)
        {
            std::unique_ptr<T[]> scratch;
            size_t scratchCapacity = 0;
            for (size_t task = begin; task < end; ++task)
            {
                for (size_t chunk = task; chunk < chunkCount; chunk += taskCount)
                {
                    if (failed.load())
                        return;
                    const size_t first = chunk * tuplesPerChunk;
                    const size_t n = std::min(tuplesPerChunk, count - first);
                    const uint64_t chunkOffset = offset + first * tupleStride;
                    if (directOutput)
                    {
                        if (!ReadContiguousBinary(filePath, chunkOffset, values.data + first, n, false))
                        {
                            failed.store(true);
                            return;
                        }
                        continue;
                    }

                    const size_t scratchCount = n * 3u;
                    if (scratchCapacity < scratchCount)
                    {
                        scratch.reset(new T[scratchCount]);
                        scratchCapacity = scratchCount;
                    }
                    if (!ReadContiguousBinary(filePath, chunkOffset, scratch.get(), scratchCount, swapEndian))
                    {
                        failed.store(true);
                        return;
                    }
                    for (size_t i = 0; i < n; ++i)
                    {
                        values.data[first + i] =
                            GfVec3f(static_cast<float>(scratch[i * 3u]), static_cast<float>(scratch[i * 3u + 1u]),
                                    static_cast<float>(scratch[i * 3u + 2u]));
                    }
                }
            }
        });

    if (failed.load())
        throw cae::FileFormatError("Failed to read OpenFOAM binary vector data from: " + filePath);
    return std::move(values.array);
}

template <typename T>
static VtFloatArray ReadChunkedBinaryScalarArrayAsFloat(
    const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options, bool swapEndian)
{
    if constexpr (std::is_same_v<T, float>)
    {
        return ReadChunkedBinaryArray<float>(filePath, offset, count, options, swapEndian);
    }
    else
    {
        UninitializedVtArray<float> values = MakeUninitializedVtArray<float>(count);
        if (count == 0)
            return values.array;

        const size_t elementsPerChunk = std::max<size_t>(1, ReadGrainBytes / sizeof(T));
        const size_t chunkCount = (count + elementsPerChunk - 1) / elementsPerChunk;
        const size_t taskCount =
            (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;

        std::atomic failed(false);
        WorkParallelForN(taskCount,
                         [&failed, chunkCount, taskCount, elementsPerChunk, count, offset, &filePath, &values,
                          swapEndian](size_t begin, size_t end)
                         {
                             std::unique_ptr<T[]> scratch;
                             size_t scratchCapacity = 0;
                             for (size_t task = begin; task < end; ++task)
                             {
                                 for (size_t chunk = task; chunk < chunkCount; chunk += taskCount)
                                 {
                                     if (failed.load())
                                         return;
                                     const size_t first = chunk * elementsPerChunk;
                                     const size_t n = std::min(elementsPerChunk, count - first);
                                     const uint64_t chunkOffset = offset + first * sizeof(T);
                                     if (scratchCapacity < n)
                                     {
                                         scratch.reset(new T[n]);
                                         scratchCapacity = n;
                                     }
                                     if (!ReadContiguousBinary(filePath, chunkOffset, scratch.get(), n, swapEndian))
                                     {
                                         failed.store(true);
                                         return;
                                     }
                                     for (size_t i = 0; i < n; ++i)
                                         values.data[first + i] = static_cast<float>(scratch[i]);
                                 }
                             }
                         });

        if (failed.load())
            throw cae::FileFormatError("Failed to read OpenFOAM binary scalar data from: " + filePath);
        return std::move(values.array);
    }
}

static bool TryReadNumCellsFromOwnerNote(const std::string& ownerPath, int* numCells)
{
    if (!numCells)
        return false;

    std::ifstream input(ownerPath, std::ios::binary);
    if (!input)
        return false;

    const FoamHeaderInfo header = ParseHeaderInfo(ReadHeader(input), ownerPath);
    const auto noteIt = header.entries.find("note");
    if (noteIt == header.entries.end())
        return false;

    const std::string& note = noteIt->second;
    const std::string needle = "nCells:";
    const size_t pos = note.find(needle);
    if (pos == std::string::npos)
        return false;

    size_t cursor = pos + needle.size();
    while (cursor < note.size() && std::isspace(static_cast<unsigned char>(note[cursor])))
        ++cursor;

    size_t end = cursor;
    while (end < note.size() && std::isdigit(static_cast<unsigned char>(note[end])))
        ++end;
    if (end == cursor)
        return false;

    *numCells = std::stoi(note.substr(cursor, end - cursor));
    return true;
}

static VtVec3fArray LoadPointsArray(const std::string& path, const ReadOptions& options)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM points file: " + path);
    const FoamHeaderInfo header = ParseHeaderInfo(ReadHeader(input), path);

    const auto count = static_cast<int>(ReadUIntToken(input));
    ExpectToken(input, "(");

    if (!header.binary)
    {
        UninitializedVtArray<GfVec3f> out = MakeUninitializedVtArray<GfVec3f>(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            ExpectToken(input, "(");
            const float x = ReadFloatToken(input);
            const float y = ReadFloatToken(input);
            const float z = ReadFloatToken(input);
            ExpectToken(input, ")");
            out.data[static_cast<size_t>(i)] = GfVec3f(x, y, z);
        }
        ExpectToken(input, ")");
        return std::move(out.array);
    }

    const auto dataOffset = static_cast<uint64_t>(input.tellg());
    const bool swapEndian = !header.littleEndian;
    if (header.scalarBits == 64)
        return ReadChunkedBinaryVec3fArray<double>(path, dataOffset, static_cast<size_t>(count), options, swapEndian);
    if (header.scalarBits == 32)
        return ReadChunkedBinaryVec3fArray<float>(path, dataOffset, static_cast<size_t>(count), options, swapEndian);
    throw cae::FileFormatError("Unsupported OpenFOAM scalar bit width in points file: " + path);
}

static VtIntArray LoadBinaryLabelArray(
    const std::string& path, uint64_t dataOffset, size_t count, const FoamHeaderInfo& header, const ReadOptions& options)
{
    const bool swapEndian = !header.littleEndian;
    if (header.labelBits == 64)
        return ReadChunkedBinaryIntArrayAsInt<int64_t>(path, dataOffset, count, options, swapEndian);
    if (header.labelBits == 32)
        return ReadChunkedBinaryIntArrayAsInt<int32_t>(path, dataOffset, count, options, swapEndian);
    throw cae::FileFormatError("Unsupported OpenFOAM label bit width: " + path);
}

struct FaceArrayInfo
{
    bool compactBinary = false;
    FoamHeaderInfo header;
    uint64_t offsetsDataOffset = 0;
    uint64_t indicesDataOffset = 0;
    size_t offsetsCount = 0;
    size_t indicesCount = 0;
};

static FaceArrayInfo InspectFaceArrays(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM faces file: " + path);

    FaceArrayInfo info;
    info.header = ParseHeaderInfo(ReadHeader(input), path);
    if (info.header.foamClass != "faceCompactList")
        return info;
    if (!info.header.binary)
        throw cae::FileFormatError("ASCII faceCompactList is not supported in: " + path);

    const auto labelBytes = static_cast<size_t>(info.header.labelBits / 8);
    info.offsetsCount = ReadUIntToken(input);
    ExpectToken(input, "(");
    info.offsetsDataOffset = static_cast<uint64_t>(input.tellg());
    input.seekg(static_cast<std::streamoff>(info.offsetsCount * labelBytes), std::ios::cur);
    ExpectToken(input, ")");

    info.indicesCount = ReadUIntToken(input);
    ExpectToken(input, "(");
    info.indicesDataOffset = static_cast<uint64_t>(input.tellg());
    info.compactBinary = true;
    return info;
}

static std::pair<VtIntArray, VtIntArray> LoadFaceArrays(const std::string& path, const ReadOptions& options)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM faces file: " + path);
    const FoamHeaderInfo header = ParseHeaderInfo(ReadHeader(input), path);

    if (header.foamClass == "faceCompactList")
    {
        if (!header.binary)
            throw cae::FileFormatError("ASCII faceCompactList is not supported in: " + path);

        const auto offsetsCount = ReadUIntToken(input);
        ExpectToken(input, "(");
        const auto offsetsDataOffset = static_cast<uint64_t>(input.tellg());
        input.seekg(static_cast<std::streamoff>(offsetsCount * static_cast<size_t>(header.labelBits / 8)), std::ios::cur);
        ExpectToken(input, ")");

        const auto indicesCount = ReadUIntToken(input);
        ExpectToken(input, "(");
        const auto indicesDataOffset = static_cast<uint64_t>(input.tellg());

        return { LoadBinaryLabelArray(path, indicesDataOffset, indicesCount, header, options),
                 LoadBinaryLabelArray(path, offsetsDataOffset, offsetsCount, header, options) };
    }

    if (header.foamClass != "faceList")
        throw cae::FileFormatError("Unsupported OpenFOAM faces class '" + header.foamClass + "' in: " + path);

    const auto faceCount = static_cast<int>(ReadUIntToken(input));
    ExpectToken(input, "(");
    std::vector<int> offsets;
    std::vector<int> indices;
    offsets.reserve(static_cast<size_t>(faceCount + 1));
    offsets.push_back(0);
    for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex)
    {
        const int pointCount = ReadIntToken(input);
        ExpectToken(input, "(");
        for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            indices.push_back(ReadIntToken(input));
        ExpectToken(input, ")");
        offsets.push_back(static_cast<int>(indices.size()));
    }
    ExpectToken(input, ")");
    return { VtIntArray(indices.begin(), indices.end()), VtIntArray(offsets.begin(), offsets.end()) };
}

static VtIntArray LoadFaceIndicesArray(const std::string& path, const FaceArrayInfo& info, const ReadOptions& options)
{
    if (info.compactBinary)
        return LoadBinaryLabelArray(path, info.indicesDataOffset, info.indicesCount, info.header, options);
    return LoadFaceArrays(path, options).first;
}

static VtIntArray LoadFaceOffsetsArray(const std::string& path, const FaceArrayInfo& info, const ReadOptions& options)
{
    if (info.compactBinary)
        return LoadBinaryLabelArray(path, info.offsetsDataOffset, info.offsetsCount, info.header, options);
    return LoadFaceArrays(path, options).second;
}

static VtIntArray LoadLabelList(const std::string& path, const ReadOptions& options)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM label list file: " + path);
    const FoamHeaderInfo header = ParseHeaderInfo(ReadHeader(input), path);

    const auto count = ReadUIntToken(input);
    ExpectToken(input, "(");
    if (!header.binary)
    {
        UninitializedVtArray<int> out = MakeUninitializedVtArray<int>(count);
        for (size_t i = 0; i < count; ++i)
            out.data[i] = ReadIntToken(input);
        ExpectToken(input, ")");
        return std::move(out.array);
    }

    const auto dataOffset = static_cast<uint64_t>(input.tellg());
    return LoadBinaryLabelArray(path, dataOffset, count, header, options);
}

static int ComputeNumCells(const std::string& ownerPath, const std::string& neighbourPath, const ReadOptions& options)
{
    int numCells = 0;
    if (TryReadNumCellsFromOwnerNote(ownerPath, &numCells))
    {
        TF_DEBUG(CAE_OPENFOAM_FILEFORMAT).Msg("[OpenFOAM] numCells=%d from owner note '%s'\n", numCells, ownerPath.c_str());
        return numCells;
    }

    const VtIntArray owner = LoadLabelList(ownerPath, options);
    const VtIntArray neighbour = LoadLabelList(neighbourPath, options);
    int maxId = -1;
    for (const int value : owner)
        maxId = std::max(maxId, value);
    for (const int value : neighbour)
        maxId = std::max(maxId, value);
    numCells = maxId + 1;
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] numCells=%d derived from owner/neighbour arrays (owner=%zu neighbour=%zu)\n", numCells,
             owner.size(), neighbour.size());
    return numCells;
}

struct FieldDescriptor
{
    FieldKind kind = FieldKind::Scalar;
    bool uniform = false;
};

static FieldDescriptor InspectInternalField(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM field file: " + path);
    ParseHeaderInfo(ReadHeader(input), path);

    std::string token;
    do
    {
        token = ReadToken(input);
    } while (!token.empty() && token != "internalField");
    if (token != "internalField")
        throw cae::FileFormatError("OpenFOAM field file missing internalField: " + path);

    FieldDescriptor desc;
    const std::string uniformity = ReadToken(input);
    desc.uniform = (uniformity == "uniform");
    if (!desc.uniform && uniformity != "nonuniform")
        throw cae::FileFormatError("Unsupported internalField form in: " + path);

    const std::string typeToken = ReadToken(input);
    if (cae::StringContains(typeToken, "vector"))
        desc.kind = FieldKind::Vector;
    else if (cae::StringContains(typeToken, "scalar") || desc.uniform)
        desc.kind = FieldKind::Scalar;

    if (desc.uniform && typeToken == "(")
        desc.kind = FieldKind::Vector;

    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] inspected field '%s': kind=%s uniform=%d\n", path.c_str(), FieldKindName(desc.kind),
             desc.uniform ? 1 : 0);
    return desc;
}

static VtValue LoadInternalField(const std::string& path, FieldKind kind, int numCells, const ReadOptions& options)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM field file: " + path);
    const FoamHeaderInfo header = ParseHeaderInfo(ReadHeader(input), path);

    std::string token;
    do
    {
        token = ReadToken(input);
    } while (!token.empty() && token != "internalField");
    if (token != "internalField")
        throw cae::FileFormatError("OpenFOAM field file missing internalField: " + path);

    const std::string uniformity = ReadToken(input);
    if (uniformity == "uniform")
    {
        if (kind == FieldKind::Scalar)
        {
            const float value = ReadFloatToken(input);
            VtFloatArray values(static_cast<size_t>(numCells), value);
            return VtValue::Take(values);
        }

        ExpectToken(input, "(");
        const float x = ReadFloatToken(input);
        const float y = ReadFloatToken(input);
        const float z = ReadFloatToken(input);
        ExpectToken(input, ")");
        VtVec3fArray values(static_cast<size_t>(numCells), GfVec3f(x, y, z));
        return VtValue::Take(values);
    }

    if (uniformity != "nonuniform")
        throw cae::FileFormatError("Unsupported internalField form in: " + path);

    ReadToken(input); // List<scalar> or List<vector>
    const auto count = static_cast<int>(ReadUIntToken(input));
    ExpectToken(input, "(");

    if (!header.binary)
    {
        if (kind == FieldKind::Scalar)
        {
            UninitializedVtArray<float> out = MakeUninitializedVtArray<float>(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
                out.data[static_cast<size_t>(i)] = ReadFloatToken(input);
            ExpectToken(input, ")");
            return VtValue::Take(out.array);
        }

        UninitializedVtArray<GfVec3f> out = MakeUninitializedVtArray<GfVec3f>(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            ExpectToken(input, "(");
            const float x = ReadFloatToken(input);
            const float y = ReadFloatToken(input);
            const float z = ReadFloatToken(input);
            ExpectToken(input, ")");
            out.data[static_cast<size_t>(i)] = GfVec3f(x, y, z);
        }
        ExpectToken(input, ")");
        return VtValue::Take(out.array);
    }

    const auto dataOffset = static_cast<uint64_t>(input.tellg());
    const bool swapEndian = !header.littleEndian;
    if (kind == FieldKind::Scalar)
    {
        if (header.scalarBits == 64)
        {
            VtFloatArray values = ReadChunkedBinaryScalarArrayAsFloat<double>(
                path, dataOffset, static_cast<size_t>(count), options, swapEndian);
            return VtValue::Take(values);
        }
        if (header.scalarBits == 32)
        {
            VtFloatArray values = ReadChunkedBinaryScalarArrayAsFloat<float>(
                path, dataOffset, static_cast<size_t>(count), options, swapEndian);
            return VtValue::Take(values);
        }
        throw cae::FileFormatError("Unsupported OpenFOAM scalar bit width in field file: " + path);
    }

    if (header.scalarBits == 64)
    {
        VtVec3fArray values =
            ReadChunkedBinaryVec3fArray<double>(path, dataOffset, static_cast<size_t>(count), options, swapEndian);
        return VtValue::Take(values);
    }
    if (header.scalarBits == 32)
    {
        VtVec3fArray values =
            ReadChunkedBinaryVec3fArray<float>(path, dataOffset, static_cast<size_t>(count), options, swapEndian);
        return VtValue::Take(values);
    }
    throw cae::FileFormatError("Unsupported OpenFOAM scalar bit width in field file: " + path);
}

static std::vector<BoundaryPatchInfo> ParseBoundaryFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open OpenFOAM boundary file: " + path);
    ParseHeaderInfo(ReadHeader(input), path);

    const int patchCount = ReadIntToken(input);
    ExpectToken(input, "(");
    std::vector<BoundaryPatchInfo> patches;
    patches.reserve(static_cast<size_t>(patchCount));
    for (int patchIndex = 0; patchIndex < patchCount; ++patchIndex)
    {
        BoundaryPatchInfo patch;
        patch.name = ReadToken(input);
        ExpectToken(input, "{");
        while (true)
        {
            const std::string key = ReadToken(input);
            if (key == "}")
                break;
            if (key == "type")
            {
                patch.type = ReadToken(input);
                ExpectToken(input, ";");
            }
            else if (key == "startFace")
            {
                patch.startFace = ReadIntToken(input);
                ExpectToken(input, ";");
            }
            else if (key == "nFaces")
            {
                patch.nFaces = ReadIntToken(input);
                ExpectToken(input, ";");
            }
            else
            {
                int parenDepth = 0;
                while (true)
                {
                    const std::string token = ReadToken(input);
                    if (token.empty())
                        throw cae::FileFormatError("Unexpected EOF while parsing boundary file: " + path);
                    if (token == ";" && parenDepth == 0)
                        break;
                    if (token == "(")
                        ++parenDepth;
                    else if (token == ")")
                        parenDepth = std::max(0, parenDepth - 1);
                }
            }
        }
        TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
            .Msg("[OpenFOAM] boundary patch name='%s' type='%s' startFace=%d nFaces=%d\n", patch.name.c_str(),
                 patch.type.c_str(), patch.startFace, patch.nFaces);
        patches.push_back(patch);
    }
    ExpectToken(input, ")");
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] parsed %zu boundary patch(es) from '%s'\n", patches.size(), path.c_str());
    return patches;
}

static ReadOptions ParseReadOptions(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key) -> std::string
    {
        const auto it = args.find(key.GetString());
        return it != args.end() ? it->second : std::string{};
    };

    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(fname, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);

    const std::string timeScale = getArg(OmniSciOpenFoamFileFormatTokens->ArgTimeScale);
    if (!timeScale.empty())
        options.timeScale = std::stod(timeScale);

    const std::string timeOffset = getArg(OmniSciOpenFoamFileFormatTokens->ArgTimeOffset);
    if (!timeOffset.empty())
        options.timeOffset = std::stod(timeOffset);

    const std::string timeSource = getArg(OmniSciOpenFoamFileFormatTokens->ArgTimeSource);
    if (!timeSource.empty())
        options.timeSource = timeSource;

    const std::string ioThreads = getArg(OmniSciOpenFoamFileFormatTokens->ArgIoThreads);

    options.ioThreads = ioThreads.empty() ? 1 : std::max(1, std::stoi(ioThreads));

    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] options root=%s cacheMode=%s timeSource='%s' timeScale=%g timeOffset=%g ioThreads=%d\n",
             options.rootPath.GetText(), CacheModeName(options.cacheMode), options.timeSource.c_str(),
             options.timeScale, options.timeOffset, options.ioThreads);
    return options;
}

static double ResolveSampleTime(const CaseInfo& caseInfo, size_t sampleIndex)
{
    const bool useTimeValues = TfStringToLower(caseInfo.options.timeSource) == "timevalue";
    const double rawTime = (useTimeValues && sampleIndex < caseInfo.timeValues.size()) ?
                               caseInfo.timeValues[sampleIndex] :
                               static_cast<double>(sampleIndex);
    return rawTime * caseInfo.options.timeScale + caseInfo.options.timeOffset;
}

static std::vector<double> ResolveSampleTimes(const CaseInfo& caseInfo)
{
    const bool useTimeValues = TfStringToLower(caseInfo.options.timeSource) == "timevalue";

    std::vector<double> sampleTimes;
    sampleTimes.reserve(caseInfo.timeValues.size());
    for (size_t sampleIndex = 0; sampleIndex < caseInfo.timeValues.size(); ++sampleIndex)
    {
        const double rawTime = useTimeValues ? caseInfo.timeValues[sampleIndex] : static_cast<double>(sampleIndex);
        sampleTimes.push_back(rawTime * caseInfo.options.timeScale + caseInfo.options.timeOffset);
    }
    return sampleTimes;
}

static double GetSampleTime(const CaseInfo& caseInfo, size_t sampleIndex)
{
    return sampleIndex < caseInfo.sampleTimes.size() ? caseInfo.sampleTimes[sampleIndex] :
                                                       ResolveSampleTime(caseInfo, sampleIndex);
}

static std::vector<std::pair<std::string, double>> ScanTimeDirectories(const std::string& caseDir)
{
    std::vector<std::pair<std::string, double>> out;
    for (const fs::directory_entry& entry : fs::directory_iterator(caseDir))
    {
        if (!entry.is_directory())
            continue;
        double value = 0.0;
        if (!ParseDoubleString(entry.path().filename().string(), &value))
            continue;
        out.emplace_back(entry.path().filename().string(), value);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] discovered %zu numeric time %s in '%s'\n", out.size(),
             out.size() == 1 ? "directory" : "directories", caseDir.c_str());
    return out;
}

static CaseInfo ParseCase(const std::string& casePath, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT).Msg("[OpenFOAM] ParseCase('%s')\n", casePath.c_str());
    CaseInfo info;
    info.casePath = casePath;
    info.caseDir = fs::path(casePath).parent_path().string();
    info.meshDir = (fs::path(info.caseDir) / "constant" / "polyMesh").string();
    info.options = ParseReadOptions(casePath, args);

    const fs::path ownerPath = fs::path(info.meshDir) / "owner";
    const fs::path neighbourPath = fs::path(info.meshDir) / "neighbour";
    const fs::path boundaryPath = fs::path(info.meshDir) / "boundary";
    info.numCells = ComputeNumCells(ownerPath.string(), neighbourPath.string(), info.options);
    info.patches = ParseBoundaryFile(boundaryPath.string());

    const auto times = ScanTimeDirectories(info.caseDir);
    for (const auto& [directory, value] : times)
    {
        info.timeDirs.push_back(directory);
        info.timeValues.push_back(value);
    }
    info.sampleTimes = ResolveSampleTimes(info);
    info.sampleTimesAreSorted = std::is_sorted(info.sampleTimes.begin(), info.sampleTimes.end());

    if (!info.timeDirs.empty())
    {
        const fs::path firstDir = fs::path(info.caseDir) / info.timeDirs.front();
        for (const fs::directory_entry& entry : fs::directory_iterator(firstDir))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string sourceName = entry.path().filename().string();
            try
            {
                const FieldDescriptor desc = InspectInternalField(entry.path().string());
                FieldInfo field;
                field.sourceName = sourceName;
                field.instanceName = TfToken(TfMakeValidIdentifier(sourceName));
                field.kind = desc.kind;

                bool presentOnAllSamples = true;
                for (const std::string& timeDir : info.timeDirs)
                {
                    const fs::path samplePath = fs::path(info.caseDir) / timeDir / sourceName;
                    if (!fs::is_regular_file(samplePath))
                    {
                        presentOnAllSamples = false;
                        break;
                    }
                    field.samplePaths.push_back(samplePath.string());
                }
                if (presentOnAllSamples)
                {
                    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                        .Msg("[OpenFOAM] field '%s' accepted kind=%s samples=%zu\n", sourceName.c_str(),
                             FieldKindName(field.kind), field.samplePaths.size());
                    info.fields.push_back(std::move(field));
                }
                else
                {
                    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                        .Msg("[OpenFOAM] field '%s' skipped: missing one or more time samples\n", sourceName.c_str());
                }
            }
            catch (const cae::FileFormatError& ex)
            {
                // Skip unsupported field files in v1.
                TF_DEBUG(CAE_OPENFOAM_FILEFORMAT).Msg("[OpenFOAM] field '%s' skipped: %s\n", sourceName.c_str(), ex.what());
            }
        }
    }

    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] case summary cells=%d patches=%zu timeDirs=%zu sampleTimesSorted=%d fields=%zu\n", info.numCells,
             info.patches.size(), info.timeDirs.size(), info.sampleTimesAreSorted ? 1 : 0, info.fields.size());
    return info;
}

static bool ShouldAuthorStructure(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::FileDataOnly;
}

static bool ShouldRegisterFileData(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::StructureOnly;
}

static ReadContext CreateReadContext(const CaseInfo& caseInfo, ReadMode mode)
{
    ReadContext ctx;
    ctx.caseInfo = caseInfo;
    ctx.mode = mode;
    if (ShouldAuthorStructure(ctx))
    {
        ctx.layer = SdfLayer::CreateAnonymous();
        ctx.stage = UsdStage::Open(ctx.layer);
        UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
        // Canonical simulation-seconds timecodes: self-describe the unit so
        // direct Stage.Open plays at real-time and composition tooling can
        // reconcile host-stage TCPS through SdfLayerOffset.
        ctx.stage->SetTimeCodesPerSecond(1.0);
    }
    if (ShouldRegisterFileData(ctx))
        ctx.fileData = CreateCaeFileFormatData(ctx.caseInfo.options.cacheMode);
    return ctx;
}

static void RegisterTimeSamples(ReadContext& ctx,
                                const SdfPath& primPath,
                                const TfToken& attrName,
                                const TfToken& typeName,
                                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples)
{
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] register %s time samples prim=%s attr=%s type=%s count=%zu\n",
             ctx.caseInfo.sampleTimesAreSorted ? "sorted" : "unsorted", primPath.GetText(), attrName.GetText(),
             typeName.GetText(), samples.size());
    if (ctx.caseInfo.sampleTimesAreSorted)
        ctx.fileData->RegisterLazyTimeSamplesSorted(primPath, attrName, typeName, std::move(samples));
    else
        ctx.fileData->RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples));
}

static CaeFileFormatData::Loader MakePointsLoader(const std::string& path, const ReadOptions& options)
{
    return [path, options]()
    {
        VtVec3fArray values = LoadPointsArray(path, options);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeFaceIndicesLoader(const std::string& path,
                                                       const FaceArrayInfo& info,
                                                       const ReadOptions& options)
{
    return [path, info, options]()
    {
        VtIntArray values = LoadFaceIndicesArray(path, info, options);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeFaceOffsetsLoader(const std::string& path,
                                                       const FaceArrayInfo& info,
                                                       const ReadOptions& options)
{
    return [path, info, options]()
    {
        VtIntArray values = LoadFaceOffsetsArray(path, info, options);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeLabelLoader(const std::string& path, const ReadOptions& options)
{
    return [path, options]()
    {
        VtIntArray values = LoadLabelList(path, options);
        return VtValue::Take(values);
    };
}

static CaeFileFormatData::Loader MakeFieldLoader(const std::string& path,
                                                 FieldKind kind,
                                                 int numCells,
                                                 const ReadOptions& options)
{
    return [path, kind, numCells, options]() { return LoadInternalField(path, kind, numCells, options); };
}

static ReadOpenFoamResult ReadOpenFoam(const std::string& casePath, const SdfLayer::FileFormatArguments& args, ReadMode mode)
{
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] ReadOpenFoam('%s') mode=%s args=%zu\n", casePath.c_str(), ReadModeName(mode), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_OPENFOAM_FILEFORMAT).Msg("[OpenFOAM]   arg %s='%s'\n", key.c_str(), value.c_str());

    const CaseInfo caseInfo = ParseCase(casePath, args);
    ReadContext ctx = CreateReadContext(caseInfo, mode);
    const bool authorStructure = ShouldAuthorStructure(ctx);
    const bool registerFileData = ShouldRegisterFileData(ctx);
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("[OpenFOAM] authorStructure=%d registerFileData=%d root=%s\n", authorStructure ? 1 : 0,
             registerFileData ? 1 : 0, caseInfo.options.rootPath.GetText());

    const SdfPath rootPath = caseInfo.options.rootPath;
    if (authorStructure)
        UsdGeomScope::Define(ctx.stage, rootPath);

    const SdfPath volumePath = rootPath.AppendChild(TfToken("Volume"));
    UsdPrim volumePrim;
    if (authorStructure)
    {
        volumePrim = OmniSciDataset::Define(ctx.stage, volumePath).GetPrim();
        OmniSciOpenFoamPolyMeshAPI::Apply(volumePrim);
    }

    const fs::path meshDir(caseInfo.meshDir);
    const std::string pointsPath = (meshDir / "points").string();
    const std::string facesPath = (meshDir / "faces").string();
    const std::string ownerPath = (meshDir / "owner").string();
    const std::string neighbourPath = (meshDir / "neighbour").string();
    const FaceArrayInfo faceInfo = InspectFaceArrays(facesPath);

    const auto registerArray = [&](const TfToken& arrayName, const TfToken& typeName, CaeFileFormatData::Loader loader)
    {
        if (authorStructure)
        {
            OmniSciArrayAPI array = OmniSciArrayAPI::Apply(volumePrim, arrayName);
            array.CreateDeviceAttr().Set(TfToken("cpu"));
        }
        if (registerFileData)
        {
            TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                .Msg("[OpenFOAM] register mesh array %s type=%s on %s\n", arrayName.GetText(), typeName.GetText(),
                     volumePath.GetText());
            ctx.fileData->RegisterLazySingleState(
                volumePath, MakeArrayValueAttrName(arrayName), typeName, GetSampleTime(caseInfo, 0), std::move(loader));
        }
    };

    registerArray(OmniSciOpenFoamTokens->points, TfToken("float3[]"), MakePointsLoader(pointsPath, caseInfo.options));
    registerArray(
        OmniSciOpenFoamTokens->faces, TfToken("int[]"), MakeFaceIndicesLoader(facesPath, faceInfo, caseInfo.options));
    registerArray(OmniSciOpenFoamTokens->facesOffsets, TfToken("int[]"),
                  MakeFaceOffsetsLoader(facesPath, faceInfo, caseInfo.options));
    registerArray(OmniSciOpenFoamTokens->owner, TfToken("int[]"), MakeLabelLoader(ownerPath, caseInfo.options));
    registerArray(OmniSciOpenFoamTokens->neighbour, TfToken("int[]"), MakeLabelLoader(neighbourPath, caseInfo.options));

    if (authorStructure)
    {
        const SdfPath boundariesPath = rootPath.AppendChild(TfToken("Boundaries"));
        UsdGeomScope::Define(ctx.stage, boundariesPath);
        for (const BoundaryPatchInfo& patch : caseInfo.patches)
        {
            const SdfPath patchPath = boundariesPath.AppendChild(TfToken(TfMakeValidIdentifier(patch.name)));
            UsdPrim patchPrim = OmniSciDataset::Define(ctx.stage, patchPath).GetPrim();
            OmniSciOpenFoamBoundaryPatchAPI api = OmniSciOpenFoamBoundaryPatchAPI::Apply(patchPrim);
            api.CreateMeshRel().SetTargets({ volumePath });
            api.CreateNameAttr().Set(patch.name);
            api.CreateTypeAttr().Set(patch.type);
            api.CreateStartFaceAttr().Set(patch.startFace);
            api.CreateNFacesAttr().Set(patch.nFaces);
            TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                .Msg("[OpenFOAM] authored boundary patch '%s' at %s\n", patch.name.c_str(), patchPath.GetText());
        }
    }

    for (const FieldInfo& field : caseInfo.fields)
    {
        if (authorStructure)
        {
            OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(volumePrim, field.instanceName);
            fieldAPI.CreateNameAttr().Set(field.sourceName);
            fieldAPI.CreateAssociationAttr().Set(TfToken("element"));

            OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(volumePrim, field.instanceName);
            arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
            TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                .Msg("[OpenFOAM] authored field '%s' instance=%s kind=%s\n", field.sourceName.c_str(),
                     field.instanceName.GetText(), FieldKindName(field.kind));
        }

        if (!registerFileData)
            continue;

        const TfToken valueAttr = MakeArrayValueAttrName(field.instanceName);
        const TfToken valueType = field.kind == FieldKind::Scalar ? TfToken("float[]") : TfToken("float3[]");
        if (field.samplePaths.size() > 1)
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(field.samplePaths.size());
            for (size_t i = 0; i < field.samplePaths.size(); ++i)
            {
                samples.push_back({ GetSampleTime(caseInfo, i), MakeFieldLoader(field.samplePaths[i], field.kind,
                                                                                caseInfo.numCells, caseInfo.options) });
            }
            RegisterTimeSamples(ctx, volumePath, valueAttr, valueType, std::move(samples));
        }
        else if (!field.samplePaths.empty())
        {
            TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
                .Msg("[OpenFOAM] register static field '%s' type=%s path='%s'\n", field.sourceName.c_str(),
                     valueType.GetText(), field.samplePaths.front().c_str());
            ctx.fileData->RegisterLazySingleState(
                volumePath, valueAttr, valueType, GetSampleTime(caseInfo, 0),
                MakeFieldLoader(field.samplePaths.front(), field.kind, caseInfo.numCells, caseInfo.options));
        }
    }

    return { ctx.layer, ctx.fileData };
}

} // namespace detail

OmniSciOpenFoamFileFormat::OmniSciOpenFoamFileFormat()
    : SdfFileFormat(OmniSciOpenFoamFileFormatTokens->Id,
                    OmniSciOpenFoamFileFormatTokens->Version,
                    OmniSciOpenFoamFileFormatTokens->Target,
                    OmniSciOpenFoamFileFormatTokens->Extension)
{
}

OmniSciOpenFoamFileFormat::~OmniSciOpenFoamFileFormat() = default;

bool OmniSciOpenFoamFileFormat::CanRead(const std::string& filePath) const
{
    if (TfGetExtension(filePath) != OmniSciOpenFoamFileFormatTokens->Extension)
    {
        TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
            .Msg("OmniSciOpenFoamFileFormat::CanRead('%s') -> false (extension mismatch)\n", filePath.c_str());
        return false;
    }
    if (!CaeCanScanAdjacentFiles(filePath, ArResolvedPath(filePath)))
        return false;

    const fs::path caseDir = fs::path(filePath).parent_path();
    const bool hasPoints = fs::exists(caseDir / "constant" / "polyMesh" / "points");
    const bool hasFaces = fs::exists(caseDir / "constant" / "polyMesh" / "faces");
    const bool hasOwner = fs::exists(caseDir / "constant" / "polyMesh" / "owner");
    const bool hasNeighbour = fs::exists(caseDir / "constant" / "polyMesh" / "neighbour");
    const bool hasBoundary = fs::exists(caseDir / "constant" / "polyMesh" / "boundary");
    const bool result = hasPoints && hasFaces && hasOwner && hasNeighbour && hasBoundary;
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT)
        .Msg("OmniSciOpenFoamFileFormat::CanRead('%s') -> %d points=%d faces=%d owner=%d neighbour=%d boundary=%d\n",
             filePath.c_str(), result ? 1 : 0, hasPoints ? 1 : 0, hasFaces ? 1 : 0, hasOwner ? 1 : 0,
             hasNeighbour ? 1 : 0, hasBoundary ? 1 : 0);
    return result;
}

bool OmniSciOpenFoamFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    try
    {
        CaeRequireAdjacentFileScanning("OpenFOAM", identifier, ArResolvedPath(resolvedPath));
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciOpenFoamFileFormat: %s", ex.what());
        return false;
    }
    const auto readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
    TF_DEBUG(CAE_OPENFOAM_FILEFORMAT).Msg("OmniSciOpenFoamFileFormat::Read('%s')\n", resolvedPath.c_str());

    auto result = detail::ReadOpenFoam(resolvedPath, readArgs, detail::ReadMode::StructureAndFileData);
    if (!result.first || !result.second)
        return false;

    result.second->CopyFrom(_GetLayerData(*result.first));
    SdfAbstractDataRefPtr fileData = result.second;
    _SetLayerData(layer, fileData);

    SdfPath rootPath;
    try
    {
        rootPath = CaeResolveRootPrimPath(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciOpenFoamFileFormat: %s", ex.what());
        return false;
    }
    CaeAuthorMountPathOvers(layer, rootPath);
    return true;
}

void OmniSciOpenFoamFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                    const PcpDynamicFileFormatContext& context,
                                                                    FileFormatArguments* args,
                                                                    VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciOpenFoamFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciOpenFoamFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciOpenFoamFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciOpenFoamFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciOpenFoamFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
