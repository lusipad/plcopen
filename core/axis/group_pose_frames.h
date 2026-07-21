#pragma once

#include "geom/frame.h"
#include "kin/kinematics.h"
#include "kin/pose.h"

namespace plcopen::core::axis
{

class AxisGroup;

// Frame/tool transform, pose/kinematics and readback context. Tool/payload
// stores and selection-management state deliberately remain owned by
// AxisGroup.
class GroupPoseFramesState
{
    friend class AxisGroup;

    const kin::PoseKinematics *pose_kinematics_ = nullptr;
    double pose_min_margin_ = 0.0;
    double pose_max_joint_step_ = 0.0;
    const kin::Kinematics *kinematics_ = nullptr;
    double kinematics_min_margin_ = 0.0;
    double cartesian_velocity_limit_ = 0.0;
    geom::Vec3 tool_offset_{};
    double workpiece_frame_rpy_[6] = {};
    double tool_transform_rpy_[6] = {};
    geom::RigidTransform workpiece_frame_{};
    geom::RigidTransform tracking_hold_pose_{};
    geom::RigidTransform pending_dynamic_reference_frame_{};
    geom::RigidTransform active_dynamic_reference_frame_{};
    geom::RigidTransform pose_tool_{};
    geom::RigidTransform pose_tool_inverse_{};
    geom::RigidTransform active_pose_tool_inverse_{};
    geom::RigidTransform active_pose_tool_{};
    geom::RigidTransform jog_pose_tool_inverse_{};
    geom::Vec3 jog_tool_offset_{};
    geom::Vec3 active_tool_offset_{};
};

} // namespace plcopen::core::axis
