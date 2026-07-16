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
// Each successfully started Step FB clears homed; only MC_FinishHoming sets it.
// Soft-limit monitoring is suspended during homing steps and restored by
// FinishHoming (Part 5 contract: limits are meaningless before homing).

// MC_HomeDirect: abort the current command and set axis position without motion.
class FbHomeDirect
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    double set_position = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (!execute)
        {
            clear(outputs);
            return;
        }
        if (!rising)
        {
            return;
        }
        clear(outputs);
        if (axis_ref == nullptr || !std::isfinite(set_position))
        {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        if (buffer_mode != axis::BufferMode::aborting)
        {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::unsupported;
            return;
        }
        const rt::ErrorCode result = axis_ref->home_direct(set_position);
        if (result != rt::ErrorCode::ok)
        {
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
    double distance = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;

    void call()
    {
        if (rising_edge())
        {
            homed_applied_ = false;
            if (axis_ref == nullptr)
            {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            if (buffer_mode != axis::BufferMode::aborting &&
                buffer_mode != axis::BufferMode::buffered)
            {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported));
                return;
            }
            if (!std::isfinite(distance) || !std::isfinite(velocity) || velocity <= 0.0 ||
                !std::isfinite(acceleration) || acceleration <= 0.0 ||
                !std::isfinite(deceleration) || deceleration <= 0.0 || !std::isfinite(jerk) ||
                jerk <= 0.0)
            {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            if (distance != 0.0)
            {
                axis::AxisCommand cmd{};
                cmd.kind = axis::CommandKind::move_relative;
                cmd.value = distance;
                cmd.velocity = velocity;
                cmd.acceleration = acceleration;
                cmd.deceleration = deceleration;
                cmd.jerk = jerk;
                cmd.buffer_mode = buffer_mode;
                if (buffer_mode == axis::BufferMode::aborting)
                {
                    const rt::ErrorCode preflight =
                        axis_ref->preflight_position_sequence(&cmd, 1, true);
                    if (preflight != rt::ErrorCode::ok)
                    {
                        accept(rt::Result<std::uint32_t>::failure(preflight));
                        return;
                    }
                }
                const rt::Result<std::uint32_t> submitted = axis_ref->submit(cmd);
                accept(submitted);
            }
            else
            {
                const rt::ErrorCode finished = axis_ref->finish_homing_now();
                if (finished != rt::ErrorCode::ok)
                {
                    accept(rt::Result<std::uint32_t>::failure(finished));
                    return;
                }
                accept(rt::Result<std::uint32_t>::success(1));
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
                homed_applied_ = true;
            }
        }
        observe_axis();
        if (axis_ref != nullptr && !homed_applied_ && (outputs.active || outputs.done))
        {
            axis_ref->finish_homing();
            homed_applied_ = true;
        }
    }

  private:
    bool homed_applied_ = false;
};

// Common base for switch/pulse homing search steps. The lifecycle is:
//   escaping (optional) -> searching -> halting -> positioning -> done
class HomingSearchFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    double velocity = 1.0;
    double set_position = 0.0;
    bool set_position_enabled = true;
    axis::HomeDirection direction = axis::HomeDirection::positive;
    double torque_limit = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    MotionOutputs outputs{};

  protected:
    enum class Phase
    {
        idle,
        queued_escape,
        queued_search,
        escaping,
        searching,
        halting,
        positioning,
    };

    enum class SignalSource
    {
        reference,
        positive_limit,
        negative_limit,
    };

    Phase phase_ = Phase::idle;

    bool rising_edge()
    {
        const bool falling = !execute && last_execute_;
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (falling && phase_ == Phase::idle &&
            (outputs.done || outputs.command_aborted || outputs.error))
        {
            clear(outputs);
        }
        if (!execute && terminal_low_cycle_)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        if (rising)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        return rising;
    }

    bool validate_inputs(bool require_reference) const
    {
        return axis_ref != nullptr && std::isfinite(velocity) && velocity > 0.0 &&
               std::isfinite(set_position) && std::isfinite(torque_limit) && torque_limit >= 0.0 &&
               (buffer_mode == axis::BufferMode::aborting ||
                buffer_mode == axis::BufferMode::buffered) &&
               (!require_reference || signal_input() < axis::AxisModel::DigitalInputCount);
    }

    rt::ErrorCode start_escape()
    {
        const rt::ErrorCode precondition = axis_ref->homing_step_precondition();
        if (precondition != rt::ErrorCode::ok)
        {
            return precondition;
        }
        const rt::ErrorCode vel = submit_velocity(-direction_sign_);
        if (vel != rt::ErrorCode::ok)
        {
            return vel;
        }
        if (axis_ref->command_pending(phase_command_id_))
        {
            outputs.active = false;
            phase_ = Phase::queued_escape;
        }
        else
        {
            activate_escape();
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode start_search()
    {
        const rt::ErrorCode precondition = axis_ref->homing_step_precondition();
        if (precondition != rt::ErrorCode::ok)
        {
            return precondition;
        }
        const rt::ErrorCode vel = submit_velocity(direction_sign_);
        if (vel != rt::ErrorCode::ok)
        {
            return vel;
        }
        if (axis_ref->command_pending(phase_command_id_))
        {
            outputs.active = false;
            phase_ = Phase::queued_search;
            return rt::ErrorCode::ok;
        }
        return activate_search();
    }

    void activate_escape()
    {
        axis_ref->clear_homed();
        if (phase_ == Phase::idle || phase_ == Phase::queued_escape)
        {
            start_position_ = axis_ref->snapshot().command_position;
            search_cycles_ = 0;
        }
        outputs.active = true;
        phase_ = Phase::escaping;
    }

    rt::ErrorCode activate_search()
    {
        if (phase_ != Phase::escaping)
        {
            axis_ref->clear_homed();
            start_position_ = axis_ref->snapshot().command_position;
            search_cycles_ = 0;
        }
        outputs.active = true;
        const rt::Result<bool> level = read_signal();
        if (!level)
        {
            return level.error();
        }
        last_signal_level_ = level.value();
        if (!use_probe_)
        {
            phase_ = Phase::searching;
            return rt::ErrorCode::ok;
        }
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(signal_input(), false, 0.0, 0.0);
        if (!armed)
        {
            submit_halt();
            phase_command_id_ = 0;
            return armed.error();
        }
        probe_command_id_ = armed.value();
        phase_ = Phase::searching;
        return rt::ErrorCode::ok;
    }

    void observe()
    {
        if (axis_ref == nullptr || phase_ == Phase::idle)
        {
            return;
        }
        const axis::AxisSnapshot &snap = axis_ref->snapshot();
        if (snap.status == axis::AxisStatus::errorstop)
        {
            fail(rt::ErrorCode::precondition_failed);
            return;
        }

        switch (phase_)
        {
        case Phase::queued_escape:
        case Phase::queued_search:
            if (axis_ref->command_pending(phase_command_id_))
            {
                break;
            }
            if (snap.active_command_id != phase_command_id_)
            {
                command_aborted();
                break;
            }
            if (phase_ == Phase::queued_escape)
            {
                activate_escape();
            }
            else
            {
                const rt::ErrorCode started = activate_search();
                if (started != rt::ErrorCode::ok)
                {
                    fail(started);
                }
            }
            break;
        case Phase::escaping:
        {
            if (snap.active_command_id != phase_command_id_)
            {
                command_aborted();
                return;
            }
            if (limit_exceeded(snap))
            {
                return;
            }
            const rt::Result<bool> level = read_signal();
            if (level && level.value() != escape_level_)
            {
                const rt::ErrorCode started = start_search();
                if (started != rt::ErrorCode::ok)
                {
                    fail(started);
                }
            }
            break;
        }
        case Phase::searching:
            if (snap.active_command_id != phase_command_id_ ||
                (use_probe_ && axis_ref->probe_command_id(signal_input()) != probe_command_id_))
            {
                command_aborted();
                return;
            }
            if (limit_exceeded(snap))
            {
                return;
            }
            if (reverse_on_limit_ && (axis_ref->axis_info_inputs().limit_switch_pos ||
                                      axis_ref->axis_info_inputs().limit_switch_neg))
            {
                const rt::Result<bool> level = read_signal();
                if (!level)
                {
                    fail(level.error());
                    return;
                }
                set_escape_level(level.value());
                const rt::ErrorCode started = start_escape();
                if (started != rt::ErrorCode::ok)
                {
                    fail(started);
                }
                return;
            }
            if (search_condition_met())
            {
                captured_position_ = use_probe_ ? axis_ref->probe_recorded_position(signal_input())
                                                : snap.command_position;
                release_owned_probe();
                const rt::ErrorCode halted = submit_halt();
                if (halted != rt::ErrorCode::ok)
                {
                    fail(halted);
                    return;
                }
                phase_ = Phase::halting;
            }
            break;
        case Phase::halting:
            if (snap.active_command_id == phase_command_id_)
            {
                break;
            }
            if (snap.active_command_id != 0 || snap.last_completed_command_id != phase_command_id_)
            {
                command_aborted();
                return;
            }
            {
                axis::AxisCommand cmd{};
                cmd.kind = axis::CommandKind::move_absolute;
                cmd.value = captured_position_;
                cmd.velocity = velocity;
                cmd.acceleration = axis_ref->motion_limits().max_acceleration;
                cmd.deceleration = axis_ref->motion_limits().max_deceleration;
                cmd.jerk = axis_ref->motion_limits().max_jerk;
                cmd.homing = true;
                const rt::Result<std::uint32_t> moved = axis_ref->submit(cmd);
                if (!moved)
                {
                    fail(moved.error());
                    return;
                }
                phase_command_id_ = moved.value();
                phase_ = Phase::positioning;
            }
            break;
        case Phase::positioning:
            if (snap.active_command_id == phase_command_id_)
            {
                break;
            }
            if (snap.active_command_id != 0 || snap.last_completed_command_id != phase_command_id_)
            {
                command_aborted();
                return;
            }
            {
                if (set_position_enabled)
                {
                    const rt::ErrorCode set = axis_ref->set_position(set_position);
                    if (set != rt::ErrorCode::ok)
                    {
                        fail(set);
                        return;
                    }
                }
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
                phase_ = Phase::idle;
                phase_command_id_ = 0;
                terminal_low_cycle_ = !execute;
            }
            break;
        default:
            break;
        }
    }

  private:
    void fail(rt::ErrorCode code)
    {
        release_owned_probe();
        outputs.done = false;
        outputs.command_aborted = false;
        outputs.error = true;
        outputs.error_id = code;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
        phase_command_id_ = 0;
        terminal_low_cycle_ = !execute;
    }

    void command_aborted()
    {
        release_owned_probe();
        if (phase_ != Phase::halting && phase_command_id_ != 0 &&
            axis_ref->snapshot().active_command_id == phase_command_id_)
        {
            submit_halt();
        }
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        outputs.command_aborted = true;
        outputs.error = false;
        outputs.error_id = rt::ErrorCode::ok;
        phase_ = Phase::idle;
        phase_command_id_ = 0;
        terminal_low_cycle_ = !execute;
    }

    void release_owned_probe()
    {
        if (axis_ref != nullptr && probe_command_id_ != 0 &&
            axis_ref->probe_command_id(signal_input()) == probe_command_id_)
        {
            axis_ref->abort_trigger(signal_input());
        }
        probe_command_id_ = 0;
    }

    bool search_condition_met()
    {
        if (use_probe_)
        {
            return axis_ref->probe_captured(signal_input());
        }
        const rt::Result<bool> level = read_signal();
        if (!level)
        {
            return false;
        }
        const bool current = level.value();
        const bool rising = !last_signal_level_ && current;
        const bool falling = last_signal_level_ && !current;
        last_signal_level_ = current;
        switch (switch_mode_)
        {
        case axis::SwitchMode::on:
            return current;
        case axis::SwitchMode::off:
            return !current;
        case axis::SwitchMode::rising_edge:
            return rising;
        case axis::SwitchMode::falling_edge:
            return falling;
        case axis::SwitchMode::edge_positive:
            return direction_sign_ > 0.0 ? rising : falling;
        case axis::SwitchMode::edge_negative:
            return direction_sign_ < 0.0 ? rising : falling;
        }
        return false;
    }

    rt::ErrorCode submit_velocity(double dir)
    {
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = dir;
        cmd.velocity = velocity;
        cmd.acceleration = axis_ref->motion_limits().max_acceleration;
        cmd.deceleration = axis_ref->motion_limits().max_deceleration;
        cmd.jerk = axis_ref->motion_limits().max_jerk;
        cmd.torque_limit = torque_limit;
        cmd.buffer_mode = phase_ == Phase::idle ? buffer_mode : axis::BufferMode::aborting;
        cmd.homing = true;
        const rt::Result<std::uint32_t> result = axis_ref->submit(cmd);
        if (result)
        {
            phase_command_id_ = result.value();
        }
        return result ? rt::ErrorCode::ok : result.error();
    }

  protected:
    std::size_t signal_input() const { return reference_signal_.input; }

    rt::Result<bool> read_signal() const
    {
        if (axis_ref == nullptr)
        {
            return rt::Result<bool>::failure(rt::ErrorCode::invalid_argument);
        }
        if (signal_source_ == SignalSource::positive_limit)
        {
            return rt::Result<bool>::success(axis_ref->axis_info_inputs().limit_switch_pos);
        }
        if (signal_source_ == SignalSource::negative_limit)
        {
            return rt::Result<bool>::success(axis_ref->axis_info_inputs().limit_switch_neg);
        }
        return axis_ref->digital_input(signal_input());
    }

    void configure_reference(axis::ReferenceSignalRef reference, axis::SwitchMode mode,
                             bool use_probe)
    {
        reference_signal_ = reference;
        switch_mode_ = mode;
        signal_source_ = SignalSource::reference;
        use_probe_ = use_probe;
    }

    void configure_limit(axis::SwitchMode mode)
    {
        switch_mode_ = mode;
        signal_source_ = direction == axis::HomeDirection::positive ? SignalSource::positive_limit
                                                                    : SignalSource::negative_limit;
        use_probe_ = false;
    }

    bool valid_switch_mode() const
    {
        switch (switch_mode_)
        {
        case axis::SwitchMode::on:
        case axis::SwitchMode::off:
        case axis::SwitchMode::rising_edge:
        case axis::SwitchMode::falling_edge:
        case axis::SwitchMode::edge_positive:
        case axis::SwitchMode::edge_negative:
            return true;
        }
        return false;
    }

    void set_escape_level(bool level) { escape_level_ = level; }

    void set_reverse_on_limit(bool enabled) { reverse_on_limit_ = enabled; }

    bool set_direction_sign(bool initial_level, bool allow_switch_modes)
    {
        switch (direction)
        {
        case axis::HomeDirection::positive:
            direction_sign_ = 1.0;
            return true;
        case axis::HomeDirection::negative:
            direction_sign_ = -1.0;
            return true;
        case axis::HomeDirection::switch_positive:
            if (!allow_switch_modes)
                return false;
            direction_sign_ = initial_level ? -1.0 : 1.0;
            return true;
        case axis::HomeDirection::switch_negative:
            if (!allow_switch_modes)
                return false;
            direction_sign_ = initial_level ? 1.0 : -1.0;
            return true;
        }
        return false;
    }

  private:
    bool limit_exceeded(const axis::AxisSnapshot &snapshot)
    {
        ++search_cycles_;
        if ((time_limit > 0 && search_cycles_ > time_limit) ||
            (distance_limit > 0.0 &&
             std::fabs(snapshot.command_position - start_position_) > distance_limit))
        {
            axis_ref->trigger_error();
            fail(rt::ErrorCode::out_of_range);
            return true;
        }
        return false;
    }

    rt::ErrorCode submit_halt()
    {
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::halt;
        cmd.velocity = velocity;
        cmd.acceleration = axis_ref->motion_limits().max_acceleration;
        cmd.deceleration = axis_ref->motion_limits().max_deceleration;
        cmd.jerk = axis_ref->motion_limits().max_jerk;
        cmd.homing = true;
        const rt::Result<std::uint32_t> result = axis_ref->submit(cmd);
        if (!result)
        {
            return result.error();
        }
        phase_command_id_ = result.value();
        return rt::ErrorCode::ok;
    }

    double start_position_ = 0.0;
    double captured_position_ = 0.0;
    std::int64_t search_cycles_ = 0;
    std::uint32_t phase_command_id_ = 0;
    std::uint32_t probe_command_id_ = 0;
    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
    double direction_sign_ = 1.0;
    axis::ReferenceSignalRef reference_signal_{};
    axis::SwitchMode switch_mode_ = axis::SwitchMode::rising_edge;
    SignalSource signal_source_ = SignalSource::reference;
    bool use_probe_ = true;
    bool last_signal_level_ = false;
    bool escape_level_ = false;
    bool reverse_on_limit_ = false;
};

// MC_StepAbsoluteSwitch: search for absolute switch edge. If the switch is
// already triggered, escapes in the opposite direction first.
class FbStepAbsoluteSwitch : public HomingSearchFb
{
  public:
    axis::SwitchMode switch_mode = axis::SwitchMode::rising_edge;
    axis::ReferenceSignalRef reference_signal{};

    void call()
    {
        if (rising_edge())
        {
            configure_reference(reference_signal, switch_mode, false);
            set_reverse_on_limit(true);
            if (buffer_mode != axis::BufferMode::aborting &&
                buffer_mode != axis::BufferMode::buffered)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::unsupported;
                return;
            }
            if (!validate_inputs(true) || !valid_switch_mode())
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            const rt::Result<bool> level = axis_ref->digital_input(signal_input());
            if (!level || !set_direction_sign(level.value(), true))
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            const rt::ErrorCode e = start_search();
            if (e != rt::ErrorCode::ok)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = e;
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
    axis::SwitchMode limit_switch_mode = axis::SwitchMode::rising_edge;

    void call()
    {
        if (rising_edge())
        {
            if (buffer_mode != axis::BufferMode::aborting &&
                buffer_mode != axis::BufferMode::buffered)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::unsupported;
                return;
            }
            if (!validate_inputs(false) || (direction != axis::HomeDirection::positive &&
                                            direction != axis::HomeDirection::negative))
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            configure_limit(limit_switch_mode);
            if (limit_switch_mode != axis::SwitchMode::on &&
                limit_switch_mode != axis::SwitchMode::off &&
                limit_switch_mode != axis::SwitchMode::rising_edge &&
                limit_switch_mode != axis::SwitchMode::falling_edge)
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            const rt::Result<bool> level = read_signal();
            if (!level || !set_direction_sign(level.value(), false))
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            set_escape_level(true);
            const rt::ErrorCode e = level.value() ? start_escape() : start_search();
            if (e != rt::ErrorCode::ok)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = e;
            }
        }
        observe();
    }
};

// MC_StepReferencePulse: search for encoder reference pulse (Z-phase).
class FbStepReferencePulse : public HomingSearchFb
{
  public:
    axis::ReferenceSignalRef reference_signal{};

    void call()
    {
        if (rising_edge())
        {
            configure_reference(reference_signal, axis::SwitchMode::rising_edge, true);
            if (buffer_mode != axis::BufferMode::aborting &&
                buffer_mode != axis::BufferMode::buffered)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::unsupported;
                return;
            }
            if (!validate_inputs(true))
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            const rt::Result<bool> level = axis_ref->digital_input(signal_input());
            if (!level || !set_direction_sign(level.value(), false))
            {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            outputs.busy = true;
            outputs.active = true;
            const bool on_limit = axis_ref->axis_info_inputs().limit_switch_pos ||
                                  axis_ref->axis_info_inputs().limit_switch_neg;
            if (on_limit)
            {
                set_escape_level(level.value());
            }
            const rt::ErrorCode e = on_limit ? start_escape() : start_search();
            if (e != rt::ErrorCode::ok)
            {
                clear(outputs);
                outputs.error = true;
                outputs.error_id = e;
            }
        }
        observe();
    }
};

class FbStepBlock
{
  public:
    // KB-072: actual feedback is the only block-detection source.
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    axis::HomeDirection direction = axis::HomeDirection::positive;
    double velocity = 1.0;
    double set_position = 0.0;
    bool set_position_enabled = false;
    double detection_velocity_limit = 0.0;
    std::int64_t detection_velocity_time = 0;
    double torque_limit = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    MotionOutputs outputs{};

    void call()
    {
        const bool falling = !execute && last_execute_;
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (falling && phase_ == Phase::idle &&
            (outputs.done || outputs.command_aborted || outputs.error))
        {
            clear(outputs);
        }
        if (!execute && terminal_low_cycle_)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        if (rising)
            start();
        observe();
    }

  private:
    enum class Phase
    {
        idle,
        queued,
        searching,
        halting,
        positioning
    };

    void start()
    {
        clear(outputs);
        terminal_low_cycle_ = false;
        if (buffer_mode != axis::BufferMode::aborting && buffer_mode != axis::BufferMode::buffered)
        {
            fail(rt::ErrorCode::unsupported);
            return;
        }
        if (axis_ref == nullptr || !std::isfinite(velocity) || velocity <= 0.0 ||
            !std::isfinite(set_position) || !std::isfinite(detection_velocity_limit) ||
            detection_velocity_limit < 0.0 || detection_velocity_time < 0 ||
            !std::isfinite(torque_limit) || torque_limit < 0.0 || time_limit < 0 ||
            !std::isfinite(distance_limit) || distance_limit < 0.0 ||
            (direction != axis::HomeDirection::positive &&
             direction != axis::HomeDirection::negative) ||
            axis_ref->homing_step_precondition() != rt::ErrorCode::ok)
        {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::move_velocity;
        command.value = direction == axis::HomeDirection::positive ? 1.0 : -1.0;
        command.velocity = velocity;
        command.acceleration = axis_ref->motion_limits().max_acceleration;
        command.deceleration = axis_ref->motion_limits().max_deceleration;
        command.jerk = axis_ref->motion_limits().max_jerk;
        command.torque_limit = torque_limit;
        command.buffer_mode = buffer_mode;
        command.homing = true;
        const rt::Result<std::uint32_t> submitted = axis_ref->submit(command);
        if (!submitted)
        {
            fail(submitted.error());
            return;
        }
        command_id_ = submitted.value();
        elapsed_cycles_ = 0;
        detected_cycles_ = 0;
        outputs.busy = true;
        if (axis_ref->command_pending(command_id_))
        {
            outputs.active = false;
            phase_ = Phase::queued;
        }
        else
        {
            activate_search();
        }
    }

    void observe()
    {
        if (axis_ref == nullptr || phase_ == Phase::idle)
            return;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if (phase_ == Phase::queued)
        {
            if (axis_ref->command_pending(command_id_))
            {
                return;
            }
            if (snapshot.active_command_id == command_id_)
            {
                activate_search();
                return;
            }
            const rt::ErrorCode error = axis_ref->command_error(command_id_);
            if (error != rt::ErrorCode::ok)
            {
                fail(error);
            }
            else
            {
                abort();
            }
            return;
        }
        if (phase_ == Phase::searching)
        {
            if (snapshot.active_command_id != command_id_)
            {
                abort();
                return;
            }
            ++elapsed_cycles_;
            if ((time_limit > 0 && elapsed_cycles_ > time_limit) ||
                (distance_limit > 0.0 &&
                 std::fabs(snapshot.command_position - start_position_) > distance_limit))
            {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            const bool torque_reached =
                torque_limit == 0.0 || std::fabs(snapshot.actual_torque) >= torque_limit;
            const bool velocity_reached =
                std::fabs(snapshot.actual_velocity) <= detection_velocity_limit;
            detected_cycles_ = torque_reached && velocity_reached ? detected_cycles_ + 1 : 0;
            const std::int64_t required =
                detection_velocity_time == 0 ? 1 : detection_velocity_time;
            if (detected_cycles_ < required)
                return;
            axis::AxisCommand halt{};
            halt.kind = axis::CommandKind::halt;
            halt.velocity = velocity;
            halt.acceleration = axis_ref->motion_limits().max_acceleration;
            halt.deceleration = axis_ref->motion_limits().max_deceleration;
            halt.jerk = axis_ref->motion_limits().max_jerk;
            halt.homing = true;
            const rt::Result<std::uint32_t> submitted = axis_ref->submit(halt);
            if (!submitted)
            {
                fail(submitted.error());
                return;
            }
            command_id_ = submitted.value();
            phase_ = Phase::halting;
            return;
        }
        if (snapshot.active_command_id == command_id_)
            return;
        if (snapshot.active_command_id != 0 || snapshot.last_completed_command_id != command_id_)
        {
            abort();
            return;
        }
        if (phase_ == Phase::halting && set_position_enabled)
        {
            if (axis_ref->set_position(set_position) != rt::ErrorCode::ok)
            {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
            phase_ = Phase::positioning;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    void activate_search()
    {
        axis_ref->clear_homed();
        start_position_ = axis_ref->snapshot().command_position;
        elapsed_cycles_ = 0;
        detected_cycles_ = 0;
        outputs.active = true;
        phase_ = Phase::searching;
    }

    void fail(rt::ErrorCode error)
    {
        clear(outputs);
        outputs.error = true;
        outputs.error_id = error;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    void abort()
    {
        clear(outputs);
        outputs.command_aborted = true;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    Phase phase_ = Phase::idle;
    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
    std::uint32_t command_id_ = 0;
    double start_position_ = 0.0;
    std::int64_t elapsed_cycles_ = 0;
    std::int64_t detected_cycles_ = 0;
};

struct DistanceCodeEntry
{
    double signed_distance = 0.0;
    double second_mark_position = 0.0;
};

struct DistanceCodeMap
{
    static constexpr std::size_t Capacity = 32;
    DistanceCodeEntry entries[Capacity]{};
    std::size_t count = 0;
    double tolerance = 0.0;
};

class FbStepDistanceCoded
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    axis::HomeDirection direction = axis::HomeDirection::positive;
    double velocity = 1.0;
    double torque_limit = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    MotionOutputs outputs{};

    rt::ErrorCode bind_distance_code_map(const DistanceCodeMap *map)
    {
        if (outputs.busy)
            return rt::ErrorCode::precondition_failed;
        if (map == nullptr)
            return rt::ErrorCode::invalid_argument;
        code_map_ = map;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode bind_reference_signal(axis::ReferenceSignalRef reference)
    {
        if (outputs.busy)
            return rt::ErrorCode::precondition_failed;
        if (reference.input >= axis::AxisModel::DigitalInputCount)
        {
            return rt::ErrorCode::invalid_argument;
        }
        reference_signal_ = reference;
        return rt::ErrorCode::ok;
    }

    void call()
    {
        const bool falling = !execute && last_execute_;
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (falling && phase_ == Phase::idle &&
            (outputs.done || outputs.command_aborted || outputs.error))
        {
            clear(outputs);
        }
        if (!execute && terminal_low_cycle_)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        if (rising)
            start();
        observe();
    }

  private:
    enum class Phase
    {
        idle,
        queued,
        first_mark,
        second_mark,
        halting
    };

    bool valid_map() const
    {
        if (code_map_ == nullptr || code_map_->count == 0 ||
            code_map_->count > DistanceCodeMap::Capacity || !std::isfinite(code_map_->tolerance) ||
            code_map_->tolerance < 0.0)
        {
            return false;
        }
        for (std::size_t i = 0; i < code_map_->count; ++i)
        {
            const DistanceCodeEntry &entry = code_map_->entries[i];
            if (!std::isfinite(entry.signed_distance) ||
                !std::isfinite(entry.second_mark_position) || entry.signed_distance == 0.0)
                return false;
            for (std::size_t j = i + 1; j < code_map_->count; ++j)
            {
                if (std::fabs(entry.signed_distance - code_map_->entries[j].signed_distance) <=
                    code_map_->tolerance)
                    return false;
            }
        }
        return true;
    }

    void start()
    {
        clear(outputs);
        terminal_low_cycle_ = false;
        if (buffer_mode != axis::BufferMode::aborting && buffer_mode != axis::BufferMode::buffered)
        {
            fail(rt::ErrorCode::unsupported);
            return;
        }
        if (axis_ref == nullptr || !valid_map() || !std::isfinite(velocity) || velocity <= 0.0 ||
            !std::isfinite(torque_limit) || torque_limit < 0.0 || time_limit < 0 ||
            !std::isfinite(distance_limit) || distance_limit < 0.0 ||
            (direction != axis::HomeDirection::positive &&
             direction != axis::HomeDirection::negative) ||
            reference_signal_.input >= axis::AxisModel::DigitalInputCount ||
            axis_ref->homing_step_precondition() != rt::ErrorCode::ok)
        {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::move_velocity;
        command.value = direction == axis::HomeDirection::positive ? 1.0 : -1.0;
        command.velocity = velocity;
        command.acceleration = axis_ref->motion_limits().max_acceleration;
        command.deceleration = axis_ref->motion_limits().max_deceleration;
        command.jerk = axis_ref->motion_limits().max_jerk;
        command.torque_limit = torque_limit;
        command.buffer_mode = buffer_mode;
        command.homing = true;
        const rt::Result<std::uint32_t> submitted = axis_ref->submit(command);
        if (!submitted)
        {
            fail(submitted.error());
            return;
        }
        command_id_ = submitted.value();
        elapsed_cycles_ = 0;
        outputs.busy = true;
        if (axis_ref->command_pending(command_id_))
        {
            outputs.active = false;
            phase_ = Phase::queued;
        }
        else
        {
            activate();
        }
    }

    void observe()
    {
        if (axis_ref == nullptr || phase_ == Phase::idle)
            return;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if (phase_ == Phase::queued)
        {
            if (axis_ref->command_pending(command_id_))
                return;
            if (snapshot.active_command_id == command_id_)
            {
                activate();
            }
            else
            {
                abort();
            }
            return;
        }
        if (phase_ == Phase::first_mark || phase_ == Phase::second_mark)
        {
            if (snapshot.active_command_id != command_id_)
            {
                abort();
                return;
            }
            ++elapsed_cycles_;
            if ((time_limit > 0 && elapsed_cycles_ > time_limit) ||
                (distance_limit > 0.0 &&
                 std::fabs(snapshot.command_position - start_position_) > distance_limit))
            {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            if (!axis_ref->probe_captured(reference_signal_.input))
                return;
            const double mark = axis_ref->probe_recorded_position(reference_signal_.input);
            release_probe();
            if (phase_ == Phase::first_mark)
            {
                first_mark_ = mark;
                const rt::Result<std::uint32_t> armed =
                    axis_ref->arm_touch_probe(reference_signal_.input, false, 0.0, 0.0);
                if (!armed)
                {
                    fail(armed.error());
                    return;
                }
                probe_id_ = armed.value();
                phase_ = Phase::second_mark;
                return;
            }
            const double distance = mark - first_mark_;
            std::size_t matches = 0;
            double position = 0.0;
            for (std::size_t i = 0; i < code_map_->count; ++i)
            {
                if (std::fabs(distance - code_map_->entries[i].signed_distance) <=
                    code_map_->tolerance)
                {
                    ++matches;
                    position = code_map_->entries[i].second_mark_position;
                }
            }
            if (matches != 1)
            {
                fail(matches == 0 ? rt::ErrorCode::out_of_range : rt::ErrorCode::invalid_argument);
                return;
            }
            resolved_position_ = position;
            axis::AxisCommand halt{};
            halt.kind = axis::CommandKind::halt;
            halt.velocity = velocity;
            halt.acceleration = axis_ref->motion_limits().max_acceleration;
            halt.deceleration = axis_ref->motion_limits().max_deceleration;
            halt.jerk = axis_ref->motion_limits().max_jerk;
            halt.homing = true;
            const rt::Result<std::uint32_t> submitted = axis_ref->submit(halt);
            if (!submitted)
            {
                fail(submitted.error());
                return;
            }
            command_id_ = submitted.value();
            phase_ = Phase::halting;
            return;
        }
        if (snapshot.active_command_id == command_id_)
            return;
        if (snapshot.active_command_id != 0 || snapshot.last_completed_command_id != command_id_)
        {
            abort();
            return;
        }
        if (axis_ref->set_position(resolved_position_) != rt::ErrorCode::ok)
        {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    void activate()
    {
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(reference_signal_.input, false, 0.0, 0.0);
        if (!armed)
        {
            fail(armed.error());
            return;
        }
        axis_ref->clear_homed();
        probe_id_ = armed.value();
        start_position_ = axis_ref->snapshot().command_position;
        elapsed_cycles_ = 0;
        outputs.active = true;
        phase_ = Phase::first_mark;
    }

    void release_probe()
    {
        if (axis_ref != nullptr && probe_id_ != 0 &&
            axis_ref->probe_command_id(reference_signal_.input) == probe_id_)
        {
            axis_ref->abort_trigger(reference_signal_.input);
        }
        probe_id_ = 0;
    }

    void fail(rt::ErrorCode error)
    {
        release_probe();
        clear(outputs);
        outputs.error = true;
        outputs.error_id = error;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    void abort()
    {
        release_probe();
        clear(outputs);
        outputs.command_aborted = true;
        phase_ = Phase::idle;
        terminal_low_cycle_ = !execute;
    }

    Phase phase_ = Phase::idle;
    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
    std::uint32_t command_id_ = 0;
    std::uint32_t probe_id_ = 0;
    double start_position_ = 0.0;
    double first_mark_ = 0.0;
    double resolved_position_ = 0.0;
    std::int64_t elapsed_cycles_ = 0;
    const DistanceCodeMap *code_map_ = nullptr;
    axis::ReferenceSignalRef reference_signal_{0};
};

class FbHomeAbsolute
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool execute = false;
    MotionOutputs outputs{};

    rt::ErrorCode bind_source(const double *source)
    {
        if (started_)
            return rt::ErrorCode::precondition_failed;
        if (source == nullptr)
            return rt::ErrorCode::invalid_argument;
        source_ = source;
        return rt::ErrorCode::ok;
    }

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (!execute)
        {
            clear(outputs);
            return;
        }
        if (!rising)
            return;
        started_ = true;
        clear(outputs);
        if (axis_ref == nullptr || source_ == nullptr || !std::isfinite(*source_))
        {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        if (buffer_mode != axis::BufferMode::aborting)
        {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::unsupported;
            return;
        }
        const rt::ErrorCode result = axis_ref->home_direct(*source_);
        if (result != rt::ErrorCode::ok)
        {
            outputs.error = true;
            outputs.error_id = result;
            return;
        }
        outputs.done = true;
    }

  private:
    const double *source_ = nullptr;
    bool last_execute_ = false;
    bool started_ = false;
};

class PassiveHomingFb
{
  public:
    // KB-072: one passive owner observes motion without replacing it.
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    axis::ReferenceSignalRef reference_signal{};
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    double set_position = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    MotionOutputs outputs{};

  protected:
    bool rising_edge()
    {
        const bool falling = !execute && last_execute_;
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (falling && owner_id_ == 0 && (outputs.done || outputs.command_aborted || outputs.error))
        {
            clear(outputs);
        }
        if (!execute && terminal_low_cycle_)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        if (rising)
        {
            clear(outputs);
            terminal_low_cycle_ = false;
        }
        return rising;
    }

    void start()
    {
        clear(outputs);
        terminal_low_cycle_ = false;
        if (axis_ref == nullptr || !std::isfinite(set_position) || time_limit < 0 ||
            !std::isfinite(distance_limit) || distance_limit < 0.0 ||
            buffer_mode != axis::BufferMode::aborting ||
            reference_signal.input >= axis::AxisModel::DigitalInputCount)
        {
            fail(buffer_mode != axis::BufferMode::aborting ? rt::ErrorCode::unsupported
                                                           : rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<std::uint32_t> begun =
            axis_ref->begin_passive_homing(reference_signal.input);
        if (!begun)
        {
            fail(begun.error());
            return;
        }
        owner_id_ = begun.value();
        start_position_ = axis_ref->snapshot().actual_position;
        elapsed_cycles_ = 0;
        outputs.busy = true;
        outputs.active = true;
    }

    void observe(bool condition, bool use_current_position = false)
    {
        if (axis_ref == nullptr || owner_id_ == 0)
            return;
        if (axis_ref->passive_homing_aborted_id() == owner_id_)
        {
            clear(outputs);
            outputs.command_aborted = true;
            owner_id_ = 0;
            terminal_low_cycle_ = !execute;
            return;
        }
        ++elapsed_cycles_;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if ((time_limit > 0 && elapsed_cycles_ > time_limit) ||
            (distance_limit > 0.0 &&
             std::fabs(snapshot.actual_position - start_position_) > distance_limit))
        {
            fail(rt::ErrorCode::out_of_range);
            axis_ref->abort_passive_homing();
            owner_id_ = 0;
            return;
        }
        if (!condition ||
            (!use_current_position && !axis_ref->probe_captured(reference_signal.input)))
            return;
        const double captured = use_current_position
                                    ? snapshot.actual_position
                                    : axis_ref->probe_recorded_position(reference_signal.input);
        const double delta = set_position - captured;
        const rt::ErrorCode shifted = axis_ref->set_position_on_the_fly(delta);
        if (shifted != rt::ErrorCode::ok ||
            axis_ref->finish_passive_homing(owner_id_) != rt::ErrorCode::ok)
        {
            if (shifted != rt::ErrorCode::ok)
                axis_ref->trigger_error();
            fail(shifted != rt::ErrorCode::ok ? shifted : rt::ErrorCode::precondition_failed);
            owner_id_ = 0;
            return;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        owner_id_ = 0;
        terminal_low_cycle_ = !execute;
    }

    void fail(rt::ErrorCode error)
    {
        clear(outputs);
        outputs.error = true;
        outputs.error_id = error;
        terminal_low_cycle_ = !execute;
    }

    std::uint32_t owner_id_ = 0;

  private:
    bool last_execute_ = false;
    bool terminal_low_cycle_ = false;
    double start_position_ = 0.0;
    std::int64_t elapsed_cycles_ = 0;
};

class FbStepReferenceFlyingSwitch : public PassiveHomingFb
{
  public:
    axis::SwitchMode switch_mode = axis::SwitchMode::rising_edge;

    void call()
    {
        if (rising_edge())
        {
            if (!valid_switch_mode())
            {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
            const rt::Result<bool> level =
                axis_ref == nullptr ? rt::Result<bool>::failure(rt::ErrorCode::invalid_argument)
                                    : axis_ref->digital_input(reference_signal.input);
            if (!level)
            {
                fail(level.error());
                return;
            }
            last_level_ = level.value();
            start();
        }
        if (axis_ref == nullptr || owner_id_ == 0)
            return;
        const rt::Result<bool> level = axis_ref->digital_input(reference_signal.input);
        if (!level)
        {
            fail(level.error());
            return;
        }
        const bool rising = level.value() && !last_level_;
        const bool falling = !level.value() && last_level_;
        last_level_ = level.value();
        const double actual_velocity = axis_ref->snapshot().actual_velocity;
        if ((switch_mode == axis::SwitchMode::edge_positive ||
             switch_mode == axis::SwitchMode::edge_negative) &&
            actual_velocity == 0.0)
        {
            axis_ref->abort_passive_homing();
            owner_id_ = 0;
            fail(rt::ErrorCode::precondition_failed);
            return;
        }
        bool condition = false;
        switch (switch_mode)
        {
        case axis::SwitchMode::on:
            condition = level.value();
            break;
        case axis::SwitchMode::off:
            condition = !level.value();
            break;
        case axis::SwitchMode::rising_edge:
            condition = rising;
            break;
        case axis::SwitchMode::falling_edge:
            condition = falling;
            break;
        case axis::SwitchMode::edge_positive:
            condition = actual_velocity > 0.0 ? rising : falling;
            break;
        case axis::SwitchMode::edge_negative:
            condition = actual_velocity < 0.0 ? rising : falling;
            break;
        }
        observe(condition, true);
    }

  private:
    bool valid_switch_mode() const
    {
        switch (switch_mode)
        {
        case axis::SwitchMode::on:
        case axis::SwitchMode::off:
        case axis::SwitchMode::rising_edge:
        case axis::SwitchMode::falling_edge:
        case axis::SwitchMode::edge_positive:
        case axis::SwitchMode::edge_negative:
            return true;
        }
        return false;
    }

    bool last_level_ = false;
};

class FbStepReferenceFlyingRefPulse : public PassiveHomingFb
{
  public:
    void call()
    {
        if (rising_edge())
            start();
        observe(true);
    }
};

class FbAbortPassiveHoming
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (!execute)
        {
            clear(outputs);
            return;
        }
        if (!rising)
            return;
        clear(outputs);
        if (axis_ref == nullptr)
        {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::Result<std::uint32_t> aborted = axis_ref->abort_passive_homing();
        if (!aborted)
        {
            outputs.error = true;
            outputs.error_id = aborted.error();
            return;
        }
        outputs.done = true;
    }

  private:
    bool last_execute_ = false;
};

} // namespace plcopen::core::fb
