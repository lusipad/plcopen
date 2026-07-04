#pragma once

#include <array>
#include <cstddef>

#include "rt/error.h"

namespace plcopen::core::rt
{

template <typename T, std::size_t Capacity> class StaticVector
{
public:
    static_assert(Capacity > 0, "StaticVector capacity must be positive");

    constexpr std::size_t size() const
    {
        return size_;
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    constexpr bool empty() const
    {
        return size_ == 0;
    }

    constexpr bool full() const
    {
        return size_ == Capacity;
    }

    constexpr ErrorCode push_back(const T &value)
    {
        if(full()) {
            return ErrorCode::capacity_exceeded;
        }
        data_[size_] = value;
        ++size_;
        return ErrorCode::ok;
    }

    constexpr ErrorCode pop_back()
    {
        if(empty()) {
            return ErrorCode::out_of_range;
        }
        --size_;
        return ErrorCode::ok;
    }

    constexpr void clear()
    {
        size_ = 0;
    }

    constexpr T &operator[](std::size_t index)
    {
        return data_[index];
    }

    constexpr const T &operator[](std::size_t index) const
    {
        return data_[index];
    }

    constexpr const T *data() const
    {
        return data_.data();
    }

    constexpr T *data()
    {
        return data_.data();
    }

private:
    std::array<T, Capacity> data_{};
    std::size_t size_ = 0;
};

} // namespace plcopen::core::rt
