#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Part 4 path table and transform FBs (approved matrix 2026-07-07).

// Waypoint for MC_PathSelect/MC_MovePath.
struct PathWaypoint
{
    axis::GroupPosition target{};
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    axis::TransitionMode transition_mode = axis::TransitionMode::none;
    double transition_parameter = 0.0;
    axis::InterpolationSpace interpolation_space = axis::InterpolationSpace::joint;
};

// PathTable: validated waypoint array with a handle. The caller owns the
// storage; PathSelect validates and stamps a handle, MovePath consumes it.
struct PathTable
{
    static constexpr std::size_t MaxWaypoints = 32;

    PathWaypoint waypoints[MaxWaypoints]{};
    std::size_t count = 0;
    std::uint32_t handle = 0;
    std::size_t axis_count = 0;
};

// MC_PathSelect: validate a path table and issue a handle.
class FbPathSelect
{
public:
    axis::AxisGroup *group_ref = nullptr;
    PathTable *table = nullptr;
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
        if(group_ref == nullptr || table == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        if(table->count < 2 || table->count > PathTable::MaxWaypoints) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const std::size_t n_axes = group_ref->member_count();
        for(std::size_t i = 0; i < table->count; ++i) {
            const PathWaypoint &wp = table->waypoints[i];
            if(wp.target.size != n_axes) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            for(std::size_t j = 0; j < n_axes; ++j) {
                if(!std::isfinite(wp.target.value[j])) {
                    outputs.error = true;
                    outputs.error_id = rt::ErrorCode::invalid_argument;
                    return;
                }
            }
            if(!std::isfinite(wp.velocity) || wp.velocity <= 0.0 ||
               !std::isfinite(wp.acceleration) || wp.acceleration <= 0.0 ||
               !std::isfinite(wp.deceleration) || wp.deceleration <= 0.0 ||
               !std::isfinite(wp.jerk) || wp.jerk <= 0.0) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
            if(!std::isfinite(wp.transition_parameter)) {
                outputs.error = true;
                outputs.error_id = rt::ErrorCode::invalid_argument;
                return;
            }
        }
        table->axis_count = n_axes;
        table->handle = ++next_handle_;
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
    std::uint32_t next_handle_ = 0;
};

// MC_MovePath: consume a validated path table handle and submit all
// waypoints through the group's window machine as blending commands.
class FbMovePath : public GroupExecuteFb
{
public:
    PathTable *table = nullptr;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr || table == nullptr || table->handle == 0 ||
               table->count < 2 || table->axis_count != group_ref->member_count()) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const PathWaypoint &first = table->waypoints[0];
            axis::GroupCommand cmd{};
            cmd.target = first.target;
            cmd.velocity = first.velocity;
            cmd.acceleration = first.acceleration;
            cmd.deceleration = first.deceleration;
            cmd.jerk = first.jerk;
            cmd.buffer_mode = axis::BufferMode::aborting;
            cmd.interpolation_space = first.interpolation_space;
            const auto result = group_ref->submit_linear(cmd);
            if(!result) {
                accept(result);
                return;
            }
            for(std::size_t i = 1; i < table->count; ++i) {
                const PathWaypoint &wp = table->waypoints[i];
                axis::GroupCommand seg{};
                seg.target = wp.target;
                seg.velocity = wp.velocity;
                seg.acceleration = wp.acceleration;
                seg.deceleration = wp.deceleration;
                seg.jerk = wp.jerk;
                seg.transition_mode = wp.transition_mode;
                seg.transition_parameter = wp.transition_parameter;
                seg.interpolation_space = wp.interpolation_space;
                if(i < table->count - 1 &&
                   wp.transition_mode != axis::TransitionMode::none) {
                    seg.buffer_mode = axis::BufferMode::blending_low;
                } else {
                    seg.buffer_mode = axis::BufferMode::buffered;
                }
                const auto seg_result = group_ref->submit_linear(seg);
                if(!seg_result) {
                    accept(seg_result);
                    return;
                }
            }
            accept(result);
        }
        observe_group();
    }
};

class FbSyncGroupToAxis
{
public:
    FbSyncGroupToAxis()
    {
        tuc_numerator.fill(1);
        tuc_denominator.fill(1);
    }

    axis::AxisModel *master_ref = nullptr;
    axis::AxisGroup *group_ref = nullptr;
    PathTable *path_data = nullptr;
    bool execute = false;
    axis::PathMode mode = axis::PathMode::non_periodic;
    std::array<int, axis::AxisGroup::MaxAxes> tuc_numerator{};
    std::array<int, axis::AxisGroup::MaxAxes> tuc_denominator{};
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    MotionOutputs outputs{};
    bool in_sync = false;

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            in_sync = false;
            tracked_command_id_ = 0;
            return;
        }
        if(rising) {
            submit();
        }
        observe();
    }

private:
    void submit()
    {
        clear(outputs);
        if(master_ref == nullptr || group_ref == nullptr || path_data == nullptr ||
           path_data->handle == 0 || path_data->axis_count != group_ref->member_count() ||
           !std::isfinite(acceleration) || !std::isfinite(deceleration) ||
           !std::isfinite(jerk) || acceleration < 0.0 || deceleration < 0.0 || jerk < 0.0) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        for(std::size_t i = 0; i < path_data->count; ++i) {
            sync_path_[i] = path_data->waypoints[i].target;
        }
        const rt::Result<std::uint32_t> accepted = group_ref->sync_group_to_axis(
            *master_ref, sync_path_.data(), path_data->count, mode, tuc_numerator,
            tuc_denominator, coord_system, buffer_mode);
        if(!accepted) {
            outputs.error = true;
            outputs.error_id = accepted.error();
            return;
        }
        tracked_command_id_ = accepted.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
        in_sync = true;
    }

    void observe()
    {
        if(tracked_command_id_ == 0 || group_ref == nullptr) return;
        if(group_ref->group_to_axis_sync_active(tracked_command_id_)) {
            outputs.busy = true;
            outputs.active = true;
            in_sync = true;
            return;
        }
        if(group_ref->group_to_axis_sync_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
        }
        outputs.busy = false;
        outputs.active = false;
        in_sync = false;
        tracked_command_id_ = 0;
    }

    std::array<axis::GroupPosition, PathTable::MaxWaypoints> sync_path_{};
    bool last_execute_ = false;
    std::uint32_t tracked_command_id_ = 0;
};

// MC_SetKinTransform: FB facade for set_pose_kinematics / set_kinematics.
class FbSetKinTransform : public GroupExecuteFb
{
public:
    const kin::PoseKinematics *pose_plugin = nullptr;
    const kin::Kinematics *kinematics_plugin = nullptr;
    double min_singularity_margin = 0.0;
    double max_joint_step = 0.01;

    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        if(pose_plugin != nullptr) {
            const rt::ErrorCode result = group_ref->set_pose_kinematics(
                pose_plugin, min_singularity_margin, max_joint_step);
            if(result != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(result));
                return;
            }
        }
        if(kinematics_plugin != nullptr) {
            const rt::ErrorCode result =
                group_ref->set_kinematics(kinematics_plugin, min_singularity_margin);
            if(result != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(result));
                return;
            }
        }
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

// MC_ReadCartesianTransform: enable-based readback of frame configuration.
class FbReadCartesianTransform
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    double workpiece_frame[6] = {};
    double tool_transform[6] = {};

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            for(int i = 0; i < 6; ++i) {
                workpiece_frame[i] = 0.0;
                tool_transform[i] = 0.0;
            }
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        group_ref->workpiece_frame_rpy(workpiece_frame);
        group_ref->tool_transform_rpy(tool_transform);
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

} // namespace plcopen::core::fb
