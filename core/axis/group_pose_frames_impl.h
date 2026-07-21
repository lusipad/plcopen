#pragma once

// AxisGroup batch 4 frame/pose/kinematics implementation.
// Included by group.h after AxisGroup is complete.

namespace plcopen::core::axis
{

inline rt::ErrorCode AxisGroup::set_workpiece_frame(double x, double y, double z, double rot_z)
{
    return set_workpiece_frame_rpy(x, y, z, 0.0, 0.0, rot_z);
}

inline rt::ErrorCode AxisGroup::set_workpiece_frame_rpy(double x,
                                      double y,
                                      double z,
                                      double roll,
                                      double pitch,
                                      double yaw)
{
    if(status_ != GroupStatus::standby || !queue_.empty() || !std::isfinite(x) ||
       !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(roll) ||
       !std::isfinite(pitch) || !std::isfinite(yaw)) {
        return rt::ErrorCode::invalid_argument;
    }
    cancel_tracking();
    pose_frames_.workpiece_frame_ = geom::make_rpy_transform(x, y, z, roll, pitch, yaw);
    const double echo[6] = {x, y, z, roll, pitch, yaw};
    for(int i = 0; i < 6; ++i) {
        pose_frames_.workpiece_frame_rpy_[i] = echo[i];
    }
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::set_tool_transform_rpy(double x,
                                     double y,
                                     double z,
                                     double roll,
                                     double pitch,
                                     double yaw)
{
    if(numbered_tool_mode_) return rt::ErrorCode::precondition_failed;
    if(status_ != GroupStatus::standby || !queue_.empty() || !std::isfinite(x) ||
       !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(roll) ||
       !std::isfinite(pitch) || !std::isfinite(yaw)) {
        return rt::ErrorCode::invalid_argument;
    }
    pose_frames_.pose_tool_ = geom::make_rpy_transform(x, y, z, roll, pitch, yaw);
    pose_frames_.pose_tool_inverse_ = geom::invert(pose_frames_.pose_tool_);
    const double echo[6] = {x, y, z, roll, pitch, yaw};
    for(int i = 0; i < 6; ++i) {
        pose_frames_.tool_transform_rpy_[i] = echo[i];
    }
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::set_pose_kinematics(const kin::PoseKinematics *plugin,
                                  double min_singularity_margin,
                                  double max_joint_step)
{
    if(status_ != GroupStatus::standby || !queue_.empty() ||
       !std::isfinite(min_singularity_margin) || min_singularity_margin < 0.0 ||
       !std::isfinite(max_joint_step) || max_joint_step <= 0.0) {
        return rt::ErrorCode::invalid_argument;
    }
    if(plugin != nullptr && (pose_frames_.kinematics_ != nullptr || axes_.size() != 6 ||
                             plugin->joint_count() != 6)) {
        return rt::ErrorCode::invalid_argument;
    }
    pose_frames_.pose_kinematics_ = plugin;
    pose_frames_.pose_min_margin_ = min_singularity_margin;
    pose_frames_.pose_max_joint_step_ = max_joint_step;
    return rt::ErrorCode::ok;
}

inline void AxisGroup::workpiece_frame_rpy(double out[6]) const
{
    for(int i = 0; i < 6; ++i) {
        out[i] = pose_frames_.workpiece_frame_rpy_[i];
    }
}

inline void AxisGroup::tool_transform_rpy(double out[6]) const
{
    for(int i = 0; i < 6; ++i) {
        out[i] = pose_frames_.tool_transform_rpy_[i];
    }
}

inline geom::Vec3 AxisGroup::tool_offset() const
{
    return pose_frames_.tool_offset_;
}

inline rt::ErrorCode AxisGroup::read_cartesian(CoordSystem cs,
                             PositionSource source,
                             GroupPosition &out,
                             bool *gimbal_lock) const
{
    if(gimbal_lock != nullptr) {
        *gimbal_lock = false;
    }
    switch(cs) {
    case CoordSystem::acs:
    case CoordSystem::mcs:
    case CoordSystem::pcs:
        break;
    default:
        return rt::ErrorCode::unsupported;
    }
    if(axes_.size() == 0 || status_ == GroupStatus::disabled) {
        return rt::ErrorCode::invalid_argument;
    }
    out.size = axes_.size();
    double joints[MaxAxes] = {};
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        const AxisSnapshot &snapshot = axes_[i]->snapshot();
        joints[i] = source == PositionSource::actual ? snapshot.actual_position
                                                     : snapshot.command_position;
        out.value[i] = joints[i];
    }
    if(cs == CoordSystem::acs) {
        return rt::ErrorCode::ok;
    }

    if(pose_frames_.pose_kinematics_ != nullptr) {
        kin::Pose6 flange{};
        pose_frames_.pose_kinematics_->forward(joints, flange);
        geom::RigidTransform pose{};
        pose.translation = geom::Vec3{flange.position[0], flange.position[1],
                                      flange.position[2]};
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                pose.rotation[i][j] = flange.rotation[i][j];
            }
        }
        const geom::RigidTransform &tool = active_tool_transform_applies()
                                               ? pose_frames_.active_pose_tool_
                                               : pose_frames_.pose_tool_;
        pose = geom::compose(pose, tool);
        if(cs == CoordSystem::pcs) {
            pose = geom::compose(geom::invert(pose_frames_.workpiece_frame_), pose);
        }
        out.value[0] = pose.translation.x;
        out.value[1] = pose.translation.y;
        out.value[2] = pose.translation.z;
        double roll = 0.0;
        double pitch = 0.0;
        double yaw = 0.0;
        const bool gimbal = geom::extract_rpy(pose.rotation, roll, pitch, yaw);
        out.value[3] = roll;
        out.value[4] = pitch;
        out.value[5] = yaw;
        if(gimbal_lock != nullptr) {
            *gimbal_lock = gimbal;
        }
        return rt::ErrorCode::ok;
    }

    geom::Vec3 point{};
    if(pose_frames_.kinematics_ != nullptr) {
        const rt::ErrorCode forwarded =
            pose_frames_.kinematics_->forward(joints, axes_.size(), point);
        if(forwarded != rt::ErrorCode::ok) {
            return forwarded;
        }
    } else {
        point = cartesian_part(out);
    }
    point = point + (active_tool_transform_applies() ? pose_frames_.active_tool_offset_
                                                     : pose_frames_.tool_offset_);
    if(cs == CoordSystem::pcs) {
        point = geom::transform_point(geom::invert(pose_frames_.workpiece_frame_), point);
    }
    store_cartesian_part(out, point);
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::set_tool_offset(double x, double y, double z)
{
    if(numbered_tool_mode_) return rt::ErrorCode::precondition_failed;
    if(status_ != GroupStatus::standby || !queue_.empty()) {
        return rt::ErrorCode::invalid_argument;
    }
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return rt::ErrorCode::invalid_argument;
    }
    pose_frames_.tool_offset_ = geom::Vec3{x, y, z};
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::set_cartesian_velocity_limit(double limit)
{
    if(status_ != GroupStatus::standby || !queue_.empty() || !std::isfinite(limit) ||
       limit < 0.0) {
        return rt::ErrorCode::invalid_argument;
    }
    pose_frames_.cartesian_velocity_limit_ = limit;
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::set_kinematics(const kin::Kinematics *plugin,
                              double min_singularity_margin)
{
    if(status_ != GroupStatus::standby || !queue_.empty() ||
       !std::isfinite(min_singularity_margin) || min_singularity_margin < 0.0) {
        return rt::ErrorCode::invalid_argument;
    }
    if(plugin != nullptr &&
       (pose_frames_.pose_kinematics_ != nullptr || plugin->joint_count() != axes_.size() ||
        plugin->cartesian_count() != plugin->joint_count() ||
        plugin->cartesian_count() < 2 || plugin->cartesian_count() > 3)) {
        return rt::ErrorCode::invalid_argument;
    }
    pose_frames_.kinematics_ = plugin;
    pose_frames_.kinematics_min_margin_ = min_singularity_margin;
    return rt::ErrorCode::ok;
}

inline const kin::Kinematics *AxisGroup::kinematics_plugin() const
{ return pose_frames_.kinematics_; }

inline const kin::PoseKinematics *AxisGroup::pose_kinematics_plugin() const
{ return pose_frames_.pose_kinematics_; }

inline KinTransformRef AxisGroup::kin_transform() const
{
    if(pose_frames_.pose_kinematics_ != nullptr) {
        return {KinTransformKind::pose, nullptr, pose_frames_.pose_kinematics_};
    }
    if(pose_frames_.kinematics_ != nullptr) {
        return {KinTransformKind::kinematics, pose_frames_.kinematics_, nullptr};
    }
    return {};
}

inline rt::ErrorCode AxisGroup::set_coordinate_transform(CoordSystem coordinate_system,
                                       const ToolData &transform,
                                       ExecutionMode execution_mode)
{
    if(execution_mode != ExecutionMode::immediately) return rt::ErrorCode::unsupported;
    if(coordinate_system == CoordSystem::pcs) {
        return set_workpiece_frame_rpy(transform.value[0], transform.value[1],
                                       transform.value[2], transform.value[3],
                                       transform.value[4], transform.value[5]);
    }
    if(coordinate_system == CoordSystem::tcs) {
        return set_tool_transform_rpy(transform.value[0], transform.value[1],
                                      transform.value[2], transform.value[3],
                                      transform.value[4], transform.value[5]);
    }
    return rt::ErrorCode::unsupported;
}

inline rt::ErrorCode AxisGroup::coordinate_transform(CoordSystem coordinate_system,
                                   ToolData &transform) const
{
    if(coordinate_system == CoordSystem::pcs) {
        workpiece_frame_rpy(transform.value.data());
        return rt::ErrorCode::ok;
    }
    if(coordinate_system == CoordSystem::tcs) {
        tool_transform_rpy(transform.value.data());
        return rt::ErrorCode::ok;
    }
    return rt::ErrorCode::unsupported;
}

inline rt::ErrorCode AxisGroup::transform_position(const GroupPosition &position,
                                 CoordSystem source,
                                 CoordSystem target,
                                 GroupPosition &output,
                                 bool &singular_position) const
{
    singular_position = false;
    if(position.size != axes_.size() || axes_.empty()) {
        return rt::ErrorCode::invalid_argument;
    }
    for(std::size_t i = 0; i < position.size; ++i) {
        if(!std::isfinite(position.value[i])) return rt::ErrorCode::invalid_argument;
    }
    const auto supported = [](CoordSystem system) {
        return system == CoordSystem::acs || system == CoordSystem::mcs ||
               system == CoordSystem::pcs;
    };
    if(!supported(source) || !supported(target)) return rt::ErrorCode::unsupported;
    if(source == target) {
        output = position;
        return rt::ErrorCode::ok;
    }

    GroupPosition mcs = position;
    if(source == CoordSystem::acs) {
        if(pose_frames_.pose_kinematics_ != nullptr) {
            kin::Pose6 flange{};
            pose_frames_.pose_kinematics_->forward(position.value.data(), flange);
            geom::RigidTransform transform{};
            transform.translation = {flange.position[0], flange.position[1],
                                     flange.position[2]};
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    transform.rotation[row][column] = flange.rotation[row][column];
                }
            }
            const geom::RigidTransform &tool = active_tool_transform_applies()
                                                   ? pose_frames_.active_pose_tool_
                                                   : pose_frames_.pose_tool_;
            transform = geom::compose(transform, tool);
            mcs.value[0] = transform.translation.x;
            mcs.value[1] = transform.translation.y;
            mcs.value[2] = transform.translation.z;
            geom::extract_rpy(transform.rotation, mcs.value[3], mcs.value[4],
                              mcs.value[5]);
        } else {
            geom::Vec3 point{};
            if(pose_frames_.kinematics_ != nullptr) {
                const rt::ErrorCode result = pose_frames_.kinematics_->forward(
                    position.value.data(), position.size, point);
                if(result != rt::ErrorCode::ok) return result;
            } else {
                point = {position.value[0], position.value[1], position.value[2]};
            }
            point = point + (active_tool_transform_applies() ? pose_frames_.active_tool_offset_
                                                             : pose_frames_.tool_offset_);
            mcs.value[0] = point.x;
            mcs.value[1] = point.y;
            mcs.value[2] = point.z;
        }
    } else if(source == CoordSystem::pcs) {
        if(pose_frames_.pose_kinematics_ != nullptr) {
            geom::RigidTransform transform = geom::make_rpy_transform(
                position.value[0], position.value[1], position.value[2],
                position.value[3], position.value[4], position.value[5]);
            transform = geom::compose(pose_frames_.workpiece_frame_, transform);
            mcs.value[0] = transform.translation.x;
            mcs.value[1] = transform.translation.y;
            mcs.value[2] = transform.translation.z;
            geom::extract_rpy(transform.rotation, mcs.value[3], mcs.value[4],
                              mcs.value[5]);
        } else {
            const geom::Vec3 point = geom::transform_point(
                pose_frames_.workpiece_frame_, {position.value[0], position.value[1],
                                   position.value[2]});
            mcs.value[0] = point.x;
            mcs.value[1] = point.y;
            mcs.value[2] = point.z;
        }
    }

    if(target == CoordSystem::mcs) {
        output = mcs;
        return rt::ErrorCode::ok;
    }
    if(target == CoordSystem::pcs) {
        output = mcs;
        if(pose_frames_.pose_kinematics_ != nullptr) {
            geom::RigidTransform transform = geom::make_rpy_transform(
                mcs.value[0], mcs.value[1], mcs.value[2], mcs.value[3],
                mcs.value[4], mcs.value[5]);
            transform = geom::compose(geom::invert(pose_frames_.workpiece_frame_), transform);
            output.value[0] = transform.translation.x;
            output.value[1] = transform.translation.y;
            output.value[2] = transform.translation.z;
            geom::extract_rpy(transform.rotation, output.value[3], output.value[4],
                              output.value[5]);
        } else {
            const geom::Vec3 point = geom::transform_point(
                geom::invert(pose_frames_.workpiece_frame_),
                {mcs.value[0], mcs.value[1], mcs.value[2]});
            output.value[0] = point.x;
            output.value[1] = point.y;
            output.value[2] = point.z;
        }
        return rt::ErrorCode::ok;
    }

    output = mcs;
    if(pose_frames_.pose_kinematics_ != nullptr) {
        geom::RigidTransform tcp = geom::make_rpy_transform(
            mcs.value[0], mcs.value[1], mcs.value[2], mcs.value[3], mcs.value[4],
            mcs.value[5]);
        const geom::RigidTransform &tool_inverse = active_tool_transform_applies()
                                                       ? pose_frames_.active_pose_tool_inverse_
                                                       : pose_frames_.pose_tool_inverse_;
        const geom::RigidTransform flange = geom::compose(tcp, tool_inverse);
        kin::Pose6 pose{};
        pose.position[0] = flange.translation.x;
        pose.position[1] = flange.translation.y;
        pose.position[2] = flange.translation.z;
        for(int row = 0; row < 3; ++row) {
            for(int column = 0; column < 3; ++column) {
                pose.rotation[row][column] = flange.rotation[row][column];
            }
        }
        double seed[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            seed[i] = axes_[i]->snapshot().command_position;
        }
        const rt::ErrorCode result = pose_frames_.pose_kinematics_->inverse(
            pose, seed, pose_frames_.pose_max_joint_step_, output.value.data());
        if(result != rt::ErrorCode::ok) return result;
        singular_position =
            pose_frames_.pose_kinematics_->singularity_margin(output.value.data()) < pose_frames_.pose_min_margin_;
    } else {
        const geom::Vec3 tool = active_tool_transform_applies() ? pose_frames_.active_tool_offset_
                                                                : pose_frames_.tool_offset_;
        const geom::Vec3 point{mcs.value[0] - tool.x, mcs.value[1] - tool.y,
                               mcs.value[2] - tool.z};
        if(pose_frames_.kinematics_ != nullptr) {
            double seed[MaxAxes] = {};
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                seed[i] = axes_[i]->snapshot().command_position;
            }
            const rt::ErrorCode result = pose_frames_.kinematics_->inverse(
                point, seed, axes_.size(), output.value.data());
            if(result != rt::ErrorCode::ok) return result;
            singular_position = pose_frames_.kinematics_->singularity_margin(
                                    output.value.data(), axes_.size()) <
                                pose_frames_.kinematics_min_margin_;
        } else {
            output.value[0] = point.x;
            output.value[1] = point.y;
            output.value[2] = point.z;
        }
    }
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::validate_kinematics(const KinTransformRef &transform,
                                  double min_singularity_margin,
                                  double max_joint_step) const
{
    if(!std::isfinite(min_singularity_margin) || min_singularity_margin < 0.0) {
        return rt::ErrorCode::invalid_argument;
    }
    switch(transform.kind) {
    case KinTransformKind::none:
        return transform.kinematics == nullptr && transform.pose == nullptr
                   ? rt::ErrorCode::ok
                   : rt::ErrorCode::invalid_argument;
    case KinTransformKind::pose:
        if(transform.pose == nullptr || transform.kinematics != nullptr) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!std::isfinite(max_joint_step) || max_joint_step <= 0.0 ||
           axes_.size() != 6 ||
           transform.pose->joint_count() != 6) {
            return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::ok;
    case KinTransformKind::kinematics:
        if(transform.kinematics == nullptr || transform.pose != nullptr ||
           transform.kinematics->joint_count() != axes_.size() ||
           transform.kinematics->cartesian_count() !=
               transform.kinematics->joint_count() ||
           transform.kinematics->cartesian_count() < 2 ||
           transform.kinematics->cartesian_count() > 3) {
            return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::ok;
    default:
        return rt::ErrorCode::invalid_argument;
    }
}

inline rt::ErrorCode AxisGroup::apply_kinematics(const KinTransformRef &transform,
                               double min_singularity_margin,
                               double max_joint_step)
{
    const rt::ErrorCode valid = validate_kinematics(
        transform, min_singularity_margin, max_joint_step);
    if(valid != rt::ErrorCode::ok) return valid;
    if(transform.kind == KinTransformKind::none) {
        pose_frames_.kinematics_ = nullptr;
        pose_frames_.pose_kinematics_ = nullptr;
        pose_frames_.kinematics_min_margin_ = 0.0;
        pose_frames_.pose_min_margin_ = 0.0;
        pose_frames_.pose_max_joint_step_ = 0.01;
    } else if(transform.kind == KinTransformKind::kinematics) {
        pose_frames_.kinematics_ = transform.kinematics;
        pose_frames_.kinematics_min_margin_ = min_singularity_margin;
        pose_frames_.pose_kinematics_ = nullptr;
        pose_frames_.pose_min_margin_ = 0.0;
        pose_frames_.pose_max_joint_step_ = 0.01;
    } else {
        pose_frames_.pose_kinematics_ = transform.pose;
        pose_frames_.pose_min_margin_ = min_singularity_margin;
        pose_frames_.pose_max_joint_step_ = max_joint_step;
        pose_frames_.kinematics_ = nullptr;
        pose_frames_.kinematics_min_margin_ = 0.0;
    }
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::validate_coordinate_transform(CoordSystem coordinate_system,
                                            const ToolData &transform) const
{
    if(coordinate_system != CoordSystem::pcs && coordinate_system != CoordSystem::tcs) {
        return rt::ErrorCode::unsupported;
    }
    if(coordinate_system == CoordSystem::tcs && numbered_tool_mode_) {
        return rt::ErrorCode::precondition_failed;
    }
    for(double value : transform.value) {
        if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
    }
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::apply_coordinate_transform(CoordSystem coordinate_system,
                                         const ToolData &transform)
{
    const rt::ErrorCode valid = validate_coordinate_transform(
        coordinate_system, transform);
    if(valid != rt::ErrorCode::ok) return valid;
    if(coordinate_system == CoordSystem::pcs) {
        cancel_tracking();
        pose_frames_.workpiece_frame_ = geom::make_rpy_transform(
            transform.value[0], transform.value[1], transform.value[2],
            transform.value[3], transform.value[4], transform.value[5]);
        for(int i = 0; i < 6; ++i) {
            pose_frames_.workpiece_frame_rpy_[i] = transform.value[i];
        }
        return rt::ErrorCode::ok;
    }
    pose_frames_.pose_tool_ = geom::make_rpy_transform(
        transform.value[0], transform.value[1], transform.value[2],
        transform.value[3], transform.value[4], transform.value[5]);
    pose_frames_.pose_tool_inverse_ = geom::invert(pose_frames_.pose_tool_);
    for(int i = 0; i < 6; ++i) {
        pose_frames_.tool_transform_rpy_[i] = transform.value[i];
    }
    return rt::ErrorCode::ok;
}

inline void AxisGroup::snapshot_active_tool_transform()
{
    pose_frames_.active_pose_tool_ = pose_frames_.pose_tool_;
    pose_frames_.active_pose_tool_inverse_ = pose_frames_.pose_tool_inverse_;
    pose_frames_.active_tool_offset_ = pose_frames_.tool_offset_;
}

inline bool AxisGroup::active_tool_transform_applies() const
{
    return status_ == GroupStatus::moving || status_ == GroupStatus::stopping ||
           status_ == GroupStatus::interrupted;
}

inline geom::Vec3 AxisGroup::cartesian_part(const GroupPosition &position)
{
    return geom::Vec3{position.value[0],
                      position.size > 1 ? position.value[1] : 0.0,
                      position.size > 2 ? position.value[2] : 0.0};
}

inline rt::ErrorCode AxisGroup::select_orientation_interpolation(GroupCommand &command) const
{
    switch(command.orientation_mode) {
    case OrientationMode::joint_space:
        return rt::ErrorCode::ok;
    case OrientationMode::shortest_path:
    case OrientationMode::constant:
        if(pose_frames_.pose_kinematics_ == nullptr) {
            return rt::ErrorCode::unsupported;
        }
        if(command.coord_system != CoordSystem::mcs &&
           command.coord_system != CoordSystem::pcs) {
            return rt::ErrorCode::unsupported;
        }
        command.interpolation_space = InterpolationSpace::cartesian;
        return rt::ErrorCode::ok;
    default:
        return rt::ErrorCode::invalid_argument;
    }
}

inline void AxisGroup::store_cartesian_part(GroupPosition &position, geom::Vec3 point)
{
    position.value[0] = point.x;
    if(position.size > 1) {
        position.value[1] = point.y;
    }
    if(position.size > 2) {
        position.value[2] = point.z;
    }
}

inline rt::ErrorCode AxisGroup::apply_coordinate_frame(GroupCommand &command) const
{
    switch(command.coord_system) {
    case CoordSystem::acs:
        return rt::ErrorCode::ok;
    case CoordSystem::mcs:
    case CoordSystem::pcs:
        break;
    default:
        return rt::ErrorCode::unsupported;
    }

    const bool pcs = command.coord_system == CoordSystem::pcs;
    const bool circular = command.path_kind == GroupPathKind::circular;

    // Orientation batch (approved matrix, decision #6): the pose
    // pipeline consumes [x,y,z,roll,pitch,yaw] targets on 6-joint
    // groups. v1 is submit_linear + absolute only; relative, circular,
    // and blending transitions report explicit unsupported. The frame
    // and tool compose on the pose, the analytic inverse (seeded by the
    // segment start joints, KB-041 gates) lands the 6 ACS joint targets,
    // and the in-segment interpolation stays a joint-space line
    // (declared boundary, orientation edition).
    if(pose_frames_.pose_kinematics_ != nullptr) {
        if(circular || command.relative ||
           command.buffer_mode == BufferMode::blending_low ||
           command.buffer_mode == BufferMode::blending_high) {
            return rt::ErrorCode::unsupported;
        }
        geom::RigidTransform target = geom::make_rpy_transform(
            command.target.value[0], command.target.value[1],
            command.target.value[2], command.target.value[3],
            command.target.value[4], command.target.value[5]);
        if(pcs) {
            target = geom::compose(pose_frames_.workpiece_frame_, target);
        }
        const geom::RigidTransform flange = geom::compose(target, pose_frames_.pose_tool_inverse_);

        kin::Pose6 pose{};
        pose.position[0] = flange.translation.x;
        pose.position[1] = flange.translation.y;
        pose.position[2] = flange.translation.z;
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                pose.rotation[i][j] = flange.rotation[i][j];
            }
        }

        double seed[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            seed[i] = queued_finish(i);
        }
        double joints[MaxAxes] = {};
        const rt::ErrorCode inverted =
            pose_frames_.pose_kinematics_->inverse(pose, seed, pose_frames_.pose_max_joint_step_, joints);
        if(inverted != rt::ErrorCode::ok) {
            return inverted;
        }
        if(pose_frames_.pose_kinematics_->singularity_margin(joints) < pose_frames_.pose_min_margin_) {
            return rt::ErrorCode::precondition_failed;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            command.target.value[i] = joints[i];
        }
        command.coord_system = CoordSystem::acs;
        return rt::ErrorCode::ok;
    }

    // Kinematics-configured pipeline (approved kinematics matrix): the
    // Cartesian point goes through the workpiece frame and tool offset,
    // then the inverse solution — seeded with the segment start joints —
    // becomes the ACS joint target. v1 solves endpoints and aux points
    // only; the in-segment interpolation stays joint-space (declared
    // boundary: an MCS line is a joint-space line, not a Cartesian line,
    // on nonlinear mechanisms).
    if(pose_frames_.kinematics_ != nullptr) {
        double seed[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            seed[i] = queued_finish(i);
        }
        rt::ErrorCode solved =
            solve_cartesian_target(command.target, command.relative, pcs, seed);
        if(solved != rt::ErrorCode::ok) {
            return solved;
        }
        if(circular) {
            solved = solve_cartesian_target(command.aux, command.relative, pcs, seed);
            if(solved != rt::ErrorCode::ok) {
                return solved;
            }
        }
        command.relative = false;
        command.coord_system = CoordSystem::acs;

        // Dual-space limiting (BS3.6): sample the joint-space chord
        // through the forward solution; the worst Cartesian displacement
        // per path-parameter step scales the command velocity down. The
        // path parameter references the longest member travel (KB-027).
        if(pose_frames_.cartesian_velocity_limit_ > 0.0 && !circular) {
            double longest = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double travel = std::fabs(command.target.value[i] - seed[i]);
                if(travel > longest) {
                    longest = travel;
                }
            }
            if(longest > 0.0) {
                constexpr int Samples = 16;
                double joints[MaxAxes] = {};
                geom::Vec3 previous{};
                double worst_ratio = 0.0;
                for(int step = 0; step <= Samples; ++step) {
                    const double fraction =
                        static_cast<double>(step) / static_cast<double>(Samples);
                    for(std::size_t i = 0; i < axes_.size(); ++i) {
                        joints[i] =
                            seed[i] + fraction * (command.target.value[i] - seed[i]);
                    }
                    geom::Vec3 cartesian{};
                    const rt::ErrorCode forwarded =
                        pose_frames_.kinematics_->forward(joints, axes_.size(), cartesian);
                    if(forwarded != rt::ErrorCode::ok) {
                        return forwarded;
                    }
                    if(step > 0) {
                        const double chord = geom::norm(cartesian - previous);
                        const double parameter_step =
                            longest / static_cast<double>(Samples);
                        const double ratio = chord / parameter_step;
                        if(ratio > worst_ratio) {
                            worst_ratio = ratio;
                        }
                    }
                    previous = cartesian;
                }
                if(worst_ratio > 0.0) {
                    const double allowed = pose_frames_.cartesian_velocity_limit_ / worst_ratio;
                    if(allowed < command.velocity) {
                        command.velocity = allowed;
                    }
                }
            }
        }
        return rt::ErrorCode::ok;
    }

    if(command.relative) {
        geom::Vec3 direction = cartesian_part(command.target);
        if(pcs) {
            direction = geom::transform_rotate(pose_frames_.workpiece_frame_, direction);
        }
        store_cartesian_part(command.target, direction);
        if(circular) {
            geom::Vec3 aux = cartesian_part(command.aux);
            if(pcs) {
                aux = geom::transform_rotate(pose_frames_.workpiece_frame_, aux);
            }
            store_cartesian_part(command.aux, aux);
        }
    } else {
        geom::Vec3 point = cartesian_part(command.target);
        if(pcs) {
            point = geom::transform_point(pose_frames_.workpiece_frame_, point);
        }
        store_cartesian_part(command.target,
                             point - pose_frames_.tool_offset_);
        if(circular) {
            geom::Vec3 aux = cartesian_part(command.aux);
            if(pcs) {
                aux = geom::transform_point(pose_frames_.workpiece_frame_, aux);
            }
            store_cartesian_part(command.aux, aux - pose_frames_.tool_offset_);
        }
    }
    command.coord_system = CoordSystem::acs;
    return rt::ErrorCode::ok;
}

inline rt::ErrorCode AxisGroup::solve_cartesian_target(GroupPosition &position,
                                     bool relative,
                                     bool pcs,
                                     const double *seed) const
{
    geom::Vec3 point = cartesian_part(position);
    if(relative) {
        if(pcs) {
            point = geom::transform_rotate(pose_frames_.workpiece_frame_, point);
        }
        geom::Vec3 start{};
        const rt::ErrorCode forwarded =
            pose_frames_.kinematics_->forward(seed, axes_.size(), start);
        if(forwarded != rt::ErrorCode::ok) {
            return forwarded;
        }
        point = start + point;
    } else {
        if(pcs) {
            point = geom::transform_point(pose_frames_.workpiece_frame_, point);
        }
        point = point - pose_frames_.tool_offset_;
    }

    double joints[MaxAxes] = {};
    const rt::ErrorCode inverted =
        pose_frames_.kinematics_->inverse(point, seed, axes_.size(), joints);
    if(inverted != rt::ErrorCode::ok) {
        return inverted;
    }
    if(pose_frames_.kinematics_->singularity_margin(joints, axes_.size()) < pose_frames_.kinematics_min_margin_) {
        return rt::ErrorCode::precondition_failed;
    }
    for(std::size_t i = 0; i < axes_.size(); ++i) {
        position.value[i] = joints[i];
    }
    return rt::ErrorCode::ok;
}

} // namespace plcopen::core::axis
