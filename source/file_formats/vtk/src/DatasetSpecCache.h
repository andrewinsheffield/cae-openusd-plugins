// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file DatasetSpecCache.h
///
/// Process-local immutable metadata cache used by the file-format orchestration
/// layer to avoid reparsing between structure and heavy-data passes.

#include "ContainerUtils.h"
#include "DatasetSpec.h"
#include "Parser.h"
#include "Types.h"

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cae::vtk
{

/// Cross-platform file signature used to reject stale parsed specs.
struct FileSignature
{
    std::string resolvedPath;
    uintmax_t fileSize = 0;
    std::filesystem::file_time_type lastWriteTime;
    uint64_t quickContentHash = 0;
};

/// Immutable DatasetSpec cache.
class DatasetSpecCache
{
public:
    /// Return a cached parsed spec or parse and cache a fresh one.
    ///
    /// The cache is keyed by resolved path and guarded by a file signature so
    /// stale entries are rejected when the file changes. Concurrent misses for
    /// the same path use single-flight behavior: one thread parses while others
    /// wait for the same result or exception.
    std::shared_ptr<const DatasetSpec> GetOrParse(const std::string& resolvedPath,
                                                  const ReadOptions& options,
                                                  const Parser& parser);

    void Clear();

private:
    struct Entry
    {
        FileSignature signature;
        std::shared_ptr<const DatasetSpec> spec;
    };

    struct InFlight
    {
        FileSignature signature;
        std::condition_variable condition;
        bool done = false;
        std::shared_ptr<const DatasetSpec> spec;
        std::exception_ptr exception;
    };

    std::mutex _mutex;
    cae::StringUnorderedMap<Entry> _entries;
    cae::StringUnorderedMap<std::shared_ptr<InFlight>> _inFlight;
};

} // namespace cae::vtk
