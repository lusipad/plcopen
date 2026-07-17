#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include "rt/error.h"
#include "st/tasking.h"
#include "st/vm.h"

namespace plcopen::core::st
{

class ConfigurationRuntime
{
    friend class DebugSession;
public:
    ConfigurationRuntime() = default;
    ~ConfigurationRuntime() { unload(); }
    ConfigurationRuntime(const ConfigurationRuntime &) = delete;
    ConfigurationRuntime &operator=(const ConfigurationRuntime &) = delete;

    rt::ErrorCode load(const Program &program, const char *configuration_name,
                       unsigned char *buffer, std::size_t buffer_bytes,
                       std::int64_t base_tick_ns)
    {
        unload();
        if(configuration_name == nullptr || buffer == nullptr ||
           base_tick_ns <= 0 ||
           (reinterpret_cast<std::uintptr_t>(buffer) & 7U) != 0) {
            return rt::ErrorCode::invalid_argument;
        }
        const ConfigurationInfo *configuration = nullptr;
        for(const ConfigurationInfo &candidate : program.configurations) {
            if(tasking_detail::same_name(candidate.lower, configuration_name)) {
                configuration = &candidate;
                break;
            }
        }
        if(configuration == nullptr ||
           configuration->base_tick_ns !=
               static_cast<std::uint64_t>(base_tick_ns)) {
            return rt::ErrorCode::invalid_argument;
        }
        TaskingReport report;
        const rt::ErrorCode reported =
            program.tasking_report(configuration_name, report);
        if(reported != rt::ErrorCode::ok) return reported;
        if(report.required_runtime_bytes > buffer_bytes ||
           report.required_runtime_bytes >
               std::numeric_limits<std::size_t>::max()) {
            return rt::ErrorCode::capacity_exceeded;
        }

        program_ = &program;
        configuration_ = configuration;
        base_tick_ns_ = base_tick_ns;
        resource_count_ = configuration->resources.size();
        resources_.reset(resource_count_ == 0
                             ? nullptr
                             : new ResourceRuntime[resource_count_]);
        unsigned char *cursor = buffer;
        unsigned char *const end = buffer + buffer_bytes;

        for(std::size_t resource_index = 0;
            resource_index < resource_count_; ++resource_index) {
            ResourceRuntime &runtime = resources_[resource_index];
            runtime.info = &configuration->resources[resource_index];
            runtime.storage = reinterpret_cast<ResourceRuntimeStorage *>(
                take(cursor, end, sizeof(ResourceRuntimeStorage)));
            if(runtime.storage == nullptr) return load_failed();
            new(runtime.storage) ResourceRuntimeStorage{};

            runtime.task_count = runtime.info->tasks.size();
            runtime.tasks.reset(runtime.task_count == 0
                                    ? nullptr
                                    : new TaskRuntime[runtime.task_count]);
            runtime.schedule.reset(runtime.task_count == 0
                                       ? nullptr
                                       : new std::uint16_t[runtime.task_count]);
            for(std::size_t task_index = 0; task_index < runtime.task_count;
                ++task_index) {
                TaskRuntime &task = runtime.tasks[task_index];
                task.info = &runtime.info->tasks[task_index];
                task.storage = reinterpret_cast<TaskRuntimeStorage *>(
                    take(cursor, end, sizeof(TaskRuntimeStorage)));
                if(task.storage == nullptr) return load_failed();
                new(task.storage) TaskRuntimeStorage{};
                runtime.schedule[task_index] =
                    static_cast<std::uint16_t>(task_index);
            }
            if(runtime.task_count > 1) {
                std::sort(runtime.schedule.get(),
                          runtime.schedule.get() + runtime.task_count,
                          [&](std::uint16_t left, std::uint16_t right) {
                          const TaskInfo &a = runtime.info->tasks[left];
                          const TaskInfo &b = runtime.info->tasks[right];
                          return a.priority != b.priority
                                     ? a.priority < b.priority
                                     : a.declaration_order <
                                           b.declaration_order;
                          });
            }

            runtime.mapping_count = runtime.info->mappings.size();
            runtime.mappings.reset(runtime.mapping_count == 0
                                       ? nullptr
                                       : new MappingRuntime[
                                             runtime.mapping_count]);
            for(std::size_t mapping_index = 0;
                mapping_index < runtime.mapping_count; ++mapping_index) {
                MappingRuntime &mapping = runtime.mappings[mapping_index];
                mapping.info = &runtime.info->mappings[mapping_index];
                mapping.program = find_program(mapping.info->program);
                if(mapping.program == nullptr) return load_failed();
                mapping.bytes = mapping.program->required_bytes();
                mapping.buffer = take(cursor, end, mapping.bytes);
                if(mapping.buffer == nullptr) return load_failed();
            }

            if(!build_image_program(runtime)) return load_failed();
            const std::size_t image_bytes =
                static_cast<std::size_t>(
                    runtime.image_program.process_image.input_bytes) +
                runtime.image_program.process_image.output_bytes +
                runtime.image_program.process_image.memory_bytes;
            unsigned char *image_buffer = take(cursor, end, image_bytes);
            if(image_buffer == nullptr ||
               runtime.image.load(runtime.image_program, image_buffer,
                                  image_bytes) != rt::ErrorCode::ok) {
                return load_failed();
            }
            for(std::size_t task_index = 0; task_index < runtime.task_count;
                ++task_index) {
                TaskRuntime &task = runtime.tasks[task_index];
                task.parked_output_bytes =
                    runtime.image_program.process_image.output_bytes;
                task.parked_memory_bytes =
                    runtime.image_program.process_image.memory_bytes;
                task.parked_output = take(cursor, end,
                                          task.parked_output_bytes);
                task.parked_output_dirty = take(cursor, end,
                                                task.parked_output_bytes);
                task.parked_memory = take(cursor, end,
                                          task.parked_memory_bytes);
                task.parked_memory_dirty = take(cursor, end,
                                                task.parked_memory_bytes);
                if((task.parked_output_bytes != 0 &&
                    (task.parked_output == nullptr ||
                     task.parked_output_dirty == nullptr)) ||
                   (task.parked_memory_bytes != 0 &&
                    (task.parked_memory == nullptr ||
                     task.parked_memory_dirty == nullptr)))
                    return load_failed();
            }
            runtime.write_stamps.assign(
                (runtime.image_program.process_image.output_bytes +
                 runtime.image_program.process_image.memory_bytes) * 8U,
                0);
            runtime.write_owners.assign(runtime.write_stamps.size(), 0);

            for(std::size_t mapping_index = 0;
                mapping_index < runtime.mapping_count; ++mapping_index) {
                MappingRuntime &mapping = runtime.mappings[mapping_index];
                const std::int64_t period = task_period(runtime, mapping);
                if(period <= 0 ||
                   mapping.instance.load(*mapping.program, mapping.buffer,
                                         mapping.bytes, period,
                                         &runtime.image) !=
                       rt::ErrorCode::ok) {
                    return load_failed();
                }
                mapping.instance.defer_process_image_publish(true);
            }
        }
        loaded_ = true;
        return rt::ErrorCode::ok;
    }

    void unload() noexcept
    {
        if(debug_hooks_ != nullptr) debug_hooks_->on_runtime_detach();
        resources_.reset();
        resource_count_ = 0;
        program_ = nullptr;
        configuration_ = nullptr;
        base_tick_ns_ = 0;
        loaded_ = false;
        have_boundary_ = false;
        last_boundary_ = 0;
        debug_hooks_ = nullptr;
    }

    rt::ErrorCode boundary(std::uint64_t tick,
                           const EventSample *events = nullptr,
                           std::size_t event_count = 0)
    {
        if(!loaded_ || (event_count != 0 && events == nullptr) ||
           (have_boundary_ && tick <= last_boundary_)) {
            return rt::ErrorCode::invalid_argument;
        }
        have_boundary_ = true;
        last_boundary_ = tick;
        for(std::size_t resource_index = 0;
            resource_index < resource_count_; ++resource_index) {
            ResourceRuntime &resource = resources_[resource_index];
            resource.storage->boundary_tick = tick;
            if(resource.storage->pending_fault != ResourceFault::none) {
                resource.storage->status.fault =
                    resource.storage->pending_fault;
                resource.storage->pending_fault = ResourceFault::none;
                ++resource.storage->status.fault_count;
                abort_resource(resource);
            }
            for(std::size_t task_index = 0;
                task_index < resource.task_count; ++task_index) {
                apply_pending(resource, task_index, tick);
            }
            if(resource.storage->status.fault != ResourceFault::none) continue;
            for(std::size_t task_index = 0;
                task_index < resource.task_count; ++task_index) {
                release(resource, task_index, tick, events, event_count);
            }
        }
        if(debug_hooks_ != nullptr) debug_hooks_->on_scheduler_boundary();
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode run(std::int64_t slice)
    {
        if(!loaded_ || slice < 0) return rt::ErrorCode::invalid_argument;
        while(slice > 0) {
            if(debug_hooks_ != nullptr)
                debug_hooks_->on_scheduler_boundary();
            ResourceRuntime *resource = nullptr;
            std::size_t task_index = 0;
            if(!next_runnable(resource, task_index)) break;
            TaskRuntime &task = resource->tasks[task_index];
            if(task.storage->mapping_cursor >= task.info->mappings.size()) {
                complete_task(*resource, task_index);
                continue;
            }
            if(!task.storage->transaction_active) {
                if(resource->transaction_owner != invalid_task_owner &&
                   resource->transaction_owner != task_index) {
                    return rt::ErrorCode::invalid_argument;
                }
                const std::uint64_t force_release =
                    task.storage->status.release_count;
                resource->image.begin_transaction(
                    static_cast<std::uint32_t>(task_index), force_release);
                resource->transaction_owner = task_index;
                task.storage->transaction_active = true;
                if(task.parked) {
                    if(!resource->image.restore_transaction(
                           task.parked_output,
                           task.parked_output_dirty,
                           task.parked_output_bytes,
                           task.parked_memory,
                           task.parked_memory_dirty,
                           task.parked_memory_bytes)) {
                        fault_task(*resource, task_index,
                                   TaskFault::task_runtime_fault, nullptr);
                        continue;
                    }
                    task.parked = false;
                }
            }
            const std::uint16_t mapping_index =
                task.info->mappings[task.storage->mapping_cursor];
            if(mapping_index >= resource->mapping_count) {
                fault_task(*resource, task_index, TaskFault::task_runtime_fault,
                           nullptr);
                continue;
            }
            MappingRuntime &mapping = resource->mappings[mapping_index];
            if(!mapping.instance.running()) {
                const ScanError started =
                    mapping.instance.begin(task.storage->status.remaining_budget);
                if(started != ScanError::ok) {
                    fault_task(*resource, task_index,
                               started == ScanError::budget_exceeded
                                   ? TaskFault::task_budget_exceeded
                                   : TaskFault::task_runtime_fault,
                               &mapping);
                    continue;
                }
            }
            std::int64_t executed = 0;
            const ScanError result = mapping.instance.resume(slice, executed);
            slice -= executed;
            task.storage->status.remaining_budget =
                mapping.instance.remaining_budget();
            if(result != ScanError::ok) {
                if(result == ScanError::paused) {
                    task.parked = resource->image.park_transaction(
                        task.parked_output, task.parked_output_dirty,
                        task.parked_output_bytes, task.parked_memory,
                        task.parked_memory_dirty, task.parked_memory_bytes);
                    task.storage->status.state = TaskState::paused;
                    discard_transaction(*resource, task_index);
                    if(debug_hooks_ != nullptr)
                        debug_hooks_->on_debug_event(
                            resource, task_index, DebugEventKind::task,
                            mapping.instance.instruction_offset());
                    continue;
                }
                fault_task(*resource, task_index,
                           result == ScanError::budget_exceeded
                               ? TaskFault::task_budget_exceeded
                               : TaskFault::task_runtime_fault,
                           &mapping);
                continue;
            }
            if(mapping.instance.complete()) {
                ++task.storage->mapping_cursor;
                if(task.storage->mapping_cursor >= task.info->mappings.size())
                    complete_task(*resource, task_index);
            }
            if(executed == 0) break;
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode task_status(const char *resource_name,
                              const char *task_name,
                              TaskStatus &status) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        if(resource == nullptr) return rt::ErrorCode::invalid_argument;
        const int task = find_task(*resource, task_name);
        if(task < 0) return rt::ErrorCode::invalid_argument;
        status = resource->tasks[static_cast<std::size_t>(task)].storage->status;
        return rt::ErrorCode::ok;
    }

    std::string_view artifact_pou_name(std::uint32_t index) const noexcept
    {
        return program_ == nullptr ? std::string_view{}
                                   : program_->artifact_pou_name(index);
    }

    std::string_view artifact_sfc_name(std::uint32_t pou_index,
                                       std::uint32_t sfc_index) const noexcept
    {
        return program_ == nullptr
                   ? std::string_view{}
                   : program_->artifact_sfc_name(pou_index, sfc_index);
    }

    std::string_view artifact_sfc_action_name(
        std::uint32_t pou_index, std::uint32_t sfc_index,
        std::uint32_t action_index) const noexcept
    {
        return program_ == nullptr
                   ? std::string_view{}
                   : program_->artifact_sfc_action_name(
                         pou_index, sfc_index, action_index);
    }

    rt::ErrorCode resource_status(const char *resource_name,
                                  ResourceStatus &status) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        if(resource == nullptr) return rt::ErrorCode::invalid_argument;
        status = resource->storage->status;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode value_i64(const char *resource_name,
                            const char *instance_name, const char *name,
                            std::int64_t &value) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        if(resource == nullptr || instance_name == nullptr || name == nullptr)
            return rt::ErrorCode::invalid_argument;
        for(std::size_t index = 0; index < resource->mapping_count; ++index) {
            const MappingRuntime &mapping = resource->mappings[index];
            if(!tasking_detail::same_name(mapping.info->lower,
                                          instance_name))
                continue;
            const int variable = mapping.instance.find(name);
            if(variable < 0) return rt::ErrorCode::invalid_argument;
            value = mapping.instance.value_i64(
                static_cast<std::size_t>(variable));
            return rt::ErrorCode::ok;
        }
        return rt::ErrorCode::invalid_argument;
    }

    rt::ErrorCode output_snapshot(const char *resource_name,
                                  unsigned char *bytes,
                                  std::size_t capacity,
                                  std::size_t &written,
                                  std::uint64_t &version) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        return resource == nullptr
                   ? rt::ErrorCode::invalid_argument
                   : resource->image.output_snapshot(bytes, capacity, written,
                                                     version);
    }

    rt::ErrorCode memory_snapshot(const char *resource_name,
                                  unsigned char *bytes,
                                  std::size_t capacity,
                                  std::size_t &written,
                                  std::uint64_t &version) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        return resource == nullptr
                   ? rt::ErrorCode::invalid_argument
                   : resource->image.memory_snapshot(bytes, capacity, written,
                                                     version);
    }

    rt::ErrorCode submit_input(const char *resource_name,
                               const unsigned char *bytes,
                               std::size_t size, std::uint64_t version)
    {
        ResourceRuntime *resource = find_resource(resource_name);
        return resource == nullptr
                   ? rt::ErrorCode::invalid_argument
                   : resource->image.submit_input(bytes, size, version);
    }

    rt::ErrorCode report_wallclock_exceeded(const char *resource_name,
                                             const char *task_name)
    {
        ResourceRuntime *resource = find_resource(resource_name);
        const int task = resource == nullptr ? -1 : find_task(*resource,
                                                              task_name);
        if(task < 0) return rt::ErrorCode::invalid_argument;
        resource->tasks[static_cast<std::size_t>(task)]
            .storage->pending_wallclock_fault = true;
        return rt::ErrorCode::ok;
    }

    std::uint64_t force_queue_version(const char *resource_name) const
    {
        const ResourceRuntime *resource = find_resource(resource_name);
        return resource == nullptr ? 0 : resource->image.force_queue_version();
    }

    rt::ErrorCode reset_task(const char *resource_name, const char *task_name)
    {
        return queue_task_action(resource_name, task_name, false);
    }

    rt::ErrorCode restart_task(const char *resource_name,
                               const char *task_name)
    {
        return queue_task_action(resource_name, task_name, true);
    }

    rt::ErrorCode report_resource_fault(const char *resource_name,
                                        ResourceFault fault)
    {
        ResourceRuntime *resource = find_resource(resource_name);
        if(resource == nullptr || fault == ResourceFault::none)
            return rt::ErrorCode::invalid_argument;
        resource->storage->pending_fault = fault;
        return rt::ErrorCode::ok;
    }

private:
    struct MappingRuntime
    {
        const ProgramMappingInfo *info = nullptr;
        const Program *program = nullptr;
        Instance instance;
        unsigned char *buffer = nullptr;
        std::size_t bytes = 0;
    };

    struct TaskRuntime
    {
        const TaskInfo *info = nullptr;
        TaskRuntimeStorage *storage = nullptr;
        unsigned char *parked_output = nullptr;
        unsigned char *parked_output_dirty = nullptr;
        unsigned char *parked_memory = nullptr;
        unsigned char *parked_memory_dirty = nullptr;
        std::size_t parked_output_bytes = 0;
        std::size_t parked_memory_bytes = 0;
        bool parked = false;
    };

    struct ResourceRuntime
    {
        const ResourceInfo *info = nullptr;
        ResourceRuntimeStorage *storage = nullptr;
        Program image_program;
        ProcessImage image;
        std::unique_ptr<TaskRuntime[]> tasks;
        std::unique_ptr<MappingRuntime[]> mappings;
        std::unique_ptr<std::uint16_t[]> schedule;
        std::size_t task_count = 0;
        std::size_t mapping_count = 0;
        std::vector<std::uint64_t> write_stamps;
        std::vector<std::uint16_t> write_owners;
        std::size_t transaction_owner = invalid_task_owner;
    };

    static constexpr std::size_t invalid_task_owner =
        std::numeric_limits<std::size_t>::max();

    static unsigned char *take(unsigned char *&cursor,
                               unsigned char *end, std::size_t bytes)
    {
        const std::size_t aligned =
            static_cast<std::size_t>(tasking_detail::align_runtime(bytes));
        if(cursor > end || aligned > static_cast<std::size_t>(end - cursor))
            return nullptr;
        unsigned char *result = cursor;
        cursor += aligned;
        return result;
    }

    rt::ErrorCode load_failed()
    {
        unload();
        return rt::ErrorCode::invalid_argument;
    }

    const Program *find_program(const std::string &lower) const
    {
        for(const Program &program : program_->programs)
            if(program.program_name == lower) return &program;
        return nullptr;
    }

    static bool same_location(const LocatedVarInfo &left,
                              const LocatedVarInfo &right)
    {
        return left.area == right.area &&
               left.byte_offset == right.byte_offset &&
               left.bit == right.bit &&
               left.bit_address == right.bit_address &&
               left.type_id == right.type_id &&
               left.byte_width == right.byte_width;
    }

    static std::uint64_t physical_begin(const LocatedVarInfo &var)
    {
        return static_cast<std::uint64_t>(var.byte_offset) * 8U +
               (var.bit_address ? var.bit : 0U);
    }

    static std::uint64_t physical_end(const LocatedVarInfo &var)
    {
        return physical_begin(var) +
               (var.bit_address ? 1U
                                : static_cast<std::uint64_t>(var.byte_width) *
                                      8U);
    }

    static bool overlaps(const LocatedVarInfo &left,
                         const LocatedVarInfo &right)
    {
        return left.area == right.area &&
               physical_begin(left) < physical_end(right) &&
               physical_begin(right) < physical_end(left);
    }

    static bool compatible_input_alias(const LocatedVarInfo &left,
                                       const LocatedVarInfo &right)
    {
        if(left.area != ProcessArea::input ||
           right.area != ProcessArea::input || !overlaps(left, right)) {
            return false;
        }
        const std::uint64_t left_begin = physical_begin(left);
        const std::uint64_t left_end = physical_end(left);
        const std::uint64_t right_begin = physical_begin(right);
        const std::uint64_t right_end = physical_end(right);
        if(left_begin == right_begin && left_end == right_end) return true;
        if(left.bit_address && !right.bit_address)
            return left_begin >= right_begin && left_begin < right_end;
        if(!left.bit_address && right.bit_address)
            return right_begin >= left_begin && right_begin < left_end;
        return false;
    }

    static bool same_initial_value(const Program &program,
                                   const LocatedVarInfo &var,
                                   const Program &merged_program,
                                   const LocatedVarInfo &merged)
    {
        const std::size_t source_end =
            static_cast<std::size_t>(var.var_offset) + var.byte_width;
        const std::size_t merged_end =
            static_cast<std::size_t>(merged.var_offset) + merged.byte_width;
        return source_end <= program.initial_data.size() &&
               merged_end <= merged_program.initial_data.size() &&
               std::memcmp(program.initial_data.data() + var.var_offset,
                           merged_program.initial_data.data() +
                               merged.var_offset,
                           var.byte_width) == 0;
    }

    bool build_image_program(ResourceRuntime &runtime)
    {
        ProcessImageInfo &image = runtime.image_program.process_image;
        image.max_force_entries = 0;
        for(std::size_t index = 0; index < runtime.mapping_count; ++index) {
            const Program &program = *runtime.mappings[index].program;
            image.input_bytes = std::max(image.input_bytes,
                                         program.process_image.input_bytes);
            image.output_bytes = std::max(image.output_bytes,
                                          program.process_image.output_bytes);
            image.memory_bytes = std::max(image.memory_bytes,
                                          program.process_image.memory_bytes);
            image.max_force_entries = std::max(
                image.max_force_entries,
                program.process_image.max_force_entries);
            image.fingerprint ^= program.process_image.fingerprint;
            const std::size_t prior_count = image.variables.size();
            for(const LocatedVarInfo &var : program.process_image.variables) {
                bool found = false;
                for(std::size_t prior = 0; prior < prior_count; ++prior) {
                    const LocatedVarInfo &existing = image.variables[prior];
                    if(!overlaps(existing, var)) continue;
                    if(var.area == ProcessArea::input) {
                        if(!compatible_input_alias(existing, var)) return false;
                        if(same_location(existing, var)) {
                            found = true;
                            break;
                        }
                        continue;
                    }
                    const bool same_shared_logical =
                        existing.shared && var.shared &&
                        existing.stable_id == var.stable_id;
                    if(!same_shared_logical ||
                       !same_location(existing, var) ||
                       !same_initial_value(program, var,
                                           runtime.image_program, existing)) {
                        return false;
                    }
                    found = true;
                    break;
                }
                if(found) continue;
                if(runtime.image_program.initial_data.size() >
                   std::numeric_limits<std::uint32_t>::max())
                    return false;
                LocatedVarInfo merged = var;
                merged.var_offset = static_cast<std::uint32_t>(
                    runtime.image_program.initial_data.size());
                image.variables.push_back(merged);
                const std::size_t required =
                    static_cast<std::size_t>(var.var_offset) + var.byte_width;
                if(required > program.initial_data.size()) return false;
                runtime.image_program.initial_data.insert(
                    runtime.image_program.initial_data.end(),
                    program.initial_data.begin() + var.var_offset,
                    program.initial_data.begin() + required);
            }
        }
        return true;
    }

    std::int64_t task_period(const ResourceRuntime &resource,
                             const MappingRuntime &mapping) const
    {
        for(const TaskInfo &task : resource.info->tasks) {
            if(task.lower != mapping.info->task) continue;
            if(task.kind == TaskKind::event) return base_tick_ns_;
            if(task.interval_ticks >
               static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max() /
                   base_tick_ns_)) {
                return 0;
            }
            return static_cast<std::int64_t>(task.interval_ticks) *
                   base_tick_ns_;
        }
        return 0;
    }

    void apply_pending(ResourceRuntime &resource, std::size_t task_index,
                       std::uint64_t tick)
    {
        TaskRuntime &task = resource.tasks[task_index];
        TaskRuntimeStorage &storage = *task.storage;
        if(storage.pending_wallclock_fault) {
            storage.pending_wallclock_fault = false;
            if(storage.status.state == TaskState::running)
                fault_task(resource, task_index,
                           TaskFault::task_wallclock_exceeded, nullptr);
        }
        if(!storage.pending_reset && !storage.pending_restart) return;
        const bool restart = storage.pending_restart;
        storage.pending_reset = false;
        storage.pending_restart = false;
        const std::uint64_t releases = storage.status.release_count;
        const std::uint64_t completed = storage.status.completed_count;
        const std::uint64_t faults = storage.status.fault_count;
        discard_transaction(resource, task_index);
        for(const std::uint16_t mapping_index : task.info->mappings) {
            if(mapping_index >= resource.mapping_count) continue;
            MappingRuntime &mapping = resource.mappings[mapping_index];
            if(restart) {
                const std::int64_t period = task_period(resource, mapping);
                (void)mapping.instance.load(*mapping.program, mapping.buffer,
                                            mapping.bytes, period,
                                            &resource.image);
                mapping.instance.defer_process_image_publish(true);
                mapping.instance.set_debug_hooks(debug_hooks_, mapping_index);
            } else {
                mapping.instance.reset();
            }
        }
        storage = TaskRuntimeStorage{};
        task.parked = false;
        storage.status.release_count = releases;
        storage.status.completed_count = completed;
        storage.status.fault_count = faults;
        storage.eligible_tick = tick == std::numeric_limits<std::uint64_t>::max()
                                    ? tick
                                    : tick + 1;
    }

    void release(ResourceRuntime &resource, std::size_t task_index,
                 std::uint64_t tick, const EventSample *events,
                 std::size_t event_count)
    {
        TaskRuntime &task = resource.tasks[task_index];
        TaskRuntimeStorage &storage = *task.storage;
        bool due = false;
        if(task.info->kind == TaskKind::periodic) {
            due = tick >= task.info->phase_ticks &&
                  (tick - task.info->phase_ticks) %
                          task.info->interval_ticks ==
                      0;
        } else {
            bool level = false;
            for(std::size_t index = 0; index < event_count; ++index) {
                if(tasking_detail::same_name(resource.info->lower,
                                             events[index].resource) &&
                   tasking_detail::same_name(task.info->event,
                                             events[index].event)) {
                    level = events[index].level;
                }
            }
            due = level && !storage.last_event_level;
            storage.last_event_level = level;
        }
        if(!due || storage.status.state == TaskState::faulted ||
           storage.status.state == TaskState::paused ||
           tick < storage.eligible_tick) {
            return;
        }
        if(storage.status.state == TaskState::running) {
            ++storage.status.missed_release_count;
            return;
        }
        storage.status.state = TaskState::running;
        storage.status.remaining_budget = task.info->instruction_budget;
        ++storage.status.release_count;
        storage.release_tick = tick;
        storage.mapping_cursor = 0;
        if(task.info->mappings.empty()) complete_task(resource, task_index);
    }

    bool next_runnable(ResourceRuntime *&resource, std::size_t &task_index)
    {
        for(std::size_t resource_at = 0; resource_at < resource_count_;
            ++resource_at) {
            ResourceRuntime &candidate = resources_[resource_at];
            if(candidate.storage->status.fault != ResourceFault::none) continue;
            if(candidate.transaction_owner != invalid_task_owner) {
                const std::size_t owner = candidate.transaction_owner;
                if(owner < candidate.task_count &&
                   candidate.tasks[owner].storage->status.state ==
                       TaskState::running) {
                    resource = &candidate;
                    task_index = owner;
                    return true;
                }
                candidate.image.discard_scan();
                candidate.transaction_owner = invalid_task_owner;
            }
            for(std::size_t order = 0; order < candidate.task_count; ++order) {
                const std::size_t index = candidate.schedule[order];
                if(candidate.tasks[index].storage->status.state ==
                   TaskState::running) {
                    resource = &candidate;
                    task_index = index;
                    return true;
                }
            }
        }
        return false;
    }

    void complete_task(ResourceRuntime &resource, std::size_t task_index)
    {
        TaskRuntime &task = resource.tasks[task_index];
        if(task.storage->transaction_active) {
            record_writes(resource, task_index, task.storage->release_tick);
            resource.image.commit_transaction();
            resource.transaction_owner = invalid_task_owner;
            task.storage->transaction_active = false;
        }
        task.storage->status.state = TaskState::idle;
        task.storage->status.remaining_budget = 0;
        ++task.storage->status.completed_count;
        task.parked = false;
        if(debug_hooks_ != nullptr) {
            debug_hooks_->on_debug_event(&resource, task_index,
                                         DebugEventKind::scan);
            debug_hooks_->on_task_boundary(&resource, task_index, false);
        }
    }

    void fault_task(ResourceRuntime &resource, std::size_t task_index,
                    TaskFault fault, MappingRuntime *mapping)
    {
        TaskRuntimeStorage &storage = *resource.tasks[task_index].storage;
        storage.status.state = TaskState::faulted;
        storage.status.fault = fault;
        ++storage.status.fault_count;
        discard_transaction(resource, task_index);
        resource.tasks[task_index].parked = false;
        if(mapping == nullptr && storage.mapping_cursor <
                                    resource.tasks[task_index]
                                        .info->mappings.size()) {
            const std::uint16_t current = resource.tasks[task_index]
                                              .info->mappings[
                                                  storage.mapping_cursor];
            if(current < resource.mapping_count)
                mapping = &resource.mappings[current];
        }
        if(mapping != nullptr) {
            for(std::size_t index = 0; index < program_->pous.size(); ++index) {
                if(program_->pous[index].lower ==
                   mapping->program->program_name) {
                    storage.status.fault_pou_index =
                        static_cast<std::uint32_t>(index);
                    break;
                }
            }
            storage.status.fault_instruction =
                mapping->instance.instruction_offset();
            storage.status.fault_sfc_index =
                mapping->instance.fault_sfc_network_index();
            storage.status.fault_element_kind =
                mapping->instance.fault_sfc_element_kind();
            if(storage.status.fault_element_kind ==
               TaskFaultElementKind::transition) {
                storage.status.fault_transition_index =
                    mapping->instance.fault_sfc_element_index();
            } else if(storage.status.fault_element_kind ==
                      TaskFaultElementKind::action) {
                storage.status.fault_action_index =
                    mapping->instance.fault_sfc_element_index();
            }
        }
        if(debug_hooks_ != nullptr) {
            debug_hooks_->on_debug_event(&resource, task_index,
                                         DebugEventKind::scan,
                                         storage.status.fault_instruction);
            debug_hooks_->on_debug_event(&resource, task_index,
                                         DebugEventKind::fault,
                                         storage.status.fault_instruction,
                                         mapping == nullptr
                                             ? nullptr
                                             : mapping->instance
                                                   .last_debug_entry(),
                                         mapping == nullptr
                                             ? nullptr
                                             : &mapping->instance
                                                   .last_debug_instruction_id());
            debug_hooks_->on_task_boundary(&resource, task_index, true);
        }
        for(const std::uint16_t mapping_index :
            resource.tasks[task_index].info->mappings) {
            if(mapping_index < resource.mapping_count)
                resource.mappings[mapping_index].instance.abort();
        }
    }

    void abort_resource(ResourceRuntime &resource)
    {
        if(resource.transaction_owner != invalid_task_owner) {
            resource.image.discard_scan();
            resource.transaction_owner = invalid_task_owner;
        }
        for(std::size_t task_index = 0; task_index < resource.task_count;
            ++task_index) {
            TaskRuntime &task = resource.tasks[task_index];
            for(const std::uint16_t mapping_index : task.info->mappings)
                if(mapping_index < resource.mapping_count)
                    resource.mappings[mapping_index].instance.abort();
            if(task.storage->status.state == TaskState::running)
                task.storage->status.state = TaskState::idle;
            task.storage->transaction_active = false;
        }
    }

    void discard_transaction(ResourceRuntime &resource,
                             std::size_t task_index)
    {
        TaskRuntimeStorage &storage = *resource.tasks[task_index].storage;
        if(resource.transaction_owner == task_index) {
            resource.image.discard_scan();
            resource.transaction_owner = invalid_task_owner;
        }
        storage.transaction_active = false;
    }

    void record_writes(ResourceRuntime &resource, std::size_t task_index,
                       std::uint64_t tick)
    {
        const std::uint64_t stamp = tick ==
                                            std::numeric_limits<
                                                std::uint64_t>::max()
                                        ? tick
                                        : tick + 1;
        const std::uint16_t owner = static_cast<std::uint16_t>(task_index + 1);
        bool conflict = false;
        const std::size_t output_bytes =
            resource.image_program.process_image.output_bytes;
        const std::size_t memory_bytes =
            resource.image_program.process_image.memory_bytes;
        for(const ProcessArea area : {ProcessArea::output,
                                      ProcessArea::memory}) {
            const std::size_t bytes = area == ProcessArea::output
                                          ? output_bytes
                                          : memory_bytes;
            const std::size_t base =
                area == ProcessArea::output ? 0 : output_bytes;
            for(std::size_t byte = 0; byte < bytes; ++byte) {
                const std::uint8_t mask =
                    resource.image.transaction_dirty(area, byte);
                for(unsigned bit = 0; bit < 8; ++bit) {
                    if((mask & (1U << bit)) == 0) continue;
                    const std::size_t slot = (base + byte) * 8U + bit;
                    if(resource.write_stamps[slot] == stamp &&
                       resource.write_owners[slot] != owner)
                        conflict = true;
                    resource.write_stamps[slot] = stamp;
                    resource.write_owners[slot] = owner;
                }
            }
        }
        if(conflict) ++resource.storage->status.write_conflict_count;
    }

    const ResourceRuntime *find_resource(const char *name) const
    {
        if(name == nullptr) return nullptr;
        for(std::size_t index = 0; index < resource_count_; ++index)
            if(tasking_detail::same_name(resources_[index].info->lower, name))
                return &resources_[index];
        return nullptr;
    }

    ResourceRuntime *find_resource(const char *name)
    {
        return const_cast<ResourceRuntime *>(
            static_cast<const ConfigurationRuntime *>(this)->find_resource(
                name));
    }

    static int find_task(const ResourceRuntime &resource, const char *name)
    {
        if(name == nullptr) return -1;
        for(std::size_t index = 0; index < resource.task_count; ++index)
            if(tasking_detail::same_name(resource.tasks[index].info->lower,
                                         name))
                return static_cast<int>(index);
        return -1;
    }

    rt::ErrorCode queue_task_action(const char *resource_name,
                                    const char *task_name, bool restart)
    {
        ResourceRuntime *resource = find_resource(resource_name);
        const int task = resource == nullptr ? -1 : find_task(*resource,
                                                              task_name);
        if(task < 0) return rt::ErrorCode::invalid_argument;
        TaskRuntimeStorage &storage =
            *resource->tasks[static_cast<std::size_t>(task)].storage;
        if(restart)
            storage.pending_restart = true;
        else
            storage.pending_reset = true;
        return rt::ErrorCode::ok;
    }

    const Program *program_ = nullptr;
    RuntimeDebugHooks *debug_hooks_ = nullptr;
    const ConfigurationInfo *configuration_ = nullptr;
    std::unique_ptr<ResourceRuntime[]> resources_;
    std::size_t resource_count_ = 0;
    std::int64_t base_tick_ns_ = 0;
    bool loaded_ = false;
    bool have_boundary_ = false;
    std::uint64_t last_boundary_ = 0;
};

} // namespace plcopen::core::st
