#pragma once

#include <cmath>
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
    bool rising_edge(bool retain_active_on_falling = false)
    {
        const bool rising = execute && !last_execute_;
        const bool falling = !execute && last_execute_;
        last_execute_ = execute;
        if(rising || (!execute && terminal_low_cycle_)) {
            clear(outputs);
            tracked_command_id_ = 0;
            terminal_low_cycle_ = false;
        } else if(falling && terminal() && !retain_active_on_falling) {
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
        if(tracked_command_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        // Done holds while Execute stays high; a later takeover command must
        // not retroactively turn a completed command into an aborted one.
        if(outputs.done) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        const rt::ErrorCode command_error = axis_ref->command_error(tracked_command_id_);
        if(command_error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = command_error;
            outputs.command_aborted = false;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        if(snapshot.active_command_id == tracked_command_id_) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = false;
            return;
        }
        // A completed command reports done even when a queued or blending
        // successor started within the same cycle.
        if(snapshot.last_completed_command_id == tracked_command_id_) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        // A command waiting in the buffered queue is busy, not aborted.
        if(axis_ref->command_pending(tracked_command_id_)) {
            outputs.busy = true;
            outputs.active = false;
            return;
        }
        outputs.command_aborted = true;
        outputs.busy = false;
        outputs.active = false;
        terminal_observed();
    }

    std::uint32_t tracked_command_id_ = 0;

    void fail_runtime(rt::ErrorCode code)
    {
        if(axis_ref != nullptr && tracked_command_id_ != 0) {
            axis_ref->fail_command(tracked_command_id_, code);
        }
    }

    void terminal_observed()
    {
        terminal_low_cycle_ = !execute;
    }

private:
    bool terminal() const
    {
        return outputs.done || outputs.command_aborted || outputs.error;
    }

    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
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

class FbSetOverride
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    double vel_factor = 1.0;
    bool enabled = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        if(!enable) {
            enabled = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(axis_ref == nullptr) {
            enabled = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        error_id = axis_ref->set_override(vel_factor);
        error = error_id != rt::ErrorCode::ok;
        enabled = !error;
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
    axis::Direction direction = axis::Direction::current;
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
        if(kind == axis::CommandKind::move_absolute) {
            command.direction = direction;
        }
        command.lock_stopping = kind == axis::CommandKind::stop;
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
    axis::Direction direction_mode = axis::Direction::current;
    bool continuous_update = false;
    bool in_velocity = false;

    void call()
    {
        if(rising_edge()) {
            continuous_update_enabled_ = continuous_update;
            submit_velocity();
        } else if(execute && continuous_update_enabled_ && tracked_command_id_ != 0 &&
                  axis_ref != nullptr &&
                  (velocity != last_velocity_ || direction != last_direction_)) {
            const double signed_direction = resolved_direction();
            const rt::ErrorCode updated =
                signed_direction == 0.0
                    ? rt::ErrorCode::invalid_argument
                    : axis_ref->update_active_velocity(tracked_command_id_, signed_direction,
                                                       std::fabs(velocity));
            if(updated != rt::ErrorCode::ok) {
                fail_runtime(updated);
            } else {
                last_velocity_ = velocity;
                last_direction_ = direction;
            }
        }
        observe_axis();
        update_in_velocity();
    }

private:
    double resolved_direction() const
    {
        if(!std::isfinite(velocity) || velocity == 0.0 ||
           !std::isfinite(direction) || direction == 0.0) {
            return 0.0;
        }
        double mode = direction;
        if(direction_mode == axis::Direction::positive) {
            mode = 1.0;
        } else if(direction_mode == axis::Direction::negative) {
            mode = -1.0;
        } else if(direction_mode == axis::Direction::shortest_way) {
            return 0.0;
        }
        return (mode < 0.0 ? -1.0 : 1.0) * (velocity < 0.0 ? -1.0 : 1.0);
    }

    void submit_velocity()
    {
        if(direction_mode == axis::Direction::shortest_way) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported));
            return;
        }
        const double signed_direction = resolved_direction();
        if(axis_ref == nullptr || signed_direction == 0.0) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::move_velocity;
        command.value = signed_direction;
        command.velocity = std::fabs(velocity);
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        const rt::Result<std::uint32_t> accepted = axis_ref->submit(command);
        if(accepted) {
            last_velocity_ = velocity;
            last_direction_ = direction;
        }
        accept(accepted);
    }

    void update_in_velocity()
    {
        in_velocity = false;
        in_velocity = axis_ref != nullptr &&
                      axis_ref->command_in_velocity(tracked_command_id_);
    }

    double last_velocity_ = 0.0;
    double last_direction_ = 0.0;
    bool continuous_update_enabled_ = false;
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
        if(!execute && last_stop_execute_ && axis_ref != nullptr && stop_command_id_ != 0) {
            axis_ref->release_stop(stop_command_id_);
        }
        last_stop_execute_ = execute;
        if(rising_edge()) {
            submit(axis::CommandKind::stop, 0.0);
            stop_command_id_ = outputs.command_id;
        }
        observe_axis();
    }

private:
    std::uint32_t stop_command_id_ = 0;
    bool last_stop_execute_ = false;
};

// MC_MoveContinuous*: InEndVelocity is the persistent target state. The
// command remains Busy/Active until takeover and never aliases that state to
// the mutually exclusive Done output.
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
    bool in_end_velocity = false;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge(true)) {
            continuous_update_enabled_ = continuous_update;
            submit_continuous(axis::CommandKind::move_continuous_absolute, position);
        } else {
            update_target(position, position);
        }
        observe_continuous();
        update_in_end_velocity();
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
        if(!execute || !continuous_update_enabled_ || tracked_command_id_ == 0 ||
           axis_ref == nullptr ||
           input_value == last_target_) {
            return;
        }
        last_target_ = input_value;
        const rt::ErrorCode updated =
            axis_ref->update_active_target(tracked_command_id_, absolute_value);
        if(updated != rt::ErrorCode::ok) {
            fail_runtime(updated);
        }
    }

    void observe_continuous()
    {
        if(tracked_command_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        const rt::ErrorCode command_error = axis_ref->command_error(tracked_command_id_);
        if(command_error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = command_error;
            outputs.command_aborted = false;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        if(snapshot.active_command_id != tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        outputs.busy = true;
        outputs.active = true;
        outputs.done = false;
    }

    void update_in_end_velocity()
    {
        in_end_velocity = false;
        in_end_velocity = axis_ref != nullptr &&
                          axis_ref->command_in_end_velocity(tracked_command_id_);
    }

    double start_position_ = 0.0;
    double last_target_ = 0.0;
    bool continuous_update_enabled_ = false;
};

class FbMoveContinuousRelative : public FbMoveContinuousAbsolute
{
public:
    double distance = 0.0;

    void call()
    {
        if(rising_edge(true)) {
            continuous_update_enabled_ = continuous_update;
            submit_continuous(axis::CommandKind::move_continuous_relative, distance);
        } else {
            // ContinuousUpdate distances re-resolve from the original command start.
            update_target(distance, start_position_ + distance);
        }
        observe_continuous();
        update_in_end_velocity();
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
        const bool falling = !execute && last_execute_;
        last_execute_ = execute;
        if(rising) {
            clear(outputs);
            tracked_command_id_ = 0;
            terminal_low_cycle_ = false;
        } else if(!execute && terminal_low_cycle_) {
            clear(outputs);
            tracked_command_id_ = 0;
            terminal_low_cycle_ = false;
            return;
        } else if(falling &&
                  (outputs.done || outputs.command_aborted || outputs.error)) {
            clear(outputs);
            tracked_command_id_ = 0;
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
            terminal_low_cycle_ = !execute;
            return;
        }
        outputs.command_aborted = true;
        outputs.busy = false;
        outputs.active = false;
        terminal_low_cycle_ = !execute;
    }

    std::uint32_t tracked_command_id_ = 0;
    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
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
        if(rising_edge()) {
            if(axis_ref == nullptr || !std::isfinite(torque)) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                axis::AxisCommand command{};
                command.kind = axis::CommandKind::torque;
                command.value = torque;
                const rt::Result<std::uint32_t> accepted = axis_ref->submit(command);
                if(accepted) {
                    commanded_torque_ = torque;
                }
                accept(accepted);
            }
        }
        observe_axis();
        in_torque = axis_ref != nullptr && tracked_command_id_ != 0 &&
                    axis_ref->torque_command_id() == tracked_command_id_ &&
                    std::fabs(axis_ref->command_torque() - commanded_torque_) <= 1e-12;
        if(in_torque) {
            outputs.done = false;
            outputs.busy = true;
            outputs.active = true;
        }
    }

private:
    double commanded_torque_ = 0.0;
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
        const axis::GroupStatus gs = group_ref->status();
        if(gs == axis::GroupStatus::moving || gs == axis::GroupStatus::stopping) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = false;
            return;
        }
        if(gs == axis::GroupStatus::interrupted) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            outputs.done = false;
            return;
        }
        outputs.done = gs == axis::GroupStatus::standby;
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
    // Approved coordinate matrix (B1 v1): ACS/MCS/PCS; WCS/FCS/TCS report
    // explicit unsupported.
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    // A4 geometric blending inputs (approved blending matrix): TransitionMode
    // MaxCornerDeviation with a positive tolerance and a blending buffer mode
    // requests a quintic corner blend; unlisted combinations are explicit
    // errors.
    axis::TransitionMode transition_mode = axis::TransitionMode::none;
    double transition_parameter = 0.0;

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
        command.transition_mode = transition_mode;
        command.transition_parameter = transition_parameter;
        command.coord_system = coord_system;
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

// MC_MoveCircularAbsolute (approved circular matrix, A3 v1): three-point
// BORDER arcs in ACS, 2-8 axes, Aborting/Buffered. CENTER/RADIUS modes and
// blending buffer modes report explicit errors instead of approximations.
class FbMoveCircularAbsolute : public GroupExecuteFb
{
public:
    axis::CircMode circ_mode = axis::CircMode::border;
    axis::GroupPosition aux_point{};
    axis::GroupPosition end_point{};
    axis::CircPathChoice path_choice = axis::CircPathChoice::counter_clockwise;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    // Approved coordinate matrix (B1 v1): ACS/MCS/PCS; WCS/FCS/TCS report
    // explicit unsupported.
    axis::CoordSystem coord_system = axis::CoordSystem::acs;

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
        command.path_kind = axis::GroupPathKind::circular;
        command.circ_mode = circ_mode;
        command.aux = aux_point;
        command.target = end_point;
        command.path_choice = path_choice;
        command.relative = relative;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.buffer_mode = buffer_mode;
        command.coord_system = coord_system;
        accept(group_ref->submit_circular(command));
    }
};

class FbMoveCircularRelative : public FbMoveCircularAbsolute
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
