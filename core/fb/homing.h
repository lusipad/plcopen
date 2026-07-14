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

// MC_StepDirect: abort the current command and set axis position without motion.
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
        const rt::ErrorCode result = axis_ref->home_direct(set_position);
        if(result != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = result;
            return;
        }
        axis_ref->clear_homed();
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
            if(park_enabled) {
                axis::AxisCommand cmd{};
                cmd.kind = axis::CommandKind::move_absolute;
                cmd.value = park_position;
                cmd.velocity = velocity;
                cmd.acceleration = acceleration;
                cmd.deceleration = deceleration;
                cmd.jerk = jerk;
                const rt::ErrorCode preflight =
                    axis_ref->preflight_position_sequence(&cmd, 1, true);
                if(preflight != rt::ErrorCode::ok) {
                    accept(rt::Result<std::uint32_t>::failure(preflight));
                    return;
                }
                axis_ref->set_homed();
                accept(axis_ref->submit(cmd));
            } else {
                axis_ref->set_homed();
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
        const bool was_execute = last_execute_;
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            if(was_execute && phase_ != Phase::idle) {
                cancel_on_falling_edge();
            }
            clear(outputs);
            phase_ = Phase::idle;
            phase_command_id_ = 0;
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
        const rt::ErrorCode precondition = axis_ref->homing_step_precondition();
        if(precondition != rt::ErrorCode::ok) {
            return precondition;
        }
        const rt::ErrorCode vel = submit_velocity(-direction);
        if(vel != rt::ErrorCode::ok) {
            return vel;
        }
        axis_ref->clear_homed();
        start_position_ = axis_ref->snapshot().command_position;
        search_cycles_ = 0;
        phase_ = Phase::escaping;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode start_search()
    {
        const rt::ErrorCode precondition = axis_ref->homing_step_precondition();
        if(precondition != rt::ErrorCode::ok) {
            return precondition;
        }
        const rt::ErrorCode vel = submit_velocity(direction);
        if(vel != rt::ErrorCode::ok) {
            return vel;
        }
        if(phase_ != Phase::escaping) {
            axis_ref->clear_homed();
            start_position_ = axis_ref->snapshot().command_position;
            search_cycles_ = 0;
        }
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(trigger_input, false, 0.0, 0.0);
        if(!armed) {
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
            if(snap.active_command_id != phase_command_id_) {
                command_aborted();
                return;
            }
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
            if(snap.active_command_id != phase_command_id_ ||
               axis_ref->probe_command_id(trigger_input) != probe_command_id_) {
                command_aborted();
                return;
            }
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
                release_owned_probe();
                const rt::ErrorCode halted = submit_halt();
                if(halted != rt::ErrorCode::ok) {
                    fail(halted);
                    return;
                }
                phase_ = Phase::halting;
            }
            break;
        case Phase::halting:
            if(snap.active_command_id == phase_command_id_) {
                break;
            }
            if(snap.active_command_id != 0 ||
               snap.last_completed_command_id != phase_command_id_) {
                command_aborted();
                return;
            }
            {
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
                phase_command_id_ = moved.value();
                phase_ = Phase::positioning;
            }
            break;
        case Phase::positioning:
            if(snap.active_command_id == phase_command_id_) {
                break;
            }
            if(snap.active_command_id != 0 ||
               snap.last_completed_command_id != phase_command_id_) {
                command_aborted();
                return;
            }
            {
                const rt::ErrorCode set = axis_ref->set_position(set_position);
                if(set != rt::ErrorCode::ok) {
                    fail(set);
                    return;
                }
                outputs.done = true;
                outputs.busy = false;
                outputs.active = false;
                phase_ = Phase::idle;
                phase_command_id_ = 0;
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
    }

    void command_aborted()
    {
        release_owned_probe();
        if(phase_ != Phase::halting && phase_command_id_ != 0 &&
           axis_ref->snapshot().active_command_id == phase_command_id_) {
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
    }

    void cancel_on_falling_edge()
    {
        if(axis_ref == nullptr) {
            return;
        }
        release_owned_probe();
        if(phase_ != Phase::halting && phase_command_id_ != 0 &&
           axis_ref->snapshot().active_command_id == phase_command_id_) {
            submit_halt();
        }
    }

    void release_owned_probe()
    {
        if(axis_ref != nullptr && probe_command_id_ != 0 &&
           axis_ref->probe_command_id(trigger_input) == probe_command_id_) {
            axis_ref->abort_trigger(trigger_input);
        }
        probe_command_id_ = 0;
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
        if(result) {
            phase_command_id_ = result.value();
        }
        return result ? rt::ErrorCode::ok : result.error();
    }

    rt::ErrorCode submit_halt()
    {
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::halt;
        cmd.velocity = velocity;
        cmd.acceleration = acceleration;
        cmd.deceleration = deceleration;
        cmd.jerk = jerk;
        const rt::Result<std::uint32_t> result = axis_ref->submit(cmd);
        if(!result) {
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

class FbStepBlock
{
public:
    // KB-072: actual feedback is the only block-detection source.
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    axis::HomeDirection direction = axis::HomeDirection::positive;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    double set_position = 0.0;
    bool set_position_enabled = false;
    double detection_velocity_limit = 0.0;
    std::int64_t detection_velocity_cycles = 0;
    double torque_limit = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            phase_ = Phase::idle;
            return;
        }
        if(rising) start();
        observe();
    }

private:
    enum class Phase { idle, searching, halting, positioning };

    void start()
    {
        clear(outputs);
        if(axis_ref == nullptr || !std::isfinite(velocity) || velocity <= 0.0 ||
           !std::isfinite(acceleration) || acceleration <= 0.0 ||
           !std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0 ||
           !std::isfinite(set_position) ||
           !std::isfinite(detection_velocity_limit) ||
           detection_velocity_limit < 0.0 || detection_velocity_cycles < 0 ||
           !std::isfinite(torque_limit) || torque_limit < 0.0 ||
           time_limit < 0 || !std::isfinite(distance_limit) ||
           distance_limit < 0.0 ||
           axis_ref->homing_step_precondition() != rt::ErrorCode::ok) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::move_velocity;
        command.value = direction == axis::HomeDirection::positive ? 1.0 : -1.0;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        const rt::Result<std::uint32_t> submitted = axis_ref->submit(command);
        if(!submitted) {
            fail(submitted.error());
            return;
        }
        axis_ref->clear_homed();
        command_id_ = submitted.value();
        start_position_ = axis_ref->snapshot().command_position;
        elapsed_cycles_ = 0;
        detected_cycles_ = 0;
        outputs.busy = true;
        outputs.active = true;
        phase_ = Phase::searching;
    }

    void observe()
    {
        if(axis_ref == nullptr || phase_ == Phase::idle) return;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if(phase_ == Phase::searching) {
            if(snapshot.active_command_id != command_id_) {
                abort();
                return;
            }
            ++elapsed_cycles_;
            if((time_limit > 0 && elapsed_cycles_ > time_limit) ||
               (distance_limit > 0.0 &&
                std::fabs(snapshot.command_position - start_position_) >
                    distance_limit)) {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            const bool torque_reached =
                torque_limit == 0.0 ||
                std::fabs(snapshot.actual_torque) >= torque_limit;
            const bool velocity_reached =
                std::fabs(snapshot.actual_velocity) <= detection_velocity_limit;
            detected_cycles_ = torque_reached && velocity_reached
                                   ? detected_cycles_ + 1 : 0;
            const std::int64_t required =
                detection_velocity_cycles == 0 ? 1 : detection_velocity_cycles;
            if(detected_cycles_ < required) return;
            axis::AxisCommand halt{};
            halt.kind = axis::CommandKind::halt;
            halt.velocity = velocity;
            halt.acceleration = acceleration;
            halt.deceleration = deceleration;
            halt.jerk = jerk;
            const rt::Result<std::uint32_t> submitted = axis_ref->submit(halt);
            if(!submitted) {
                fail(submitted.error());
                return;
            }
            command_id_ = submitted.value();
            phase_ = Phase::halting;
            return;
        }
        if(snapshot.active_command_id == command_id_) return;
        if(snapshot.active_command_id != 0 ||
           snapshot.last_completed_command_id != command_id_) {
            abort();
            return;
        }
        if(phase_ == Phase::halting && set_position_enabled) {
            if(axis_ref->set_position(set_position) != rt::ErrorCode::ok) {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
            phase_ = Phase::positioning;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
    }

    void fail(rt::ErrorCode error)
    {
        clear(outputs);
        outputs.error = true;
        outputs.error_id = error;
        phase_ = Phase::idle;
    }

    void abort()
    {
        clear(outputs);
        outputs.command_aborted = true;
        phase_ = Phase::idle;
    }

    Phase phase_ = Phase::idle;
    bool last_execute_ = false;
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
    const DistanceCodeMap *code_map = nullptr;
    bool execute = false;
    axis::HomeDirection direction = axis::HomeDirection::positive;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    double torque_limit = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    std::size_t trigger_input = 0;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            release_probe();
            phase_ = Phase::idle;
            return;
        }
        if(rising) start();
        observe();
    }

private:
    enum class Phase { idle, first_mark, second_mark, halting };

    bool valid_map() const
    {
        if(code_map == nullptr || code_map->count == 0 ||
           code_map->count > DistanceCodeMap::Capacity ||
           !std::isfinite(code_map->tolerance) || code_map->tolerance < 0.0) {
            return false;
        }
        for(std::size_t i = 0; i < code_map->count; ++i) {
            const DistanceCodeEntry &entry = code_map->entries[i];
            if(!std::isfinite(entry.signed_distance) ||
               !std::isfinite(entry.second_mark_position) ||
               entry.signed_distance == 0.0) return false;
            for(std::size_t j = i + 1; j < code_map->count; ++j) {
                if(std::fabs(entry.signed_distance -
                             code_map->entries[j].signed_distance) <=
                   code_map->tolerance) return false;
            }
        }
        return true;
    }

    void start()
    {
        clear(outputs);
        if(axis_ref == nullptr || !valid_map() ||
           !std::isfinite(velocity) || velocity <= 0.0 ||
           !std::isfinite(acceleration) || acceleration <= 0.0 ||
           !std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0 ||
           !std::isfinite(torque_limit) || torque_limit < 0.0 ||
           time_limit < 0 || !std::isfinite(distance_limit) ||
           distance_limit < 0.0 ||
           trigger_input >= axis::AxisModel::DigitalInputCount ||
           axis_ref->homing_step_precondition() != rt::ErrorCode::ok) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        axis::AxisCommand command{};
        command.kind = axis::CommandKind::move_velocity;
        command.value = direction == axis::HomeDirection::positive ? 1.0 : -1.0;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        const rt::Result<std::uint32_t> submitted = axis_ref->submit(command);
        if(!submitted) {
            fail(submitted.error());
            return;
        }
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(trigger_input, false, 0.0, 0.0);
        if(!armed) {
            fail(armed.error());
            return;
        }
        axis_ref->clear_homed();
        command_id_ = submitted.value();
        probe_id_ = armed.value();
        start_position_ = axis_ref->snapshot().command_position;
        elapsed_cycles_ = 0;
        outputs.busy = true;
        outputs.active = true;
        phase_ = Phase::first_mark;
    }

    void observe()
    {
        if(axis_ref == nullptr || phase_ == Phase::idle) return;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if(phase_ == Phase::first_mark || phase_ == Phase::second_mark) {
            if(snapshot.active_command_id != command_id_) {
                abort();
                return;
            }
            ++elapsed_cycles_;
            if((time_limit > 0 && elapsed_cycles_ > time_limit) ||
               (distance_limit > 0.0 &&
                std::fabs(snapshot.command_position - start_position_) >
                    distance_limit)) {
                axis_ref->trigger_error();
                fail(rt::ErrorCode::out_of_range);
                return;
            }
            if(!axis_ref->probe_captured(trigger_input)) return;
            const double mark = axis_ref->probe_recorded_position(trigger_input);
            release_probe();
            if(phase_ == Phase::first_mark) {
                first_mark_ = mark;
                const rt::Result<std::uint32_t> armed =
                    axis_ref->arm_touch_probe(trigger_input, false, 0.0, 0.0);
                if(!armed) {
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
            for(std::size_t i = 0; i < code_map->count; ++i) {
                if(std::fabs(distance - code_map->entries[i].signed_distance) <=
                   code_map->tolerance) {
                    ++matches;
                    position = code_map->entries[i].second_mark_position;
                }
            }
            if(matches != 1) {
                fail(matches == 0 ? rt::ErrorCode::out_of_range
                                  : rt::ErrorCode::invalid_argument);
                return;
            }
            resolved_position_ = position;
            axis::AxisCommand halt{};
            halt.kind = axis::CommandKind::halt;
            halt.velocity = velocity;
            halt.acceleration = acceleration;
            halt.deceleration = deceleration;
            halt.jerk = jerk;
            const rt::Result<std::uint32_t> submitted = axis_ref->submit(halt);
            if(!submitted) {
                fail(submitted.error());
                return;
            }
            command_id_ = submitted.value();
            phase_ = Phase::halting;
            return;
        }
        if(snapshot.active_command_id == command_id_) return;
        if(snapshot.active_command_id != 0 ||
           snapshot.last_completed_command_id != command_id_) {
            abort();
            return;
        }
        if(axis_ref->set_position(resolved_position_) != rt::ErrorCode::ok) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        phase_ = Phase::idle;
    }

    void release_probe()
    {
        if(axis_ref != nullptr && probe_id_ != 0 &&
           axis_ref->probe_command_id(trigger_input) == probe_id_) {
            axis_ref->abort_trigger(trigger_input);
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
    }

    void abort()
    {
        release_probe();
        clear(outputs);
        outputs.command_aborted = true;
        phase_ = Phase::idle;
    }

    Phase phase_ = Phase::idle;
    bool last_execute_ = false;
    std::uint32_t command_id_ = 0;
    std::uint32_t probe_id_ = 0;
    double start_position_ = 0.0;
    double first_mark_ = 0.0;
    double resolved_position_ = 0.0;
    std::int64_t elapsed_cycles_ = 0;
};

class FbHomeAbsolute
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

    rt::ErrorCode bind_source(const double *source)
    {
        if(started_) return rt::ErrorCode::precondition_failed;
        if(source == nullptr) return rt::ErrorCode::invalid_argument;
        source_ = source;
        return rt::ErrorCode::ok;
    }

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            return;
        }
        if(!rising) return;
        started_ = true;
        clear(outputs);
        if(axis_ref == nullptr || source_ == nullptr ||
           !std::isfinite(*source_) || axis_ref->has_standalone_motion()) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode result = axis_ref->home_direct(*source_);
        if(result != rt::ErrorCode::ok) {
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
    std::size_t trigger_input = 0;
    double set_position = 0.0;
    std::int64_t time_limit = 0;
    double distance_limit = 0.0;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) clear(outputs);
        return rising;
    }

    void start()
    {
        clear(outputs);
        if(axis_ref == nullptr || !std::isfinite(set_position) ||
           time_limit < 0 || !std::isfinite(distance_limit) ||
           distance_limit < 0.0 ||
           trigger_input >= axis::AxisModel::DigitalInputCount) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<std::uint32_t> begun =
            axis_ref->begin_passive_homing(trigger_input);
        if(!begun) {
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
        if(axis_ref == nullptr || owner_id_ == 0) return;
        if(axis_ref->passive_homing_aborted_id() == owner_id_) {
            clear(outputs);
            outputs.command_aborted = true;
            owner_id_ = 0;
            return;
        }
        ++elapsed_cycles_;
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if((time_limit > 0 && elapsed_cycles_ > time_limit) ||
           (distance_limit > 0.0 &&
            std::fabs(snapshot.actual_position - start_position_) >
                distance_limit)) {
            fail(rt::ErrorCode::out_of_range);
            axis_ref->abort_passive_homing();
            owner_id_ = 0;
            return;
        }
        if(!condition ||
           (!use_current_position && !axis_ref->probe_captured(trigger_input))) return;
        const double captured = use_current_position
                                    ? snapshot.actual_position
                                    : axis_ref->probe_recorded_position(trigger_input);
        const double delta = set_position - captured;
        const rt::ErrorCode shifted = axis_ref->shift_coordinates(delta);
        if(shifted != rt::ErrorCode::ok ||
           axis_ref->finish_passive_homing(owner_id_) != rt::ErrorCode::ok) {
            if(shifted != rt::ErrorCode::ok) axis_ref->trigger_error();
            fail(shifted != rt::ErrorCode::ok ? shifted
                                              : rt::ErrorCode::precondition_failed);
            owner_id_ = 0;
            return;
        }
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
        owner_id_ = 0;
    }

    void fail(rt::ErrorCode error)
    {
        clear(outputs);
        outputs.error = true;
        outputs.error_id = error;
    }

    std::uint32_t owner_id_ = 0;

private:
    bool last_execute_ = false;
    double start_position_ = 0.0;
    std::int64_t elapsed_cycles_ = 0;
};

class FbStepReferenceFlyingSwitch : public PassiveHomingFb
{
public:
    axis::SwitchMode switch_mode = axis::SwitchMode::rising_edge;

    void call()
    {
        if(rising_edge()) {
            const rt::Result<bool> level =
                axis_ref == nullptr
                    ? rt::Result<bool>::failure(rt::ErrorCode::invalid_argument)
                    : axis_ref->digital_input(trigger_input);
            if(!level) {
                fail(level.error());
                return;
            }
            last_level_ = level.value();
            start();
        }
        if(axis_ref == nullptr || owner_id_ == 0) return;
        const rt::Result<bool> level = axis_ref->digital_input(trigger_input);
        if(!level) {
            fail(level.error());
            return;
        }
        const bool rising = level.value() && !last_level_;
        const bool falling = !level.value() && last_level_;
        last_level_ = level.value();
        const double actual_velocity = axis_ref->snapshot().actual_velocity;
        if((switch_mode == axis::SwitchMode::edge_positive ||
            switch_mode == axis::SwitchMode::edge_negative) &&
           actual_velocity == 0.0) {
            axis_ref->abort_passive_homing();
            owner_id_ = 0;
            fail(rt::ErrorCode::precondition_failed);
            return;
        }
        bool condition = false;
        switch(switch_mode) {
        case axis::SwitchMode::on: condition = level.value(); break;
        case axis::SwitchMode::off: condition = !level.value(); break;
        case axis::SwitchMode::rising_edge: condition = rising; break;
        case axis::SwitchMode::falling_edge: condition = falling; break;
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
    bool last_level_ = false;
};

class FbStepReferenceFlyingRefPulse : public PassiveHomingFb
{
public:
    void call()
    {
        if(rising_edge()) start();
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
        if(!execute) {
            clear(outputs);
            return;
        }
        if(!rising) return;
        clear(outputs);
        if(axis_ref == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::Result<std::uint32_t> aborted =
            axis_ref->abort_passive_homing();
        if(!aborted) {
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
