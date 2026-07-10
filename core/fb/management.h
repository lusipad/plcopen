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
    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const rt::ErrorCode homed = group_ref->group_home();
            if(homed != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(homed));
                return;
            }
            accept(rt::Result<std::uint32_t>::success(1));
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
        }
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

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            accept(group_ref->submit_direct(
                position, false, velocity, acceleration, deceleration, jerk));
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
        if(group_ref->last_completed_direct_command() == tracked_command_id_) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->last_aborted_direct_command() == tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if((gs == axis::GroupStatus::moving || gs == axis::GroupStatus::stopping) &&
           group_ref->direct_motion_active()) {
            outputs.busy = true;
            outputs.active = true;
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

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            accept(group_ref->submit_direct(
                distance, true, velocity, acceleration, deceleration, jerk));
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
        if(group_ref->last_completed_direct_command() == tracked_command_id_) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->last_aborted_direct_command() == tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.done = false;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if((gs == axis::GroupStatus::moving || gs == axis::GroupStatus::stopping) &&
           group_ref->direct_motion_active()) {
            outputs.busy = true;
            outputs.active = true;
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

// MC_GroupSetOverride: group-level velocity factor.
class FbGroupSetOverride : public GroupExecuteFb
{
public:
    double vel_factor = 1.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const rt::ErrorCode result = group_ref->set_group_override(vel_factor);
            if(result != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(result));
                return;
            }
            accept(rt::Result<std::uint32_t>::success(1));
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
        }
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

} // namespace plcopen::core::fb
