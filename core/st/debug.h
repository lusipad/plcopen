#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include "st/configuration_runtime.h"

namespace plcopen::core::st
{

class DebugSession final : public RuntimeDebugHooks
{
public:
    ~DebugSession() { detach(); }

    DebugError attach(ConfigurationRuntime &runtime, DebugTarget target,
                      std::atomic<std::uint64_t> *publish_storage,
                      std::size_t publish_word_capacity,
                      DebugSnapshotEntry *publish_entries,
                      std::size_t publish_entry_capacity,
                      std::atomic<std::uint64_t> *trace_storage,
                      std::size_t trace_word_capacity,
                      const DebugSessionOptions &options = {})
    {
        detach();
        last_error_ = DebugError::invalid_argument;
        if(runtime.program_ == nullptr || target.resource == nullptr ||
           target.task == nullptr || publish_storage == nullptr ||
           publish_entries == nullptr ||
           (trace_word_capacity != 0 && trace_storage == nullptr))
            return last_error_;
        if(runtime.debug_hooks_ != nullptr && runtime.debug_hooks_ != this)
            return last_error_;
        if(runtime.program_->debug_mode != DebugMode::enabled) {
            last_error_ = DebugError::debugging_disabled;
            return last_error_;
        }
        if(trace_word_capacity != 0 &&
           trace_word_capacity <
               sizeof(DebugTraceRecord) / sizeof(std::uint64_t))
            return last_error_ = DebugError::capacity_exceeded;
        ConfigurationRuntime::ResourceRuntime *resource =
            runtime.find_resource(target.resource);
        int task_index = resource == nullptr
                             ? -1
                             : runtime.find_task(*resource, target.task);
        if(task_index < 0) return last_error_;
        ConfigurationRuntime::TaskRuntime &task =
            resource->tasks[static_cast<std::size_t>(task_index)];
        if(task.info->mappings.empty()) return last_error_;
        const std::size_t mapping_index = task.info->mappings.front();
        if(mapping_index >= resource->mapping_count) return last_error_;
        std::vector<std::uint32_t> breakpoint_bases(
            resource->mapping_count, invalid_instruction);
        std::vector<ActivePou> active_pous;
        std::vector<SnapshotSfcStep> snapshot_sfc_steps;
        active_pous.reserve(task.info->mappings.size());
        std::size_t breakpoint_count = 0;
        for(const std::uint16_t selected : task.info->mappings) {
            if(selected >= resource->mapping_count) return last_error_;
            ConfigurationRuntime::MappingRuntime &selected_mapping =
                resource->mappings[selected];
            active_pous.push_back({
                stable_symbol_id(selected_mapping.program->program_name),
                &selected_mapping.program->program_name,
                static_cast<std::uint32_t>(selected)});
            for(std::size_t network = 0;
                network < selected_mapping.program->sfc_networks.size();
                ++network) {
                const SfcNetworkInfo &network_info =
                    selected_mapping.program->sfc_networks[network];
                const SymbolId network_id =
                    stable_symbol_id(network_info.lower);
                for(std::size_t step = 0;
                    step < network_info.steps.size(); ++step) {
                    if(snapshot_sfc_steps.size() >= kMaxDebugSfcSteps)
                        return last_error_ = DebugError::capacity_exceeded;
                    const SfcStepInfo &step_info = network_info.steps[step];
                    snapshot_sfc_steps.push_back({
                        &selected_mapping, network, step, network_id,
                        stable_symbol_id(network_info.lower + "." +
                                         step_info.lower),
                        &network_info.name, &step_info.name,
                        static_cast<std::uint32_t>(selected)});
                }
            }
            breakpoint_bases[selected] =
                static_cast<std::uint32_t>(breakpoint_count);
            breakpoint_count += resource->mappings[selected]
                                    .program->source_map.entries.size();
            if(breakpoint_count >
               std::numeric_limits<std::uint32_t>::max())
                return last_error_ = DebugError::capacity_exceeded;
        }
        const std::size_t breakpoint_words =
            (breakpoint_count + 63U) / 64U;
        const std::size_t sfc_state_words =
            (snapshot_sfc_steps.size() + 63U) / 64U;
        if(breakpoint_words + header_words + sfc_state_words >
           publish_word_capacity)
            return last_error_ = DebugError::capacity_exceeded;
        static_assert(sizeof(DebugTraceRecord) % sizeof(std::uint64_t) == 0,
                      "trace records use complete atomic words");
        const std::size_t trace_words_per_record =
            sizeof(DebugTraceRecord) / sizeof(std::uint64_t);
        const std::size_t trace_capacity = std::min(
            trace_word_capacity / trace_words_per_record,
            options.trace_capacity);
        for(std::size_t index = 0; index < publish_word_capacity; ++index)
            publish_storage[index].store(0, std::memory_order_relaxed);
        for(std::size_t index = 0;
            index < trace_capacity * trace_words_per_record;
            ++index)
            trace_storage[index].store(0, std::memory_order_relaxed);

        runtime_ = &runtime;
        resource_ = resource;
        task_ = &task;
        task_index_ = static_cast<std::size_t>(task_index);
        mapping_index_ = mapping_index;
        mapping_ = &resource->mappings[mapping_index];
        program_ = mapping_->program;
        trace_resource_id_ = stable_symbol_id(resource->info->lower).value;
        trace_task_id_ = stable_symbol_id(task.info->lower).value;
        publish_words_ = publish_storage;
        publish_word_capacity_ = publish_word_capacity;
        publish_entries_ = publish_entries;
        publish_entry_capacity_ = publish_entry_capacity;
        trace_words_ = trace_storage;
        trace_words_per_record_ = trace_words_per_record;
        trace_storage_capacity_ = trace_capacity;
        options_ = options;
        watches_.clear();
        watches_.reserve(options.max_watch_symbols);
        watch_plan_state_.store(watch_plan_open,
                                std::memory_order_relaxed);
        snapshot_ready_.store(true, std::memory_order_relaxed);
        value_word_count_ = 0;
        value_bytes_ = 0;
        breakpoints_.clear();
        breakpoints_.reserve(options.max_breakpoints);
        mapping_breakpoint_bases_ =
            static_cast<std::vector<std::uint32_t> &&>(breakpoint_bases);
        active_pous_ = static_cast<std::vector<ActivePou> &&>(active_pous);
        snapshot_sfc_steps_ =
            static_cast<std::vector<SnapshotSfcStep> &&>(snapshot_sfc_steps);
        sfc_state_word_count_ = sfc_state_words;
        breakpoint_word_count_ = breakpoint_words;
        breakpoint_words_ = publish_words_ +
                            publish_word_capacity_ - breakpoint_word_count_;
        trace_lock_.store(0, std::memory_order_relaxed);
        trace_begin_.store(0, std::memory_order_relaxed);
        trace_size_.store(0, std::memory_order_relaxed);
        trace_sequence_.store(0, std::memory_order_relaxed);
        trace_dropped_.store(0, std::memory_order_relaxed);
        stop_pending_.store(false, std::memory_order_relaxed);
        command_mailbox_.store(0, std::memory_order_relaxed);
        observed_task_state_.store(
            static_cast<std::uint8_t>(task.storage->status.state),
            std::memory_order_relaxed);
        observed_release_count_.store(task.storage->status.release_count,
                                      std::memory_order_relaxed);
        current_entry_.store(nullptr, std::memory_order_relaxed);
        current_mapping_.store(UINT32_MAX, std::memory_order_relaxed);
        current_instruction_.store(0, std::memory_order_relaxed);
        step_command_ = DebugCommand::continue_;
        skip_instruction_valid_ = false;
        runtime.debug_hooks_ = this;
        for(const std::uint16_t index : task.info->mappings)
            resource->mappings[index].instance.set_debug_hooks(this, index);
        last_error_ = DebugError::ok;
        publish_snapshot(snapshot_initial);
        return last_error_;
    }

    DebugError last_error() const noexcept { return last_error_; }

    DebugError add_watch(SymbolId id)
    {
        if(program_ == nullptr) return DebugError::invalid_argument;
        std::uint8_t expected = watch_plan_open;
        if(!watch_plan_state_.compare_exchange_strong(
               expected, watch_plan_editing, std::memory_order_acq_rel,
               std::memory_order_relaxed))
            return expected == watch_plan_frozen ||
                           expected == watch_plan_freeze_pending
                       ? DebugError::watch_plan_frozen
                       : DebugError::snapshot_busy;
        const auto finish = [this](DebugError error) {
            std::uint8_t editing = watch_plan_editing;
            if(!watch_plan_state_.compare_exchange_strong(
                   editing, watch_plan_open, std::memory_order_release,
                   std::memory_order_relaxed))
                watch_plan_state_.store(watch_plan_frozen,
                                        std::memory_order_release);
            return error;
        };
        for(const Watch &watch : watches_)
            if(watch.symbol->id == id) return finish(DebugError::ok);
        if(watches_.size() >= options_.max_watch_symbols ||
           watches_.size() >= publish_entry_capacity_)
            return finish(DebugError::capacity_exceeded);
        const SymbolInfo *symbol = find_symbol(id);
        ConfigurationRuntime::MappingRuntime *watch_mapping =
            symbol == nullptr ? nullptr : find_mapping(*symbol);
        if(watch_mapping == nullptr)
            return finish(DebugError::invalid_symbol);
        const std::size_t word = value_word_count_ + header_words +
                                 sfc_state_word_count_;
        const std::size_t words = (symbol->size + 7U) / 8U;
        if(word + words > publish_word_capacity_ - breakpoint_word_count_)
            return finish(DebugError::capacity_exceeded);
        Watch watch;
        watch.symbol = symbol;
        watch.mapping = watch_mapping;
        watch.word = word;
        watches_.push_back(watch);
        DebugSnapshotEntry &entry = publish_entries_[watches_.size() - 1U];
        entry.symbol_id = id;
        entry.type_id = symbol->type_id;
        entry.offset = static_cast<std::uint32_t>(value_bytes_);
        entry.size = symbol->size;
        value_word_count_ += words;
        value_bytes_ += symbol->size;
        snapshot_ready_.store(false, std::memory_order_release);
        return finish(DebugError::ok);
    }

    DebugError add_breakpoint(const char *source, std::uint32_t line,
                              std::uint32_t column, BreakpointId &id)
    {
        if(last_error_ == DebugError::debugging_disabled)
            return DebugError::debugging_disabled;
        if(resource_ == nullptr || task_ == nullptr)
            return DebugError::invalid_argument;
        bool found = false;
        id = invalid_instruction;
        std::vector<std::uint32_t> keys;
        keys.reserve(task_->info->mappings.size());
        for(const std::uint16_t mapping : task_->info->mappings) {
            const SourceMapEntry *entry =
                find_source(mapping, source, line, column);
            if(entry == nullptr) continue;
            const std::uint32_t key = breakpoint_key(mapping, *entry);
            bool duplicate = false;
            for(const std::uint32_t candidate : keys)
                duplicate = duplicate || candidate == key;
            if(duplicate) continue;
            keys.push_back(key);
            if(!found) id = key;
            found = true;
        }
        if(!found) return DebugError::invalid_source_location;
        std::size_t additions = 0;
        for(std::size_t index = 0; index < keys.size(); ++index) {
            bool present = false;
            for(const std::uint32_t existing : breakpoints_)
                present = present || existing == keys[index];
            if(!present) ++additions;
        }
        if(additions > options_.max_breakpoints - breakpoints_.size())
            return DebugError::capacity_exceeded;
        for(std::size_t index = 0; index < keys.size(); ++index) {
            bool present = false;
            for(const std::uint32_t existing : breakpoints_)
                present = present || existing == keys[index];
            if(present) continue;
            breakpoints_.push_back(keys[index]);
            set_breakpoint(keys[index]);
        }
        return DebugError::ok;
    }

    DebugError add_breakpoint_instruction(std::uint32_t instruction,
                                          BreakpointId &id)
    {
        if(last_error_ == DebugError::debugging_disabled)
            return DebugError::debugging_disabled;
        if(resource_ == nullptr || task_ == nullptr)
            return DebugError::invalid_argument;
        const SourceMapEntry *match = nullptr;
        std::size_t match_mapping = 0;
        for(const std::uint16_t mapping : task_->info->mappings) {
            for(const SourceMapEntry &entry : resource_->mappings[mapping]
                                                    .program->source_map.entries) {
                if(entry.instruction != instruction) continue;
                if(match != nullptr) return DebugError::invalid_instruction;
                match = &entry;
                match_mapping = mapping;
            }
        }
        return match == nullptr
                   ? DebugError::invalid_instruction
                   : add_breakpoint_entry(*match, match_mapping, id);
    }

    DebugError remove_breakpoint_at(const char *source, std::uint32_t line,
                                    std::uint32_t column)
    {
        if(last_error_ == DebugError::debugging_disabled)
            return DebugError::debugging_disabled;
        if(resource_ == nullptr || task_ == nullptr)
            return DebugError::invalid_argument;
        bool removed = false;
        for(const std::uint16_t mapping : task_->info->mappings) {
            const SourceMapEntry *entry =
                find_source(mapping, source, line, column);
            if(entry == nullptr) continue;
            const std::uint32_t key = breakpoint_key(mapping, *entry);
            for(std::size_t index = 0; index < breakpoints_.size(); ++index) {
                if(breakpoints_[index] == key) {
                    breakpoints_.erase(breakpoints_.begin() + index);
                    clear_breakpoint(key);
                    removed = true;
                    break;
                }
            }
        }
        return removed ? DebugError::ok
                       : DebugError::invalid_source_location;
    }

    std::size_t breakpoint_count() const noexcept
    {
        return runtime_ == nullptr ? 0 : breakpoints_.size();
    }

    DebugError poll_stop(DebugStop &stop) const noexcept
    {
        if(runtime_ == nullptr) return DebugError::invalid_argument;
        if(!stop_pending_.load(std::memory_order_acquire))
            return DebugError::invalid_argument;
        for(std::size_t attempt = 0; attempt < 8; ++attempt) {
            const std::uint64_t before =
                published_stop_.sequence.load(std::memory_order_acquire);
            if((before & 1U) != 0) continue;
            const std::string *task =
                published_stop_.task.load(std::memory_order_relaxed);
            const std::string *pou =
                published_stop_.pou.load(std::memory_order_relaxed);
            const std::string *source =
                published_stop_.source.load(std::memory_order_relaxed);
            const std::string *sfc =
                published_stop_.sfc.load(std::memory_order_relaxed);
            DebugStop candidate{};
            candidate.reason = static_cast<DebugStopReason>(
                published_stop_.reason.load(std::memory_order_relaxed));
            candidate.task = task == nullptr ? std::string_view{} : *task;
            candidate.pou = pou == nullptr ? std::string_view{} : *pou;
            candidate.source_name =
                source == nullptr ? std::string_view{} : *source;
            candidate.sfc = sfc == nullptr ? std::string_view{} : *sfc;
            candidate.line =
                published_stop_.line.load(std::memory_order_relaxed);
            candidate.column =
                published_stop_.column.load(std::memory_order_relaxed);
            candidate.instruction =
                published_stop_.instruction.load(std::memory_order_relaxed);
            candidate.instruction_id.artifact =
                published_stop_.artifact.load(std::memory_order_relaxed);
            candidate.instruction_id.mapping =
                published_stop_.mapping.load(std::memory_order_relaxed);
            candidate.instruction_id.region =
                published_stop_.region.load(std::memory_order_relaxed);
            candidate.instruction_id.offset =
                published_stop_.offset.load(std::memory_order_relaxed);
            candidate.instruction_id.region_kind =
                static_cast<DebugRegionKind>(published_stop_.region_kind.load(
                    std::memory_order_relaxed));
            candidate.call_depth =
                published_stop_.call_depth.load(std::memory_order_relaxed);
            const std::uint64_t after =
                published_stop_.sequence.load(std::memory_order_acquire);
            if(before != after || (after & 1U) != 0) continue;
            stop = candidate;
            return DebugError::ok;
        }
        return DebugError::snapshot_busy;
    }

    DebugError control(DebugCommand command) noexcept
    {
        if(last_error_ == DebugError::debugging_disabled)
            return DebugError::debugging_disabled;
        if(task_ == nullptr) return DebugError::invalid_argument;
        if(static_cast<TaskState>(observed_task_state_.load(
               std::memory_order_acquire)) == TaskState::faulted)
            return DebugError::task_faulted;
        const TaskState observed = static_cast<TaskState>(
            observed_task_state_.load(std::memory_order_acquire));
        if(command == DebugCommand::continue_) {
            command_mailbox_.store(static_cast<std::uint8_t>(command) + 1U,
                                   std::memory_order_release);
            return DebugError::ok;
        }
        if(!stop_pending_.load(std::memory_order_acquire) ||
           observed != TaskState::paused)
            return DebugError::invalid_argument;
        command_mailbox_.store(static_cast<std::uint8_t>(command) + 1U,
                               std::memory_order_release);
        return DebugError::ok;
    }

    DebugError read_snapshot(DebugSnapshot &snapshot,
                             DebugSnapshotEntry *entries,
                             std::size_t entry_capacity,
                             unsigned char *values,
                             std::size_t value_capacity,
                             std::size_t retries) const noexcept
    {
        snapshot = {};
        if(runtime_ == nullptr || entries == nullptr || values == nullptr)
            return DebugError::invalid_argument;
        if(entry_capacity < watches_.size() || value_capacity < value_bytes_)
            return DebugError::capacity_exceeded;
        if(!snapshot_ready_.load(std::memory_order_acquire))
            return DebugError::snapshot_busy;
        if(retries == 0) return DebugError::snapshot_busy;
        for(std::size_t attempt = 0; attempt < retries; ++attempt) {
            const std::uint64_t before =
                publish_words_[0].load(std::memory_order_acquire);
            if((before & 1U) != 0) continue;
            const std::uint64_t state_bits =
                publish_words_[1].load(std::memory_order_acquire);
            const std::uint64_t fault =
                publish_words_[2].load(std::memory_order_acquire);
            const std::uint64_t entry_bits =
                publish_words_[3].load(std::memory_order_acquire);
            const std::uint32_t active_mapping = static_cast<std::uint32_t>(
                publish_words_[4].load(std::memory_order_acquire));
            const std::uint32_t active_instruction =
                static_cast<std::uint32_t>(
                    publish_words_[5].load(std::memory_order_acquire));
            const std::uint8_t state = static_cast<std::uint8_t>(state_bits);
            const std::uint8_t snapshot_kind =
                static_cast<std::uint8_t>(state_bits >> 8U);
            std::array<std::uint64_t,
                       (kMaxDebugSfcSteps + 63U) / 64U> sfc_bits{};
            for(std::size_t word = 0; word < sfc_state_word_count_; ++word)
                sfc_bits[word] = publish_words_[header_words + word].load(
                    std::memory_order_relaxed);
            std::size_t destination = 0;
            for(std::size_t index = 0; index < watches_.size(); ++index) {
                entries[index] = publish_entries_[index];
                const Watch &watch = watches_[index];
                std::size_t remaining = watch.symbol->size;
                std::size_t word = watch.word;
                while(remaining != 0) {
                    const std::uint64_t bits =
                        publish_words_[word++].load(std::memory_order_relaxed);
                    const std::size_t bytes = std::min<std::size_t>(8, remaining);
                    std::memcpy(values + destination, &bits, bytes);
                    destination += bytes;
                    remaining -= bytes;
                }
            }
            const std::uint64_t after =
                publish_words_[0].load(std::memory_order_acquire);
            if(before != after || (after & 1U) != 0) continue;
            snapshot.version = after / 2U;
            snapshot.value_count = watches_.size();
            snapshot.value_bytes = value_bytes_;
            snapshot.task_state = static_cast<TaskState>(state);
            snapshot.fault = static_cast<TaskFault>(fault);
            const SourceMapEntry *active =
                reinterpret_cast<const SourceMapEntry *>(
                    static_cast<std::uintptr_t>(entry_bits));
            if(active != nullptr) {
                std::string_view path = active->call_path;
                std::size_t begin = 0;
                while(begin < path.size() &&
                      snapshot.active_pou_count < kMaxDebugStackFrames) {
                    const std::size_t slash = path.find('/', begin);
                    const std::size_t end = slash == std::string_view::npos
                                                ? path.size()
                                                : slash;
                    snapshot.call_stack[snapshot.active_pou_count++].pou =
                        path.substr(begin, end - begin);
                    if(slash == std::string_view::npos) break;
                    begin = slash + 1U;
                }
                if(snapshot.active_pou_count == 0) {
                    snapshot.call_stack[0].pou = active->pou;
                    snapshot.active_pou_count = 1;
                }
                DebugStackFrame &leaf =
                    snapshot.call_stack[snapshot.active_pou_count - 1U];
                leaf.pou_id = SymbolId{active->pou_id};
                leaf.source_name = active->source_name;
                leaf.sfc = active->sfc;
                leaf.line = active->line;
                leaf.column = active->column;
                leaf.instruction_id = active->instruction_id;
                leaf.instruction_id.mapping = active_mapping;
                leaf.instruction_id.offset = active_instruction;
                snapshot.call_depth = static_cast<std::uint16_t>(
                    snapshot.active_pou_count);
            }
            for(std::size_t index = 0;
                index < snapshot_sfc_steps_.size(); ++index) {
                const SnapshotSfcStep &published = snapshot_sfc_steps_[index];
                DebugSfcStepState &step =
                    snapshot.sfc_steps[snapshot.sfc_step_count++];
                step.network_id = published.network_id;
                step.step_id = published.step_id;
                step.network = *published.network;
                step.step = *published.step;
                step.mapping = published.mapping;
                step.active =
                    (sfc_bits[index / 64U] &
                     (std::uint64_t{1} << (index % 64U))) != 0;
            }
            return DebugError::ok;
        }
        return DebugError::snapshot_busy;
    }

    DebugError write_symbol(SymbolId, TypeId, const void *,
                            std::size_t) const noexcept
    {
        return DebugError::permission_denied;
    }

    DebugError queue_force(SymbolId id, TypeId type, const void *value,
                           std::size_t size, ForceReceipt &receipt)
    {
        if(resource_ == nullptr || task_ == nullptr)
            return DebugError::invalid_argument;
        const SymbolInfo *symbol = find_symbol(id);
        if(symbol == nullptr || !symbol->located)
            return DebugError::invalid_symbol;
        if(symbol->type_id != type || symbol->size != size)
            return DebugError::type_mismatch;
        const int physical_index = resource_->image.located_index(*symbol);
        if(physical_index < 0) return DebugError::invalid_symbol;
        const std::uint64_t target_release =
            observed_release_count_.load(std::memory_order_acquire) +
            (static_cast<TaskState>(observed_task_state_.load(
                 std::memory_order_acquire)) == TaskState::paused ? 0U : 1U);
        std::uint64_t queue_version = 0;
        const rt::ErrorCode error = resource_->image.queue_force_index(
            static_cast<std::size_t>(physical_index), type,
            static_cast<const unsigned char *>(value), size,
            static_cast<std::uint32_t>(task_index_), target_release,
            queue_version);
        if(error != rt::ErrorCode::ok)
            return error == rt::ErrorCode::capacity_exceeded
                       ? DebugError::capacity_exceeded
                       : DebugError::invalid_argument;
        receipt.queue_version = queue_version;
        receipt.target_release = target_release;
        receipt.target_resource = resource_->info->lower;
        receipt.target_task = task_->info->lower;
        return DebugError::ok;
    }

    DebugError release_force(SymbolId id, ForceReceipt &receipt)
    {
        if(resource_ == nullptr || task_ == nullptr)
            return DebugError::invalid_argument;
        const SymbolInfo *symbol = find_symbol(id);
        if(symbol == nullptr || !symbol->located)
            return DebugError::invalid_symbol;
        const int physical_index = resource_->image.located_index(*symbol);
        if(physical_index < 0) return DebugError::invalid_symbol;
        const std::uint64_t target_release =
            observed_release_count_.load(std::memory_order_acquire) +
            (static_cast<TaskState>(observed_task_state_.load(
                 std::memory_order_acquire)) == TaskState::paused ? 0U : 1U);
        std::uint64_t queue_version = 0;
        if(resource_->image.queue_release_index(
               static_cast<std::size_t>(physical_index),
               static_cast<std::uint32_t>(task_index_), target_release,
               queue_version) != rt::ErrorCode::ok)
            return DebugError::invalid_argument;
        receipt.queue_version = queue_version;
        receipt.target_release = target_release;
        receipt.target_resource = resource_->info->lower;
        receipt.target_task = task_->info->lower;
        return DebugError::ok;
    }

    DebugError read_trace(DebugTraceRecord *records, std::size_t capacity,
                          DebugTraceReport &report) const noexcept
    {
        report = {};
        if(runtime_ == nullptr || records == nullptr)
            return DebugError::invalid_argument;
        for(std::size_t attempt = 0; attempt < 8; ++attempt) {
            const std::uint64_t before =
                trace_lock_.load(std::memory_order_acquire);
            if((before & 1U) != 0) continue;
            const std::size_t size =
                trace_size_.load(std::memory_order_acquire);
            const std::size_t begin =
                trace_begin_.load(std::memory_order_acquire);
            const std::size_t written = std::min(capacity, size);
            const std::size_t skip = size - written;
            for(std::size_t index = 0; index < written; ++index) {
                const std::size_t logical = skip + index;
                const std::size_t slot = (begin + logical) %
                                         trace_storage_capacity_;
                for(std::size_t word = 0; word < trace_words_per_record_;
                    ++word) {
                    const std::uint64_t bits = trace_words_[
                        slot * trace_words_per_record_ + word]
                        .load(std::memory_order_relaxed);
                    std::memcpy(reinterpret_cast<unsigned char *>(
                                    &records[index]) +
                                    word * sizeof(bits),
                                &bits, sizeof(bits));
                }
            }
            const std::uint64_t dropped =
                trace_dropped_.load(std::memory_order_acquire);
            const std::uint64_t after =
                trace_lock_.load(std::memory_order_acquire);
            if(before != after || (after & 1U) != 0) continue;
            report.version = after / 2U;
            report.written = written;
            report.dropped = dropped;
            return DebugError::ok;
        }
        return DebugError::snapshot_busy;
    }

    bool on_probe(std::size_t mapping, const SourceMapEntry &entry,
                  std::uint32_t instruction) noexcept override
    {
        if(!mapping_selected(mapping) || program_ == nullptr) return false;
        current_mapping_.store(static_cast<std::uint32_t>(mapping),
                               std::memory_order_relaxed);
        current_instruction_.store(instruction, std::memory_order_relaxed);
        current_entry_.store(&entry, std::memory_order_release);
        InstructionId instruction_id = entry.instruction_id;
        instruction_id.mapping = static_cast<std::uint32_t>(mapping);
        if(entry.sfc.empty()) {
            push_trace(DebugEventKind::pou, instruction, &instruction_id,
                       &entry);
            if(entry.event_kind == DebugEventKind::fb)
                push_trace(DebugEventKind::fb, instruction, &instruction_id,
                           &entry);
        } else {
            push_trace(DebugEventKind::sfc, instruction, &instruction_id,
                       &entry);
        }
        if(skip_instruction_valid_ &&
           instruction_id == skip_instruction_once_) {
            skip_instruction_valid_ = false;
            return false;
        }
        bool stop = breakpoint_set(breakpoint_key(mapping, entry));
        DebugStopReason reason = DebugStopReason::breakpoint;
        if(step_command_ != DebugCommand::continue_) {
            if(step_command_ == DebugCommand::step_in) {
                stop = true;
            } else if(step_command_ == DebugCommand::step_over) {
                stop = entry.call_depth <= step_origin_depth_;
            } else {
                stop = entry.call_depth < step_origin_depth_;
            }
            reason = DebugStopReason::step;
        }
        if(!stop) return false;
        rt_stop_entry_ = &entry;
        rt_stop_instruction_ = instruction;
        rt_stop_reason_ = reason;
        rt_stop_instruction_id_ = instruction_id;
        rt_stop_depth_ = entry.call_depth;
        step_command_ = DebugCommand::continue_;
        return true;
    }

    void on_task_boundary(const void *resource, std::size_t task,
                          bool faulted) noexcept override
    {
        if(resource != resource_ || task != task_index_) return;
        bool publish = false;
        for(;;) {
            std::uint8_t state =
                watch_plan_state_.load(std::memory_order_acquire);
            if(state == watch_plan_frozen) {
                publish = true;
                break;
            }
            if(state == watch_plan_freeze_pending) break;
            const std::uint8_t desired = state == watch_plan_open
                                             ? watch_plan_frozen
                                             : watch_plan_freeze_pending;
            if(watch_plan_state_.compare_exchange_weak(
                   state, desired, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
                publish = desired == watch_plan_frozen;
                break;
            }
        }
        if(!faulted) current_entry_.store(nullptr, std::memory_order_release);
        if(publish)
            publish_snapshot(faulted ? snapshot_fault : snapshot_committed);
        observed_task_state_.store(
            static_cast<std::uint8_t>(task_->storage->status.state),
            std::memory_order_release);
        observed_release_count_.store(task_->storage->status.release_count,
                                      std::memory_order_release);
        push_trace(DebugEventKind::task);
    }

    void on_debug_event(const void *resource, std::size_t task,
                        DebugEventKind kind,
                        std::uint32_t instruction,
                        const SourceMapEntry *entry,
                        const InstructionId *instruction_id) noexcept override
    {
        if(resource != resource_ || task != task_index_) return;
        observed_task_state_.store(
            static_cast<std::uint8_t>(task_->storage->status.state),
            std::memory_order_release);
        observed_release_count_.store(task_->storage->status.release_count,
                                      std::memory_order_release);
        if(kind == DebugEventKind::task &&
           task_->storage->status.state == TaskState::paused) {
            if(rt_stop_entry_ != nullptr) {
                publish_stop(rt_stop_reason_, *rt_stop_entry_,
                             rt_stop_instruction_, rt_stop_instruction_id_);
                stop_pending_.store(true, std::memory_order_release);
                rt_stop_entry_ = nullptr;
            }
            publish_snapshot(snapshot_paused);
        }
        push_trace(kind, instruction, instruction_id, entry);
    }

    void on_scheduler_boundary() noexcept override
    {
        if(task_ == nullptr) return;
        TaskStatus &status = task_->storage->status;
        const std::uint8_t packed =
            command_mailbox_.exchange(0, std::memory_order_acq_rel);
        if(packed != 0 && status.state == TaskState::paused) {
            skip_instruction_once_ = rt_stop_instruction_id_;
            skip_instruction_valid_ = true;
            step_command_ = static_cast<DebugCommand>(packed - 1U);
            step_origin_depth_ = rt_stop_depth_;
            stop_pending_.store(false, std::memory_order_release);
            status.state = TaskState::running;
        } else if(packed != 0 && status.state != TaskState::faulted) {
            stop_pending_.store(false, std::memory_order_release);
            step_command_ = DebugCommand::continue_;
        }
        observed_task_state_.store(static_cast<std::uint8_t>(status.state),
                                   std::memory_order_release);
        observed_release_count_.store(status.release_count,
                                      std::memory_order_release);
    }

    void on_runtime_detach() noexcept override
    {
        runtime_ = nullptr;
        resource_ = nullptr;
        task_ = nullptr;
        mapping_ = nullptr;
        program_ = nullptr;
        publish_words_ = nullptr;
        publish_entries_ = nullptr;
        trace_words_ = nullptr;
        stop_pending_.store(false, std::memory_order_release);
        command_mailbox_.store(0, std::memory_order_release);
        current_entry_.store(nullptr, std::memory_order_release);
        reset_detached_state();
    }

private:
    static constexpr std::size_t header_words = 6;
    static constexpr std::uint32_t invalid_instruction = UINT32_MAX;
    static constexpr std::uint8_t watch_plan_open = 0;
    static constexpr std::uint8_t watch_plan_editing = 1;
    static constexpr std::uint8_t watch_plan_freeze_pending = 2;
    static constexpr std::uint8_t watch_plan_frozen = 3;
    static constexpr std::uint8_t snapshot_initial = 0;
    static constexpr std::uint8_t snapshot_committed = 1;
    static constexpr std::uint8_t snapshot_paused = 2;
    static constexpr std::uint8_t snapshot_fault = 3;
    struct ActivePou
    {
        SymbolId id{};
        const std::string *name = nullptr;
        std::uint32_t mapping = 0;
    };
    struct SnapshotSfcStep
    {
        ConfigurationRuntime::MappingRuntime *mapping_runtime = nullptr;
        std::size_t network_index = 0;
        std::size_t step_index = 0;
        SymbolId network_id{};
        SymbolId step_id{};
        const std::string *network = nullptr;
        const std::string *step = nullptr;
        std::uint32_t mapping = 0;
    };
    struct Watch
    {
        const SymbolInfo *symbol = nullptr;
        ConfigurationRuntime::MappingRuntime *mapping = nullptr;
        std::size_t word = 0;
    };
    struct PublishedStop
    {
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<const std::string *> task{nullptr};
        std::atomic<const std::string *> pou{nullptr};
        std::atomic<const std::string *> source{nullptr};
        std::atomic<const std::string *> sfc{nullptr};
        std::atomic<std::uint32_t> line{0};
        std::atomic<std::uint32_t> column{0};
        std::atomic<std::uint32_t> instruction{0};
        std::atomic<std::uint32_t> artifact{0};
        std::atomic<std::uint32_t> mapping{UINT32_MAX};
        std::atomic<std::uint32_t> region{0};
        std::atomic<std::uint32_t> offset{0};
        std::atomic<std::uint16_t> call_depth{0};
        std::atomic<std::uint8_t> reason{0};
        std::atomic<std::uint8_t> region_kind{0};
    };

    void detach() noexcept
    {
        if(runtime_ != nullptr && resource_ != nullptr && task_ != nullptr) {
            for(const std::uint16_t index : task_->info->mappings)
                resource_->mappings[index].instance.set_debug_hooks(nullptr,
                                                                    index);
        }
        if(runtime_ != nullptr && runtime_->debug_hooks_ == this)
            runtime_->debug_hooks_ = nullptr;
        runtime_ = nullptr;
        resource_ = nullptr;
        task_ = nullptr;
        mapping_ = nullptr;
        program_ = nullptr;
        publish_words_ = nullptr;
        publish_entries_ = nullptr;
        trace_words_ = nullptr;
        stop_pending_.store(false, std::memory_order_release);
        command_mailbox_.store(0, std::memory_order_release);
        current_entry_.store(nullptr, std::memory_order_release);
        reset_detached_state();
    }

    void reset_detached_state() noexcept
    {
        publish_word_capacity_ = 0;
        publish_entry_capacity_ = 0;
        trace_words_per_record_ = 0;
        trace_storage_capacity_ = 0;
        breakpoint_words_ = nullptr;
        breakpoint_word_count_ = 0;
        value_word_count_ = 0;
        value_bytes_ = 0;
        mapping_breakpoint_bases_.clear();
        breakpoints_.clear();
        watches_.clear();
        active_pous_.clear();
        snapshot_sfc_steps_.clear();
        sfc_state_word_count_ = 0;
        current_mapping_.store(UINT32_MAX, std::memory_order_release);
        current_instruction_.store(0, std::memory_order_release);
    }

    static bool same_name(std::string_view left, const char *right) noexcept
    {
        if(right == nullptr || left.size() != std::strlen(right)) return false;
        for(std::size_t index = 0; index < left.size(); ++index) {
            char a = left[index]; char b = right[index];
            if(a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if(b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if(a != b) return false;
        }
        return true;
    }

    const SymbolInfo *find_symbol(SymbolId id) const noexcept
    {
        if(runtime_ == nullptr || id.value == 0) return nullptr;
        for(const SymbolInfo &symbol : runtime_->program_->symbols)
            if(symbol.id == id) return &symbol;
        return nullptr;
    }

    ConfigurationRuntime::MappingRuntime *find_mapping(
        const SymbolInfo &symbol) const noexcept
    {
        if(resource_ == nullptr || task_ == nullptr) return nullptr;
        for(const std::uint16_t index : task_->info->mappings) {
            if(index < resource_->mapping_count &&
               resource_->mappings[index].info->lower == symbol.instance)
                return &resource_->mappings[index];
        }
        return nullptr;
    }

    const SourceMapEntry *find_source(std::size_t mapping,
                                      const char *source, std::uint32_t line,
                                      std::uint32_t column) const noexcept
    {
        if(!mapping_selected(mapping)) return nullptr;
        const SourceMapEntry *first = nullptr;
        const SourceMapEntry *best = nullptr;
        for(const SourceMapEntry &entry :
            resource_->mappings[mapping].program->source_map.entries) {
            if(entry.line != line || !same_name(entry.source_name, source))
                continue;
            if(first == nullptr || entry.column < first->column) first = &entry;
            if(entry.column <= column &&
               (best == nullptr || entry.column > best->column))
                best = &entry;
        }
        return best == nullptr ? first : best;
    }

    bool mapping_selected(std::size_t mapping) const noexcept
    {
        return mapping < mapping_breakpoint_bases_.size() &&
               mapping_breakpoint_bases_[mapping] != invalid_instruction;
    }

    std::uint32_t breakpoint_key(
        std::size_t mapping, const SourceMapEntry &entry) const noexcept
    {
        return mapping_breakpoint_bases_[mapping] + entry.breakpoint_key;
    }

    DebugError add_breakpoint_entry(const SourceMapEntry &entry,
                                    std::size_t mapping,
                                    BreakpointId &id)
    {
        const std::uint32_t key = breakpoint_key(mapping, entry);
        for(const std::uint32_t existing : breakpoints_) {
            if(existing == key) {
                id = existing;
                return DebugError::ok;
            }
        }
        if(breakpoints_.size() >= options_.max_breakpoints)
            return DebugError::capacity_exceeded;
        breakpoints_.push_back(key);
        set_breakpoint(key);
        id = key;
        return DebugError::ok;
    }

    void publish_stop(DebugStopReason reason, const SourceMapEntry &entry,
                      std::uint32_t instruction,
                      const InstructionId &instruction_id) noexcept
    {
        const std::uint64_t prior =
            published_stop_.sequence.load(std::memory_order_relaxed);
        published_stop_.sequence.store(prior | 1U,
                                       std::memory_order_release);
        published_stop_.task.store(&task_->info->lower,
                                   std::memory_order_relaxed);
        published_stop_.pou.store(&entry.pou, std::memory_order_relaxed);
        published_stop_.source.store(&entry.source_name,
                                     std::memory_order_relaxed);
        published_stop_.sfc.store(&entry.sfc, std::memory_order_relaxed);
        published_stop_.line.store(entry.line, std::memory_order_relaxed);
        published_stop_.column.store(entry.column,
                                     std::memory_order_relaxed);
        published_stop_.instruction.store(instruction,
                                          std::memory_order_relaxed);
        published_stop_.artifact.store(instruction_id.artifact,
                                       std::memory_order_relaxed);
        published_stop_.mapping.store(instruction_id.mapping,
                                      std::memory_order_relaxed);
        published_stop_.region.store(instruction_id.region,
                                     std::memory_order_relaxed);
        published_stop_.offset.store(instruction_id.offset,
                                     std::memory_order_relaxed);
        published_stop_.call_depth.store(entry.call_depth,
                                         std::memory_order_relaxed);
        published_stop_.reason.store(static_cast<std::uint8_t>(reason),
                                     std::memory_order_relaxed);
        published_stop_.region_kind.store(
            static_cast<std::uint8_t>(instruction_id.region_kind),
            std::memory_order_relaxed);
        published_stop_.sequence.store((prior | 1U) + 1U,
                                       std::memory_order_release);
    }

    void publish_snapshot(std::uint8_t snapshot_kind) noexcept
    {
        if(publish_words_ == nullptr || publish_word_capacity_ < header_words)
            return;
        const std::uint64_t prior =
            publish_words_[0].load(std::memory_order_relaxed);
        publish_words_[0].store(prior | 1U, std::memory_order_release);
        publish_words_[1].store(
            static_cast<std::uint64_t>(task_->storage->status.state) |
                (static_cast<std::uint64_t>(snapshot_kind) << 8U),
            std::memory_order_relaxed);
        publish_words_[2].store(
            static_cast<std::uint64_t>(task_->storage->status.fault),
            std::memory_order_relaxed);
        const SourceMapEntry *active =
            current_entry_.load(std::memory_order_acquire);
        publish_words_[3].store(
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                active)),
            std::memory_order_relaxed);
        publish_words_[4].store(
            current_mapping_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        publish_words_[5].store(
            current_instruction_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        for(std::size_t word = 0; word < sfc_state_word_count_; ++word) {
            std::uint64_t bits = 0;
            const std::size_t begin = word * 64U;
            const std::size_t end = std::min(
                begin + 64U, snapshot_sfc_steps_.size());
            for(std::size_t index = begin; index < end; ++index) {
                const SnapshotSfcStep &step = snapshot_sfc_steps_[index];
                if(step.mapping_runtime->instance
                       .debug_sfc_committed_step_active(
                           step.network_index, step.step_index))
                    bits |= std::uint64_t{1} << (index - begin);
            }
            publish_words_[header_words + word].store(
                bits, std::memory_order_relaxed);
        }
        for(const Watch &watch : watches_) {
            std::size_t remaining = watch.symbol->size;
            std::size_t source = 0;
            std::size_t word = watch.word;
            while(remaining != 0) {
                std::uint64_t bits = 0;
                const std::size_t count = std::min<std::size_t>(8, remaining);
                if(!watch.mapping->instance.read_variable(
                       watch.symbol->offset +
                           static_cast<std::uint32_t>(source),
                       static_cast<std::uint32_t>(count),
                       reinterpret_cast<unsigned char *>(&bits)))
                    break;
                publish_words_[word++].store(bits, std::memory_order_relaxed);
                source += count;
                remaining -= count;
            }
        }
        publish_words_[0].store((prior | 1U) + 1U,
                                std::memory_order_release);
        snapshot_ready_.store(true, std::memory_order_release);
    }

    bool breakpoint_set(std::uint32_t key) const noexcept
    {
        const std::size_t word = key / 64U;
        return word < breakpoint_word_count_ &&
               (breakpoint_words_[word].load(std::memory_order_acquire) &
                (std::uint64_t{1} << (key % 64U))) != 0;
    }

    void set_breakpoint(std::uint32_t key) noexcept
    {
        breakpoint_words_[key / 64U].fetch_or(
            std::uint64_t{1} << (key % 64U), std::memory_order_release);
    }

    void clear_breakpoint(std::uint32_t key) noexcept
    {
        breakpoint_words_[key / 64U].fetch_and(
            ~(std::uint64_t{1} << (key % 64U)),
            std::memory_order_release);
    }

    void push_trace(DebugEventKind kind,
                    std::uint32_t instruction = 0,
                    const InstructionId *instruction_id = nullptr,
                    const SourceMapEntry *entry = nullptr) noexcept
    {
        if(trace_words_ == nullptr || trace_storage_capacity_ == 0) return;
        const std::uint64_t prior =
            trace_lock_.load(std::memory_order_relaxed);
        trace_lock_.store(prior | 1U, std::memory_order_release);
        std::size_t size = trace_size_.load(std::memory_order_relaxed);
        std::size_t begin = trace_begin_.load(std::memory_order_relaxed);
        std::size_t slot = 0;
        if(size < trace_storage_capacity_) {
            slot = (begin + size) % trace_storage_capacity_;
            ++size;
        } else {
            slot = begin;
            begin = (begin + 1U) % trace_storage_capacity_;
            trace_dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        DebugTraceRecord record{};
        record.sequence = trace_sequence_.fetch_add(
                              1, std::memory_order_relaxed) + 1U;
        record.resource_id = trace_resource_id_;
        record.task_id = trace_task_id_;
        record.release =
            observed_release_count_.load(std::memory_order_relaxed);
        if(entry != nullptr) {
            record.pou_id = entry->pou_id;
            record.sfc_id = entry->sfc_id;
        }
        record.kind = kind;
        record.instruction = instruction;
        record.instruction_id = instruction_id == nullptr
                                    ? InstructionId{}
                                    : *instruction_id;
        for(std::size_t word = 0; word < trace_words_per_record_; ++word) {
            std::uint64_t bits = 0;
            std::memcpy(&bits,
                        reinterpret_cast<const unsigned char *>(&record) +
                            word * sizeof(bits),
                        sizeof(bits));
            trace_words_[slot * trace_words_per_record_ + word].store(
                bits, std::memory_order_relaxed);
        }
        trace_begin_.store(begin, std::memory_order_relaxed);
        trace_size_.store(size, std::memory_order_relaxed);
        trace_lock_.store((prior | 1U) + 1U, std::memory_order_release);
    }

    ConfigurationRuntime *runtime_ = nullptr;
    ConfigurationRuntime::ResourceRuntime *resource_ = nullptr;
    ConfigurationRuntime::TaskRuntime *task_ = nullptr;
    ConfigurationRuntime::MappingRuntime *mapping_ = nullptr;
    const Program *program_ = nullptr;
    std::size_t task_index_ = 0;
    std::size_t mapping_index_ = 0;
    std::atomic<std::uint64_t> *publish_words_ = nullptr;
    std::size_t publish_word_capacity_ = 0;
    DebugSnapshotEntry *publish_entries_ = nullptr;
    std::size_t publish_entry_capacity_ = 0;
    std::vector<Watch> watches_;
    std::vector<ActivePou> active_pous_;
    std::vector<SnapshotSfcStep> snapshot_sfc_steps_;
    std::size_t sfc_state_word_count_ = 0;
    std::atomic<std::uint8_t> watch_plan_state_{watch_plan_open};
    std::atomic<bool> snapshot_ready_{false};
    std::vector<std::uint32_t> breakpoints_;
    std::vector<std::uint32_t> mapping_breakpoint_bases_;
    std::atomic<std::uint64_t> *breakpoint_words_ = nullptr;
    std::size_t breakpoint_word_count_ = 0;
    std::size_t value_word_count_ = 0;
    std::size_t value_bytes_ = 0;
    std::atomic<std::uint64_t> *trace_words_ = nullptr;
    std::size_t trace_words_per_record_ = 0;
    std::size_t trace_storage_capacity_ = 0;
    mutable std::atomic<std::uint64_t> trace_lock_{0};
    std::atomic<std::size_t> trace_begin_{0};
    std::atomic<std::size_t> trace_size_{0};
    std::atomic<std::uint64_t> trace_sequence_{0};
    std::atomic<std::uint64_t> trace_dropped_{0};
    std::uint64_t trace_resource_id_ = 0;
    std::uint64_t trace_task_id_ = 0;
    DebugSessionOptions options_{};
    PublishedStop published_stop_{};
    std::atomic<bool> stop_pending_{false};
    std::atomic<std::uint8_t> command_mailbox_{0};
    std::atomic<std::uint8_t> observed_task_state_{
        static_cast<std::uint8_t>(TaskState::idle)};
    std::atomic<std::uint64_t> observed_release_count_{0};
    std::atomic<const SourceMapEntry *> current_entry_{nullptr};
    std::atomic<std::uint32_t> current_mapping_{UINT32_MAX};
    std::atomic<std::uint32_t> current_instruction_{0};
    DebugCommand step_command_ = DebugCommand::continue_;
    std::uint16_t step_origin_depth_ = 0;
    InstructionId rt_stop_instruction_id_{};
    const SourceMapEntry *rt_stop_entry_ = nullptr;
    std::uint32_t rt_stop_instruction_ = 0;
    DebugStopReason rt_stop_reason_ = DebugStopReason::breakpoint;
    std::uint16_t rt_stop_depth_ = 0;
    InstructionId skip_instruction_once_{};
    bool skip_instruction_valid_ = false;
    DebugError last_error_ = DebugError::invalid_argument;
};

} // namespace plcopen::core::st
