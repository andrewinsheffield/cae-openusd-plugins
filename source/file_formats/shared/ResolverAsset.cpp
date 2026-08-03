// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ResolverAsset.h"

#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAssetDebugCodes.h"
CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layer.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <Windows.h>
#    include <io.h>
#elif defined(__APPLE__)
#    include <fcntl.h>
#    include <limits.h>
#    include <unistd.h>
#else
#    include <limits.h>
#    include <unistd.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(CAE_RESOLVER_ASSET, "OpenUSD resolver asset diagnostics");
}

namespace
{

namespace fs = std::filesystem;

bool IsRegularFile(const std::string& path)
{
    std::error_code error;
    return !path.empty() && fs::is_regular_file(fs::path(path), error);
}

std::string GetFileBackingPath(FILE* file)
{
    if (!file)
        return {};

#if defined(_WIN32)
    const intptr_t osHandle = _get_osfhandle(_fileno(file));
    if (osHandle == -1)
        return {};

    const HANDLE handle = reinterpret_cast<HANDLE>(osHandle);
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (required == 0)
        return {};

    std::wstring path(required, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, path.data(), required, FILE_NAME_NORMALIZED);
    if (written == 0 || written >= required)
        return {};
    path.resize(written);
    if (path.rfind(L"\\\\?\\", 0) == 0)
        path.erase(0, 4);
    return fs::path(path).u8string();
#elif defined(__APPLE__)
    std::array<char, PATH_MAX> path{};
    if (fcntl(fileno(file), F_GETPATH, path.data()) != 0)
        return {};
    return path.data();
#else
    const std::string descriptorPath = "/proc/self/fd/" + std::to_string(fileno(file));
    std::array<char, PATH_MAX + 1> path{};
    const ssize_t count = readlink(descriptorPath.c_str(), path.data(), PATH_MAX);
    if (count <= 0)
        return {};
    path[static_cast<size_t>(count)] = '\0';
    return path.data();
#endif
}

std::pair<std::string, std::string> MaterializeAsset(const std::string& identifier, const ArAsset& asset)
{
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] materializing identifier='%s' size=%zu\n", identifier.c_str(), asset.GetSize());

    const std::string tempDirectory = ArchMakeTmpSubdir(ArchGetTmpDir(), "cae-openusd-asset");
    if (tempDirectory.empty())
        throw cae::FileFormatError("Failed to create a temporary directory for resolver asset '" + identifier + "'.");

    std::string extension = TfGetExtension(identifier);
    if (!extension.empty())
        extension.insert(extension.begin(), '.');
    const fs::path localPath = fs::path(tempDirectory) / ("asset" + extension);

    std::ofstream output(localPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        std::error_code ignored;
        fs::remove_all(tempDirectory, ignored);
        throw cae::FileFormatError("Failed to create a temporary file for resolver asset '" + identifier + "'.");
    }

    constexpr size_t chunkSize = 8u * 1024u * 1024u;
    std::vector<char> buffer(std::min(chunkSize, asset.GetSize()));
    size_t offset = 0;
    while (offset < asset.GetSize())
    {
        const size_t requested = std::min(buffer.size(), asset.GetSize() - offset);
        const size_t count = asset.Read(buffer.data(), requested, offset);
        if (count != requested)
        {
            output.close();
            std::error_code ignored;
            fs::remove_all(tempDirectory, ignored);
            throw cae::FileFormatError("Failed while materializing resolver asset '" + identifier + "' at byte " +
                                       std::to_string(offset) + ".");
        }
        output.write(buffer.data(), static_cast<std::streamsize>(count));
        if (!output)
        {
            output.close();
            std::error_code ignored;
            fs::remove_all(tempDirectory, ignored);
            throw cae::FileFormatError("Failed while writing resolver asset '" + identifier + "' to temporary storage.");
        }
        offset += count;
    }
    output.close();
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] materialized identifier='%s' localPath='%s' size=%zu\n", identifier.c_str(),
             localPath.string().c_str(), asset.GetSize());
    return { localPath.string(), tempDirectory };
}

bool IsFilesystemIdentifier(const std::string& identifier)
{
    const size_t scheme = identifier.find("://");
    return scheme == std::string::npos || TfStringStartsWith(identifier, "file://");
}

} // namespace

struct CaeResolverAssetFactory
{
    static CaeResolverAssetPtr Create(std::string identifier,
                                      ArResolvedPath resolvedPath,
                                      std::shared_ptr<ArAsset> asset,
                                      std::string localPath,
                                      std::string temporaryDirectory)
    {
        return CaeResolverAssetPtr(new CaeResolverAsset(std::move(identifier), std::move(resolvedPath), std::move(asset),
                                                        std::move(localPath), std::move(temporaryDirectory)));
    }
};

CaeResolverAsset::CaeResolverAsset(std::string identifier,
                                   ArResolvedPath resolvedPath,
                                   std::shared_ptr<ArAsset> asset,
                                   std::string localPath,
                                   std::string temporaryDirectory)
    : _identifier(std::move(identifier)),
      _resolvedPath(std::move(resolvedPath)),
      _asset(std::move(asset)),
      _localPath(std::move(localPath)),
      _temporaryDirectory(std::move(temporaryDirectory))
{
}

CaeResolverAsset::~CaeResolverAsset()
{
    if (!_temporaryDirectory.empty())
    {
        std::error_code error;
        fs::remove_all(_temporaryDirectory, error);
        if (error)
        {
            TF_DEBUG(CAE_RESOLVER_ASSET)
                .Msg("[ResolverAsset] release identifier='%s' temporaryDirectory='%s' removed=false error='%s'\n",
                     _identifier.c_str(), _temporaryDirectory.c_str(), error.message().c_str());
        }
        else
        {
            TF_DEBUG(CAE_RESOLVER_ASSET)
                .Msg("[ResolverAsset] release identifier='%s' temporaryDirectory='%s' removed=true\n",
                     _identifier.c_str(), _temporaryDirectory.c_str());
        }
    }
    else
    {
        TF_DEBUG(CAE_RESOLVER_ASSET)
            .Msg("[ResolverAsset] release identifier='%s' localPath='%s'\n", _identifier.c_str(), _localPath.c_str());
    }
}

CaeResolverAssetPtr CaeOpenResolverAsset(const std::string& identifier, const ArResolvedPath& resolvedPath)
{
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] open identifier='%s' resolvedPath='%s'\n", identifier.c_str(),
             resolvedPath.GetPathString().c_str());

    if (resolvedPath.empty())
        throw cae::FileFormatError("Asset resolver could not resolve '" + identifier + "'.");

    const std::string resolvedString = resolvedPath.GetPathString();
    if (IsRegularFile(resolvedString))
    {
        TF_DEBUG(CAE_RESOLVER_ASSET)
            .Msg("[ResolverAsset] strategy=native-resolved-file identifier='%s' localPath='%s'\n", identifier.c_str(),
                 resolvedString.c_str());
        return CaeResolverAssetFactory::Create(identifier, resolvedPath, nullptr, resolvedString, std::string{});
    }

    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] resolved path is not a native file; opening ArAsset identifier='%s'\n", identifier.c_str());
    std::shared_ptr<ArAsset> asset = ArGetResolver().OpenAsset(resolvedPath);
    if (!asset)
        throw cae::FileFormatError("Asset resolver could not open '" + identifier + "' (" + resolvedString + ").");

    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] ArAsset opened identifier='%s' size=%zu\n", identifier.c_str(), asset->GetSize());

    const auto [file, offset] = asset->GetFileUnsafe();
    if (file && offset == 0)
    {
        const std::string backingPath = GetFileBackingPath(file);
        if (IsRegularFile(backingPath))
        {
            TF_DEBUG(CAE_RESOLVER_ASSET)
                .Msg("[ResolverAsset] strategy=resolver-native-backing-file identifier='%s' localPath='%s'\n",
                     identifier.c_str(), backingPath.c_str());
            return CaeResolverAssetFactory::Create(
                identifier, resolvedPath, std::move(asset), backingPath, std::string{});
        }
        TF_DEBUG(CAE_RESOLVER_ASSET)
            .Msg("[ResolverAsset] ArAsset native handle has no reusable filesystem path identifier='%s'\n",
                 identifier.c_str());
    }
    else
    {
        TF_DEBUG(CAE_RESOLVER_ASSET)
            .Msg("[ResolverAsset] ArAsset native backing unavailable identifier='%s' hasFile=%s offset=%lld\n",
                 identifier.c_str(), file ? "true" : "false", static_cast<long long>(offset));
    }

    auto [localPath, temporaryDirectory] = MaterializeAsset(identifier, *asset);
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] strategy=materialized-copy identifier='%s' localPath='%s'\n", identifier.c_str(),
             localPath.c_str());
    return CaeResolverAssetFactory::Create(
        identifier, resolvedPath, std::move(asset), std::move(localPath), std::move(temporaryDirectory));
}

CaeResolverAssetPtr CaeResolveAsset(const std::string& identifier)
{
    ArResolver& resolver = ArGetResolver();
    const ArResolvedPath resolvedPath = resolver.Resolve(identifier);
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] resolve identifier='%s' resolvedPath='%s'\n", identifier.c_str(),
             resolvedPath.GetPathString().c_str());
    return CaeOpenResolverAsset(identifier, resolvedPath);
}

CaeResolverAssetPtr CaeResolveSiblingAsset(const std::string& anchorIdentifier, const std::string& referencedPath)
{
    if (referencedPath.empty())
        throw cae::FileFormatError("Cannot resolve an empty asset reference relative to '" + anchorIdentifier + "'.");

    ArResolver& resolver = ArGetResolver();
    const ArResolvedPath anchorResolved = resolver.Resolve(anchorIdentifier);
    if (anchorResolved.empty())
        throw cae::FileFormatError("Asset resolver could not resolve anchor '" + anchorIdentifier + "'.");

    const std::string identifier = resolver.CreateIdentifier(referencedPath, anchorResolved);
    const ArResolvedPath resolvedPath = resolver.Resolve(identifier);
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg(
            "[ResolverAsset] resolve-sibling anchorIdentifier='%s' anchorResolvedPath='%s' reference='%s' "
            "childIdentifier='%s' childResolvedPath='%s'\n",
            anchorIdentifier.c_str(), anchorResolved.GetPathString().c_str(), referencedPath.c_str(),
            identifier.c_str(), resolvedPath.GetPathString().c_str());
    return CaeOpenResolverAsset(identifier, resolvedPath);
}

std::string CaeGetLayerAssetIdentifier(const SdfLayer& layer)
{
    std::string identifier;
    SdfLayer::FileFormatArguments unusedArgs;
    SdfLayer::SplitIdentifier(layer.GetIdentifier(), &identifier, &unusedArgs);
    return identifier;
}

SdfFileFormat::FileFormatArguments CaePrepareResolverArguments(const std::string& identifier,
                                                               const SdfFileFormat::FileFormatArguments& args)
{
    SdfFileFormat::FileFormatArguments prepared = args;
    if (prepared.find(CaeMountPathArgName()) == prepared.end())
        prepared[CaeMountPathArgName()] = CaeResolveRootPrimPath(identifier, args).GetString();
    return prepared;
}

bool CaeCanScanAdjacentFiles(const std::string& identifier, const ArResolvedPath& resolvedPath)
{
    const bool filesystemIdentifier = IsFilesystemIdentifier(identifier);
    const bool nativeResolvedFile = IsRegularFile(resolvedPath.GetPathString());
    const bool supported = filesystemIdentifier && nativeResolvedFile;
    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg(
            "[ResolverAsset] adjacent-scan identifier='%s' resolvedPath='%s' filesystemIdentifier=%s "
            "nativeResolvedFile=%s supported=%s\n",
            identifier.c_str(), resolvedPath.GetPathString().c_str(), filesystemIdentifier ? "true" : "false",
            nativeResolvedFile ? "true" : "false", supported ? "true" : "false");
    return supported;
}

void CaeRequireAdjacentFileScanning(const char* formatName,
                                    const std::string& identifier,
                                    const ArResolvedPath& resolvedPath)
{
    if (CaeCanScanAdjacentFiles(identifier, resolvedPath))
        return;

    TF_DEBUG(CAE_RESOLVER_ASSET)
        .Msg("[ResolverAsset] rejecting adjacent-directory scan format='%s' identifier='%s' resolvedPath='%s'\n",
             formatName ? formatName : "This file format", identifier.c_str(), resolvedPath.GetPathString().c_str());
    throw cae::FileFormatError(
        std::string(formatName ? formatName : "This file format") + " cannot load resolver-backed asset '" + identifier +
        "' because the dataset requires adjacent-directory scanning. OpenUSD's Asset Resolver API resolves named "
        "assets but does not provide portable directory enumeration. Copy the complete dataset to a local filesystem "
        "or use an explicit manifest-based representation.");
}

PXR_NAMESPACE_CLOSE_SCOPE
