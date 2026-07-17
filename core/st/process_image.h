#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "rt/error.h"
#include "st/bytecode.h"

namespace plcopen::core::st
{

enum class SnapshotKind : std::uint8_t
{
    retain = 0,
    persistent,
};

enum class SnapshotError : std::uint8_t
{
    ok = 0,
    invalid_argument,
    capacity_exceeded,
    malformed,
    version_mismatch,
    fingerprint_mismatch,
};

struct SnapshotReport
{
    std::uint16_t restored = 0;
    std::uint16_t rejected = 0;
    std::uint16_t defaulted = 0;
    bool fingerprint_match = false;
};

class ProcessImage
{
public:
    static constexpr std::uint32_t wildcard_force_owner =
        std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint64_t wildcard_force_release = 0;

    ProcessImage() = default;
    ProcessImage(const ProcessImage &) = delete;
    ProcessImage &operator=(const ProcessImage &) = delete;

    rt::ErrorCode load(const Program &program, unsigned char *buffer,
                       std::size_t buffer_bytes)
    {
        const std::size_t required =
            static_cast<std::size_t>(program.process_image.input_bytes) +
            program.process_image.output_bytes +
            program.process_image.memory_bytes;
        if((required != 0 && buffer == nullptr) || buffer_bytes < required) {
            return required > buffer_bytes ? rt::ErrorCode::capacity_exceeded
                                           : rt::ErrorCode::invalid_argument;
        }
        program_ = &program;
        unsigned char *cursor = buffer;
        input_ = cursor;
        cursor += program.process_image.input_bytes;
        output_ = cursor;
        cursor += program.process_image.output_bytes;
        memory_ = cursor;
        if(required != 0) std::memset(buffer, 0, required);
        output_shadow_.assign(program.process_image.output_bytes, 0);
        memory_shadow_.assign(program.process_image.memory_bytes, 0);
        current_output_dirty_.assign(program.process_image.output_bytes, 0);
        current_memory_dirty_.assign(program.process_image.memory_bytes, 0);
        transaction_output_dirty_.assign(program.process_image.output_bytes, 0);
        transaction_memory_dirty_.assign(program.process_image.memory_bytes, 0);
        snapshot_scratch_.assign(program.process_image.memory_bytes, 0);
        force_count_ = program.process_image.variables.size();
        forces_.reset(force_count_ == 0 ? nullptr : new ForceState[force_count_]);
        prepare_channel(input_channel_, program.process_image.input_bytes);
        prepare_channel(output_channel_, program.process_image.output_bytes);
        prepare_channel(memory_channel_, program.process_image.memory_bytes);
        prepare_restore_channel(program.process_image.variables.size());
        input_version_ = 0;
        output_version_ = 0;
        memory_version_ = 0;
        force_queue_version_.store(0, std::memory_order_relaxed);
        force_queue_lock_.clear(std::memory_order_relaxed);
        transaction_active_ = false;
        for(const LocatedVarInfo &var : program.process_image.variables) {
            if(var.area == ProcessArea::input) continue;
            unsigned char *image = var.area == ProcessArea::output
                                       ? output_
                                       : memory_;
            write_image_value(image, var,
                              program.initial_data.data() + var.var_offset);
        }
        initialize_channel(output_channel_, output_,
                           program.process_image.output_bytes, 0);
        initialize_channel(memory_channel_, memory_,
                           program.process_image.memory_bytes, 0);
        return rt::ErrorCode::ok;
    }

    bool loaded_for(const Program &program) const noexcept
    {
        return program_ == &program;
    }

    bool supports(const Program &program) const noexcept
    {
        return program_ != nullptr &&
               program.process_image.input_bytes <=
                   program_->process_image.input_bytes &&
               program.process_image.output_bytes <=
                   program_->process_image.output_bytes &&
               program.process_image.memory_bytes <=
                   program_->process_image.memory_bytes;
    }

    rt::ErrorCode submit_input(const unsigned char *bytes, std::size_t size,
                               std::uint64_t version)
    {
        if(program_ == nullptr || size != program_->process_image.input_bytes ||
           (size != 0 && bytes == nullptr)) {
            return rt::ErrorCode::invalid_argument;
        }
        publish_channel(input_channel_, bytes, size, version);
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode output_snapshot(unsigned char *bytes, std::size_t capacity,
                                  std::size_t &written,
                                  std::uint64_t &version) const
    {
        return snapshot_channel(output_channel_,
                                program_ ? program_->process_image.output_bytes
                                         : 0,
                                bytes, capacity, written, version);
    }

    rt::ErrorCode memory_snapshot(unsigned char *bytes, std::size_t capacity,
                                  std::size_t &written,
                                  std::uint64_t &version) const
    {
        return snapshot_channel(memory_channel_,
                                program_ ? program_->process_image.memory_bytes
                                         : 0,
                                bytes, capacity, written, version);
    }

    rt::ErrorCode queue_force(const char *name, TypeId type,
                              const unsigned char *value, std::size_t size)
    {
        const int index = find_located(name);
        std::uint64_t ignored = 0;
        return index < 0
            ? rt::ErrorCode::invalid_argument
            : queue_force_index(static_cast<std::size_t>(index), type, value,
                                size, wildcard_force_owner,
                                wildcard_force_release, ignored);
    }

    rt::ErrorCode queue_force_index(std::size_t index, TypeId type,
                                    const unsigned char *value,
                                    std::size_t size, std::uint32_t owner,
                                    std::uint64_t release,
                                    std::uint64_t &queue_version)
    {
        if(program_ == nullptr || index >= force_count_ || value == nullptr)
            return rt::ErrorCode::invalid_argument;
        const LocatedVarInfo &var =
            program_->process_image.variables[index];
        if(var.type_id != type || size != var.byte_width) {
            return rt::ErrorCode::invalid_argument;
        }
        while(force_queue_lock_.test_and_set(std::memory_order_acquire)) {}
        ForceState &state = forces_[index];
        if(!state.requested_active.load(std::memory_order_acquire)) {
            std::size_t count = 0;
            for(std::size_t at = 0; at < force_count_; ++at) {
                if(forces_[at].requested_active.load(
                       std::memory_order_acquire)) {
                    ++count;
                }
            }
            if(count >= program_->process_image.max_force_entries) {
                force_queue_lock_.clear(std::memory_order_release);
                return rt::ErrorCode::capacity_exceeded;
            }
        }
        queue_version =
            force_queue_version_.fetch_add(1, std::memory_order_acq_rel) + 1U;
        publish_force(state, true, value, size, owner, release);
        state.requested_active.store(true, std::memory_order_release);
        force_queue_lock_.clear(std::memory_order_release);
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode queue_release(const char *name)
    {
        const int index = find_located(name);
        std::uint64_t ignored = 0;
        return index < 0
            ? rt::ErrorCode::invalid_argument
            : queue_release_index(static_cast<std::size_t>(index),
                                  wildcard_force_owner,
                                  wildcard_force_release, ignored);
    }

    rt::ErrorCode queue_release_index(std::size_t index, std::uint32_t owner,
                                      std::uint64_t release,
                                      std::uint64_t &queue_version)
    {
        if(program_ == nullptr || index >= force_count_)
            return rt::ErrorCode::invalid_argument;
        while(force_queue_lock_.test_and_set(std::memory_order_acquire)) {}
        ForceState &state = forces_[index];
        queue_version =
            force_queue_version_.fetch_add(1, std::memory_order_acq_rel) + 1U;
        publish_force(state, false, nullptr, 0, owner, release);
        state.requested_active.store(false, std::memory_order_release);
        force_queue_lock_.clear(std::memory_order_release);
        return rt::ErrorCode::ok;
    }

    int located_index(const char *name) const { return find_located(name); }

    int located_index(const SymbolInfo &symbol) const noexcept
    {
        if(program_ == nullptr || !symbol.located) return -1;
        for(std::size_t index = 0;
            index < program_->process_image.variables.size(); ++index) {
            const LocatedVarInfo &candidate =
                program_->process_image.variables[index];
            if(static_cast<std::uint8_t>(candidate.area) ==
                   symbol.physical_area &&
               candidate.byte_offset == symbol.physical_byte_offset &&
               candidate.bit == symbol.physical_bit &&
               candidate.byte_width == symbol.physical_width &&
               candidate.bit_address == symbol.physical_bit_address &&
               candidate.type_id == symbol.type_id)
                return static_cast<int>(index);
        }
        return -1;
    }

    std::uint64_t force_queue_version() const noexcept
    {
        return force_queue_version_.load(std::memory_order_acquire);
    }

    SnapshotError encode_snapshot(SnapshotKind kind, unsigned char *bytes,
                                  std::size_t capacity,
                                  std::size_t &written) const
    {
        written = 0;
        if(program_ == nullptr || bytes == nullptr) {
            return SnapshotError::invalid_argument;
        }
        std::size_t snapshot_bytes = 0;
        std::uint64_t snapshot_version = 0;
        if(snapshot_channel(memory_channel_,
                            program_->process_image.memory_bytes,
                            snapshot_scratch_.data(), snapshot_scratch_.size(),
                            snapshot_bytes, snapshot_version) !=
           rt::ErrorCode::ok) {
            return SnapshotError::invalid_argument;
        }
        (void)snapshot_bytes;
        (void)snapshot_version;
        const std::size_t count = snapshot_target_count(kind);
        std::size_t required = 4U + 4U + 1U + 8U + 2U;
        for(const LocatedVarInfo &var : program_->process_image.variables) {
            if(snapshot_member(var, kind)) required += 8U + 4U + 1U + var.byte_width;
        }
        if(capacity < required) return SnapshotError::capacity_exceeded;
        unsigned char *out = bytes;
        *out++ = 'S'; *out++ = 'T'; *out++ = 'L'; *out++ = '3';
        put(out, 1U, 4);
        *out++ = static_cast<unsigned char>(kind);
        put(out, program_->process_image.fingerprint, 8);
        put(out, count, 2);
        for(const LocatedVarInfo &var : program_->process_image.variables) {
            if(!snapshot_member(var, kind)) continue;
            put(out, var.stable_id, 8);
            put(out, var.type_id, 4);
            *out++ = var.byte_width;
            read_image_value(snapshot_scratch_.data(), var, out);
            out += var.byte_width;
        }
        written = static_cast<std::size_t>(out - bytes);
        return SnapshotError::ok;
    }

    SnapshotError restore_snapshot(SnapshotKind kind,
                                   const unsigned char *bytes,
                                   std::size_t size, SnapshotReport &report)
    {
        report = SnapshotReport{};
        if(program_ == nullptr || bytes == nullptr) {
            return SnapshotError::invalid_argument;
        }
        report.defaulted = static_cast<std::uint16_t>(snapshot_target_count(kind));
        if(size < 19U || std::memcmp(bytes, "STL3", 4) != 0) {
            return SnapshotError::malformed;
        }
        const unsigned char *cursor = bytes + 4;
        const unsigned char *end = bytes + size;
        std::uint64_t version = 0;
        if(!get(cursor, end, version, 4)) return SnapshotError::malformed;
        if(version != 1U) return SnapshotError::version_mismatch;
        if(cursor == end || *cursor != static_cast<unsigned char>(kind)) {
            return SnapshotError::malformed;
        }
        ++cursor;
        std::uint64_t fingerprint = 0;
        std::uint64_t count = 0;
        if(!get(cursor, end, fingerprint, 8) ||
           !get(cursor, end, count, 2)) return SnapshotError::malformed;
        report.fingerprint_match =
            fingerprint == program_->process_image.fingerprint;
        if(kind == SnapshotKind::retain && !report.fingerprint_match) {
            return SnapshotError::fingerprint_mismatch;
        }
        const SnapshotReport rejected_report = report;
        const int batch_index = claim_restore_slot();
        RestoreSlot &batch = restore_channel_.slots[
            static_cast<std::size_t>(batch_index)];
        for(RestoreState &state : batch.entries) {
            state.pending = false;
            state.seen = false;
        }
        const auto reject_batch = [&](SnapshotError error) {
            batch.state.store(slot_empty, std::memory_order_release);
            report = rejected_report;
            return error;
        };
        for(std::uint64_t item = 0; item < count; ++item) {
            std::uint64_t id = 0;
            std::uint64_t type = 0;
            if(!get(cursor, end, id, 8) || !get(cursor, end, type, 4) ||
               cursor == end) return reject_batch(SnapshotError::malformed);
            const std::uint8_t length = *cursor++;
            if(static_cast<std::size_t>(end - cursor) < length) {
                return reject_batch(SnapshotError::malformed);
            }
            int found = -1;
            for(std::size_t index = 0;
                index < program_->process_image.variables.size(); ++index) {
                const LocatedVarInfo &var = program_->process_image.variables[index];
                if(snapshot_member(var, kind) && var.stable_id == id) {
                    found = static_cast<int>(index);
                    break;
                }
            }
            if(found >= 0) {
                RestoreState &state = batch.entries[
                    static_cast<std::size_t>(found)];
                if(state.seen) return reject_batch(SnapshotError::malformed);
                state.seen = true;
                const LocatedVarInfo &var =
                    program_->process_image.variables[static_cast<std::size_t>(found)];
                if(var.type_id == type && var.byte_width == length) {
                    state.pending = true;
                    std::memcpy(state.value, cursor, length);
                    ++report.restored;
                    --report.defaulted;
                } else {
                    ++report.rejected;
                }
            }
            cursor += length;
        }
        if(cursor != end) return reject_batch(SnapshotError::malformed);
        publish_restore_batch(batch_index);
        return SnapshotError::ok;
    }

    void begin_scan(unsigned char *vars)
    {
        begin_scan(*program_, vars);
    }

    void begin_scan(const Program &layout, unsigned char *vars)
    {
        if(!transaction_active_) prepare_scan_boundary();
        std::fill(current_output_dirty_.begin(), current_output_dirty_.end(), 0);
        std::fill(current_memory_dirty_.begin(), current_memory_dirty_.end(), 0);
        for(const LocatedVarInfo &var : layout.process_image.variables) {
            const unsigned char *image = var.area == ProcessArea::input
                ? input_
                : var.area == ProcessArea::output ? output_ : memory_;
            read_image_value(image, var, vars + var.var_offset);
            const ForceState *force = active_force(var);
            if(force != nullptr) {
                std::memcpy(vars + var.var_offset, force->value,
                            var.byte_width);
                mark_physical(var, false);
            }
        }
    }

    void begin_transaction()
    {
        begin_transaction(wildcard_force_owner, wildcard_force_release);
    }

    void begin_transaction(std::uint32_t owner, std::uint64_t release)
    {
        prepare_scan_boundary(owner, release);
        std::fill(transaction_output_dirty_.begin(),
                  transaction_output_dirty_.end(), 0);
        std::fill(transaction_memory_dirty_.begin(),
                  transaction_memory_dirty_.end(), 0);
        transaction_active_ = true;
    }

    void mark_store(const Program &layout, std::uint32_t offset, TypeId type)
    {
        for(const LocatedVarInfo &var : layout.process_image.variables) {
            if(var.var_offset == offset && var.type_id == type) {
                mark_physical(var, true);
                return;
            }
        }
    }

    std::uint8_t transaction_dirty(ProcessArea area,
                                   std::size_t byte) const noexcept
    {
        const std::vector<unsigned char> *dirty =
            area == ProcessArea::output ? &transaction_output_dirty_
            : area == ProcessArea::memory ? &transaction_memory_dirty_
                                          : nullptr;
        return dirty != nullptr && byte < dirty->size() ? (*dirty)[byte] : 0;
    }

    bool park_transaction(unsigned char *output, unsigned char *output_dirty,
                          std::size_t output_size, unsigned char *memory,
                          unsigned char *memory_dirty,
                          std::size_t memory_size) const noexcept
    {
        if(!transaction_active_ || output_size != output_shadow_.size() ||
           memory_size != memory_shadow_.size() ||
           (output_size != 0 && (output == nullptr || output_dirty == nullptr)) ||
           (memory_size != 0 && (memory == nullptr || memory_dirty == nullptr)))
            return false;
        if(output_size != 0) {
            std::memcpy(output, output_, output_size);
            std::memcpy(output_dirty, transaction_output_dirty_.data(),
                        output_size);
        }
        if(memory_size != 0) {
            std::memcpy(memory, memory_, memory_size);
            std::memcpy(memory_dirty, transaction_memory_dirty_.data(),
                        memory_size);
        }
        return true;
    }

    bool restore_transaction(const unsigned char *output,
                             const unsigned char *output_dirty,
                             std::size_t output_size,
                             const unsigned char *memory,
                             const unsigned char *memory_dirty,
                             std::size_t memory_size) noexcept
    {
        if(!transaction_active_ || output_size != output_shadow_.size() ||
           memory_size != memory_shadow_.size() ||
           (output_size != 0 && (output == nullptr || output_dirty == nullptr)) ||
           (memory_size != 0 && (memory == nullptr || memory_dirty == nullptr)))
            return false;
        const auto restore = [](unsigned char *destination,
                                std::vector<unsigned char> &dirty,
                                const unsigned char *parked,
                                const unsigned char *parked_dirty,
                                std::size_t size) {
            for(std::size_t index = 0; index < size; ++index) {
                const unsigned char mask = parked_dirty[index];
                destination[index] = static_cast<unsigned char>(
                    (destination[index] & static_cast<unsigned char>(~mask)) |
                    (parked[index] & mask));
                dirty[index] = mask;
            }
        };
        restore(output_, transaction_output_dirty_, output, output_dirty,
                output_size);
        restore(memory_, transaction_memory_dirty_, memory, memory_dirty,
                memory_size);
        return true;
    }

    bool forced_store(std::uint32_t offset, TypeId type,
                      std::uint64_t &value) const
    {
        return forced_store(*program_, offset, type, value);
    }

    bool forced_store(const Program &layout, std::uint32_t offset,
                      TypeId type, std::uint64_t &value) const
    {
        for(const LocatedVarInfo &var : layout.process_image.variables) {
            if(var.var_offset != offset || var.type_id != type) continue;
            const ForceState *force = active_force(var);
            if(force == nullptr) continue;
            value = 0;
            for(std::uint8_t byte = 0; byte < var.byte_width; ++byte) {
                value |= static_cast<std::uint64_t>(force->value[byte])
                         << (byte * 8U);
            }
            return true;
        }
        return false;
    }

    void commit_scan(const unsigned char *vars)
    {
        commit_scan(*program_, vars);
    }

    void commit_scan(const Program &layout, const unsigned char *vars)
    {
        commit_scan(layout, vars, true);
    }

    void commit_scan(const Program &layout, const unsigned char *vars,
                     bool publish)
    {
        if(!output_shadow_.empty())
            std::memcpy(output_shadow_.data(), output_, output_shadow_.size());
        if(!memory_shadow_.empty())
            std::memcpy(memory_shadow_.data(), memory_, memory_shadow_.size());
        for(const LocatedVarInfo &var : layout.process_image.variables) {
            if(var.area == ProcessArea::input) continue;
            if(transaction_active_ && !current_dirty(var)) continue;
            unsigned char *image = var.area == ProcessArea::output
                ? output_shadow_.data() : memory_shadow_.data();
            write_image_value(image, var, vars + var.var_offset);
        }
        if(!output_shadow_.empty())
            std::memcpy(output_, output_shadow_.data(), output_shadow_.size());
        if(!memory_shadow_.empty())
            std::memcpy(memory_, memory_shadow_.data(), memory_shadow_.size());
        if(publish) publish_scan();
    }

    void publish_scan()
    {
        ++output_version_;
        ++memory_version_;
        publish_channel(output_channel_, output_, output_shadow_.size(),
                        output_version_);
        publish_channel(memory_channel_, memory_, memory_shadow_.size(),
                        memory_version_);
    }

    void commit_transaction()
    {
        publish_scan();
        transaction_active_ = false;
    }

    void discard_scan()
    {
        std::size_t written = 0;
        std::uint64_t version = 0;
        if(snapshot_channel(output_channel_, output_shadow_.size(),
                            output_shadow_.data(), output_shadow_.size(),
                            written, version) == rt::ErrorCode::ok) {
            if(!output_shadow_.empty())
                std::memcpy(output_, output_shadow_.data(), written);
            output_version_ = version;
        }
        if(snapshot_channel(memory_channel_, memory_shadow_.size(),
                            memory_shadow_.data(), memory_shadow_.size(),
                            written, version) == rt::ErrorCode::ok) {
            if(!memory_shadow_.empty())
                std::memcpy(memory_, memory_shadow_.data(), written);
            memory_version_ = version;
        }
        transaction_active_ = false;
    }

private:
    static constexpr std::uint8_t slot_empty = 0;
    static constexpr std::uint8_t slot_writing = 1;
    static constexpr std::uint8_t slot_published = 2;
    static constexpr std::uint8_t slot_reading = 3;

    struct ByteSlot
    {
        std::atomic<std::uint8_t> state{slot_empty};
        std::vector<unsigned char> bytes;
        std::uint64_t version = 0;
    };
    struct ByteChannel
    {
        std::array<ByteSlot, 3> slots;
        std::atomic<int> published{-1};
    };
    struct ForceState
    {
        bool active = false;
        unsigned char value[8]{};
        std::uint64_t applied_sequence = 0;
        std::atomic<std::uint64_t> pending_sequence{0};
        std::atomic<std::uint64_t> pending_value{0};
        std::atomic<std::uint64_t> pending_release{wildcard_force_release};
        std::atomic<std::uint32_t> pending_owner{wildcard_force_owner};
        std::atomic<bool> pending_active{false};
        std::atomic<bool> requested_active{false};
    };
    struct RestoreState
    {
        bool pending = false;
        bool seen = false;
        unsigned char value[8]{};
    };
    struct RestoreSlot
    {
        std::atomic<std::uint8_t> state{slot_empty};
        std::vector<RestoreState> entries;
    };
    struct RestoreChannel
    {
        std::array<RestoreSlot, 3> slots;
        std::atomic<int> published{-1};
    };

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "L3 force publication requires lock-free 64-bit atomics");

    static void put(unsigned char *&out, std::uint64_t value,
                    unsigned bytes)
    {
        for(unsigned index = 0; index < bytes; ++index)
            *out++ = static_cast<unsigned char>(value >> (index * 8U));
    }

    static bool get(const unsigned char *&cursor, const unsigned char *end,
                    std::uint64_t &value, unsigned bytes)
    {
        if(static_cast<std::size_t>(end - cursor) < bytes) return false;
        value = 0;
        for(unsigned index = 0; index < bytes; ++index)
            value |= static_cast<std::uint64_t>(*cursor++) << (index * 8U);
        return true;
    }

    static bool snapshot_member(const LocatedVarInfo &var, SnapshotKind kind)
    {
        return kind == SnapshotKind::retain ? var.retain : var.persistent;
    }

    std::size_t snapshot_target_count(SnapshotKind kind) const
    {
        std::size_t count = 0;
        for(const LocatedVarInfo &var : program_->process_image.variables)
            if(snapshot_member(var, kind)) ++count;
        return count;
    }

    int find_located(const char *name) const
    {
        if(program_ == nullptr || name == nullptr) return -1;
        for(std::size_t index = 0;
            index < program_->process_image.variables.size(); ++index) {
            const std::string &lower = program_->process_image.variables[index].lower;
            std::size_t pos = 0;
            while(name[pos] != '\0' && pos < lower.size()) {
                char c = name[pos];
                if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                if(c != lower[pos]) break;
                ++pos;
            }
            if(name[pos] == '\0' && pos == lower.size())
                return static_cast<int>(index);
        }
        return -1;
    }

    const ForceState *active_force(const LocatedVarInfo &layout) const
    {
        for(std::size_t index = 0;
            index < program_->process_image.variables.size(); ++index) {
            const LocatedVarInfo &candidate =
                program_->process_image.variables[index];
            if(!forces_[index].active || candidate.area != layout.area ||
               candidate.byte_offset != layout.byte_offset ||
               candidate.bit != layout.bit ||
               candidate.bit_address != layout.bit_address ||
               candidate.type_id != layout.type_id ||
               candidate.byte_width != layout.byte_width) {
                continue;
            }
            return &forces_[index];
        }
        return nullptr;
    }

    static void read_image_value(const unsigned char *image,
                                 const LocatedVarInfo &var,
                                 unsigned char *value)
    {
        if(var.bit_address) {
            value[0] = static_cast<unsigned char>(
                (image[var.byte_offset] >> var.bit) & 1U);
        } else {
            std::memcpy(value, image + var.byte_offset, var.byte_width);
        }
    }

    static void write_image_value(unsigned char *image,
                                  const LocatedVarInfo &var,
                                  const unsigned char *value)
    {
        if(var.bit_address) {
            const unsigned char mask =
                static_cast<unsigned char>(1U << var.bit);
            image[var.byte_offset] = value[0] != 0
                ? static_cast<unsigned char>(image[var.byte_offset] | mask)
                : static_cast<unsigned char>(image[var.byte_offset] & ~mask);
        } else {
            std::memcpy(image + var.byte_offset, value, var.byte_width);
        }
    }

    void prepare_scan_boundary(
        std::uint32_t owner = wildcard_force_owner,
        std::uint64_t release = wildcard_force_release)
    {
        apply_pending_restore();
        for(std::size_t index = 0; index < force_count_; ++index)
            consume_force(forces_[index], owner, release);
        consume_channel(input_channel_, input_,
                        program_->process_image.input_bytes, input_version_);
    }

    static std::uint8_t physical_mask(const LocatedVarInfo &var,
                                      std::size_t byte)
    {
        if(var.bit_address)
            return byte == var.byte_offset
                       ? static_cast<std::uint8_t>(1U << var.bit)
                       : 0;
        return byte >= var.byte_offset &&
                       byte < static_cast<std::size_t>(var.byte_offset) +
                                  var.byte_width
                   ? 0xFFU
                   : 0;
    }

    void mark_physical(const LocatedVarInfo &var, bool task_write)
    {
        if(var.area == ProcessArea::input) return;
        std::vector<unsigned char> &current =
            var.area == ProcessArea::output ? current_output_dirty_
                                            : current_memory_dirty_;
        std::vector<unsigned char> &transaction =
            var.area == ProcessArea::output ? transaction_output_dirty_
                                            : transaction_memory_dirty_;
        for(std::size_t byte = var.byte_offset; byte < current.size(); ++byte) {
            const std::uint8_t mask = physical_mask(var, byte);
            if(mask == 0) {
                if(!var.bit_address &&
                   byte >= static_cast<std::size_t>(var.byte_offset) +
                               var.byte_width)
                    break;
                continue;
            }
            current[byte] = static_cast<std::uint8_t>(current[byte] | mask);
            if(task_write)
                transaction[byte] =
                    static_cast<std::uint8_t>(transaction[byte] | mask);
        }
    }

    bool current_dirty(const LocatedVarInfo &var) const
    {
        const std::vector<unsigned char> &current =
            var.area == ProcessArea::output ? current_output_dirty_
                                            : current_memory_dirty_;
        for(std::size_t byte = var.byte_offset; byte < current.size(); ++byte) {
            const std::uint8_t mask = physical_mask(var, byte);
            if(mask != 0 && (current[byte] & mask) != 0) return true;
            if(!var.bit_address &&
               byte >= static_cast<std::size_t>(var.byte_offset) +
                           var.byte_width)
                break;
        }
        return false;
    }

    template<typename Channel>
    static int claim_empty_slot(Channel &channel)
    {
        for(;;) {
            const int current = channel.published.load(std::memory_order_acquire);
            for(std::size_t index = 0; index < channel.slots.size(); ++index) {
                if(static_cast<int>(index) != current) {
                    std::uint8_t stale = slot_published;
                    (void)channel.slots[index].state.compare_exchange_strong(
                        stale, slot_empty, std::memory_order_acq_rel,
                        std::memory_order_acquire);
                }
                std::uint8_t expected = slot_empty;
                if(channel.slots[index].state.compare_exchange_weak(
                       expected, slot_writing, std::memory_order_acq_rel,
                       std::memory_order_acquire)) {
                    return static_cast<int>(index);
                }
            }
        }
    }

    static void prepare_channel(ByteChannel &channel, std::size_t size)
    {
        channel.published.store(-1, std::memory_order_relaxed);
        for(ByteSlot &slot : channel.slots) {
            slot.bytes.assign(size, 0);
            slot.version = 0;
            slot.state.store(slot_empty, std::memory_order_relaxed);
        }
    }

    static void initialize_channel(ByteChannel &channel,
                                   const unsigned char *bytes,
                                   std::size_t size, std::uint64_t version)
    {
        ByteSlot &slot = channel.slots[0];
        if(size != 0) std::memcpy(slot.bytes.data(), bytes, size);
        slot.version = version;
        slot.state.store(slot_published, std::memory_order_release);
        channel.published.store(0, std::memory_order_release);
    }

    static void publish_channel(ByteChannel &channel,
                                const unsigned char *bytes,
                                std::size_t size, std::uint64_t version)
    {
        const int index = claim_empty_slot(channel);
        ByteSlot &slot = channel.slots[static_cast<std::size_t>(index)];
        if(size != 0) std::memcpy(slot.bytes.data(), bytes, size);
        slot.version = version;
        const int old = channel.published.exchange(index,
                                                   std::memory_order_acq_rel);
        slot.state.store(slot_published, std::memory_order_release);
        if(old >= 0 && old != index) {
            std::uint8_t expected = slot_published;
            (void)channel.slots[static_cast<std::size_t>(old)]
                .state.compare_exchange_strong(
                    expected, slot_empty, std::memory_order_acq_rel,
                    std::memory_order_acquire);
        }
    }

    static void finish_snapshot_read(ByteChannel &channel, int index)
    {
        ByteSlot &slot = channel.slots[static_cast<std::size_t>(index)];
        if(channel.published.load(std::memory_order_acquire) != index) {
            slot.state.store(slot_empty, std::memory_order_release);
            return;
        }
        slot.state.store(slot_published, std::memory_order_release);
        if(channel.published.load(std::memory_order_acquire) != index) {
            std::uint8_t expected = slot_published;
            (void)slot.state.compare_exchange_strong(
                expected, slot_empty, std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
    }

    static rt::ErrorCode snapshot_channel(
        ByteChannel &channel, std::size_t size, unsigned char *bytes,
        std::size_t capacity, std::size_t &written, std::uint64_t &version)
    {
        written = 0;
        if(capacity < size) return rt::ErrorCode::capacity_exceeded;
        if(size != 0 && bytes == nullptr) return rt::ErrorCode::invalid_argument;
        for(;;) {
            const int index = channel.published.load(std::memory_order_acquire);
            if(index < 0) return rt::ErrorCode::invalid_argument;
            ByteSlot &slot = channel.slots[static_cast<std::size_t>(index)];
            std::uint8_t expected = slot_published;
            if(!slot.state.compare_exchange_weak(
                   expected, slot_reading, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
                continue;
            }
            if(size != 0) std::memcpy(bytes, slot.bytes.data(), size);
            version = slot.version;
            finish_snapshot_read(channel, index);
            written = size;
            return rt::ErrorCode::ok;
        }
    }

    static bool consume_channel(ByteChannel &channel, unsigned char *bytes,
                                std::size_t size, std::uint64_t &version)
    {
        const int index = channel.published.load(std::memory_order_acquire);
        if(index < 0) return false;
        ByteSlot &slot = channel.slots[static_cast<std::size_t>(index)];
        std::uint8_t expected = slot_published;
        if(!slot.state.compare_exchange_strong(
               expected, slot_reading, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
            return false;
        }
        if(size != 0) std::memcpy(bytes, slot.bytes.data(), size);
        version = slot.version;
        slot.state.store(slot_empty, std::memory_order_release);
        return true;
    }

    static void publish_force(ForceState &state, bool active,
                              const unsigned char *value, std::size_t size,
                              std::uint32_t owner, std::uint64_t release)
    {
        std::uint64_t packed = 0;
        for(std::size_t index = 0; index < size; ++index) {
            packed |= static_cast<std::uint64_t>(value[index]) << (index * 8U);
        }
        state.pending_sequence.fetch_add(1, std::memory_order_acq_rel);
        state.pending_value.store(packed, std::memory_order_relaxed);
        state.pending_owner.store(owner, std::memory_order_relaxed);
        state.pending_release.store(release, std::memory_order_relaxed);
        state.pending_active.store(active, std::memory_order_relaxed);
        state.pending_sequence.fetch_add(1, std::memory_order_release);
    }

    static void consume_force(ForceState &state, std::uint32_t boundary_owner,
                              std::uint64_t boundary_release)
    {
        std::uint64_t sequence = 0;
        std::uint64_t value = 0;
        std::uint64_t release = 0;
        std::uint32_t owner = wildcard_force_owner;
        bool active = false;
        for(;;) {
            sequence = state.pending_sequence.load(std::memory_order_acquire);
            if((sequence & 1U) != 0) continue;
            value = state.pending_value.load(std::memory_order_relaxed);
            owner = state.pending_owner.load(std::memory_order_relaxed);
            release = state.pending_release.load(std::memory_order_relaxed);
            active = state.pending_active.load(std::memory_order_relaxed);
            if(sequence ==
               state.pending_sequence.load(std::memory_order_acquire)) {
                break;
            }
        }
        if(sequence == state.applied_sequence) return;
        if((owner != wildcard_force_owner && owner != boundary_owner) ||
           (release != wildcard_force_release &&
            release != boundary_release))
            return;
        state.active = active;
        if(active) {
            for(unsigned index = 0; index < sizeof(state.value); ++index) {
                state.value[index] = static_cast<unsigned char>(
                    value >> (index * 8U));
            }
        }
        state.applied_sequence = sequence;
    }

    void prepare_restore_channel(std::size_t entries)
    {
        restore_channel_.published.store(-1, std::memory_order_relaxed);
        for(RestoreSlot &slot : restore_channel_.slots) {
            slot.entries.assign(entries, RestoreState{});
            slot.state.store(slot_empty, std::memory_order_relaxed);
        }
    }

    int claim_restore_slot()
    {
        return claim_empty_slot(restore_channel_);
    }

    void publish_restore_batch(int index)
    {
        RestoreSlot &slot = restore_channel_.slots[
            static_cast<std::size_t>(index)];
        const int old = restore_channel_.published.exchange(
            index, std::memory_order_acq_rel);
        slot.state.store(slot_published, std::memory_order_release);
        if(old >= 0 && old != index) {
            std::uint8_t expected = slot_published;
            (void)restore_channel_.slots[static_cast<std::size_t>(old)]
                .state.compare_exchange_strong(
                    expected, slot_empty, std::memory_order_acq_rel,
                    std::memory_order_acquire);
        }
    }

    void apply_pending_restore()
    {
        const int index = restore_channel_.published.load(
            std::memory_order_acquire);
        if(index < 0) return;
        RestoreSlot &slot = restore_channel_.slots[
            static_cast<std::size_t>(index)];
        std::uint8_t expected = slot_published;
        if(!slot.state.compare_exchange_strong(
               expected, slot_reading, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
            return;
        }
        for(std::size_t at = 0; at < slot.entries.size(); ++at) {
            const RestoreState &state = slot.entries[at];
            if(!state.pending) continue;
            write_image_value(memory_, program_->process_image.variables[at],
                              state.value);
        }
        slot.state.store(slot_empty, std::memory_order_release);
    }

    const Program *program_ = nullptr;
    unsigned char *input_ = nullptr;
    unsigned char *output_ = nullptr;
    unsigned char *memory_ = nullptr;
    std::vector<unsigned char> output_shadow_;
    std::vector<unsigned char> memory_shadow_;
    std::vector<unsigned char> current_output_dirty_;
    std::vector<unsigned char> current_memory_dirty_;
    std::vector<unsigned char> transaction_output_dirty_;
    std::vector<unsigned char> transaction_memory_dirty_;
    mutable std::vector<unsigned char> snapshot_scratch_;
    std::unique_ptr<ForceState[]> forces_;
    std::size_t force_count_ = 0;
    mutable ByteChannel input_channel_;
    mutable ByteChannel output_channel_;
    mutable ByteChannel memory_channel_;
    RestoreChannel restore_channel_;
    std::uint64_t input_version_ = 0;
    std::uint64_t output_version_ = 0;
    std::uint64_t memory_version_ = 0;
    std::atomic<std::uint64_t> force_queue_version_{0};
    std::atomic_flag force_queue_lock_ = ATOMIC_FLAG_INIT;
    bool transaction_active_ = false;
};

} // namespace plcopen::core::st
