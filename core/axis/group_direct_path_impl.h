#pragma once

// AxisGroup batch 5 MoveDirect lifecycle/path implementation.
// Included by group.h after AxisGroup is complete.

namespace plcopen::core::axis
{

inline rt::Result<std::uint32_t> AxisGroup::submit_direct(GroupCommand command)
{
    const bool blending = command.buffer_mode == BufferMode::blending_low ||
                          command.buffer_mode == BufferMode::blending_high;
    if(status_ != GroupStatus::standby && status_ != GroupStatus::moving) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
    }
    if(command.target.size != axes_.size() || !finite(command.target) ||
       !std::isfinite(command.velocity) || command.velocity <= 0.0 ||
       !std::isfinite(command.acceleration) || command.acceleration <= 0.0 ||
       !std::isfinite(command.deceleration) || command.deceleration <= 0.0 ||
       !std::isfinite(command.jerk) || command.jerk <= 0.0 ||
       !std::isfinite(command.transition_velocity) ||
       command.transition_velocity < 0.0 ||
       command.transition_velocity > command.velocity ||
       !std::isfinite(command.transition_parameter)) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
    }
    if(blending) {
        if(command.transition_mode != TransitionMode::max_corner_deviation) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.transition_parameter <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        command.kind = GroupCommandKind::motion;
        command.direct_semantics = true;
        return submit_linear(command);
    }
    if(command.buffer_mode != BufferMode::aborting &&
       command.buffer_mode != BufferMode::buffered) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.transition_mode != TransitionMode::none) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
    }
    if(command.transition_velocity != 0.0 || command.transition_parameter != 0.0) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
    }
    const rt::ErrorCode framed = apply_coordinate_frame(command);
    if(framed != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(framed);
    }
    command = normalize(command);
    command.kind = GroupCommandKind::direct;
    command.direct_semantics = true;
    command.command_id = next_command_id_++;
    if(!members_ready_for_group_motion()) {
        return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
    }
    const rt::ErrorCode limited = preflight_member_targets(command.target);
    if(limited != rt::ErrorCode::ok) {
        return rt::Result<std::uint32_t>::failure(limited);
    }
    if(command.buffer_mode == BufferMode::aborting) {
        abort_wait();
        abort_halt();
        abort_direct_members();
        abort_motion();
    }
    if(command.buffer_mode == BufferMode::aborting ||
       (!active_ && !direct_path_.direct_active_ && !joint_window_.active_ &&
        !wait_blocks_motion_start())) {
        const rt::ErrorCode started = start_direct(command);
        if(started != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(started);
        }
    } else {
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
    }
    return rt::Result<std::uint32_t>::success(command.command_id);
}

inline bool AxisGroup::direct_motion_active() const
{
    return direct_path_.direct_active_;
}

inline bool AxisGroup::direct_command_done(std::uint32_t command_id) const
{
    return command_id != 0 && command_id == direct_path_.last_completed_direct_id_;
}

inline bool AxisGroup::direct_command_aborted(std::uint32_t command_id) const
{
    return command_id != 0 && command_id == direct_path_.last_aborted_direct_id_;
}

inline bool AxisGroup::direct_command_active(std::uint32_t command_id) const
{
    if(direct_path_.direct_active_ && direct_path_.direct_command_id_ == command_id) return true;
    if(active_ && active_command_.direct_semantics &&
       active_command_.command_id == command_id) return true;
    return joint_window_.active_ && !joint_window_.segments_.empty() &&
           joint_window_.segments_[joint_window_.index_].kind == WindowKind::direct_line &&
           joint_window_.segments_[joint_window_.index_].command_id == command_id;
}

inline bool AxisGroup::direct_command_busy(std::uint32_t command_id) const
{
    if(direct_command_active(command_id)) return true;
    for(std::size_t i = 0; i < queue_.size(); ++i) {
        if(queue_[i].direct_semantics &&
           queue_[i].command_id == command_id) return true;
    }
    if(joint_window_.active_) {
        for(std::size_t i = joint_window_.index_; i < joint_window_.segments_.size(); ++i) {
            if(joint_window_.segments_[i].kind == WindowKind::direct_line &&
               joint_window_.segments_[i].command_id == command_id) {
                return true;
            }
        }
    }
    return false;
}

inline std::uint32_t AxisGroup::last_completed_direct_command() const
{
    return direct_path_.last_completed_direct_id_;
}

inline std::uint32_t AxisGroup::last_aborted_direct_command() const
{
    return direct_path_.last_aborted_direct_id_;
}

inline void AxisGroup::abort_direct_members()
{
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        axes_[i]->abort_group_owned_motion();
    }
}

inline rt::ErrorCode AxisGroup::start_direct(const GroupCommand &command)
{
    if(!members_ready_for_group_motion()) return rt::ErrorCode::invalid_argument;
    std::array<AxisCommand, MaxAxes> commands{};
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        AxisCommand &member = commands[i];
        member.kind = CommandKind::move_absolute;
        member.value = command.target.value[i];
        member.velocity = command.velocity;
        member.acceleration = command.acceleration;
        member.deceleration = command.deceleration;
        member.jerk = command.jerk;
        const rt::ErrorCode preflight = axes_[i]->preflight_group_owned(member);
        if(preflight != rt::ErrorCode::ok) return preflight;
    }
    snapshot_selections();
    snapshot_active_tool_transform();
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        const rt::Result<std::uint32_t> submitted =
            axes_[i]->submit_group_owned(commands[i]);
        if(!submitted) {
            abort_direct_members();
            set_group_error(submitted.error());
            return submitted.error();
        }
    }
    direct_path_.direct_active_ = true;
    direct_path_.direct_stopping_ = false;
    direct_path_.direct_command_id_ = command.command_id;
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        active_finish_[i] = command.target.value[i];
    }
    status_ = GroupStatus::moving;
    return rt::ErrorCode::ok;
}

inline void AxisGroup::complete_direct(std::uint32_t command_id)
{
    direct_path_.last_completed_direct_id_ = command_id;
}

inline void AxisGroup::abort_direct(std::uint32_t command_id)
{
    direct_path_.last_aborted_direct_id_ = command_id;
}

inline rt::ErrorCode AxisGroup::stop_direct_members(double deceleration, double jerk)
{
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        if(!axes_[i]->powered() || axes_[i]->status() == AxisStatus::errorstop) {
            abort_direct_members();
            abort_motion();
            set_group_error(rt::ErrorCode::precondition_failed);
            return rt::ErrorCode::precondition_failed;
        }
    }

    for(std::size_t i = 0; i < axes_.size(); ++i) {
        AxisCommand halt{};
        halt.kind = CommandKind::halt;
        halt.velocity = 1.0;
        halt.acceleration = deceleration;
        halt.deceleration = deceleration;
        halt.jerk = jerk;
        const rt::Result<std::uint32_t> submitted = axes_[i]->submit_group_owned(halt);
        if(!submitted) {
            direct_path_.last_aborted_direct_id_ = direct_path_.direct_command_id_;
            abort_direct_members();
            abort_motion();
            status_ = GroupStatus::standby;
            return submitted.error();
        }
    }
    direct_path_.direct_stopping_ = true;
    status_ = GroupStatus::stopping;
    return rt::ErrorCode::ok;
}

} // namespace plcopen::core::axis
