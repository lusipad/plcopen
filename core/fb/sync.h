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
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            in_sync = false;
            start_sync = false;
            tracked_command_id_ = 0;
            last_phase_ = axis::SyncPhase::idle;
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

    void observe_sync()
    {
        start_sync = false;
        if(!execute || tracked_command_id_ == 0 || slave_ref == nullptr) {
            return;
        }
        if(slave_ref->sync_command_id() != tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            in_sync = false;
            tracked_command_id_ = 0;
            return;
        }

        const axis::SyncPhase phase = slave_ref->sync_phase();
        outputs.busy = true;
        outputs.active = phase != axis::SyncPhase::queued;
        in_sync = phase == axis::SyncPhase::engaged;
        start_sync = phase != last_phase_ && (phase == axis::SyncPhase::approaching ||
                                              phase == axis::SyncPhase::engaged);
        last_phase_ = phase;
    }

    std::uint32_t tracked_command_id_ = 0;

private:
    bool last_execute_ = false;
    axis::SyncPhase last_phase_ = axis::SyncPhase::idle;
};

class FbGearIn : public SyncExecuteFb
{
public:
    double ratio_numerator = 1.0;
    double ratio_denominator = 1.0;
    axis::MasterValueSource master_value_source = axis::MasterValueSource::command;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit_gear(make_command());
        } else {
            update_gear_ratio();
        }
        observe_sync();
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
        if(!execute || !continuous_update || tracked_command_id_ == 0 || slave_ref == nullptr) {
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
    // Per-cycle displacement cap for the slave approach; 0 leaves it uncapped.
    // Acceleration-shaped approach profiles are not modeled in the rewrite core.
    double velocity = 0.0;

    void call()
    {
        if(rising_edge()) {
            axis::GearInCommand command = make_command();
            command.position_sync = true;
            command.master_sync_position = master_sync_position;
            command.slave_sync_position = slave_sync_position;
            command.master_start_distance = master_start_distance;
            command.approach_velocity = velocity;
            submit_gear(command);
        } else {
            update_gear_ratio();
        }
        observe_sync();
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
    exec::CamTableView cam_table{};
    bool execute = false;
    bool done = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    exec::CamTableView cam_table_selected{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            done = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(!rising) {
            return;
        }
        if(!cam_table.valid()) {
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            done = false;
            cam_table_selected = {};
            return;
        }
        cam_table_selected = cam_table;
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
    axis::MasterValueSource master_value_source = axis::MasterValueSource::command;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit();
        } else if(execute && continuous_update && tracked_command_id_ != 0 &&
                  slave_ref != nullptr) {
            const rt::ErrorCode updated =
                slave_ref->cam_update(master_offset, master_scaling, slave_offset, slave_scaling);
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
        if(master_ref == nullptr || slave_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        if(!same_enabled_group(*master_ref, *slave_ref)) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed));
            return;
        }
        axis::CamInCommand command{};
        command.master = master_ref;
        command.table = cam_table;
        command.master_offset = master_offset;
        command.master_scaling = master_scaling;
        command.slave_offset = slave_offset;
        command.slave_scaling = slave_scaling;
        command.source = master_value_source;
        command.buffer_mode = buffer_mode;
        command.master_sync_position = master_sync_position;
        command.master_start_distance = master_start_distance;
        command.approach_velocity = velocity;
        accept(slave_ref->cam_in(command));
    }
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
        } else if(execute && continuous_update && tracked_command_id_ != 0 &&
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
    bool execute = false;
    MotionOutputs outputs{};

protected:
    explicit PhasingFb(bool relative)
        : relative_(relative)
    {
    }

    void step()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            started_ = false;
            return;
        }
        if(rising) {
            start();
            return;
        }
        if(started_ && outputs.busy && slave_ref != nullptr && !slave_ref->phasing_active()) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
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
        const rt::ErrorCode requested = relative_
                                            ? slave_ref->phasing_relative(phase_shift, velocity)
                                            : slave_ref->phasing_absolute(phase_shift, velocity);
        if(requested != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = requested;
            return;
        }
        started_ = true;
        if(slave_ref->phasing_active()) {
            outputs.busy = true;
            outputs.active = true;
        } else {
            outputs.done = true;
        }
    }

    bool relative_ = false;
    bool last_execute_ = false;
    bool started_ = false;
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
