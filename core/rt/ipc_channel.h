#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace plcopen::core::rt
{

enum class IpcStatus
{
    ok,
    empty,
    full,
    busy,
    invalid_owner,
};

struct IpcWriteReservation
{
    std::uint64_t sequence = 0;
    std::size_t slot = 0;
    bool active = false;
};

namespace detail
{

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "ipc channels require lock-free atomic uint64 words");

template <typename T> struct AtomicPayloadTraits
{
    static_assert(std::is_trivially_copyable_v<T>, "ipc payloads must be trivially copyable");
    static constexpr std::size_t WordSize = sizeof(std::uint64_t);
    static_assert(sizeof(T) % WordSize == 0,
                  "ipc payload size must be a multiple of uint64_t");

    static constexpr std::size_t WordCount = sizeof(T) / WordSize;
    using WordArray = std::array<std::uint64_t, WordCount>;
    using AtomicWordArray = std::array<std::atomic<std::uint64_t>, WordCount>;
};

template <typename T>
inline void store_payload(typename AtomicPayloadTraits<T>::AtomicWordArray &storage, const T &value)
{
    typename AtomicPayloadTraits<T>::WordArray words{};
    std::memcpy(words.data(), &value, sizeof(T));
    for(std::size_t index = 0; index < words.size(); ++index) {
        storage[index].store(words[index], std::memory_order_relaxed);
    }
}

template <typename T>
inline void load_payload(const typename AtomicPayloadTraits<T>::AtomicWordArray &storage, T &value)
{
    typename AtomicPayloadTraits<T>::WordArray words{};
    for(std::size_t index = 0; index < words.size(); ++index) {
        words[index] = storage[index].load(std::memory_order_relaxed);
    }
    std::memcpy(&value, words.data(), sizeof(T));
}

inline bool claim_owner(std::atomic<std::uint64_t> &owner, std::uint64_t id)
{
    if(id == 0) {
        return false;
    }
    std::uint64_t expected = 0;
    // Claim is a one-shot latch for the mapping lifetime. Even the same token
    // cannot claim twice; a restarted cooperative peer must rebuild the region.
    return owner.compare_exchange_strong(expected, id, std::memory_order_acq_rel,
                                          std::memory_order_acquire);
}

inline bool owner_matches(const std::atomic<std::uint64_t> &owner, std::uint64_t id)
{
    return id != 0 && owner.load(std::memory_order_acquire) == id;
}

} // namespace detail

template <typename T, std::size_t Capacity> class IpcSpscRing
{
public:
    static_assert(Capacity > 0, "IpcSpscRing capacity must be positive");

    void initialize()
    {
        writer_owner_.store(0, std::memory_order_relaxed);
        reader_owner_.store(0, std::memory_order_relaxed);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        for(auto &slot : slots_) {
            for(auto &word : slot.words) {
                word.store(0, std::memory_order_relaxed);
            }
        }
    }

    bool claim_writer(std::uint64_t owner)
    {
        return detail::claim_owner(writer_owner_, owner);
    }

    bool claim_reader(std::uint64_t owner)
    {
        return detail::claim_owner(reader_owner_, owner);
    }

    IpcStatus push(std::uint64_t owner, const T &value)
    {
        if(!detail::owner_matches(writer_owner_, owner)) {
            return IpcStatus::invalid_owner;
        }

        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t next = increment(head);
        if(next == tail_.load(std::memory_order_acquire)) {
            return IpcStatus::full;
        }

        detail::store_payload<T>(slots_[static_cast<std::size_t>(head)].words, value);
        head_.store(next, std::memory_order_release);
        return IpcStatus::ok;
    }

    IpcStatus pop(std::uint64_t owner, T &value)
    {
        if(!detail::owner_matches(reader_owner_, owner)) {
            return IpcStatus::invalid_owner;
        }

        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire)) {
            return IpcStatus::empty;
        }

        detail::load_payload<T>(slots_[static_cast<std::size_t>(tail)].words, value);
        tail_.store(increment(tail), std::memory_order_release);
        return IpcStatus::ok;
    }

private:
    static constexpr std::size_t StorageSize = Capacity + 1;

    struct Slot
    {
        typename detail::AtomicPayloadTraits<T>::AtomicWordArray words{};
    };

    static constexpr std::uint64_t increment(std::uint64_t index)
    {
        return (index + 1u) % static_cast<std::uint64_t>(StorageSize);
    }

    std::array<Slot, StorageSize> slots_{};
    std::atomic<std::uint64_t> writer_owner_{0};
    std::atomic<std::uint64_t> reader_owner_{0};
    std::atomic<std::uint64_t> head_{0};
    std::atomic<std::uint64_t> tail_{0};
};

template <typename T> class IpcDoubleBuffer
{
public:
    void initialize()
    {
        writer_owner_.store(0, std::memory_order_relaxed);
        pending_state_.store(0, std::memory_order_relaxed);
        published_state_.store(0, std::memory_order_relaxed);
        for(auto &slot : slots_) {
            slot.stamp.store(0, std::memory_order_relaxed);
            for(auto &word : slot.words) {
                word.store(0, std::memory_order_relaxed);
            }
        }
    }

    bool claim_writer(std::uint64_t owner)
    {
        return detail::claim_owner(writer_owner_, owner);
    }

    IpcStatus publish(std::uint64_t owner, const T &value)
    {
        IpcWriteReservation reservation{};
        IpcStatus status = begin_publish(owner, reservation);
        if(status != IpcStatus::ok) {
            return status;
        }
        status = write_pending(owner, reservation, value);
        if(status != IpcStatus::ok) {
            pending_state_.store(0, std::memory_order_release);
            reservation.active = false;
            return status;
        }
        return commit_publish(owner, reservation);
    }

    IpcStatus begin_publish(std::uint64_t owner, IpcWriteReservation &reservation)
    {
        if(!detail::owner_matches(writer_owner_, owner)) {
            return IpcStatus::invalid_owner;
        }

        const std::uint64_t state = published_state_.load(std::memory_order_acquire);
        const std::uint64_t sequence = unpack_sequence(state);
        const std::size_t active_slot = unpack_slot(state);
        const std::size_t pending_slot = (sequence == 0) ? 0 : (active_slot ^ 1u);
        const std::uint64_t pending = pack_state(sequence + 1u, pending_slot);

        std::uint64_t expected = 0;
        if(!pending_state_.compare_exchange_strong(expected, pending, std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
            return IpcStatus::busy;
        }

        reservation.sequence = sequence + 1;
        reservation.slot = pending_slot;
        reservation.active = true;
        slots_[pending_slot].stamp.store((reservation.sequence << 1u) | 1u,
                                         std::memory_order_release);
        return IpcStatus::ok;
    }

    IpcStatus write_pending(std::uint64_t owner, const IpcWriteReservation &reservation,
                            const T &value)
    {
        if(!detail::owner_matches(writer_owner_, owner)) {
            return IpcStatus::invalid_owner;
        }
        if(!reservation.active || reservation.slot >= slots_.size()) {
            return IpcStatus::busy;
        }

        const std::uint64_t pending = pending_state_.load(std::memory_order_acquire);
        if(pending == 0 || pending != pack_state(reservation.sequence, reservation.slot)) {
            return IpcStatus::busy;
        }

        detail::store_payload<T>(slots_[reservation.slot].words, value);
        return IpcStatus::ok;
    }

    IpcStatus commit_publish(std::uint64_t owner, IpcWriteReservation &reservation)
    {
        if(!detail::owner_matches(writer_owner_, owner)) {
            return IpcStatus::invalid_owner;
        }
        if(!reservation.active || reservation.slot >= slots_.size()) {
            return IpcStatus::busy;
        }

        const std::uint64_t pending = pack_state(reservation.sequence, reservation.slot);
        std::uint64_t expected = pending;
        if(!pending_state_.compare_exchange_strong(
               expected, CommittingState, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return IpcStatus::busy;
        }

        slots_[reservation.slot].stamp.store(reservation.sequence << 1u, std::memory_order_release);
        published_state_.store(pack_state(reservation.sequence, reservation.slot),
                               std::memory_order_release);
        pending_state_.store(0, std::memory_order_release);
        reservation.active = false;
        return IpcStatus::ok;
    }

    IpcStatus read(T &value, std::uint64_t &sequence, std::size_t max_retries) const
    {
        const std::uint64_t first_state = published_state_.load(std::memory_order_acquire);
        if(unpack_sequence(first_state) == 0) {
            return IpcStatus::empty;
        }
        if(max_retries == 0) {
            return IpcStatus::busy;
        }

        for(std::size_t attempt = 0; attempt < max_retries; ++attempt) {
            const std::uint64_t state = published_state_.load(std::memory_order_acquire);
            const std::uint64_t published_sequence = unpack_sequence(state);
            if(published_sequence == 0) {
                return IpcStatus::empty;
            }

            const std::size_t slot_index = unpack_slot(state);
            const std::uint64_t stable_stamp = published_sequence << 1u;
            const Slot &slot = slots_[slot_index];
            const std::uint64_t begin = slot.stamp.load(std::memory_order_acquire);
            if(begin != stable_stamp) {
                continue;
            }

            detail::load_payload<T>(slot.words, value);

            const std::uint64_t end = slot.stamp.load(std::memory_order_acquire);
            if(begin == end && published_state_.load(std::memory_order_acquire) == state) {
                sequence = published_sequence;
                return IpcStatus::ok;
            }
        }
        return IpcStatus::busy;
    }

private:
    static constexpr std::uint64_t CommittingState = ~std::uint64_t{0};

    struct Slot
    {
        typename detail::AtomicPayloadTraits<T>::AtomicWordArray words{};
        std::atomic<std::uint64_t> stamp{0};
    };

    static constexpr std::uint64_t pack_state(std::uint64_t sequence, std::size_t slot)
    {
        return (sequence << 1u) | static_cast<std::uint64_t>(slot & 1u);
    }

    static constexpr std::uint64_t unpack_sequence(std::uint64_t state)
    {
        return state >> 1u;
    }

    static constexpr std::size_t unpack_slot(std::uint64_t state)
    {
        return static_cast<std::size_t>(state & 1u);
    }

    std::array<Slot, 2> slots_{};
    std::atomic<std::uint64_t> writer_owner_{0};
    std::atomic<std::uint64_t> pending_state_{0};
    std::atomic<std::uint64_t> published_state_{0};
};

} // namespace plcopen::core::rt
