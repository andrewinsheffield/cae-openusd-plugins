// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <hdf5.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

template <typename T>
void WriteDataset(hid_t file, const char* name, hid_t type, const std::vector<hsize_t>& dims, const std::vector<T>& values)
{
    hid_t space = H5Screate_simple(static_cast<int>(dims.size()), dims.data(), nullptr);
    hid_t dataset = H5Dcreate2(file, name, type, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (space < 0 || dataset < 0 || H5Dwrite(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
        throw std::runtime_error(std::string("Failed to write fixture dataset: ") + name);
    H5Dclose(dataset);
    H5Sclose(space);
}

struct IntegerScalar
{
    char name[512];
    int32_t value;
};

struct RealScalar
{
    char name[512];
    double value;
};

struct SimInfo
{
    int32_t fileFormatVersion;
    char flashVersion[32];
    char fileCreationTime[32];
};

template <size_t N>
void SetString(char (&dest)[N], const std::string& value)
{
    std::fill(std::begin(dest), std::end(dest), '\0');
    value.copy(dest, N - 1);
}

hid_t FixedStringType(size_t width)
{
    hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, width);
    H5Tset_strpad(type, H5T_STR_NULLPAD);
    return type;
}

void WriteScalars(hid_t file, int32_t blocks, int32_t step, double time)
{
    std::vector<IntegerScalar> integers(5);
    const std::array<std::pair<const char*, int32_t>, 5> integerValues = {
        std::make_pair("nxb", 2), std::make_pair("nyb", 2), std::make_pair("nzb", 1),
        std::make_pair("globalnumblocks", blocks), std::make_pair("nstep", step)
    };
    for (size_t i = 0; i < integers.size(); ++i)
    {
        SetString(integers[i].name, integerValues[i].first);
        integers[i].value = integerValues[i].second;
    }
    hid_t nameType = FixedStringType(512);
    hid_t integerType = H5Tcreate(H5T_COMPOUND, sizeof(IntegerScalar));
    H5Tinsert(integerType, "name", HOFFSET(IntegerScalar, name), nameType);
    H5Tinsert(integerType, "value", HOFFSET(IntegerScalar, value), H5T_NATIVE_INT32);
    WriteDataset(file, "integer scalars", integerType, { integers.size() }, integers);
    H5Tclose(integerType);

    RealScalar real{};
    SetString(real.name, "time");
    real.value = time;
    hid_t realType = H5Tcreate(H5T_COMPOUND, sizeof(RealScalar));
    H5Tinsert(realType, "name", HOFFSET(RealScalar, name), nameType);
    H5Tinsert(realType, "value", HOFFSET(RealScalar, value), H5T_NATIVE_DOUBLE);
    WriteDataset(file, "real scalars", realType, { 1 }, std::vector<RealScalar>{ real });
    H5Tclose(realType);
    H5Tclose(nameType);
}

void WriteSimInfo(hid_t file, const std::string& creationTime)
{
    SimInfo info{};
    info.fileFormatVersion = 9;
    SetString(info.flashVersion, "FLASH-X fixture");
    SetString(info.fileCreationTime, creationTime);
    hid_t versionType = FixedStringType(32);
    hid_t creationType = FixedStringType(32);
    hid_t type = H5Tcreate(H5T_COMPOUND, sizeof(SimInfo));
    H5Tinsert(type, "file format version", HOFFSET(SimInfo, fileFormatVersion), H5T_NATIVE_INT32);
    H5Tinsert(type, "flash version", HOFFSET(SimInfo, flashVersion), versionType);
    H5Tinsert(type, "file creation time", HOFFSET(SimInfo, fileCreationTime), creationType);
    WriteDataset(file, "sim info", type, { 1 }, std::vector<SimInfo>{ info });
    H5Tclose(type);
    H5Tclose(creationType);
    H5Tclose(versionType);
}

void WriteSnapshot(const fs::path& path, int32_t blocks, int32_t step, double time, float fieldBase)
{
    hid_t file = H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0)
        throw std::runtime_error("Failed to create fixture HDF5 file");

    WriteScalars(file, blocks, step, time);
    WriteSimInfo(file, step == 10 ? "fixture-time-0" : "fixture-time-1");

    hid_t fieldNameType = FixedStringType(4);
    std::vector<std::array<char, 4>> names(2);
    names[0] = { 'd', 'e', 'n', 's' };
    names[1] = { 'v', 'e', 'l', 'x' };
    WriteDataset(file, "unknown names", fieldNameType, { 2, 1 }, names);
    H5Tclose(fieldNameType);

    std::vector<int32_t> nodeType(static_cast<size_t>(blocks), 1);
    nodeType[0] = 2;
    WriteDataset(file, "node type", H5T_NATIVE_INT32, { static_cast<hsize_t>(blocks) }, nodeType);

    std::vector<int32_t> levels(static_cast<size_t>(blocks), 2);
    levels[0] = 1;
    WriteDataset(file, "refine level", H5T_NATIVE_INT32, { static_cast<hsize_t>(blocks) }, levels);

    std::vector<int32_t> gid(static_cast<size_t>(blocks) * 9, -1);
    for (int32_t block = 0; block < blocks; ++block)
    {
        gid[static_cast<size_t>(block) * 9 + 4] = block == 0 ? -1 : 1;
        if (block == 0)
        {
            for (int child = 0; child < 4 && child + 1 < blocks; ++child)
                gid[5 + static_cast<size_t>(child)] = child + 2;
        }
    }
    WriteDataset(file, "gid", H5T_NATIVE_INT32, { static_cast<hsize_t>(blocks), 9 }, gid);

    std::vector<float> bounds(static_cast<size_t>(blocks) * 6);
    std::vector<float> coordinates(static_cast<size_t>(blocks) * 3);
    for (int32_t block = 0; block < blocks; ++block)
    {
        const float x0 = static_cast<float>(block) * 0.5f;
        bounds[static_cast<size_t>(block) * 6 + 0] = x0;
        bounds[static_cast<size_t>(block) * 6 + 1] = x0 + 0.5f;
        bounds[static_cast<size_t>(block) * 6 + 2] = 0.0f;
        bounds[static_cast<size_t>(block) * 6 + 3] = 1.0f;
        bounds[static_cast<size_t>(block) * 6 + 4] = 0.0f;
        bounds[static_cast<size_t>(block) * 6 + 5] = 0.1f;
        coordinates[static_cast<size_t>(block) * 3 + 0] = x0 + 0.25f;
        coordinates[static_cast<size_t>(block) * 3 + 1] = 0.5f;
        coordinates[static_cast<size_t>(block) * 3 + 2] = 0.05f;
    }
    WriteDataset(file, "bounding box", H5T_NATIVE_FLOAT, { static_cast<hsize_t>(blocks), 3, 2 }, bounds);
    WriteDataset(file, "coordinates", H5T_NATIVE_FLOAT, { static_cast<hsize_t>(blocks), 3 }, coordinates);

    std::vector<int32_t> processor(static_cast<size_t>(blocks));
    for (int32_t block = 0; block < blocks; ++block)
        processor[static_cast<size_t>(block)] = block % 2;
    WriteDataset(file, "processor number", H5T_NATIVE_INT32, { static_cast<hsize_t>(blocks) }, processor);

    std::vector<float> density(static_cast<size_t>(blocks) * 4);
    std::vector<float> velocity(static_cast<size_t>(blocks) * 4);
    for (size_t i = 0; i < density.size(); ++i)
    {
        density[i] = fieldBase + static_cast<float>(i);
        velocity[i] = fieldBase * 10.0f + static_cast<float>(i);
    }
    WriteDataset(file, "dens", H5T_NATIVE_FLOAT, { static_cast<hsize_t>(blocks), 1, 2, 2 }, density);
    WriteDataset(file, "velx", H5T_NATIVE_FLOAT, { static_cast<hsize_t>(blocks), 1, 2, 2 }, velocity);

    const std::vector<int32_t> unsupported = { 42 };
    WriteDataset(file, "fixture unsupported", H5T_NATIVE_INT32, { 1 }, unsupported);
    H5Fclose(file);
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const fs::path output = fs::absolute(argv[1]);
    fs::create_directories(output);
    WriteSnapshot(output / "hdf5_plt_cnt_0000", 3, 10, 0.0, 1.0f);
    WriteSnapshot(output / "hdf5_plt_cnt_0001", 4, 20, 0.5, 101.0f);

    std::ofstream single(output / "minimal.flash");
    single << "{\n  \"format\": \"flash-paramesh-hdf5\",\n  \"version\": 1,\n"
              "  \"file\": \"hdf5_plt_cnt_0000\"\n}\n";
    std::ofstream series(output / "series.flash");
    series << "{\n  \"format\": \"flash-paramesh-hdf5\",\n  \"version\": 1,\n"
              "  \"pattern\": \"hdf5_plt_cnt_*\"\n}\n";
    return 0;
}
