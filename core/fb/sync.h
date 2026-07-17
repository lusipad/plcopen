#pragma once

#include <cmath>
#include <cstdint>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// MC_GearIn / MC_GearInPos / MC_CamIn require master and slave to be members
// of the same enabled group (KB boundary carried over from the v0.x tests).
inline bool same_enabled_group(const axis::AxisModel &master, const axis::AxisModel &slave)
{
    void *owner = slave.group_owner();
    if(owner == nullptr || owner != master.group_owner()) {
        return false;
    }
    const auto *group = static_cast<const axis::AxisGroup *>(owner);
    return group->status() != axis::GroupStatus::disabled;
}

// Shared facade state for master/slave synchronization commands. in_sync maps
// to the v0.x InGear/InSync outputs; start_sync pulses one cycle at approach
// start and at sync entry.
class SyncExecuteFb
{
public:
    axis::AxisModel *master_ref = nullptr;
    axis::AxisModel *slave_ref = nullptr;
    bool execute = false;
    bool continuous_update = false;
    MotionOutputs outputs{};
    bool in_sync = false;
    bool start_sync = false;

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        const bool falling = !execute && last_execute_;
        last_execute_ = execute;
        if(rising) {
            clear(outputs);
            in_sync = false;
            start_sync = false;
            tracked_command_id_ = 0;
            last_phase_ = axis::SyncPhase::idle;
            continuous_update_enabled_ = continuous_update;
            terminal_low_cycle_ = false;
        } else if(!execute && terminal_low_cycle_) {
            clear(outputs);
            tracked_command_id_ = 0;
            terminal_low_cycle_ = false;
        } else if(falling &&
                  (outputs.done || outputs.command_aborted || outputs.error)) {
            clear(outputs);
            tracked_command_id_ = 0;
        }
        return rising;
    }

    void accept(rt::Result<std::uint32_t> accepted)
    {
        outputs.command_accepted = false;
        outputs.command_aborted = false;
        outputs.error = false;
        outputs.error_id = rt::ErrorCode::ok;
        in_sync = false;
        start_sync = false;
        last_phase_ = axis::SyncPhase::idle;
        if(!accepted) {
            outputs.error = true;
            outputs.error_id = accepted.error();
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        tracked_command_id_ = accepted.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
    }

    void observe_sync(bool pulse_approach = true)
    {
        start_sync = false;
        if(tracked_command_id_ == 0 || slave_ref == nullptr) {
            return;
        }
        const rt::ErrorCode command_error = slave_ref->command_error(tracked_command_id_);
        if(command_error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = command_error;
            outputs.command_aborted = false;
            outputs.busy = false;
            outputs.active = false;
            in_sync = false;
            terminal_low_cycle_ = !execute;
            return;
        }
        if(slave_ref->sync_command_id() != tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            in_sync = false;
            terminal_low_cycle_ = !execute;
            return;
        }

        const axis::SyncPhase phase = slave_ref->sync_phase();
        outputs.busy = true;
        outputs.active = phase != axis::SyncPhase::queued;
        in_sync = phase == axis::SyncPhase::engaged;
        start_sync = phase != last_phase_ &&
                     (phase == axis::SyncPhase::engaged ||
                      (pulse_approach && phase == axis::SyncPhase::approaching));
        last_phase_ = phase;
    }

    std::uint32_t tracked_command_id_ = 0;

    bool continuous_update_allowed() const
    {
        return continuous_update_enabled_;
    }

private:
    bool last_execute_ = false;
    bool continuous_update_enabled_ = false;
    bool terminal_low_cycle_ = false;
    axis::SyncPhase last_phase_ = axis::SyncPhase::idle;
};

class FbSyncAxisToGroup : public SyncExecuteFb
{
public:
    axis::AxisGroup *group_ref = nullptr;
    double ratio_numerator = 1.0;
    double ratio_denominator = 1.0;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr || slave_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                accept(group_ref->sync_axis_to_group(
                    *slave_ref, ratio_numerator, ratio_denominator, acceleration,
                    deceleration, jerk, buffer_mode));
            }
        }
        observe_sync();
    }
};

class FbGearIn : public SyncExecuteFb
{
public:
    double ratio_numerator = 1.0;
    double ratio_denominator = 1.0;
    axis::MasterValueSource master_value_source = axis::MasterValueSource::command;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool in_gear = false;

    void call()
    {
        if(rising_edge()) {
            submit_gear(make_command());
        } else {
            update_gear_ratio();
        }
        observe_sync(false);
        in_gear = in_sync;
    }

protected:
    void submit_gear(const axis::GearInCommand &command)
    {
        if(master_ref == nullptr || slave_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        if(!same_enabled_group(*master_ref, *slave_ref)) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed));
            return;
        }
        accept(slave_ref->gear_in(command));
    }

    void update_gear_ratio()
    {
        if(!execute || !continuous_update_allowed() || tracked_command_id_ == 0 ||
           slave_ref == nullptr) {
            return;
        }
        const rt::ErrorCode updated = slave_ref->gear_update(ratio_numerator, ratio_denominator);
        if(updated != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = updated;
        }
    }

    axis::GearInCommand make_command() const
    {
        axis::GearInCommand command{};
        command.master = master_ref;
        command.ratio_numerator = ratio_numerator;
        command.ratio_denominator = ratio_denominator;
        command.source = master_value_source;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        return command;
    }
};

class FbGearInPos : public FbGearIn
{
public:
    double master_sync_position = 0.0;
    double slave_sync_position = 0.0;
    double master_start_distance = 0.0;
    axis::SyncMode sync_mode = axis::SyncMode::shortest;
    double velocity = 0.0;

    void call()
    {
        if(rising_edge()) {
            axis::GearInCommand command = make_command();
            command.position_sync = true;
            command.master_sync_position = master_sync_position;
            command.slave_sync_position = slave_sync_position;
            command.master_start_distance = master_start_distance;
            command.sync_mode = sync_mode;
            command.approach_velocity = velocity;
            submit_gear(command);
        } else {
            update_gear_ratio();
        }
        observe_sync();
        in_gear = in_sync;
    }
};

class FbGearOut
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    bool done = false;
    bool error = false;
    MotionOutputs outputs{};

    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(axis_ref == nullptr) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::ErrorCode detached = axis_ref->sync_out();
        if(detached != rt::ErrorCode::ok) {
            fail(detached);
            return;
        }
        done = true;
        outputs.done = true;
    }

private:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            done = false;
            error = false;
        }
        return rising;
    }

    void fail(rt::ErrorCode code)
    {
        error = true;
        outputs.error = true;
        outputs.error_id = code;
        done = false;
        outputs.done = false;
    }

    bool last_execute_ = false;
};

using FbCamOut = FbGearOut;

class FbCamTableSelect
{
public:
    axis::AxisModel *master_ref = nullptr;
    axis::AxisModel *slave_ref = nullptr;
    exec::CamTableView cam_table{};
    bool periodic = false;
    bool master_absolute = true;
    bool slave_absolute = true;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;
    bool execute = false;
    bool done = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    std::uint32_t cam_table_id = 0;
    exec::CamTableView cam_table_selected{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            done = false;
            busy = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(!rising) {
            return;
        }
        busy = false;
        if(master_ref == nullptr || slave_ref == nullptr || !cam_table.valid()) {
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            done = false;
            cam_table_selected = {};
            return;
        }
        if(!same_enabled_group(*master_ref, *slave_ref)) {
            error = true;
            error_id = rt::ErrorCode::precondition_failed;
            done = false;
            cam_table_selected = {};
            return;
        }
        if(execution_mode != axis::ExecutionMode::immediately) {
            error = true;
            error_id = rt::ErrorCode::unsupported;
            done = false;
            cam_table_selected = {};
            return;
        }
        const rt::Result<std::uint32_t> selected =
            slave_ref->select_cam_table(master_ref, cam_table, periodic,
                                        master_absolute, slave_absolute,
                                        cam_table_id);
        if(!selected) {
            error = true;
            error_id = selected.error();
            done = false;
            cam_table_selected = {};
            return;
        }
        cam_table_selected = cam_table;
        cam_table_selected.periodic = periodic;
        cam_table_id = selected.value();
        done = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    bool last_execute_ = false;
};

class FbCamIn : public SyncExecuteFb
{
public:
    exec::CamTableView cam_table{};
    double master_offset = 0.0;
    double master_scaling = 1.0;
    double slave_offset = 0.0;
    double slave_scaling = 1.0;
    double master_sync_position = 0.0;
    double master_start_distance = 0.0;
    double velocity = 0.0;
    axis::CamStartMode start_mode = axis::CamStartMode::absolute;
    axis::MasterValueSource master_value_source = axis::MasterValueSource::command;
    std::uint32_t cam_table_id = 0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool end_of_profile = false;

    void call()
    {
        if(rising_edge()) {
            reset_profile_tracking();
            submit();
        } else if(execute && continuous_update_allowed() && tracked_command_id_ != 0 &&
                  slave_ref != nullptr) {
            const double effective_master_offset = master_offset + selected_master_origin_;
            const double effective_slave_offset =
                slave_offset + selected_slave_origin_ + relative_slave_bias_;
            const rt::ErrorCode updated = slave_ref->cam_update(effective_master_offset,
                                                                master_scaling,
                                                                effective_slave_offset,
                                                                slave_scaling);
            if(updated != rt::ErrorCode::ok) {
                outputs.error = true;
                outputs.error_id = updated;
            } else {
                profile_master_offset_ = effective_master_offset;
                profile_master_scaling_ = master_scaling;
            }
        }
        observe_sync();
        update_end_of_profile();
    }

private:
    void submit()
    {
        if(master_ref == nullptr || slave_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        if(!same_enabled_group(*master_ref, *slave_ref)) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed));
            return;
        }
        if(start_mode == axis::CamStartMode::ramp_in) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported));
            return;
        }
        axis::CamInCommand command{};
        command.master = master_ref;
        command.table = cam_table;
        command.master_offset = master_offset;
        command.master_scaling = master_scaling;
        command.slave_offset = slave_offset;
        command.slave_scaling = slave_scaling;
        selected_master_origin_ = 0.0;
        selected_slave_origin_ = 0.0;
        relative_slave_bias_ = 0.0;
        if(cam_table_id != 0) {
            const rt::Result<axis::CamTableSelection> selected =
                slave_ref->cam_table_selection(cam_table_id);
            if(!selected || selected.value().master != master_ref) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const axis::CamTableSelection selection = selected.value();
            command.table = selection.table;
            if(!selection.master_absolute) {
                selected_master_origin_ = selection.master_origin;
                command.master_offset += selected_master_origin_;
            }
            if(!selection.slave_absolute) {
                selected_slave_origin_ = selection.slave_origin;
                command.slave_offset += selected_slave_origin_;
            }
        }
        if(start_mode == axis::CamStartMode::relative) {
            if(!command.table.valid() || !std::isfinite(command.master_scaling) ||
               command.master_scaling == 0.0 || !std::isfinite(command.slave_scaling)) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const axis::AxisSnapshot &master_snapshot = master_ref->snapshot();
            const double master_position =
                master_value_source == axis::MasterValueSource::actual
                    ? master_snapshot.actual_position
                    : master_snapshot.command_position;
            const double table_input =
                (master_position - command.master_offset) / command.master_scaling;
            const rt::Result<double> sampled = command.table.sample(table_input);
            if(!sampled) {
                accept(rt::Result<std::uint32_t>::failure(sampled.error()));
                return;
            }
            command.slave_offset = slave_ref->snapshot().command_position -
                                   command.slave_scaling * sampled.value();
            relative_slave_bias_ =
                command.slave_offset - slave_offset - selected_slave_origin_;
        }
        command.source = master_value_source;
        command.buffer_mode = buffer_mode;
        command.master_sync_position = master_sync_position;
        command.master_start_distance = master_start_distance;
        command.approach_velocity = velocity;
        const rt::Result<std::uint32_t> accepted = slave_ref->cam_in(command);
        if(accepted) {
            profile_table_ = command.table;
            profile_master_offset_ = command.master_offset;
            profile_master_scaling_ = command.master_scaling;
            profile_source_ = command.source;
        }
        accept(accepted);
    }

    void reset_profile_tracking()
    {
        end_of_profile = false;
        profile_period_valid_ = false;
        profile_table_ = {};
    }

    void update_end_of_profile()
    {
        end_of_profile = false;
        if(!in_sync || master_ref == nullptr || !profile_table_.valid() ||
           profile_master_scaling_ == 0.0) {
            profile_period_valid_ = false;
            return;
        }
        const axis::AxisSnapshot &snapshot = master_ref->snapshot();
        const double master_position = profile_source_ == axis::MasterValueSource::actual
                                           ? snapshot.actual_position
                                           : snapshot.command_position;
        const double input =
            (master_position - profile_master_offset_) / profile_master_scaling_;
        const double first = profile_table_.points[0].master;
        const double last = profile_table_.points[profile_table_.size - 1].master;
        if(!profile_table_.periodic) {
            end_of_profile = input < first || input > last;
            return;
        }
        const double span = last - first;
        const std::int64_t period = static_cast<std::int64_t>(std::floor((input - first) / span));
        if(profile_period_valid_) {
            end_of_profile = period != profile_period_;
        }
        profile_period_ = period;
        profile_period_valid_ = true;
    }

    exec::CamTableView profile_table_{};
    double profile_master_offset_ = 0.0;
    double profile_master_scaling_ = 1.0;
    axis::MasterValueSource profile_source_ = axis::MasterValueSource::command;
    double selected_master_origin_ = 0.0;
    double selected_slave_origin_ = 0.0;
    double relative_slave_bias_ = 0.0;
    std::int64_t profile_period_ = 0;
    bool profile_period_valid_ = false;
};

class FbCombineAxes : public SyncExecuteFb
{
public:
    axis::AxisModel *master1_ref = nullptr;
    axis::AxisModel *master2_ref = nullptr;
    axis::CombineMode combine_mode = axis::CombineMode::add_axes;
    double ratio_numerator_m1 = 1.0;
    double ratio_denominator_m1 = 1.0;
    double ratio_numerator_m2 = 1.0;
    double ratio_denominator_m2 = 1.0;
    axis::MasterValueSource master_value_source_m1 = axis::MasterValueSource::command;
    axis::MasterValueSource master_value_source_m2 = axis::MasterValueSource::command;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit();
        } else if(execute && continuous_update_allowed() && tracked_command_id_ != 0 &&
                  slave_ref != nullptr) {
            const rt::ErrorCode updated = slave_ref->combine_update(combine_mode,
                                                                    ratio_numerator_m1,
                                                                    ratio_denominator_m1,
                                                                    ratio_numerator_m2,
                                                                    ratio_denominator_m2);
            if(updated != rt::ErrorCode::ok) {
                outputs.error = true;
                outputs.error_id = updated;
            }
        }
        observe_sync();
    }

private:
    void submit()
    {
        if(master1_ref == nullptr || master2_ref == nullptr || slave_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        axis::CombineAxesCommand command{};
        command.master1 = master1_ref;
        command.master2 = master2_ref;
        command.mode = combine_mode;
        command.ratio_numerator_m1 = ratio_numerator_m1;
        command.ratio_denominator_m1 = ratio_denominator_m1;
        command.ratio_numerator_m2 = ratio_numerator_m2;
        command.ratio_denominator_m2 = ratio_denominator_m2;
        command.source_m1 = master_value_source_m1;
        command.source_m2 = master_value_source_m2;
        command.buffer_mode = buffer_mode;
        accept(slave_ref->combine_in(command));
    }
};

// Phase inputs are latched on the rising edge; changing them while the
// transition runs has no effect (v0.x latching boundary).
class PhasingFb
{
public:
    axis::AxisModel *master_ref = nullptr;
    axis::AxisModel *slave_ref = nullptr;
    double phase_shift = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool execute = false;
    MotionOutputs outputs{};
    double absolute_phase_shift = 0.0;
    double covered_phase_shift = 0.0;

protected:
    explicit PhasingFb(bool relative)
        : relative_(relative)
    {
    }

    void step()
    {
        const bool rising = execute && !last_execute_;
        const bool falling = !execute && last_execute_;
        last_execute_ = execute;
        if(rising) {
            clear(outputs);
            started_ = false;
            tracked_command_id_ = 0;
            absolute_phase_shift = 0.0;
            covered_phase_shift = 0.0;
            terminal_low_cycle_ = false;
        } else if(!execute && terminal_low_cycle_) {
            clear(outputs);
            started_ = false;
            tracked_command_id_ = 0;
            terminal_low_cycle_ = false;
            return;
        } else if(falling &&
                  (outputs.done || outputs.command_aborted || outputs.error)) {
            clear(outputs);
            started_ = false;
            tracked_command_id_ = 0;
        }
        if(rising) {
            start();
            return;
        }
        if(started_ && slave_ref != nullptr) {
            absolute_phase_shift = slave_ref->gear_phase_offset();
            covered_phase_shift = slave_ref->phasing_covered_shift(tracked_command_id_);
            const axis::PhasingCommandState state =
                slave_ref->phasing_command_state(tracked_command_id_);
            outputs.busy = state == axis::PhasingCommandState::queued ||
                           state == axis::PhasingCommandState::active;
            outputs.active = state == axis::PhasingCommandState::active;
            if(state == axis::PhasingCommandState::completed) {
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
                terminal_low_cycle_ = !execute;
            } else if(state == axis::PhasingCommandState::aborted) {
                outputs.command_aborted = true;
                outputs.busy = false;
                outputs.active = false;
                terminal_low_cycle_ = !execute;
            }
        }
    }

private:
    void start()
    {
        clear(outputs);
        started_ = false;
        if(master_ref == nullptr || slave_ref == nullptr || master_ref == slave_ref ||
           !slave_ref->gear_engaged_with(master_ref)) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        axis::PhasingCommand command{};
        command.phase_shift = phase_shift;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        command.relative = relative_;
        const rt::Result<std::uint32_t> requested = slave_ref->submit_phasing(command);
        if(!requested) {
            outputs.error = true;
            outputs.error_id = requested.error();
            return;
        }
        started_ = true;
        tracked_command_id_ = requested.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        absolute_phase_shift = slave_ref->gear_phase_offset();
        covered_phase_shift = slave_ref->phasing_covered_shift(tracked_command_id_);
        const axis::PhasingCommandState state =
            slave_ref->phasing_command_state(tracked_command_id_);
        outputs.busy = state == axis::PhasingCommandState::queued ||
                       state == axis::PhasingCommandState::active;
        outputs.active = state == axis::PhasingCommandState::active;
        outputs.done = state == axis::PhasingCommandState::completed;
    }

    bool relative_ = false;
    bool last_execute_ = false;
    bool started_ = false;
    bool terminal_low_cycle_ = false;
    std::uint32_t tracked_command_id_ = 0;
};

class FbPhasingAbsolute : public PhasingFb
{
public:
    FbPhasingAbsolute()
        : PhasingFb(false)
    {
    }

    void call()
    {
        step();
    }
};

class FbPhasingRelative : public PhasingFb
{
public:
    FbPhasingRelative()
        : PhasingFb(true)
    {
    }

    void call()
    {
        step();
    }
};

} // namespace plcopen::core::fb
