// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DatasetSpecCache.h"

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

constexpr size_t MaxCachedSpecs = 16;
constexpr size_t QuickHashPrefixBytes = 64u * 1024u;

bool SignaturesMatch(const FileSignature& lhs, const FileSignature& rhs)
{
    return lhs.resolvedPath == rhs.resolvedPath && lhs.fileSize == rhs.fileSize &&
           lhs.lastWriteTime == rhs.lastWriteTime && lhs.quickContentHash == rhs.quickContentHash;
}

uint64_t FnvaUpdate(uint64_t hash, const unsigned char* data, size_t size)
{
    constexpr uint64_t Prime = 1099511628211ULL;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= Prime;
    }
    return hash;
}

uint64_t ComputeQuickContentHash(const std::string& resolvedPath, uintmax_t fileSize)
{
    constexpr uint64_t OffsetBasis = 14695981039346656037ULL;
    std::ifstream input(resolvedPath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("Failed to open VTK file while computing metadata cache signature: " + resolvedPath);

    const auto byteCount = std::min<uintmax_t>(fileSize, QuickHashPrefixBytes);
    std::string bytes(byteCount, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<size_t>(input.gcount()) != byteCount)
        throw cae::FileFormatError("Failed to read VTK file prefix while computing metadata cache signature: " +
                                   resolvedPath);

    uint64_t hash = OffsetBasis;
    hash = FnvaUpdate(hash, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    hash = FnvaUpdate(hash, reinterpret_cast<const unsigned char*>(&fileSize), sizeof(fileSize));
    return hash;
}

FileSignature ComputeFileSignature(const std::string& resolvedPath)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(resolvedPath, error);
    if (error || !std::filesystem::exists(status) || !std::filesystem::is_regular_file(status))
        throw cae::FileFormatError("VTK metadata cache cannot stat regular file: " + resolvedPath);

    FileSignature signature;
    signature.resolvedPath = resolvedPath;
    signature.fileSize = std::filesystem::file_size(resolvedPath);
    signature.lastWriteTime = std::filesystem::last_write_time(resolvedPath);
    signature.quickContentHash = ComputeQuickContentHash(resolvedPath, signature.fileSize);
    return signature;
}

template <typename EntryMap>
void EvictIfNeeded(EntryMap* entries, const std::string& protectedKey)
{
    while (entries->size() > MaxCachedSpecs)
    {
        auto victim =
            std::find_if(entries->begin(), entries->end(), [&](const auto& item) { return item.first != protectedKey; });
        if (victim == entries->end())
            victim = entries->begin();

        TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache evict path='%s'\n", victim->first.c_str());
        entries->erase(victim);
    }
}

} // namespace

std::shared_ptr<const DatasetSpec> DatasetSpecCache::GetOrParse(const std::string& resolvedPath,
                                                                const ReadOptions& options,
                                                                const Parser& parser)
{
    std::shared_ptr<InFlight> flight;
    FileSignature signature;

    while (true)
    {
        signature = ComputeFileSignature(resolvedPath);
        std::unique_lock lock(_mutex);
        const auto entry = _entries.find(resolvedPath);
        if (entry != _entries.end())
        {
            if (SignaturesMatch(entry->second.signature, signature))
            {
                TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache hit path='%s'\n", resolvedPath.c_str());
                return entry->second.spec;
            }

            TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache invalidate path='%s'\n", resolvedPath.c_str());
            _entries.erase(entry);
        }

        const auto inFlight = _inFlight.find(resolvedPath);
        if (inFlight != _inFlight.end())
        {
            std::shared_ptr<InFlight> waitFlight = inFlight->second;
            TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache wait path='%s'\n", resolvedPath.c_str());
            waitFlight->condition.wait(lock, [waitFlight]() { return waitFlight->done; });

            if (SignaturesMatch(waitFlight->signature, signature))
            {
                if (waitFlight->exception)
                    std::rethrow_exception(waitFlight->exception);
                if (waitFlight->spec)
                    return waitFlight->spec;
            }
            continue;
        }

        TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache miss path='%s'\n", resolvedPath.c_str());
        flight = std::make_shared<InFlight>();
        flight->signature = signature;
        _inFlight[resolvedPath] = flight;
        break;
    }

    try
    {
        std::shared_ptr<const DatasetSpec> parsed = std::make_shared<DatasetSpec>(parser.Parse(resolvedPath, options));

        {
            std::scoped_lock lock(_mutex);
            _entries[resolvedPath] = Entry{ signature, parsed };
            EvictIfNeeded(&_entries, resolvedPath);

            flight->spec = parsed;
            flight->done = true;
            _inFlight.erase(resolvedPath);
        }
        flight->condition.notify_all();
        return parsed;
    }
    catch (...)
    {
        {
            std::scoped_lock lock(_mutex);
            flight->exception = std::current_exception();
            flight->done = true;
            _inFlight.erase(resolvedPath);
        }
        flight->condition.notify_all();

        TF_DEBUG(CAE_VTK_FILEFORMAT).Msg("[VTK] metadata cache parse failure path='%s'\n", resolvedPath.c_str());
        throw;
    }
}

void DatasetSpecCache::Clear()
{
    std::scoped_lock lock(_mutex);
    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] metadata cache clear entries=%zu inFlight=%zu\n", _entries.size(), _inFlight.size());
    _entries.clear();
}

} // namespace cae::vtk
