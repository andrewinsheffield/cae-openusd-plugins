// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file UninitializedVtArray.h
///
/// Header-only helpers for creating `VtArray` instances backed by
/// caller-filled storage. These arrays are foreign-wrapped so file-format
/// readers can fill large payloads without paying `VtArray(size_t)`
/// value-initialization cost first.

#include "DisablePXRWarnings.h"

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

template <typename T>
struct UninitializedVtArray
{
    UninitializedVtArray() = default;
    ~UninitializedVtArray() noexcept
    {
        // The array owns the foreign storage; invalidate its non-owning
        // writable view before the array releases that storage.
        data = nullptr;
    }
    UninitializedVtArray(const UninitializedVtArray&) = delete;
    UninitializedVtArray& operator=(const UninitializedVtArray&) = delete;

    UninitializedVtArray(VtArray<T>&& sourceArray, T* sourceData) noexcept
        : array(std::move(sourceArray)), data(sourceData)
    {
    }

    UninitializedVtArray(UninitializedVtArray&& other) noexcept
        : array(std::move(other.array)), data(std::exchange(other.data, nullptr))
    {
    }

    UninitializedVtArray& operator=(UninitializedVtArray&& other) noexcept
    {
        array = std::move(other.array);
        data = std::exchange(other.data, nullptr);
        return *this;
    }

    VtArray<T> array;
    T* data = nullptr;
};

namespace detail
{

inline std::byte* AllocateAlignedBytes(size_t byteCount, size_t alignment)
{
    if (byteCount == 0)
        return nullptr;

#if defined(__cpp_aligned_new) && defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
        return static_cast<std::byte*>(::operator new(byteCount, std::align_val_t(alignment)));
#else
    (void)alignment;
#endif
    return static_cast<std::byte*>(::operator new(byteCount));
}

inline void DeallocateAlignedBytes(std::byte* data, size_t alignment) noexcept
{
    if (!data)
        return;

#if defined(__cpp_aligned_new) && defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        ::operator delete(data, std::align_val_t(alignment));
        return;
    }
#else
    (void)alignment;
#endif
    ::operator delete(data);
}

template <typename T>
class UninitializedArraySource final : public Vt_ArrayForeignDataSource
{
    static_assert(std::is_trivially_default_constructible_v<T>, "Uninitialized VtArray helpers require trivial elements");
    static_assert(std::is_trivially_destructible_v<T>, "Uninitialized VtArray helpers require trivial elements");

public:
    explicit UninitializedArraySource(size_t count)
        : Vt_ArrayForeignDataSource(&UninitializedArraySource::Detached), _count(count)
    {
        if (_count > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::overflow_error("Uninitialized VtArray byte count overflow");

        _data = static_cast<T*>(static_cast<void*>(AllocateAlignedBytes(_count * sizeof(T), alignof(T))));
        std::uninitialized_default_construct_n(_data, _count);
    }

    T* Data() const
    {
        return _data;
    }

    ~UninitializedArraySource()
    {
        DeallocateAlignedBytes(static_cast<std::byte*>(static_cast<void*>(_data)), alignof(T));
    }

    static void Detached(Vt_ArrayForeignDataSource* self)
    {
        std::default_delete<UninitializedArraySource>{}(static_cast<UninitializedArraySource*>(self));
    }

private:
    size_t _count = 0;
    T* _data = nullptr;
};

} // namespace detail

template <typename T>
UninitializedVtArray<T> MakeUninitializedVtArray(size_t count)
{
    if (count == 0)
        return {};

    auto source = std::make_unique<detail::UninitializedArraySource<T>>(count);
    T* data = source->Data();
    VtArray<T> array(source.get(), data, count);
    source.release();
    return { std::move(array), data };
}

PXR_NAMESPACE_CLOSE_SCOPE
