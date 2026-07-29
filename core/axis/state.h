#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "exec/sync.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "rt/error.h"
#include "rt/static_vector.h"
#include "rt/units.h"
#include "stream/filter.h"

namespace plcopen::core::axis
{

enum class AxisStatus
{
    disabled,
    standstill,
    homing,
    discrete_motion,
    continuous_motion,
    synchronized_motion,
    stopping,
    errorstop,
};

enum class GroupStatus
{
    disabled,
    standby,
    moving,
    stopping,
    errorstop,
    interrupted,
};

enum class BufferMode
{
    aborting,
    buffered,
    blending_low,
    blending_high,
};

enum class CommandKind
{
    move_absolute,
    move_relative,
    // move_additive resolves against the previous commanded endpoint even when
    // it aborts the active command (MC_MoveAdditive boundary).
    move_additive,
    move_velocity,
    // move_continuous_* reach the target with a non-zero end velocity and then
    // hold it (MC_MoveContinuousAbsolute/Relative).
    move_continuous_absolute,
    move_continuous_relative,
    home,
    halt,
    stop,
    torque,
    acceleration_profile,
};

enum class Direction
{
    current,
    positive,
    negative,
    shortest_way,
};

enum class HomeDirection
{
    positive,
    negative,
    switch_positive,
    switch_negative,
};

enum class SwitchMode
{
    on,
    off,
    rising_edge,
    falling_edge,
    edge_positive,
    edge_negative,
};

struct ReferenceSignalRef
{
    std::size_t input = static_cast<std::size_t>(-1);
};

struct MotionLimits
{
    double max_velocity = 1.0;
    double max_acceleration = 1.0;
    double max_deceleration = 1.0;
    double max_jerk = 1.0;
    double min_position = 0.0;
    double max_position = 0.0;
    bool min_position_enabled = false;
    bool max_position_enabled = false;
};

struct AxisSiConfig
{
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
    double max_deceleration = 0.0;
    double max_jerk = 0.0;
    double min_position = 0.0;
    double max_position = 0.0;
    bool min_position_enabled = false;
    bool max_position_enabled = false;
};

// Supported parameter registry (MC_Read/WriteParameter). Unsupported entries
// return rt::ErrorCode::unsupported instead of guessing (KB-006 carried over).
// Position-lag monitoring is not modeled in the rewrite core; its parameters
// stay in the enum so callers get the explicit unsupported error.
enum class AxisParameter
{
    commanded_position,
    sw_limit_pos,
    sw_limit_neg,
    enable_limit_pos,
    enable_limit_neg,
    enable_pos_lag_monitoring,
    max_position_lag,
    max_velocity_system,
    max_velocity_appl,
    actual_velocity,
    commanded_velocity,
    max_acceleration_system,
    max_acceleration_appl,
    max_deceleration_system,
    max_deceleration_appl,
    max_jerk_system,
    max_jerk_appl,
};

enum class AxisManagementKind
{
    set_position,
    write_parameter,
    write_bool_parameter,
    write_digital_output,
};

struct AxisManagementCommand
{
    AxisManagementKind kind = AxisManagementKind::set_position;
    AxisParameter parameter = AxisParameter::commanded_position;
    double value = 0.0;
    std::size_t channel = 0;
    bool flag = false;
    std::uint32_t command_id = 0;
};

struct AxisCommand
{
    CommandKind kind = CommandKind::move_absolute;
    double value = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    // Optional drive-side torque/force ceiling. Zero means no limit.
    double torque_limit = 0.0;
    // MC_TorqueControl load-domain rate converted to a per-cycle maximum
    // setpoint delta. Zero applies the target immediately.
    double torque_ramp = 0.0;
    Direction direction = Direction::current;
    // Only used by the move_continuous_* kinds; it is a signed terminal
    // velocity. Zero remains explicitly unsupported.
    double end_velocity = 0.0;
    // Minimum command time in cycles (profile-table segments). A position
    // command holds at its target until the duration elapses; a velocity
    // command finishes after it (0 keeps the plain unlimited hold).
    std::int64_t min_duration_cycles = 0;
    BufferMode buffer_mode = BufferMode::aborting;
    bool continuous_update = false;
    bool lock_stopping = false;
    // Part 5 step commands keep the axis in Homing across their internal
    // velocity, halt, and positioning phases.
    bool homing = false;
    std::uint32_t command_id = 0;
};

// One profile-table segment (MC_Position/Velocity/AccelerationProfile).
// target is a position for position profiles and a signed velocity for
// velocity/acceleration profiles.
struct ProfileSegment
{
    double target = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    std::int64_t duration_cycles = 0;
    bool relative = false;
};

enum class MasterValueSource
{
    command,
    actual,
};

enum class CamStartMode
{
    absolute,
    relative,
    ramp_in,
};

enum class SyncKind
{
    none,
    gear,
    cam,
    combine,
    group_path,
};

enum class SyncPhase
{
    idle,
    queued,
    waiting_window,
    approaching,
    engaged,
};

enum class SyncMode
{
    shortest,
    catch_up,
    slow_down,
};

enum class CombineMode
{
    add_axes,
    sub_axes,
};

class AxisModel;
class AxisGroup;

struct GearInCommand
{
    const AxisModel *master = nullptr;
    double ratio_numerator = 1.0;
    double ratio_denominator = 1.0;
    MasterValueSource source = MasterValueSource::command;
    BufferMode buffer_mode = BufferMode::aborting;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    // position_sync selects MC_GearInPos semantics: wait for the master sync
    // window, approach the slave sync position, then follow with aligned phase.
    bool position_sync = false;
    double master_sync_position = 0.0;
    double slave_sync_position = 0.0;
    double master_start_distance = 0.0;
    SyncMode sync_mode = SyncMode::shortest;
    // Maximum approach velocity. Zero selects the axis motion limit.
    double approach_velocity = 0.0;
};

struct PhasingCommand
{
    double phase_shift = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    BufferMode buffer_mode = BufferMode::aborting;
    bool relative = false;
    std::uint32_t command_id = 0;
};

enum class PhasingCommandState
{
    unknown,
    queued,
    active,
    completed,
    aborted,
};

struct PhasingCommandResult
{
    std::uint32_t command_id = 0;
    PhasingCommandState state = PhasingCommandState::unknown;
    double covered_shift = 0.0;
};

struct CamInCommand
{
    const AxisModel *master = nullptr;
    exec::CamTableView table{};
    // Approved cam matrix (decision #2): linear is the byte-identical C0
    // compatibility default; spline reconstructs a C2 cubic at engage.
    exec::CamInterpolation interpolation = exec::CamInterpolation::linear;
    double master_offset = 0.0;
    double master_scaling = 1.0;
    double slave_offset = 0.0;
    double slave_scaling = 1.0;
    MasterValueSource source = MasterValueSource::command;
    BufferMode buffer_mode = BufferMode::aborting;
    // master_start_distance > 0 arms the approach window that ends at
    // master_sync_position; 0 engages immediately.
    double master_sync_position = 0.0;
    double master_start_distance = 0.0;
    double approach_velocity = 0.0;
};

struct CamTableSelection
{
    std::uint32_t id = 0;
    const AxisModel *master = nullptr;
    exec::CamTableView table{};
    bool master_absolute = true;
    bool slave_absolute = true;
    double master_origin = 0.0;
    double slave_origin = 0.0;
};

struct CombineAxesCommand
{
    const AxisModel *master1 = nullptr;
    const AxisModel *master2 = nullptr;
    CombineMode mode = CombineMode::add_axes;
    double ratio_numerator_m1 = 1.0;
    double ratio_denominator_m1 = 1.0;
    double ratio_numerator_m2 = 1.0;
    double ratio_denominator_m2 = 1.0;
    MasterValueSource source_m1 = MasterValueSource::command;
    MasterValueSource source_m2 = MasterValueSource::command;
    BufferMode buffer_mode = BufferMode::aborting;
};

struct AxisSnapshot
{
    AxisStatus status = AxisStatus::disabled;
    double command_position = 0.0;
    double command_velocity = 0.0;
    double command_acceleration = 0.0;
    double actual_position = 0.0;
    double actual_velocity = 0.0;
    double actual_acceleration = 0.0;
    double command_torque = 0.0;
    double actual_torque = 0.0;
    double command_torque_limit = 0.0;
    double torque_velocity_limit = 0.0;
    double torque_acceleration_limit = 0.0;
    double torque_deceleration_limit = 0.0;
    double torque_jerk_limit = 0.0;
    Direction torque_direction = Direction::current;
    bool torque_mode = false;
    bool powered = false;
    bool homed = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    // Set while a move_continuous_* command holds its end velocity after
    // reaching the target position.
    bool active_command_reached_target = false;
    std::uint32_t active_command_id = 0;
    // Id of the most recently *completed* (not aborted) command, so facades
    // can latch Done even when a queued successor starts in the same cycle.
    std::uint32_t last_completed_command_id = 0;
};

inline bool is_finite_command(const AxisCommand &command)
{
    return std::isfinite(command.value) && std::isfinite(command.velocity) &&
           std::isfinite(command.acceleration) && std::isfinite(command.deceleration) &&
           std::isfinite(command.jerk) && std::isfinite(command.torque_limit) &&
           command.torque_limit >= 0.0 && std::isfinite(command.torque_ramp) &&
           command.torque_ramp >= 0.0 && std::isfinite(command.end_velocity);
}

inline bool is_valid_direction(Direction direction)
{
    return direction == Direction::current || direction == Direction::positive ||
           direction == Direction::negative || direction == Direction::shortest_way;
}

inline bool is_valid_buffer_mode(BufferMode mode)
{
    return mode == BufferMode::aborting || mode == BufferMode::buffered ||
           mode == BufferMode::blending_low || mode == BufferMode::blending_high;
}

inline bool is_continuous_kind(CommandKind kind)
{
    return kind == CommandKind::move_continuous_absolute ||
           kind == CommandKind::move_continuous_relative;
}

class AxisModel
{
  public:
    static constexpr std::size_t QueueCapacity = 8;

    explicit AxisModel(int domain_id = 0) : domain_id_(domain_id) {}

    AxisModel(const AxisModel &) = delete;
    AxisModel &operator=(const AxisModel &) = delete;
    AxisModel(AxisModel &&) = delete;
    AxisModel &operator=(AxisModel &&) = delete;

    int domain_id() const { return domain_id_; }

    AxisStatus status() const { return snapshot_.status; }

    const AxisSnapshot &snapshot() const { return snapshot_; }

    bool powered() const { return snapshot_.powered; }

    const MotionLimits &motion_limits() const { return limits_; }

    rt::Result<AxisSiConfig> si_config(const rt::CycleConfig &cycle) const
    {
        if(cycle.period_ns() <= 0) {
            return rt::Result<AxisSiConfig>::failure(rt::ErrorCode::invalid_argument);
        }
        AxisSiConfig result{};
        result.max_velocity = cycle.velocity_to_si(limits_.max_velocity);
        result.max_acceleration = cycle.acceleration_to_si(limits_.max_acceleration);
        result.max_deceleration = cycle.acceleration_to_si(limits_.max_deceleration);
        result.max_jerk = cycle.jerk_to_si(limits_.max_jerk);
        result.min_position = limits_.min_position;
        result.max_position = limits_.max_position;
        result.min_position_enabled = limits_.min_position_enabled;
        result.max_position_enabled = limits_.max_position_enabled;
        return rt::Result<AxisSiConfig>::success(result);
    }

    // KB-068: a standby group may claim this member only when every
    // standalone writer (base, sync, stream, and superimposed) is idle.
    bool has_standalone_motion() const
    {
        return active_ || !queue_.empty() || sync_kind_ != SyncKind::none || superimposed_active_ ||
               stream_active_;
    }

    void *group_owner() const { return group_owner_; }

    rt::ErrorCode homing_step_precondition() const
    {
        if (snapshot_.status == AxisStatus::errorstop || snapshot_.error ||
            (group_owner_ != nullptr &&
             (group_status_ == nullptr || *group_status_ != GroupStatus::standby)))
        {
            return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode configure_limits(MotionLimits limits)
    {
        if (group_owner_ != nullptr)
            return rt::ErrorCode::precondition_failed;
        if (snapshot_.powered || !std::isfinite(limits.max_velocity) ||
            !std::isfinite(limits.max_acceleration) || !std::isfinite(limits.max_deceleration) ||
            !std::isfinite(limits.max_jerk) || limits.max_velocity <= 0.0 ||
            limits.max_acceleration <= 0.0 || limits.max_deceleration <= 0.0 ||
            limits.max_jerk <= 0.0)
        {
            return rt::ErrorCode::invalid_argument;
        }
        limits_ = limits;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode configure_si(const rt::CycleConfig &cycle, const AxisSiConfig &config)
    {
        const rt::Result<MotionLimits> limits = si_to_limits(cycle, config, limits_);
        if(!limits) return limits.error();
        return configure_limits(limits.value());
    }

    rt::ErrorCode set_power(bool enabled, bool enable_positive = true,
                            bool enable_negative = true)
    {
        if (snapshot_.status == AxisStatus::errorstop && enabled)
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (enabled && !power_feedback_)
        {
            return rt::ErrorCode::precondition_failed;
        }
        const bool positive_changed = positive_enabled_ != (enabled && enable_positive);
        const bool negative_changed = negative_enabled_ != (enabled && enable_negative);
        positive_enabled_ = enabled && enable_positive;
        negative_enabled_ = enabled && enable_negative;

        // MC_Power is level-controlled and called every scan cycle. A power
        // transition always aborts motion; changing a feed enable only aborts
        // a command that would continue into the newly disabled direction.
        if (enabled == snapshot_.powered)
        {
            if ((positive_changed || negative_changed) &&
                !active_direction_enabled())
            {
                abort_motion();
                snapshot_.status = AxisStatus::standstill;
            }
            return rt::ErrorCode::ok;
        }

        abort_motion();
        snapshot_.powered = enabled;
        snapshot_.status = enabled ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    // External power-stage feedback is separate from MC_Power's normal
    // level-controlled disable. Losing feedback while enabled is an axis
    // fault, not a user abort.
    rt::ErrorCode set_power_feedback(bool available)
    {
        power_feedback_ = available;
        if (available || !snapshot_.powered)
        {
            return rt::ErrorCode::ok;
        }
        snapshot_.powered = false;
        return trigger_error(rt::ErrorCode::precondition_failed);
    }

    rt::ErrorCode set_position(double position)
    {
        if (!std::isfinite(position) || is_moving() || !target_inside_limits(position, true))
        {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.command_position = position;
        snapshot_.actual_position = position;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        return rt::ErrorCode::ok;
    }

    rt::Result<double> read_parameter(AxisParameter parameter) const
    {
        double value = 0.0;
        switch (parameter)
        {
        case AxisParameter::commanded_position:
            value = snapshot_.command_position;
            break;
        case AxisParameter::sw_limit_pos:
            value = limits_.max_position;
            break;
        case AxisParameter::sw_limit_neg:
            value = limits_.min_position;
            break;
        case AxisParameter::enable_limit_pos:
            value = limits_.max_position_enabled ? 1.0 : 0.0;
            break;
        case AxisParameter::enable_limit_neg:
            value = limits_.min_position_enabled ? 1.0 : 0.0;
            break;
        case AxisParameter::max_velocity_system:
        case AxisParameter::max_velocity_appl:
            value = limits_.max_velocity;
            break;
        case AxisParameter::actual_velocity:
            value = snapshot_.actual_velocity;
            break;
        case AxisParameter::commanded_velocity:
            value = snapshot_.command_velocity;
            break;
        case AxisParameter::max_acceleration_system:
        case AxisParameter::max_acceleration_appl:
            value = limits_.max_acceleration;
            break;
        case AxisParameter::max_deceleration_system:
        case AxisParameter::max_deceleration_appl:
            value = limits_.max_deceleration;
            break;
        case AxisParameter::max_jerk_system:
        case AxisParameter::max_jerk_appl:
            value = limits_.max_jerk;
            break;
        default:
            return rt::Result<double>::failure(rt::ErrorCode::unsupported);
        }
        return rt::Result<double>::success(value);
    }

    rt::ErrorCode shift_coordinates(double delta)
    {
        // MC_SetPosition remaps the complete absolute coordinate domain,
        // including active and queued absolute targets.
        const bool has_position_profile = active_ &&
                                          active_command_.kind != CommandKind::move_velocity &&
                                          active_command_.kind != CommandKind::torque &&
                                          active_command_.kind != CommandKind::acceleration_profile;
        if (!std::isfinite(delta) || sync_kind_ != SyncKind::none || stream_active_ ||
            superimposed_active_ || group_blocks_standalone_motion())
        {
            return rt::ErrorCode::precondition_failed;
        }
        if (!std::isfinite(snapshot_.command_position + delta) ||
            !std::isfinite(snapshot_.actual_position + delta) ||
            !target_inside_limits(snapshot_.command_position + delta, true) ||
            !target_inside_limits(snapshot_.actual_position + delta, true))
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (has_position_profile && (!std::isfinite(active_target_ + delta) ||
                                     !target_inside_limits(active_target_ + delta, true)))
        {
            return rt::ErrorCode::invalid_argument;
        }
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            if ((queue_[i].kind == CommandKind::move_absolute ||
                 queue_[i].kind == CommandKind::move_continuous_absolute) &&
                (!std::isfinite(queue_[i].value + delta) ||
                 !target_inside_limits(queue_[i].value + delta, true)))
            {
                return rt::ErrorCode::invalid_argument;
            }
        }
        if (has_position_profile && active_profile_.translate(delta) != rt::ErrorCode::ok)
        {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.command_position += delta;
        snapshot_.actual_position += delta;
        if (active_)
        {
            if (has_position_profile)
            {
                active_target_ += delta;
                active_last_sample_ += delta;
            }
            if (active_command_.kind == CommandKind::move_absolute ||
                active_command_.kind == CommandKind::move_continuous_absolute)
            {
                active_command_.value += delta;
            }
        }
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            if (queue_[i].kind == CommandKind::move_absolute ||
                queue_[i].kind == CommandKind::move_continuous_absolute)
            {
                queue_[i].value += delta;
            }
        }
        for (ProbeSlot &probe : probes_)
        {
            if (probe.captured)
                probe.recorded_position += delta;
            if (probe.window_only)
            {
                probe.first_position += delta;
                probe.last_position += delta;
            }
        }
        return rt::ErrorCode::ok;
    }

    // Part 5 flying reference: remap the current coordinate while preserving
    // the numeric absolute targets of the active and queued motion commands.
    rt::ErrorCode set_position_on_the_fly(double delta)
    {
        if (!active_ || !std::isfinite(delta) || sync_kind_ != SyncKind::none || stream_active_ ||
            superimposed_active_ || group_blocks_standalone_motion())
        {
            return rt::ErrorCode::precondition_failed;
        }

        const double position = snapshot_.command_position + delta;
        const double actual_position = snapshot_.actual_position + delta;
        if (!std::isfinite(position) || !std::isfinite(actual_position) ||
            !target_inside_limits(position, true) || !target_inside_limits(actual_position, true))
        {
            return rt::ErrorCode::invalid_argument;
        }

        const bool profile_driven = active_command_.kind != CommandKind::move_velocity &&
                                    active_command_.kind != CommandKind::torque &&
                                    active_command_.kind != CommandKind::acceleration_profile &&
                                    !continuous_holding_ && !override_paused_;
        otg::Profile1D updated_profile{};
        if (profile_driven)
        {
            if (override_braking_)
            {
                updated_profile = active_profile_;
                if (updated_profile.translate(delta) != rt::ErrorCode::ok)
                {
                    return rt::ErrorCode::invalid_argument;
                }
            }
            else
            {
                const double end_velocity = is_continuous_kind(active_command_.kind)
                                                ? active_command_.end_velocity * override_
                                                : 0.0;
                const otg::Limits1D limits = scaled_limits(active_command_);
                const rt::Result<otg::Profile1D> replanned = otg::plan_time_optimal(
                    {position, snapshot_.command_velocity, snapshot_.command_acceleration},
                    {active_target_, end_velocity, 0.0}, limits);
                if (!replanned)
                {
                    return replanned.error();
                }
                updated_profile = replanned.value();
            }
        }

        snapshot_.command_position = position;
        snapshot_.actual_position = actual_position;
        if (profile_driven)
        {
            active_profile_ = updated_profile;
            if (override_braking_)
            {
                active_last_sample_ += delta;
            }
            else
            {
                active_tick_ = 0;
                active_last_sample_ = position;
                snapshot_.active_command_reached_target = false;
            }
        }
        for (ProbeSlot &probe : probes_)
        {
            if (probe.captured)
                probe.recorded_position += delta;
            if (probe.window_only)
            {
                probe.first_position += delta;
                probe.last_position += delta;
            }
        }
        return rt::ErrorCode::ok;
    }

    rt::Result<bool> read_bool_parameter(AxisParameter parameter) const
    {
        bool value = false;
        switch (parameter)
        {
        case AxisParameter::enable_limit_pos:
            value = limits_.max_position_enabled;
            break;
        case AxisParameter::enable_limit_neg:
            value = limits_.min_position_enabled;
            break;
        default:
            return rt::Result<bool>::failure(rt::ErrorCode::unsupported);
        }
        return rt::Result<bool>::success(value);
    }

    rt::ErrorCode write_parameter(AxisParameter parameter, double value)
    {
        if (!std::isfinite(value))
        {
            return rt::ErrorCode::invalid_argument;
        }
        switch (parameter)
        {
        case AxisParameter::sw_limit_pos:
            if (limits_.min_position_enabled && limits_.max_position_enabled &&
                value < limits_.min_position)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_position = value;
            return rt::ErrorCode::ok;
        case AxisParameter::sw_limit_neg:
            if (limits_.min_position_enabled && limits_.max_position_enabled &&
                value > limits_.max_position)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.min_position = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_velocity_system:
        case AxisParameter::max_velocity_appl:
            if (value <= 0.0)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_velocity = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_acceleration_system:
        case AxisParameter::max_acceleration_appl:
            if (value <= 0.0)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_acceleration = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_deceleration_system:
        case AxisParameter::max_deceleration_appl:
            if (value <= 0.0)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_deceleration = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_jerk_system:
        case AxisParameter::max_jerk_appl:
            if (value <= 0.0)
            {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_jerk = value;
            return rt::ErrorCode::ok;
        default:
            return rt::ErrorCode::unsupported;
        }
    }

    rt::ErrorCode write_bool_parameter(AxisParameter parameter, bool value)
    {
        switch (parameter)
        {
        case AxisParameter::enable_limit_pos:
            limits_.max_position_enabled = value;
            return rt::ErrorCode::ok;
        case AxisParameter::enable_limit_neg:
            limits_.min_position_enabled = value;
            return rt::ErrorCode::ok;
        default:
            return rt::ErrorCode::unsupported;
        }
    }

    rt::Result<std::uint32_t> submit_set_position(double position, bool relative,
                                                  bool queued)
    {
        AxisManagementCommand command{};
        command.kind = AxisManagementKind::set_position;
        command.value = position;
        command.flag = relative;
        return submit_management(command, queued);
    }

    rt::Result<std::uint32_t> submit_write_parameter(AxisParameter parameter,
                                                     double value, bool queued)
    {
        AxisManagementCommand command{};
        command.kind = AxisManagementKind::write_parameter;
        command.parameter = parameter;
        command.value = value;
        return submit_management(command, queued);
    }

    rt::Result<std::uint32_t> submit_write_bool_parameter(AxisParameter parameter,
                                                          bool value, bool queued)
    {
        AxisManagementCommand command{};
        command.kind = AxisManagementKind::write_bool_parameter;
        command.parameter = parameter;
        command.flag = value;
        return submit_management(command, queued);
    }

    rt::Result<std::uint32_t> submit_write_digital_output(std::size_t channel,
                                                          bool value, bool queued)
    {
        AxisManagementCommand command{};
        command.kind = AxisManagementKind::write_digital_output;
        command.channel = channel;
        command.flag = value;
        return submit_management(command, queued);
    }

    // MC_SetOverride carries the KB-003 contract: newly planned commands scale
    // by the override, velocity commands respond per cycle through
    // signed_velocity(), and an active profile-driven command re-plans from
    // its current state under the re-scaled velocity limit (an override drop
    // below the current velocity plans a deceleration entry). A failed replan
    // keeps the previous override and the running profile untouched.
    rt::ErrorCode set_override(double factor)
    {
        return set_override(factor, acceleration_override_, jerk_override_);
    }

    rt::ErrorCode set_override(double velocity_factor,
                               double acceleration_factor,
                               double jerk_factor)
    {
        if (group_blocks_standalone_motion())
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (!std::isfinite(velocity_factor) || velocity_factor < 0.0 ||
            velocity_factor > 1.0 || !std::isfinite(acceleration_factor) ||
            acceleration_factor <= 0.0 || acceleration_factor > 1.0 ||
            !std::isfinite(jerk_factor) || jerk_factor <= 0.0 ||
            jerk_factor > 1.0)
        {
            return rt::ErrorCode::invalid_argument;
        }
        const double previous_velocity = override_;
        const double previous_acceleration = acceleration_override_;
        const double previous_jerk = jerk_override_;
        override_ = velocity_factor;
        acceleration_override_ = acceleration_factor;
        jerk_override_ = jerk_factor;
        if (previous_velocity == velocity_factor &&
            previous_acceleration == acceleration_factor &&
            previous_jerk == jerk_factor)
        {
            return rt::ErrorCode::ok;
        }

        const auto restore_previous = [&]() {
            override_ = previous_velocity;
            acceleration_override_ = previous_acceleration;
            jerk_override_ = previous_jerk;
        };

        if (velocity_factor == 0.0 && active_ && active_command_.kind != CommandKind::halt &&
            active_command_.kind != CommandKind::stop)
        {
            const double v0 = snapshot_.command_velocity;
            const double a0 = snapshot_.command_acceleration;
            const double scaled_acceleration =
                active_command_.acceleration * acceleration_override_;
            const double scaled_deceleration =
                active_command_.deceleration * acceleration_override_;
            const double scaled_jerk = active_command_.jerk * jerk_override_;
            const otg::Limits1D halt_limits{std::fabs(v0) + active_command_.velocity + 1e-9,
                                            std::fmax(std::fabs(a0), scaled_acceleration),
                                            scaled_deceleration, scaled_jerk};
            double brake_velocity = v0;
            double brake_shift = 0.0;
            if (a0 != 0.0)
            {
                const double zero_cycles = std::ceil(std::fabs(a0) / scaled_jerk);
                brake_velocity += 0.5 * a0 * zero_cycles;
                brake_shift += v0 * zero_cycles + a0 * zero_cycles * zero_cycles / 3.0;
            }
            const double stop_position =
                snapshot_.command_position + brake_shift +
                otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {snapshot_.command_position, v0, a0}, {stop_position, 0.0, 0.0}, halt_limits);
            if (!profile)
            {
                restore_previous();
                return profile.error();
            }
            active_profile_ = profile.value();
            active_last_sample_ = snapshot_.command_position;
            active_tick_ = 0;
            override_braking_ = true;
            override_paused_ = false;
            return rt::ErrorCode::ok;
        }

        if (previous_velocity == 0.0 && velocity_factor > 0.0 && override_paused_)
        {
            override_paused_ = false;
            if (active_command_.kind == CommandKind::move_velocity || continuous_holding_)
            {
                return rt::ErrorCode::ok;
            }
            const otg::Limits1D limits = scaled_limits(active_command_);
            const double direction = active_target_ >= snapshot_.command_position ? 1.0 : -1.0;
            const double end_velocity = is_continuous_kind(active_command_.kind)
                                            ? direction * active_command_.end_velocity * velocity_factor
                                            : 0.0;
            const rt::Result<otg::Profile1D> profile =
                otg::plan_time_optimal({snapshot_.command_position, 0.0, 0.0},
                                       {active_target_, end_velocity, 0.0}, limits);
            if (!profile)
            {
                restore_previous();
                override_paused_ = true;
                return profile.error();
            }
            active_profile_ = profile.value();
            active_last_sample_ = snapshot_.command_position;
            active_tick_ = 0;
            return rt::ErrorCode::ok;
        }

        const bool profile_driven = active_ && active_command_.kind != CommandKind::move_velocity &&
                                    active_command_.kind != CommandKind::halt &&
                                    active_command_.kind != CommandKind::stop;
        if (!profile_driven)
        {
            return rt::ErrorCode::ok;
        }

        const double end_velocity = is_continuous_kind(active_command_.kind)
                                        ? active_command_.end_velocity * override_
                                        : 0.0;
        if (continuous_holding_)
        {
            continuous_hold_velocity_ = end_velocity;
            return rt::ErrorCode::ok;
        }

        const otg::Limits1D limits = scaled_limits(active_command_);
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity,
                                    snapshot_.command_acceleration},
                                   {active_target_, end_velocity, 0.0}, limits);
        if (!profile)
        {
            restore_previous();
            return profile.error();
        }
        active_profile_ = profile.value();
        active_tick_ = 0;
        active_last_sample_ = snapshot_.command_position;
        continuous_hold_velocity_ = end_velocity;
        snapshot_.active_command_reached_target = false;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode trigger_error(rt::ErrorCode code = rt::ErrorCode::precondition_failed)
    {
        if (snapshot_.active_command_id != 0)
        {
            record_command_error(snapshot_.active_command_id, code);
        }
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            record_command_error(queue_[i].command_id, code);
        }
        if (sync_id_ != 0)
            record_command_error(sync_id_, code);
        if (superimposed_id_ != 0)
            record_command_error(superimposed_id_, code);
        if (stream_id_ != 0)
            record_command_error(stream_id_, code);
        for (std::size_t i = 0; i < management_queue_.size(); ++i)
        {
            remember_management_result(management_queue_[i].command_id, code);
        }
        management_queue_.clear();
        abort_motion();
        snapshot_.error = true;
        snapshot_.error_id = code;
        snapshot_.status = AxisStatus::errorstop;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode reset_error()
    {
        if (snapshot_.status != AxisStatus::errorstop)
        {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.error = false;
        snapshot_.error_id = rt::ErrorCode::ok;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit(AxisCommand command)
    {
        if (group_blocks_standalone_motion())
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        return submit_impl(command);
    }

    rt::Result<std::uint32_t> submit_acceleration_profile(const ProfileSegment *segments,
                                                          std::size_t segment_count,
                                                          double acceleration_scale,
                                                          double acceleration_offset,
                                                          double time_scale)
    {
        if (group_blocks_standalone_motion() || !snapshot_.powered ||
            snapshot_.status == AxisStatus::errorstop || segments == nullptr ||
            segment_count == 0 || segment_count > QueueCapacity ||
            !std::isfinite(acceleration_scale) || !std::isfinite(acceleration_offset) ||
            !std::isfinite(time_scale) || time_scale <= 0.0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for (std::size_t i = 0; i < segment_count; ++i)
        {
            const double acceleration =
                segments[i].target * acceleration_scale + acceleration_offset;
            const double duration = static_cast<double>(segments[i].duration_cycles) * time_scale;
            if (!std::isfinite(acceleration) || segments[i].relative ||
                segments[i].duration_cycles <= 0 || !std::isfinite(duration) ||
                std::round(duration) < 1.0 || std::round(duration) >= std::ldexp(1.0, 63))
            {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
        }

        abort_motion();
        acceleration_segment_count_ = segment_count;
        acceleration_segment_index_ = 0;
        acceleration_segment_tick_ = 0;
        acceleration_profile_completed_ = false;
        for (std::size_t i = 0; i < segment_count; ++i)
        {
            AxisCommand storage{};
            storage.kind = CommandKind::acceleration_profile;
            storage.value = segments[i].target * acceleration_scale + acceleration_offset;
            storage.min_duration_cycles = static_cast<std::int64_t>(
                std::llround(static_cast<double>(segments[i].duration_cycles) * time_scale));
            queue_.push_back(storage);
        }
        active_ = true;
        active_command_ = {};
        active_command_.kind = CommandKind::acceleration_profile;
        active_command_.command_id = next_command_id_++;
        snapshot_.active_command_id = active_command_.command_id;
        snapshot_.active_command_reached_target = false;
        snapshot_.status = AxisStatus::continuous_motion;
        return rt::Result<std::uint32_t>::success(active_command_.command_id);
    }

    // Validate an aborting position command followed by buffered position
    // commands without changing axis state. Profile-table and homing facades
    // use this to reject the whole sequence before the first command starts.
    rt::ErrorCode preflight_position_sequence(const AxisCommand *commands,
                                              std::size_t command_count,
                                              bool enforce_soft_limits = false) const
    {
        if (commands == nullptr || command_count == 0 || command_count > QueueCapacity ||
            group_blocks_standalone_motion() || !snapshot_.powered ||
            snapshot_.status == AxisStatus::errorstop || snapshot_.error)
        {
            return rt::ErrorCode::invalid_argument;
        }

        const BufferMode first_mode = commands[0].buffer_mode;
        if(!is_valid_buffer_mode(first_mode))
        {
            return rt::ErrorCode::invalid_argument;
        }
        if(active_ && first_mode != BufferMode::aborting &&
           queue_.size() + command_count > QueueCapacity)
        {
            return rt::ErrorCode::capacity_exceeded;
        }
        double from_position = first_mode == BufferMode::aborting
                                   ? snapshot_.command_position
                                   : queued_endpoint();
        double from_velocity = first_mode == BufferMode::aborting
                                   ? snapshot_.command_velocity
                                   : 0.0;
        double from_acceleration = first_mode == BufferMode::aborting
                                       ? snapshot_.command_acceleration
                                       : 0.0;
        for (std::size_t i = 0; i < command_count; ++i)
        {
            const AxisCommand &command = commands[i];
            const BufferMode expected_mode = i == 0 ? first_mode : BufferMode::buffered;
            if (command.buffer_mode != expected_mode ||
                (command.kind != CommandKind::move_absolute &&
                 command.kind != CommandKind::move_relative) ||
                (command.kind == CommandKind::move_absolute &&
                 !is_valid_direction(command.direction)) ||
                !is_finite_command(command) || command.velocity <= 0.0 ||
                command.acceleration <= 0.0 || command.deceleration <= 0.0 || command.jerk <= 0.0 ||
                command.min_duration_cycles < 0)
            {
                return rt::ErrorCode::invalid_argument;
            }

            const double target = command.kind == CommandKind::move_relative
                                      ? from_position + command.value
                                      : command.value;
            if (!std::isfinite(target))
            {
                return rt::ErrorCode::invalid_argument;
            }
            if (!target_inside_limits(target, enforce_soft_limits))
            {
                return rt::ErrorCode::out_of_range;
            }

            const otg::Limits1D limits = scaled_limits(command);
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {from_position, from_velocity, from_acceleration}, {target, 0.0, 0.0}, limits);
            if (!profile)
            {
                return profile.error();
            }

            from_position = target;
            from_velocity = 0.0;
            from_acceleration = 0.0;
        }
        return rt::ErrorCode::ok;
    }

  private:
    friend class AxisGroup;

    static rt::Result<MotionLimits> si_to_limits(const rt::CycleConfig &cycle,
                                                 const AxisSiConfig &config,
                                                 MotionLimits base)
    {
        if(cycle.period_ns() <= 0) {
            return rt::Result<MotionLimits>::failure(rt::ErrorCode::invalid_argument);
        }
        base.max_velocity = cycle.velocity_to_cycle(config.max_velocity);
        base.max_acceleration = cycle.acceleration_to_cycle(config.max_acceleration);
        base.max_deceleration = cycle.acceleration_to_cycle(config.max_deceleration);
        base.max_jerk = cycle.jerk_to_cycle(config.max_jerk);
        base.min_position = config.min_position;
        base.max_position = config.max_position;
        base.min_position_enabled = config.min_position_enabled;
        base.max_position_enabled = config.max_position_enabled;
        if(!std::isfinite(base.max_velocity) || !std::isfinite(base.max_acceleration) ||
           !std::isfinite(base.max_deceleration) || !std::isfinite(base.max_jerk) ||
           (config.min_position_enabled && !std::isfinite(config.min_position)) ||
           (config.max_position_enabled && !std::isfinite(config.max_position)) ||
           (config.min_position_enabled && config.max_position_enabled &&
            config.min_position > config.max_position) ||
           base.max_velocity <= 0.0 || base.max_acceleration <= 0.0 ||
           base.max_deceleration <= 0.0 || base.max_jerk <= 0.0) {
            return rt::Result<MotionLimits>::failure(rt::ErrorCode::invalid_argument);
        }
        return rt::Result<MotionLimits>::success(base);
    }

    rt::ErrorCode set_group_owner(void *owner, const GroupStatus *status = nullptr)
    {
        group_owner_ = owner;
        group_status_ = owner == nullptr ? nullptr : status;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_group_owned(AxisCommand command)
    {
        return submit_impl(command);
    }

    rt::ErrorCode preflight_group_owned(AxisCommand command) const
    {
        if (!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
            !is_valid_buffer_mode(command.buffer_mode) ||
            !is_finite_command(command) || command.velocity <= 0.0 || command.acceleration <= 0.0 ||
            command.deceleration <= 0.0 || command.jerk <= 0.0 ||
            (command.kind != CommandKind::move_absolute &&
             command.kind != CommandKind::move_relative))
        {
            return rt::ErrorCode::invalid_argument;
        }

        command = normalize(command, queued_endpoint());
        if (!target_inside_limits(command.value))
        {
            return rt::ErrorCode::out_of_range;
        }
        const otg::Limits1D limits = scaled_limits(command);
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity,
                                    snapshot_.command_acceleration},
                                   {command.value, 0.0, 0.0}, limits);
        return profile ? rt::ErrorCode::ok : profile.error();
    }

    void abort_group_owned_motion()
    {
        abort_motion();
        if (snapshot_.status != AxisStatus::errorstop)
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
    }

    // KB-068: once the owning group leaves standby, public member motion
    // entry points must not become a second writer.
    bool group_blocks_standalone_motion() const
    {
        return group_owner_ != nullptr &&
               (group_status_ == nullptr || *group_status_ != GroupStatus::standby);
    }

    // Pure admissibility prefix of submit_impl(): every rejection that only
    // reads state, in strict first-match order — each branch returns a
    // different code, so reordering them is an observable behavior change.
    //
    // The seam sits exactly at the first mutation in submit_impl(), the
    // command-id allocation. The two later rejections (the acceleration
    // profile takeover rule and the soft-limit check) deliberately stay
    // behind that allocation: a command rejected there has already consumed
    // a command id, and hoisting them would shift the observable command-id
    // sequence, which the golden replay baseline pins.
    rt::ErrorCode submit_admissibility(const AxisCommand &command) const
    {
        const bool torque = command.kind == CommandKind::torque;
        if (!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
            !is_valid_buffer_mode(command.buffer_mode) ||
            !is_finite_command(command) ||
            (torque ? (command.velocity < 0.0 || command.acceleration < 0.0 ||
                       command.deceleration < 0.0 || command.jerk < 0.0 ||
                       !is_valid_direction(command.direction) ||
                       command.direction == Direction::shortest_way)
                    : (command.velocity <= 0.0 || command.acceleration <= 0.0 ||
                       command.deceleration <= 0.0 || command.jerk <= 0.0)))
        {
            return rt::ErrorCode::invalid_argument;
        }
        if(torque && command.buffer_mode != BufferMode::aborting) {
            // A torque command has no natural completion point. Matching the
            // Beckhoff CST contract, only an aborting takeover is defined.
            return rt::ErrorCode::unsupported;
        }
        if (stop_lock_id_ != 0 && command.kind != CommandKind::stop)
        {
            return rt::ErrorCode::precondition_failed;
        }
        if ((command.kind == CommandKind::move_absolute ||
             command.kind == CommandKind::move_continuous_absolute) &&
            !is_valid_direction(command.direction))
        {
            return rt::ErrorCode::invalid_argument;
        }

        // Queueing a motion command behind an engaged synchronization or a
        // stream session has no defined completion point; only aborting
        // commands may take over.
        if ((sync_kind_ != SyncKind::none || stream_active_) &&
            command.buffer_mode != BufferMode::aborting)
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (is_continuous_kind(command.kind) && command.end_velocity == 0.0)
        {
            return rt::ErrorCode::unsupported;
        }
        const double direction_reference =
            command.buffer_mode == BufferMode::aborting
                ? snapshot_.command_position
                : queued_endpoint();
        if (!command_direction_enabled(command, direction_reference))
        {
            return rt::ErrorCode::precondition_failed;
        }
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_impl(AxisCommand command)
    {
        const rt::ErrorCode admissible = submit_admissibility(command);
        if (admissible != rt::ErrorCode::ok)
        {
            return rt::Result<std::uint32_t>::failure(admissible);
        }

        if (command.command_id == 0)
        {
            command.command_id = next_command_id_++;
        }

        if (command.kind == CommandKind::torque)
        {
            const double prior_torque = snapshot_.command_torque;
            abort_motion();
            active_ = true;
            active_command_ = command;
            torque_command_id_ = command.command_id;
            snapshot_.active_command_id = command.command_id;
            snapshot_.command_torque = command.torque_ramp == 0.0
                                           ? command.value
                                           : prior_torque;
            snapshot_.torque_velocity_limit = command.velocity;
            snapshot_.torque_acceleration_limit = command.acceleration;
            snapshot_.torque_deceleration_limit = command.deceleration;
            snapshot_.torque_jerk_limit = command.jerk;
            snapshot_.torque_direction = command.direction;
            snapshot_.torque_mode = true;
            snapshot_.status = AxisStatus::continuous_motion;
            return rt::Result<std::uint32_t>::success(command.command_id);
        }
        if (active_ && active_command_.kind == CommandKind::acceleration_profile &&
            command.buffer_mode != BufferMode::aborting)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        // The additive target resolves against the endpoint that was committed
        // before an aborting takeover discards it.
        const double takeover_endpoint = queued_endpoint();
        const AxisStatus takeover_status = snapshot_.status;
        // Aborting takeovers keep kinematic continuity: abort_motion() zeroes
        // the command velocity/acceleration, but the new command must plan
        // from the state the axis was actually in (KB-026).
        const double takeover_velocity = snapshot_.command_velocity;
        const double takeover_acceleration = snapshot_.command_acceleration;
        if (command.buffer_mode == BufferMode::aborting)
        {
            abort_motion();
            snapshot_.command_velocity = takeover_velocity;
            snapshot_.command_acceleration = takeover_acceleration;
            snapshot_.actual_velocity = takeover_velocity;
            snapshot_.actual_acceleration = takeover_acceleration;
        }
        if (command.buffer_mode == BufferMode::aborting)
        {
            const double relative_base = snapshot_.command_position;
            const double additive_base = takeover_status == AxisStatus::discrete_motion
                                             ? takeover_endpoint
                                             : snapshot_.command_position;
            command =
                normalize(command, command.kind == CommandKind::move_additive ? additive_base
                                                                              : relative_base);
        }
        if ((command.kind == CommandKind::move_absolute ||
             command.kind == CommandKind::move_continuous_absolute ||
             command.kind == CommandKind::home) &&
            !target_inside_limits(command.value))
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::out_of_range);
        }

        if (command.buffer_mode == BufferMode::aborting || !active_)
        {
            const rt::ErrorCode started = start(command);
            if (started != rt::ErrorCode::ok)
            {
                return rt::Result<std::uint32_t>::failure(started);
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        const rt::ErrorCode queued = queue_.push_back(command);
        if (queued != rt::ErrorCode::ok)
        {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

  public:
    rt::Result<std::uint32_t> select_cam_table(const AxisModel *master,
                                               exec::CamTableView table,
                                               bool periodic,
                                               bool master_absolute,
                                               bool slave_absolute,
                                               std::uint32_t replace_id = 0)
    {
        table.periodic = periodic;
        if(master == nullptr || master == this || !table.valid() ||
           (!master_absolute && !table.periodic &&
            (table.points[0].master > 0.0 || table.points[table.size - 1].master < 0.0))) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        CamTableSelection *slot = nullptr;
        if(replace_id != 0) {
            for(CamTableSelection &selection : cam_table_selections_) {
                if(selection.id == replace_id) {
                    slot = &selection;
                    break;
                }
            }
        }
        if(slot == nullptr) {
            for(CamTableSelection &selection : cam_table_selections_) {
                if(selection.id == 0) {
                    slot = &selection;
                    break;
                }
            }
        }
        if(slot == nullptr) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }

        const std::uint32_t id = slot->id == 0 ? next_command_id_++ : slot->id;
        *slot = {id,
                 master,
                 table,
                 master_absolute,
                 slave_absolute,
                 master->snapshot().command_position,
                 snapshot_.command_position};
        return rt::Result<std::uint32_t>::success(id);
    }

    rt::Result<CamTableSelection> cam_table_selection(std::uint32_t id) const
    {
        if(id == 0) {
            return rt::Result<CamTableSelection>::failure(rt::ErrorCode::invalid_argument);
        }
        for(const CamTableSelection &selection : cam_table_selections_) {
            if(selection.id == id) {
                return rt::Result<CamTableSelection>::success(selection);
            }
        }
        return rt::Result<CamTableSelection>::failure(rt::ErrorCode::invalid_argument);
    }

    rt::Result<std::uint32_t> gear_in(const GearInCommand &command)
    {
        if (command.master == nullptr || command.master == this ||
            !std::isfinite(command.ratio_numerator) || !std::isfinite(command.ratio_denominator) ||
            command.ratio_denominator == 0.0 || !std::isfinite(command.master_sync_position) ||
            !std::isfinite(command.slave_sync_position) ||
            !std::isfinite(command.master_start_distance) ||
            !std::isfinite(command.approach_velocity) || command.approach_velocity < 0.0 ||
            !std::isfinite(command.acceleration) || command.acceleration < 0.0 ||
            !std::isfinite(command.deceleration) || command.deceleration < 0.0 ||
            !std::isfinite(command.jerk) || command.jerk < 0.0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if (command.sync_mode != SyncMode::shortest &&
            command.sync_mode != SyncMode::catch_up &&
            command.sync_mode != SyncMode::slow_down)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if (command.position_sync && command.sync_mode != SyncMode::shortest)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::gear, command.buffer_mode);
        if (!begun)
        {
            return begun;
        }
        sync_gear_ = command;
        phase_offset_ = 0.0;
        abort_phasing_commands();
        sync_profile_ready_ = false;
        sync_entry_phase_ = command.position_sync ? SyncPhase::waiting_window : SyncPhase::approaching;
        if (sync_phase_ != SyncPhase::queued)
        {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    rt::Result<std::uint32_t> cam_in(const CamInCommand &command)
    {
        if (command.master == nullptr || command.master == this || !command.table.valid() ||
            !std::isfinite(command.master_offset) || !std::isfinite(command.master_scaling) ||
            command.master_scaling == 0.0 || !std::isfinite(command.slave_offset) ||
            !std::isfinite(command.slave_scaling) || !std::isfinite(command.master_sync_position) ||
            !std::isfinite(command.master_start_distance) || command.master_start_distance < 0.0 ||
            !std::isfinite(command.approach_velocity) || command.approach_velocity < 0.0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        // Approved cam matrix: the spline reconstruction happens at engage
        // in the planning domain; a failed build rejects before any state
        // changes.
        if (command.interpolation == exec::CamInterpolation::spline)
        {
            const rt::ErrorCode built = cam_spline_.build(command.table);
            if (built != rt::ErrorCode::ok)
            {
                return rt::Result<std::uint32_t>::failure(built);
            }
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::cam, command.buffer_mode);
        if (!begun)
        {
            return begun;
        }
        sync_cam_ = command;
        sync_entry_phase_ =
            command.master_start_distance > 0.0 ? SyncPhase::waiting_window : SyncPhase::engaged;
        if (sync_phase_ != SyncPhase::queued)
        {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    rt::Result<std::uint32_t> combine_in(const CombineAxesCommand &command)
    {
        if (command.master1 == nullptr || command.master2 == nullptr || command.master1 == this ||
            command.master2 == this || !std::isfinite(command.ratio_numerator_m1) ||
            !std::isfinite(command.ratio_denominator_m1) || command.ratio_denominator_m1 == 0.0 ||
            !std::isfinite(command.ratio_numerator_m2) ||
            !std::isfinite(command.ratio_denominator_m2) || command.ratio_denominator_m2 == 0.0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::combine, command.buffer_mode);
        if (!begun)
        {
            return begun;
        }
        sync_combine_ = command;
        sync_entry_phase_ = SyncPhase::engaged;
        if (sync_phase_ != SyncPhase::queued)
        {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    // Approved cam matrix (decision #5): online table switch on an engaged
    // cam. The new geometry must agree with the current slave position at
    // the current master input (within tolerance); velocity/acceleration may
    // jump within the declared boundary — the slave rides the new table from
    // the next cycle. The engagement phase, sync id, and master stay.
    rt::ErrorCode cam_switch(const CamInCommand &command, double tolerance)
    {
        if (sync_kind_ != SyncKind::cam || sync_phase_ != SyncPhase::engaged ||
            command.master != sync_cam_.master || !command.table.valid() ||
            !std::isfinite(tolerance) || tolerance < 0.0 || command.master_start_distance != 0.0 ||
            !std::isfinite(command.master_offset) || !std::isfinite(command.master_scaling) ||
            command.master_scaling == 0.0 || !std::isfinite(command.slave_offset) ||
            !std::isfinite(command.slave_scaling))
        {
            return rt::ErrorCode::invalid_argument;
        }

        exec::CamSpline replacement{};
        if (command.interpolation == exec::CamInterpolation::spline)
        {
            const rt::ErrorCode built = replacement.build(command.table);
            if (built != rt::ErrorCode::ok)
            {
                return built;
            }
        }

        const double master_position = master_value(*sync_cam_.master, sync_cam_.source);
        const double table_input =
            (master_position - command.master_offset) / command.master_scaling;
        const rt::Result<double> sampled = command.interpolation == exec::CamInterpolation::spline
                                               ? replacement.sample(table_input)
                                               : command.table.sample(table_input);
        if (!sampled)
        {
            return sampled.error();
        }
        const double replacement_slave =
            command.slave_offset + command.slave_scaling * sampled.value();
        const rt::Result<double> current = cam_slave_value(master_position);
        if (!current)
        {
            return current.error();
        }
        if (std::fabs(replacement_slave - current.value()) > tolerance)
        {
            return rt::ErrorCode::invalid_argument;
        }

        sync_cam_.table = command.table;
        sync_cam_.interpolation = command.interpolation;
        sync_cam_.master_offset = command.master_offset;
        sync_cam_.master_scaling = command.master_scaling;
        sync_cam_.slave_offset = command.slave_offset;
        sync_cam_.slave_scaling = command.slave_scaling;
        if (command.interpolation == exec::CamInterpolation::spline)
        {
            cam_spline_ = replacement;
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode gear_update(double ratio_numerator, double ratio_denominator)
    {
        if (sync_kind_ != SyncKind::gear || !std::isfinite(ratio_numerator) ||
            !std::isfinite(ratio_denominator) || ratio_denominator == 0.0)
        {
            return rt::ErrorCode::invalid_argument;
        }
        sync_gear_.ratio_numerator = ratio_numerator;
        sync_gear_.ratio_denominator = ratio_denominator;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode cam_update(double master_offset, double master_scaling, double slave_offset,
                             double slave_scaling)
    {
        if (sync_kind_ != SyncKind::cam || !std::isfinite(master_offset) ||
            !std::isfinite(master_scaling) || master_scaling == 0.0 ||
            !std::isfinite(slave_offset) || !std::isfinite(slave_scaling))
        {
            return rt::ErrorCode::invalid_argument;
        }
        sync_cam_.master_offset = master_offset;
        sync_cam_.master_scaling = master_scaling;
        sync_cam_.slave_offset = slave_offset;
        sync_cam_.slave_scaling = slave_scaling;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode combine_update(CombineMode mode, double ratio_numerator_m1,
                                 double ratio_denominator_m1, double ratio_numerator_m2,
                                 double ratio_denominator_m2)
    {
        if (sync_kind_ != SyncKind::combine || !std::isfinite(ratio_numerator_m1) ||
            !std::isfinite(ratio_denominator_m1) || ratio_denominator_m1 == 0.0 ||
            !std::isfinite(ratio_numerator_m2) || !std::isfinite(ratio_denominator_m2) ||
            ratio_denominator_m2 == 0.0)
        {
            return rt::ErrorCode::invalid_argument;
        }
        sync_combine_.mode = mode;
        sync_combine_.ratio_numerator_m1 = ratio_numerator_m1;
        sync_combine_.ratio_denominator_m1 = ratio_denominator_m1;
        sync_combine_.ratio_numerator_m2 = ratio_numerator_m2;
        sync_combine_.ratio_denominator_m2 = ratio_denominator_m2;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode sync_out()
    {
        if (sync_kind_ == SyncKind::none)
        {
            return rt::ErrorCode::invalid_argument;
        }
        const double velocity = snapshot_.command_velocity;
        reset_sync();
        snapshot_.active_command_id = 0;
        if (velocity != 0.0 && snapshot_.powered)
        {
            AxisCommand hold{};
            hold.kind = CommandKind::move_velocity;
            hold.value = velocity < 0.0 ? -1.0 : 1.0;
            hold.velocity = std::fabs(velocity);
            hold.command_id = next_command_id_++;
            start(hold);
        }
        else
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
            snapshot_.command_velocity = 0.0;
            snapshot_.actual_velocity = 0.0;
        }
        return rt::ErrorCode::ok;
    }

    // B9 stream session (approved trajectory-stream matrix, decisions #9/#10).
    // Engaging is an aborting-class takeover: the filter starts from the
    // current kinematic state (no jump; a moving entry arms the filter's
    // controlled-stop ladder until the first target). Undefined combinations
    // are explicit errors: no engage while unpowered, in errorstop, gear/cam/
    // combine-synchronized, group-owned, or already streaming; axis software
    // position limits are not auto-applied — wire them through the filter
    // envelope in the config.
    rt::Result<std::uint32_t> stream_engage(const stream::StreamFilterConfig &config)
    {
        if (!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
            sync_kind_ != SyncKind::none || stream_active_ || group_owner_ != nullptr)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::ErrorCode configured = stream_filter_.configure(config);
        if (configured != rt::ErrorCode::ok)
        {
            return rt::Result<std::uint32_t>::failure(configured);
        }

        // Aborting takeovers keep kinematic continuity (KB-026 pattern):
        // abort_motion() zeroes the command velocity/acceleration, but the
        // filter must start from the state the axis was actually in.
        const double takeover_velocity = snapshot_.command_velocity;
        const double takeover_acceleration = snapshot_.command_acceleration;
        abort_motion();
        snapshot_.command_velocity = takeover_velocity;
        snapshot_.actual_velocity = takeover_velocity;
        snapshot_.command_acceleration = takeover_acceleration;
        snapshot_.actual_acceleration = takeover_acceleration;

        const rt::ErrorCode reset = stream_filter_.reset(
            {snapshot_.command_position, takeover_velocity, takeover_acceleration});
        if (reset != rt::ErrorCode::ok)
        {
            snapshot_.command_velocity = 0.0;
            snapshot_.actual_velocity = 0.0;
            snapshot_.command_acceleration = 0.0;
            snapshot_.actual_acceleration = 0.0;
            return rt::Result<std::uint32_t>::failure(reset);
        }
        stream_active_ = true;
        stream_id_ = next_command_id_++;
        snapshot_.status = AxisStatus::synchronized_motion;
        return rt::Result<std::uint32_t>::success(stream_id_);
    }

    rt::ErrorCode stream_push(const stream::StreamTarget &target)
    {
        if (!stream_active_)
        {
            return rt::ErrorCode::invalid_argument;
        }
        return stream_filter_.push_target(target);
    }

    // Graceful exit is only defined at rest; a moving session exits through
    // a standard aborting command (MC_Halt/MC_Stop/motion takeover).
    rt::ErrorCode stream_disengage()
    {
        if (!stream_active_)
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (snapshot_.command_velocity != 0.0 || snapshot_.command_acceleration != 0.0)
        {
            return rt::ErrorCode::precondition_failed;
        }
        stream_filter_.end_session();
        stream_active_ = false;
        stream_id_ = 0;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    std::uint32_t stream_session_id() const { return stream_id_; }

    // Read-only session introspection (mode, counters, clamped flag); only
    // meaningful while the session is engaged.
    const stream::StreamFilter1D &stream_filter() const { return stream_filter_; }

    SyncPhase sync_phase() const { return sync_phase_; }

    std::uint32_t sync_command_id() const { return sync_id_; }

    double gear_phase_offset() const { return phase_offset_; }

    bool phasing_active() const { return phasing_active_; }

    PhasingCommandState phasing_command_state(std::uint32_t command_id) const
    {
        for (std::size_t i = 0; i < phasing_result_count_; ++i)
        {
            const std::size_t index =
                (phasing_result_cursor_ + PhasingResultCapacity - 1 - i) % PhasingResultCapacity;
            if (phasing_results_[index].command_id == command_id)
            {
                return phasing_results_[index].state;
            }
        }
        return PhasingCommandState::unknown;
    }

    double phasing_covered_shift(std::uint32_t command_id) const
    {
        for (std::size_t i = 0; i < phasing_result_count_; ++i)
        {
            const std::size_t index =
                (phasing_result_cursor_ + PhasingResultCapacity - 1 - i) % PhasingResultCapacity;
            if (phasing_results_[index].command_id == command_id)
            {
                return phasing_results_[index].covered_shift;
            }
        }
        return 0.0;
    }

    bool gear_engaged_with(const AxisModel *master) const
    {
        return master != nullptr && sync_kind_ == SyncKind::gear &&
               sync_phase_ == SyncPhase::engaged && sync_gear_.master == master;
    }

    rt::Result<std::uint32_t> submit_phasing(PhasingCommand command)
    {
        if (sync_kind_ != SyncKind::gear || sync_phase_ != SyncPhase::engaged ||
            !std::isfinite(command.phase_shift) || !std::isfinite(command.velocity) ||
            !std::isfinite(command.acceleration) || !std::isfinite(command.deceleration) ||
            !std::isfinite(command.jerk) || command.velocity < 0.0 ||
            command.acceleration < 0.0 || command.deceleration < 0.0 || command.jerk < 0.0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if (command.buffer_mode != BufferMode::aborting &&
            command.buffer_mode != BufferMode::buffered)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        const bool direct = command.velocity == 0.0;
        if ((direct && (command.acceleration != 0.0 || command.deceleration != 0.0 ||
                        command.jerk != 0.0)) ||
            (!direct && (command.acceleration <= 0.0 || command.deceleration <= 0.0 ||
                         command.jerk <= 0.0)))
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if (command.buffer_mode == BufferMode::buffered &&
            phasing_active_ && phasing_queue_.size() >= QueueCapacity)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }
        if (command.buffer_mode == BufferMode::aborting && !direct)
        {
            const double target = command.relative ? phase_offset_ + command.phase_shift
                                                   : command.phase_shift;
            if (target != phase_offset_)
            {
                const otg::Limits1D limits{command.velocity, command.acceleration,
                                           command.deceleration, command.jerk};
                const rt::Result<otg::Profile1D> preflight = otg::plan_time_optimal(
                    {phase_offset_, phasing_velocity_, phasing_acceleration_},
                    {target, 0.0, 0.0}, limits);
                if (!preflight)
                {
                    return rt::Result<std::uint32_t>::failure(preflight.error());
                }
            }
        }

        command.command_id = next_command_id_++;
        remember_phasing(command.command_id, PhasingCommandState::queued, 0.0);
        if (command.buffer_mode == BufferMode::aborting)
        {
            abort_phasing_commands(true);
            remember_phasing(command.command_id, PhasingCommandState::queued, 0.0);
        }
        if (phasing_active_)
        {
            const rt::ErrorCode queued = phasing_queue_.push_back(command);
            if (queued != rt::ErrorCode::ok)
            {
                return rt::Result<std::uint32_t>::failure(queued);
            }
        }
        else
        {
            const rt::ErrorCode started = start_phasing(command);
            if (started != rt::ErrorCode::ok)
            {
                remember_phasing(command.command_id, PhasingCommandState::aborted, 0.0);
                return rt::Result<std::uint32_t>::failure(started);
            }
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    // Adapter hook: inject measured feedback without touching command state.
    rt::ErrorCode set_actual_feedback(double position, double velocity, double acceleration = 0.0,
                                      double torque = 0.0)
    {
        if (!std::isfinite(position) || !std::isfinite(velocity) || !std::isfinite(acceleration) ||
            !std::isfinite(torque))
        {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.actual_position = position;
        snapshot_.actual_velocity = velocity;
        snapshot_.actual_acceleration = acceleration;
        snapshot_.actual_torque = torque;
        return rt::ErrorCode::ok;
    }

    // MC_Home direct mode: remap the coordinate and mark the axis homed.
    rt::ErrorCode home_direct(double position)
    {
        if (!std::isfinite(position) || homing_step_precondition() != rt::ErrorCode::ok)
        {
            return rt::ErrorCode::invalid_argument;
        }
        abort_motion();
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        const rt::ErrorCode set = set_position(position);
        if (set != rt::ErrorCode::ok)
        {
            return set;
        }
        snapshot_.homed = true;
        homing_limits_suspended_ = false;
        return rt::ErrorCode::ok;
    }

    // Part 5 homed lifecycle: accepted Step FBs clear, FinishHoming sets.
    void clear_homed()
    {
        snapshot_.homed = false;
        homing_limits_suspended_ = true;
    }

    void set_homed()
    {
        snapshot_.homed = true;
        homing_limits_suspended_ = false;
    }

    void finish_homing()
    {
        set_homed();
        if (!has_standalone_motion() && snapshot_.status == AxisStatus::homing)
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
    }

    rt::ErrorCode finish_homing_now()
    {
        if (homing_step_precondition() != rt::ErrorCode::ok)
        {
            return rt::ErrorCode::invalid_argument;
        }
        abort_motion();
        set_homed();
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    // Digital IO banks. Adapters feed input levels through set_digital_input
    // and consume outputs written by the IO function blocks. The touch-probe
    // trigger channels are these digital inputs (v0.x Servo extension channel
    // semantics); probes capture on the rising edge evaluated inside cycle().
    // An input that is already high when the probe arms does not capture until
    // a fresh edge.
    static constexpr std::size_t DigitalInputCount = 4;
    static constexpr std::size_t DigitalOutputCount = 4;

    rt::ErrorCode set_digital_input(std::size_t input, bool level)
    {
        if (input >= DigitalInputCount)
        {
            return rt::ErrorCode::unsupported;
        }
        digital_input_[input] = level;
        return rt::ErrorCode::ok;
    }

    rt::Result<bool> digital_input(std::size_t input) const
    {
        if (input >= DigitalInputCount)
        {
            return rt::Result<bool>::failure(rt::ErrorCode::unsupported);
        }
        return rt::Result<bool>::success(digital_input_[input]);
    }

    rt::ErrorCode set_digital_output(std::size_t output, bool level)
    {
        if (output >= DigitalOutputCount)
        {
            return rt::ErrorCode::unsupported;
        }
        digital_output_[output] = level;
        return rt::ErrorCode::ok;
    }

    rt::Result<bool> digital_output(std::size_t output) const
    {
        if (output >= DigitalOutputCount)
        {
            return rt::Result<bool>::failure(rt::ErrorCode::unsupported);
        }
        return rt::Result<bool>::success(digital_output_[output]);
    }

    // Diagnostic info bits for MC_ReadAxisInfo. Defaults describe the built-in
    // simulation (ready, no switches, no warning); adapters override them.
    struct AxisInfoInputs
    {
        bool communication_ready = true;
        bool ready_for_power_on = true;
        bool home_abs_switch = false;
        bool limit_switch_pos = false;
        bool limit_switch_neg = false;
        bool warning = false;
    };

    void set_axis_info_inputs(const AxisInfoInputs &inputs) { axis_info_ = inputs; }

    const AxisInfoInputs &axis_info_inputs() const { return axis_info_; }

    rt::Result<std::uint32_t> arm_touch_probe(std::size_t input, bool window_only,
                                              double first_position, double last_position)
    {
        if (input >= DigitalInputCount)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if (window_only && (!std::isfinite(first_position) || !std::isfinite(last_position) ||
                            first_position > last_position))
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        ProbeSlot &slot = probes_[input];
        slot.armed = true;
        slot.captured = false;
        slot.window_only = window_only;
        slot.first_position = first_position;
        slot.last_position = last_position;
        slot.recorded_position = 0.0;
        slot.last_level = digital_input_[input];
        slot.command_id = next_command_id_++;
        return rt::Result<std::uint32_t>::success(slot.command_id);
    }

    // Disarming an idle input is not an error (matches the v0.x boundary).
    rt::ErrorCode abort_trigger(std::size_t input)
    {
        if (input >= DigitalInputCount)
        {
            return rt::ErrorCode::unsupported;
        }
        probes_[input].armed = false;
        probes_[input].captured = false;
        probes_[input].command_id = 0;
        return rt::ErrorCode::ok;
    }

    std::uint32_t probe_command_id(std::size_t input) const
    {
        return input < DigitalInputCount ? probes_[input].command_id : 0;
    }

    rt::Result<std::uint32_t> begin_passive_homing(std::size_t input)
    {
        if (!active_ || input >= DigitalInputCount || sync_kind_ != SyncKind::none ||
            stream_active_ || superimposed_active_ || group_blocks_standalone_motion())
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        if (passive_homing_id_ != 0)
        {
            passive_homing_aborted_id_ = passive_homing_id_;
            abort_trigger(passive_homing_input_);
        }
        const rt::Result<std::uint32_t> armed = arm_touch_probe(input, false, 0.0, 0.0);
        if (!armed)
            return armed;
        passive_homing_id_ = armed.value();
        passive_homing_input_ = input;
        return armed;
    }

    rt::ErrorCode finish_passive_homing(std::uint32_t owner)
    {
        if (owner == 0 || passive_homing_id_ != owner)
        {
            return rt::ErrorCode::precondition_failed;
        }
        passive_homing_id_ = 0;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> abort_passive_homing()
    {
        if (passive_homing_id_ == 0)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        const std::uint32_t owner = passive_homing_id_;
        abort_trigger(passive_homing_input_);
        passive_homing_aborted_id_ = owner;
        passive_homing_id_ = 0;
        return rt::Result<std::uint32_t>::success(owner);
    }

    std::uint32_t passive_homing_id() const { return passive_homing_id_; }
    std::uint32_t passive_homing_aborted_id() const { return passive_homing_aborted_id_; }

    bool probe_captured(std::size_t input) const
    {
        return input < DigitalInputCount && probes_[input].captured;
    }

    double probe_recorded_position(std::size_t input) const
    {
        return input < DigitalInputCount ? probes_[input].recorded_position : 0.0;
    }

    void cycle()
    {
        if (snapshot_.status == AxisStatus::errorstop)
        {
            return;
        }
        if(!has_standalone_motion())
        {
            cycle_management();
        }
        if (stream_cycle())
        {
            cycle_probes();
            return;
        }
        if (sync_cycle())
        {
            cycle_probes();
            return;
        }
        cycle_base_motion();
        cycle_superimposed();
        cycle_probes();
    }

    // Independent offset profile on top of the base motion (MC_MoveSuperimposed).
    rt::Result<std::uint32_t> submit_superimposed(double distance, double velocity,
                                                  double acceleration, double deceleration,
                                                  double jerk)
    {
        if (group_blocks_standalone_motion() || !snapshot_.powered ||
            snapshot_.status == AxisStatus::errorstop || sync_kind_ != SyncKind::none ||
            stream_active_ || !std::isfinite(distance) || !std::isfinite(velocity) ||
            velocity <= 0.0 || !std::isfinite(acceleration) || acceleration <= 0.0 ||
            !std::isfinite(deceleration) || deceleration <= 0.0 || !std::isfinite(jerk) ||
            jerk <= 0.0 || !direction_enabled(distance))
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        const otg::Limits1D limits =
            scaled_limits(velocity, acceleration, deceleration, jerk);
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({0.0, 0.0, 0.0}, {distance, 0.0, 0.0}, limits);
        if (!profile)
        {
            return rt::Result<std::uint32_t>::failure(profile.error());
        }

        superimposed_profile_ = profile.value();
        superimposed_tick_ = 0;
        superimposed_last_ = 0.0;
        superimposed_active_ = true;
        superimposed_halting_ = false;
        superimposed_id_ = next_command_id_++;
        superimposed_completed_id_ = 0;
        return rt::Result<std::uint32_t>::success(superimposed_id_);
    }

    // Stops only the superimposed offset; the base command keeps running. The
    // contribution accumulated so far persists in the command position.
    rt::ErrorCode halt_superimposed(double deceleration, double jerk)
    {
        if (!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
            !std::isfinite(deceleration) || deceleration <= 0.0 ||
            !std::isfinite(jerk) || jerk <= 0.0)
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (!superimposed_active_)
        {
            return rt::ErrorCode::ok;
        }

        const otg::State1D current = otg::sample(
            superimposed_profile_, rt::CycleTick::from_cycles(superimposed_tick_));
        if (current.velocity == 0.0 && current.acceleration == 0.0)
        {
            superimposed_active_ = false;
            superimposed_id_ = 0;
            superimposed_halting_ = false;
            superimposed_completed_id_ = 0;
            return rt::ErrorCode::ok;
        }

        double brake_velocity = current.velocity;
        double brake_shift = 0.0;
        if (current.acceleration != 0.0)
        {
            const double zero_cycles =
                std::ceil(std::fabs(current.acceleration) / jerk);
            brake_velocity += 0.5 * current.acceleration * zero_cycles;
            brake_shift += current.velocity * zero_cycles +
                           current.acceleration * zero_cycles * zero_cycles / 3.0;
        }
        const otg::Limits1D halt_limits{
            std::fabs(current.velocity) + std::fabs(brake_velocity) + 1e-9,
            std::fmax(std::fabs(current.acceleration), deceleration),
            deceleration, jerk};
        const double stop_position = current.position + brake_shift +
            otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
        const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
            current, {stop_position, 0.0, 0.0}, halt_limits);
        if (!halt)
        {
            return halt.error();
        }
        superimposed_profile_ = halt.value();
        superimposed_tick_ = 0;
        superimposed_last_ = current.position;
        superimposed_halting_ = true;
        superimposed_completed_id_ = 0;
        return rt::ErrorCode::ok;
    }

    void abort_superimposed()
    {
        superimposed_active_ = false;
        superimposed_id_ = 0;
        superimposed_completed_id_ = 0;
        superimposed_halting_ = false;
    }

    bool superimposed_active() const { return superimposed_active_; }

    std::uint32_t superimposed_command_id() const { return superimposed_id_; }

    std::uint32_t superimposed_completed_id() const { return superimposed_completed_id_; }

    double superimposed_distance() const { return superimposed_last_; }

    rt::ErrorCode update_superimposed_target(std::uint32_t command_id,
                                             double distance,
                                             double velocity,
                                             double acceleration,
                                             double deceleration,
                                             double jerk)
    {
        if(!superimposed_active_ || superimposed_halting_ || command_id == 0 ||
           command_id != superimposed_id_ || !std::isfinite(distance) ||
           !std::isfinite(velocity) || velocity <= 0.0 ||
           !std::isfinite(acceleration) || acceleration <= 0.0 ||
           !std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0 ||
           !direction_enabled(distance - superimposed_last_))
        {
            return rt::ErrorCode::invalid_argument;
        }
        const otg::State1D current = otg::sample(
            superimposed_profile_, rt::CycleTick::from_cycles(superimposed_tick_));
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            current, {distance, 0.0, 0.0},
            scaled_limits(velocity, acceleration, deceleration, jerk));
        if(!profile)
        {
            return profile.error();
        }
        superimposed_profile_ = profile.value();
        superimposed_tick_ = 0;
        superimposed_last_ = current.position;
        return rt::ErrorCode::ok;
    }

    // True while the command sits in the buffered queue (facades report Busy
    // for queued successors instead of misreading them as aborted).
    bool command_pending(std::uint32_t command_id) const
    {
        if (command_id == 0)
        {
            return false;
        }
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            if (queue_[i].command_id == command_id)
            {
                return true;
            }
        }
        return false;
    }

    bool management_command_pending(std::uint32_t command_id) const
    {
        if(command_id == 0) return false;
        for(std::size_t i = 0; i < management_queue_.size(); ++i) {
            if(management_queue_[i].command_id == command_id) return true;
        }
        return false;
    }

    bool management_command_active(std::uint32_t command_id) const
    {
        return command_id != 0 && active_management_id_ == command_id;
    }

    bool management_command_done(std::uint32_t command_id) const
    {
        for(std::size_t i = 0; i < management_results_.size(); ++i) {
            if(management_results_[i].command_id == command_id) {
                return management_results_[i].error == rt::ErrorCode::ok;
            }
        }
        return false;
    }

    rt::ErrorCode management_command_error(std::uint32_t command_id) const
    {
        for(std::size_t i = 0; i < management_results_.size(); ++i) {
            if(management_results_[i].command_id == command_id) {
                return management_results_[i].error;
            }
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode command_error(std::uint32_t command_id) const
    {
        if (command_id == 0)
        {
            return rt::ErrorCode::ok;
        }
        for (std::size_t offset = 0; offset < command_error_count_; ++offset)
        {
            const std::size_t index =
                (command_error_cursor_ + CommandErrorCapacity - 1 - offset) % CommandErrorCapacity;
            if (command_errors_[index].command_id == command_id)
            {
                return command_errors_[index].error;
            }
        }
        return rt::ErrorCode::ok;
    }

    // A function-block-local runtime failure terminates only the owning
    // command. The first buffered successor takes over from the live state.
    rt::ErrorCode fail_command(std::uint32_t command_id, rt::ErrorCode code)
    {
        if (command_id == 0 || code == rt::ErrorCode::ok || !active_ ||
            snapshot_.active_command_id != command_id)
        {
            return rt::ErrorCode::invalid_argument;
        }
        record_command_error(command_id, code);
        active_ = false;
        continuous_holding_ = false;
        override_braking_ = false;
        override_paused_ = false;
        blend_armed_ = false;
        snapshot_.active_command_id = 0;
        snapshot_.active_command_reached_target = false;
        snapshot_.command_acceleration = 0.0;
        snapshot_.actual_acceleration = 0.0;
        snapshot_.status = active_command_.homing ? AxisStatus::homing
                                                  : (snapshot_.powered ? AxisStatus::standstill
                                                                       : AxisStatus::disabled);
        start_next_queued();
        if (!active_)
        {
            base_velocity_ = 0.0;
            snapshot_.command_velocity = 0.0;
            snapshot_.actual_velocity = 0.0;
        }
        return rt::ErrorCode::ok;
    }

    std::uint32_t torque_command_id() const { return torque_command_id_; }

    bool command_in_velocity(std::uint32_t command_id) const
    {
        return command_id != 0 && active_ && snapshot_.active_command_id == command_id &&
               active_command_.kind == CommandKind::move_velocity &&
               std::fabs(snapshot_.command_velocity - signed_velocity(active_command_)) <= 1e-12;
    }

    bool command_in_end_velocity(std::uint32_t command_id) const
    {
        return command_id != 0 && active_ && snapshot_.active_command_id == command_id &&
               is_continuous_kind(active_command_.kind) &&
               snapshot_.active_command_reached_target &&
               std::fabs(snapshot_.command_velocity - continuous_hold_velocity_) <= 1e-12;
    }

    double command_torque() const { return snapshot_.command_torque; }

    rt::ErrorCode release_stop(std::uint32_t command_id)
    {
        if (command_id == 0 || stop_lock_id_ != command_id)
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (active_ && snapshot_.active_command_id == command_id)
        {
            stop_release_requested_ = true;
            return rt::ErrorCode::ok;
        }
        stop_lock_id_ = 0;
        stop_release_requested_ = false;
        if (snapshot_.status == AxisStatus::stopping)
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
        return rt::ErrorCode::ok;
    }

    // ContinuousUpdate for the active velocity command (MC_MoveVelocity,
    // KB-009): the new signed direction and magnitude apply from the next
    // cycle; the override keeps scaling per cycle.
    rt::ErrorCode update_active_velocity(std::uint32_t command_id, double direction_value,
                                         double velocity)
    {
        if (!active_ || snapshot_.active_command_id != command_id || command_id == 0 ||
            active_command_.kind != CommandKind::move_velocity || !std::isfinite(direction_value) ||
            !std::isfinite(velocity) || velocity <= 0.0 ||
            !direction_enabled(direction_value))
        {
            return rt::ErrorCode::invalid_argument;
        }
        active_command_.value = direction_value;
        active_command_.velocity = velocity;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode update_active_torque(std::uint32_t command_id,
                                       const AxisCommand &command)
    {
        if(!active_ || snapshot_.active_command_id != command_id ||
           command_id == 0 || active_command_.kind != CommandKind::torque ||
           command.kind != CommandKind::torque || !is_finite_command(command) ||
           command.velocity < 0.0 || command.acceleration < 0.0 ||
           command.deceleration < 0.0 || command.jerk < 0.0 ||
           !is_valid_direction(command.direction) ||
           command.direction == Direction::shortest_way ||
           command.buffer_mode != BufferMode::aborting) {
            return rt::ErrorCode::invalid_argument;
        }
        active_command_.value = command.value;
        active_command_.torque_ramp = command.torque_ramp;
        active_command_.velocity = command.velocity;
        active_command_.acceleration = command.acceleration;
        active_command_.deceleration = command.deceleration;
        active_command_.jerk = command.jerk;
        active_command_.direction = command.direction;
        snapshot_.torque_velocity_limit = command.velocity;
        snapshot_.torque_acceleration_limit = command.acceleration;
        snapshot_.torque_deceleration_limit = command.deceleration;
        snapshot_.torque_jerk_limit = command.jerk;
        snapshot_.torque_direction = command.direction;
        return rt::ErrorCode::ok;
    }

    // Retargets an active position command
    // (ContinuousUpdate).
    rt::ErrorCode update_active_target(std::uint32_t command_id, double target)
    {
        if (group_blocks_standalone_motion() || !active_ ||
            snapshot_.active_command_id != command_id || command_id == 0 ||
            !std::isfinite(target) ||
            (!is_continuous_kind(active_command_.kind) &&
             active_command_.kind != CommandKind::move_absolute &&
             active_command_.kind != CommandKind::move_relative &&
             active_command_.kind != CommandKind::move_additive))
        {
            return rt::ErrorCode::invalid_argument;
        }
        if (!target_inside_limits(target))
        {
            return rt::ErrorCode::out_of_range;
        }
        if (!direction_enabled(target - snapshot_.command_position))
        {
            return rt::ErrorCode::precondition_failed;
        }

        double end_velocity = 0.0;
        if (is_continuous_kind(active_command_.kind))
        {
            end_velocity = active_command_.end_velocity * override_;
        }
        const otg::Limits1D limits = scaled_limits(active_command_);
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity,
                                    snapshot_.command_acceleration},
                                   {target, end_velocity, 0.0}, limits);
        if (!profile)
        {
            return profile.error();
        }

        active_profile_ = profile.value();
        active_tick_ = 0;
        active_last_sample_ = snapshot_.command_position;
        active_target_ = target;
        active_command_.value = target;
        continuous_holding_ = false;
        continuous_hold_velocity_ = end_velocity;
        snapshot_.active_command_reached_target = false;
        return rt::ErrorCode::ok;
    }

  private:
    void set_synchronized_position(double position) { set_synchronized_state(position, 0.0, 0.0); }

    void set_synchronized_state(double position, double velocity, double acceleration)
    {
        snapshot_.status = AxisStatus::synchronized_motion;
        snapshot_.command_position = position;
        snapshot_.actual_position = position;
        snapshot_.command_velocity = velocity;
        snapshot_.actual_velocity = velocity;
        snapshot_.command_acceleration = acceleration;
        snapshot_.actual_acceleration = acceleration;
    }

    void clear_synchronized()
    {
        reset_sync();
        if (snapshot_.status == AxisStatus::synchronized_motion)
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
    }

    rt::Result<std::uint32_t> begin_sync(SyncKind kind, BufferMode buffer_mode)
    {
        // Synchronizing a streaming axis is undefined (approved stream
        // matrix, decision #10): explicit error, not a takeover.
        if (group_blocks_standalone_motion() || !snapshot_.powered ||
            snapshot_.status == AxisStatus::errorstop || stream_active_)
        {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if (buffer_mode == BufferMode::aborting)
        {
            abort_motion();
        }
        else if (sync_kind_ != SyncKind::none)
        {
            // Re-synchronizing a synchronized axis is only defined as a takeover.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        // Synchronization owns the axis position; a running superimposed offset
        // cannot compose with it.
        reset_superimposed();
        sync_kind_ = kind;
        sync_id_ = next_command_id_++;
        sync_phase_ = active_ ? SyncPhase::queued : SyncPhase::idle;
        return rt::Result<std::uint32_t>::success(sync_id_);
    }

    void reset_sync()
    {
        abort_phasing_commands();
        sync_kind_ = SyncKind::none;
        sync_phase_ = SyncPhase::idle;
        sync_entry_phase_ = SyncPhase::idle;
        sync_id_ = 0;
        phase_offset_ = 0.0;
        sync_profile_ready_ = false;
    }

    double master_value(const AxisModel &master, MasterValueSource source) const
    {
        const AxisSnapshot &snapshot = master.snapshot();
        return source == MasterValueSource::actual ? snapshot.actual_position
                                                   : snapshot.command_position;
    }

    double master_velocity(const AxisModel &master, MasterValueSource source) const
    {
        const AxisSnapshot &snapshot = master.snapshot();
        return source == MasterValueSource::actual ? snapshot.actual_velocity
                                                   : snapshot.command_velocity;
    }

    double sync_master_sync_position() const
    {
        return sync_kind_ == SyncKind::gear ? sync_gear_.master_sync_position
                                            : sync_cam_.master_sync_position;
    }

    double sync_master_start_distance() const
    {
        return sync_kind_ == SyncKind::gear ? sync_gear_.master_start_distance
                                            : sync_cam_.master_start_distance;
    }

    double sync_approach_velocity() const
    {
        return sync_kind_ == SyncKind::gear ? sync_gear_.approach_velocity
                                            : sync_cam_.approach_velocity;
    }

    // Slave setpoint at the moment the master crosses the sync position.
    rt::Result<double> sync_target_at_sync() const
    {
        if (sync_kind_ == SyncKind::gear)
        {
            return rt::Result<double>::success(sync_gear_.slave_sync_position);
        }
        return cam_slave_value(sync_master_sync_position());
    }

    rt::Result<double> cam_slave_value(double master_position) const
    {
        const double table_input =
            (master_position - sync_cam_.master_offset) / sync_cam_.master_scaling;
        const rt::Result<double> sampled = sync_cam_.interpolation == exec::CamInterpolation::spline
                                               ? cam_spline_.sample(table_input)
                                               : sync_cam_.table.sample(table_input);
        if (!sampled)
        {
            return sampled;
        }
        return rt::Result<double>::success(sync_cam_.slave_offset +
                                           sync_cam_.slave_scaling * sampled.value());
    }

    void remember_phasing(std::uint32_t command_id, PhasingCommandState state,
                          double covered_shift)
    {
        for (std::size_t i = 0; i < phasing_result_count_; ++i)
        {
            const std::size_t index =
                (phasing_result_cursor_ + PhasingResultCapacity - 1 - i) % PhasingResultCapacity;
            if (phasing_results_[index].command_id == command_id)
            {
                phasing_results_[index].state = state;
                phasing_results_[index].covered_shift = covered_shift;
                return;
            }
        }
        phasing_results_[phasing_result_cursor_] = {command_id, state, covered_shift};
        phasing_result_cursor_ = (phasing_result_cursor_ + 1) % PhasingResultCapacity;
        if (phasing_result_count_ < PhasingResultCapacity)
        {
            ++phasing_result_count_;
        }
    }

    void abort_phasing_commands(bool preserve_kinematics = false)
    {
        if (phasing_active_)
        {
            remember_phasing(phasing_id_, PhasingCommandState::aborted,
                              phase_offset_ - phasing_start_offset_);
        }
        for (std::size_t i = 0; i < phasing_queue_.size(); ++i)
        {
            remember_phasing(phasing_queue_[i].command_id, PhasingCommandState::aborted, 0.0);
        }
        phasing_queue_.clear();
        phasing_active_ = false;
        phasing_id_ = 0;
        if (!preserve_kinematics)
        {
            phasing_velocity_ = 0.0;
            phasing_acceleration_ = 0.0;
        }
        phasing_tick_ = 0;
    }

    rt::ErrorCode start_phasing(const PhasingCommand &command)
    {
        const double target = command.relative ? phase_offset_ + command.phase_shift
                                               : command.phase_shift;
        phasing_start_offset_ = phase_offset_;
        phasing_target_ = target;
        phasing_id_ = command.command_id;
        if (target == phase_offset_ || command.velocity == 0.0)
        {
            phase_offset_ = target;
            phasing_velocity_ = 0.0;
            phasing_acceleration_ = 0.0;
            phasing_active_ = false;
            remember_phasing(command.command_id, PhasingCommandState::completed,
                              phase_offset_ - phasing_start_offset_);
            phasing_id_ = 0;
            return rt::ErrorCode::ok;
        }

        const otg::Limits1D limits{command.velocity, command.acceleration,
                                   command.deceleration, command.jerk};
        const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(
            {phase_offset_, phasing_velocity_, phasing_acceleration_}, {target, 0.0, 0.0}, limits);
        if (!planned)
        {
            phasing_id_ = 0;
            return planned.error();
        }
        phasing_profile_ = planned.value();
        phasing_tick_ = 0;
        phasing_active_ = true;
        remember_phasing(command.command_id, PhasingCommandState::active, 0.0);
        return rt::ErrorCode::ok;
    }

    void start_next_phasing()
    {
        while (!phasing_active_ && !phasing_queue_.empty())
        {
            const PhasingCommand next = phasing_queue_[0];
            for (std::size_t i = 1; i < phasing_queue_.size(); ++i)
            {
                phasing_queue_[i - 1] = phasing_queue_[i];
            }
            phasing_queue_.pop_back();
            const rt::ErrorCode started = start_phasing(next);
            if (started != rt::ErrorCode::ok)
            {
                remember_phasing(next.command_id, PhasingCommandState::aborted, 0.0);
            }
        }
    }

    void advance_phasing()
    {
        if (!phasing_active_)
        {
            start_next_phasing();
            return;
        }
        ++phasing_tick_;
        const otg::State1D state =
            otg::sample(phasing_profile_, rt::CycleTick::from_cycles(phasing_tick_));
        phase_offset_ = state.position;
        phasing_velocity_ = state.velocity;
        phasing_acceleration_ = state.acceleration;
        remember_phasing(phasing_id_, PhasingCommandState::active,
                          phase_offset_ - phasing_start_offset_);
        if (phasing_tick_ >= phasing_profile_.duration_cycles())
        {
            phase_offset_ = phasing_target_;
            phasing_velocity_ = 0.0;
            phasing_acceleration_ = 0.0;
            remember_phasing(phasing_id_, PhasingCommandState::completed,
                              phase_offset_ - phasing_start_offset_);
            phasing_active_ = false;
            phasing_id_ = 0;
            start_next_phasing();
        }
    }

    rt::Result<double> sync_engaged_position()
    {
        if (sync_kind_ == SyncKind::gear)
        {
            advance_phasing();
            const double master = master_value(*sync_gear_.master, sync_gear_.source);
            const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
            return rt::Result<double>::success(master * ratio + phase_offset_);
        }
        if (sync_kind_ == SyncKind::cam)
        {
            return cam_slave_value(master_value(*sync_cam_.master, sync_cam_.source));
        }
        const double master1 =
            master_value(*sync_combine_.master1, sync_combine_.source_m1) *
            (sync_combine_.ratio_numerator_m1 / sync_combine_.ratio_denominator_m1);
        const double master2 =
            master_value(*sync_combine_.master2, sync_combine_.source_m2) *
            (sync_combine_.ratio_numerator_m2 / sync_combine_.ratio_denominator_m2);
        return rt::Result<double>::success(
            sync_combine_.mode == CombineMode::sub_axes ? master1 - master2 : master1 + master2);
    }

    double sync_engaged_velocity(double next_position) const
    {
        if (sync_kind_ == SyncKind::gear)
        {
            return master_velocity(*sync_gear_.master, sync_gear_.source) *
                   (sync_gear_.ratio_numerator / sync_gear_.ratio_denominator);
        }
        if (sync_kind_ == SyncKind::combine)
        {
            const double master1 =
                master_velocity(*sync_combine_.master1, sync_combine_.source_m1) *
                (sync_combine_.ratio_numerator_m1 / sync_combine_.ratio_denominator_m1);
            const double master2 =
                master_velocity(*sync_combine_.master2, sync_combine_.source_m2) *
                (sync_combine_.ratio_numerator_m2 / sync_combine_.ratio_denominator_m2);
            return sync_combine_.mode == CombineMode::sub_axes ? master1 - master2
                                                               : master1 + master2;
        }
        return next_position - snapshot_.command_position;
    }

    otg::Limits1D gear_limits() const
    {
        const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
        const double target_velocity =
            master_velocity(*sync_gear_.master, sync_gear_.source) * ratio;
        const double velocity = sync_gear_.position_sync
                                    ? (sync_gear_.approach_velocity > 0.0
                                           ? sync_gear_.approach_velocity
                                           : limits_.max_velocity)
                                    : std::fmax(std::fabs(snapshot_.command_velocity),
                                                std::fabs(target_velocity)) + 1e-9;
        return {velocity,
                sync_gear_.acceleration > 0.0 ? sync_gear_.acceleration
                                              : limits_.max_acceleration,
                sync_gear_.deceleration > 0.0 ? sync_gear_.deceleration
                                              : limits_.max_deceleration,
                sync_gear_.jerk > 0.0 ? sync_gear_.jerk : limits_.max_jerk};
    }

    rt::ErrorCode plan_gear_velocity_transition()
    {
        if (sync_gear_.acceleration == 0.0 && sync_gear_.deceleration == 0.0 &&
            sync_gear_.jerk == 0.0)
        {
            // The three optional dynamics inputs form one contract: all zero
            // selects unprofiled velocity locking, any nonzero value enables
            // the fully constrained engagement planner.
            sync_profile_ready_ = false;
            return rt::ErrorCode::ok;
        }
        const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
        const double target_velocity =
            master_velocity(*sync_gear_.master, sync_gear_.source) * ratio;
        if (std::fabs(snapshot_.command_velocity - target_velocity) <= 1e-12 &&
            std::fabs(snapshot_.command_acceleration) <= 1e-12)
        {
            sync_profile_ready_ = false;
            return rt::ErrorCode::ok;
        }
        const otg::Limits1D limits = gear_limits();
        const double ramp_distance =
            otg::detail::ramp_between(snapshot_.command_velocity, target_velocity, limits).distance;
        const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(
            {snapshot_.command_position, snapshot_.command_velocity,
             snapshot_.command_acceleration},
            {snapshot_.command_position + ramp_distance, target_velocity, 0.0}, limits);
        if (!planned)
        {
            return planned.error();
        }
        sync_profile_ = planned.value();
        sync_profile_tick_ = 0;
        sync_profile_target_velocity_ = target_velocity;
        sync_profile_ready_ = true;
        return rt::ErrorCode::ok;
    }

    int sync_window_direction() const
    {
        const double distance = sync_master_start_distance();
        if (distance > 0.0)
            return 1;
        if (distance < 0.0)
            return -1;
        const double velocity = sync_kind_ == SyncKind::gear
                                    ? master_velocity(*sync_gear_.master, sync_gear_.source)
                                    : master_velocity(*sync_cam_.master, sync_cam_.source);
        if (velocity > 0.0)
            return 1;
        if (velocity < 0.0)
            return -1;
        return sync_window_master_value() <= sync_master_sync_position() ? 1 : -1;
    }

    bool sync_reached(double master, double boundary) const
    {
        return sync_window_direction() > 0 ? master >= boundary : master <= boundary;
    }

    rt::ErrorCode plan_position_sync_profile()
    {
        const double master = sync_window_master_value();
        const double master_speed = sync_kind_ == SyncKind::gear
                                        ? master_velocity(*sync_gear_.master, sync_gear_.source)
                                        : master_velocity(*sync_cam_.master, sync_cam_.source);
        const double remaining = sync_master_sync_position() - master;
        if (remaining * master_speed <= 0.0)
        {
            return rt::ErrorCode::precondition_failed;
        }
        // The master setpoint is sampled before the slave in the same task.
        // One guard cycle keeps the fixed-time slave endpoint aligned with
        // the first cycle that observes the master crossing the sync point.
        const std::int64_t cycles = static_cast<std::int64_t>(
            std::ceil(std::fabs(remaining / master_speed))) + 1;
        const rt::Result<double> target = sync_target_at_sync();
        if (!target)
        {
            return target.error();
        }
        const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
        const double target_velocity = master_speed * ratio;
        const rt::Result<otg::Profile1D> planned = otg::solve_fixed_time(
            {snapshot_.command_position, snapshot_.command_velocity,
             snapshot_.command_acceleration},
            {target.value(), target_velocity, 0.0}, gear_limits(), cycles > 0 ? cycles : 1);
        if (!planned)
        {
            return planned.error();
        }
        sync_profile_ = planned.value();
        sync_profile_tick_ = 0;
        sync_profile_ready_ = true;
        approach_window_begin_ = sync_master_sync_position() - sync_master_start_distance();
        approach_start_slave_ = snapshot_.command_position;
        return rt::ErrorCode::ok;
    }

    void fail_sync_command(rt::ErrorCode code)
    {
        const std::uint32_t failed_id = sync_id_;
        reset_sync();
        record_command_error(failed_id, code);
        snapshot_.active_command_id = 0;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        snapshot_.command_acceleration = 0.0;
        snapshot_.actual_acceleration = 0.0;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
    }

    void enter_engaged()
    {
        if (sync_kind_ == SyncKind::gear)
        {
            const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
            phase_offset_ = sync_gear_.position_sync
                                ? sync_gear_.slave_sync_position -
                                      sync_gear_.master_sync_position * ratio
                                : snapshot_.command_position -
                                      master_value(*sync_gear_.master, sync_gear_.source) * ratio;
        }
        sync_profile_ready_ = false;
        sync_phase_ = SyncPhase::engaged;
        snapshot_.active_command_id = sync_id_;
        snapshot_.status = AxisStatus::synchronized_motion;
    }

    // Returns true when a stream session owns this cycle: the filter output
    // is the axis setpoint (the filter starts from the takeover state, so
    // the position domain is continuous with the pre-engage motion).
    bool stream_cycle()
    {
        if (!stream_active_)
        {
            return false;
        }
        const otg::State1D state = stream_filter_.cycle();
        snapshot_.command_position = state.position;
        snapshot_.actual_position = state.position;
        snapshot_.command_velocity = state.velocity;
        snapshot_.actual_velocity = state.velocity;
        snapshot_.command_acceleration = state.acceleration;
        snapshot_.actual_acceleration = state.acceleration;
        return true;
    }

    // Returns true when synchronization owns this cycle (idle motion excluded).
    bool sync_cycle()
    {
        if (sync_kind_ == SyncKind::none)
        {
            return false;
        }
        if (sync_kind_ == SyncKind::group_path)
        {
            return true;
        }
        if (sync_phase_ == SyncPhase::queued)
        {
            if (active_)
            {
                return false;
            }
            sync_phase_ = sync_entry_phase_;
        }

        if (sync_phase_ == SyncPhase::waiting_window)
        {
            const double master = sync_window_master_value();
            const double window_begin = sync_master_sync_position() - sync_master_start_distance();
            if (!sync_reached(master, window_begin))
            {
                return true;
            }
            if (!sync_reached(master, sync_master_sync_position()))
            {
                if (sync_kind_ == SyncKind::gear)
                {
                    const rt::ErrorCode planned = plan_position_sync_profile();
                    if (planned != rt::ErrorCode::ok)
                    {
                        fail_sync_command(planned);
                        return true;
                    }
                }
                else
                {
                    approach_window_begin_ = window_begin;
                    approach_start_slave_ = snapshot_.command_position;
                }
                sync_phase_ = SyncPhase::approaching;
            }
            else
            {
                enter_engaged();
            }
        }

        if (sync_phase_ == SyncPhase::approaching)
        {
            if (sync_kind_ == SyncKind::gear && !sync_gear_.position_sync)
            {
                if (!sync_profile_ready_)
                {
                    const rt::ErrorCode planned = plan_gear_velocity_transition();
                    if (planned != rt::ErrorCode::ok)
                    {
                        fail_sync_command(planned);
                        return true;
                    }
                    if (!sync_profile_ready_)
                    {
                        enter_engaged();
                        return true;
                    }
                }
                ++sync_profile_tick_;
                const otg::State1D state =
                    otg::sample(sync_profile_, rt::CycleTick::from_cycles(sync_profile_tick_));
                snapshot_.active_command_id = sync_id_;
                set_synchronized_state(state.position, state.velocity, state.acceleration);
                if (sync_profile_tick_ >= sync_profile_.duration_cycles())
                {
                    const double live_target =
                        master_velocity(*sync_gear_.master, sync_gear_.source) *
                        (sync_gear_.ratio_numerator / sync_gear_.ratio_denominator);
                    sync_profile_ready_ = false;
                    if (std::fabs(live_target - sync_profile_target_velocity_) <= 1e-12)
                    {
                        enter_engaged();
                    }
                }
                return true;
            }

            const double master = sync_window_master_value();
            if (sync_kind_ == SyncKind::gear && sync_gear_.position_sync)
            {
                if (sync_profile_tick_ < sync_profile_.duration_cycles())
                {
                    ++sync_profile_tick_;
                    const otg::State1D state = otg::sample(
                        sync_profile_, rt::CycleTick::from_cycles(sync_profile_tick_));
                    snapshot_.active_command_id = sync_id_;
                    set_synchronized_state(state.position, state.velocity, state.acceleration);
                    if (sync_profile_tick_ >= sync_profile_.duration_cycles())
                    {
                        if (!sync_reached(master, sync_master_sync_position()))
                        {
                            fail_sync_command(rt::ErrorCode::infeasible);
                        }
                        else
                        {
                            enter_engaged();
                        }
                    }
                    return true;
                }
                if (!sync_reached(master, sync_master_sync_position()))
                {
                    fail_sync_command(rt::ErrorCode::infeasible);
                    return true;
                }
                enter_engaged();
            }
            else if (sync_reached(master, sync_master_sync_position()))
            {
                enter_engaged();
            }
            else
            {
                const rt::Result<double> target_at_sync = sync_target_at_sync();
                if (!target_at_sync)
                {
                    trigger_error();
                    return true;
                }
                const double span = sync_master_sync_position() - approach_window_begin_;
                double progress = span > 0.0 ? (master - approach_window_begin_) / span : 1.0;
                progress = progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
                double target = approach_start_slave_ +
                                (target_at_sync.value() - approach_start_slave_) * progress;
                target = cap_approach_step(target);
                snapshot_.active_command_id = sync_id_;
                set_synchronized_position(target);
                return true;
            }
        }

        if (sync_phase_ == SyncPhase::engaged)
        {
            const rt::Result<double> position = sync_engaged_position();
            if (!position)
            {
                trigger_error();
                return true;
            }
            snapshot_.active_command_id = sync_id_;
            const double velocity = sync_engaged_velocity(position.value());
            set_synchronized_state(position.value(), velocity,
                                   velocity - snapshot_.command_velocity);
            return true;
        }
        return true;
    }

    double sync_window_master_value() const
    {
        return sync_kind_ == SyncKind::gear ? master_value(*sync_gear_.master, sync_gear_.source)
                                            : master_value(*sync_cam_.master, sync_cam_.source);
    }

    double cap_approach_step(double target) const
    {
        const double cap = sync_approach_velocity();
        if (cap <= 0.0)
        {
            return target;
        }
        const double step = target - snapshot_.command_position;
        if (step > cap)
        {
            return snapshot_.command_position + cap;
        }
        if (step < -cap)
        {
            return snapshot_.command_position - cap;
        }
        return target;
    }

    bool is_moving() const
    {
        return snapshot_.status == AxisStatus::discrete_motion ||
               snapshot_.status == AxisStatus::continuous_motion ||
               snapshot_.status == AxisStatus::synchronized_motion ||
               snapshot_.status == AxisStatus::stopping;
    }

    double queued_endpoint() const
    {
        double endpoint = active_ ? active_target_ : snapshot_.command_position;
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            const AxisCommand &queued = queue_[i];
            if (queued.kind == CommandKind::move_absolute ||
                queued.kind == CommandKind::move_continuous_absolute ||
                queued.kind == CommandKind::home)
            {
                endpoint = queued.value;
            }
        }
        return endpoint;
    }

    AxisCommand normalize(AxisCommand command, double takeover_endpoint) const
    {
        if (command.kind == CommandKind::move_relative ||
            command.kind == CommandKind::move_additive)
        {
            command.value = takeover_endpoint + command.value;
            command.kind = CommandKind::move_absolute;
        }
        else if (command.kind == CommandKind::move_continuous_relative)
        {
            command.value = takeover_endpoint + command.value;
            command.kind = CommandKind::move_continuous_absolute;
        }
        return command;
    }

    double signed_velocity(AxisCommand command) const
    {
        const double scaled = command.velocity * override_;
        return command.value < 0.0 ? -scaled : scaled;
    }

    otg::Limits1D scaled_limits(double velocity, double acceleration,
                                double deceleration, double jerk) const
    {
        return {velocity * override_, acceleration * acceleration_override_,
                deceleration * acceleration_override_, jerk * jerk_override_};
    }

    otg::Limits1D scaled_limits(const AxisCommand &command) const
    {
        return scaled_limits(command.velocity, command.acceleration,
                             command.deceleration, command.jerk);
    }

    bool direction_enabled(double delta) const
    {
        return delta == 0.0 || (delta > 0.0 ? positive_enabled_ : negative_enabled_);
    }

    bool command_direction_enabled(const AxisCommand &command,
                                   double reference_position) const
    {
        switch (command.kind)
        {
        case CommandKind::move_velocity:
        case CommandKind::torque:
        case CommandKind::move_relative:
        case CommandKind::move_additive:
        case CommandKind::move_continuous_relative:
            return direction_enabled(command.value);
        case CommandKind::move_absolute:
        case CommandKind::move_continuous_absolute:
        case CommandKind::home:
            return direction_enabled(command.value - reference_position);
        default:
            return true;
        }
    }

    bool active_direction_enabled() const
    {
        if (!active_ && sync_kind_ == SyncKind::none && !stream_active_)
        {
            return true;
        }
        if (snapshot_.command_velocity != 0.0)
        {
            return direction_enabled(snapshot_.command_velocity);
        }
        return !active_ || command_direction_enabled(active_command_,
                                                      snapshot_.command_position);
    }

    bool target_inside_limits(double target, bool enforce_soft_limits = false) const
    {
        if (homing_limits_suspended_ && !enforce_soft_limits)
        {
            return true;
        }
        if (limits_.min_position_enabled && target < limits_.min_position)
        {
            return false;
        }
        if (limits_.max_position_enabled && target > limits_.max_position)
        {
            return false;
        }
        return true;
    }

    rt::ErrorCode start(AxisCommand command)
    {
        if (command.kind == CommandKind::move_relative ||
            command.kind == CommandKind::move_additive ||
            command.kind == CommandKind::move_continuous_relative)
        {
            command = normalize(command, snapshot_.command_position);
        }
        active_command_ = command;
        active_tick_ = 0;
        continuous_holding_ = false;
        blend_armed_ = false;
        snapshot_.active_command_reached_target = false;
        snapshot_.active_command_id = command.command_id;
        snapshot_.command_torque_limit = command.torque_limit;

        if (command.kind == CommandKind::move_velocity)
        {
            active_ = true;
            snapshot_.status = command.homing ? AxisStatus::homing : AxisStatus::continuous_motion;
            return rt::ErrorCode::ok;
        }

        if (command.kind == CommandKind::halt || command.kind == CommandKind::stop)
        {
            if (command.kind == CommandKind::stop && command.lock_stopping)
            {
                stop_lock_id_ = command.command_id;
                stop_release_requested_ = false;
            }
            // Controlled stop (MC_Halt/MC_Stop carry over the v0.x contract):
            // decelerate from the current state with the commanded
            // deceleration/jerk. The braking target is exempt from the
            // software position limits — the axis must be allowed to come to
            // rest. A resting axis still finishes within one cycle.
            halt_profiled_ = false;
            const double v0 = snapshot_.command_velocity;
            const double a0 = snapshot_.command_acceleration;
            if (v0 != 0.0 || a0 != 0.0)
            {
                double brake_velocity = v0;
                double brake_shift = 0.0;
                if (a0 != 0.0)
                {
                    const double zero_cycles = std::ceil(std::fabs(a0) / command.jerk);
                    brake_velocity += 0.5 * a0 * zero_cycles;
                    brake_shift += v0 * zero_cycles + a0 * zero_cycles * zero_cycles / 3.0;
                }
                const otg::Limits1D halt_limits{
                    std::fabs(v0) + std::fabs(brake_velocity) + 1e-9,
                    std::fabs(a0) > command.acceleration ? std::fabs(a0) : command.acceleration,
                    command.deceleration, command.jerk};
                const double stop_position =
                    snapshot_.command_position + brake_shift +
                    otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
                const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
                    {snapshot_.command_position, v0, a0}, {stop_position, 0.0, 0.0}, halt_limits);
                if (halt)
                {
                    active_profile_ = halt.value();
                    active_last_sample_ = snapshot_.command_position;
                    active_target_ = stop_position;
                    halt_profiled_ = true;
                }
            }
            active_ = true;
            snapshot_.status = command.homing ? AxisStatus::homing : AxisStatus::stopping;
            return rt::ErrorCode::ok;
        }

        const double target = command.value;
        if (!target_inside_limits(target))
        {
            return rt::ErrorCode::out_of_range;
        }

        double target_velocity = 0.0;
        if (is_continuous_kind(command.kind))
        {
            target_velocity = command.end_velocity * override_;
        }

        active_target_ = target;
        const otg::Limits1D limits = scaled_limits(command);
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity,
                                    snapshot_.command_acceleration},
                                   {target, target_velocity, 0.0}, limits);
        if (!profile)
        {
            return profile.error();
        }

        active_profile_ = profile.value();
        active_last_sample_ = snapshot_.command_position;
        continuous_hold_velocity_ = target_velocity;
        active_ = true;
        snapshot_.status = command.homing
                               ? AxisStatus::homing
                               : (is_continuous_kind(command.kind) ? AxisStatus::continuous_motion
                                                                   : AxisStatus::discrete_motion);
        return rt::ErrorCode::ok;
    }

    void cycle_base_motion()
    {
        if (!active_)
        {
            return;
        }

        if (override_paused_)
        {
            return;
        }

        if (active_command_.kind == CommandKind::acceleration_profile)
        {
            if (!acceleration_profile_completed_)
            {
                const AxisCommand &segment = queue_[acceleration_segment_index_];
                const double acceleration = segment.value;
                snapshot_.command_acceleration = acceleration;
                snapshot_.actual_acceleration = acceleration;
                snapshot_.command_velocity += acceleration;
                snapshot_.actual_velocity = snapshot_.command_velocity;
                snapshot_.command_position += snapshot_.command_velocity;
                snapshot_.actual_position = snapshot_.command_position;
                base_velocity_ = snapshot_.command_velocity;
                ++acceleration_segment_tick_;
                if (acceleration_segment_tick_ >= segment.min_duration_cycles)
                {
                    acceleration_segment_tick_ = 0;
                    ++acceleration_segment_index_;
                    if (acceleration_segment_index_ >= acceleration_segment_count_)
                    {
                        acceleration_profile_completed_ = true;
                        snapshot_.active_command_reached_target = true;
                        snapshot_.command_acceleration = 0.0;
                        snapshot_.actual_acceleration = 0.0;
                    }
                }
            }
            else
            {
                snapshot_.command_position += snapshot_.command_velocity;
                snapshot_.actual_position = snapshot_.command_position;
            }
            return;
        }

        if (!override_braking_ &&
            (active_command_.kind == CommandKind::move_velocity ||
             active_command_.kind == CommandKind::torque || continuous_holding_))
        {
            if (active_command_.kind == CommandKind::torque)
            {
                const double delta =
                    active_command_.value - snapshot_.command_torque;
                if(active_command_.torque_ramp == 0.0 ||
                   std::fabs(delta) <= active_command_.torque_ramp) {
                    snapshot_.command_torque = active_command_.value;
                } else {
                    snapshot_.command_torque +=
                        delta < 0.0 ? -active_command_.torque_ramp
                                    : active_command_.torque_ramp;
                }
                snapshot_.command_velocity = 0.0;
                snapshot_.command_acceleration = 0.0;
                return;
            }
            const double velocity =
                continuous_holding_ ? continuous_hold_velocity_ : signed_velocity(active_command_);
            base_velocity_ = velocity;
            snapshot_.command_velocity = velocity;
            snapshot_.actual_velocity = velocity;
            snapshot_.command_acceleration = 0.0;
            snapshot_.actual_acceleration = 0.0;
            snapshot_.command_position += velocity;
            snapshot_.actual_position = snapshot_.command_position;
            if (!continuous_holding_ && active_command_.min_duration_cycles > 0)
            {
                ++active_tick_;
                if (active_tick_ >= active_command_.min_duration_cycles)
                {
                    finish_active();
                }
            }
            return;
        }

        if ((active_command_.kind == CommandKind::halt ||
             active_command_.kind == CommandKind::stop) &&
            !halt_profiled_)
        {
            finish_active();
            return;
        }

        ++active_tick_;
        const otg::State1D state =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        // Incremental application lets the superimposed offset compose with the
        // base profile without a second position bookkeeping domain.
        const double step = state.position - active_last_sample_;
        active_last_sample_ = state.position;
        snapshot_.command_position += step;
        snapshot_.command_velocity = state.velocity;
        snapshot_.command_acceleration = state.acceleration;
        snapshot_.actual_position = snapshot_.command_position;
        snapshot_.actual_velocity = state.velocity;
        snapshot_.actual_acceleration = state.acceleration;
        base_velocity_ = state.velocity;

        // KB-001 velocity-threshold blending: arm once the speed exceeds the
        // successor's threshold of the nominal command velocity, hand over
        // once it falls back below. Short moves that never arm degrade to
        // BUFFERED; homing, halt/stop, and continuous holds do not blend.
        const bool blend_eligible = active_command_.kind == CommandKind::move_absolute &&
                                    !queue_.empty() &&
                                    (queue_[0].buffer_mode == BufferMode::blending_low ||
                                     queue_[0].buffer_mode == BufferMode::blending_high);
        if (blend_eligible)
        {
            const double nominal = active_command_.velocity * override_;
            const double threshold =
                (queue_[0].buffer_mode == BufferMode::blending_low ? 0.3 : 0.7) * nominal;
            const double speed = std::fabs(state.velocity);
            if (speed > threshold)
            {
                blend_armed_ = true;
            }
            else if (blend_armed_)
            {
                blend_into_next();
                return;
            }
        }
        else
        {
            blend_armed_ = false;
        }

        if (active_tick_ >= active_profile_.duration_cycles())
        {
            if (override_braking_)
            {
                override_braking_ = false;
                override_paused_ = true;
                snapshot_.command_velocity = 0.0;
                snapshot_.actual_velocity = 0.0;
                snapshot_.command_acceleration = 0.0;
                snapshot_.actual_acceleration = 0.0;
                return;
            }
            // Timed profile segments hold at the target until their minimum
            // duration elapses (otg::sample keeps returning the finish state).
            if (active_tick_ < active_command_.min_duration_cycles)
            {
                return;
            }
            if (active_command_.kind == CommandKind::home)
            {
                snapshot_.homed = true;
            }
            if (is_continuous_kind(active_command_.kind))
            {
                snapshot_.active_command_reached_target = true;
                continuous_holding_ = true;
                snapshot_.status = AxisStatus::continuous_motion;
                return;
            }
            finish_active();
        }
    }

    void cycle_superimposed()
    {
        if (!superimposed_active_)
        {
            return;
        }
        if (!active_ && snapshot_.status == AxisStatus::standstill)
        {
            snapshot_.status = AxisStatus::discrete_motion;
        }

        ++superimposed_tick_;
        const otg::State1D state =
            otg::sample(superimposed_profile_, rt::CycleTick::from_cycles(superimposed_tick_));
        const double step = state.position - superimposed_last_;
        superimposed_last_ = state.position;
        snapshot_.command_position += step;
        snapshot_.actual_position = snapshot_.command_position;
        snapshot_.command_velocity = base_velocity_ + state.velocity;
        snapshot_.actual_velocity = snapshot_.command_velocity;

        if (superimposed_tick_ >= superimposed_profile_.duration_cycles())
        {
            superimposed_active_ = false;
            superimposed_completed_id_ =
                superimposed_halting_ ? 0 : superimposed_id_;
            superimposed_id_ = 0;
            superimposed_halting_ = false;
            snapshot_.command_velocity = base_velocity_;
            snapshot_.actual_velocity = base_velocity_;
            if (!active_ && snapshot_.status == AxisStatus::discrete_motion)
            {
                snapshot_.status = AxisStatus::standstill;
            }
        }
    }

    void finish_active()
    {
        const bool locked_stop =
            active_command_.kind == CommandKind::stop && active_command_.lock_stopping &&
            stop_lock_id_ == snapshot_.active_command_id && !stop_release_requested_;
        active_ = false;
        continuous_holding_ = false;
        base_velocity_ = 0.0;
        snapshot_.last_completed_command_id = snapshot_.active_command_id;
        snapshot_.active_command_id = 0;
        snapshot_.active_command_reached_target = false;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        snapshot_.command_acceleration = 0.0;
        snapshot_.actual_acceleration = 0.0;
        snapshot_.command_torque_limit = 0.0;
        snapshot_.status =
            locked_stop
                ? AxisStatus::stopping
                : (active_command_.homing
                       ? AxisStatus::homing
                       : (snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled));
        if (active_command_.kind == CommandKind::stop && active_command_.lock_stopping &&
            !locked_stop)
        {
            stop_lock_id_ = 0;
            stop_release_requested_ = false;
        }
        if (locked_stop)
        {
            return;
        }
        start_next_queued();
    }

    // KB-001 velocity-threshold blending: the queued successor takes over
    // while the active profile is still decelerating, planning from the live
    // state (the predecessor counts as completed at the handover point).
    void blend_into_next()
    {
        snapshot_.last_completed_command_id = snapshot_.active_command_id;
        blend_armed_ = false;
        start_next_queued();
    }

    void start_next_queued()
    {
        if (queue_.empty())
        {
            return;
        }

        const AxisCommand next = queue_[0];
        for (std::size_t i = 1; i < queue_.size(); ++i)
        {
            queue_[i - 1] = queue_[i];
        }
        queue_.pop_back();
        start(next);
    }

    void abort_motion()
    {
        active_ = false;
        active_tick_ = 0;
        continuous_holding_ = false;
        acceleration_profile_completed_ = false;
        acceleration_segment_count_ = 0;
        acceleration_segment_index_ = 0;
        acceleration_segment_tick_ = 0;
        override_braking_ = false;
        override_paused_ = false;
        base_velocity_ = 0.0;
        queue_.clear();
        reset_sync();
        reset_superimposed();
        if (stream_active_)
        {
            stream_filter_.end_session();
        }
        stream_active_ = false;
        stream_id_ = 0;
        torque_command_id_ = 0;
        stop_lock_id_ = 0;
        stop_release_requested_ = false;
        snapshot_.command_torque = 0.0;
        snapshot_.command_torque_limit = 0.0;
        snapshot_.torque_velocity_limit = 0.0;
        snapshot_.torque_acceleration_limit = 0.0;
        snapshot_.torque_deceleration_limit = 0.0;
        snapshot_.torque_jerk_limit = 0.0;
        snapshot_.torque_direction = Direction::current;
        snapshot_.torque_mode = false;
        if (snapshot_.status == AxisStatus::synchronized_motion)
        {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
        snapshot_.active_command_id = 0;
        snapshot_.active_command_reached_target = false;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        snapshot_.command_acceleration = 0.0;
        snapshot_.actual_acceleration = 0.0;
    }

    void reset_superimposed()
    {
        superimposed_active_ = false;
        superimposed_id_ = 0;
        superimposed_completed_id_ = 0;
        superimposed_tick_ = 0;
        superimposed_last_ = 0.0;
        superimposed_halting_ = false;
    }

    void cycle_probes()
    {
        for (std::size_t input = 0; input < DigitalInputCount; ++input)
        {
            ProbeSlot &slot = probes_[input];
            const bool level = digital_input_[input];
            const bool rising = level && !slot.last_level;
            slot.last_level = level;
            if (!slot.armed || !rising)
            {
                continue;
            }
            const double position = snapshot_.actual_position;
            if (slot.window_only &&
                (position < slot.first_position || position > slot.last_position))
            {
                continue;
            }
            slot.armed = false;
            slot.captured = true;
            slot.recorded_position = position;
        }
    }

    struct ProbeSlot
    {
        bool armed = false;
        bool captured = false;
        bool window_only = false;
        bool last_level = false;
        double first_position = 0.0;
        double last_position = 0.0;
        double recorded_position = 0.0;
        std::uint32_t command_id = 0;
    };

    struct CommandError
    {
        std::uint32_t command_id = 0;
        rt::ErrorCode error = rt::ErrorCode::ok;
    };

    struct AxisManagementResult
    {
        std::uint32_t command_id = 0;
        rt::ErrorCode error = rt::ErrorCode::ok;
    };

    rt::Result<std::uint32_t> submit_management(AxisManagementCommand command,
                                                bool queued)
    {
        if(queued) {
            command.command_id = next_command_id_++;
            const rt::ErrorCode added = management_queue_.push_back(command);
            return added == rt::ErrorCode::ok
                       ? rt::Result<std::uint32_t>::success(command.command_id)
                       : rt::Result<std::uint32_t>::failure(added);
        }
        const rt::ErrorCode result = execute_management(command);
        if(result != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(result);
        }
        return rt::Result<std::uint32_t>::success(last_management_command_id_);
    }

    rt::ErrorCode execute_management(AxisManagementCommand command)
    {
        if(command.command_id == 0) command.command_id = next_command_id_++;
        active_management_id_ = command.command_id;
        rt::ErrorCode result = rt::ErrorCode::ok;
        switch(command.kind) {
        case AxisManagementKind::set_position: {
            const AxisSnapshot before = snapshot_;
            const double delta = command.flag ? command.value
                                              : command.value - before.actual_position;
            const bool moving = before.status == AxisStatus::discrete_motion ||
                                before.status == AxisStatus::continuous_motion ||
                                before.status == AxisStatus::stopping;
            result = moving ? shift_coordinates(delta)
                            : set_position(before.actual_position + delta);
            break;
        }
        case AxisManagementKind::write_parameter:
            result = write_parameter(command.parameter, command.value);
            break;
        case AxisManagementKind::write_bool_parameter:
            result = write_bool_parameter(command.parameter, command.flag);
            break;
        case AxisManagementKind::write_digital_output:
            result = set_digital_output(command.channel, command.flag);
            break;
        }
        remember_management_result(command.command_id, result);
        last_management_command_id_ = command.command_id;
        active_management_id_ = 0;
        return result;
    }

    void cycle_management()
    {
        if(management_queue_.empty()) return;
        const AxisManagementCommand command = management_queue_[0];
        for(std::size_t i = 1; i < management_queue_.size(); ++i) {
            management_queue_[i - 1] = management_queue_[i];
        }
        management_queue_.pop_back();
        execute_management(command);
    }

    void remember_management_result(std::uint32_t command_id, rt::ErrorCode error)
    {
        if(management_results_.size() == QueueCapacity) {
            for(std::size_t i = 1; i < management_results_.size(); ++i) {
                management_results_[i - 1] = management_results_[i];
            }
            management_results_.pop_back();
        }
        management_results_.push_back({command_id, error});
    }

    // Active + full buffered queue + the independent sync/superimposed/stream
    // owners must all retain axis-error attribution in the same cycle.
    static constexpr std::size_t CommandErrorCapacity = QueueCapacity + 4;

    void record_command_error(std::uint32_t command_id, rt::ErrorCode code)
    {
        if (command_id == 0 || code == rt::ErrorCode::ok)
        {
            return;
        }
        command_errors_[command_error_cursor_] = {command_id, code};
        command_error_cursor_ = (command_error_cursor_ + 1) % CommandErrorCapacity;
        if (command_error_count_ < CommandErrorCapacity)
        {
            ++command_error_count_;
        }
    }

    void *group_owner_ = nullptr;
    const GroupStatus *group_status_ = nullptr;
    double active_target_ = 0.0;
    double active_last_sample_ = 0.0;
    double base_velocity_ = 0.0;
    double continuous_hold_velocity_ = 0.0;
    double override_ = 1.0;
    double acceleration_override_ = 1.0;
    double jerk_override_ = 1.0;
    std::int64_t active_tick_ = 0;
    std::int64_t superimposed_tick_ = 0;
    double superimposed_last_ = 0.0;
    static constexpr std::size_t PhasingResultCapacity = 16;
    double phase_offset_ = 0.0;
    double phasing_target_ = 0.0;
    double phasing_start_offset_ = 0.0;
    double phasing_velocity_ = 0.0;
    double phasing_acceleration_ = 0.0;
    double approach_window_begin_ = 0.0;
    double approach_start_slave_ = 0.0;
    double sync_profile_target_velocity_ = 0.0;
    std::int64_t sync_profile_tick_ = 0;
    std::int64_t phasing_tick_ = 0;
    std::size_t command_error_cursor_ = 0;
    std::size_t command_error_count_ = 0;
    std::size_t acceleration_segment_count_ = 0;
    std::size_t acceleration_segment_index_ = 0;
    std::int64_t acceleration_segment_tick_ = 0;
    std::size_t passive_homing_input_ = 0;
    MotionLimits limits_{};
    GearInCommand sync_gear_{};
    CombineAxesCommand sync_combine_{};
    AxisSnapshot snapshot_{};
    AxisCommand active_command_{};
    CamInCommand sync_cam_{};
    std::array<CamTableSelection, QueueCapacity> cam_table_selections_{};
    std::array<ProbeSlot, DigitalInputCount> probes_{};
    rt::StaticVector<AxisCommand, QueueCapacity> queue_{};
    rt::StaticVector<AxisManagementCommand, QueueCapacity> management_queue_{};
    rt::StaticVector<AxisManagementResult, QueueCapacity> management_results_{};
    rt::StaticVector<PhasingCommand, QueueCapacity> phasing_queue_{};
    exec::CamSpline cam_spline_{};
    otg::Profile1D active_profile_{};
    otg::Profile1D superimposed_profile_{};
    otg::Profile1D sync_profile_{};
    otg::Profile1D phasing_profile_{};
    stream::StreamFilter1D stream_filter_{};
    int domain_id_ = 0;
    std::uint32_t next_command_id_ = 1;
    std::uint32_t active_management_id_ = 0;
    std::uint32_t last_management_command_id_ = 0;
    std::uint32_t superimposed_id_ = 0;
    std::uint32_t superimposed_completed_id_ = 0;
    SyncKind sync_kind_ = SyncKind::none;
    SyncPhase sync_phase_ = SyncPhase::idle;
    SyncPhase sync_entry_phase_ = SyncPhase::idle;
    std::uint32_t sync_id_ = 0;
    std::uint32_t phasing_id_ = 0;
    std::uint32_t stream_id_ = 0;
    std::uint32_t torque_command_id_ = 0;
    std::uint32_t stop_lock_id_ = 0;
    std::uint32_t passive_homing_id_ = 0;
    std::uint32_t passive_homing_aborted_id_ = 0;
    std::array<CommandError, CommandErrorCapacity> command_errors_{};
    std::array<PhasingCommandResult, PhasingResultCapacity> phasing_results_{};
    std::size_t phasing_result_cursor_ = 0;
    std::size_t phasing_result_count_ = 0;
    bool active_ = false;
    bool continuous_holding_ = false;
    bool override_braking_ = false;
    bool override_paused_ = false;
    bool halt_profiled_ = false;
    bool blend_armed_ = false;
    bool superimposed_active_ = false;
    bool superimposed_halting_ = false;
    bool phasing_active_ = false;
    bool sync_profile_ready_ = false;
    bool stream_active_ = false;
    bool homing_limits_suspended_ = false;
    bool stop_release_requested_ = false;
    bool power_feedback_ = true;
    bool positive_enabled_ = false;
    bool negative_enabled_ = false;
    bool acceleration_profile_completed_ = false;
    std::array<bool, DigitalInputCount> digital_input_{};
    std::array<bool, DigitalOutputCount> digital_output_{};
    AxisInfoInputs axis_info_{};
};

} // namespace plcopen::core::axis
