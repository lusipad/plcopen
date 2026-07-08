#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Part 5 composable homing step FBs (approved matrix 2026-07-07).
// Each Step FB clears the homed flag on start; only MC_FinishHoming sets it.
// Soft-limit monitoring is suspended during homing steps and restored by
// FinishHoming (Part 5 contract: limits are meaningless before homing).

// MC_StepDirect: set axis position without motion (reuses set_position).
class FbStepDirect
{
public:
    axis::AxisModel *axis_ref = nullptr;
    double set_position = 0.0;
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
        if(axis_ref == nullptr || !std::isfinite(set_position)) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        axis_ref->clear_homed();
        const rt::ErrorCode result = axis_ref->set_position(set_position);
        if(result != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = result;
            return;
        }
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
};

// MC_FinishHoming: set homed flag + optional park move.
class FbFinishHoming : public AxisExecuteFb
{
public:
    double park_position = 0.0;
    bool park_enabled = false;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;

    void call()
    {
        if(rising_edge()) {
            if(axis_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            axis_ref->set_homed();
            if(park_enabled && std::isfinite(park_position)) {
                axis::AxisCommand cmd{};
                cmd.kind = axis::CommandKind::move_absolute;
                cmd.value = park_position;
                cmd.velocity = velocity;
                cmd.acceleration = acceleration;
                cmd.deceleration = deceleration;
                cmd.jerk = jerk;
                accept(axis_ref->submit(cmd));
            } else {
                accept(rt::Result<std::uint32_t>::success(1));
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
            }
        }
        observe_axis();
    }
};

// Common base for switch/pulse homing search steps. The lifecycle is:
//   escaping (optional) -> searching -> halting -> positioning -> done
class HomingSearchFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    double set_position = 0.0;
    double offset = 0.0;
    double direction = 1.0;
    std::size_t trigger_input = 0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    MotionOutputs outputs{};

protected:
    enum class Phase
    {
        idle,
        escaping,
        searching,
        halting,
        positioning,
    };

    Phase phase_ = Phase::idle;

    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            phase_ = Phase::idle;
        }
        return rising;
    }

    bool validate_inputs() const
    {
        return axis_ref != nullptr && std::isfinite(velocity) && velocity > 0.0 &&
               std::isfinite(acceleration) && acceleration > 0.0 &&
               std::isfinite(deceleration) && deceleration > 0.0 &&
               std::isfinite(jerk) && jerk > 0.0 && std::isfinite(set_position) &&
               std::isfinite(offset) && std::isfinite(direction) && direction != 0.0 &&
               trigger_input < axis::AxisModel::DigitalInputCount;
    }

    rt::ErrorCode start_escape()
    {
        axis_ref->clear_homed();
        start_position_ = axis_ref->snapshot().command_position;
        search_cycles_ = 0;
        const rt::ErrorCode vel = submit_velocity(-direction);
        if(vel != rt::ErrorCode::ok) {
            return vel;
        }
        phase_ = Phase::escaping;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode start_search()
    {
        if(phase_ != Phase::escaping) {
            axis_ref->clear_homed();
            start_position_ = axis_ref->snapshot().command_position;
            search_cycles_ = 0;
        }
        const rt::ErrorCode vel = submit_velocity(direction);
        if(vel != rt::ErrorCode::ok) {
            return vel;
        }
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(trigger_input, false, 0.0, 0.0);
        if(!armed) {
            submit_halt();
            return armed.error();
        }
        phase_ = Phase::searching;
        return rt::ErrorCode::ok;
    }

    void observe()
    {
        if(axis_ref == nullptr || phase_ == Phase::idle) {
            return;
        }
        const axis::AxisSnapshot &snap = axis_ref->snapshot();
        if(snap.status == axis::AxisStatus::errorstop) {
            fail(rt::ErrorCode::precondition_failed);
            return;
        }

        switch(phase_) {
        case Phase::escaping: {
            const rt::Result<bool> level = axis_ref->digital_input(trigger_input);
            if(level && !level.value()) {
                const rt::ErrorCode started = start_search();
                if(started != rt::ErrorCode::ok) {
                    fail(started);
                }
            }
            break;
        }
        case Phase::searching:
            ++search_cycles_;
            if(time_limit > 0 && search_cycles_ > time_limit) {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            if(distance_limit > 0.0 &&
               std::fabs(snap.command_position - start_position_) > distance_limit) {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            if(axis_ref->probe_captured(trigger_input)) {
                captured_position_ = axis_ref->probe_recorded_position(trigger_input);
                submit_halt();
                phase_ = Phase::halting;
            }
            break;
        case Phase::halting:
            if(snap.status == axis::AxisStatus::standstill) {
                axis::AxisCommand cmd{};
                cmd.kind = axis::CommandKind::move_absolute;
                cmd.value = captured_position_ + offset;
                cmd.velocity = velocity;
                cmd.acceleration = acceleration;
                cmd.deceleration = deceleration;
                cmd.jerk = jerk;
                const rt::Result<std::uint32_t> moved = axis_ref->submit(cmd);
                if(!moved) {
                    fail(moved.error());
                    return;
                }
                phase_ = Phase::positioning;
            }
            break;
        case Phase::positioning:
            if(snap.status == axis::AxisStatus::standstill) {
                const rt::ErrorCode set = axis_ref->set_position(set_position);
                if(set != rt::ErrorCode::ok) {
                    fail(set);
                    return;
                }
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
                phase_ = Phase::idle;
            }
            break;
        default:
            break;
        }
    }

private:
    void fail(rt::ErrorCode code)
    {
        outputs.error = true;
        outputs.error_id = code;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
    }

    rt::ErrorCode submit_velocity(double dir)
    {
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = dir;
        cmd.velocity = velocity;
        cmd.acceleration = acceleration;
        cmd.deceleration = deceleration;
        cmd.jerk = jerk;
        const rt::Result<std::uint32_t> result = axis_ref->submit(cmd);
        return result ? rt::ErrorCode::ok : result.error();
    }

    void submit_halt()
    {
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::halt;
        cmd.velocity = velocity;
        cmd.acceleration = acceleration;
        cmd.deceleration = deceleration;
        cmd.jerk = jerk;
        axis_ref->submit(cmd);
    }

    double start_position_ = 0.0;
    double captured_position_ = 0.0;
    std::int64_t search_cycles_ = 0;
    bool last_execute_ = false;
};

// MC_StepAbsSwitch: search for absolute switch edge. If the switch is
// already triggered, escapes in the opposite direction first.
class FbStepAbsSwitch : public HomingSearchFb
{
public:
    void call()
    {
        if(rising_edge()) {
            if(!validate_inputs()) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            const rt::Result<bool> level = axis_ref->digital_input(trigger_input);
            if(level && level.value()) {
                const rt::ErrorCode e = start_escape();
                if(e != rt::ErrorCode::ok) {
                    clear(outputs);
                    outputs.error = true;
                    outputs.error_id = e;
                }
            } else {
                const rt::ErrorCode e = start_search();
                if(e != rt::ErrorCode::ok) {
                    clear(outputs);
                    outputs.error = true;
                    outputs.error_id = e;
                }
            }
        }
        observe();
    }
};

// MC_StepLimitSwitch: search for limit switch edge. The switch must not
// already be triggered (approach direction must go from off to on).
class FbStepLimitSwitch : public HomingSearchFb
{
public:
    void call()
    {
        if(rising_edge()) {
            if(!validate_inputs()) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            const rt::Result<bool> level = axis_ref->digital_input(trigger_input);
            if(level && level.value()) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            const rt::ErrorCode e = start_search();
            if(e != rt::ErrorCode::ok) {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = e;
            }
        }
        observe();
    }
};

// MC_StepRefPulse: search for encoder reference pulse (Z-phase).
class FbStepRefPulse : public HomingSearchFb
{
public:
    void call()
    {
        if(rising_edge()) {
            if(!validate_inputs()) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            const rt::ErrorCode e = start_search();
            if(e != rt::ErrorCode::ok) {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = e;
            }
        }
        observe();
    }
};

} // namespace plcopen::core::fb
