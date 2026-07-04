#pragma once

#include <cmath>
#include <cstdint>

#include "exec/sync.h"
#include "otg/profile1d.h"
#include "rt/error.h"
#include "rt/static_vector.h"

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
    move_velocity,
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

struct AxisCommand
{
    CommandKind kind = CommandKind::move_absolute;
    double value = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    BufferMode buffer_mode = BufferMode::aborting;
    bool continuous_update = false;
    std::uint32_t command_id = 0;
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
    double actual_torque = 0.0;
    bool powered = false;
    bool homed = false;
    bool error = false;
    std::uint32_t active_command_id = 0;
};

inline bool is_finite_command(const AxisCommand &command)
{
    return std::isfinite(command.value) && std::isfinite(command.velocity) &&
           std::isfinite(command.acceleration) && std::isfinite(command.deceleration) &&
           std::isfinite(command.jerk);
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

    rt::ErrorCode set_override(double percent)
    {
        if(!std::isfinite(percent) || percent <= 0.0 || percent > 100.0) {
            return rt::ErrorCode::invalid_argument;
        }
        override_ = percent;
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

        // Queueing a motion command behind an engaged synchronization has no
        // defined completion point; only aborting commands may take over.
        if(sync_kind_ != SyncKind::none && command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }

        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }

        if(command.kind == CommandKind::torque) {
            snapshot_.actual_torque = command.value;
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        if(command.buffer_mode == BufferMode::aborting) {
            abort_motion();
        }
        command = normalize(command);
        if((command.kind == CommandKind::move_absolute || command.kind == CommandKind::home) &&
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
    rt::ErrorCode set_actual_feedback(double position, double velocity)
    {
        if(!std::isfinite(position) || !std::isfinite(velocity)) {
            return rt::ErrorCode::invalid_argument;
        }
        snapshot_.actual_position = position;
        snapshot_.actual_velocity = velocity;
        return rt::ErrorCode::ok;
    }

    void cycle()
    {
        if(snapshot_.status == AxisStatus::errorstop) {
            return;
        }
        if(sync_cycle()) {
            return;
        }
        if(!active_) {
            return;
        }

        if(active_command_.kind == CommandKind::move_velocity) {
            const double velocity = signed_velocity(active_command_);
            snapshot_.command_velocity = velocity;
            snapshot_.actual_velocity = velocity;
            snapshot_.command_position += velocity;
            snapshot_.actual_position = snapshot_.command_position;
            return;
        }

        if(active_command_.kind == CommandKind::halt || active_command_.kind == CommandKind::stop) {
            finish_active();
            return;
        }

        ++active_tick_;
        const otg::State1D state =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        snapshot_.command_position = state.position;
        snapshot_.command_velocity = state.velocity;
        snapshot_.command_acceleration = state.acceleration;
        snapshot_.actual_position = state.position;
        snapshot_.actual_velocity = state.velocity;

        if(active_tick_ >= active_profile_.duration_cycles()) {
            if(active_command_.kind == CommandKind::home) {
                snapshot_.homed = true;
            }
            finish_active();
        }
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
        if(!snapshot_.powered || snapshot_.status == AxisStatus::errorstop) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(buffer_mode == BufferMode::aborting) {
            abort_motion();
        } else if(sync_kind_ != SyncKind::none) {
            // Re-synchronizing a synchronized axis is only defined as a takeover.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
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
            if(queued.kind == CommandKind::move_absolute || queued.kind == CommandKind::home) {
                endpoint = queued.value;
            }
        }
        return endpoint;
    }

    AxisCommand normalize(AxisCommand command) const
    {
        if(command.kind == CommandKind::move_relative) {
            command.value = queued_endpoint() + command.value;
            command.kind = CommandKind::move_absolute;
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
        snapshot_.active_command_id = command.command_id;

        if(command.kind == CommandKind::move_velocity) {
            active_ = true;
            snapshot_.status = AxisStatus::continuous_motion;
            return rt::ErrorCode::ok;
        }

        if(command.kind == CommandKind::halt || command.kind == CommandKind::stop) {
            active_ = true;
            snapshot_.status = AxisStatus::stopping;
            return rt::ErrorCode::ok;
        }

        const double target = command.value;
        if(!target_inside_limits(target)) {
            return rt::ErrorCode::out_of_range;
        }

        active_target_ = target;
        otg::Limits1D limits{command.velocity * (override_ / 100.0),
                             command.acceleration,
                             command.deceleration,
                             command.jerk};
        const rt::Result<otg::Profile1D> profile =
            otg::plan({snapshot_.command_position, snapshot_.command_velocity, 0.0},
                      {target, 0.0, 0.0},
                      limits);
        if(!profile) {
            return profile.error();
        }

        active_profile_ = profile.value();
        active_ = true;
        snapshot_.status = AxisStatus::discrete_motion;
        return rt::ErrorCode::ok;
    }

    void finish_active()
    {
        active_ = false;
        snapshot_.active_command_id = 0;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        snapshot_.command_acceleration = 0.0;
        snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
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
        queue_.clear();
        reset_sync();
        if(snapshot_.status == AxisStatus::synchronized_motion) {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
        snapshot_.active_command_id = 0;
        snapshot_.command_velocity = 0.0;
        snapshot_.actual_velocity = 0.0;
        snapshot_.command_acceleration = 0.0;
    }

    int domain_id_ = 0;
    void *group_owner_ = nullptr;
    AxisSnapshot snapshot_{};
    MotionLimits limits_{};
    rt::StaticVector<AxisCommand, QueueCapacity> queue_{};
    AxisCommand active_command_{};
    otg::Profile1D active_profile_{};
    double active_target_ = 0.0;
    double override_ = 100.0;
    std::int64_t active_tick_ = 0;
    std::uint32_t next_command_id_ = 1;
    bool active_ = false;

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
};

} // namespace plcopen::core::axis
