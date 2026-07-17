#pragma once

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Part 4 management FBs (approved matrix 2026-07-07).

// MC_GroupHome: parallel homing of all group members.
class FbGroupHome : public GroupExecuteFb
{
public:
    axis::GroupPosition position{};
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            accept(group_ref->submit_group_home(position, coord_system, buffer_mode));
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error || outputs.command_aborted) return;
        if(group_ref->management_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        const rt::ErrorCode error = group_ref->management_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->management_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->management_command_active(tracked_command_id_);
    }
};

// MC_MoveDirectAbsolute: non-coordinated PTP to absolute targets.
class FbMoveDirectAbsolute : public GroupExecuteFb
{
public:
    axis::GroupPosition position{};
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    double transition_velocity = 0.0;
    axis::TransitionMode transition_mode = axis::TransitionMode::none;
    double transition_parameter = 0.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            axis::GroupCommand command{};
            command.target = position;
            command.velocity = velocity;
            command.acceleration = acceleration;
            command.deceleration = deceleration;
            command.jerk = jerk;
            command.coord_system = coord_system;
            command.buffer_mode = buffer_mode;
            command.transition_velocity = transition_velocity;
            command.transition_mode = transition_mode;
            command.transition_parameter = transition_parameter;
            accept(group_ref->submit_direct(command));
        }
        observe_direct();
    }

private:
    void observe_direct()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr) {
            return;
        }
        if(outputs.done || outputs.command_aborted || outputs.error) {
            return;
        }
        const axis::GroupStatus gs = group_ref->status();
        if(gs == axis::GroupStatus::errorstop) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::precondition_failed;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->direct_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->direct_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(group_ref->direct_command_busy(tracked_command_id_)) {
            outputs.busy = true;
            outputs.active = group_ref->direct_command_active(tracked_command_id_);
            outputs.done = false;
            return;
        }
        outputs.command_aborted = true;
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        tracked_command_id_ = 0;
    }
};

// MC_MoveDirectRelative: non-coordinated PTP with relative targets.
class FbMoveDirectRelative : public GroupExecuteFb
{
public:
    axis::GroupPosition distance{};
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    double transition_velocity = 0.0;
    axis::TransitionMode transition_mode = axis::TransitionMode::none;
    double transition_parameter = 0.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            axis::GroupCommand command{};
            command.target = distance;
            command.relative = true;
            command.velocity = velocity;
            command.acceleration = acceleration;
            command.deceleration = deceleration;
            command.jerk = jerk;
            command.coord_system = coord_system;
            command.buffer_mode = buffer_mode;
            command.transition_velocity = transition_velocity;
            command.transition_mode = transition_mode;
            command.transition_parameter = transition_parameter;
            accept(group_ref->submit_direct(command));
        }
        observe_direct();
    }

private:
    void observe_direct()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr) {
            return;
        }
        if(outputs.done || outputs.command_aborted || outputs.error) {
            return;
        }
        const axis::GroupStatus gs = group_ref->status();
        if(gs == axis::GroupStatus::errorstop) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::precondition_failed;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->direct_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->direct_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(group_ref->direct_command_busy(tracked_command_id_)) {
            outputs.busy = true;
            outputs.active = group_ref->direct_command_active(tracked_command_id_);
            outputs.done = false;
            return;
        }
        outputs.command_aborted = true;
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        tracked_command_id_ = 0;
    }
};

// MC_GroupSetOverride: enable-based cyclic group override.
class FbGroupSetOverride
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    double vel_factor = 1.0;
    double acc_factor = 1.0;
    double jerk_factor = 1.0;
    bool enabled = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        if(!enable) {
            enabled = false;
            busy = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        busy = true;
        enabled = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        if(group_ref == nullptr) {
            busy = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const auto clamp_factor = [](double factor) {
            return factor < 0.0 ? 0.0 : (factor > 1.0 ? 1.0 : factor);
        };
        const rt::ErrorCode result = group_ref->set_group_override(
            clamp_factor(vel_factor), clamp_factor(acc_factor),
            clamp_factor(jerk_factor));
        busy = false;
        if(result != rt::ErrorCode::ok) {
            error = true;
            error_id = result;
            return;
        }
        enabled = true;
    }
};

// MC_GroupInterrupt: controlled pause preserving motion state.
class FbGroupInterrupt : public GroupExecuteFb
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
                const rt::ErrorCode result = group_ref->interrupt(deceleration, jerk);
                accept(result == rt::ErrorCode::ok
                           ? rt::Result<std::uint32_t>::success(1)
                           : rt::Result<std::uint32_t>::failure(result));
            }
        }
        observe_interrupt();
    }

private:
    void observe_interrupt()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr) {
            return;
        }
        const axis::GroupStatus gs = group_ref->status();
        if(gs == axis::GroupStatus::stopping) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = false;
            return;
        }
        if(gs == axis::GroupStatus::interrupted) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(gs == axis::GroupStatus::errorstop) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::precondition_failed;
            outputs.busy = false;
            outputs.active = false;
        }
    }
};

// MC_GroupContinue: resume from interrupted state.
class FbGroupContinue : public GroupExecuteFb
{
public:
    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const rt::ErrorCode result = group_ref->continue_motion();
            if(result != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(result));
                return;
            }
            accept(rt::Result<std::uint32_t>::success(1));
        }
        observe_group();
    }
};

class FbGroupHalt : public GroupExecuteFb
{
public:
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::invalid_argument));
            } else {
                accept(group_ref->halt(deceleration, jerk, buffer_mode));
            }
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error) {
            return;
        }
        if(group_ref->halt_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(group_ref->halt_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->halt_command_active(tracked_command_id_);
    }
};

class FbGroupWaitTime : public GroupExecuteFb
{
public:
    std::int64_t duration = 0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::invalid_argument));
            } else {
                accept(group_ref->submit_wait(duration, buffer_mode));
            }
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error) {
            return;
        }
        if(group_ref->wait_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(group_ref->wait_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = group_ref->wait_command_busy(tracked_command_id_);
        outputs.active = group_ref->wait_command_active(tracked_command_id_);
    }
};

} // namespace plcopen::core::fb
