#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace plcopen::core::rt
{

template <typename T, std::size_t Capacity> class SpscQueue
{
public:
    static_assert(Capacity > 0, "SpscQueue capacity must be positive");

    bool push(const T &value)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if(next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        data_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T &value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        value = data_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    bool full() const
    {
        const std::size_t next = increment(head_.load(std::memory_order_acquire));
        return next == tail_.load(std::memory_order_acquire);
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

private:
    static constexpr std::size_t StorageSize = Capacity + 1;

    // Producer-written head and consumer-written tail each get a full cache
    // line so cross-core traffic stays on the payload, not on false sharing
    // between the two indices (same line size the IPC TransportHeader uses).
    static constexpr std::size_t CacheLineSize = 64;

    static constexpr std::size_t increment(std::size_t value)
    {
        return (value + 1) % StorageSize;
    }

    std::array<T, StorageSize> data_{};
    alignas(CacheLineSize) std::atomic<std::size_t> head_{0};
    alignas(CacheLineSize) std::atomic<std::size_t> tail_{0};
};

} // namespace plcopen::core::rt
