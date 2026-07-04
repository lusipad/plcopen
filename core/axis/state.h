#pragma once

#include <cmath>
#include <cstdint>

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

    void cycle()
    {
        if(snapshot_.status == AxisStatus::errorstop || !active_) {
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
        if(snapshot_.status == AxisStatus::synchronized_motion) {
            snapshot_.status = snapshot_.powered ? AxisStatus::standstill : AxisStatus::disabled;
        }
    }

private:
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
};

} // namespace plcopen::core::axis
