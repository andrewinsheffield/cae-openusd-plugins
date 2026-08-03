// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ArrayEvaluator.h"

#include "DebugCodes.h"
#include "DisablePXRWarnings.h"
#include "FileFormatError.h"
#include "UninitializedVtArray.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

PXR_NAMESPACE_USING_DIRECTIVE

namespace cae::vtk
{

namespace
{

std::string DebugContext(const ArraySpec& array)
{
    if (!array.debugContext.empty())
        return array.debugContext;
    if (!array.sourceName.empty())
        return array.sourceName;
    return array.arrayName.GetString();
}

PXR_NS::VtValue ReadDirectScalarPayload(const ArraySpec& array, FileHandle& file)
{
    std::unique_ptr<ScalarPayloadReader> reader = MakeScalarPayloadReader(array.payload);
    return reader->ReadData(file, array.payload.request);
}

template <typename SrcT, typename DstT>
PXR_NS::VtValue ConvertScalarArray(PXR_NS::VtValue value, const ArraySpec& array, const char* dstTypeName)
{
    if (!value.IsHolding<PXR_NS::VtArray<SrcT>>())
        throw cae::FileFormatError("VTK scalar payload type does not match array metadata: " + DebugContext(array));

    const PXR_NS::VtArray<SrcT>& in = value.UncheckedGet<PXR_NS::VtArray<SrcT>>();
    PXR_NS::VtArray<DstT> out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out.data()[i] = static_cast<DstT>(in.cdata()[i]);

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] widen scalar payload array='%s' debug='%s' values=%zu target=%s\n", array.arrayName.GetText(),
             DebugContext(array).c_str(), in.size(), dstTypeName);
    return PXR_NS::VtValue::Take(out);
}

PXR_NS::VtValue NormalizeScalarValueForUsd(PXR_NS::VtValue value, const ArraySpec& array)
{
    switch (array.scalarType)
    {
    case ScalarType::Int8:
        return ConvertScalarArray<int8_t, int>(std::move(value), array, "int[]");
    case ScalarType::Int16:
        return ConvertScalarArray<int16_t, int>(std::move(value), array, "int[]");
    case ScalarType::UInt16:
        return ConvertScalarArray<uint16_t, unsigned int>(std::move(value), array, "uint[]");
    case ScalarType::UInt8:
    case ScalarType::Int32:
    case ScalarType::UInt32:
    case ScalarType::Int64:
    case ScalarType::UInt64:
    case ScalarType::Float32:
    case ScalarType::Float64:
        return value;
    }
    throw cae::FileFormatError("Unsupported VTK scalar array mapping: " + DebugContext(array));
}

template <typename ScalarT, typename VecT, int Components>
class VectorArraySource final : public PXR_NS::Vt_ArrayForeignDataSource
{
public:
    explicit VectorArraySource(PXR_NS::VtArray<ScalarT> flat)
        : PXR_NS::Vt_ArrayForeignDataSource(&VectorArraySource::Detached), _flat(std::move(flat))
    {
    }

    VecT* Data()
    {
        return static_cast<VecT*>(static_cast<void*>(_flat.data()));
    }

    static void Detached(PXR_NS::Vt_ArrayForeignDataSource* self)
    {
        std::default_delete<VectorArraySource>{}(static_cast<VectorArraySource*>(self));
    }

private:
    PXR_NS::VtArray<ScalarT> _flat;
};

template <typename ScalarT, typename VecT, int Components>
PXR_NS::VtValue MakeVectorValue(PXR_NS::VtArray<ScalarT> flat, const ArraySpec& array)
{
    static_assert(std::is_floating_point_v<ScalarT>, "First-pass vector mapping only supports floating arrays");
    static_assert(
        sizeof(VecT) == sizeof(ScalarT) * Components, "Gf vector layout must match contiguous scalar component layout");

    if ((flat.size() % Components) != 0)
        throw cae::FileFormatError("VTK vector scalar payload count is not divisible by component count: " +
                                   DebugContext(array));

    const size_t tupleCount = flat.size() / Components;
    if (tupleCount != array.tupleCount)
        throw cae::FileFormatError("VTK vector tuple count does not match array metadata: " + DebugContext(array));
    if (tupleCount == 0)
        return PXR_NS::VtValue(PXR_NS::VtArray<VecT>());

    auto* storage = static_cast<void*>(flat.data());
    void* aligned = storage;
    if (size_t alignmentSpace = sizeof(VecT); std::align(alignof(VecT), sizeof(VecT), aligned, alignmentSpace) == storage)
    {
        auto source = std::make_unique<VectorArraySource<ScalarT, VecT, Components>>(std::move(flat));
        PXR_NS::VtArray<VecT> vectors(source.get(), source->Data(), tupleCount);
        source.release();
        return PXR_NS::VtValue::Take(vectors);
    }

    TF_DEBUG(CAE_VTK_FILEFORMAT)
        .Msg("[VTK] copy vector payload due to alignment array='%s' debug='%s' address=%p align=%zu tuples=%zu\n",
             array.arrayName.GetText(), DebugContext(array).c_str(), static_cast<const void*>(flat.cdata()),
             alignof(VecT), tupleCount);

    PXR_NS::VtArray<VecT> vectors(tupleCount);
    const ScalarT* data = flat.cdata();
    for (size_t tuple = 0; tuple < tupleCount; ++tuple)
    {
        VecT value;
        for (int component = 0; component < Components; ++component)
            value[component] = data[tuple * Components + component];
        vectors.data()[tuple] = value;
    }
    return PXR_NS::VtValue::Take(vectors);
}

PXR_NS::VtValue ReinterpretFloatingVector(PXR_NS::VtValue value, const ArraySpec& array)
{
    if (array.componentCount == 2)
    {
        if (value.IsHolding<PXR_NS::VtArray<float>>())
            return MakeVectorValue<float, PXR_NS::GfVec2f, 2>(value.UncheckedRemove<PXR_NS::VtArray<float>>(), array);
        if (value.IsHolding<PXR_NS::VtArray<double>>())
            return MakeVectorValue<double, PXR_NS::GfVec2d, 2>(value.UncheckedRemove<PXR_NS::VtArray<double>>(), array);
    }
    else if (array.componentCount == 3)
    {
        if (value.IsHolding<PXR_NS::VtArray<float>>())
            return MakeVectorValue<float, PXR_NS::GfVec3f, 3>(value.UncheckedRemove<PXR_NS::VtArray<float>>(), array);
        if (value.IsHolding<PXR_NS::VtArray<double>>())
            return MakeVectorValue<double, PXR_NS::GfVec3d, 3>(value.UncheckedRemove<PXR_NS::VtArray<double>>(), array);
    }
    else if (array.componentCount == 4)
    {
        if (value.IsHolding<PXR_NS::VtArray<float>>())
            return MakeVectorValue<float, PXR_NS::GfVec4f, 4>(value.UncheckedRemove<PXR_NS::VtArray<float>>(), array);
        if (value.IsHolding<PXR_NS::VtArray<double>>())
            return MakeVectorValue<double, PXR_NS::GfVec4d, 4>(value.UncheckedRemove<PXR_NS::VtArray<double>>(), array);
    }

    throw cae::FileFormatError("Unsupported VTK vector array mapping: " + DebugContext(array));
}

size_t CheckedPayloadByteCount(const ScalarPayloadRequest& request, size_t scalarSize, const ArraySpec& array)
{
    if (request.valueCount > std::numeric_limits<size_t>::max() / scalarSize)
        throw std::overflow_error("VTK direct payload byte count overflow: " + DebugContext(array));
    return request.valueCount * scalarSize;
}

template <typename T>
PXR_NS::VtValue ReadPrependZeroDirectAs(const ArraySpec& array, FileHandle& file)
{
    const ScalarPayloadRequest& request = array.payload.request;
    if (request.valueCount == std::numeric_limits<size_t>::max() || array.tupleCount != request.valueCount + 1)
        throw cae::FileFormatError("VTK prepend-zero tuple count does not match source payload count: " +
                                   DebugContext(array));

    PXR_NS::UninitializedVtArray<T> out = PXR_NS::MakeUninitializedVtArray<T>(array.tupleCount);
    if (!out.data)
        return PXR_NS::VtValue::Take(out.array);
    out.data[0] = T(0);

    if (request.valueCount != 0)
    {
        std::unique_ptr<ScalarPayloadReader> reader = MakeScalarPayloadReader(array.payload);
        reader->ReadDataInto(file, request, static_cast<std::byte*>(static_cast<void*>(out.data + 1)),
                             CheckedPayloadByteCount(request, sizeof(T), array));
    }
    return PXR_NS::VtValue::Take(out.array);
}

PXR_NS::VtValue ReadPrependZeroDirect(const ArraySpec& array, FileHandle& file)
{
    switch (array.scalarType)
    {
    case ScalarType::Int8:
        return ReadPrependZeroDirectAs<int8_t>(array, file);
    case ScalarType::UInt8:
        return ReadPrependZeroDirectAs<unsigned char>(array, file);
    case ScalarType::Int16:
        return ReadPrependZeroDirectAs<int16_t>(array, file);
    case ScalarType::UInt16:
        return ReadPrependZeroDirectAs<uint16_t>(array, file);
    case ScalarType::Int32:
        return ReadPrependZeroDirectAs<int32_t>(array, file);
    case ScalarType::UInt32:
        return ReadPrependZeroDirectAs<uint32_t>(array, file);
    case ScalarType::Int64:
        return ReadPrependZeroDirectAs<int64_t>(array, file);
    case ScalarType::UInt64:
        return ReadPrependZeroDirectAs<uint64_t>(array, file);
    case ScalarType::Float32:
        return ReadPrependZeroDirectAs<float>(array, file);
    case ScalarType::Float64:
        return ReadPrependZeroDirectAs<double>(array, file);
    }
    throw cae::FileFormatError("Unsupported VTK prepend-zero scalar type: " + DebugContext(array));
}

} // namespace

ArrayEvaluator::ArrayEvaluator(FileHandle& file) : _file(file)
{
}

PXR_NS::VtValue ArrayEvaluator::Evaluate(const ArraySpec& array) const
{
    switch (array.evaluationKind)
    {
    case ArrayEvaluationKind::DirectScalarPayload:
        return NormalizeScalarValueForUsd(ReadDirectScalarPayload(array, _file), array);
    case ArrayEvaluationKind::ReinterpretedVector:
        return ReinterpretFloatingVector(ReadDirectScalarPayload(array, _file), array);
    case ArrayEvaluationKind::PrependZero:
        return NormalizeScalarValueForUsd(ReadPrependZeroDirect(array, _file), array);
    }
    throw cae::FileFormatError("Unsupported VTK array evaluation kind: " + DebugContext(array));
}

std::unique_ptr<ScalarPayloadReader> MakeScalarPayloadReader(const ScalarPayloadSpec& payload)
{
    switch (payload.source.storageKind)
    {
    case StorageKind::Ascii:
    case StorageKind::PlainBinary:
        return std::make_unique<DirectScalarPayloadReader>(payload.source);
    case StorageKind::XmlBinary:
    case StorageKind::XmlBase64Binary:
        return std::make_unique<XmlBinaryBlockPayloadReader>(payload.source);
    }
    throw cae::FileFormatError("Unsupported VTK scalar payload reader kind");
}

} // namespace cae::vtk
