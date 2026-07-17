#pragma once

// RT-SAFE: ST bytecode interpreter (approved st-l0-semantics 3.1-3.7).
// scan() is cycle-path code: zero allocation, no exceptions, bounded by an
// instruction-count budget (deterministic watchdog, matrix 3.6). All memory
// lives in the caller-provided buffer laid out at load (matrix 3.2). Fault
// semantics per matrix 3.7: the scan stops at the faulting instruction,
// prior assignments stay, the fault latches until reset().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include "rt/error.h"
#include "st/binding.h"
#include "st/binding_storage.h"
#include "st/bind.h"
#include "st/bytecode.h"
#include "st/process_image.h"
#include "st/types.h"

namespace plcopen::core::axis
{
class AxisModel;
class AxisGroup;
}

namespace plcopen::core::st
{

enum class ScanError : std::uint8_t
{
    ok = 0,
    not_loaded,
    division_by_zero,
    for_step_zero,
    budget_exceeded,
    invalid_bytecode,
    conversion_invalid, // NaN/Inf into an integer domain (L1a 4.3/5.1)
    range_violation, // enum/subrange/string/index validation (L1b+)
    string_capacity_exceeded,
    date_time_range_violation,
    alias_violation,
    invalid_argument,
    paused,
};

constexpr const char *to_string(ScanError error)
{
    switch(error) {
    case ScanError::ok: return "ok";
    case ScanError::not_loaded: return "not_loaded";
    case ScanError::division_by_zero: return "division_by_zero";
    case ScanError::for_step_zero: return "for_step_zero";
    case ScanError::budget_exceeded: return "budget_exceeded";
    case ScanError::invalid_bytecode: return "invalid_bytecode";
    case ScanError::conversion_invalid: return "conversion_invalid";
    case ScanError::range_violation: return "range_violation";
    case ScanError::string_capacity_exceeded: return "string_capacity_exceeded";
    case ScanError::date_time_range_violation: return "date_time_range_violation";
    case ScanError::alias_violation: return "alias_violation";
    case ScanError::invalid_argument: return "invalid_argument";
    case ScanError::paused: return "paused";
    }
    return "unknown";
}

enum class InstanceState : std::uint8_t
{
    idle = 0,
    running,
    complete,
    faulted,
};

class Instance
{
public:
    Instance() = default;
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&) = delete;
    Instance &operator=(Instance &&) = delete;

    ~Instance() { unload(); }

    rt::ErrorCode inject_utc_dt(std::int64_t value) noexcept
    {
        utc_dt_ns_ = value;
        return rt::ErrorCode::ok;
    }
    std::int64_t utc_dt_ns() const noexcept { return utc_dt_ns_; }

    void unload() noexcept
    {
        if(program_ != nullptr && fb_area_ != nullptr) {
            for(const FbInfo &fb : program_->fbs) {
                fb_destroy(fb.type, fb_area_ + fb.offset);
            }
        }
        program_ = nullptr;
        process_image_ = nullptr;
        vars_ = nullptr;
        visible_vars_ = nullptr;
        execution_vars_ = nullptr;
        sfc_runtime_ = nullptr;
        sfc_runner_ = nullptr;
        fb_area_ = nullptr;
        stack_ = nullptr;
        fault_ = ScanError::ok;
        execution_state_ = InstanceState::idle;
        pc_ = 0;
        instruction_pc_ = 0;
        sp_ = 0;
        remaining_budget_ = 0;
        scan_started_ = false;
        defer_process_image_publish_ = false;
        axis_targets_ = nullptr;
        group_targets_ = nullptr;
        path_tables_ = nullptr;
        path_descriptions_ = nullptr;
        cam_switch_tables_ = nullptr;
        cam_switch_outputs_ = nullptr;
        cam_track_options_ = nullptr;
        cam_tables_ = nullptr;
        position_profiles_ = nullptr;
        velocity_profiles_ = nullptr;
        acceleration_profiles_ = nullptr;
        kin_transforms_ = nullptr;
        task_period_ns_ = 0;
        debug_hooks_ = nullptr;
        debug_mapping_ = 0;
        last_debug_entry_ = nullptr;
        last_debug_instruction_id_ = {};
    }

    // Load domain. The program object must outlive the instance; the buffer
    // is caller-owned static storage, 8-byte aligned, at least
    // program.required_bytes() long.
    rt::ErrorCode load(const Program &program, unsigned char *buffer,
                       std::size_t buffer_bytes, std::int64_t task_period_ns)
    {
        unload();
        if(program.format_version != kBytecodeFormatVersion) {
            return rt::ErrorCode::bytecode_version_mismatch;
        }
        if(buffer == nullptr || task_period_ns <= 0) {
            return rt::ErrorCode::invalid_argument;
        }
        if((reinterpret_cast<std::uintptr_t>(buffer) & 7U) != 0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(buffer_bytes < program.required_bytes()) {
            return rt::ErrorCode::capacity_exceeded;
        }
        vars_ = buffer;
        visible_vars_ = vars_;
        execution_vars_ = vars_;
        fb_area_ = buffer + program.vars_bytes;
        stack_ = reinterpret_cast<std::uint64_t *>(fb_area_ + program.fb_bytes);
        std::memset(buffer, 0, program.required_bytes());
        unsigned char *binding_storage =
            buffer + program.binding_storage_offset();
        load_binding_storage(program, binding_storage);
        sfc_runner_ = ::new(static_cast<void *>(
            buffer + program.sfc_runner_storage_offset())) SfcRunnerStorage{};
        sfc_runtime_ = buffer + program.sfc_storage_offset();
        if(program.initial_data.size() > program.vars_bytes) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!program.initial_data.empty()) {
            std::memcpy(vars_, program.initial_data.data(),
                        program.initial_data.size());
        }
        for(const VarInfo &var : program.vars) {
            const TypeDesc *desc = program.types.get(var.type_id);
            if(desc == nullptr ||
               (desc->kind != TypeKind::string &&
                desc->kind != TypeKind::wstring)) {
                continue;
            }
            if(var.offset > program.vars_bytes ||
               desc->size > program.vars_bytes - var.offset ||
               !valid_string_object(vars_ + var.offset, *desc)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(const FbInfo &fb : program.fbs) {
            fb_init(fb.type, fb_area_ + fb.offset, task_period_ns);
        }
        task_period_ns_ = task_period_ns;
        program_ = &program;
        initialize_sfc_runtime();
        fault_ = ScanError::ok;
        execution_state_ = InstanceState::idle;
        pc_ = 0;
        instruction_pc_ = 0;
        sp_ = 0;
        remaining_budget_ = 0;
        scan_started_ = false;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode load(const Program &program, unsigned char *buffer,
                       std::size_t buffer_bytes, std::int64_t task_period_ns,
                       ProcessImage *process_image)
    {
        if(process_image == nullptr || !process_image->supports(program)) {
            return rt::ErrorCode::invalid_argument;
        }
        const rt::ErrorCode loaded =
            load(program, buffer, buffer_bytes, task_period_ns);
        if(loaded == rt::ErrorCode::ok) process_image_ = process_image;
        return loaded;
    }

    rt::ErrorCode load(const Program &program, const char *program_name,
                       unsigned char *buffer, std::size_t buffer_bytes,
                       std::int64_t task_period_ns)
    {
        if(program_name == nullptr) {
            return rt::ErrorCode::invalid_argument;
        }
        const Program *selected = &program;
        if(!program.programs.empty()) {
            selected = nullptr;
            for(const Program &candidate : program.programs) {
                std::size_t i = 0;
                while(i < candidate.program_name.size() &&
                      program_name[i] != '\0') {
                    char c = program_name[i];
                    if(c >= 'A' && c <= 'Z') {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                    if(c != candidate.program_name[i]) {
                        break;
                    }
                    ++i;
                }
                if(i == candidate.program_name.size() &&
                   program_name[i] == '\0') {
                    selected = &candidate;
                    break;
                }
            }
            if(selected == nullptr) {
                return rt::ErrorCode::invalid_argument;
            }
        } else if(!program.program_name.empty()) {
            std::size_t i = 0;
            while(i < program.program_name.size() && program_name[i] != '\0') {
                char c = program_name[i];
                if(c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if(c != program.program_name[i]) {
                    return rt::ErrorCode::invalid_argument;
                }
                ++i;
            }
            if(i != program.program_name.size() || program_name[i] != '\0') {
                return rt::ErrorCode::invalid_argument;
            }
        }
        return load(*selected, buffer, buffer_bytes, task_period_ns);
    }

    BindingError bind_axis(const char *name, axis::AxisModel *axis)
    {
        return bind_reference(name, BindingTarget::axis(axis));
    }

    BindingError bind_group(const char *name, axis::AxisGroup *group)
    {
        return bind_reference(name, BindingTarget::group(group));
    }

    BindingError bind_reference(const char *name, BindingTarget target)
    {
        if(program_ == nullptr || name == nullptr) {
            return BindingError::unknown;
        }
        if(scan_started_) {
            return BindingError::locked;
        }
        std::uint16_t axis_slot = 0;
        std::uint16_t group_slot = 0;
        for(const VarInfo &var : program_->vars) {
            const BindingTargetKind kind = reference_kind(var.type,
                                                          var.type_id);
            if(kind == BindingTargetKind::invalid) {
                continue;
            }
            const std::uint16_t slot = kind == BindingTargetKind::axis
                                           ? axis_slot++
                                           : group_slot++;
            if(!ascii_name_equal(name, var.lower)) {
                continue;
            }
            if(target.pointer() == nullptr) {
                return BindingError::null_target;
            }
            if(target.kind() != kind) {
                return BindingError::wrong_kind;
            }
            if((kind == BindingTargetKind::axis &&
                slot >= axis_targets_->size()) ||
               (kind == BindingTargetKind::group &&
                slot >= group_targets_->size())) {
                return BindingError::capacity_exceeded;
            }
            std::uint64_t handle = 0;
            std::memcpy(&handle, vars_ + var.offset, sizeof(handle));
            if(handle != 0) {
                return BindingError::duplicate;
            }
            if(kind == BindingTargetKind::axis) {
                (*axis_targets_)[slot] =
                    static_cast<axis::AxisModel *>(target.pointer());
            } else {
                auto *group =
                    static_cast<axis::AxisGroup *>(target.pointer());
                const rt::ErrorCode period_result =
                    group->set_task_cycle_period_ns(task_period_ns_);
                if(period_result != rt::ErrorCode::ok &&
                   group->task_cycle_period_ns() != task_period_ns_) {
                    return BindingError::invalid_value;
                }
                (*group_targets_)[slot] = group;
            }
            handle = static_cast<std::uint64_t>(slot) + 1U;
            std::memcpy(vars_ + var.offset, &handle, sizeof(handle));
            return BindingError::ok;
        }
        return BindingError::unknown;
    }

    BindingError bind_path_table(const char *name, fb::PathTable *table)
    {
        if(table == nullptr) return BindingError::null_target;
        return bind_named_handle(
            name, binding_type::mc_path_table, path_tables_->size(),
            [&](std::size_t slot) {
                (*path_tables_)[slot] = table;
                return BindingError::ok;
            });
    }

    BindingError bind_path_description(const char *name,
                                       const fb::PathWaypoint *waypoints,
                                       std::size_t count)
    {
        if(waypoints == nullptr) return BindingError::null_target;
        fb::PathDescription decoded{};
        if(count < 2U || count > fb::PathDescription::MaxWaypoints) {
            return BindingError::invalid_value;
        }
        const std::size_t axis_count = waypoints[0].target.size;
        if(axis_count < 2U || axis_count > axis::GroupPosition::MaxAxes) {
            return BindingError::invalid_value;
        }
        for(std::size_t index = 0; index < count; ++index) {
            if(waypoints[index].target.size != axis_count ||
               fb::validate_path_waypoint(waypoints[index], axis_count) !=
                   rt::ErrorCode::ok) {
                return BindingError::invalid_value;
            }
            decoded.waypoints[index] = waypoints[index];
        }
        decoded.count = count;
        return bind_named_handle(
            name, binding_type::mc_path_description,
            path_descriptions_->size(), [&](std::size_t slot) {
                (*path_descriptions_)[slot] = decoded;
                return BindingError::ok;
            });
    }

    BindingError bind_kin_transform(const char *name,
                                    axis::KinTransformRef transform)
    {
        switch(transform.kind) {
        case axis::KinTransformKind::kinematics:
            if(transform.kinematics == nullptr || transform.pose != nullptr) {
                return BindingError::invalid_value;
            }
            break;
        case axis::KinTransformKind::pose:
            if(transform.pose == nullptr || transform.kinematics != nullptr) {
                return BindingError::invalid_value;
            }
            break;
        case axis::KinTransformKind::none:
        default:
            // Handle 0 is the canonical none/clear value; the registry only
            // owns concrete non-owning plugin references.
            return BindingError::invalid_value;
        }
        return bind_named_handle(
            name, binding_type::mc_kin_transform_ref,
            kin_transforms_->size(), [&](std::size_t slot) {
                (*kin_transforms_)[slot] = transform;
                return BindingError::ok;
            });
    }

    BindingError bind_cam_switch_table(const char *name,
                                       const fb::CamSwitchAction *data,
                                       std::size_t count)
    {
        if(data == nullptr) return BindingError::null_target;
        if(count < 1U || count > 8U) return BindingError::invalid_value;
        for(std::size_t index = 0; index < count; ++index) {
            const fb::CamSwitchAction &action = data[index];
            if(action.track_number == 0U ||
               action.track_number > axis::AxisModel::DigitalOutputCount ||
               !std::isfinite(action.on_position) ||
               !std::isfinite(data[index].off_position) ||
               !std::isfinite(action.period) || action.period < 0.0 ||
               (action.axis_direction !=
                    fb::CamSwitchAction::AxisDirection::both &&
                action.axis_direction !=
                    fb::CamSwitchAction::AxisDirection::positive &&
                action.axis_direction !=
                    fb::CamSwitchAction::AxisDirection::negative) ||
               (action.cam_switch_mode !=
                    fb::CamSwitchAction::Mode::position &&
                action.cam_switch_mode != fb::CamSwitchAction::Mode::time) ||
               (action.cam_switch_mode ==
                    fb::CamSwitchAction::Mode::position &&
                action.period == 0.0 &&
                action.on_position > action.off_position) ||
               (action.cam_switch_mode == fb::CamSwitchAction::Mode::time &&
                action.duration_ns <= 0)) {
                return BindingError::invalid_value;
            }
        }
        return bind_named_handle(
            name, binding_type::mc_cam_switch_table_view,
            cam_switch_tables_->size(), [&](std::size_t slot) {
                (*cam_switch_tables_)[slot] = {data, count};
                return BindingError::ok;
            });
    }

    BindingError bind_cam_switch_outputs(const char *name, bool *data,
                                         std::size_t count)
    {
        if(data == nullptr) return BindingError::null_target;
        if(count < 1U || count > axis::AxisModel::DigitalOutputCount) {
            return BindingError::invalid_value;
        }
        return bind_named_handle(
            name, binding_type::mc_cam_switch_outputs_view,
            cam_switch_outputs_->size(), [&](std::size_t slot) {
                (*cam_switch_outputs_)[slot] = {data, count};
                return BindingError::ok;
            });
    }

    BindingError bind_cam_track_options(const char *name,
                                        const fb::CamTrackOption *data,
                                        std::size_t count)
    {
        if(data == nullptr) return BindingError::null_target;
        if(count < 1U || count > axis::AxisModel::DigitalOutputCount) {
            return BindingError::invalid_value;
        }
        return bind_named_handle(
            name, binding_type::mc_cam_track_options_view,
            cam_track_options_->size(), [&](std::size_t slot) {
                (*cam_track_options_)[slot] = {data, count};
                return BindingError::ok;
            });
    }

    BindingError bind_cam_table(const char *name, const exec::CamPoint *data,
                                std::size_t count, bool periodic)
    {
        if(data == nullptr) return BindingError::null_target;
        const exec::CamTableView view{data, count, periodic};
        if(count > 64U || !view.valid()) return BindingError::invalid_value;
        return bind_named_handle(
            name, binding_type::mc_cam_table_view, cam_tables_->size(),
            [&](std::size_t slot) {
                (*cam_tables_)[slot] = view;
                return BindingError::ok;
            });
    }

    BindingError bind_position_profile(
        const char *name, const BindingPositionProfileEntry *entries,
        std::size_t count)
    {
        BindingProfileSlot decoded{};
        if(entries == nullptr) return BindingError::null_target;
        if(count < 1U || count > decoded.segments.size()) {
            return BindingError::invalid_value;
        }
        for(std::size_t index = 0; index < count; ++index) {
            std::int64_t cycles = 0;
            if(entries[index].time_ns <= 0 ||
               !generated::st_binding_time_ns_to_cycles(
                   entries[index].time_ns, task_period_ns_, cycles) ||
               !std::isfinite(entries[index].position) ||
               !std::isfinite(entries[index].velocity) ||
               !std::isfinite(entries[index].acceleration) ||
               !std::isfinite(entries[index].deceleration) ||
               !std::isfinite(entries[index].jerk)) {
                return BindingError::invalid_value;
            }
            axis::ProfileSegment &segment = decoded.segments[index];
            segment.duration_cycles = cycles;
            segment.target = entries[index].position;
            segment.velocity = entries[index].velocity;
            segment.acceleration = entries[index].acceleration;
            segment.deceleration = entries[index].deceleration;
            segment.jerk = entries[index].jerk;
            segment.relative = entries[index].relative;
        }
        decoded.count = count;
        return bind_profile(name, binding_type::mc_time_position,
                            *position_profiles_, decoded);
    }

    BindingError bind_velocity_profile(
        const char *name, const BindingVelocityProfileEntry *entries,
        std::size_t count)
    {
        BindingProfileSlot decoded{};
        if(entries == nullptr) return BindingError::null_target;
        if(count < 1U || count > decoded.segments.size()) {
            return BindingError::invalid_value;
        }
        for(std::size_t index = 0; index < count; ++index) {
            std::int64_t cycles = 0;
            if(entries[index].time_ns <= 0 ||
               !generated::st_binding_time_ns_to_cycles(
                   entries[index].time_ns, task_period_ns_, cycles) ||
               !std::isfinite(entries[index].velocity) ||
               !std::isfinite(entries[index].acceleration) ||
               !std::isfinite(entries[index].deceleration) ||
               !std::isfinite(entries[index].jerk)) {
                return BindingError::invalid_value;
            }
            axis::ProfileSegment &segment = decoded.segments[index];
            segment.duration_cycles = cycles;
            segment.target = entries[index].velocity;
            segment.acceleration = entries[index].acceleration;
            segment.deceleration = entries[index].deceleration;
            segment.jerk = entries[index].jerk;
        }
        decoded.count = count;
        return bind_profile(name, binding_type::mc_time_velocity,
                            *velocity_profiles_, decoded);
    }

    BindingError bind_acceleration_profile(
        const char *name, const BindingAccelerationProfileEntry *entries,
        std::size_t count)
    {
        BindingProfileSlot decoded{};
        if(entries == nullptr) return BindingError::null_target;
        if(count < 1U || count > decoded.segments.size()) {
            return BindingError::invalid_value;
        }
        for(std::size_t index = 0; index < count; ++index) {
            std::int64_t cycles = 0;
            if(entries[index].time_ns <= 0 ||
               !generated::st_binding_time_ns_to_cycles(
                   entries[index].time_ns, task_period_ns_, cycles) ||
               !std::isfinite(entries[index].acceleration)) {
                return BindingError::invalid_value;
            }
            decoded.segments[index].duration_cycles = cycles;
            decoded.segments[index].target = entries[index].acceleration;
        }
        decoded.count = count;
        return bind_profile(name, binding_type::mc_time_acceleration,
                            *acceleration_profiles_, decoded);
    }

    // Clears a latched fault; variable and FB state keep their values
    // (declared in the implementation record). A fresh start is a reload.
    void reset()
    {
        fault_ = ScanError::ok;
        abort();
    }

    ScanError fault() const
    {
        return fault_;
    }

    InstanceState execution_state() const noexcept { return execution_state_; }
    bool running() const noexcept
    {
        return execution_state_ == InstanceState::running;
    }
    bool complete() const noexcept
    {
        return execution_state_ == InstanceState::complete;
    }

    void defer_process_image_publish(bool defer) noexcept
    {
        defer_process_image_publish_ = defer;
    }
    std::int64_t remaining_budget() const noexcept
    {
        return remaining_budget_;
    }
    std::uint32_t instruction_offset() const noexcept
    {
        return static_cast<std::uint32_t>(instruction_pc_);
    }
    const char *program_name() const noexcept
    {
        return program_ == nullptr ? "" : program_->program_name.c_str();
    }

    void set_debug_hooks(RuntimeDebugHooks *hooks,
                         std::size_t mapping) noexcept
    {
        debug_hooks_ = hooks;
        debug_mapping_ = mapping;
    }

    const Program *program() const noexcept { return program_; }

    const SourceMapEntry *last_debug_entry() const noexcept
    {
        return last_debug_entry_;
    }
    const InstructionId &last_debug_instruction_id() const noexcept
    {
        return last_debug_instruction_id_;
    }

    bool read_variable(std::uint32_t offset, std::uint32_t size,
                       unsigned char *destination) const noexcept
    {
        if(destination == nullptr || visible_vars_ == nullptr ||
           program_ == nullptr || offset > program_->vars_bytes ||
           size > program_->vars_bytes - offset)
            return false;
        std::memcpy(destination, visible_vars_ + offset, size);
        return true;
    }

    rt::ErrorCode sfc_step_active(const char *network_name,
                                  const char *step_name,
                                  bool &active) const noexcept
    {
        active = false;
        if(program_ == nullptr || network_name == nullptr ||
           step_name == nullptr || sfc_runtime_ == nullptr)
            return rt::ErrorCode::invalid_argument;
        for(const SfcNetworkInfo &network : program_->sfc_networks) {
            if(!ascii_name_equal(network_name, network.lower)) continue;
            for(std::size_t index = 0; index < network.steps.size(); ++index) {
                if(ascii_name_equal(step_name, network.steps[index].lower)) {
                    const std::uint32_t offset =
                        running() && sfc_runner_->phase != SfcRunnerPhase::stage_vars
                            ? network.shadow_active_offset
                            : network.committed_active_offset;
                    active = sfc_runtime_[offset + index] != 0;
                    return rt::ErrorCode::ok;
                }
            }
            return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::invalid_argument;
    }

    bool debug_sfc_committed_step_active(std::size_t network,
                                         std::size_t step) const noexcept
    {
        if(program_ == nullptr || sfc_runtime_ == nullptr ||
           network >= program_->sfc_networks.size() ||
           step >= program_->sfc_networks[network].steps.size())
            return false;
        return sfc_runtime_[program_->sfc_networks[network]
                                .committed_active_offset + step] != 0;
    }

    rt::ErrorCode configure_sfc_trace(SfcTraceRecord *records,
                                      std::size_t capacity) noexcept
    {
        if((records == nullptr) != (capacity == 0))
            return rt::ErrorCode::invalid_argument;
        if(running()) return rt::ErrorCode::invalid_argument;
        sfc_runner_->trace = records;
        sfc_runner_->trace_capacity = capacity;
        sfc_runner_->trace_size = 0;
        sfc_runner_->trace_dropped = 0;
        sfc_runner_->trace_scan_begin_size = 0;
        sfc_runner_->trace_scan_begin_dropped = 0;
        return rt::ErrorCode::ok;
    }

    std::size_t sfc_trace_size() const noexcept { return sfc_runner_->trace_size; }
    std::uint64_t sfc_trace_dropped() const noexcept
    {
        return sfc_runner_->trace_dropped;
    }

    rt::ErrorCode restart_sfc(const char *network_name) noexcept
    {
        if(program_ == nullptr || network_name == nullptr || running())
            return rt::ErrorCode::invalid_argument;
        for(const SfcNetworkInfo &network : program_->sfc_networks) {
            if(!ascii_name_equal(network_name, network.lower)) continue;
            reset_sfc_network(network);
            return rt::ErrorCode::ok;
        }
        return rt::ErrorCode::invalid_argument;
    }

    std::uint32_t fault_sfc_network_index() const noexcept
    {
        return sfc_runner_->fault_network_index;
    }
    std::uint32_t fault_sfc_element_index() const noexcept
    {
        return sfc_runner_->fault_element_index;
    }
    TaskFaultElementKind fault_sfc_element_kind() const noexcept
    {
        return sfc_runner_->fault_element_kind;
    }

    ScanError begin(std::int64_t budget)
    {
        if(program_ == nullptr) return ScanError::not_loaded;
        if(fault_ != ScanError::ok) return fault_;
        if(execution_state_ == InstanceState::running)
            return ScanError::invalid_argument;
        if(process_image_ != nullptr)
            process_image_->begin_scan(*program_, vars_);
        if(!program_->sfc_networks.empty()) {
            execution_vars_ = sfc_runtime_;
        } else {
            execution_vars_ = vars_;
        }
        scan_started_ = true;
        execution_state_ = InstanceState::running;
        pc_ = 0;
        instruction_pc_ = 0;
        sp_ = 0;
        remaining_budget_ = budget;
        sfc_runner_->active_region = nullptr;
        sfc_runner_->phase = program_->sfc_networks.empty()
            ? SfcRunnerPhase::none
            : (program_->vars_bytes == 0 ? SfcRunnerPhase::base
                                         : SfcRunnerPhase::stage_vars);
        sfc_runner_->network_index = 0;
        sfc_runner_->item_index = 0;
        sfc_runner_->subitem_index = 0;
        sfc_runner_->work_remaining = 0;
        sfc_runner_->reducer = 0;
        sfc_runner_->action_suppressed = false;
        sfc_runner_->fault_network_index = invalid_sfc_index;
        sfc_runner_->fault_element_index = invalid_sfc_index;
        sfc_runner_->fault_element_kind = TaskFaultElementKind::none;
        sfc_runner_->trace_scan_begin_size = sfc_runner_->trace_size;
        sfc_runner_->trace_scan_begin_dropped = sfc_runner_->trace_dropped;
        return ScanError::ok;
    }

    void abort() noexcept
    {
        if(execution_state_ == InstanceState::running) {
            if(sfc_commit_started()) rollback_sfc_commit();
            sfc_runner_->trace_size = sfc_runner_->trace_scan_begin_size;
            sfc_runner_->trace_dropped = sfc_runner_->trace_scan_begin_dropped;
        }
        execution_state_ = InstanceState::idle;
        pc_ = 0;
        instruction_pc_ = 0;
        sp_ = 0;
        remaining_budget_ = 0;
        execution_vars_ = vars_;
        visible_vars_ = vars_;
        sfc_runner_->active_region = nullptr;
        sfc_runner_->phase = SfcRunnerPhase::none;
        sfc_runner_->subitem_index = 0;
        sfc_runner_->work_remaining = 0;
        sfc_runner_->reducer = 0;
        sfc_runner_->action_suppressed = false;
    }

    // Cycle path. budget = number of instructions this scan may execute;
    // exact boundary: a program needing N instructions completes with
    // budget N and faults with budget N-1 (matrix 5.5).
    ScanError scan(std::int64_t budget)
    {
        const ScanError started = begin(budget);
        if(started != ScanError::ok) return started;
        std::int64_t executed = 0;
        return resume(std::numeric_limits<std::int64_t>::max(), executed);
    }

    // Cooperative execution slice. The slice counts bytecode instructions;
    // weighted L4 operation costs continue to charge only the task budget.
    ScanError resume(std::int64_t slice, std::int64_t &executed)
    {
        executed = 0;
        if(program_ == nullptr) return ScanError::not_loaded;
        if(fault_ != ScanError::ok) return fault_;
        if(execution_state_ != InstanceState::running) {
            return execution_state_ == InstanceState::complete
                       ? ScanError::ok
                       : ScanError::invalid_argument;
        }
        if(slice <= 0) return ScanError::ok;
        const std::uint8_t *code = nullptr;
        std::size_t size = 0;
        const std::uint64_t *constants = nullptr;
        std::size_t constant_count = 0;
        std::int32_t stack_limit = 0;
        const auto select_code = [&]() {
            if(sfc_runner_->active_region != nullptr) {
                code = sfc_runner_->active_region->code.data();
                size = sfc_runner_->active_region->code.size();
                constants = sfc_runner_->active_region->constants.data();
                constant_count = sfc_runner_->active_region->constants.size();
                stack_limit = sfc_runner_->active_region->stack_slots;
            } else {
                code = program_->code.data();
                size = program_->code.size();
                constants = program_->constants.data();
                constant_count = program_->constants.size();
                stack_limit = program_->stack_slots;
            }
        };
        select_code();
        std::size_t &pc = pc_;
        std::int32_t &sp = sp_;
        std::int64_t &remaining = remaining_budget_;
        const auto charge_operation = [&remaining](std::uint32_t cost) {
            const std::int64_t extra = cost > 1U
                ? static_cast<std::int64_t>(cost - 1U)
                : 0;
            if(remaining < extra) {
                return false;
            }
            remaining -= extra;
            return true;
        };

        for(;;) {
            if(executed >= slice) return ScanError::ok;
            if(!program_->sfc_networks.empty() &&
               sfc_runner_->phase != SfcRunnerPhase::none &&
               sfc_runner_->phase != SfcRunnerPhase::base && sfc_runner_->active_region == nullptr) {
                if(remaining <= 0)
                    return latch(ScanError::budget_exceeded);
                --remaining;
                ++executed;
                advance_sfc_runner();
                if(sfc_runner_->active_region != nullptr) {
                    pc = 0;
                    sp = 0;
                    select_code();
                    continue;
                }
                if(sfc_runner_->phase == SfcRunnerPhase::none) {
                    execution_state_ = InstanceState::complete;
                    return ScanError::ok;
                }
                continue;
            }
            if(remaining <= 0) {
                return latch(ScanError::budget_exceeded);
            }
            --remaining;
            ++executed;
            if(pc >= size) {
                return latch(ScanError::invalid_bytecode);
            }
            instruction_pc_ = pc;
            const Op op = static_cast<Op>(code[pc++]);
            switch(op) {
            case Op::debug_probe: {
                std::uint32_t probe = 0;
                if(!rd32(code, size, pc, probe))
                    return latch(ScanError::invalid_bytecode);
                const SourceMap *map = sfc_runner_->active_region == nullptr
                                           ? &program_->source_map
                                           : &sfc_runner_->active_region->source_map;
                if(probe >= map->entries.size())
                    return latch(ScanError::invalid_bytecode);
                last_debug_entry_ = &map->entries[probe];
                last_debug_instruction_id_ = last_debug_entry_->instruction_id;
                last_debug_instruction_id_.mapping = debug_mapping_;
                last_debug_instruction_id_.offset =
                    static_cast<std::uint32_t>(instruction_pc_);
                if(debug_hooks_ != nullptr &&
                   debug_hooks_->on_probe(
                       debug_mapping_, map->entries[probe],
                       static_cast<std::uint32_t>(instruction_pc_))) {
                    pc = instruction_pc_;
                    ++remaining;
                    --executed;
                    return ScanError::paused;
                }
                ++remaining;
                --executed;
                break;
            }
            case Op::halt:
                if(!program_->sfc_networks.empty()) {
                    if(sfc_runner_->active_region != nullptr)
                        finish_sfc_region();
                    else if(sfc_runner_->phase == SfcRunnerPhase::base)
                        begin_sfc_regions();
                    continue;
                } else if(process_image_ != nullptr) {
                    process_image_->commit_scan(
                        *program_, vars_, !defer_process_image_publish_);
                }
                execution_state_ = InstanceState::complete;
                return ScanError::ok;

            case Op::push_const: {
                std::uint16_t index = 0;
                if(!rd16(code, size, pc, index) || index >= constant_count ||
                   sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = constants[index];
                break;
            }
            case Op::load_var: {
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                std::uint64_t value = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || sp >= stack_limit ||
                   !load_value(offset, type, value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = value;
                break;
            }
            case Op::store_var: {
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || sp < 1 ||
                   !store_value(offset, type, stack_[sp - 1])) {
                    return latch(ScanError::invalid_bytecode);
                }
                --sp;
                break;
            }
            case Op::load_access:
            case Op::store_access: {
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || pc >= size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t count = code[pc++];
                const int needed = static_cast<int>(count) +
                                   (op == Op::store_access ? 1 : 0);
                if(sp < needed ||
                   (op == Op::load_access &&
                    sp - static_cast<int>(count) >= stack_limit)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const int index_base = sp - static_cast<int>(count);
                for(std::uint8_t i = 0; i < count; ++i) {
                    std::uint64_t lower_raw = 0;
                    std::uint64_t upper_raw = 0;
                    std::uint64_t stride = 0;
                    if(!rd64(code, size, pc, lower_raw) ||
                       !rd64(code, size, pc, upper_raw) ||
                       !rd64(code, size, pc, stride)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::int64_t lower =
                        static_cast<std::int64_t>(lower_raw);
                    const std::int64_t upper =
                        static_cast<std::int64_t>(upper_raw);
                    const std::int64_t value = as_i64(
                        stack_[index_base + static_cast<int>(i)]);
                    if(value < lower || value > upper) {
                        return latch(ScanError::range_violation);
                    }
                    const std::uint64_t delta =
                        static_cast<std::uint64_t>(value - lower) * stride;
                    if(delta > std::numeric_limits<std::uint32_t>::max() -
                                   offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    offset += static_cast<std::uint32_t>(delta);
                }
                if(op == Op::load_access) {
                    std::uint64_t value = 0;
                    sp -= count;
                    if(!load_value(offset, type, value)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    stack_[sp++] = value;
                } else {
                    const std::uint64_t value =
                        stack_[index_base - 1];
                    if(!store_value(offset, type, value)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    sp -= needed;
                }
                break;
            }
            case Op::copy_bytes: {
                std::uint32_t destination = 0;
                std::uint32_t source = 0;
                std::uint32_t count = 0;
                if(!rd32(code, size, pc, destination) ||
                   !rd32(code, size, pc, source) ||
                   !rd32(code, size, pc, count) ||
                   destination > program_->vars_bytes ||
                   count > program_->vars_bytes - destination ||
                   source > program_->vars_bytes ||
                   count > program_->vars_bytes - source) {
                    return latch(ScanError::invalid_bytecode);
                }
                std::memmove(execution_vars_ + destination,
                             execution_vars_ + source, count);
                break;
            }
            case Op::string_copy:
            case Op::string_copy_const: {
                std::uint32_t destination = 0;
                std::uint32_t destination_type = 0;
                std::uint32_t source = 0;
                std::uint32_t source_type = 0;
                if(!rd32(code, size, pc, destination) ||
                   !rd32(code, size, pc, destination_type) ||
                   !rd32(code, size, pc, source) ||
                   !rd32(code, size, pc, source_type)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *dst = program_->types.get(destination_type);
                const TypeDesc *src = program_->types.get(source_type);
                if(dst == nullptr || src == nullptr || dst->kind != src->kind ||
                   (dst->kind != TypeKind::string &&
                    dst->kind != TypeKind::wstring) ||
                   destination > program_->vars_bytes ||
                   dst->size > program_->vars_bytes - destination) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint32_t operation_cost = std::max(
                    dst->string.capacity, src->string.capacity);
                if(!charge_operation(operation_cost)) {
                    return latch(ScanError::budget_exceeded);
                }
                const unsigned char *source_bytes = nullptr;
                if(op == Op::string_copy) {
                    if(source > program_->vars_bytes ||
                       src->size > program_->vars_bytes - source) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    source_bytes = execution_vars_ + source;
                } else {
                    if(source > program_->string_constants.size() ||
                       4U > program_->string_constants.size() - source) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    source_bytes = program_->string_constants.data() + source;
                }
                const std::uint32_t length = read_le32(source_bytes);
                if(length > src->string.capacity) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t width =
                    dst->kind == TypeKind::wstring ? 4U : 1U;
                const std::uint64_t count = 4U + width * length;
                if(op == Op::string_copy_const &&
                   count > program_->string_constants.size() - source) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!valid_string_object(source_bytes, *src)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(length > dst->string.capacity) {
                    return latch(ScanError::string_capacity_exceeded);
                }
                if(op == Op::string_copy && source == destination) {
                    break;
                }
                std::memset(execution_vars_ + destination, 0,
                            static_cast<std::size_t>(dst->size));
                std::memmove(execution_vars_ + destination, source_bytes,
                             static_cast<std::size_t>(count));
                break;
            }
            case Op::string_compare: {
                if(pc >= size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t comparison = code[pc++];
                const unsigned char *objects[2] = {nullptr, nullptr};
                const TypeDesc *descs[2] = {nullptr, nullptr};
                for(int i = 0; i < 2; ++i) {
                    if(pc >= size) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::uint8_t constant = code[pc++];
                    std::uint32_t offset = 0;
                    std::uint32_t id = 0;
                    if(!rd32(code, size, pc, offset) ||
                       !rd32(code, size, pc, id)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    descs[i] = program_->types.get(id);
                    if(descs[i] == nullptr ||
                       (descs[i]->kind != TypeKind::string &&
                        descs[i]->kind != TypeKind::wstring)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    if(constant != 0) {
                        if(offset > program_->string_constants.size() ||
                           4U > program_->string_constants.size() - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        objects[i] = program_->string_constants.data() + offset;
                        const std::uint32_t length = read_le32(objects[i]);
                        const std::uint64_t object_size = 4U +
                            static_cast<std::uint64_t>(length) *
                                (descs[i]->kind == TypeKind::wstring ? 4U : 1U);
                        if(length > descs[i]->string.capacity ||
                           object_size >
                               program_->string_constants.size() - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                    } else {
                        if(offset > program_->vars_bytes ||
                           descs[i]->size > program_->vars_bytes - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        objects[i] = execution_vars_ + offset;
                    }
                }
                if(descs[0]->kind != descs[1]->kind || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint32_t operation_cost = std::max(
                    descs[0]->string.capacity, descs[1]->string.capacity);
                if(!charge_operation(operation_cost)) {
                    return latch(ScanError::budget_exceeded);
                }
                const int order = compare_strings(objects[0], *descs[0],
                                                  objects[1], *descs[1]);
                if(order == 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                bool result = false;
                switch(comparison) {
                case 8: result = order == 0; break;
                case 9: result = order != 0; break;
                case 10: result = order < 0; break;
                case 11: result = order > 0; break;
                case 12: result = order <= 0; break;
                case 13: result = order >= 0; break;
                default: return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = result ? 1U : 0U;
                break;
            }
            case Op::string_index: {
                std::uint32_t offset = 0;
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, id) || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr ||
                   (desc->kind != TypeKind::string &&
                    desc->kind != TypeKind::wstring) ||
                   offset > program_->vars_bytes ||
                   desc->size > program_->vars_bytes - offset) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!charge_operation(desc->string.capacity)) {
                    return latch(ScanError::budget_exceeded);
                }
                if(!valid_string_object(execution_vars_ + offset, *desc)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t index = as_i64(stack_[sp - 1]);
                const std::uint32_t length = read_le32(execution_vars_ + offset);
                if(index < 1 || static_cast<std::uint64_t>(index) > length) {
                    return latch(ScanError::range_violation);
                }
                if(desc->kind == TypeKind::wstring) {
                    stack_[sp - 1] = read_le32(
                        execution_vars_ + offset + 4U +
                        (static_cast<std::uint32_t>(index) - 1U) * 4U);
                } else {
                    stack_[sp - 1] = execution_vars_[offset + 4U +
                        static_cast<std::uint32_t>(index) - 1U];
                }
                break;
            }
            case Op::check_unicode:
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[sp - 1] > 0x10FFFFU ||
                   (stack_[sp - 1] >= 0xD800U &&
                    stack_[sp - 1] <= 0xDFFFU)) {
                    return latch(ScanError::conversion_invalid);
                }
                break;
            case Op::date_arith: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t kind = code[pc++];
                const std::int64_t right = as_i64(stack_[--sp]);
                const std::int64_t left = as_i64(stack_[sp - 1]);
                std::int64_t value = 0;
                if(kind <= 1U) {
                    const std::int64_t delta = kind == 0U ? right : -right;
                    value = left + delta;
                    if(value < std::numeric_limits<std::int32_t>::min() ||
                       value > std::numeric_limits<std::int32_t>::max()) {
                        return latch(ScanError::date_time_range_violation);
                    }
                } else if(kind <= 3U) {
                    constexpr std::int64_t day = 86400000000000LL;
                    const std::int64_t delta = right % day;
                    value = kind == 2U ? left + delta : left - delta;
                    value %= day;
                    if(value < 0) {
                        value += day;
                    }
                } else if(kind <= 5U) {
                    if((kind == 4U &&
                        ((right > 0 && left >
                            std::numeric_limits<std::int64_t>::max() - right) ||
                         (right < 0 && left <
                            std::numeric_limits<std::int64_t>::min() - right))) ||
                       (kind == 5U &&
                        ((right < 0 && left >
                            std::numeric_limits<std::int64_t>::max() + right) ||
                         (right > 0 && left <
                            std::numeric_limits<std::int64_t>::min() + right)))) {
                        return latch(ScanError::date_time_range_violation);
                    }
                    value = kind == 4U ? left + right : left - right;
                } else {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = static_cast<std::uint64_t>(value);
                break;
            }

            case Op::add_int:
            case Op::sub_int:
            case Op::mul_int:
            case Op::add_dint:
            case Op::sub_dint:
            case Op::mul_dint:
            case Op::add_time:
            case Op::sub_time: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                std::int64_t value = 0;
                switch(op) {
                case Op::add_int:
                    value = detail::wrap16(detail::wrap_add64(a, b));
                    break;
                case Op::sub_int:
                    value = detail::wrap16(detail::wrap_sub64(a, b));
                    break;
                case Op::mul_int:
                    value = detail::wrap16(detail::wrap_mul64(a, b));
                    break;
                case Op::add_dint:
                    value = detail::wrap32(detail::wrap_add64(a, b));
                    break;
                case Op::sub_dint:
                    value = detail::wrap32(detail::wrap_sub64(a, b));
                    break;
                case Op::mul_dint:
                    value = detail::wrap32(detail::wrap_mul64(a, b));
                    break;
                case Op::add_time:
                    value = detail::wrap_add64(a, b);
                    break;
                default:
                    value = detail::wrap_sub64(a, b);
                    break;
                }
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::div_int:
            case Op::mod_int:
            case Op::div_dint:
            case Op::mod_dint: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                if(b == 0) {
                    return latch(ScanError::division_by_zero);
                }
                std::int64_t value = 0;
                switch(op) {
                case Op::div_int:
                    value = detail::wrap16(a / b);
                    break;
                case Op::mod_int:
                    value = detail::wrap16(a % b);
                    break;
                case Op::div_dint:
                    value = detail::wrap32(a / b);
                    break;
                default:
                    value = detail::wrap32(a % b);
                    break;
                }
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::neg_int:
            case Op::neg_dint: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t a = as_i64(stack_[--sp]);
                const std::int64_t value =
                    op == Op::neg_int
                        ? detail::wrap16(detail::wrap_sub64(0, a))
                        : detail::wrap32(detail::wrap_sub64(0, a));
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::add_real:
            case Op::sub_real:
            case Op::mul_real:
            case Op::div_real: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const float b =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                const float a =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                float value = 0.0F;
                switch(op) {
                case Op::add_real: value = a + b; break;
                case Op::sub_real: value = a - b; break;
                case Op::mul_real: value = a * b; break;
                default: value = a / b; break;
                }
                stack_[sp++] =
                    detail::double_bits(static_cast<double>(value));
                break;
            }
            case Op::neg_real: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const float a =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                stack_[sp++] =
                    detail::double_bits(static_cast<double>(-a));
                break;
            }

            case Op::add_lreal:
            case Op::sub_lreal:
            case Op::mul_lreal:
            case Op::div_lreal: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                double value = 0.0;
                switch(op) {
                case Op::add_lreal: value = a + b; break;
                case Op::sub_lreal: value = a - b; break;
                case Op::mul_lreal: value = a * b; break;
                default: value = a / b; break;
                }
                stack_[sp++] = detail::double_bits(value);
                break;
            }
            case Op::neg_lreal: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double a = detail::bits_double(stack_[--sp]);
                stack_[sp++] = detail::double_bits(-a);
                break;
            }

            case Op::and_bool:
            case Op::or_bool:
            case Op::xor_bool: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const bool b = stack_[--sp] != 0;
                const bool a = stack_[--sp] != 0;
                bool value = false;
                if(op == Op::and_bool) {
                    value = a && b;
                } else if(op == Op::or_bool) {
                    value = a || b;
                } else {
                    value = a != b;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }
            case Op::not_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = stack_[sp - 1] == 0 ? 1 : 0;
                break;
            }

            case Op::cmp_eq_i:
            case Op::cmp_ne_i:
            case Op::cmp_lt_i:
            case Op::cmp_gt_i:
            case Op::cmp_le_i:
            case Op::cmp_ge_i: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                bool value = false;
                switch(op) {
                case Op::cmp_eq_i: value = a == b; break;
                case Op::cmp_ne_i: value = a != b; break;
                case Op::cmp_lt_i: value = a < b; break;
                case Op::cmp_gt_i: value = a > b; break;
                case Op::cmp_le_i: value = a <= b; break;
                default: value = a >= b; break;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::cmp_eq_f:
            case Op::cmp_ne_f:
            case Op::cmp_lt_f:
            case Op::cmp_gt_f:
            case Op::cmp_le_f:
            case Op::cmp_ge_f: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                bool value = false;
                switch(op) {
                case Op::cmp_eq_f: value = a == b; break;
                case Op::cmp_ne_f: value = a != b; break;
                case Op::cmp_lt_f: value = a < b; break;
                case Op::cmp_gt_f: value = a > b; break;
                case Op::cmp_le_f: value = a <= b; break;
                default: value = a >= b; break;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::jmp: {
                std::uint32_t target = 0;
                if(!rd32(code, size, pc, target) || target > size) {
                    return latch(ScanError::invalid_bytecode);
                }
                pc = target;
                break;
            }
            case Op::jmp_if_false: {
                std::uint32_t target = 0;
                if(!rd32(code, size, pc, target) || target > size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[--sp] == 0) {
                    pc = target;
                }
                break;
            }

            case Op::for_guard: {
                std::uint32_t offset = 0;
                std::uint64_t value = 0;
                if(!rd32(code, size, pc, offset) ||
                   !load_raw64(offset, value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(as_i64(value) == 0) {
                    return latch(ScanError::for_step_zero);
                }
                break;
            }
            case Op::for_test: {
                std::uint32_t ctrl = 0;
                std::uint32_t to = 0;
                std::uint32_t by = 0;
                std::uint32_t type = 0;
                std::uint64_t control_raw = 0;
                std::uint64_t bound_raw = 0;
                std::uint64_t step_raw = 0;
                if(!rd32(code, size, pc, ctrl) || !rd32(code, size, pc, to) ||
                   !rd32(code, size, pc, by) || !rd32(code, size, pc, type) ||
                   sp >= stack_limit || !load_value(ctrl, type, control_raw) ||
                   !load_value(to, type, bound_raw) ||
                   !load_value(by, type, step_raw)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t control = as_i64(control_raw);
                const std::int64_t bound = as_i64(bound_raw);
                const std::int64_t step = as_i64(step_raw);
                const bool keep_running =
                    step >= 0 ? control <= bound : control >= bound;
                stack_[sp++] = keep_running ? 1 : 0;
                break;
            }
            case Op::for_step_int:
            case Op::for_step_dint: {
                std::uint32_t ctrl = 0;
                std::uint32_t by = 0;
                std::uint32_t type = 0;
                std::uint64_t control = 0;
                std::uint64_t step = 0;
                if(!rd32(code, size, pc, ctrl) || !rd32(code, size, pc, by) ||
                   !rd32(code, size, pc, type) ||
                   !load_value(ctrl, type, control) ||
                   !load_value(by, type, step)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t stepped = detail::wrap_add64(
                    as_i64(control), as_i64(step));
                const std::uint64_t stored = as_u64(
                    op == Op::for_step_int ? detail::wrap16(stepped)
                                           : detail::wrap32(stepped));
                if(!store_value(ctrl, type, stored)) {
                    return latch(ScanError::invalid_bytecode);
                }
                break;
            }

            case Op::fb_store_in: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || pc >= size || sp < 1 ||
                   fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                const FbInfo &info = program_->fbs[fb];
                if(pin >= pin_table(info.type).count) {
                    return latch(ScanError::invalid_bytecode);
                }
                std::int64_t value = as_i64(stack_[--sp]);
                const PinDesc &pin_desc = pin_table(info.type).pins[pin];
                if(generated::st_binding_can_store_tagged_reference(
                       info.type, pin)) {
                    generated::StBindingNativeTaggedReferenceValue tagged{};
                    if(!resolve_tagged_reference_payload(
                           pin_desc.type_id, value, tagged) ||
                       !fb_store_tagged_reference(
                           info.type, fb_area_ + info.offset, pin,
                           pin_desc.type_id, tagged)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    break;
                }
                if(generated::st_binding_can_store_sequence(info.type, pin)) {
                    generated::StBindingNativeSequenceValue sequence{};
                    if(!resolve_sequence_payload(pin_desc.type_id, value,
                                                 sequence) ||
                       !fb_store_sequence(info.type, fb_area_ + info.offset,
                                          pin, sequence)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    break;
                }
                const Type pin_type = pin_desc.type;
                if(!resolve_reference_payload(pin_type, pin_desc.type_id,
                                              value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!fb_store_scalar(info.type, fb_area_ + info.offset, pin,
                                    as_u64(value))) {
                    return latch(ScanError::invalid_bytecode);
                }
                break;
            }
            case Op::fb_store_object: {
                std::uint16_t fb = 0;
                std::uint32_t offset = 0;
                std::uint32_t type_id = 0;
                if(!rd16(code, size, pc, fb) || pc >= size ||
                   fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type_id)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const FbInfo &info = program_->fbs[fb];
                const TypeDesc *desc = program_->types.get(type_id);
                if(pin >= pin_table(info.type).count || desc == nullptr ||
                   (desc->kind != TypeKind::array &&
                    desc->kind != TypeKind::struct_ &&
                    desc->kind != TypeKind::string &&
                    desc->kind != TypeKind::wstring) ||
                   offset > program_->vars_bytes ||
                   desc->size > program_->vars_bytes - offset ||
                   !charge_operation(static_cast<std::uint32_t>(desc->size)) ||
                   !fb_store_object(info.type, fb_area_ + info.offset, pin,
                                    execution_vars_ + offset, type_id,
                                    static_cast<std::uint32_t>(desc->size))) {
                    return latch(ScanError::invalid_bytecode);
                }
                break;
            }
            case Op::fb_call: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const FbInfo &info = program_->fbs[fb];
                fb_cycle(info.type, fb_area_ + info.offset, task_period_ns_);
                break;
            }
            case Op::fb_load_out: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || pc >= size ||
                   sp >= stack_limit || fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                const FbInfo &info = program_->fbs[fb];
                if(pin >= pin_table(info.type).count) {
                    return latch(ScanError::invalid_bytecode);
                }
                const PinDesc &pin_desc = pin_table(info.type).pins[pin];
                if(generated::st_binding_can_load_tagged_reference(
                       info.type, pin)) {
                    generated::StBindingNativeTaggedReferenceValue tagged{};
                    std::int64_t handle = 0;
                    if(!fb_load_tagged_reference(
                           info.type, fb_area_ + info.offset, pin,
                           pin_desc.type_id, tagged) ||
                       !encode_tagged_reference_payload(
                           pin_desc.type_id, tagged, handle)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    stack_[sp++] = as_u64(handle);
                    break;
                }
                if(generated::st_binding_can_load_sequence(info.type, pin)) {
                    generated::StBindingNativeSequenceValue sequence{};
                    std::int64_t handle = 0;
                    if(!fb_load_sequence(info.type, fb_area_ + info.offset,
                                         pin, sequence) ||
                       !encode_sequence_payload(pin_desc.type_id, sequence,
                                                handle)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    stack_[sp++] = as_u64(handle);
                    break;
                }
                std::uint64_t value = 0;
                if(!fb_load_scalar(info.type, fb_area_ + info.offset, pin,
                                   value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                std::int64_t resolved = as_i64(value);
                if(!encode_reference_payload(pin_desc.type, pin_desc.type_id,
                                             resolved)) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = as_u64(resolved);
                break;
            }
            case Op::fb_load_object: {
                std::uint16_t fb = 0;
                std::uint32_t offset = 0;
                std::uint32_t type_id = 0;
                if(!rd16(code, size, pc, fb) || pc >= size ||
                   fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type_id)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const FbInfo &info = program_->fbs[fb];
                const TypeDesc *desc = program_->types.get(type_id);
                if(pin >= pin_table(info.type).count || desc == nullptr ||
                   (desc->kind != TypeKind::array &&
                    desc->kind != TypeKind::struct_) ||
                   offset > program_->vars_bytes ||
                   desc->size > program_->vars_bytes - offset ||
                   !charge_operation(static_cast<std::uint32_t>(desc->size)) ||
                   !fb_load_object(info.type, fb_area_ + info.offset, pin,
                                   execution_vars_ + offset, type_id,
                                   static_cast<std::uint32_t>(desc->size))) {
                    return latch(ScanError::invalid_bytecode);
                }
                break;
            }

            // --- L1a extensions (approved st-l1a-semantics) --------------

            case Op::iarith: {
                if(pc + 2 > size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const Type type = static_cast<Type>(code[pc++]);
                if(sub == 5) { // neg (signed only)
                    if(sp < 1) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::int64_t a = as_i64(stack_[sp - 1]);
                    stack_[sp - 1] = detail::canon(
                        type,
                        static_cast<std::uint64_t>(detail::wrap_sub64(0, a)));
                    break;
                }
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t rb = stack_[--sp];
                const std::uint64_t ra = stack_[--sp];
                std::uint64_t bits = 0;
                if(is_unsigned_int(type)) {
                    switch(sub) {
                    case 0: bits = ra + rb; break;
                    case 1: bits = ra - rb; break;
                    case 2: bits = ra * rb; break;
                    case 3:
                        if(rb == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        bits = ra / rb;
                        break;
                    case 4:
                        if(rb == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        bits = ra % rb;
                        break;
                    default:
                        return latch(ScanError::invalid_bytecode);
                    }
                } else {
                    const std::int64_t a = as_i64(ra);
                    const std::int64_t b = as_i64(rb);
                    std::int64_t value = 0;
                    switch(sub) {
                    case 0: value = detail::wrap_add64(a, b); break;
                    case 1: value = detail::wrap_sub64(a, b); break;
                    case 2: value = detail::wrap_mul64(a, b); break;
                    case 3:
                        if(b == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        value = b == -1 ? detail::wrap_sub64(0, a) : a / b;
                        break;
                    case 4:
                        if(b == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        value = b == -1 ? 0 : a % b;
                        break;
                    default:
                        return latch(ScanError::invalid_bytecode);
                    }
                    bits = static_cast<std::uint64_t>(value);
                }
                stack_[sp++] = detail::canon(type, bits);
                break;
            }

            case Op::cmp_u: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const std::uint64_t b = stack_[--sp];
                const std::uint64_t a = stack_[--sp];
                bool value = false;
                switch(sub) {
                case 0: value = a < b; break;
                case 1: value = a > b; break;
                case 2: value = a <= b; break;
                case 3: value = a >= b; break;
                default:
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::bit_and:
            case Op::bit_or:
            case Op::bit_xor: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t b = stack_[--sp];
                const std::uint64_t a = stack_[--sp];
                stack_[sp++] = op == Op::bit_and
                                   ? (a & b)
                                   : (op == Op::bit_or ? (a | b) : (a ^ b));
                break;
            }
            case Op::bit_not: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                stack_[sp - 1] = detail::canon(type, ~stack_[sp - 1]);
                break;
            }

            case Op::time_scale: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const std::uint64_t rs = stack_[--sp];
                const std::int64_t t = as_i64(stack_[--sp]);
                std::uint64_t bits = 0;
                switch(sub) {
                case 0:
                    bits = static_cast<std::uint64_t>(
                        detail::wrap_mul64(t, as_i64(rs)));
                    break;
                case 1: {
                    const std::int64_t s = as_i64(rs);
                    if(s == 0) {
                        return latch(ScanError::division_by_zero);
                    }
                    bits = static_cast<std::uint64_t>(
                        s == -1 ? detail::wrap_sub64(0, t) : t / s);
                    break;
                }
                case 2:
                case 3: {
                    const double s = detail::bits_double(rs);
                    const double value = sub == 2
                                             ? static_cast<double>(t) * s
                                             : static_cast<double>(t) / s;
                    if(!std::isfinite(value)) {
                        return latch(ScanError::conversion_invalid);
                    }
                    bits = detail::wrap_double_to_u64(std::trunc(value));
                    break;
                }
                default:
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = bits;
                break;
            }

            case Op::power: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                const double value = std::pow(a, b);
                stack_[sp++] =
                    type == Type::real
                        ? detail::double_bits(static_cast<double>(
                              static_cast<float>(value)))
                        : detail::double_bits(value);
                break;
            }

            case Op::conv_wrap: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                stack_[sp - 1] = detail::canon(type, stack_[sp - 1]);
                break;
            }
            case Op::conv_i2d: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(
                    static_cast<double>(as_i64(stack_[sp - 1])));
                break;
            }
            case Op::conv_u2d: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(
                    static_cast<double>(stack_[sp - 1]));
                break;
            }
            case Op::f_narrow: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(static_cast<double>(
                    static_cast<float>(detail::bits_double(stack_[sp - 1]))));
                break;
            }
            case Op::conv_round:
            case Op::conv_trunc: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                const double value = detail::bits_double(stack_[sp - 1]);
                if(!std::isfinite(value)) {
                    return latch(ScanError::conversion_invalid);
                }
                const double adjusted = op == Op::conv_trunc
                                            ? std::trunc(value)
                                            : std::nearbyint(value);
                stack_[sp - 1] = detail::canon(
                    type, detail::wrap_double_to_u64(adjusted));
                break;
            }
            case Op::conv_to_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = stack_[sp - 1] != 0 ? 1 : 0;
                break;
            }
            case Op::conv_f_to_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] =
                    detail::bits_double(stack_[sp - 1]) != 0.0 ? 1 : 0;
                break;
            }
            case Op::check_range: {
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, id) || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr ||
                   (desc->kind != TypeKind::enum_ &&
                    desc->kind != TypeKind::subrange)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t bits = stack_[sp - 1];
                bool valid = false;
                if(desc->kind == TypeKind::enum_) {
                    for(const EnumItem &item : desc->enum_items) {
                        if(static_cast<std::uint64_t>(item.value.as_signed()) ==
                           bits) {
                            valid = true;
                            break;
                        }
                    }
                } else if(desc->integer_sign == IntegerSign::signed_) {
                    const std::int64_t value = as_i64(bits);
                    valid = value >= desc->subrange.lower.as_signed() &&
                            value <= desc->subrange.upper.as_signed();
                } else {
                    valid = bits >= desc->subrange.lower.as_unsigned() &&
                            bits <= desc->subrange.upper.as_unsigned();
                }
                if(!valid) {
                    return latch(ScanError::range_violation);
                }
                break;
            }
            case Op::commit_outputs: {
                std::uint16_t count = 0;
                std::uint16_t dynamic_count = 0;
                if(!rd16(code, size, pc, count) ||
                   !rd16(code, size, pc, dynamic_count) || count > 256U ||
                   sp < static_cast<std::int32_t>(dynamic_count)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!charge_operation(count)) {
                    return latch(ScanError::budget_exceeded);
                }
                // A commit is not observable unless the bytecode stream can
                // also reach at least its next instruction.  This preserves
                // the exact N/N-1 watchdog boundary for a terminal call:
                // N-1 faults before publishing any OUTPUT value.
                if(remaining <= 0) {
                    return latch(ScanError::budget_exceeded);
                }
                std::uint32_t destinations[256] = {};
                std::uint32_t sources[256] = {};
                std::uint32_t bytes[256] = {};
                const std::int32_t index_base =
                    sp - static_cast<std::int32_t>(dynamic_count);
                std::int32_t index_at = index_base;
                for(std::uint16_t item = 0; item < count; ++item) {
                    std::uint32_t source = 0;
                    std::uint32_t source_type = 0;
                    std::uint32_t destination = 0;
                    std::uint32_t destination_type = 0;
                    if(!rd32(code, size, pc, source) ||
                       !rd32(code, size, pc, source_type) ||
                       !rd32(code, size, pc, destination) ||
                       !rd32(code, size, pc, destination_type) || pc >= size) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::uint8_t indices = code[pc++];
                    if(index_at + indices > sp) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    for(std::uint8_t i = 0; i < indices; ++i) {
                        std::uint64_t lower_raw = 0;
                        std::uint64_t upper_raw = 0;
                        std::uint64_t stride = 0;
                        if(!rd64(code, size, pc, lower_raw) ||
                           !rd64(code, size, pc, upper_raw) ||
                           !rd64(code, size, pc, stride)) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        const std::int64_t lower =
                            static_cast<std::int64_t>(lower_raw);
                        const std::int64_t upper =
                            static_cast<std::int64_t>(upper_raw);
                        const std::int64_t value = as_i64(stack_[index_at++]);
                        if(value < lower || value > upper) {
                            return latch(ScanError::range_violation);
                        }
                        const std::uint64_t delta =
                            static_cast<std::uint64_t>(value - lower) * stride;
                        if(delta > std::numeric_limits<std::uint32_t>::max() -
                                       destination) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        destination += static_cast<std::uint32_t>(delta);
                    }
                    if(source_type != destination_type) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const TypeDesc *desc = program_->types.get(destination_type);
                    if(desc == nullptr || desc->size == 0 ||
                       source > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - source ||
                       destination > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - destination) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    sources[item] = source;
                    destinations[item] = destination;
                    bytes[item] = static_cast<std::uint32_t>(desc->size);
                    for(std::uint16_t prior = 0; prior < item; ++prior) {
                        const std::uint64_t prior_end =
                            static_cast<std::uint64_t>(destinations[prior]) +
                            bytes[prior];
                        const std::uint64_t current_end =
                            static_cast<std::uint64_t>(destination) +
                            bytes[item];
                        if(destination < prior_end &&
                           destinations[prior] < current_end) {
                            return latch(ScanError::alias_violation);
                        }
                    }
                }
                if(index_at != sp) {
                    return latch(ScanError::invalid_bytecode);
                }
                for(std::uint16_t item = 0; item < count; ++item) {
                    std::memmove(execution_vars_ + destinations[item],
                                 execution_vars_ + sources[item], bytes[item]);
                }
                sp = index_base;
                break;
            }
            case Op::alias_guard:
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[sp - 1] == 0) {
                    return latch(ScanError::alias_violation);
                }
                break;
            case Op::string_length: {
                if(pc >= size || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t constant = code[pc++];
                std::uint32_t offset = 0;
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, id)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr || (desc->kind != TypeKind::string &&
                                       desc->kind != TypeKind::wstring)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const unsigned char *object = nullptr;
                if(constant != 0) {
                    if(offset > program_->string_constants.size() ||
                       desc->size > program_->string_constants.size() - offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    object = program_->string_constants.data() + offset;
                } else {
                    if(offset > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    object = execution_vars_ + offset;
                }
                if(!valid_string_object(object, *desc) ||
                   !charge_operation(read_le32(object))) {
                    return latch(valid_string_object(object, *desc)
                                     ? ScanError::budget_exceeded
                                     : ScanError::invalid_bytecode);
                }
                stack_[sp++] = read_le32(object);
                break;
            }
            case Op::standard_scalar: {
                if(pc + 3U > size) return latch(ScanError::invalid_bytecode);
                const StandardFunction function =
                    static_cast<StandardFunction>(code[pc++]);
                const std::uint8_t argc = code[pc++];
                const Type type = static_cast<Type>(code[pc++]);
                if(argc == 0 || sp < argc ||
                   function >= StandardFunction::count) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!charge_operation(argc)) {
                    return latch(ScanError::budget_exceeded);
                }
                const int base = sp - argc;
                const bool floating = is_real_family(type);
                auto real = [this](std::uint64_t raw) {
                    return detail::bits_double(raw);
                };
                auto put_real = [this, type](double value) {
                    if(type == Type::real) value = static_cast<float>(value);
                    return detail::double_bits(value);
                };
                auto less = [floating, &real, type](std::uint64_t a,
                                                    std::uint64_t b) {
                    if(floating) return real(a) < real(b);
                    if(is_unsigned_int(type) || is_bitstring(type)) return a < b;
                    return static_cast<std::int64_t>(a) <
                           static_cast<std::int64_t>(b);
                };
                auto equal = [floating, &real](std::uint64_t a,
                                               std::uint64_t b) {
                    return floating ? real(a) == real(b) : a == b;
                };
                std::uint64_t value = stack_[base];
                if(function <= StandardFunction::atan) {
                    if(function == StandardFunction::abs) {
                        if(floating) value = put_real(std::fabs(real(value)));
                        else if(static_cast<std::int64_t>(value) < 0)
                            value = detail::canon(type, 0U - value);
                    } else {
                        const double x = real(value);
                        double y = x;
                        switch(function) {
                        case StandardFunction::sqrt: y = std::sqrt(x); break;
                        case StandardFunction::ln: y = std::log(x); break;
                        case StandardFunction::log: y = std::log10(x); break;
                        case StandardFunction::exp: y = std::exp(x); break;
                        case StandardFunction::sin: y = std::sin(x); break;
                        case StandardFunction::cos: y = std::cos(x); break;
                        case StandardFunction::tan: y = std::tan(x); break;
                        case StandardFunction::asin: y = std::asin(x); break;
                        case StandardFunction::acos: y = std::acos(x); break;
                        case StandardFunction::atan: y = std::atan(x); break;
                        default: break;
                        }
                        value = put_real(y);
                    }
                } else if(function >= StandardFunction::add &&
                          function <= StandardFunction::max) {
                    if(function == StandardFunction::expt) {
                        value = put_real(std::pow(real(stack_[base]),
                                                  real(stack_[base + 1])));
                    } else if(function == StandardFunction::min ||
                              function == StandardFunction::max) {
                        for(int i = base + 1; i < sp; ++i) {
                            const bool take = function == StandardFunction::min
                                ? less(stack_[i], value) : less(value, stack_[i]);
                            if(take) value = stack_[i];
                        }
                    } else if(floating) {
                        double y = real(value);
                        if(function == StandardFunction::add ||
                           function == StandardFunction::mul) {
                            for(int i = base + 1; i < sp; ++i)
                                y = function == StandardFunction::add
                                    ? y + real(stack_[i]) : y * real(stack_[i]);
                        } else {
                            const double rhs = real(stack_[base + 1]);
                            y = function == StandardFunction::sub ? y - rhs
                              : function == StandardFunction::div ? y / rhs
                              : std::fmod(y, rhs);
                        }
                        value = put_real(y);
                    } else {
                        auto apply = [type](std::uint64_t a, std::uint64_t b,
                                           StandardFunction op) {
                            if(op == StandardFunction::add) return detail::canon(type, a + b);
                            if(op == StandardFunction::sub) return detail::canon(type, a - b);
                            if(op == StandardFunction::mul) return detail::canon(type, a * b);
                            if(is_unsigned_int(type)) {
                                return detail::canon(type,
                                    op == StandardFunction::div ? a / b : a % b);
                            }
                            const std::int64_t x = static_cast<std::int64_t>(a);
                            const std::int64_t y = static_cast<std::int64_t>(b);
                            if(x == std::numeric_limits<std::int64_t>::min() && y == -1)
                                return detail::canon(type,
                                    op == StandardFunction::div ? a : 0U);
                            return detail::canon(type, static_cast<std::uint64_t>(op == StandardFunction::div ? x / y : x % y));
                        };
                        if((function == StandardFunction::div || function == StandardFunction::mod) && stack_[base + 1] == 0)
                            return latch(ScanError::division_by_zero);
                        for(int i = base + 1; i < sp; ++i)
                            value = apply(value, stack_[i], function);
                    }
                } else if(function == StandardFunction::limit) {
                    if(less(stack_[base + 2], stack_[base]))
                        return latch(ScanError::invalid_argument);
                    value = less(stack_[base + 1], stack_[base]) ? stack_[base]
                          : less(stack_[base + 2], stack_[base + 1])
                              ? stack_[base + 2] : stack_[base + 1];
                } else if(function == StandardFunction::sel) {
                    value = stack_[base] ? stack_[base + 2] : stack_[base + 1];
                } else if(function == StandardFunction::mux) {
                    const std::int64_t index = static_cast<std::int64_t>(stack_[base]);
                    if(index < 0 || index >= argc - 1)
                        return latch(ScanError::range_violation);
                    value = stack_[base + 1 + static_cast<int>(index)];
                } else if(function >= StandardFunction::gt &&
                          function <= StandardFunction::ne) {
                    bool result = true;
                    for(int i = base; i + 1 < sp && result; ++i) {
                        const bool lt = less(stack_[i], stack_[i + 1]);
                        const bool eq = equal(stack_[i], stack_[i + 1]);
                        switch(function) {
                        case StandardFunction::gt: result = !lt && !eq; break;
                        case StandardFunction::ge: result = !lt; break;
                        case StandardFunction::eq: result = eq; break;
                        case StandardFunction::le: result = lt || eq; break;
                        case StandardFunction::lt: result = lt; break;
                        case StandardFunction::ne: result = !eq; break;
                        default: break;
                        }
                    }
                    value = result ? 1U : 0U;
                } else if(function >= StandardFunction::shl &&
                          function <= StandardFunction::ror) {
                    const std::int64_t count = static_cast<std::int64_t>(stack_[base + 1]);
                    if(count < 0) return latch(ScanError::range_violation);
                    const unsigned width = static_cast<unsigned>(width_bits(type));
                    const std::uint64_t mask = width == 64 ? ~0ULL : ((1ULL << width) - 1U);
                    const std::uint64_t input = stack_[base] & mask;
                    if(function == StandardFunction::shl || function == StandardFunction::shr) {
                        value = static_cast<std::uint64_t>(count) >= width ? 0U
                            : function == StandardFunction::shl
                                ? (input << count) & mask : input >> count;
                    } else {
                        const unsigned rotate = static_cast<unsigned>(count % width);
                        value = rotate == 0 ? input
                            : function == StandardFunction::rol
                                ? ((input << rotate) | (input >> (width - rotate))) & mask
                                : ((input >> rotate) | (input << (width - rotate))) & mask;
                    }
                } else {
                    constexpr std::int64_t day = 86400000000000LL;
                    const std::int64_t a = static_cast<std::int64_t>(stack_[base]);
                    const std::int64_t b = static_cast<std::int64_t>(stack_[base + 1]);
                    std::int64_t out = 0;
                    auto add_overflow = [](std::int64_t x, std::int64_t y, std::int64_t &z) {
                        if((y > 0 && x > std::numeric_limits<std::int64_t>::max() - y) ||
                           (y < 0 && x < std::numeric_limits<std::int64_t>::min() - y)) return true;
                        z = x + y; return false;
                    };
                    auto sub_overflow = [](std::int64_t x, std::int64_t y,
                                           std::int64_t &z) {
                        if((y > 0 && x < std::numeric_limits<std::int64_t>::min() + y) ||
                           (y < 0 && x > std::numeric_limits<std::int64_t>::max() + y)) return true;
                        z = x - y; return false;
                    };
                    auto mul_overflow = [](std::int64_t x, std::int64_t y,
                                           std::int64_t &z) {
                        if(x == 0 || y == 0) { z = 0; return false; }
                        if((x == -1 && y == std::numeric_limits<std::int64_t>::min()) ||
                           (y == -1 && x == std::numeric_limits<std::int64_t>::min())) return true;
                        if(x > 0) {
                            if((y > 0 && x > std::numeric_limits<std::int64_t>::max() / y) ||
                               (y < 0 && y < std::numeric_limits<std::int64_t>::min() / x)) return true;
                        } else {
                            if((y > 0 && x < std::numeric_limits<std::int64_t>::min() / y) ||
                               (y < 0 && x < std::numeric_limits<std::int64_t>::max() / y)) return true;
                        }
                        z = x * y; return false;
                    };
                    switch(function) {
                    case StandardFunction::add_time:
                    case StandardFunction::add_dt_time:
                        if(add_overflow(a, b, out)) return latch(ScanError::date_time_range_violation);
                        break;
                    case StandardFunction::sub_time:
                    case StandardFunction::sub_dt_dt:
                        if(sub_overflow(a, b, out))
                            return latch(ScanError::date_time_range_violation);
                        break;
                    case StandardFunction::add_tod_time:
                        out = ((a + b % day) % day + day) % day; break;
                    case StandardFunction::sub_tod_time:
                        out = ((a - b % day) % day + day) % day; break;
                    case StandardFunction::sub_date_date:
                        { std::int64_t days = 0;
                          if(sub_overflow(a, b, days) || mul_overflow(days, day, out))
                              return latch(ScanError::date_time_range_violation); }
                        break;
                    case StandardFunction::multime:
                        if(mul_overflow(a, b, out))
                            return latch(ScanError::date_time_range_violation);
                        break;
                    case StandardFunction::divtime:
                        if(b == 0) return latch(ScanError::division_by_zero);
                        if(a == std::numeric_limits<std::int64_t>::min() && b == -1)
                            return latch(ScanError::date_time_range_violation);
                        out = a / b; break;
                    case StandardFunction::concat_date_tod:
                        { std::int64_t date_ns = 0;
                          if(mul_overflow(a, day, date_ns) ||
                             add_overflow(date_ns, b, out))
                              return latch(ScanError::date_time_range_violation); }
                        break;
                    default: return latch(ScanError::invalid_bytecode);
                    }
                    value = static_cast<std::uint64_t>(out);
                }
                stack_[base] = value;
                sp = base + 1;
                break;
            }
            case Op::standard_string: {
                if(pc + 10U > size) return latch(ScanError::invalid_bytecode);
                const StandardFunction function = static_cast<StandardFunction>(code[pc++]);
                const std::uint8_t argc = code[pc++];
                std::uint32_t destination = 0;
                std::uint32_t destination_id = 0;
                if(argc == 0 || argc > 32 || !rd32(code, size, pc, destination) ||
                   !rd32(code, size, pc, destination_id))
                    return latch(ScanError::invalid_bytecode);
                const unsigned char *objects[32] = {};
                const TypeDesc *descs[32] = {};
                bool string_arg[32] = {};
                int scalar_count = 0;
                std::uint32_t capacities[32] = {};
                std::uint8_t string_count = 0;
                for(std::uint8_t i = 0; i < argc; ++i) {
                    if(pc >= size) return latch(ScanError::invalid_bytecode);
                    string_arg[i] = code[pc++] != 0;
                    if(!string_arg[i]) { ++scalar_count; continue; }
                    if(pc >= size) return latch(ScanError::invalid_bytecode);
                    const bool constant = code[pc++] != 0;
                    std::uint32_t offset = 0, id = 0;
                    if(!rd32(code, size, pc, offset) || !rd32(code, size, pc, id))
                        return latch(ScanError::invalid_bytecode);
                    descs[i] = program_->types.get(id);
                    if(descs[i] == nullptr ||
                       (descs[i]->kind != TypeKind::string && descs[i]->kind != TypeKind::wstring))
                        return latch(ScanError::invalid_bytecode);
                    capacities[string_count++] = static_cast<std::uint32_t>(
                        descs[i]->string.capacity);
                    if(constant) {
                        if(offset > program_->string_constants.size() ||
                           4U > program_->string_constants.size() - offset)
                            return latch(ScanError::invalid_bytecode);
                        objects[i] = program_->string_constants.data() + offset;
                        const std::uint32_t units = read_le32(objects[i]);
                        const std::uint64_t bytes = 4U + static_cast<std::uint64_t>(units) *
                            (descs[i]->kind == TypeKind::wstring ? 4U : 1U);
                        if(bytes > program_->string_constants.size() - offset)
                            return latch(ScanError::invalid_bytecode);
                    } else {
                        if(offset > program_->vars_bytes || descs[i]->size > program_->vars_bytes - offset)
                            return latch(ScanError::invalid_bytecode);
                        objects[i] = execution_vars_ + offset;
                    }
                    if(!valid_string_object(objects[i], *descs[i]))
                        return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *destination_desc =
                    destination == std::numeric_limits<std::uint32_t>::max()
                        ? nullptr : program_->types.get(destination_id);
                if(destination != std::numeric_limits<std::uint32_t>::max() &&
                   (destination_desc == nullptr ||
                    (destination_desc->kind != TypeKind::string &&
                     destination_desc->kind != TypeKind::wstring))) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint32_t destination_capacity =
                    destination_desc == nullptr ? 0U
                        : static_cast<std::uint32_t>(
                              destination_desc->string.capacity);
                const std::uint32_t operation_cost = standard_string_cost(
                    function, capacities, string_count, destination_capacity);
                if(sp < scalar_count || !charge_operation(operation_cost))
                    return latch(sp < scalar_count ? ScanError::invalid_bytecode
                                                   : ScanError::budget_exceeded);
                const int scalar_base = sp - scalar_count;
                int scalar_at = 0;
                auto scalar = [&](std::uint8_t argument) -> std::int64_t {
                    int at = 0;
                    for(std::uint8_t i = 0; i < argument; ++i)
                        if(!string_arg[i]) ++at;
                    return static_cast<std::int64_t>(stack_[scalar_base + at]);
                };
                auto scalar_length = [&](const unsigned char *object,
                                         const TypeDesc &desc) {
                    if(desc.kind == TypeKind::wstring) return read_le32(object);
                    const std::uint32_t bytes = read_le32(object);
                    std::uint32_t at = 0, count = 0, codepoint = 0;
                    while(at < bytes && next_utf8(object + 4U, bytes, at, codepoint)) ++count;
                    return count;
                };
                auto unit_at = [&](const unsigned char *object, const TypeDesc &desc,
                                   std::uint32_t scalar_index) {
                    if(desc.kind == TypeKind::wstring) return scalar_index * 4U;
                    const std::uint32_t bytes = read_le32(object);
                    std::uint32_t at = 0, count = 0, codepoint = 0;
                    while(count < scalar_index && at < bytes) {
                        next_utf8(object + 4U, bytes, at, codepoint); ++count;
                    }
                    return at;
                };
                if(function == StandardFunction::len || function == StandardFunction::find) {
                    std::int64_t answer = 0;
                    if(function == StandardFunction::len) {
                        answer = scalar_length(objects[0], *descs[0]);
                    } else {
                        const std::uint32_t hay_units = read_le32(objects[0]);
                        const std::uint32_t needle_units = read_le32(objects[1]);
                        answer = needle_units == 0 ? 1 : 0;
                        const std::uint32_t width =
                            descs[0]->kind == TypeKind::wstring ? 4U : 1U;
                        for(std::uint32_t pos = 0;
                            answer == 0 && pos + needle_units <= hay_units;
                            ++pos) {
                            bool match = true;
                            for(std::uint32_t unit = 0;
                                unit < needle_units && match; ++unit) {
                                const std::uint32_t hay_value = width == 4U
                                    ? read_le32(objects[0] + 4U +
                                                (pos + unit) * 4U)
                                    : objects[0][4U + pos + unit];
                                const std::uint32_t needle_value = width == 4U
                                    ? read_le32(objects[1] + 4U + unit * 4U)
                                    : objects[1][4U + unit];
                                match = hay_value == needle_value;
                            }
                            if(match) {
                                if(width == 4U) {
                                    answer = static_cast<std::int64_t>(pos + 1U);
                                } else {
                                    std::uint32_t byte = 0, scalar_pos = 0,
                                                  codepoint = 0;
                                    while(byte < pos && next_utf8(
                                        objects[0] + 4U, hay_units, byte,
                                        codepoint)) ++scalar_pos;
                                    answer = static_cast<std::int64_t>(
                                        scalar_pos + 1U);
                                }
                            }
                        }
                    }
                    sp = scalar_base;
                    if(sp >= stack_limit) return latch(ScanError::invalid_bytecode);
                    stack_[sp++] = static_cast<std::uint64_t>(answer);
                    break;
                }
                const TypeDesc *dst = destination_desc;
                if(dst == nullptr || destination == std::numeric_limits<std::uint32_t>::max() ||
                   destination > program_->vars_bytes || dst->size > program_->vars_bytes - destination)
                    return latch(ScanError::invalid_bytecode);
                std::array<unsigned char, 16388> output{};
                std::uint32_t output_units = 0;
                bool capacity_exceeded = false;
                const std::uint32_t destination_width =
                    dst->kind == TypeKind::wstring ? 4U : 1U;
                const std::uint32_t destination_bytes =
                    static_cast<std::uint32_t>(dst->string.capacity) *
                    destination_width;
                auto append = [&](const unsigned char *object, const TypeDesc &desc,
                                  std::uint32_t first, std::uint32_t last) {
                    const std::uint32_t begin = unit_at(object, desc, first);
                    const std::uint32_t end = unit_at(object, desc, last);
                    const std::uint32_t bytes = end - begin;
                    if(bytes > destination_bytes - output_units ||
                       4U + output_units + bytes > output.size()) {
                        capacity_exceeded = true;
                        return false;
                    }
                    std::memcpy(output.data() + 4U + output_units, object + 4U + begin, bytes);
                    output_units += bytes;
                    return true;
                };
                const std::uint32_t source_length = scalar_length(objects[0], *descs[0]);
                bool valid = true;
                switch(function) {
                case StandardFunction::concat:
                    for(std::uint8_t i = 0; i < argc && valid; ++i)
                        valid = append(objects[i], *descs[i], 0, scalar_length(objects[i], *descs[i]));
                    break;
                case StandardFunction::left: {
                    const std::int64_t count = scalar(1);
                    if(count < 0) valid = false;
                    else valid = append(objects[0], *descs[0], 0,
                                        std::min<std::uint32_t>(source_length, static_cast<std::uint32_t>(count)));
                    break;
                }
                case StandardFunction::right: {
                    const std::int64_t count = scalar(1);
                    if(count < 0) valid = false;
                    else { const std::uint32_t take = std::min<std::uint32_t>(source_length, static_cast<std::uint32_t>(count));
                        valid = append(objects[0], *descs[0], source_length - take, source_length); }
                    break;
                }
                case StandardFunction::mid: {
                    const std::int64_t count = scalar(1), position = scalar(2);
                    if(count < 0 || position < 1 || position > static_cast<std::int64_t>(source_length) + 1) valid = false;
                    else { const std::uint32_t begin = static_cast<std::uint32_t>(position - 1);
                        valid = append(objects[0], *descs[0], begin,
                            std::min<std::uint32_t>(source_length, begin + static_cast<std::uint32_t>(count))); }
                    break;
                }
                case StandardFunction::insert: {
                    const std::int64_t position = scalar(2);
                    if(position < 1 || position > static_cast<std::int64_t>(source_length) + 1) valid = false;
                    else { const std::uint32_t at = static_cast<std::uint32_t>(position - 1);
                        valid = append(objects[0], *descs[0], 0, at) &&
                                append(objects[1], *descs[1], 0, scalar_length(objects[1], *descs[1])) &&
                                append(objects[0], *descs[0], at, source_length); }
                    break;
                }
                case StandardFunction::delete_: {
                    const std::int64_t count = scalar(1), position = scalar(2);
                    if(count < 0 || position < 1 || position > static_cast<std::int64_t>(source_length) + 1) valid = false;
                    else { const std::uint32_t at = static_cast<std::uint32_t>(position - 1);
                        const std::uint32_t end = std::min<std::uint32_t>(source_length, at + static_cast<std::uint32_t>(count));
                        valid = append(objects[0], *descs[0], 0, at) && append(objects[0], *descs[0], end, source_length); }
                    break;
                }
                case StandardFunction::replace: {
                    const std::int64_t count = scalar(2), position = scalar(3);
                    if(count < 0 || position < 1 || position > static_cast<std::int64_t>(source_length) + 1) valid = false;
                    else { const std::uint32_t at = static_cast<std::uint32_t>(position - 1);
                        const std::uint32_t end = std::min<std::uint32_t>(source_length, at + static_cast<std::uint32_t>(count));
                        valid = append(objects[0], *descs[0], 0, at) &&
                                append(objects[1], *descs[1], 0, scalar_length(objects[1], *descs[1])) &&
                                append(objects[0], *descs[0], end, source_length); }
                    break;
                }
                default: return latch(ScanError::invalid_bytecode);
                }
                if(!valid) return latch(capacity_exceeded
                    ? ScanError::string_capacity_exceeded
                    : ScanError::range_violation);
                const std::uint32_t width = destination_width;
                const std::uint32_t stored_units = width == 4U ? output_units / 4U : output_units;
                if(stored_units > dst->string.capacity)
                    return latch(ScanError::string_capacity_exceeded);
                output[0] = static_cast<unsigned char>(stored_units);
                output[1] = static_cast<unsigned char>(stored_units >> 8U);
                output[2] = static_cast<unsigned char>(stored_units >> 16U);
                output[3] = static_cast<unsigned char>(stored_units >> 24U);
                std::memset(execution_vars_ + destination, 0, dst->size);
                std::memcpy(execution_vars_ + destination, output.data(),
                            4U + output_units);
                sp = scalar_base;
                (void)scalar_at;
                break;
            }

            default:
                return latch(ScanError::invalid_bytecode);
            }
        }
    }

    // --- read-only symbol access (matrix 3.13; twin/monitoring ground) ----

    std::size_t variable_count() const
    {
        return program_ ? program_->vars.size() : 0;
    }

    const VarInfo *variable(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return nullptr;
        }
        return &program_->vars[index];
    }

    // Case-insensitive lookup; returns -1 when absent.
    int find(const char *name) const
    {
        if(!program_ || name == nullptr) {
            return -1;
        }
        for(std::size_t i = 0; i < program_->vars.size(); ++i) {
            const char *lower = program_->vars[i].lower.c_str();
            std::size_t k = 0;
            bool match = true;
            for(; name[k] != '\0' && lower[k] != '\0'; ++k) {
                char c = name[k];
                if(c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if(c != lower[k]) {
                    match = false;
                    break;
                }
            }
            if(match && name[k] == '\0' && lower[k] == '\0') {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::int64_t value_i64(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return 0;
        }
        std::uint64_t value = 0;
        return load_value_at(visible_vars_, program_->vars[index].offset,
                             program_->vars[index].type_id, value)
                   ? as_i64(value)
                   : 0;
    }

    double value_f64(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return 0.0;
        }
        std::uint64_t value = 0;
        return load_value_at(visible_vars_, program_->vars[index].offset,
                             program_->vars[index].type_id, value)
                   ? detail::bits_double(value)
                   : 0.0;
    }

    bool value_bool(std::size_t index) const
    {
        return value_i64(index) != 0;
    }

private:
    static constexpr std::uint32_t invalid_sfc_index =
        std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t sfc_block_bytes = 16;
    static constexpr std::uint8_t action_direct = 1U;
    static constexpr std::uint8_t action_store = 2U;
    static constexpr std::uint8_t action_reset = 4U;
    static constexpr std::uint8_t action_expire = 8U;

    unsigned char *sfc_at(std::uint32_t offset) const noexcept
    {
        return sfc_runtime_ + offset;
    }

    bool sfc_commit_started() const noexcept
    {
        return sfc_runner_->phase == SfcRunnerPhase::commit_vars ||
               sfc_runner_->phase == SfcRunnerPhase::commit_active ||
               sfc_runner_->phase == SfcRunnerPhase::commit_blocks ||
               sfc_runner_->phase == SfcRunnerPhase::commit_actions ||
               sfc_runner_->phase == SfcRunnerPhase::finish;
    }

    void rollback_sfc_commit() noexcept
    {
        if(program_ == nullptr || sfc_runtime_ == nullptr) return;
        std::memcpy(vars_, sfc_at(program_->sfc_committed_shadow_offset()),
                    program_->vars_bytes);
        for(const SfcNetworkInfo &network : program_->sfc_networks) {
            std::memcpy(sfc_at(network.committed_active_offset),
                        sfc_at(network.shadow_active_offset),
                        network.steps.size());
            std::memcpy(sfc_at(network.committed_block_offset),
                        sfc_at(network.shadow_block_offset),
                        network.action_blocks.size() * sfc_block_bytes);
            std::memcpy(sfc_at(network.committed_action_offset),
                        sfc_at(network.shadow_action_offset),
                        network.actions.size());
        }
    }

    static std::int64_t block_elapsed(const unsigned char *state) noexcept
    {
        std::int64_t result = 0;
        std::memcpy(&result, state, sizeof(result));
        return result;
    }

    static void block_elapsed(unsigned char *state,
                              std::int64_t value) noexcept
    {
        std::memcpy(state, &value, sizeof(value));
    }

    static std::int64_t saturating_add(std::int64_t left,
                                       std::int64_t right) noexcept
    {
        if(right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
            return std::numeric_limits<std::int64_t>::max();
        return left + right;
    }

    void reset_sfc_network(const SfcNetworkInfo &network) noexcept
    {
        std::memset(sfc_at(network.committed_active_offset), 0,
                    network.steps.size());
        std::memset(sfc_at(network.staged_active_offset), 0,
                    network.steps.size());
        std::memset(sfc_at(network.firing_offset), 0,
                    network.transitions.size());
        std::memset(sfc_at(network.committed_block_offset), 0,
                    network.action_blocks.size() * sfc_block_bytes);
        std::memset(sfc_at(network.staged_block_offset), 0,
                    network.action_blocks.size() * sfc_block_bytes);
        std::memset(sfc_at(network.committed_action_offset), 0,
                    network.actions.size());
        std::memset(sfc_at(network.staged_action_offset), 0,
                    network.actions.size());
        std::memset(sfc_at(network.direct_action_offset), 0,
                    network.actions.size());
        for(std::size_t index = 0; index < network.steps.size(); ++index) {
            if(!network.steps[index].initial) continue;
            sfc_at(network.committed_active_offset)[index] = 1U;
            sfc_at(network.staged_active_offset)[index] = 1U;
        }
    }

    void initialize_sfc_runtime() noexcept
    {
        if(program_ == nullptr || sfc_runtime_ == nullptr ||
           program_->sfc_runtime_bytes == 0) return;
        std::memset(sfc_runtime_, 0, program_->sfc_runtime_bytes);
        for(const SfcNetworkInfo &network : program_->sfc_networks)
            reset_sfc_network(network);
    }

    bool transition_eligible(const SfcNetworkInfo &network,
                             std::size_t transition_index) const noexcept
    {
        const SfcTransitionInfo &transition =
            network.transitions[transition_index];
        if((transition.sources.size() > 1 || transition.targets.size() > 1) &&
           !transition.simultaneous) return false;
        const unsigned char *active =
            sfc_at(network.committed_active_offset);
        for(std::uint16_t source : transition.sources)
            if(source >= network.steps.size() || active[source] == 0)
                return false;
        const unsigned char *firing = sfc_at(network.firing_offset);
        for(std::size_t prior = 0; prior < transition_index; ++prior) {
            if(firing[prior] == 0) continue;
            for(std::uint16_t source : transition.sources) {
                if(std::find(network.transitions[prior].sources.begin(),
                             network.transitions[prior].sources.end(),
                             source) !=
                   network.transitions[prior].sources.end()) return false;
            }
        }
        return true;
    }

    void begin_sfc_regions() noexcept
    {
        sfc_runner_->phase = SfcRunnerPhase::stage_active;
        sfc_runner_->network_index = 0;
        sfc_runner_->item_index = 0;
        sfc_runner_->subitem_index = 0;
        sfc_runner_->active_region = nullptr;
        sfc_runner_->work_remaining = 0;
        sfc_runner_->reducer = 0;
    }

    void finish_sfc_region() noexcept
    {
        if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
            sfc_runner_->active_region = nullptr;
            return;
        }
        SfcNetworkInfo const &network =
            program_->sfc_networks[sfc_runner_->network_index];
        if(sfc_runner_->phase == SfcRunnerPhase::transitions &&
           sfc_runner_->item_index < network.transitions.size()) {
            const SfcRegionInfo &condition =
                network.transitions[sfc_runner_->item_index].condition;
            std::uint64_t raw = 0;
            if(load_value(condition.result_offset, type_id(Type::bool_), raw) &&
               raw != 0) {
                sfc_at(network.firing_offset)[sfc_runner_->item_index] = 1U;
            }
        }
        ++sfc_runner_->item_index;
        sfc_runner_->work_remaining = 0;
        sfc_runner_->active_region = nullptr;
    }

    void update_sfc_qualifier_block(SfcNetworkInfo const &network,
                                    std::size_t index) noexcept
    {
        unsigned char *active = sfc_at(network.staged_active_offset);
        const unsigned char *old_active =
            sfc_at(network.committed_active_offset);
        const SfcActionBlockInfo &block = network.action_blocks[index];
            unsigned char *state = sfc_at(network.staged_block_offset) +
                                   index * sfc_block_bytes;
            const bool signal = block.step < network.steps.size() &&
                                active[block.step] != 0;
            const bool activation = block.step < network.steps.size() &&
                                    (signal || old_active[block.step] != 0);
            const bool previous = state[9] != 0;
            const bool rising = activation && !previous;
            std::int64_t elapsed = block_elapsed(state);
            bool pending = state[8] != 0;
            bool latched = state[10] != 0;
            std::uint8_t contribution = 0;
            const SfcQualifier qualifier =
                static_cast<SfcQualifier>(block.qualifier);
            switch(qualifier) {
            case SfcQualifier::n:
                if(signal) contribution |= action_direct;
                break;
            case SfcQualifier::s:
                if(rising) latched = true;
                if(latched) contribution |= action_store;
                break;
            case SfcQualifier::r:
                if(signal) contribution |= action_reset;
                break;
            case SfcQualifier::p:
                if(rising) contribution |= action_direct;
                break;
            case SfcQualifier::l:
                if(!signal) {
                    elapsed = 0;
                } else {
                    elapsed = rising ? task_period_ns_ :
                        saturating_add(elapsed, task_period_ns_);
                    if(block.duration_ns > 0 &&
                       elapsed <= block.duration_ns)
                        contribution |= action_direct;
                }
                break;
            case SfcQualifier::d:
                if(!signal) {
                    elapsed = 0;
                } else {
                    elapsed = rising ? task_period_ns_ :
                        saturating_add(elapsed, task_period_ns_);
                    if(elapsed >= block.duration_ns)
                        contribution |= action_direct;
                }
                break;
            case SfcQualifier::sd:
                if(rising && !latched) pending = true;
                if(pending) {
                    elapsed = saturating_add(elapsed, task_period_ns_);
                    if(elapsed >= block.duration_ns) {
                        pending = false;
                        latched = true;
                        contribution |= action_store;
                    }
                }
                if(latched) contribution |= action_store;
                break;
            case SfcQualifier::ds:
                if(!activation && !latched) {
                    elapsed = 0;
                    pending = false;
                } else if(activation && !latched) {
                    pending = true;
                    elapsed = rising ? task_period_ns_ :
                        saturating_add(elapsed, task_period_ns_);
                    if(elapsed >= block.duration_ns) {
                        pending = false;
                        latched = true;
                        contribution |= action_store;
                    }
                }
                if(latched) contribution |= action_store;
                break;
            case SfcQualifier::sl:
                if(rising) {
                    latched = true;
                    elapsed = task_period_ns_;
                    contribution |= action_store;
                } else if(latched) {
                    elapsed = saturating_add(elapsed, task_period_ns_);
                }
                if(latched && elapsed >= block.duration_ns) {
                    latched = false;
                    contribution |= action_expire;
                }
                if(latched) contribution |= action_store;
                break;
            }
            block_elapsed(state, elapsed);
            state[8] = pending ? 1U : 0U;
            state[9] = signal ? 1U : 0U;
            state[10] = latched ? 1U : 0U;
            state[11] = contribution;
    }

    void start_sfc_phase(SfcRunnerPhase phase) noexcept
    {
        sfc_runner_->phase = phase;
        sfc_runner_->network_index = 0;
        sfc_runner_->item_index = 0;
        sfc_runner_->subitem_index = 0;
        sfc_runner_->work_remaining = 0;
    }

    std::uint64_t transition_check_cost(
        const SfcNetworkInfo &network, std::size_t index) const noexcept
    {
        const SfcTransitionInfo &transition = network.transitions[index];
        std::uint64_t cost = 1U + transition.sources.size();
        for(std::size_t prior = 0; prior < index; ++prior)
            cost += 1U + transition.sources.size() *
                             network.transitions[prior].sources.size();
        return cost;
    }

    void advance_sfc_runner() noexcept
    {
        for(;;) {
            if(sfc_runner_->phase == SfcRunnerPhase::stage_vars) {
                if(sfc_runner_->item_index >= program_->vars_bytes) {
                    visible_vars_ = sfc_at(
                        program_->sfc_committed_shadow_offset());
                    sfc_runner_->phase = SfcRunnerPhase::base;
                    sfc_runner_->item_index = 0;
                    sfc_runner_->subitem_index = 0;
                    return;
                }
                if(sfc_runner_->subitem_index == 0) {
                    sfc_runtime_[sfc_runner_->item_index] = vars_[sfc_runner_->item_index];
                    sfc_runner_->subitem_index = 1;
                } else {
                    sfc_at(program_->sfc_committed_shadow_offset())
                        [sfc_runner_->item_index] = vars_[sfc_runner_->item_index];
                    ++sfc_runner_->item_index;
                    sfc_runner_->subitem_index = 0;
                    if(sfc_runner_->item_index >= program_->vars_bytes) {
                        visible_vars_ = sfc_at(
                            program_->sfc_committed_shadow_offset());
                        sfc_runner_->phase = SfcRunnerPhase::base;
                        sfc_runner_->item_index = 0;
                    }
                }
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::stage_active) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::stage_firing);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.steps.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                if(sfc_runner_->subitem_index == 0) {
                    sfc_at(network.staged_active_offset)[sfc_runner_->item_index] =
                        sfc_at(network.committed_active_offset)[sfc_runner_->item_index];
                    sfc_runner_->subitem_index = 1;
                } else {
                    sfc_at(network.shadow_active_offset)[sfc_runner_->item_index] =
                        sfc_at(network.committed_active_offset)[sfc_runner_->item_index];
                    ++sfc_runner_->item_index;
                    sfc_runner_->subitem_index = 0;
                }
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::stage_firing) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::stage_blocks);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.transitions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                sfc_at(network.firing_offset)[sfc_runner_->item_index++] = 0;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::stage_blocks) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::stage_actions);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.action_blocks.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t byte = sfc_runner_->subitem_index;
                const std::size_t offset =
                    sfc_runner_->item_index * sfc_block_bytes + byte / 2U;
                if((byte & 1U) == 0)
                    sfc_at(network.staged_block_offset)[offset] =
                        sfc_at(network.committed_block_offset)[offset];
                else
                    sfc_at(network.shadow_block_offset)[offset] =
                        sfc_at(network.committed_block_offset)[offset];
                ++sfc_runner_->subitem_index;
                if(sfc_runner_->subitem_index == sfc_block_bytes * 2U) {
                    ++sfc_runner_->item_index;
                    sfc_runner_->subitem_index = 0;
                }
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::stage_actions ||
               sfc_runner_->phase == SfcRunnerPhase::stage_direct) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(sfc_runner_->phase == SfcRunnerPhase::stage_actions
                                        ? SfcRunnerPhase::stage_direct
                                        : SfcRunnerPhase::transitions);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.actions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                if(sfc_runner_->phase == SfcRunnerPhase::stage_actions) {
                    if(sfc_runner_->subitem_index == 0) {
                        sfc_at(network.staged_action_offset)[sfc_runner_->item_index] =
                            sfc_at(network.committed_action_offset)
                                  [sfc_runner_->item_index];
                        sfc_runner_->subitem_index = 1;
                    } else {
                        sfc_at(network.shadow_action_offset)[sfc_runner_->item_index] =
                            sfc_at(network.committed_action_offset)
                                  [sfc_runner_->item_index];
                        ++sfc_runner_->item_index;
                        sfc_runner_->subitem_index = 0;
                    }
                } else {
                    sfc_at(network.direct_action_offset)[sfc_runner_->item_index] = 0;
                    ++sfc_runner_->item_index;
                }
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::transitions) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::active_updates);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.transitions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0;
                    sfc_runner_->work_remaining = 0; continue;
                }
                if(sfc_runner_->work_remaining == 0)
                    sfc_runner_->work_remaining = transition_check_cost(
                        network, sfc_runner_->item_index);
                if(--sfc_runner_->work_remaining != 0) return;
                if(transition_eligible(network, sfc_runner_->item_index)) {
                    sfc_runner_->active_region =
                        &network.transitions[sfc_runner_->item_index].condition;
                    return;
                }
                ++sfc_runner_->item_index;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::active_updates) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::qualifier_blocks);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.transitions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0;
                    sfc_runner_->subitem_index = 0; continue;
                }
                const SfcTransitionInfo &transition =
                    network.transitions[sfc_runner_->item_index];
                const bool firing =
                    sfc_at(network.firing_offset)[sfc_runner_->item_index] != 0;
                if(sfc_runner_->subitem_index == 0) {
                    ++sfc_runner_->subitem_index;
                    return;
                }
                if(sfc_runner_->subitem_index <= transition.sources.size()) {
                    if(firing)
                        sfc_at(network.staged_active_offset)
                            [transition.sources[sfc_runner_->subitem_index - 1]] = 0;
                    ++sfc_runner_->subitem_index;
                    return;
                }
                const std::size_t target = sfc_runner_->subitem_index -
                                           transition.sources.size() - 1U;
                if(target < transition.targets.size()) {
                    if(firing)
                        sfc_at(network.staged_active_offset)
                            [transition.targets[target]] = 1U;
                    ++sfc_runner_->subitem_index;
                    if(target + 1U < transition.targets.size()) return;
                }
                ++sfc_runner_->item_index;
                sfc_runner_->subitem_index = 0;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::qualifier_blocks) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::qualifier_actions);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.action_blocks.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                update_sfc_qualifier_block(network, sfc_runner_->item_index++);
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::qualifier_actions) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::actions);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.actions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0;
                    sfc_runner_->subitem_index = 0; continue;
                }
                const std::size_t blocks = network.action_blocks.size();
                if(sfc_runner_->subitem_index == 0) {
                    sfc_runner_->reducer = 0;
                    ++sfc_runner_->subitem_index;
                    return;
                }
                if(sfc_runner_->subitem_index <= blocks) {
                    const std::size_t block = sfc_runner_->subitem_index - 1U;
                    if(network.action_blocks[block].action == sfc_runner_->item_index)
                        sfc_runner_->reducer |=
                            sfc_at(network.staged_block_offset)
                                  [block * sfc_block_bytes + 11U];
                    ++sfc_runner_->subitem_index;
                    return;
                }
                unsigned char *stored = sfc_at(network.staged_action_offset);
                if(sfc_runner_->subitem_index == blocks + 1U) {
                    sfc_runner_->action_suppressed =
                        (sfc_runner_->reducer & (action_reset | action_expire)) != 0;
                    if((sfc_runner_->reducer & action_reset) != 0 ||
                       (sfc_runner_->reducer & action_expire) != 0)
                        stored[sfc_runner_->item_index] = 0;
                    else if((sfc_runner_->reducer & action_store) != 0)
                        stored[sfc_runner_->item_index] = 1U;
                    ++sfc_runner_->subitem_index;
                    return;
                }
                const std::size_t cleanup = sfc_runner_->subitem_index - blocks - 2U;
                if(cleanup < blocks) {
                    if((sfc_runner_->reducer & action_reset) != 0 &&
                       network.action_blocks[cleanup].action == sfc_runner_->item_index) {
                        unsigned char *state =
                            sfc_at(network.staged_block_offset) +
                            cleanup * sfc_block_bytes;
                        const unsigned char previous = state[9];
                        std::memset(state, 0, sfc_block_bytes);
                        state[9] = previous;
                    }
                    ++sfc_runner_->subitem_index;
                    return;
                }
                sfc_at(network.direct_action_offset)[sfc_runner_->item_index] =
                    !sfc_runner_->action_suppressed &&
                            (stored[sfc_runner_->item_index] != 0 ||
                             (sfc_runner_->reducer & action_direct) != 0)
                        ? 1U : 0U;
                ++sfc_runner_->item_index;
                sfc_runner_->subitem_index = 0;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::actions) {
                if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                    start_sfc_phase(SfcRunnerPhase::trace_exits);
                    continue;
                }
                const SfcNetworkInfo &network =
                    program_->sfc_networks[sfc_runner_->network_index];
                if(sfc_runner_->item_index >= network.actions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                if(sfc_at(network.direct_action_offset)[sfc_runner_->item_index] != 0) {
                    sfc_runner_->active_region = &network.actions[sfc_runner_->item_index].region;
                    return;
                }
                ++sfc_runner_->item_index;
                return;
            }
            break;
        }
        advance_sfc_trace_and_commit();
    }

    void append_sfc_trace(std::size_t network_index, SfcEventKind kind,
                          std::size_t entity_index, std::uint8_t qualifier,
                          std::int32_t line, std::int32_t column,
                          std::int32_t end_line,
                          std::int32_t end_column) noexcept
    {
        if(sfc_runner_->trace_size >= sfc_runner_->trace_capacity) {
            ++sfc_runner_->trace_dropped;
            return;
        }
        SfcTraceRecord &record = sfc_runner_->trace[sfc_runner_->trace_size++];
        record = SfcTraceRecord{};
        record.scan = sfc_runner_->scan;
        record.network = static_cast<std::uint32_t>(network_index);
        record.kind = kind;
        record.qualifier = qualifier;
        record.line = line;
        record.column = column;
        record.end_line = end_line;
        record.end_column = end_column;
        const std::uint32_t index = static_cast<std::uint32_t>(entity_index);
        if(kind == SfcEventKind::step_exit ||
           kind == SfcEventKind::step_enter) record.step = index;
        else if(kind == SfcEventKind::transition_fire)
            record.transition = index;
        else if(kind == SfcEventKind::qualifier_update) record.block = index;
        else if(kind == SfcEventKind::action_execute) record.action = index;
    }

    void advance_sfc_trace_and_commit() noexcept
    {
        for(;;) {
            if(sfc_runner_->phase == SfcRunnerPhase::commit_vars) {
                if(sfc_runner_->item_index < program_->vars_bytes) {
                    vars_[sfc_runner_->item_index] = sfc_runtime_[sfc_runner_->item_index];
                    ++sfc_runner_->item_index;
                    return;
                }
                start_sfc_phase(SfcRunnerPhase::commit_active);
                continue;
            }
            if(sfc_runner_->network_index >= program_->sfc_networks.size()) {
                SfcRunnerPhase next = SfcRunnerPhase::finish;
                switch(sfc_runner_->phase) {
                case SfcRunnerPhase::trace_exits:
                    next = SfcRunnerPhase::trace_transitions; break;
                case SfcRunnerPhase::trace_transitions:
                    next = SfcRunnerPhase::trace_enters; break;
                case SfcRunnerPhase::trace_enters:
                    next = SfcRunnerPhase::trace_qualifiers; break;
                case SfcRunnerPhase::trace_qualifiers:
                    next = SfcRunnerPhase::trace_actions; break;
                case SfcRunnerPhase::trace_actions:
                    next = SfcRunnerPhase::commit_vars; break;
                case SfcRunnerPhase::commit_active:
                    next = SfcRunnerPhase::commit_blocks; break;
                case SfcRunnerPhase::commit_blocks:
                    next = SfcRunnerPhase::commit_actions; break;
                case SfcRunnerPhase::commit_actions:
                    next = SfcRunnerPhase::finish; break;
                default: break;
                }
                start_sfc_phase(next);
                continue;
            }
            const SfcNetworkInfo &network =
                program_->sfc_networks[sfc_runner_->network_index];
            const unsigned char *old_active =
                sfc_at(network.committed_active_offset);
            const unsigned char *new_active =
                sfc_at(network.staged_active_offset);
            if(sfc_runner_->phase == SfcRunnerPhase::trace_exits ||
               sfc_runner_->phase == SfcRunnerPhase::trace_enters) {
                if(sfc_runner_->item_index >= network.steps.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t step = sfc_runner_->item_index++;
                const bool emit = sfc_runner_->phase == SfcRunnerPhase::trace_exits
                    ? old_active[step] != 0 && new_active[step] == 0
                    : old_active[step] == 0 && new_active[step] != 0;
                if(emit)
                    append_sfc_trace(sfc_runner_->network_index,
                                     sfc_runner_->phase == SfcRunnerPhase::trace_exits
                                         ? SfcEventKind::step_exit
                                         : SfcEventKind::step_enter,
                                     step, 0, network.steps[step].line,
                                     network.steps[step].column,
                                     network.steps[step].end_line,
                                     network.steps[step].end_column);
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::trace_transitions) {
                if(sfc_runner_->item_index >= network.transitions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t transition = sfc_runner_->item_index++;
                if(sfc_at(network.firing_offset)[transition] != 0)
                    append_sfc_trace(sfc_runner_->network_index,
                                     SfcEventKind::transition_fire,
                                     transition, 0,
                                     network.transitions[transition].line,
                                     network.transitions[transition].column,
                                     network.transitions[transition].end_line,
                                     network.transitions[transition].end_column);
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::trace_qualifiers) {
                if(sfc_runner_->item_index >= network.action_blocks.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t block = sfc_runner_->item_index++;
                append_sfc_trace(sfc_runner_->network_index,
                                 SfcEventKind::qualifier_update, block,
                                 network.action_blocks[block].qualifier,
                                 network.action_blocks[block].line,
                                 network.action_blocks[block].column,
                                 network.action_blocks[block].end_line,
                                 network.action_blocks[block].end_column);
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::trace_actions) {
                if(sfc_runner_->item_index >= network.actions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t action = sfc_runner_->item_index++;
                const unsigned char *execute =
                    sfc_at(network.direct_action_offset);
                if(execute[action] != 0)
                    append_sfc_trace(sfc_runner_->network_index,
                                     SfcEventKind::action_execute, action, 0,
                                     network.actions[action].line,
                                     network.actions[action].column,
                                     network.actions[action].end_line,
                                     network.actions[action].end_column);
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::commit_active) {
                if(sfc_runner_->item_index >= network.steps.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                sfc_at(network.committed_active_offset)[sfc_runner_->item_index] =
                    sfc_at(network.staged_active_offset)[sfc_runner_->item_index];
                ++sfc_runner_->item_index;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::commit_blocks) {
                if(sfc_runner_->item_index >= network.action_blocks.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                const std::size_t offset =
                    sfc_runner_->item_index * sfc_block_bytes + sfc_runner_->subitem_index;
                sfc_at(network.committed_block_offset)[offset] =
                    sfc_at(network.staged_block_offset)[offset];
                ++sfc_runner_->subitem_index;
                if(sfc_runner_->subitem_index == sfc_block_bytes) {
                    ++sfc_runner_->item_index;
                    sfc_runner_->subitem_index = 0;
                }
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::commit_actions) {
                if(sfc_runner_->item_index >= network.actions.size()) {
                    ++sfc_runner_->network_index; sfc_runner_->item_index = 0; continue;
                }
                sfc_at(network.committed_action_offset)[sfc_runner_->item_index] =
                    sfc_at(network.staged_action_offset)[sfc_runner_->item_index];
                ++sfc_runner_->item_index;
                return;
            }
            if(sfc_runner_->phase == SfcRunnerPhase::finish) {
                execution_vars_ = vars_;
                visible_vars_ = vars_;
                ++sfc_runner_->scan;
                if(process_image_ != nullptr)
                    process_image_->commit_scan(
                        *program_, vars_, !defer_process_image_publish_);
                sfc_runner_->phase = SfcRunnerPhase::none;
                return;
            }
        }
    }

    template <typename Storage>
    static Storage *construct_binding_storage(unsigned char *&cursor)
    {
        static_assert(alignof(Storage) <= 8U);
        Storage *storage = ::new(static_cast<void *>(cursor)) Storage{};
        cursor += sizeof(Storage);
        return storage;
    }

    void load_binding_storage(const Program &program, unsigned char *cursor)
    {
        for(std::uint8_t value =
                static_cast<std::uint8_t>(BindingStorageKind::axis_targets);
            value <=
            static_cast<std::uint8_t>(BindingStorageKind::kin_transforms);
            ++value) {
            const BindingStorageKind kind =
                static_cast<BindingStorageKind>(value);
            if(!program.uses_binding_storage(kind)) continue;
            switch(kind) {
            case BindingStorageKind::axis_targets:
                axis_targets_ =
                    construct_binding_storage<AxisTargetStorage>(cursor);
                break;
            case BindingStorageKind::group_targets:
                group_targets_ =
                    construct_binding_storage<GroupTargetStorage>(cursor);
                break;
            case BindingStorageKind::path_tables:
                path_tables_ =
                    construct_binding_storage<PathTableStorage>(cursor);
                break;
            case BindingStorageKind::path_descriptions:
                path_descriptions_ =
                    construct_binding_storage<PathDescriptionStorage>(cursor);
                break;
            case BindingStorageKind::cam_switch_tables:
                cam_switch_tables_ =
                    construct_binding_storage<CamSwitchTableStorage>(cursor);
                break;
            case BindingStorageKind::cam_switch_outputs:
                cam_switch_outputs_ =
                    construct_binding_storage<CamSwitchOutputStorage>(cursor);
                break;
            case BindingStorageKind::cam_track_options:
                cam_track_options_ =
                    construct_binding_storage<CamTrackOptionStorage>(cursor);
                break;
            case BindingStorageKind::cam_tables:
                cam_tables_ =
                    construct_binding_storage<CamTableStorage>(cursor);
                break;
            case BindingStorageKind::position_profiles:
                position_profiles_ =
                    construct_binding_storage<PositionProfileStorage>(cursor);
                break;
            case BindingStorageKind::velocity_profiles:
                velocity_profiles_ =
                    construct_binding_storage<VelocityProfileStorage>(cursor);
                break;
            case BindingStorageKind::acceleration_profiles:
                acceleration_profiles_ = construct_binding_storage<
                    AccelerationProfileStorage>(cursor);
                break;
            case BindingStorageKind::kin_transforms:
                kin_transforms_ =
                    construct_binding_storage<KinTransformStorage>(cursor);
                break;
            case BindingStorageKind::none: break;
            }
        }
    }

    template <typename Register>
    BindingError bind_named_handle(const char *name, TypeId expected,
                                   std::size_t capacity,
                                   Register register_value)
    {
        if(program_ == nullptr || name == nullptr) return BindingError::unknown;
        if(scan_started_) return BindingError::locked;
        std::size_t slot = 0;
        for(const VarInfo &var : program_->vars) {
            if(var.type_id != expected) continue;
            if(!ascii_name_equal(name, var.lower)) {
                ++slot;
                continue;
            }
            if(slot >= capacity) return BindingError::capacity_exceeded;
            std::uint64_t handle = 0;
            std::memcpy(&handle, vars_ + var.offset, sizeof(handle));
            if(handle != 0U) return BindingError::duplicate;
            const BindingError result = register_value(slot);
            if(result != BindingError::ok) return result;
            handle = static_cast<std::uint64_t>(slot) + 1U;
            std::memcpy(vars_ + var.offset, &handle, sizeof(handle));
            return BindingError::ok;
        }
        return BindingError::unknown;
    }

    template <std::size_t Capacity>
    BindingError bind_profile(const char *name, TypeId type,
                              std::array<BindingProfileSlot, Capacity> &profiles,
                              const BindingProfileSlot &decoded)
    {
        return bind_named_handle(
            name, type, profiles.size(), [&](std::size_t slot) {
                profiles[slot] = decoded;
                return BindingError::ok;
            });
    }

    static BindingTargetKind reference_kind(Type type, TypeId type_id)
    {
        if(type == Type::axis_ref || type_id == binding_type::axis_ref ||
           type_id == binding_type::mc_input_ref ||
           type_id == binding_type::mc_output_ref) {
            return BindingTargetKind::axis;
        }
        if(type == Type::group_ref || type_id == binding_type::group_ref) {
            return BindingTargetKind::group;
        }
        return BindingTargetKind::invalid;
    }

    template <typename Text>
    static bool ascii_name_equal(const char *name, const Text &lower)
    {
        std::size_t index = 0;
        while(name[index] != '\0' && index < lower.size()) {
            char value = name[index];
            if(value >= 'A' && value <= 'Z') {
                value = static_cast<char>(value - 'A' + 'a');
            }
            if(value != lower[index]) return false;
            ++index;
        }
        return index == lower.size() && name[index] == '\0';
    }

    bool resolve_reference_payload(Type type, TypeId type_id,
                                   std::int64_t &value) const
    {
        const BindingTargetKind kind = reference_kind(type, type_id);
        if(kind == BindingTargetKind::invalid) return true;
        const std::uint64_t handle = static_cast<std::uint64_t>(value);
        if(handle == 0) {
            value = 0;
            return true;
        }
        const std::size_t slot = static_cast<std::size_t>(handle - 1U);
        void *target = nullptr;
        if(kind == BindingTargetKind::axis) {
            if(slot >= axis_targets_->size()) return false;
            target = (*axis_targets_)[slot];
        } else {
            if(slot >= group_targets_->size()) return false;
            target = (*group_targets_)[slot];
        }
        if(target == nullptr) return false;
        value = static_cast<std::int64_t>(
            reinterpret_cast<std::uintptr_t>(target));
        return true;
    }

    bool encode_reference_payload(Type type, TypeId type_id,
                                  std::int64_t &value) const
    {
        const BindingTargetKind kind = reference_kind(type, type_id);
        if(kind == BindingTargetKind::invalid) return true;
        if(value == 0) return true;
        const void *target = reinterpret_cast<const void *>(
            static_cast<std::uintptr_t>(value));
        if(kind == BindingTargetKind::axis) {
            for(std::size_t slot = 0; slot < axis_targets_->size(); ++slot) {
                if((*axis_targets_)[slot] == target) {
                    value = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
        } else {
            for(std::size_t slot = 0; slot < group_targets_->size(); ++slot) {
                if((*group_targets_)[slot] == target) {
                    value = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
        }
        return false;
    }

    bool resolve_sequence_payload(
        TypeId type, std::int64_t value,
        generated::StBindingNativeSequenceValue &sequence) const
    {
        if(value == 0) {
            sequence = {};
            return true;
        }
        if(value < 0) return false;
        const std::uint64_t handle = static_cast<std::uint64_t>(value);
        const std::size_t slot = static_cast<std::size_t>(handle - 1U);
        switch(type) {
        case binding_type::mc_path_table:
            if(slot >= path_tables_->size() ||
               (*path_tables_)[slot] == nullptr) {
                return false;
            }
            sequence = {(*path_tables_)[slot], 1U, false};
            return true;
        case binding_type::mc_path_description:
            if(slot >= path_descriptions_->size() ||
               (*path_descriptions_)[slot].count == 0U) {
                return false;
            }
            sequence = {&(*path_descriptions_)[slot], 1U, false};
            return true;
        case binding_type::mc_cam_switch_table_view:
            if(slot >= cam_switch_tables_->size() ||
               (*cam_switch_tables_)[slot].data == nullptr) {
                return false;
            }
            sequence = {(*cam_switch_tables_)[slot].data,
                        (*cam_switch_tables_)[slot].size, false};
            return true;
        case binding_type::mc_cam_switch_outputs_view:
            if(slot >= cam_switch_outputs_->size() ||
               (*cam_switch_outputs_)[slot].data == nullptr) {
                return false;
            }
            sequence = {(*cam_switch_outputs_)[slot].data,
                        (*cam_switch_outputs_)[slot].size, false};
            return true;
        case binding_type::mc_cam_track_options_view:
            if(slot >= cam_track_options_->size() ||
               (*cam_track_options_)[slot].data == nullptr) {
                return false;
            }
            sequence = {(*cam_track_options_)[slot].data,
                        (*cam_track_options_)[slot].size, false};
            return true;
        case binding_type::mc_cam_table_view:
            if(slot >= cam_tables_->size() ||
               (*cam_tables_)[slot].points == nullptr) {
                return false;
            }
            sequence = {(*cam_tables_)[slot].points,
                        (*cam_tables_)[slot].size,
                        (*cam_tables_)[slot].periodic};
            return true;
        case binding_type::mc_time_position:
            return resolve_profile_payload(*position_profiles_, slot,
                                           sequence);
        case binding_type::mc_time_velocity:
            return resolve_profile_payload(*velocity_profiles_, slot,
                                           sequence);
        case binding_type::mc_time_acceleration:
            return resolve_profile_payload(*acceleration_profiles_, slot,
                                           sequence);
        default: return false;
        }
    }

    bool resolve_tagged_reference_payload(
        TypeId type, std::int64_t value,
        generated::StBindingNativeTaggedReferenceValue &tagged) const
    {
        if(type != binding_type::mc_kin_transform_ref) return false;
        if(value == 0) {
            tagged = {};
            return true;
        }
        if(value < 0) return false;
        const std::size_t slot = static_cast<std::size_t>(
            static_cast<std::uint64_t>(value) - 1U);
        if(slot >= kin_transforms_->size()) return false;
        const axis::KinTransformRef &transform = (*kin_transforms_)[slot];
        switch(transform.kind) {
        case axis::KinTransformKind::kinematics:
            if(transform.kinematics == nullptr || transform.pose != nullptr) {
                return false;
            }
            tagged = {
                transform.kinematics,
                generated::StBindingNativeTaggedReferenceTag::kinematics};
            return true;
        case axis::KinTransformKind::pose:
            if(transform.pose == nullptr || transform.kinematics != nullptr) {
                return false;
            }
            tagged = {transform.pose,
                      generated::StBindingNativeTaggedReferenceTag::pose};
            return true;
        case axis::KinTransformKind::none:
        default: return false;
        }
    }

    bool encode_tagged_reference_payload(
        TypeId type,
        const generated::StBindingNativeTaggedReferenceValue &tagged,
        std::int64_t &handle) const
    {
        if(type != binding_type::mc_kin_transform_ref) return false;
        if(tagged.tag ==
               generated::StBindingNativeTaggedReferenceTag::none &&
           tagged.data == nullptr) {
            handle = 0;
            return true;
        }
        for(std::size_t slot = 0; slot < kin_transforms_->size(); ++slot) {
            const axis::KinTransformRef &transform = (*kin_transforms_)[slot];
            const bool matches =
                (tagged.tag ==
                     generated::StBindingNativeTaggedReferenceTag::kinematics &&
                 transform.kind == axis::KinTransformKind::kinematics &&
                 transform.kinematics == tagged.data &&
                 transform.pose == nullptr) ||
                (tagged.tag ==
                     generated::StBindingNativeTaggedReferenceTag::pose &&
                 transform.kind == axis::KinTransformKind::pose &&
                 transform.pose == tagged.data &&
                 transform.kinematics == nullptr);
            if(matches) {
                handle = static_cast<std::int64_t>(slot + 1U);
                return true;
            }
        }
        return false;
    }

    bool encode_sequence_payload(
        TypeId type, const generated::StBindingNativeSequenceValue &sequence,
        std::int64_t &handle) const
    {
        if(sequence.data == nullptr && sequence.count == 0U &&
           !sequence.flag) {
            handle = 0;
            return true;
        }
        switch(type) {
        case binding_type::mc_path_table:
            if(sequence.count != 1U || sequence.flag) return false;
            return find_sequence_handle(*path_tables_, sequence.data, handle);
        case binding_type::mc_path_description:
            if(sequence.count != 1U || sequence.flag) return false;
            for(std::size_t slot = 0; slot < path_descriptions_->size();
                ++slot) {
                if((*path_descriptions_)[slot].count != 0U &&
                   &(*path_descriptions_)[slot] == sequence.data) {
                    handle = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
            return false;
        case binding_type::mc_cam_switch_table_view:
            if(sequence.flag) return false;
            for(std::size_t slot = 0; slot < cam_switch_tables_->size();
                ++slot) {
                const auto &view = (*cam_switch_tables_)[slot];
                if(view.data == sequence.data && view.size == sequence.count) {
                    handle = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
            return false;
        case binding_type::mc_cam_switch_outputs_view:
            if(sequence.flag) return false;
            for(std::size_t slot = 0; slot < cam_switch_outputs_->size();
                ++slot) {
                const auto &view = (*cam_switch_outputs_)[slot];
                if(view.data == sequence.data &&
                   view.size == sequence.count) {
                    handle = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
            return false;
        case binding_type::mc_cam_track_options_view:
            if(sequence.flag) return false;
            for(std::size_t slot = 0; slot < cam_track_options_->size();
                ++slot) {
                const auto &view = (*cam_track_options_)[slot];
                if(view.data == sequence.data &&
                   view.size == sequence.count) {
                    handle = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
            return false;
        case binding_type::mc_cam_table_view:
            for(std::size_t slot = 0; slot < cam_tables_->size(); ++slot) {
                const auto &view = (*cam_tables_)[slot];
                if(view.points == sequence.data && view.size == sequence.count &&
                   view.periodic == sequence.flag) {
                    handle = static_cast<std::int64_t>(slot + 1U);
                    return true;
                }
            }
            return false;
        case binding_type::mc_time_position:
            if(sequence.flag) return false;
            return find_profile_handle(*position_profiles_, sequence, handle);
        case binding_type::mc_time_velocity:
            if(sequence.flag) return false;
            return find_profile_handle(*velocity_profiles_, sequence, handle);
        case binding_type::mc_time_acceleration:
            if(sequence.flag) return false;
            return find_profile_handle(*acceleration_profiles_, sequence,
                                       handle);
        default: return false;
        }
    }

    template <std::size_t Capacity>
    static bool resolve_profile_payload(
        const std::array<BindingProfileSlot, Capacity> &profiles,
        std::size_t slot,
        generated::StBindingNativeSequenceValue &sequence)
    {
        if(slot >= profiles.size() || profiles[slot].count == 0U) return false;
        sequence = {profiles[slot].segments.data(), profiles[slot].count,
                    false};
        return true;
    }

    template <typename Pointer, std::size_t Capacity>
    static bool find_sequence_handle(const std::array<Pointer, Capacity> &values,
                                     const void *data, std::int64_t &handle)
    {
        for(std::size_t slot = 0; slot < values.size(); ++slot) {
            if(values[slot] == data) {
                handle = static_cast<std::int64_t>(slot + 1U);
                return true;
            }
        }
        return false;
    }

    template <std::size_t Capacity>
    static bool find_profile_handle(
        const std::array<BindingProfileSlot, Capacity> &profiles,
        const generated::StBindingNativeSequenceValue &sequence,
        std::int64_t &handle)
    {
        for(std::size_t slot = 0; slot < profiles.size(); ++slot) {
            if(profiles[slot].count == sequence.count &&
               profiles[slot].segments.data() == sequence.data) {
                handle = static_cast<std::int64_t>(slot + 1U);
                return true;
            }
        }
        return false;
    }

    static bool rd16(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint16_t &value)
    {
        if(pc + 2 > size) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(code[pc]) |
            (static_cast<std::uint16_t>(code[pc + 1]) << 8));
        pc += 2;
        return true;
    }

    static std::uint32_t read_le32(const unsigned char *bytes)
    {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) |
               (static_cast<std::uint32_t>(bytes[3]) << 24U);
    }

    static bool next_utf8(const unsigned char *bytes, std::uint32_t length,
                          std::uint32_t &at, std::uint32_t &scalar)
    {
        if(at >= length) {
            return false;
        }
        const std::uint8_t lead = bytes[at++];
        unsigned count = 0;
        if(lead < 0x80U) {
            scalar = lead;
            return true;
        }
        if(lead >= 0xC2U && lead <= 0xDFU) {
            scalar = lead & 0x1FU;
            count = 1;
        } else if(lead >= 0xE0U && lead <= 0xEFU) {
            scalar = lead & 0x0FU;
            count = 2;
        } else if(lead >= 0xF0U && lead <= 0xF4U) {
            scalar = lead & 0x07U;
            count = 3;
        } else {
            return false;
        }
        if(count > length - at) {
            return false;
        }
        for(unsigned i = 0; i < count; ++i) {
            const std::uint8_t part = bytes[at++];
            if((part & 0xC0U) != 0x80U) {
                return false;
            }
            if(i == 0U &&
               ((lead == 0xE0U && part < 0xA0U) ||
                (lead == 0xEDU && part > 0x9FU) ||
                (lead == 0xF0U && part < 0x90U) ||
                (lead == 0xF4U && part > 0x8FU))) {
                return false;
            }
            scalar = (scalar << 6U) | (part & 0x3FU);
        }
        return scalar <= 0x10FFFFU &&
               !(scalar >= 0xD800U && scalar <= 0xDFFFU);
    }

    static bool valid_string_object(const unsigned char *object,
                                    const TypeDesc &desc)
    {
        const std::uint32_t length = read_le32(object);
        if(length > desc.string.capacity) {
            return false;
        }
        if(desc.kind == TypeKind::wstring) {
            for(std::uint32_t i = 0; i < length; ++i) {
                const std::uint32_t scalar =
                    read_le32(object + 4U + i * 4U);
                if(scalar > 0x10FFFFU ||
                   (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
                    return false;
                }
            }
            return true;
        }
        if(desc.kind != TypeKind::string) {
            return false;
        }
        std::uint32_t at = 0;
        while(at < length) {
            std::uint32_t scalar = 0;
            if(!next_utf8(object + 4U, length, at, scalar)) {
                return false;
            }
        }
        return true;
    }

    static int compare_strings(const unsigned char *left,
                               const TypeDesc &left_desc,
                               const unsigned char *right,
                               const TypeDesc &right_desc)
    {
        if(!valid_string_object(left, left_desc) ||
           !valid_string_object(right, right_desc)) {
            return 2;
        }
        const std::uint32_t left_length = read_le32(left);
        const std::uint32_t right_length = read_le32(right);
        if(left_desc.kind == TypeKind::wstring) {
            const std::uint32_t common = left_length < right_length
                                             ? left_length
                                             : right_length;
            for(std::uint32_t i = 0; i < common; ++i) {
                const std::uint32_t a = read_le32(left + 4U + i * 4U);
                const std::uint32_t b = read_le32(right + 4U + i * 4U);
                if(a != b) {
                    return a < b ? -1 : 1;
                }
            }
            return left_length == right_length ? 0
                 : left_length < right_length ? -1 : 1;
        }
        std::uint32_t left_at = 0;
        std::uint32_t right_at = 0;
        while(left_at < left_length && right_at < right_length) {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            if(!next_utf8(left + 4U, left_length, left_at, a) ||
               !next_utf8(right + 4U, right_length, right_at, b)) {
                return 2;
            }
            if(a != b) {
                return a < b ? -1 : 1;
            }
        }
        return left_at == left_length && right_at == right_length ? 0
             : left_at == left_length ? -1 : 1;
    }

    static bool rd32(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint32_t &value)
    {
        if(pc + 4 > size) {
            return false;
        }
        value = static_cast<std::uint32_t>(code[pc]) |
                (static_cast<std::uint32_t>(code[pc + 1]) << 8) |
                (static_cast<std::uint32_t>(code[pc + 2]) << 16) |
                (static_cast<std::uint32_t>(code[pc + 3]) << 24);
        pc += 4;
        return true;
    }

    static bool rd64(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint64_t &value)
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if(!rd32(code, size, pc, low) || !rd32(code, size, pc, high)) {
            return false;
        }
        value = static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U);
        return true;
    }

    bool load_raw64_at(const unsigned char *storage, std::uint32_t offset,
                       std::uint64_t &value) const
    {
        if(program_ == nullptr || offset > program_->vars_bytes ||
           8U > program_->vars_bytes - offset) {
            return false;
        }
        std::memcpy(&value, storage + offset, sizeof(value));
        return true;
    }

    bool load_raw64(std::uint32_t offset, std::uint64_t &value) const
    {
        return load_raw64_at(execution_vars_, offset, value);
    }

    bool store_raw64(std::uint32_t offset, std::uint64_t value)
    {
        if(program_ == nullptr || offset > program_->vars_bytes ||
           8U > program_->vars_bytes - offset) {
            return false;
        }
        std::memcpy(execution_vars_ + offset, &value, sizeof(value));
        return true;
    }

    bool load_value(std::uint32_t offset, TypeId type_id,
                    std::uint64_t &value) const
    {
        return load_value_at(execution_vars_, offset, type_id, value);
    }

    bool load_value_at(const unsigned char *storage, std::uint32_t offset,
                       TypeId type_id, std::uint64_t &value) const
    {
        if(program_ == nullptr || storage == nullptr) {
            return false;
        }
        const TypeDesc *desc = program_->types.get(type_id);
        if(desc == nullptr) {
            return type_id == invalid_type_id &&
                   load_raw64_at(storage, offset, value);
        }
        if(desc->kind == TypeKind::array || desc->kind == TypeKind::struct_ ||
           desc->size == 0 || desc->size > 8 ||
           offset > program_->vars_bytes ||
           desc->size > program_->vars_bytes - offset) {
            return false;
        }
        std::uint64_t raw = 0;
        for(std::uint64_t i = 0; i < desc->size; ++i) {
            raw |= static_cast<std::uint64_t>(storage[offset + i]) <<
                   (i * 8U);
        }
        TypeId base_id = type_id;
        if(desc->kind == TypeKind::enum_) {
            base_id = desc->enum_base;
        } else if(desc->kind == TypeKind::subrange) {
            base_id = desc->subrange.base;
        }
        const Type type = type_from_id(base_id);
        if(type == Type::real) {
            const std::uint32_t raw32 = static_cast<std::uint32_t>(raw);
            float number = 0.0F;
            std::memcpy(&number, &raw32, sizeof(number));
            value = detail::double_bits(static_cast<double>(number));
            return true;
        }
        if((is_signed_int(type) || type == Type::date) && desc->size < 8) {
            const unsigned bits = static_cast<unsigned>(desc->size * 8U);
            const std::uint64_t sign = std::uint64_t{1} << (bits - 1U);
            raw = (raw ^ sign) - sign;
        }
        value = raw;
        return true;
    }

    bool store_value(std::uint32_t offset, TypeId type_id,
                     std::uint64_t value)
    {
        if(program_ == nullptr) {
            return false;
        }
        if(process_image_ != nullptr) {
            (void)process_image_->forced_store(*program_, offset, type_id,
                                               value);
        }
        const TypeDesc *desc = program_->types.get(type_id);
        if(desc == nullptr) {
            return type_id == invalid_type_id && store_raw64(offset, value);
        }
        if(desc->kind == TypeKind::array || desc->kind == TypeKind::struct_ ||
           desc->size == 0 || desc->size > 8 ||
           offset > program_->vars_bytes ||
           desc->size > program_->vars_bytes - offset) {
            return false;
        }
        TypeId base_id = type_id;
        if(desc->kind == TypeKind::enum_) {
            base_id = desc->enum_base;
        } else if(desc->kind == TypeKind::subrange) {
            base_id = desc->subrange.base;
        }
        if(type_from_id(base_id) == Type::real) {
            const float number =
                static_cast<float>(detail::bits_double(value));
            std::uint32_t raw = 0;
            std::memcpy(&raw, &number, sizeof(raw));
            value = raw;
        }
        for(std::uint64_t i = 0; i < desc->size; ++i) {
            execution_vars_[offset + i] =
                static_cast<unsigned char>(value >> (i * 8U));
        }
        if(process_image_ != nullptr)
            process_image_->mark_store(*program_, offset, type_id);
        return true;
    }

    static std::int64_t as_i64(std::uint64_t raw)
    {
        return static_cast<std::int64_t>(raw);
    }

    static std::uint64_t as_u64(std::int64_t value)
    {
        return static_cast<std::uint64_t>(value);
    }

    ScanError latch(ScanError error)
    {
        if(sfc_runner_->active_region != nullptr &&
           sfc_runner_->network_index < program_->sfc_networks.size()) {
            sfc_runner_->fault_network_index =
                static_cast<std::uint32_t>(sfc_runner_->network_index);
            sfc_runner_->fault_element_index =
                static_cast<std::uint32_t>(sfc_runner_->item_index);
            sfc_runner_->fault_element_kind =
                sfc_runner_->phase == SfcRunnerPhase::transitions
                    ? TaskFaultElementKind::transition
                    : TaskFaultElementKind::action;
        }
        if(sfc_commit_started()) rollback_sfc_commit();
        execution_vars_ = vars_;
        visible_vars_ = vars_;
        sfc_runner_->trace_size = sfc_runner_->trace_scan_begin_size;
        sfc_runner_->trace_dropped = sfc_runner_->trace_scan_begin_dropped;
        sfc_runner_->active_region = nullptr;
        fault_ = error;
        execution_state_ = InstanceState::faulted;
        return error;
    }

    const Program *program_ = nullptr;
    ProcessImage *process_image_ = nullptr;
    unsigned char *vars_ = nullptr;
    const unsigned char *visible_vars_ = nullptr;
    unsigned char *execution_vars_ = nullptr;
    unsigned char *sfc_runtime_ = nullptr;
    SfcRunnerStorage *sfc_runner_ = nullptr;
    unsigned char *fb_area_ = nullptr;
    std::uint64_t *stack_ = nullptr;
    ScanError fault_ = ScanError::ok;
    InstanceState execution_state_ = InstanceState::idle;
    std::size_t pc_ = 0;
    std::uint32_t instruction_pc_ = 0;
    std::int32_t sp_ = 0;
    std::int64_t remaining_budget_ = 0;
    bool scan_started_ = false;
    bool defer_process_image_publish_ = false;
    std::int64_t task_period_ns_ = 0;
    std::int64_t utc_dt_ns_ = 0;
    RuntimeDebugHooks *debug_hooks_ = nullptr;
    std::uint32_t debug_mapping_ = 0;
    const SourceMapEntry *last_debug_entry_ = nullptr;
    InstructionId last_debug_instruction_id_{};
    AxisTargetStorage *axis_targets_ = nullptr;
    GroupTargetStorage *group_targets_ = nullptr;
    PathTableStorage *path_tables_ = nullptr;
    PathDescriptionStorage *path_descriptions_ = nullptr;
    CamSwitchTableStorage *cam_switch_tables_ = nullptr;
    CamSwitchOutputStorage *cam_switch_outputs_ = nullptr;
    CamTrackOptionStorage *cam_track_options_ = nullptr;
    CamTableStorage *cam_tables_ = nullptr;
    PositionProfileStorage *position_profiles_ = nullptr;
    VelocityProfileStorage *velocity_profiles_ = nullptr;
    AccelerationProfileStorage *acceleration_profiles_ = nullptr;
    KinTransformStorage *kin_transforms_ = nullptr;
};

} // namespace plcopen::core::st
