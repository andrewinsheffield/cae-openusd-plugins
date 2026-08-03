// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* message, const char* path)
{
    fprintf(stderr, "%s: %s\n", message, path ? path : "");
    exit(1);
}

static hid_t create_file(const char* path)
{
    hid_t file = H5Fcreate(path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0)
        fail("Failed to create HDF5 file", path);
    return file;
}

static hid_t create_group(hid_t loc, const char* path)
{
    hid_t lcpl = H5Pcreate(H5P_LINK_CREATE);
    if (lcpl < 0)
        fail("Failed to create HDF5 link property list", path);
    if (H5Pset_create_intermediate_group(lcpl, 1) < 0)
        fail("Failed to enable intermediate HDF5 groups", path);

    hid_t group = H5Gcreate2(loc, path, lcpl, H5P_DEFAULT, H5P_DEFAULT);
    H5Pclose(lcpl);
    if (group < 0)
        fail("Failed to create HDF5 group", path);
    return group;
}

static void write_string_attr(hid_t loc, const char* name, const char* value)
{
    hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, strlen(value) + 1);
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(loc, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    if (attr < 0 || H5Awrite(attr, type, value) < 0)
        fail("Failed to write string attribute", name);
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

static void write_int_attr(hid_t loc, const char* name, long long value)
{
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(loc, name, H5T_NATIVE_LLONG, space, H5P_DEFAULT, H5P_DEFAULT);
    if (attr < 0 || H5Awrite(attr, H5T_NATIVE_LLONG, &value) < 0)
        fail("Failed to write integer attribute", name);
    H5Aclose(attr);
    H5Sclose(space);
}

static void write_double_attr(hid_t loc, const char* name, double value)
{
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(loc, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT);
    if (attr < 0 || H5Awrite(attr, H5T_NATIVE_DOUBLE, &value) < 0)
        fail("Failed to write double attribute", name);
    H5Aclose(attr);
    H5Sclose(space);
}

static void write_double_dataset_2d(hid_t loc, const char* path, hsize_t rows, hsize_t cols, const double* values)
{
    const hsize_t dims[2] = { rows, cols };
    hid_t space = H5Screate_simple(2, dims, NULL);
    hid_t dataset = H5Dcreate2(loc, path, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset < 0 || H5Dwrite(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        fail("Failed to write double dataset", path);
    H5Dclose(dataset);
    H5Sclose(space);
}

static void write_double_dataset_1d(hid_t loc, const char* path, hsize_t count, const double* values)
{
    const hsize_t dims[1] = { count };
    hid_t space = H5Screate_simple(1, dims, NULL);
    hid_t dataset = H5Dcreate2(loc, path, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset < 0 || H5Dwrite(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        fail("Failed to write double dataset", path);
    H5Dclose(dataset);
    H5Sclose(space);
}

static void write_int_dataset_1d(hid_t loc, const char* path, hsize_t count, const long long* values)
{
    const hsize_t dims[1] = { count };
    hid_t space = H5Screate_simple(1, dims, NULL);
    hid_t dataset = H5Dcreate2(loc, path, H5T_NATIVE_LLONG, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset < 0 || H5Dwrite(dataset, H5T_NATIVE_LLONG, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        fail("Failed to write integer dataset", path);
    H5Dclose(dataset);
    H5Sclose(space);
}

static void write_int_dataset_2d(hid_t loc, const char* path, hsize_t rows, hsize_t cols, const long long* values)
{
    const hsize_t dims[2] = { rows, cols };
    hid_t space = H5Screate_simple(2, dims, NULL);
    hid_t dataset = H5Dcreate2(loc, path, H5T_NATIVE_LLONG, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset < 0 || H5Dwrite(dataset, H5T_NATIVE_LLONG, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        fail("Failed to write integer dataset", path);
    H5Dclose(dataset);
    H5Sclose(space);
}

static void write_spheres_dataset(hid_t loc, const char* path)
{
    typedef struct SphereRecord
    {
        char name[32];
        float pos[3];
        float physicalRadius;
        float contactRadius;
    } SphereRecord;

    const SphereRecord values[2] = {
        { "Left", { -0.5f, 0.0f, 0.0f }, 0.25f, 0.3f },
        { "Right", { 0.5f, 0.0f, 0.0f }, 0.25f, 0.3f },
    };

    hid_t string_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(string_type, sizeof(values[0].name));

    const hsize_t pos_dims[1] = { 3 };
    hid_t pos_type = H5Tarray_create2(H5T_NATIVE_FLOAT, 1, pos_dims);
    hid_t compound_type = H5Tcreate(H5T_COMPOUND, sizeof(SphereRecord));
    H5Tinsert(compound_type, "name", HOFFSET(SphereRecord, name), string_type);
    H5Tinsert(compound_type, "pos", HOFFSET(SphereRecord, pos), pos_type);
    H5Tinsert(compound_type, "physicalRadius", HOFFSET(SphereRecord, physicalRadius), H5T_NATIVE_FLOAT);
    H5Tinsert(compound_type, "contactRadius", HOFFSET(SphereRecord, contactRadius), H5T_NATIVE_FLOAT);

    const hsize_t dims[1] = { 2 };
    hid_t space = H5Screate_simple(1, dims, NULL);
    hid_t dataset = H5Dcreate2(loc, path, compound_type, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (dataset < 0 || H5Dwrite(dataset, compound_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0)
        fail("Failed to write spheres dataset", path);

    H5Dclose(dataset);
    H5Sclose(space);
    H5Tclose(compound_type);
    H5Tclose(pos_type);
    H5Tclose(string_type);
}

static void write_creator_data(hid_t file)
{
    hid_t particle_type = create_group(file, "/CreatorData/0/ParticleTypes/type0");
    hid_t sphere_type = create_group(file, "/CreatorData/0/ParticleTypes/type1");
    hid_t geometry_group = create_group(file, "/CreatorData/0/GeometryGroups/wall0");

    const double particle_coords[] = { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0 };
    const long long particle_triangles[] = { 0, 1, 2 };

    const double wall_coords[] = { 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 2.0, 0.0 };
    const long long wall_triangles[] = { 0, 1, 2 };

    write_string_attr(particle_type, "name", "Pebble");
    write_double_dataset_2d(file, "/CreatorData/0/ParticleTypes/type0/coords", 3, 3, particle_coords);
    write_int_dataset_2d(file, "/CreatorData/0/ParticleTypes/type0/triangle nodes", 1, 3, particle_triangles);

    write_string_attr(sphere_type, "name", "Dumbbell");
    write_spheres_dataset(file, "/CreatorData/0/ParticleTypes/type1/spheres");

    write_string_attr(geometry_group, "name", "Drum");
    write_double_dataset_2d(file, "/CreatorData/0/GeometryGroups/wall0/coords", 3, 3, wall_coords);
    write_int_dataset_2d(file, "/CreatorData/0/GeometryGroups/wall0/triangle nodes", 1, 3, wall_triangles);

    H5Gclose(particle_type);
    H5Gclose(sphere_type);
    H5Gclose(geometry_group);
}

static void write_time_step(hid_t file,
                            const char* step_name,
                            double time_value,
                            const double* positions0,
                            const long long* ids0,
                            const double* velocities0,
                            const double* positions1,
                            const long long* ids1,
                            const double* velocities1,
                            const double* transform)
{
    char path[512];
    snprintf(path, sizeof(path), "/TimestepData/%s", step_name);
    hid_t step = create_group(file, path);
    write_double_attr(step, "time", time_value);

    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type0", step_name);
    create_group(file, path);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type0/position", step_name);
    write_double_dataset_2d(file, path, 2, 3, positions0);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type0/ids", step_name);
    write_int_dataset_1d(file, path, 2, ids0);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type0/velocity", step_name);
    write_double_dataset_2d(file, path, 2, 3, velocities0);

    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type1", step_name);
    create_group(file, path);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type1/position", step_name);
    write_double_dataset_2d(file, path, 1, 3, positions1);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type1/ids", step_name);
    write_int_dataset_1d(file, path, 1, ids1);
    snprintf(path, sizeof(path), "/TimestepData/%s/ParticleTypes/type1/velocity", step_name);
    write_double_dataset_2d(file, path, 1, 3, velocities1);

    snprintf(path, sizeof(path), "/TimestepData/%s/GeometryGroups/wall0/Kinematics/0", step_name);
    create_group(file, path);
    snprintf(path, sizeof(path), "/TimestepData/%s/GeometryGroups/wall0/Kinematics/0/global transform", step_name);
    write_double_dataset_1d(file, path, 16, transform);

    H5Gclose(step);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 1;
    }

    char deck_path[1024];
    char sample0_path[1024];
    char sample1_path[1024];

    snprintf(deck_path, sizeof(deck_path), "%s/minimal.dem", argv[1]);
    snprintf(sample0_path, sizeof(sample0_path), "%s/minimal_data/0.h5", argv[1]);
    snprintf(sample1_path, sizeof(sample1_path), "%s/minimal_data/1.h5", argv[1]);

    hid_t deck = create_file(deck_path);
    write_int_attr(deck, "num timesteps", 2);
    H5Fclose(deck);

    hid_t sample0 = create_file(sample0_path);
    write_creator_data(sample0);
    {
        const double positions[] = { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        const long long ids[] = { 10, 20 };
        const double velocities[] = { 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
        const double positions1[] = { 5.0, 0.0, 0.0 };
        const long long ids1[] = { 30 };
        const double velocities1[] = { 1.0, 0.0, 0.0 };
        const double transform[] = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
        write_time_step(sample0, "step0", 0.0, positions, ids, velocities, positions1, ids1, velocities1, transform);
    }
    H5Fclose(sample0);

    hid_t sample1 = create_file(sample1_path);
    {
        const double positions[] = { 0.0, 1.0, 0.0, 2.0, 1.0, 0.0 };
        const long long ids[] = { 10, 20 };
        const double velocities[] = { 0.0, 2.0, 0.0, 1.0, 0.0, 0.0 };
        const double positions1[] = { 6.0, 0.0, 0.0 };
        const long long ids1[] = { 30 };
        const double velocities1[] = { 2.0, 0.0, 0.0 };
        const double transform[] = { 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, 3.0, 0.0, 0.0, 0.0, 1.0 };
        write_time_step(sample1, "step1", 0.5, positions, ids, velocities, positions1, ids1, velocities1, transform);
    }
    H5Fclose(sample1);

    return 0;
}
