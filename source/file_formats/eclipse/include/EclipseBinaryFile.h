// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Endianness used by Eclipse unformatted binary files.
enum class CaeEclipseEndian
{
    Big,
    Little
};

/// Byte range for one Fortran-record data chunk belonging to an Eclipse item.
///
/// Eclipse item payloads may be split across multiple Fortran records.  The
/// index stores byte ranges rather than eagerly reading payloads so file format
/// plugins can expose large arrays through CaeFileFormatData.
struct CaeEclipseDataChunk
{
    uint64_t offset = 0;
    uint64_t byteCount = 0;
};

/// Indexed Eclipse item record.
///
/// The common item header is:
///
/// - 8 byte keyword
/// - 32-bit item count
/// - 4 byte type tag, typically INTE, LOGI, REAL, DOUB, CHAR, or marker-only
///   tags such as MESS with count 0
///
/// This struct intentionally carries only record-level storage metadata.  File
/// format readers are responsible for source-specific interpretation.
struct CaeEclipseRecord
{
    std::string keyword;
    std::string type;
    size_t count = 0;
    std::string filePath;
    CaeEclipseEndian endian = CaeEclipseEndian::Big;
    std::vector<CaeEclipseDataChunk> chunks;
};

/// Full or partial record index for one Eclipse binary file.
struct CaeEclipseFileIndex
{
    std::string filePath;
    CaeEclipseEndian endian = CaeEclipseEndian::Big;
    std::vector<CaeEclipseRecord> records;
};

std::string CaeEclipseTrimRight(std::string value);
std::string CaeEclipseToUpper(std::string value);
std::string CaeEclipseToLower(std::string value);

std::optional<CaeEclipseEndian> CaeDetectEclipseEndian(const std::string& filePath);
bool CaeReadFirstEclipseRecordHeader(const std::string& filePath, std::string* keyword, std::string* type);

/// Build a lazy-loadable record index.
///
/// If stopAfterKeyword is non-empty, indexing stops after that keyword is
/// stored in the index.  EGRID uses this to ignore result/NNC records after
/// ENDGRID, while INIT/UNRST index the whole file.
CaeEclipseFileIndex CaeIndexEclipseBinaryFile(const std::string& filePath,
                                              const std::string& stopAfterKeyword = std::string());

const CaeEclipseRecord* CaeFindFirstEclipseRecord(const CaeEclipseFileIndex& index,
                                                  std::string_view keyword,
                                                  std::string_view stopBeforeKeyword = {});

bool CaeEclipseIsIntegerRecord(const CaeEclipseRecord& record);
bool CaeEclipseIsFloatingPointRecord(const CaeEclipseRecord& record);
bool CaeEclipseIsNumericRecord(const CaeEclipseRecord& record);

std::vector<int> CaeLoadEclipseIntValues(const CaeEclipseRecord& record);
VtIntArray CaeLoadEclipseIntArray(const CaeEclipseRecord& record);

std::vector<double> CaeLoadEclipseDoubleValues(const CaeEclipseRecord& record);
VtDoubleArray CaeLoadEclipseDoubleArray(const CaeEclipseRecord& record);

std::vector<std::string> CaeLoadEclipseCharValues(const CaeEclipseRecord& record);

PXR_NAMESPACE_CLOSE_SCOPE
