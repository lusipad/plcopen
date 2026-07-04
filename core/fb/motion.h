#pragma once

#include <cstdint>

#include "axis/group.h"
#include "axis/state.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

struct MotionOutputs
{
    bool done = false;
    bool busy = false;
    bool active = false;
    bool command_accepted = false;
    bool command_aborted = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    std::uint32_t command_id = 0;
};

inline void clear(MotionOutputs &outputs)
{
    outputs = {};
}

class AxisExecuteFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
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
        outputs.done = false;
    }

    void observe_axis()
    {
        if(!execute || tracked_command_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        // Done holds while Execute stays high; a later takeover command must
        // not retroactively turn a completed command into an aborted one.
        if(outputs.done) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if(snapshot.active_command_id == tracked_command_id_) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = false;
            return;
        }
        if(snapshot.status == axis::AxisStatus::standstill ||
           snapshot.status == axis::AxisStatus::disabled) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.command_aborted = true;
        outputs.busy = false;
        outputs.active = false;
        tracked_command_id_ = 0;
    }

    std::uint32_t tracked_command_id_ = 0;

private:
    bool last_execute_ = false;
};

class FbPower
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool status = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        if(axis_ref == nullptr) {
            valid = false;
            status = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode result = axis_ref->set_power(enable);
        error = result != rt::ErrorCode::ok;
        error_id = result;
        valid = !error;
        status = axis_ref->powered();
    }
};

class FbReset : public AxisExecuteFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            observe_axis();
            return;
        }
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        const rt::ErrorCode reset = axis_ref->reset_error();
        if(reset != rt::ErrorCode::ok) {
            accept(rt::Result<std::uint32_t>::failure(reset));
            return;
        }
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

class FbSetOverride : public AxisExecuteFb
{
public:
    double percent = 100.0;

    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        const rt::ErrorCode result = axis_ref->set_override(percent);
        if(result != rt::ErrorCode::ok) {
            accept(rt::Result<std::uint32_t>::failure(result));
            return;
        }
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

class FbMoveAbsolute : public AxisExecuteFb
{
public:
    double position = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::move_absolute, position);
        }
        observe_axis();
    }

protected:
    void submit(axis::CommandKind kind, double value)
    {
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        axis::AxisCommand command{};
        command.kind = kind;
        command.value = value;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        accept(axis_ref->submit(command));
    }
};

class FbMoveRelative : public FbMoveAbsolute
{
public:
    double distance = 0.0;

    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::move_relative, distance);
        }
        observe_axis();
    }
};

class FbMoveAdditive : public FbMoveAbsolute
{
public:
    double distance = 0.0;

    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::move_additive, distance);
        }
        observe_axis();
    }
};

class FbMoveVelocity : public FbMoveAbsolute
{
public:
    double direction = 1.0;

    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::move_velocity, direction);
        }
        observe_axis();
    }
};

class FbHome : public FbMoveAbsolute
{
public:
    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::home, position);
        }
        observe_axis();
    }
};

class FbHalt : public FbMoveAbsolute
{
public:
    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::halt, 0.0);
        }
        observe_axis();
    }
};

class FbStop : public FbMoveAbsolute
{
public:
    void call()
    {
        if(rising_edge()) {
            submit(axis::CommandKind::stop, 0.0);
        }
        observe_axis();
    }
};

// MC_MoveContinuous*: done means "target reached, holding end velocity" — an
// ongoing state rather than a completed command, so it is not latched and a
// takeover reports CommandAborted even after done.
class FbMoveContinuousAbsolute : public AxisExecuteFb
{
public:
    double position = 0.0;
    double velocity = 1.0;
    double end_velocity = 0.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    bool continuous_update = false;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit_continuous(axis::CommandKind::move_continuous_absolute, position);
        } else {
            update_target(position, position);
        }
        observe_continuous();
    }

protected:
    void submit_continuous(axis::CommandKind kind, double value)
    {
        last_target_ = value;
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        start_position_ = axis_ref->snapshot().command_position;
        axis::AxisCommand command{};
        command.kind = kind;
        command.value = value;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.end_velocity = end_velocity;
        command.buffer_mode = buffer_mode;
        accept(axis_ref->submit(command));
    }

    void update_target(double input_value, double absolute_value)
    {
        if(!execute || !continuous_update || tracked_command_id_ == 0 || axis_ref == nullptr ||
           input_value == last_target_) {
            return;
        }
        last_target_ = input_value;
        const rt::ErrorCode updated =
            axis_ref->update_active_target(tracked_command_id_, absolute_value);
        if(updated != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = updated;
        }
    }

    void observe_continuous()
    {
        if(!execute || tracked_command_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if(snapshot.active_command_id != tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        outputs.busy = true;
        outputs.active = true;
        outputs.done = snapshot.active_command_reached_target;
    }

    double start_position_ = 0.0;
    double last_target_ = 0.0;
};

class FbMoveContinuousRelative : public FbMoveContinuousAbsolute
{
public:
    double distance = 0.0;

    void call()
    {
        if(rising_edge()) {
            submit_continuous(axis::CommandKind::move_continuous_relative, distance);
        } else {
            // ContinuousUpdate distances re-resolve from the original command start.
            update_target(distance, start_position_ + distance);
        }
        observe_continuous();
    }
};

// MC_MoveSuperimposed: independent offset profile on top of the base motion.
class FbMoveSuperimposed
{
public:
    axis::AxisModel *axis_ref = nullptr;
    double distance = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            tracked_command_id_ = 0;
            return;
        }
        if(rising) {
            submit();
            return;
        }
        observe();
    }

private:
    void submit()
    {
        clear(outputs);
        if(axis_ref == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            tracked_command_id_ = 0;
            return;
        }
        const rt::Result<std::uint32_t> accepted =
            axis_ref->submit_superimposed(distance, velocity, acceleration, deceleration, jerk);
        if(!accepted) {
            outputs.error = true;
            outputs.error_id = accepted.error();
            tracked_command_id_ = 0;
            return;
        }
        tracked_command_id_ = accepted.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
    }

    void observe()
    {
        if(tracked_command_id_ == 0 || axis_ref == nullptr || outputs.done) {
            return;
        }
        if(axis_ref->superimposed_command_id() == tracked_command_id_) {
            outputs.busy = true;
            outputs.active = true;
            return;
        }
        if(axis_ref->superimposed_completed_id() == tracked_command_id_) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.command_aborted = true;
        outputs.busy = false;
        outputs.active = false;
        tracked_command_id_ = 0;
    }

    std::uint32_t tracked_command_id_ = 0;
    bool last_execute_ = false;
};

// MC_HaltSuperimposed: stops only the superimposed offset; the accumulated
// contribution persists and the halt completes within the triggering cycle.
class FbHaltSuperimposed
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            return;
        }
        if(!rising) {
            return;
        }
        clear(outputs);
        if(axis_ref == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode halted = axis_ref->halt_superimposed();
        if(halted != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = halted;
            return;
        }
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
};

class FbTorqueControl : public AxisExecuteFb
{
public:
    double torque = 0.0;
    bool in_torque = false;

    void call()
    {
        if(!execute) {
            if(axis_ref != nullptr && in_torque) {
                axis::AxisCommand clear_torque{};
                clear_torque.kind = axis::CommandKind::torque;
                clear_torque.value = 0.0;
                axis_ref->submit(clear_torque);
            }
            in_torque = false;
        }
        if(!rising_edge()) {
            return;
        }
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::torque;
        command.value = torque;
        accept(axis_ref->submit(command));
        in_torque = !outputs.error;
        outputs.done = in_torque;
        outputs.busy = false;
        outputs.active = false;
    }
};

class GroupExecuteFb
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            tracked_command_id_ = 0;
        }
        return rising;
    }

    void accept(rt::Result<std::uint32_t> accepted)
    {
        outputs.command_accepted = false;
        outputs.error = false;
        outputs.error_id = rt::ErrorCode::ok;
        if(!accepted) {
            outputs.error = true;
            outputs.error_id = accepted.error();
            tracked_command_id_ = 0;
            return;
        }
        tracked_command_id_ = accepted.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
        outputs.done = false;
    }

    void observe_group()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr) {
            return;
        }
        if(group_ref->status() == axis::GroupStatus::moving ||
           group_ref->status() == axis::GroupStatus::stopping) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = false;
            return;
        }
        outputs.done = group_ref->status() == axis::GroupStatus::standby;
        outputs.busy = false;
        outputs.active = false;
    }

    std::uint32_t tracked_command_id_ = 0;

private:
    bool last_execute_ = false;
};

class FbGroupEnable : public GroupExecuteFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        const rt::ErrorCode enabled = group_ref->enable();
        if(enabled != rt::ErrorCode::ok) {
            accept(rt::Result<std::uint32_t>::failure(enabled));
            return;
        }
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

class FbGroupDisable : public GroupExecuteFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        group_ref->disable();
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

class FbGroupStop : public GroupExecuteFb
{
public:
    double deceleration = 1.0;
    double jerk = 1.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                const rt::ErrorCode stopped = group_ref->stop(deceleration, jerk);
                accept(stopped == rt::ErrorCode::ok
                           ? rt::Result<std::uint32_t>::success(1)
                           : rt::Result<std::uint32_t>::failure(stopped));
            }
        }
        observe_group();
    }
};

class FbMoveLinearAbsolute : public GroupExecuteFb
{
public:
    axis::GroupPosition position{};
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            submit(false);
        }
        observe_group();
    }

protected:
    void submit(bool relative)
    {
        if(group_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        axis::GroupCommand command{};
        command.target = position;
        command.relative = relative;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        accept(group_ref->submit_linear(command));
    }
};

class FbMoveLinearRelative : public FbMoveLinearAbsolute
{
public:
    void call()
    {
        if(rising_edge()) {
            submit(true);
        }
        observe_group();
    }
};

} // namespace plcopen::core::fb
