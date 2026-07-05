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
#include "stream/filter.h"

namespace plcopen::core::axis
{

enum class AxisStatus
{
    disabled,
    standstill,
    discrete_motion,
    continuous_motion,
    synchronized_motion,
    stopping,
    errorstop,
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

struct AxisCommand
{
    CommandKind kind = CommandKind::move_absolute;
    double value = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    // Only used by the move_continuous_* kinds; must be positive there.
    double end_velocity = 0.0;
    // Minimum command time in cycles (profile-table segments). A position
    // command holds at its target until the duration elapses; a velocity
    // command finishes after it (0 keeps the plain unlimited hold).
    std::int64_t min_duration_cycles = 0;
    BufferMode buffer_mode = BufferMode::aborting;
    bool continuous_update = false;
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

enum class SyncKind
{
    none,
    gear,
    cam,
    combine,
};

enum class SyncPhase
{
    idle,
    queued,
    waiting_window,
    approaching,
    engaged,
};

enum class CombineMode
{
    add_axes,
    sub_axes,
};

class AxisModel;

struct GearInCommand
{
    const AxisModel *master = nullptr;
    double ratio_numerator = 1.0;
    double ratio_denominator = 1.0;
    MasterValueSource source = MasterValueSource::command;
    BufferMode buffer_mode = BufferMode::aborting;
    // position_sync selects MC_GearInPos semantics: wait for the master sync
    // window, approach the slave sync position, then follow with aligned phase.
    bool position_sync = false;
    double master_sync_position = 0.0;
    double slave_sync_position = 0.0;
    double master_start_distance = 0.0;
    // Per-cycle displacement cap for the approach segment; 0 disables the cap.
    double approach_velocity = 0.0;
};

struct CamInCommand
{
    const AxisModel *master = nullptr;
    exec::CamTableView table{};
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
    double actual_torque = 0.0;
    bool powered = false;
    bool homed = false;
    bool error = false;
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
           std::isfinite(command.jerk) && std::isfinite(command.end_velocity);
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

    explicit AxisModel(int domain_id = 0)
        : domain_id_(domain_id)
    {
    }

    int domain_id() const
    {
        return domain_id_;
    }

    AxisStatus status() const
    {
        return snapshot_.status;
    }

    const AxisSnapshot &snapshot() const
    {
        return snapshot_;
    }

    bool powered() const
    {
        return snapshot_.powered;
    }

    void *group_owner() const
    {
        return group_owner_;
    }

    rt::ErrorCode set_group_owner(void *owner)
    {
        group_owner_ = owner;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode configure_limits(MotionLimits limits)
    {
        if(snapshot_.powered || limits.max_velocity <= 0.0 || limits.max_acceleration <= 0.0 ||
           limits.max_deceleration <= 0.0 || limits.max_jerk <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        limits_ = limits;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode set_power(bool enabled)
    {
        if(snapshot_.status == AxisStatus::errorstop && enabled) {
            return rt::ErrorCode::invalid_argument;
        }
        // MC_Power is level-controlled and called every scan cycle; only a
        // real power transition may abort motion and reset the state.
        if(enabled == snapshot_.powered) {
            return rt::ErrorCode::ok;
        }

        abort_motion();
        snapshot_.powered = enabled;
        snapshot_.status = enabled ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode set_position(double position)
    {
        if(!std::isfinite(position) || is_moving()) {
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
        switch(parameter) {
        case AxisParameter::commanded_position:
            return rt::Result<double>::success(snapshot_.command_position);
        case AxisParameter::sw_limit_pos:
            return rt::Result<double>::success(limits_.max_position);
        case AxisParameter::sw_limit_neg:
            return rt::Result<double>::success(limits_.min_position);
        case AxisParameter::enable_limit_pos:
            return rt::Result<double>::success(limits_.max_position_enabled ? 1.0 : 0.0);
        case AxisParameter::enable_limit_neg:
            return rt::Result<double>::success(limits_.min_position_enabled ? 1.0 : 0.0);
        case AxisParameter::max_velocity_system:
        case AxisParameter::max_velocity_appl:
            return rt::Result<double>::success(limits_.max_velocity);
        case AxisParameter::actual_velocity:
            return rt::Result<double>::success(snapshot_.actual_velocity);
        case AxisParameter::commanded_velocity:
            return rt::Result<double>::success(snapshot_.command_velocity);
        case AxisParameter::max_acceleration_system:
        case AxisParameter::max_acceleration_appl:
            return rt::Result<double>::success(limits_.max_acceleration);
        case AxisParameter::max_deceleration_system:
        case AxisParameter::max_deceleration_appl:
            return rt::Result<double>::success(limits_.max_deceleration);
        case AxisParameter::max_jerk_system:
        case AxisParameter::max_jerk_appl:
            return rt::Result<double>::success(limits_.max_jerk);
        default:
            return rt::Result<double>::failure(rt::ErrorCode::unsupported);
        }
    }

    rt::Result<bool> read_bool_parameter(AxisParameter parameter) const
    {
        switch(parameter) {
        case AxisParameter::enable_limit_pos:
            return rt::Result<bool>::success(limits_.max_position_enabled);
        case AxisParameter::enable_limit_neg:
            return rt::Result<bool>::success(limits_.min_position_enabled);
        default:
            return rt::Result<bool>::failure(rt::ErrorCode::unsupported);
        }
    }

    rt::ErrorCode write_parameter(AxisParameter parameter, double value)
    {
        if(!std::isfinite(value)) {
            return rt::ErrorCode::invalid_argument;
        }
        switch(parameter) {
        case AxisParameter::sw_limit_pos:
            if(limits_.min_position_enabled && limits_.max_position_enabled &&
               value < limits_.min_position) {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_position = value;
            return rt::ErrorCode::ok;
        case AxisParameter::sw_limit_neg:
            if(limits_.min_position_enabled && limits_.max_position_enabled &&
               value > limits_.max_position) {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.min_position = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_velocity_system:
        case AxisParameter::max_velocity_appl:
            if(value <= 0.0) {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_velocity = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_acceleration_system:
        case AxisParameter::max_acceleration_appl:
            if(value <= 0.0) {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_acceleration = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_deceleration_system:
        case AxisParameter::max_deceleration_appl:
            if(value <= 0.0) {
                return rt::ErrorCode::invalid_argument;
            }
            limits_.max_deceleration = value;
            return rt::ErrorCode::ok;
        case AxisParameter::max_jerk_system:
        case AxisParameter::max_jerk_appl:
            if(value <= 0.0) {
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
        switch(parameter) {
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

    // MC_SetOverride carries the KB-003 contract: newly planned commands scale
    // by the override, velocity commands respond per cycle through
    // signed_velocity(), and an active profile-driven command re-plans from
    // its current state under the re-scaled velocity limit (an override drop
    // below the current velocity plans a deceleration entry). A failed replan
    // keeps the previous override and the running profile untouched.
    rt::ErrorCode set_override(double percent)
    {
        if(!std::isfinite(percent) || percent <= 0.0 || percent > 100.0) {
            return rt::ErrorCode::invalid_argument;
        }
        const double previous = override_;
        override_ = percent;
        if(previous == percent) {
            return rt::ErrorCode::ok;
        }

        const bool profile_driven =
            active_ && active_command_.kind != CommandKind::move_velocity &&
            active_command_.kind != CommandKind::halt && active_command_.kind != CommandKind::stop;
        if(!profile_driven) {
            return rt::ErrorCode::ok;
        }

        const double direction =
            active_target_ >= snapshot_.command_position ? 1.0 : -1.0;
        const double end_velocity =
            is_continuous_kind(active_command_.kind)
                ? direction * active_command_.end_velocity * (override_ / 100.0)
                : 0.0;
        if(continuous_holding_) {
            continuous_hold_velocity_ = end_velocity;
            return rt::ErrorCode::ok;
        }

        otg::Limits1D limits{active_command_.velocity * (override_ / 100.0),
                             active_command_.acceleration,
                             active_command_.deceleration,
                             active_command_.jerk};
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            {snapshot_.command_position, snapshot_.command_velocity,
             snapshot_.command_acceleration},
            {active_target_, end_velocity, 0.0},
            limits);
        if(!profile) {
            override_ = previous;
            return profile.error();
        }
        active_profile_ = profile.value();
        active_tick_ = 0;
        active_last_sample_ = snapshot_.command_position;
        continuous_hold_velocity_ = end_velocity;
        snapshot_.active_command_reached_target = false;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode trigger_error()
    {
        abort_motion();
        snapshot_.error = true;
        snapshot_.status = AxisStatus::errorstop;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode reset_error()
    {
        if(snapshot_.status != AxisStatus::errorstop) {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.error = false;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit(AxisCommand command)
    {
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
           !is_finite_command(command) || command.velocity <= 0.0 || command.acceleration <= 0.0 ||
           command.deceleration <= 0.0 || command.jerk <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        // Queueing a motion command behind an engaged synchronization or a
        // stream session has no defined completion point; only aborting
        // commands may take over.
        if((sync_kind_ != SyncKind::none || stream_active_) &&
           command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(is_continuous_kind(command.kind) && command.end_velocity <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }

        if(command.kind == CommandKind::torque) {
            snapshot_.actual_torque = command.value;
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        // The additive target resolves against the endpoint that was committed
        // before an aborting takeover discards it.
        const double takeover_endpoint = queued_endpoint();
        // Aborting takeovers keep kinematic continuity: abort_motion() zeroes
        // the command velocity/acceleration, but the new command must plan
        // from the state the axis was actually in (KB-026).
        const double takeover_velocity = snapshot_.command_velocity;
        const double takeover_acceleration = snapshot_.command_acceleration;
        if(command.buffer_mode == BufferMode::aborting) {
            abort_motion();
            snapshot_.command_velocity = takeover_velocity;
            snapshot_.command_acceleration = takeover_acceleration;
            snapshot_.actual_velocity = takeover_velocity;
            snapshot_.actual_acceleration = takeover_acceleration;
        }
        command = normalize(command, takeover_endpoint);
        if((command.kind == CommandKind::move_absolute ||
            command.kind == CommandKind::move_continuous_absolute ||
            command.kind == CommandKind::home) &&
           !target_inside_limits(command.value)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::out_of_range);
        }

        if(command.buffer_mode == BufferMode::aborting || !active_) {
            const rt::ErrorCode started = start(command);
            if(started != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(started);
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    rt::Result<std::uint32_t> gear_in(const GearInCommand &command)
    {
        if(command.master == nullptr || command.master == this ||
           !std::isfinite(command.ratio_numerator) || !std::isfinite(command.ratio_denominator) ||
           command.ratio_denominator == 0.0 || !std::isfinite(command.master_sync_position) ||
           !std::isfinite(command.slave_sync_position) ||
           !std::isfinite(command.master_start_distance) || command.master_start_distance < 0.0 ||
           !std::isfinite(command.approach_velocity) || command.approach_velocity < 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::gear, command.buffer_mode);
        if(!begun) {
            return begun;
        }
        sync_gear_ = command;
        phase_offset_ = 0.0;
        phasing_active_ = false;
        sync_entry_phase_ = command.position_sync ? SyncPhase::waiting_window : SyncPhase::engaged;
        if(sync_phase_ != SyncPhase::queued) {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    rt::Result<std::uint32_t> cam_in(const CamInCommand &command)
    {
        if(command.master == nullptr || command.master == this || !command.table.valid() ||
           !std::isfinite(command.master_offset) || !std::isfinite(command.master_scaling) ||
           command.master_scaling == 0.0 || !std::isfinite(command.slave_offset) ||
           !std::isfinite(command.slave_scaling) || !std::isfinite(command.master_sync_position) ||
           !std::isfinite(command.master_start_distance) || command.master_start_distance < 0.0 ||
           !std::isfinite(command.approach_velocity) || command.approach_velocity < 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::cam, command.buffer_mode);
        if(!begun) {
            return begun;
        }
        sync_cam_ = command;
        sync_entry_phase_ = command.master_start_distance > 0.0 ? SyncPhase::waiting_window
                                                                : SyncPhase::engaged;
        if(sync_phase_ != SyncPhase::queued) {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    rt::Result<std::uint32_t> combine_in(const CombineAxesCommand &command)
    {
        if(command.master1 == nullptr || command.master2 == nullptr || command.master1 == this ||
           command.master2 == this || !std::isfinite(command.ratio_numerator_m1) ||
           !std::isfinite(command.ratio_denominator_m1) || command.ratio_denominator_m1 == 0.0 ||
           !std::isfinite(command.ratio_numerator_m2) ||
           !std::isfinite(command.ratio_denominator_m2) || command.ratio_denominator_m2 == 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        const rt::Result<std::uint32_t> begun = begin_sync(SyncKind::combine, command.buffer_mode);
        if(!begun) {
            return begun;
        }
        sync_combine_ = command;
        sync_entry_phase_ = SyncPhase::engaged;
        if(sync_phase_ != SyncPhase::queued) {
            sync_phase_ = sync_entry_phase_;
        }
        return begun;
    }

    rt::ErrorCode gear_update(double ratio_numerator, double ratio_denominator)
    {
        if(sync_kind_ != SyncKind::gear || !std::isfinite(ratio_numerator) ||
           !std::isfinite(ratio_denominator) || ratio_denominator == 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        sync_gear_.ratio_numerator = ratio_numerator;
        sync_gear_.ratio_denominator = ratio_denominator;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode cam_update(double master_offset,
                             double master_scaling,
                             double slave_offset,
                             double slave_scaling)
    {
        if(sync_kind_ != SyncKind::cam || !std::isfinite(master_offset) ||
           !std::isfinite(master_scaling) || master_scaling == 0.0 ||
           !std::isfinite(slave_offset) || !std::isfinite(slave_scaling)) {
            return rt::ErrorCode::invalid_argument;
        }
        sync_cam_.master_offset = master_offset;
        sync_cam_.master_scaling = master_scaling;
        sync_cam_.slave_offset = slave_offset;
        sync_cam_.slave_scaling = slave_scaling;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode combine_update(CombineMode mode,
                                 double ratio_numerator_m1,
                                 double ratio_denominator_m1,
                                 double ratio_numerator_m2,
                                 double ratio_denominator_m2)
    {
        if(sync_kind_ != SyncKind::combine || !std::isfinite(ratio_numerator_m1) ||
           !std::isfinite(ratio_denominator_m1) || ratio_denominator_m1 == 0.0 ||
           !std::isfinite(ratio_numerator_m2) || !std::isfinite(ratio_denominator_m2) ||
           ratio_denominator_m2 == 0.0) {
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
        if(sync_kind_ == SyncKind::none) {
            return rt::ErrorCode::invalid_argument;
        }
        reset_sync();
        snapshot_.active_command_id = 0;
        if(snapshot_.status == AxisStatus::synchronized_motion) {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
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
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
           sync_kind_ != SyncKind::none || stream_active_ || group_owner_ != nullptr) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::ErrorCode configured = stream_filter_.configure(config);
        if(configured != rt::ErrorCode::ok) {
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
        if(reset != rt::ErrorCode::ok) {
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
        if(!stream_active_) {
            return rt::ErrorCode::invalid_argument;
        }
        return stream_filter_.push_target(target);
    }

    // Graceful exit is only defined at rest; a moving session exits through
    // a standard aborting command (MC_Halt/MC_Stop/motion takeover).
    rt::ErrorCode stream_disengage()
    {
        if(!stream_active_) {
            return rt::ErrorCode::invalid_argument;
        }
        if(snapshot_.command_velocity != 0.0 || snapshot_.command_acceleration != 0.0) {
            return rt::ErrorCode::precondition_failed;
        }
        stream_active_ = false;
        stream_id_ = 0;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        return rt::ErrorCode::ok;
    }

    std::uint32_t stream_session_id() const
    {
        return stream_id_;
    }

    // Read-only session introspection (mode, counters, clamped flag); only
    // meaningful while the session is engaged.
    const stream::StreamFilter1D &stream_filter() const
    {
        return stream_filter_;
    }

    SyncPhase sync_phase() const
    {
        return sync_phase_;
    }

    std::uint32_t sync_command_id() const
    {
        return sync_id_;
    }

    double gear_phase_offset() const
    {
        return phase_offset_;
    }

    bool phasing_active() const
    {
        return phasing_active_;
    }

    rt::ErrorCode phasing_absolute(double phase, double velocity)
    {
        if(sync_kind_ != SyncKind::gear || sync_phase_ != SyncPhase::engaged ||
           !std::isfinite(phase) || !std::isfinite(velocity) || velocity < 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(velocity == 0.0) {
            phase_offset_ = phase;
            phasing_active_ = false;
            return rt::ErrorCode::ok;
        }
        phase_target_ = phase;
        phase_rate_ = velocity;
        phasing_active_ = true;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode phasing_relative(double shift, double velocity)
    {
        if(!std::isfinite(shift)) {
            return rt::ErrorCode::invalid_argument;
        }
        return phasing_absolute(phase_offset_ + shift, velocity);
    }

    // Adapter hook: inject measured feedback without touching command state.
    rt::ErrorCode set_actual_feedback(double position, double velocity, double acceleration = 0.0)
    {
        if(!std::isfinite(position) || !std::isfinite(velocity) || !std::isfinite(acceleration)) {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.actual_position = position;
        snapshot_.actual_velocity = velocity;
        snapshot_.actual_acceleration = acceleration;
        return rt::ErrorCode::ok;
    }

    // MC_Home direct mode: remap the coordinate and mark the axis homed.
    rt::ErrorCode home_direct(double position)
    {
        const rt::ErrorCode set = set_position(position);
        if(set != rt::ErrorCode::ok) {
            return set;
        }
        snapshot_.homed = true;
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
        if(input >= DigitalInputCount) {
            return rt::ErrorCode::unsupported;
        }
        digital_input_[input] = level;
        return rt::ErrorCode::ok;
    }

    rt::Result<bool> digital_input(std::size_t input) const
    {
        if(input >= DigitalInputCount) {
            return rt::Result<bool>::failure(rt::ErrorCode::unsupported);
        }
        return rt::Result<bool>::success(digital_input_[input]);
    }

    rt::ErrorCode set_digital_output(std::size_t output, bool level)
    {
        if(output >= DigitalOutputCount) {
            return rt::ErrorCode::unsupported;
        }
        digital_output_[output] = level;
        return rt::ErrorCode::ok;
    }

    rt::Result<bool> digital_output(std::size_t output) const
    {
        if(output >= DigitalOutputCount) {
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

    void set_axis_info_inputs(const AxisInfoInputs &inputs)
    {
        axis_info_ = inputs;
    }

    const AxisInfoInputs &axis_info_inputs() const
    {
        return axis_info_;
    }

    rt::Result<std::uint32_t> arm_touch_probe(std::size_t input,
                                              bool window_only,
                                              double first_position,
                                              double last_position)
    {
        if(input >= DigitalInputCount) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(window_only && (!std::isfinite(first_position) || !std::isfinite(last_position) ||
                           first_position > last_position)) {
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
        if(input >= DigitalInputCount) {
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
        if(snapshot_.status == AxisStatus::errorstop) {
            return;
        }
        if(stream_cycle()) {
            cycle_probes();
            return;
        }
        if(sync_cycle()) {
            cycle_probes();
            return;
        }
        cycle_base_motion();
        cycle_superimposed();
        cycle_probes();
    }

    // Independent offset profile on top of the base motion (MC_MoveSuperimposed).
    rt::Result<std::uint32_t> submit_superimposed(double distance,
                                                  double velocity,
                                                  double acceleration,
                                                  double deceleration,
                                                  double jerk)
    {
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop ||
           sync_kind_ != SyncKind::none || stream_active_ || !std::isfinite(distance) ||
           !std::isfinite(velocity) || velocity <= 0.0 || !std::isfinite(acceleration) ||
           acceleration <= 0.0 || !std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        otg::Limits1D limits{velocity * (override_ / 100.0), acceleration, deceleration, jerk};
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({0.0, 0.0, 0.0}, {distance, 0.0, 0.0}, limits);
        if(!profile) {
            return rt::Result<std::uint32_t>::failure(profile.error());
        }

        superimposed_profile_ = profile.value();
        superimposed_tick_ = 0;
        superimposed_last_ = 0.0;
        superimposed_active_ = true;
        superimposed_id_ = next_command_id_++;
        superimposed_completed_id_ = 0;
        return rt::Result<std::uint32_t>::success(superimposed_id_);
    }

    // Stops only the superimposed offset; the base command keeps running. The
    // contribution accumulated so far persists in the command position.
    rt::ErrorCode halt_superimposed()
    {
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop) {
            return rt::ErrorCode::invalid_argument;
        }
        superimposed_active_ = false;
        superimposed_id_ = 0;
        superimposed_completed_id_ = 0;
        return rt::ErrorCode::ok;
    }

    bool superimposed_active() const
    {
        return superimposed_active_;
    }

    std::uint32_t superimposed_command_id() const
    {
        return superimposed_id_;
    }

    std::uint32_t superimposed_completed_id() const
    {
        return superimposed_completed_id_;
    }

    // True while the command sits in the buffered queue (facades report Busy
    // for queued successors instead of misreading them as aborted).
    bool command_pending(std::uint32_t command_id) const
    {
        if(command_id == 0) {
            return false;
        }
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].command_id == command_id) {
                return true;
            }
        }
        return false;
    }

    // ContinuousUpdate for the active velocity command (MC_MoveVelocity,
    // KB-009): the new signed direction and magnitude apply from the next
    // cycle; the override keeps scaling per cycle.
    rt::ErrorCode update_active_velocity(std::uint32_t command_id,
                                         double direction_value,
                                         double velocity)
    {
        if(!active_ || snapshot_.active_command_id != command_id || command_id == 0 ||
           active_command_.kind != CommandKind::move_velocity ||
           !std::isfinite(direction_value) || !std::isfinite(velocity) || velocity <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        active_command_.value = direction_value;
        active_command_.velocity = velocity;
        return rt::ErrorCode::ok;
    }

    // Retargets the active move_continuous_* or move_absolute command
    // (ContinuousUpdate).
    rt::ErrorCode update_active_target(std::uint32_t command_id, double target)
    {
        if(!active_ || snapshot_.active_command_id != command_id || command_id == 0 ||
           !std::isfinite(target) ||
           (!is_continuous_kind(active_command_.kind) &&
            active_command_.kind != CommandKind::move_absolute)) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!target_inside_limits(target)) {
            return rt::ErrorCode::out_of_range;
        }

        double end_velocity = 0.0;
        if(is_continuous_kind(active_command_.kind)) {
            const double direction = target >= snapshot_.command_position ? 1.0 : -1.0;
            end_velocity = direction * active_command_.end_velocity * (override_ / 100.0);
        }
        otg::Limits1D limits{active_command_.velocity * (override_ / 100.0),
                             active_command_.acceleration,
                             active_command_.deceleration,
                             active_command_.jerk};
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity, snapshot_.command_acceleration},
                      {target, end_velocity, 0.0},
                      limits);
        if(!profile) {
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

    void set_synchronized_position(double position)
    {
        snapshot_.status = AxisStatus::synchronized_motion;
        snapshot_.command_position = position;
        snapshot_.actual_position = position;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
    }

    void clear_synchronized()
    {
        reset_sync();
        if(snapshot_.status == AxisStatus::synchronized_motion) {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
    }

private:
    rt::Result<std::uint32_t> begin_sync(SyncKind kind, BufferMode buffer_mode)
    {
        // Synchronizing a streaming axis is undefined (approved stream
        // matrix, decision #10): explicit error, not a takeover.
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop || stream_active_) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(buffer_mode == BufferMode::aborting) {
            abort_motion();
        } else if(sync_kind_ != SyncKind::none) {
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
        sync_kind_ = SyncKind::none;
        sync_phase_ = SyncPhase::idle;
        sync_entry_phase_ = SyncPhase::idle;
        sync_id_ = 0;
        phase_offset_ = 0.0;
        phasing_active_ = false;
    }

    double master_value(const AxisModel &master, MasterValueSource source) const
    {
        const AxisSnapshot &snapshot = master.snapshot();
        return source == MasterValueSource::actual ? snapshot.actual_position
                                                   : snapshot.command_position;
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
        if(sync_kind_ == SyncKind::gear) {
            return rt::Result<double>::success(sync_gear_.slave_sync_position);
        }
        return cam_slave_value(sync_master_sync_position());
    }

    rt::Result<double> cam_slave_value(double master_position) const
    {
        const double table_input =
            (master_position - sync_cam_.master_offset) / sync_cam_.master_scaling;
        const rt::Result<double> sampled = sync_cam_.table.sample(table_input);
        if(!sampled) {
            return sampled;
        }
        return rt::Result<double>::success(sync_cam_.slave_offset +
                                           sync_cam_.slave_scaling * sampled.value());
    }

    void advance_phasing()
    {
        if(!phasing_active_) {
            return;
        }
        const double remaining = phase_target_ - phase_offset_;
        if(std::fabs(remaining) <= phase_rate_) {
            phase_offset_ = phase_target_;
            phasing_active_ = false;
            return;
        }
        phase_offset_ += remaining > 0.0 ? phase_rate_ : -phase_rate_;
    }

    rt::Result<double> sync_engaged_position()
    {
        if(sync_kind_ == SyncKind::gear) {
            advance_phasing();
            const double master = master_value(*sync_gear_.master, sync_gear_.source);
            const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
            return rt::Result<double>::success(master * ratio + phase_offset_);
        }
        if(sync_kind_ == SyncKind::cam) {
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

    void enter_engaged()
    {
        if(sync_kind_ == SyncKind::gear && sync_gear_.position_sync) {
            const double ratio = sync_gear_.ratio_numerator / sync_gear_.ratio_denominator;
            phase_offset_ =
                sync_gear_.slave_sync_position - sync_gear_.master_sync_position * ratio;
        }
        sync_phase_ = SyncPhase::engaged;
    }

    // Returns true when a stream session owns this cycle: the filter output
    // is the axis setpoint (the filter starts from the takeover state, so
    // the position domain is continuous with the pre-engage motion).
    bool stream_cycle()
    {
        if(!stream_active_) {
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
        if(sync_kind_ == SyncKind::none) {
            return false;
        }
        if(sync_phase_ == SyncPhase::queued) {
            if(active_) {
                return false;
            }
            sync_phase_ = sync_entry_phase_;
        }

        if(sync_phase_ == SyncPhase::waiting_window) {
            const double master = sync_window_master_value();
            const double window_begin = sync_master_sync_position() - sync_master_start_distance();
            if(master < window_begin) {
                return true;
            }
            if(sync_master_start_distance() > 0.0 && master < sync_master_sync_position()) {
                sync_phase_ = SyncPhase::approaching;
                approach_window_begin_ = window_begin;
                approach_start_slave_ = snapshot_.command_position;
            } else if(master >= sync_master_sync_position()) {
                enter_engaged();
            } else {
                return true;
            }
        }

        if(sync_phase_ == SyncPhase::approaching) {
            const double master = sync_window_master_value();
            if(master >= sync_master_sync_position()) {
                enter_engaged();
            } else {
                const rt::Result<double> target_at_sync = sync_target_at_sync();
                if(!target_at_sync) {
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

        if(sync_phase_ == SyncPhase::engaged) {
            const rt::Result<double> position = sync_engaged_position();
            if(!position) {
                trigger_error();
                return true;
            }
            snapshot_.active_command_id = sync_id_;
            set_synchronized_position(position.value());
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
        if(cap <= 0.0) {
            return target;
        }
        const double step = target - snapshot_.command_position;
        if(step > cap) {
            return snapshot_.command_position + cap;
        }
        if(step < -cap) {
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
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            const AxisCommand &queued = queue_[i];
            if(queued.kind == CommandKind::move_absolute ||
               queued.kind == CommandKind::move_continuous_absolute ||
               queued.kind == CommandKind::home) {
                endpoint = queued.value;
            }
        }
        return endpoint;
    }

    AxisCommand normalize(AxisCommand command, double takeover_endpoint) const
    {
        if(command.kind == CommandKind::move_relative) {
            command.value = queued_endpoint() + command.value;
            command.kind = CommandKind::move_absolute;
        } else if(command.kind == CommandKind::move_additive) {
            command.value = takeover_endpoint + command.value;
            command.kind = CommandKind::move_absolute;
        } else if(command.kind == CommandKind::move_continuous_relative) {
            command.value = queued_endpoint() + command.value;
            command.kind = CommandKind::move_continuous_absolute;
        }
        return command;
    }

    double signed_velocity(AxisCommand command) const
    {
        const double scaled = command.velocity * (override_ / 100.0);
        return command.value < 0.0 ? -scaled : scaled;
    }

    bool target_inside_limits(double target) const
    {
        if(limits_.min_position_enabled && target < limits_.min_position) {
            return false;
        }
        if(limits_.max_position_enabled && target > limits_.max_position) {
            return false;
        }
        return true;
    }

    rt::ErrorCode start(AxisCommand command)
    {
        active_command_ = command;
        active_tick_ = 0;
        continuous_holding_ = false;
        blend_armed_ = false;
        snapshot_.active_command_reached_target = false;
        snapshot_.active_command_id = command.command_id;

        if(command.kind == CommandKind::move_velocity) {
            active_ = true;
            snapshot_.status = AxisStatus::continuous_motion;
            return rt::ErrorCode::ok;
        }

        if(command.kind == CommandKind::halt || command.kind == CommandKind::stop) {
            // Controlled stop (MC_Halt/MC_Stop carry over the v0.x contract):
            // decelerate from the current state with the commanded
            // deceleration/jerk. The braking target is exempt from the
            // software position limits — the axis must be allowed to come to
            // rest. A resting axis still finishes within one cycle.
            halt_profiled_ = false;
            const double v0 = snapshot_.command_velocity;
            const double a0 = snapshot_.command_acceleration;
            if(v0 != 0.0 || a0 != 0.0) {
                double brake_velocity = v0;
                double brake_shift = 0.0;
                if(a0 != 0.0) {
                    const double zero_cycles = std::ceil(std::fabs(a0) / command.jerk);
                    brake_velocity += 0.5 * a0 * zero_cycles;
                    brake_shift += v0 * zero_cycles + a0 * zero_cycles * zero_cycles / 3.0;
                }
                const otg::Limits1D halt_limits{
                    std::fabs(v0) + std::fabs(brake_velocity) + 1e-9,
                    std::fabs(a0) > command.acceleration ? std::fabs(a0) : command.acceleration,
                    command.deceleration,
                    command.jerk};
                const double stop_position =
                    snapshot_.command_position + brake_shift +
                    otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
                const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
                    {snapshot_.command_position, v0, a0}, {stop_position, 0.0, 0.0},
                    halt_limits);
                if(halt) {
                    active_profile_ = halt.value();
                    active_last_sample_ = snapshot_.command_position;
                    active_target_ = stop_position;
                    halt_profiled_ = true;
                }
            }
            active_ = true;
            snapshot_.status = AxisStatus::stopping;
            return rt::ErrorCode::ok;
        }

        const double target = command.value;
        if(!target_inside_limits(target)) {
            return rt::ErrorCode::out_of_range;
        }

        double target_velocity = 0.0;
        if(is_continuous_kind(command.kind)) {
            const double direction = target >= snapshot_.command_position ? 1.0 : -1.0;
            target_velocity = direction * command.end_velocity * (override_ / 100.0);
        }

        active_target_ = target;
        otg::Limits1D limits{command.velocity * (override_ / 100.0),
                             command.acceleration,
                             command.deceleration,
                             command.jerk};
        const rt::Result<otg::Profile1D> profile =
            otg::plan_time_optimal({snapshot_.command_position, snapshot_.command_velocity, snapshot_.command_acceleration},
                      {target, target_velocity, 0.0},
                      limits);
        if(!profile) {
            return profile.error();
        }

        active_profile_ = profile.value();
        active_last_sample_ = snapshot_.command_position;
        continuous_hold_velocity_ = target_velocity;
        active_ = true;
        snapshot_.status = is_continuous_kind(command.kind) ? AxisStatus::continuous_motion
                                                            : AxisStatus::discrete_motion;
        return rt::ErrorCode::ok;
    }

    void cycle_base_motion()
    {
        if(!active_) {
            return;
        }

        if(active_command_.kind == CommandKind::move_velocity || continuous_holding_) {
            const double velocity = continuous_holding_ ? continuous_hold_velocity_
                                                        : signed_velocity(active_command_);
            base_velocity_ = velocity;
            snapshot_.command_velocity = velocity;
            snapshot_.actual_velocity = velocity;
            snapshot_.command_acceleration = 0.0;
            snapshot_.actual_acceleration = 0.0;
            snapshot_.command_position += velocity;
            snapshot_.actual_position = snapshot_.command_position;
            if(!continuous_holding_ && active_command_.min_duration_cycles > 0) {
                ++active_tick_;
                if(active_tick_ >= active_command_.min_duration_cycles) {
                    finish_active();
                }
            }
            return;
        }

        if((active_command_.kind == CommandKind::halt ||
            active_command_.kind == CommandKind::stop) &&
           !halt_profiled_) {
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
        const bool blend_eligible =
            active_command_.kind == CommandKind::move_absolute && !queue_.empty() &&
            (queue_[0].buffer_mode == BufferMode::blending_low ||
             queue_[0].buffer_mode == BufferMode::blending_high);
        if(blend_eligible) {
            const double nominal = active_command_.velocity * (override_ / 100.0);
            const double threshold =
                (queue_[0].buffer_mode == BufferMode::blending_low ? 0.3 : 0.7) * nominal;
            const double speed = std::fabs(state.velocity);
            if(speed > threshold) {
                blend_armed_ = true;
            } else if(blend_armed_) {
                blend_into_next();
                return;
            }
        } else {
            blend_armed_ = false;
        }

        if(active_tick_ >= active_profile_.duration_cycles()) {
            // Timed profile segments hold at the target until their minimum
            // duration elapses (otg::sample keeps returning the finish state).
            if(active_tick_ < active_command_.min_duration_cycles) {
                return;
            }
            if(active_command_.kind == CommandKind::home) {
                snapshot_.homed = true;
            }
            if(is_continuous_kind(active_command_.kind)) {
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
        if(!superimposed_active_) {
            return;
        }
        if(!active_ && snapshot_.status == AxisStatus::standstill) {
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

        if(superimposed_tick_ >= superimposed_profile_.duration_cycles()) {
            superimposed_active_ = false;
            superimposed_completed_id_ = superimposed_id_;
            superimposed_id_ = 0;
            snapshot_.command_velocity = base_velocity_;
            snapshot_.actual_velocity = base_velocity_;
            if(!active_ && snapshot_.status == AxisStatus::discrete_motion) {
                snapshot_.status = AxisStatus::standstill;
            }
        }
    }

    void finish_active()
    {
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
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
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
        if(queue_.empty()) {
            return;
        }

        const AxisCommand next = queue_[0];
        for(std::size_t i = 1; i < queue_.size(); ++i) {
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
        base_velocity_ = 0.0;
        queue_.clear();
        reset_sync();
        reset_superimposed();
        stream_active_ = false;
        stream_id_ = 0;
        if(snapshot_.status == AxisStatus::synchronized_motion) {
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
    }

    void cycle_probes()
    {
        for(std::size_t input = 0; input < DigitalInputCount; ++input) {
            ProbeSlot &slot = probes_[input];
            const bool level = digital_input_[input];
            const bool rising = level && !slot.last_level;
            slot.last_level = level;
            if(!slot.armed || !rising) {
                continue;
            }
            const double position = snapshot_.actual_position;
            if(slot.window_only &&
               (position < slot.first_position || position > slot.last_position)) {
                continue;
            }
            slot.armed = false;
            slot.captured = true;
            slot.recorded_position = position;
        }
    }

    int domain_id_ = 0;
    void *group_owner_ = nullptr;
    AxisSnapshot snapshot_{};
    MotionLimits limits_{};
    rt::StaticVector<AxisCommand, QueueCapacity> queue_{};
    AxisCommand active_command_{};
    otg::Profile1D active_profile_{};
    double active_target_ = 0.0;
    double active_last_sample_ = 0.0;
    double base_velocity_ = 0.0;
    double continuous_hold_velocity_ = 0.0;
    double override_ = 100.0;
    std::int64_t active_tick_ = 0;
    std::uint32_t next_command_id_ = 1;
    bool active_ = false;
    bool continuous_holding_ = false;
    bool halt_profiled_ = false;
    bool blend_armed_ = false;

    otg::Profile1D superimposed_profile_{};
    std::int64_t superimposed_tick_ = 0;
    double superimposed_last_ = 0.0;
    std::uint32_t superimposed_id_ = 0;
    std::uint32_t superimposed_completed_id_ = 0;
    bool superimposed_active_ = false;

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
    std::array<bool, DigitalInputCount> digital_input_{};
    std::array<bool, DigitalOutputCount> digital_output_{};
    std::array<ProbeSlot, DigitalInputCount> probes_{};
    AxisInfoInputs axis_info_{};

    SyncKind sync_kind_ = SyncKind::none;
    SyncPhase sync_phase_ = SyncPhase::idle;
    SyncPhase sync_entry_phase_ = SyncPhase::idle;
    std::uint32_t sync_id_ = 0;
    GearInCommand sync_gear_{};
    CamInCommand sync_cam_{};
    CombineAxesCommand sync_combine_{};
    double phase_offset_ = 0.0;
    double phase_target_ = 0.0;
    double phase_rate_ = 0.0;
    bool phasing_active_ = false;
    double approach_window_begin_ = 0.0;
    double approach_start_slave_ = 0.0;

    stream::StreamFilter1D stream_filter_{};
    bool stream_active_ = false;
    std::uint32_t stream_id_ = 0;
};

} // namespace plcopen::core::axis
