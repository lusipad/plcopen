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

// Supplier-specific MC_PATH_REF. The caller owns this fixed-capacity source
// description and may reuse it after PathSelect returns.
struct PathDescription
{
    static constexpr std::size_t MaxWaypoints = 32;

    PathWaypoint waypoints[MaxWaypoints]{};
    std::size_t count = 0;
};

// Supplier-specific MC_PATH_DATA_REF. The caller owns the selected result;
// PathSelect copies a validated description into it and MovePath consumes it.
struct PathTable
{
    static constexpr std::size_t MaxWaypoints = PathDescription::MaxWaypoints;

    PathWaypoint waypoints[MaxWaypoints]{};
    std::size_t count = 0;
    std::uint32_t handle = 0;
    std::size_t axis_count = 0;
};

inline rt::ErrorCode validate_path_waypoint(const PathWaypoint &waypoint,
                                            std::size_t axis_count)
{
    if(waypoint.target.size != axis_count) return rt::ErrorCode::invalid_argument;
    for(std::size_t axis_index = 0; axis_index < axis_count; ++axis_index) {
        if(!std::isfinite(waypoint.target.value[axis_index])) {
            return rt::ErrorCode::invalid_argument;
        }
    }
    if(!std::isfinite(waypoint.velocity) || waypoint.velocity <= 0.0 ||
       !std::isfinite(waypoint.acceleration) || waypoint.acceleration <= 0.0 ||
       !std::isfinite(waypoint.deceleration) || waypoint.deceleration <= 0.0 ||
       !std::isfinite(waypoint.jerk) || waypoint.jerk <= 0.0 ||
       !std::isfinite(waypoint.transition_parameter)) {
        return rt::ErrorCode::invalid_argument;
    }
    if(waypoint.interpolation_space != axis::InterpolationSpace::joint &&
       waypoint.interpolation_space != axis::InterpolationSpace::cartesian) {
        return rt::ErrorCode::invalid_argument;
    }
    if(waypoint.transition_mode == axis::TransitionMode::none) {
        return waypoint.transition_parameter == 0.0
                   ? rt::ErrorCode::ok
                   : rt::ErrorCode::invalid_argument;
    }
    if(waypoint.transition_mode == axis::TransitionMode::max_corner_deviation) {
        return waypoint.transition_parameter > 0.0
                   ? rt::ErrorCode::ok
                   : rt::ErrorCode::invalid_argument;
    }
    return rt::ErrorCode::unsupported;
}

// MC_PathSelect: validate a source description and publish a selected result.
class FbPathSelect
{
public:
    axis::AxisGroup *group_ref = nullptr;
    PathTable *path_data = nullptr;
    const PathDescription *path_description = nullptr;
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
        if(group_ref == nullptr || path_data == nullptr || path_description == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        if(path_description->count < 2 ||
           path_description->count > PathTable::MaxWaypoints) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const std::size_t n_axes = group_ref->member_count();
        if(n_axes < 2) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        for(std::size_t i = 0; i < path_description->count; ++i) {
            const PathWaypoint &wp = path_description->waypoints[i];
            const rt::ErrorCode valid = validate_path_waypoint(wp, n_axes);
            if(valid != rt::ErrorCode::ok) {
                outputs.error = true;
                outputs.error_id = valid;
                return;
            }
        }
        PathTable selected{};
        selected.count = path_description->count;
        selected.axis_count = n_axes;
        selected.handle = ++next_handle_;
        if(selected.handle == 0) {
            selected.handle = ++next_handle_;
        }
        for(std::size_t i = 0; i < selected.count; ++i) {
            selected.waypoints[i] = path_description->waypoints[i];
        }
        *path_data = selected;
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
    PathTable *path_data = nullptr;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    axis::TransitionMode transition_mode = axis::TransitionMode::none;
    double transition_parameter = 0.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr || path_data == nullptr || path_data->handle == 0 ||
               path_data->count < 2 ||
               path_data->count > PathTable::MaxWaypoints ||
               path_data->axis_count != group_ref->member_count()) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
                return;
            }
            const rt::ErrorCode options = validate_options();
            if(options != rt::ErrorCode::ok) {
                accept(rt::Result<std::uint32_t>::failure(options));
                return;
            }
            for(std::size_t i = 0; i < path_data->count; ++i) {
                const rt::ErrorCode valid =
                    validate_path_waypoint(path_data->waypoints[i], path_data->axis_count);
                if(valid != rt::ErrorCode::ok) {
                    accept(rt::Result<std::uint32_t>::failure(valid));
                    return;
                }
            }
            const PathWaypoint &first = path_data->waypoints[0];
            axis::GroupCommand cmd{};
            cmd.target = first.target;
            cmd.velocity = first.velocity;
            cmd.acceleration = first.acceleration;
            cmd.deceleration = first.deceleration;
            cmd.jerk = first.jerk;
            cmd.buffer_mode = buffer_mode;
            cmd.coord_system = coord_system;
            cmd.transition_mode = transition_mode;
            cmd.transition_parameter = transition_parameter;
            cmd.interpolation_space = first.interpolation_space;
            const auto result = group_ref->submit_linear(cmd);
            if(!result) {
                accept(result);
                return;
            }
            for(std::size_t i = 1; i < path_data->count; ++i) {
                const PathWaypoint &wp = path_data->waypoints[i];
                axis::GroupCommand seg{};
                seg.target = wp.target;
                seg.velocity = wp.velocity;
                seg.acceleration = wp.acceleration;
                seg.deceleration = wp.deceleration;
                seg.jerk = wp.jerk;
                seg.transition_mode = wp.transition_mode;
                seg.transition_parameter = wp.transition_parameter;
                seg.interpolation_space = wp.interpolation_space;
                seg.coord_system = coord_system;
                if(i < path_data->count - 1 &&
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

private:
    rt::ErrorCode validate_options() const
    {
        switch(coord_system) {
        case axis::CoordSystem::acs:
        case axis::CoordSystem::mcs:
        case axis::CoordSystem::pcs:
            break;
        case axis::CoordSystem::wcs:
        case axis::CoordSystem::fcs:
        case axis::CoordSystem::tcs:
            return rt::ErrorCode::unsupported;
        default:
            return rt::ErrorCode::invalid_argument;
        }
        switch(buffer_mode) {
        case axis::BufferMode::aborting:
        case axis::BufferMode::buffered:
        case axis::BufferMode::blending_low:
        case axis::BufferMode::blending_high:
            break;
        default:
            return rt::ErrorCode::invalid_argument;
        }
        if(!std::isfinite(transition_parameter)) {
            return rt::ErrorCode::invalid_argument;
        }
        if(transition_mode == axis::TransitionMode::none) {
            return transition_parameter == 0.0
                       ? rt::ErrorCode::ok
                       : rt::ErrorCode::invalid_argument;
        }
        if(transition_mode != axis::TransitionMode::max_corner_deviation) {
            return rt::ErrorCode::unsupported;
        }
        if(transition_parameter <= 0.0 || buffer_mode == axis::BufferMode::aborting) {
            return rt::ErrorCode::invalid_argument;
        }
        if(buffer_mode == axis::BufferMode::buffered) {
            return rt::ErrorCode::unsupported;
        }
        return rt::ErrorCode::ok;
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
    axis::KinTransformRef kin_transform{};
    double min_singularity_margin = 0.0;
    double max_joint_step = 0.01;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                accept(group_ref->submit_kinematics(
                    kin_transform, min_singularity_margin, max_joint_step,
                    execution_mode));
            }
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error || outputs.command_aborted) return;
        if(group_ref->management_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        const rt::ErrorCode error = group_ref->management_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->management_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->management_command_active(tracked_command_id_);
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
    axis::CoordSystem coord_system = axis::CoordSystem::pcs;
    axis::ToolData transform{};
    double trans_x = 0.0;
    double trans_y = 0.0;
    double trans_z = 0.0;
    double rot_angle1 = 0.0;
    double rot_angle2 = 0.0;
    double rot_angle3 = 0.0;

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            transform = {};
            clear_scalars();
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        transform = {};
        const rt::ErrorCode result = group_ref->coordinate_transform(
            coord_system, transform);
        if(result != rt::ErrorCode::ok) {
            valid = false;
            error = true;
            error_id = result;
            clear_scalars();
            return;
        }
        trans_x = transform.value[0];
        trans_y = transform.value[1];
        trans_z = transform.value[2];
        rot_angle1 = transform.value[3];
        rot_angle2 = transform.value[4];
        rot_angle3 = transform.value[5];
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    void clear_scalars()
    {
        trans_x = trans_y = trans_z = 0.0;
        rot_angle1 = rot_angle2 = rot_angle3 = 0.0;
    }
};

class FbSetCoordinateTransform : public GroupExecuteFb
{
public:
    axis::CoordSystem coordinate_system = axis::CoordSystem::pcs;
    axis::ToolData transform{};
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                accept(group_ref->submit_coordinate_transform(
                    coordinate_system, transform, execution_mode));
            }
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error || outputs.command_aborted) return;
        if(group_ref->management_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        const rt::ErrorCode error = group_ref->management_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->management_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->management_command_active(tracked_command_id_);
    }
};

class FbSetCartesianTransform : public GroupExecuteFb
{
public:
    axis::CoordSystem coordinate_system = axis::CoordSystem::pcs;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;
    double trans_x = 0.0;
    double trans_y = 0.0;
    double trans_z = 0.0;
    double rot_angle1 = 0.0;
    double rot_angle2 = 0.0;
    double rot_angle3 = 0.0;

    void call()
    {
        if(rising_edge()) {
            if(group_ref == nullptr) {
                accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            } else {
                axis::ToolData transform{};
                transform.value = {trans_x, trans_y, trans_z,
                                   rot_angle1, rot_angle2, rot_angle3};
                accept(group_ref->submit_coordinate_transform(
                    coordinate_system, transform, execution_mode));
            }
        }
        observe();
    }

private:
    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error || outputs.command_aborted) return;
        if(group_ref->management_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        const rt::ErrorCode error = group_ref->management_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->management_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->management_command_active(tracked_command_id_);
    }
};

class FbReadKinTransform
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::KinTransformRef kin_transform{};

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            kin_transform = {};
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            kin_transform = {};
            return;
        }
        kin_transform = group_ref->kin_transform();
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

class FbReadCoordinateTransform
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::CoordSystem coordinate_system = axis::CoordSystem::pcs;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::ToolData transform{};

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            transform = {};
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode result =
            group_ref->coordinate_transform(coordinate_system, transform);
        valid = result == rt::ErrorCode::ok;
        error = !valid;
        error_id = result;
    }
};

class FbGroupTransformPosition
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::GroupPosition position{};
    axis::CoordSystem source = axis::CoordSystem::acs;
    axis::CoordSystem target = axis::CoordSystem::mcs;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::GroupPosition output_position{};
    bool singular_position = false;

    void call()
    {
        if(!enable) {
            valid = false;
            busy = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            output_position = {};
            singular_position = false;
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            busy = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode result = group_ref->transform_position(
            position, source, target, output_position, singular_position);
        valid = result == rt::ErrorCode::ok;
        busy = false;
        error = !valid;
        error_id = result;
    }
};

} // namespace plcopen::core::fb
