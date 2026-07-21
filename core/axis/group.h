#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "axis/group_cartesian.h"
#include "axis/group_direct_path.h"
#include "axis/group_pose_frames.h"
#include "axis/group_takeover_connector.h"
#include "axis/group_window.h"
#include "axis/state.h"
#include "geom/frame.h"
#include "geom/geometry.h"
#include "kin/kinematics.h"
#include "kin/pose.h"
#include "otg/profile1d.h"
#include "plan/path.h"
#include "otg/time_optimal.h"
#include "rt/cycle.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::axis
{

struct GroupPosition
{
    static constexpr std::size_t MaxAxes = 8;
    std::array<double, MaxAxes> value{};
    std::size_t size = 0;
};

struct IdentInGroup
{
    std::size_t index = 0;
};

struct DHParameter
{
    double theta = 0.0;
    double d = 0.0;
    double a = 0.0;
    double alpha = 0.0;
};

struct JointInfo
{
    double zero_position = 0.0;
    bool direction_clockwise = false;
};

template <typename T> struct GroupArray
{
    std::array<T, GroupPosition::MaxAxes> value{};
    std::size_t count = 0;
};

using DHParameterArray = GroupArray<DHParameter>;
using JointInfoArray = GroupArray<JointInfo>;
using JogBooleanArray = GroupArray<bool>;

struct GroupKinematicsInfo
{
    bool serial = false;
    std::size_t count = 0;
    std::array<DHParameter, GroupPosition::MaxAxes> dh{};
    std::array<JointInfo, GroupPosition::MaxAxes> joint{};
};

enum class GroupValueSource
{
    commanded,
    actual,
    set,
};

enum class GroupParameter
{
    dynamics_mode,
    transition_reference_point,
};

enum class DynamicsMode
{
    absolute,
    percentage,
};

enum class TransitionReferencePoint
{
    start_point,
    end_point,
};

struct PathDynamics
{
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
};

struct JoggingDynamics
{
    PathDynamics path{};
    std::size_t size = 0;
    std::array<double, GroupPosition::MaxAxes> axis_velocity{};
    std::array<double, GroupPosition::MaxAxes> axis_acceleration{};
    std::array<double, GroupPosition::MaxAxes> axis_deceleration{};
    std::array<double, GroupPosition::MaxAxes> axis_jerk{};
};

struct JogCommandOptions
{
    double velocity_override = 1.0;
    double acceleration_override = 1.0;
    double max_linear_distance = 0.0;
    double max_angular_distance = 0.0;
};

struct ToolData
{
    std::array<double, 6> value{};
};

enum class KinTransformKind
{
    none,
    kinematics,
    pose,
};

struct KinTransformRef
{
    KinTransformKind kind = KinTransformKind::none;
    const kin::Kinematics *kinematics = nullptr;
    const kin::PoseKinematics *pose = nullptr;
};

struct PayloadData
{
    ToolData center{};
    double mass = 0.0;
    double ix = 0.0;
    double iy = 0.0;
    double iz = 0.0;
};

struct RigidBodyDynamic
{
    ToolData center_of_gravity{};
    double mass = 0.0;
    double ix = 0.0;
    double iy = 0.0;
    double iz = 0.0;
};

struct RigidBodyDynamics
{
    std::array<RigidBodyDynamic, GroupPosition::MaxAxes + 1> value{};
    std::size_t count = 0;
};

enum class SelectionSource
{
    active,
    selected,
};

struct GroupSWLimit
{
    double minimum = 0.0;
    double maximum = 0.0;
    bool minimum_enabled = false;
    bool maximum_enabled = false;
};

using GroupSWLimits = GroupArray<GroupSWLimit>;

enum class GroupCommandState
{
    accepted,
    active,
};

enum class GroupWaitState
{
    idle,
    queued,
    stopping,
    active,
    completed,
    aborted,
};

struct GroupMotionState
{
    bool tracking = false;
    bool in_sync = true;
    bool in_position = true;
    bool standstill = true;
    bool constant_velocity = false;
    bool accelerating = false;
    bool decelerating = false;
    std::uint32_t active_command_id = 0;
};

struct GroupCommandInfo
{
    GroupCommandState state = GroupCommandState::accepted;
    std::int64_t elapsed_cycles = 0;
    std::int64_t remaining_cycles = 0;
    double remaining_distance = 0.0;
    double progress = 0.0;
    std::uint16_t info_id = 0;
    std::uint16_t warning_id = 0;
};

// MC_CIRC_MODE: v1 supports only the three-point BORDER construction; CENTER
// and RADIUS report rt::ErrorCode::unsupported (approved circular matrix).
enum class CircMode
{
    border,
    center,
    radius,
};

// MC_CIRC_PATHCHOICE. In BORDER mode the three points already determine the
// arc direction; a conflicting input is an explicit error, never a silent
// reinterpretation (approved circular matrix).
enum class CircPathChoice
{
    clockwise,
    counter_clockwise,
};

enum class GroupPathKind
{
    linear,
    circular,
    cartesian_linear,
};

enum class PathMode
{
    non_periodic,
    periodic,
};

enum class TrackingKind
{
    none,
    dynamic_group,
    conveyor,
    rotary,
};

// MC_TRANSITION_MODE (approved blending matrix): v1 implements None and
// MaxCornerDeviation; StartVelocity/ConstantVelocity/CornerDistance are
// explicit unsupported.
enum class TransitionMode
{
    none,
    start_velocity,
    constant_velocity,
    corner_distance,
    max_corner_deviation,
};

// MC_COORD_SYSTEM (approved coordinate matrix, B1 v1): ACS is the raw axis
// domain (frames never applied); MCS/PCS carry Cartesian semantics on the
// first three coordinates with the v1 identity ACS<->MCS mapping declared;
// WCS/FCS/TCS report explicit unsupported.
enum class CoordSystem
{
    acs,
    mcs,
    wcs,
    pcs,
    fcs,
    tcs,
};

enum class ExecutionMode
{
    immediately,
    queued,
};

// Readback source (approved readback matrix, decision #1/#8): the command
// setpoint domain or the servo feedback domain, through one conversion
// chain.
enum class PositionSource
{
    command,
    actual,
};

// Cartesian-interpolation batch (approved matrix): opt-in in-segment
// per-cycle inverse kinematics. joint keeps the declared KB-037/KB-042
// joint-space interpolation byte-identical.
enum class InterpolationSpace
{
    joint,
    cartesian,
};

// Native coordinated-motion orientation contract. joint_space preserves the
// member-domain interpolation, shortest_path interpolates the TCP rotation on
// the shortest geodesic, and constant holds the TCP orientation captured at
// the deterministic segment start.
enum class OrientationMode
{
    joint_space,
    shortest_path,
    constant,
};

enum class GroupCommandKind
{
    motion,
    direct,
    home,
    halt,
    set_kinematics,
    set_coordinate_transform,
    write_parameter,
    write_sw_limits,
    write_tool_data,
};

struct GroupCommand // NOLINT(clang-analyzer-optin.performance.Padding)
{
    GroupCommandKind kind = GroupCommandKind::motion;
    GroupPosition target{};
    bool relative = false;
    bool direct_semantics = false;
    // Shared-path dynamics. Linear: the scalar path parameter is planned as
    // one jerk-limited 1D profile referenced to the member with the longest
    // travel (the fastest-moving member). Circular: the path parameter is the
    // arc length in the plane of the first two axes, so velocity/limits apply
    // to the in-plane path speed (approved circular matrix).
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    BufferMode buffer_mode = BufferMode::aborting;
    std::uint32_t command_id = 0;

    // Approved coordinate matrix (B1 v1): target/aux are interpreted in this
    // frame and converted to ACS at submit time; the cycle path never sees a
    // frame.
    CoordSystem coord_system = CoordSystem::acs;

    // Cartesian-interpolation batch: joint (default, byte-identical) or
    // cartesian (per-cycle inverse kinematics on the precomputed
    // line/geodesic).
    InterpolationSpace interpolation_space = InterpolationSpace::joint;

    // A4 transition inputs (linear-to-linear geometric blending v1, KB-031).
    TransitionMode transition_mode = TransitionMode::none;
    // Zero selects the planner-derived junction speed. A positive value is an
    // explicit upper bound on the transition node velocity.
    double transition_velocity = 0.0;
    double transition_parameter = 0.0;
    OrientationMode orientation_mode = OrientationMode::joint_space;

    // Circular-only inputs (ignored by submit_linear).
    GroupPathKind path_kind = GroupPathKind::linear;
    CircMode circ_mode = CircMode::border;
    CircPathChoice path_choice = CircPathChoice::counter_clockwise;
    GroupPosition aux{};
    // Independent circular geometry acceptance. Zero disables the additional
    // higher-dimensional via-point residual check.
    double tolerance = 0.0;
    // Internal: the validated arc geometry, resolved by submit_circular from
    // the command's deterministic start point. Not a user input.
    geom::ArcSegment arc{};
    CartesianSegment cart{};
    bool use_default_dynamics = false;
    std::size_t tool_number = 0;
    std::size_t payload_number = 0;
    geom::RigidTransform tool_inverse{};
    bool dynamic_pcs = false;
    KinTransformRef kin_transform{};
    double min_singularity_margin = 0.0;
    double max_joint_step = 0.01;
    GroupParameter parameter = GroupParameter::dynamics_mode;
    double parameter_value = 0.0;
    GroupSWLimits sw_limits{};
    ToolData tool_data{};
};

struct GroupManagementResult
{
    std::uint32_t command_id = 0;
    rt::ErrorCode error = rt::ErrorCode::ok;
};

class AxisGroup
{
public:
    static constexpr std::size_t MaxAxes = GroupPosition::MaxAxes;
    static constexpr std::size_t QueueCapacity = 8;
    static constexpr std::size_t ToolCapacity = 16;
    static constexpr std::size_t PayloadCapacity = 16;
    static constexpr std::size_t RigidBodyCapacity = MaxAxes + 1;
    static constexpr std::size_t SyncPathCapacity = 32;

    explicit AxisGroup(int domain_id = 0)
        : domain_id_(domain_id)
    {
    }

    // Members are non-owning and must remain alive while registered. Detach
    // them on group destruction so an outliving axis never retains owner state.
    ~AxisGroup()
    {
        if(path_sync_slave_ != nullptr &&
           path_sync_slave_->sync_kind_ == SyncKind::group_path) {
            path_sync_slave_->clear_synchronized();
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(axes_[i]->group_owner() == this) {
                axes_[i]->set_group_owner(nullptr);
            }
        }
    }

    AxisGroup(const AxisGroup &) = delete;
    AxisGroup &operator=(const AxisGroup &) = delete;
    AxisGroup(AxisGroup &&) = delete;
    AxisGroup &operator=(AxisGroup &&) = delete;

    GroupStatus status() const
    {
        return status_;
    }

    std::size_t member_count() const
    {
        return axes_.size();
    }

    rt::ErrorCode ungroup_all_axes()
    {
        if(status_ == GroupStatus::moving || status_ == GroupStatus::stopping ||
           status_ == GroupStatus::interrupted) {
            return rt::ErrorCode::precondition_failed;
        }
        abort_motion();
        cancel_tracking();
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_group_owner(nullptr);
        }
        axes_.clear();
        member_idents_.clear();
        status_ = GroupStatus::disabled;
        group_error_id_ = rt::ErrorCode::ok;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode set_group_power(bool enabled)
    {
        if(axes_.empty()) return rt::ErrorCode::precondition_failed;
        if(enabled) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                if(axes_[i]->status() == AxisStatus::errorstop) {
                    set_group_error(rt::ErrorCode::precondition_failed);
                    return rt::ErrorCode::precondition_failed;
                }
            }
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const rt::ErrorCode result = axes_[i]->set_power(enabled);
            if(result != rt::ErrorCode::ok) {
                set_group_error(result);
                return result;
            }
        }
        if(!enabled && status_ != GroupStatus::disabled) {
            abort_motion();
            set_group_error(rt::ErrorCode::precondition_failed);
        }
        return rt::ErrorCode::ok;
    }

    bool group_powered() const
    {
        if(axes_.empty()) return false;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->powered()) return false;
        }
        return true;
    }

    bool connector_active() const
    {
        return connector_.active();
    }

    double connector_tube_radius() const
    {
        return connector_.tube_radius();
    }

    double path_odometer() const
    {
        return path_odometer_;
    }

    rt::Result<std::uint32_t> sync_axis_to_group(AxisModel &slave,
                                                 double ratio_numerator,
                                                 double ratio_denominator,
                                                 double acceleration,
                                                 double deceleration,
                                                 double jerk,
                                                 BufferMode buffer_mode)
    {
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           slave.group_owner() != nullptr || !slave.powered() ||
           !std::isfinite(ratio_numerator) || !std::isfinite(ratio_denominator) ||
           ratio_denominator == 0.0 || !std::isfinite(acceleration) ||
           !std::isfinite(deceleration) || !std::isfinite(jerk) || acceleration < 0.0 ||
           deceleration < 0.0 || jerk < 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(path_sync_slave_ != nullptr && path_sync_slave_ != &slave &&
           path_sync_slave_->sync_kind_ == SyncKind::group_path) {
            path_sync_slave_->clear_synchronized();
        }
        const rt::Result<std::uint32_t> begun =
            slave.begin_sync(SyncKind::group_path, buffer_mode);
        if(!begun) return begun;
        const bool position_locked = acceleration == 0.0 && deceleration == 0.0 && jerk == 0.0;
        slave.sync_phase_ = position_locked ? SyncPhase::engaged : SyncPhase::approaching;
        path_sync_slave_ = &slave;
        path_sync_id_ = begun.value();
        path_sync_origin_ = path_odometer_;
        path_sync_slave_origin_ = slave.snapshot().command_position;
        path_sync_ratio_ = ratio_numerator / ratio_denominator;
        path_sync_acceleration_ = acceleration;
        path_sync_deceleration_ = deceleration;
        path_sync_jerk_ = jerk;
        path_sync_velocity_ = slave.snapshot().command_velocity;
        path_sync_acceleration_state_ = slave.snapshot().command_acceleration;
        path_sync_in_sync_ = position_locked;
        return begun;
    }

    rt::Result<std::uint32_t> sync_group_to_axis(
        AxisModel &master,
        const GroupPosition *waypoints,
        std::size_t count,
        PathMode mode,
        const std::array<int, MaxAxes> &tuc_numerator,
        const std::array<int, MaxAxes> &tuc_denominator,
        CoordSystem coord_system,
        BufferMode buffer_mode)
    {
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           waypoints == nullptr || count < 2 || count > SyncPathCapacity ||
           !master.powered() || master.group_owner() == this ||
           coord_system != CoordSystem::acs || buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t axis_index = 0; axis_index < axes_.size(); ++axis_index) {
            if(tuc_numerator[axis_index] == 0 || tuc_denominator[axis_index] == 0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
        }
        group_sync_cumulative_[0] = 0.0;
        for(std::size_t point = 0; point < count; ++point) {
            if(waypoints[point].size != axes_.size() || !finite(waypoints[point])) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            group_sync_waypoints_[point] = waypoints[point];
            if(point == 0) continue;
            double squared_length = 0.0;
            for(std::size_t axis_index = 0; axis_index < axes_.size(); ++axis_index) {
                const double conversion = static_cast<double>(tuc_numerator[axis_index]) /
                                          static_cast<double>(tuc_denominator[axis_index]);
                const double delta = (waypoints[point].value[axis_index] -
                                      waypoints[point - 1].value[axis_index]) *
                                     conversion;
                squared_length += delta * delta;
            }
            const double length = std::sqrt(squared_length);
            if(length <= 0.0 || !std::isfinite(length)) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            group_sync_cumulative_[point] = group_sync_cumulative_[point - 1] + length;
        }
        abort_motion();
        group_sync_master_ = &master;
        group_sync_master_origin_ = master.snapshot().command_position;
        group_sync_count_ = count;
        group_sync_mode_ = mode;
        group_sync_id_ = next_command_id_++;
        group_sync_active_ = true;
        group_sync_previous_ = group_sync_waypoints_[0];
        status_ = GroupStatus::moving;
        return rt::Result<std::uint32_t>::success(group_sync_id_);
    }

    bool group_to_axis_sync_active(std::uint32_t command_id) const
    {
        return group_sync_active_ && command_id != 0 && command_id == group_sync_id_;
    }

    bool group_to_axis_sync_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_group_sync_id_;
    }

    rt::Result<std::uint32_t> set_dynamic_coord_transform(
        AxisGroup &master,
        const ToolData &transform,
        CoordSystem coord_system,
        BufferMode buffer_mode)
    {
        if(&master == this || !valid_tracking_pose(transform)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::Result<std::uint32_t> begun =
            begin_tracking(TrackingKind::dynamic_group, coord_system, buffer_mode);
        if(!begun) return begun;
        tracking_master_group_ = &master;
        tracking_transform_ = transform;
        return begun;
    }

    rt::Result<std::uint32_t> track_conveyor(AxisModel &conveyor,
                                             const ToolData &origin,
                                             const ToolData &initial_object,
                                             CoordSystem coord_system,
                                             BufferMode buffer_mode)
    {
        if(!conveyor.powered() || !valid_tracking_pose(origin) ||
           !valid_tracking_pose(initial_object)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::Result<std::uint32_t> begun =
            begin_tracking(TrackingKind::conveyor, coord_system, buffer_mode);
        if(!begun) return begun;
        tracking_master_axis_ = &conveyor;
        tracking_master_origin_ = conveyor.snapshot().actual_position;
        tracking_origin_ = origin;
        tracking_transform_ = initial_object;
        return begun;
    }

    rt::Result<std::uint32_t> track_rotary_table(AxisModel &rotary,
                                                 const ToolData &origin,
                                                 const ToolData &initial_object,
                                                 CoordSystem coord_system,
                                                 BufferMode buffer_mode)
    {
        if(!rotary.powered() || !valid_tracking_pose(origin) ||
           !valid_tracking_pose(initial_object)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::Result<std::uint32_t> begun =
            begin_tracking(TrackingKind::rotary, coord_system, buffer_mode);
        if(!begun) return begun;
        tracking_master_axis_ = &rotary;
        tracking_master_origin_ = rotary.snapshot().actual_position;
        tracking_origin_ = origin;
        tracking_transform_ = initial_object;
        return begun;
    }

    bool tracking_command_busy(std::uint32_t command_id) const
    {
        return tracking_kind_ != TrackingKind::none && command_id != 0 &&
               command_id == tracking_command_id_;
    }

    bool tracking_command_active(std::uint32_t command_id) const
    {
        return tracking_command_busy(command_id) && tracking_motion_seen_;
    }

    bool tracking_command_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_tracking_id_;
    }

    rt::ErrorCode tracking_command_error(std::uint32_t command_id) const
    {
        return tracking_command_busy(command_id) ? tracking_error_
                                                 : rt::ErrorCode::ok;
    }

    const AxisModel *member(std::size_t index) const
    {
        return index < axes_.size() ? axes_[index] : nullptr;
    }

    std::size_t member_index(const AxisModel &axis) const
    {
        return find(axis);
    }

    rt::ErrorCode bind_kinematics_info(const GroupKinematicsInfo &info)
    {
        if(status_ != GroupStatus::disabled || kinematics_info_frozen_ ||
           !info.serial || info.count == 0 || info.count != axes_.size()) {
            return rt::ErrorCode::precondition_failed;
        }
        for(std::size_t i = 0; i < info.count; ++i) {
            const DHParameter &dh = info.dh[i];
            if(!std::isfinite(dh.theta) || !std::isfinite(dh.d) ||
               !std::isfinite(dh.a) || !std::isfinite(dh.alpha) ||
               !std::isfinite(info.joint[i].zero_position)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        kinematics_info_ = info;
        kinematics_info_bound_ = true;
        return rt::ErrorCode::ok;
    }

    rt::Result<GroupKinematicsInfo> kinematics_info() const
    {
        if(!kinematics_info_bound_ || !kinematics_info_.serial ||
           kinematics_info_.count != axes_.size()) {
            return rt::Result<GroupKinematicsInfo>::failure(
                rt::ErrorCode::precondition_failed);
        }
        return rt::Result<GroupKinematicsInfo>::success(kinematics_info_);
    }

    rt::Result<double> read_group_parameter(GroupParameter parameter) const
    {
        if(parameter == GroupParameter::dynamics_mode) {
            return rt::Result<double>::success(static_cast<double>(dynamics_mode_));
        }
        if(parameter == GroupParameter::transition_reference_point) {
            return rt::Result<double>::success(
                static_cast<double>(transition_reference_point_));
        }
        return rt::Result<double>::failure(rt::ErrorCode::unsupported);
    }

    rt::ErrorCode write_group_parameter(GroupParameter parameter, double value)
    {
        if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        if(parameter == GroupParameter::dynamics_mode) {
            if(value == static_cast<double>(DynamicsMode::absolute)) {
                dynamics_mode_ = DynamicsMode::absolute;
                return rt::ErrorCode::ok;
            }
            if(value == static_cast<double>(DynamicsMode::percentage)) {
                dynamics_mode_ = DynamicsMode::percentage;
                return rt::ErrorCode::ok;
            }
            return rt::ErrorCode::invalid_argument;
        }
        if(parameter == GroupParameter::transition_reference_point) {
            if(value == static_cast<double>(TransitionReferencePoint::end_point)) {
                transition_reference_point_ = TransitionReferencePoint::end_point;
                return rt::ErrorCode::ok;
            }
            if(value == static_cast<double>(TransitionReferencePoint::start_point)) {
                return rt::ErrorCode::unsupported;
            }
            return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::unsupported;
    }

    rt::Result<std::uint32_t> submit_group_parameter(GroupParameter parameter,
                                                     double value,
                                                     ExecutionMode mode)
    {
        if(mode == ExecutionMode::immediately) {
            return complete_immediate(write_group_parameter(parameter, value));
        }
        const rt::ErrorCode valid = validate_group_parameter(parameter, value);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        GroupCommand command{};
        command.kind = GroupCommandKind::write_parameter;
        command.parameter = parameter;
        command.parameter_value = value;
        return enqueue_management(command, mode);
    }

    const PathDynamics &reference_dynamics() const { return reference_dynamics_; }
    const PathDynamics &default_dynamics() const { return default_dynamics_; }
    const JoggingDynamics &jogging_dynamics() const { return jogging_dynamics_; }

    rt::ErrorCode write_tool_data(std::size_t number, const ToolData &data)
    {
        if(number == 0) return rt::ErrorCode::precondition_failed;
        if(number >= ToolCapacity) return rt::ErrorCode::out_of_range;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        for(double value : data.value) {
            if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        }
        tools_[number] = data;
        tool_defined_[number] = true;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_tool_data(std::size_t number,
                                              const ToolData &data,
                                              ExecutionMode mode)
    {
        if(mode == ExecutionMode::immediately) {
            return complete_immediate(write_tool_data(number, data));
        }
        const rt::ErrorCode valid = validate_tool_data(number, data);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        GroupCommand command{};
        command.kind = GroupCommandKind::write_tool_data;
        command.tool_number = number;
        command.tool_data = data;
        return enqueue_management(command, mode);
    }

    rt::Result<ToolData> read_tool_data(std::size_t number) const
    {
        if(number >= ToolCapacity || !tool_defined_[number]) {
            return rt::Result<ToolData>::failure(rt::ErrorCode::out_of_range);
        }
        return rt::Result<ToolData>::success(tools_[number]);
    }

    rt::ErrorCode select_tool(std::size_t number)
    {
        if(status_ != GroupStatus::disabled && status_ != GroupStatus::standby &&
           status_ != GroupStatus::moving) {
            return rt::ErrorCode::precondition_failed;
        }
        if(number >= ToolCapacity || !tool_defined_[number]) {
            return rt::ErrorCode::out_of_range;
        }
        selected_tool_ = number;
        numbered_tool_mode_ = true;
        apply_selected_tool();
        return rt::ErrorCode::ok;
    }

    std::size_t read_tool(SelectionSource source) const
    {
        return source == SelectionSource::active &&
                       (status_ == GroupStatus::moving || status_ == GroupStatus::stopping ||
                        status_ == GroupStatus::interrupted)
                   ? active_tool_
                   : selected_tool_;
    }

    rt::ErrorCode write_payload_data(std::size_t number, const PayloadData &data)
    {
        if(number == 0) return rt::ErrorCode::precondition_failed;
        if(number >= PayloadCapacity) return rt::ErrorCode::out_of_range;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        for(double value : data.center.value) {
            if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        }
        if(!std::isfinite(data.mass) || !std::isfinite(data.ix) ||
           !std::isfinite(data.iy) || !std::isfinite(data.iz) || data.mass < 0.0 ||
           data.ix < 0.0 || data.iy < 0.0 || data.iz < 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        payloads_[number] = data;
        payload_defined_[number] = true;
        return rt::ErrorCode::ok;
    }

    rt::Result<PayloadData> read_payload_data(std::size_t number) const
    {
        if(number >= PayloadCapacity || !payload_defined_[number]) {
            return rt::Result<PayloadData>::failure(rt::ErrorCode::out_of_range);
        }
        return rt::Result<PayloadData>::success(payloads_[number]);
    }

    rt::ErrorCode write_rigid_body_dynamics(const RigidBodyDynamics &data)
    {
        if(data.count == 0) return rt::ErrorCode::invalid_argument;
        if(data.count > RigidBodyCapacity) return rt::ErrorCode::out_of_range;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        for(std::size_t index = 0; index < data.count; ++index) {
            const RigidBodyDynamic &body = data.value[index];
            for(double value : body.center_of_gravity.value) {
                if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
            }
            if(!std::isfinite(body.mass) || !std::isfinite(body.ix) ||
               !std::isfinite(body.iy) || !std::isfinite(body.iz) ||
               body.mass < 0.0 || body.ix < 0.0 || body.iy < 0.0 || body.iz < 0.0) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        rigid_body_dynamics_ = data;
        rigid_body_dynamics_defined_ = true;
        return rt::ErrorCode::ok;
    }

    rt::Result<RigidBodyDynamics> rigid_body_dynamics() const
    {
        if(!rigid_body_dynamics_defined_) {
            return rt::Result<RigidBodyDynamics>::failure(
                rt::ErrorCode::precondition_failed);
        }
        return rt::Result<RigidBodyDynamics>::success(rigid_body_dynamics_);
    }

    rt::ErrorCode select_payload(std::size_t number)
    {
        if(status_ != GroupStatus::disabled && status_ != GroupStatus::standby &&
           status_ != GroupStatus::moving) {
            return rt::ErrorCode::precondition_failed;
        }
        if(number >= PayloadCapacity || !payload_defined_[number]) {
            return rt::ErrorCode::out_of_range;
        }
        selected_payload_ = number;
        return rt::ErrorCode::ok;
    }

    std::size_t read_payload(SelectionSource source) const
    {
        return source == SelectionSource::active &&
                       (status_ == GroupStatus::moving || status_ == GroupStatus::stopping ||
                        status_ == GroupStatus::interrupted)
                   ? active_payload_
                   : selected_payload_;
    }

    rt::Result<std::uint32_t> begin_jog(CoordSystem coord_system,
                                        const GroupPosition &direction,
                                        const JogCommandOptions &options = {})
    {
        const rt::ErrorCode valid = validate_jog(coord_system, direction, options);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        if(status_ != GroupStatus::standby && status_ != GroupStatus::moving &&
           status_ != GroupStatus::stopping) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        if(jog_active_) {
            last_aborted_jog_id_ = jog_command_id_;
        }
        abort_motion();
        jog_command_id_ = next_command_id_++;
        jog_coord_system_ = coord_system;
        jog_direction_ = direction;
        jog_options_ = options;
        jog_releasing_ = false;
        jog_error_ = rt::ErrorCode::ok;
        jog_stop_deceleration_ = 0.0;
        jog_stop_jerk_ = 0.0;
        jog_active_ = true;
        snapshot_selections();
        snapshot_active_tool_transform();
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            jog_position_[i] = axes_[i]->snapshot().command_position;
            jog_velocity_[i] = axes_[i]->snapshot().command_velocity;
            jog_acceleration_[i] = axes_[i]->snapshot().command_acceleration;
        }
        if(coord_system != CoordSystem::acs) {
            GroupPosition current{};
            const rt::ErrorCode read =
                read_cartesian(coord_system, PositionSource::command, current);
            if(read != rt::ErrorCode::ok) {
                jog_active_ = false;
                return rt::Result<std::uint32_t>::failure(read);
            }
            jog_cartesian_position_ = current;
            for(std::size_t i = 0; i < 6; ++i) {
                jog_cartesian_start_[i] = current.value[i];
            }
            pose_frames_.jog_tool_offset_ = pose_frames_.tool_offset_;
            pose_frames_.jog_pose_tool_inverse_ = pose_frames_.pose_tool_inverse_;
            jog_cart_velocity_.fill(0.0);
            jog_cart_acceleration_.fill(0.0);
        }
        status_ = any_direction(direction) ? GroupStatus::moving
                                           : GroupStatus::standby;
        return rt::Result<std::uint32_t>::success(jog_command_id_);
    }

    rt::ErrorCode update_jog(std::uint32_t command_id, const GroupPosition &direction,
                             const JogCommandOptions &options = {})
    {
        if(!jog_active_ || command_id == 0 || command_id != jog_command_id_) {
            return rt::ErrorCode::precondition_failed;
        }
        if(jog_error_ != rt::ErrorCode::ok) return jog_error_;
        const rt::ErrorCode valid = validate_jog(jog_coord_system_, direction, options);
        if(valid != rt::ErrorCode::ok) return valid;
        jog_direction_ = direction;
        jog_options_ = options;
        jog_releasing_ = false;
        if(any_direction(direction)) status_ = GroupStatus::moving;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode release_jog(std::uint32_t command_id)
    {
        if(!jog_active_ || command_id == 0 || command_id != jog_command_id_) {
            return rt::ErrorCode::precondition_failed;
        }
        jog_direction_ = {};
        jog_direction_.size = jog_coord_system_ == CoordSystem::acs ? axes_.size() : 6;
        jog_releasing_ = true;
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
    }

    bool jog_command_active(std::uint32_t command_id) const
    {
        return jog_active_ && command_id != 0 && command_id == jog_command_id_;
    }

    bool jog_command_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_jog_id_;
    }

    rt::ErrorCode jog_error() const { return jog_error_; }

    rt::ErrorCode write_reference_dynamics(const PathDynamics &update)
    {
        return update_path_dynamics(reference_dynamics_, update);
    }

    rt::ErrorCode write_default_dynamics(const PathDynamics &update)
    {
        return update_path_dynamics(default_dynamics_, update);
    }

    rt::ErrorCode write_jogging_dynamics(const JoggingDynamics &update)
    {
        if(update.size != axes_.size()) return rt::ErrorCode::invalid_argument;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        JoggingDynamics next = jogging_dynamics_;
        const rt::ErrorCode path = apply_path_update(next.path, update.path);
        if(path != rt::ErrorCode::ok) return path;
        next.size = update.size;
        for(std::size_t i = 0; i < update.size; ++i) {
            const double values[4] = {update.axis_velocity[i],
                                      update.axis_acceleration[i],
                                      update.axis_deceleration[i],
                                      update.axis_jerk[i]};
            for(double value : values) {
                if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
            }
            if(update.axis_velocity[i] > 0.0) next.axis_velocity[i] = update.axis_velocity[i];
            if(update.axis_acceleration[i] > 0.0)
                next.axis_acceleration[i] = update.axis_acceleration[i];
            if(update.axis_deceleration[i] > 0.0)
                next.axis_deceleration[i] = update.axis_deceleration[i];
            if(update.axis_jerk[i] > 0.0) next.axis_jerk[i] = update.axis_jerk[i];
        }
        jogging_dynamics_ = next;
        return rt::ErrorCode::ok;
    }

    rt::Result<GroupSWLimits> group_sw_limits() const
    {
        GroupSWLimits result{};
        result.count = axes_.size();
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const MotionLimits &limits = axes_[i]->limits_;
            result.value[i] = {limits.min_position, limits.max_position,
                               limits.min_position_enabled,
                               limits.max_position_enabled};
        }
        return rt::Result<GroupSWLimits>::success(result);
    }

    rt::ErrorCode write_group_sw_limits(const GroupSWLimits &limits)
    {
        if(limits.count != axes_.size()) return rt::ErrorCode::invalid_argument;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        for(std::size_t i = 0; i < limits.count; ++i) {
            const GroupSWLimit &entry = limits.value[i];
            if(!std::isfinite(entry.minimum) || !std::isfinite(entry.maximum) ||
               entry.minimum > entry.maximum) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(std::size_t i = 0; i < limits.count; ++i) {
            MotionLimits next = axes_[i]->limits_;
            next.min_position = limits.value[i].minimum;
            next.max_position = limits.value[i].maximum;
            next.min_position_enabled = limits.value[i].minimum_enabled;
            next.max_position_enabled = limits.value[i].maximum_enabled;
            axes_[i]->limits_ = next;
        }
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_group_sw_limits(const GroupSWLimits &limits,
                                                     ExecutionMode mode)
    {
        if(mode == ExecutionMode::immediately) {
            return complete_immediate(write_group_sw_limits(limits));
        }
        const rt::ErrorCode valid = validate_group_sw_limits(limits);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        GroupCommand command{};
        command.kind = GroupCommandKind::write_sw_limits;
        command.sw_limits = limits;
        return enqueue_management(command, mode);
    }

    bool contains(const AxisModel &axis) const
    {
        return find(axis) < axes_.size();
    }

    rt::ErrorCode add_axis(AxisModel &axis, IdentInGroup ident)
    {
        if(status_ != GroupStatus::disabled || axis.domain_id() != domain_id_) {
            return rt::ErrorCode::invalid_argument;
        }
        if(axis.group_owner() == this) {
            return rt::ErrorCode::invalid_argument;
        }
        if(axis.group_owner() != nullptr) {
            return rt::ErrorCode::out_of_range;
        }
        if(find(ident) < member_idents_.size()) {
            return rt::ErrorCode::invalid_argument;
        }

        const rt::ErrorCode pushed = axes_.push_back(&axis);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
        const rt::ErrorCode ident_pushed = member_idents_.push_back(ident);
        if(ident_pushed != rt::ErrorCode::ok) {
            axes_.pop_back();
            return ident_pushed;
        }
        axis.set_group_owner(this, &status_);
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode add_axis(AxisModel &axis)
    {
        IdentInGroup ident{};
        while(find(ident) < member_idents_.size()) {
            ++ident.index;
        }
        return add_axis(axis, ident);
    }

    rt::ErrorCode remove_axis(IdentInGroup ident)
    {
        if(status_ != GroupStatus::disabled) {
            return rt::ErrorCode::invalid_argument;
        }
        const std::size_t index = find(ident);
        if(index == member_idents_.size()) {
            return rt::ErrorCode::ok;
        }
        AxisModel *axis = axes_[index];
        for(std::size_t i = index + 1; i < axes_.size(); ++i) {
            axes_[i - 1] = axes_[i];
            member_idents_[i - 1] = member_idents_[i];
        }
        axes_.pop_back();
        member_idents_.pop_back();
        axis->set_group_owner(nullptr);
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode remove_axis(AxisModel &axis)
    {
        if(status_ != GroupStatus::disabled) {
            return rt::ErrorCode::invalid_argument;
        }
        const std::size_t index = find(axis);
        if(index == axes_.size()) {
            return rt::ErrorCode::out_of_range;
        }
        for(std::size_t i = index + 1; i < axes_.size(); ++i) {
            axes_[i - 1] = axes_[i];
            member_idents_[i - 1] = member_idents_[i];
        }
        axes_.pop_back();
        member_idents_.pop_back();
        axis.set_group_owner(nullptr);
        return rt::ErrorCode::ok;
    }

    AxisModel *member(IdentInGroup ident)
    {
        const std::size_t index = find(ident);
        return index < axes_.size() ? axes_[index] : nullptr;
    }

    const AxisModel *member(IdentInGroup ident) const
    {
        const std::size_t index = find(ident);
        return index < axes_.size() ? axes_[index] : nullptr;
    }

    IdentInGroup member_ident(const AxisModel &axis) const
    {
        const std::size_t index = find(axis);
        return index < member_idents_.size() ? member_idents_[index]
                                             : IdentInGroup{static_cast<std::size_t>(-1)};
    }

    rt::ErrorCode enable()
    {
        if(status_ != GroupStatus::disabled || axes_.empty()) {
            return rt::ErrorCode::invalid_argument;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->powered()) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        status_ = GroupStatus::standby;
        kinematics_info_frozen_ = true;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode disable()
    {
        if(direct_path_.direct_active_) {
            direct_path_.last_aborted_direct_id_ = direct_path_.direct_command_id_;
            abort_direct_members();
        }
        abort_motion();
        if(path_sync_slave_ != nullptr &&
           path_sync_slave_->sync_kind_ == SyncKind::group_path) {
            path_sync_slave_->clear_synchronized();
        }
        path_sync_slave_ = nullptr;
        path_sync_id_ = 0;
        cancel_tracking();
        status_ = GroupStatus::disabled;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
        return rt::ErrorCode::ok;
    }

    // MC_GroupStop: controlled deceleration along the original path. The halt
    // profile re-plans the path parameter from its current sampled state to
    // the minimal braking point (KB-027); a live circular takeover also
    // re-plans its member residuals instead of dropping the tolerance tube.
    rt::ErrorCode stop(double deceleration = 1.0, double jerk = 1.0)
    {
        if(cartesian_.window_active_ && !cartesian_.window_stopping_) {
            return cart_window_stop(deceleration, jerk);
        }
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           !std::isfinite(deceleration) || deceleration <= 0.0 || !std::isfinite(jerk) ||
           jerk <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(group_sync_active_) {
            abort_motion();
            status_ = GroupStatus::standby;
            return rt::ErrorCode::ok;
        }
        if(status_ != GroupStatus::moving) {
            return rt::ErrorCode::ok;
        }
        if(jog_active_) {
            jog_stop_deceleration_ = deceleration;
            jog_stop_jerk_ = jerk;
            last_aborted_jog_id_ = jog_command_id_;
            return release_jog(jog_command_id_);
        }
        if(direct_path_.direct_active_) {
            return stop_direct_members(deceleration, jerk);
        }
        queue_.clear();
        if(joint_window_.active_) {
            // Controlled stop along the committed window geometry (KB-032):
            // one halt profile over the composite arc length; not-yet-started
            // commands are cleared (they never execute), the geometry is kept
            // for braking. Mirrors the KB-027 clamp trick: the halt target may
            // lie past the path end, sampling clamps at the terminal point.
            if(joint_window_.stopping_) {
                return rt::ErrorCode::ok;
            }
            double s_live = 0.0;
            double v_live = 0.0;
            double a_live = 0.0;
            window_live_state(s_live, v_live, a_live);
            if(v_live <= 0.0) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                return rt::ErrorCode::ok;
            }
            const WindowSegment &seg = joint_window_.segments_[joint_window_.index_];
            const otg::Limits1D halt_limits{seg.limits.max_velocity,
                                            seg.limits.max_acceleration, deceleration, jerk};
            double brake_velocity = v_live;
            double brake_shift = 0.0;
            if(a_live != 0.0) {
                const double zero_cycles = std::ceil(std::fabs(a_live) / jerk);
                brake_velocity += 0.5 * a_live * zero_cycles;
                brake_shift += v_live * zero_cycles + a_live * zero_cycles * zero_cycles / 3.0;
            }
            if(brake_velocity < 0.0) {
                brake_velocity = 0.0;
            }
            const double stop_position =
                brake_shift +
                otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
            const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
                {0.0, v_live, a_live}, {stop_position, 0.0, 0.0}, halt_limits);
            if(!halt) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                return rt::ErrorCode::ok;
            }
            joint_window_.stopping_ = true;
            joint_window_.stop_profile_ = halt.value();
            joint_window_.stop_origin_ = s_live;
            joint_window_.tick_ = 0;
            status_ = GroupStatus::stopping;
            return rt::ErrorCode::ok;
        }
        if(!active_ || active_path_length_ <= 0.0) {
            abort_motion();
            status_ = GroupStatus::standby;
            return rt::ErrorCode::ok;
        }

        const otg::State1D state =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        if(connector_.vector_mode()) {
            const otg::Limits1D stop_limits{
                active_command_.velocity, active_command_.acceleration,
                deceleration, jerk};
            std::array<double, MaxAxes> output_velocity{};
            std::array<double, MaxAxes> output_acceleration{};
            current_member_output_state(output_velocity, output_acceleration);
            otg::Profile1D halt_profile{};
            std::int64_t halt_duration = 0;
            const rt::ErrorCode planned =
                active_kind_ == GroupPathKind::circular
                    ? connector_.plan_circular_stop(
                          stop_limits, active_path_length_, axes_.size(),
                          active_start_, active_finish_, active_arc_,
                          active_profile_, active_tick_, output_velocity,
                          output_acceleration, halt_profile, halt_duration)
                    : connector_.plan_linear_vector_stop(
                          stop_limits, active_path_length_, axes_.size(),
                          active_start_, active_finish_, active_profile_,
                          active_tick_, output_velocity, output_acceleration,
                          halt_profile, halt_duration);
            if(planned == rt::ErrorCode::ok) {
                active_profile_ = halt_profile;
                active_tick_ = 0;
                active_duration_ = halt_duration;
                status_ = GroupStatus::stopping;
                return rt::ErrorCode::ok;
            }
            // A finite path, continuity and the requested stop envelope cannot
            // all be preserved when the remaining segment is too short. Freeze at
            // the last commanded point and report the planning error instead
            // of snapping the residual back to the base arc.
            abort_motion();
            status_ = GroupStatus::standby;
            return planned;
        }
        if(state.velocity <= 0.0) {
            abort_motion();
            status_ = GroupStatus::standby;
            return rt::ErrorCode::ok;
        }

        // Minimal braking distance: fold the acceleration-zeroing ramp, then
        // the jerk-limited ramp to rest (same primitives as the planner). The
        // acceleration bound stays the original command's so the current
        // profile state is always inside the halt envelope.
        const otg::Limits1D halt_limits{active_command_.velocity,
                                        active_command_.acceleration, deceleration, jerk};
        double brake_velocity = state.velocity;
        double brake_shift = 0.0;
        if(state.acceleration != 0.0) {
            const double zero_cycles = std::ceil(std::fabs(state.acceleration) / jerk);
            brake_velocity += 0.5 * state.acceleration * zero_cycles;
            brake_shift += state.velocity * zero_cycles +
                           state.acceleration * zero_cycles * zero_cycles / 3.0;
        }
        if(brake_velocity < 0.0) {
            brake_velocity = 0.0;
        }
        const double stop_position = state.position + brake_shift +
                                     otg::detail::ramp_between(brake_velocity, 0.0, halt_limits)
                                         .distance;

        const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
            state, {stop_position, 0.0, 0.0}, halt_limits);
        if(!halt) {
            // Fall back to the immediate stop rather than continuing motion.
            abort_motion();
            status_ = GroupStatus::standby;
            return rt::ErrorCode::ok;
        }
        active_profile_ = halt.value();
        active_tick_ = 0;
        active_duration_ = active_profile_.duration_cycles();
        connector_.deactivate();
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> halt(double deceleration = 1.0,
                                   double jerk = 1.0,
                                   BufferMode buffer_mode = BufferMode::aborting)
    {
        if(buffer_mode != BufferMode::aborting && buffer_mode != BufferMode::buffered) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(!std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0 ||
           status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(buffer_mode == BufferMode::buffered) {
            GroupCommand command{};
            command.kind = GroupCommandKind::halt;
            command.deceleration = deceleration;
            command.jerk = jerk;
            command.buffer_mode = buffer_mode;
            command.command_id = next_command_id_++;
            const rt::ErrorCode queued = queue_.push_back(command);
            if(queued != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(queued);
            }
            halt_command_id_ = command.command_id;
            halt_command_live_ = true;
            return rt::Result<std::uint32_t>::success(command.command_id);
        }
        abort_wait();
        abort_queued_management();
        if(halt_command_live_) {
            if(halt_command_done(halt_command_id_)) {
                last_completed_halt_id_ = halt_command_id_;
            } else {
                last_aborted_halt_id_ = halt_command_id_;
            }
        }
        const rt::ErrorCode result = stop(deceleration, jerk);
        if(result != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(result);
        }
        halt_command_id_ = next_command_id_++;
        halt_command_live_ = true;
        return rt::Result<std::uint32_t>::success(halt_command_id_);
    }

    bool halt_command_done(std::uint32_t command_id) const
    {
        return command_id != 0 &&
               (command_id == last_completed_halt_id_ ||
                (halt_command_live_ && command_id == halt_command_id_ &&
                 status_ == GroupStatus::standby));
    }

    bool halt_command_active(std::uint32_t command_id) const
    {
        return halt_command_live_ && command_id == halt_command_id_ &&
               status_ != GroupStatus::standby && !command_queued(command_id);
    }

    bool halt_command_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_halt_id_;
    }

    rt::ErrorCode set_task_cycle_period_ns(std::int64_t period_ns)
    {
        if(period_ns <= 0) return rt::ErrorCode::invalid_argument;
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        task_cycle_period_ns_ = period_ns;
        return rt::ErrorCode::ok;
    }

    std::int64_t task_cycle_period_ns() const { return task_cycle_period_ns_; }

    rt::Result<std::uint32_t> submit_wait(std::int64_t duration_ns,
                                         BufferMode buffer_mode)
    {
        if(duration_ns <= 0 || status_ == GroupStatus::disabled ||
           status_ == GroupStatus::errorstop) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(buffer_mode != BufferMode::aborting && buffer_mode != BufferMode::buffered) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(wait_state_ == GroupWaitState::queued || wait_state_ == GroupWaitState::stopping ||
           wait_state_ == GroupWaitState::active) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        if(buffer_mode == BufferMode::buffered && !queue_.empty()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        wait_command_id_ = next_command_id_++;
        wait_duration_cycles_ = duration_ns / task_cycle_period_ns_;
        if(duration_ns % task_cycle_period_ns_ != 0) ++wait_duration_cycles_;
        wait_elapsed_cycles_ = 0;
        if(buffer_mode == BufferMode::aborting) {
            const rt::ErrorCode stopped = stop();
            if(stopped != rt::ErrorCode::ok) {
                wait_command_id_ = 0;
                return rt::Result<std::uint32_t>::failure(stopped);
            }
            wait_state_ = status_ == GroupStatus::stopping ? GroupWaitState::stopping
                                                            : GroupWaitState::active;
        } else {
            wait_state_ = status_ == GroupStatus::standby ? GroupWaitState::active
                                                          : GroupWaitState::queued;
        }
        return rt::Result<std::uint32_t>::success(wait_command_id_);
    }

    bool wait_command_done(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == wait_command_id_ &&
               wait_state_ == GroupWaitState::completed;
    }

    bool wait_command_active(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == wait_command_id_ &&
               wait_state_ == GroupWaitState::active;
    }

    bool wait_command_busy(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == wait_command_id_ &&
               (wait_state_ == GroupWaitState::queued ||
                wait_state_ == GroupWaitState::stopping ||
                wait_state_ == GroupWaitState::active);
    }

    bool wait_command_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_wait_id_;
    }

    // MC_GroupInterrupt: controlled deceleration preserving active/queue/window
    // state and the pause point. Transitions moving→stopping→interrupted.
    rt::ErrorCode interrupt(double deceleration = 1.0, double jerk = 1.0)
    {
        if(status_ != GroupStatus::moving || !std::isfinite(deceleration) ||
           deceleration <= 0.0 || !std::isfinite(jerk) || jerk <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(direct_path_.direct_active_) {
            return rt::ErrorCode::unsupported;
        }
        if(cartesian_.window_active_ || cartesian_.window_stopping_) {
            return rt::ErrorCode::unsupported;
        }
        if(connector_.active()) {
            return rt::ErrorCode::unsupported;
        }
        if(joint_window_.active_) {
            if(joint_window_.stopping_) {
                return rt::ErrorCode::ok;
            }
            double s_live = 0.0;
            double v_live = 0.0;
            double a_live = 0.0;
            window_live_state(s_live, v_live, a_live);
            if(v_live <= 0.0) {
                interrupted_window_ = true;
                interrupted_plain_ = false;
                status_ = GroupStatus::interrupted;
                return rt::ErrorCode::ok;
            }
            const WindowSegment &seg = joint_window_.segments_[joint_window_.index_];
            const otg::Limits1D halt_limits{seg.limits.max_velocity,
                                            seg.limits.max_acceleration, deceleration, jerk};
            double brake_velocity = v_live;
            double brake_shift = 0.0;
            if(a_live != 0.0) {
                const double zero_cycles = std::ceil(std::fabs(a_live) / jerk);
                brake_velocity += 0.5 * a_live * zero_cycles;
                brake_shift += v_live * zero_cycles + a_live * zero_cycles * zero_cycles / 3.0;
            }
            if(brake_velocity < 0.0) {
                brake_velocity = 0.0;
            }
            const double stop_position =
                brake_shift +
                otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
            const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
                {0.0, v_live, a_live}, {stop_position, 0.0, 0.0}, halt_limits);
            if(!halt) {
                interrupted_window_ = true;
                interrupted_plain_ = false;
                status_ = GroupStatus::interrupted;
                return rt::ErrorCode::ok;
            }
            joint_window_.stopping_ = true;
            joint_window_.stop_profile_ = halt.value();
            joint_window_.stop_origin_ = s_live;
            joint_window_.tick_ = 0;
            interrupting_ = true;
            interrupted_window_ = true;
            interrupted_plain_ = false;
            status_ = GroupStatus::stopping;
            return rt::ErrorCode::ok;
        }
        if(!active_ || active_path_length_ <= 0.0) {
            interrupted_plain_ = false;
            interrupted_window_ = false;
            status_ = GroupStatus::interrupted;
            return rt::ErrorCode::ok;
        }
        const otg::State1D state =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        if(state.velocity <= 0.0) {
            interrupted_plain_ = true;
            interrupted_window_ = false;
            interrupt_ratio_ = state.position / active_path_length_;
            if(interrupt_ratio_ > 1.0) { interrupt_ratio_ = 1.0; }
            active_ = false;
            status_ = GroupStatus::interrupted;
            return rt::ErrorCode::ok;
        }
        const otg::Limits1D halt_limits{active_command_.velocity,
                                        active_command_.acceleration, deceleration, jerk};
        double brake_velocity = state.velocity;
        double brake_shift = 0.0;
        if(state.acceleration != 0.0) {
            const double zero_cycles = std::ceil(std::fabs(state.acceleration) / jerk);
            brake_velocity += 0.5 * state.acceleration * zero_cycles;
            brake_shift += state.velocity * zero_cycles +
                           state.acceleration * zero_cycles * zero_cycles / 3.0;
        }
        if(brake_velocity < 0.0) {
            brake_velocity = 0.0;
        }
        const double stop_position = state.position + brake_shift +
                                     otg::detail::ramp_between(brake_velocity, 0.0, halt_limits)
                                         .distance;
        const rt::Result<otg::Profile1D> halt = otg::plan_time_optimal(
            state, {stop_position, 0.0, 0.0}, halt_limits);
        if(!halt) {
            interrupted_plain_ = true;
            interrupted_window_ = false;
            interrupt_ratio_ = state.position / active_path_length_;
            if(interrupt_ratio_ > 1.0) { interrupt_ratio_ = 1.0; }
            active_ = false;
            status_ = GroupStatus::interrupted;
            return rt::ErrorCode::ok;
        }
        active_profile_ = halt.value();
        active_tick_ = 0;
        active_duration_ = active_profile_.duration_cycles();
        connector_.deactivate();
        interrupting_ = true;
        interrupted_plain_ = true;
        interrupted_window_ = false;
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
    }

    // MC_GroupContinue: resume from interrupted state, replanning remaining
    // path geometry from rest at the pause position.
    rt::ErrorCode continue_motion()
    {
        if(status_ != GroupStatus::interrupted) {
            return rt::ErrorCode::invalid_argument;
        }
        if(interrupted_plain_) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                active_start_[i] = axes_[i]->snapshot().command_position;
            }
            const double remaining = active_path_length_ * (1.0 - interrupt_ratio_);
            if(remaining <= 0.0) {
                interrupted_plain_ = false;
                interrupted_window_ = false;
                status_ = GroupStatus::standby;
                clear_axes_synchronized();
                start_next_queued();
                return rt::ErrorCode::ok;
            }
            active_path_length_ = remaining;
            const otg::Limits1D limits{
                active_command_.velocity * group_override_,
                active_command_.acceleration * group_acc_override_,
                active_command_.deceleration * group_acc_override_,
                active_command_.jerk * group_jerk_override_};
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {0.0, 0.0, 0.0}, {remaining, 0.0, 0.0}, limits);
            if(!profile) {
                return profile.error();
            }
            active_profile_ = profile.value();
            active_tick_ = 0;
            active_duration_ = active_profile_.duration_cycles();
            active_ = true;
            interrupted_plain_ = false;
            interrupted_window_ = false;
            interrupting_ = false;
            status_ = GroupStatus::moving;
            return rt::ErrorCode::ok;
        }
        if(interrupted_window_) {
            if(!joint_window_.active_ || joint_window_.segments_.empty()) {
                interrupted_window_ = false;
                interrupted_plain_ = false;
                status_ = GroupStatus::standby;
                clear_axes_synchronized();
                start_next_queued();
                return rt::ErrorCode::ok;
            }
            joint_window_.stopping_ = false;
            joint_window_.in_curve_ = false;
            joint_window_.tick_ = 0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                joint_window_.segments_[joint_window_.index_].entry[i] = axes_[i]->snapshot().command_position;
            }
            for(std::size_t s = joint_window_.index_; s < joint_window_.segments_.size(); ++s) {
                joint_window_.segments_[s].limits.max_velocity *= group_override_;
                joint_window_.segments_[s].limits.max_acceleration *= group_acc_override_;
                joint_window_.segments_[s].limits.max_deceleration *= group_acc_override_;
                joint_window_.segments_[s].limits.max_jerk *= group_jerk_override_;
            }
            bool late = false;
            if(!window_rebuild(late)) {
                window_reset();
                clear_axes_synchronized();
                interrupted_window_ = false;
                interrupted_plain_ = false;
                status_ = GroupStatus::standby;
                start_next_queued();
                return rt::ErrorCode::ok;
            }
            interrupted_window_ = false;
            interrupted_plain_ = false;
            interrupting_ = false;
            status_ = GroupStatus::moving;
            return rt::ErrorCode::ok;
        }
        interrupted_plain_ = false;
        interrupted_window_ = false;
        status_ = GroupStatus::standby;
        clear_axes_synchronized();
        start_next_queued();
        return rt::ErrorCode::ok;
    }

    // MC_GroupSetOverride: independent group velocity, acceleration and jerk
    // factors. A zero velocity factor performs the existing controlled freeze.
    rt::ErrorCode set_group_override(double factor, double acc_factor = 1.0,
                                     double jerk_factor = 1.0)
    {
        if(!std::isfinite(factor) || factor < 0.0 || factor > 1.0 ||
           !std::isfinite(acc_factor) || acc_factor < 0.0 ||
           acc_factor > 1.0 || !std::isfinite(jerk_factor) ||
           jerk_factor < 0.0 || jerk_factor > 1.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop) {
            return rt::ErrorCode::invalid_argument;
        }
        if(direct_path_.direct_active_) {
            return rt::ErrorCode::unsupported;
        }
        if(connector_.active()) {
            return rt::ErrorCode::unsupported;
        }
        const double previous = group_override_;
        const double previous_acc = group_acc_override_;
        const double previous_jerk = group_jerk_override_;
        group_override_ = factor;
        group_acc_override_ = acc_factor;
        group_jerk_override_ = jerk_factor;
        if(previous == factor && previous_acc == acc_factor &&
           previous_jerk == jerk_factor) {
            return rt::ErrorCode::ok;
        }
        if(status_ != GroupStatus::moving) {
            return rt::ErrorCode::ok;
        }
        if(cartesian_.window_active_) {
            return rt::ErrorCode::ok;
        }
        if(joint_window_.active_ && !joint_window_.stopping_) {
            if(factor == 0.0) {
                const rt::ErrorCode paused = interrupt(active_command_.deceleration,
                                                        active_command_.jerk);
                if(paused != rt::ErrorCode::ok) {
                    group_override_ = previous;
                    group_acc_override_ = previous_acc;
                    group_jerk_override_ = previous_jerk;
                    return paused;
                }
                joint_window_.override_paused_ = true;
                status_ = GroupStatus::moving;
                return rt::ErrorCode::ok;
            }
            if(joint_window_.override_paused_) {
                joint_window_.override_paused_ = false;
                status_ = GroupStatus::interrupted;
                group_override_ = 1.0;
                const rt::ErrorCode resumed = continue_motion();
                if(resumed != rt::ErrorCode::ok || status_ != GroupStatus::moving) {
                    group_override_ = factor;
                    group_acc_override_ = acc_factor;
                    group_jerk_override_ = jerk_factor;
                    return resumed;
                }
                return set_group_override(factor, acc_factor, jerk_factor);
            }
            for(std::size_t s = joint_window_.index_ + 1; s < joint_window_.segments_.size(); ++s) {
                if(previous > 0.0) {
                    joint_window_.segments_[s].limits.max_velocity =
                        joint_window_.segments_[s].limits.max_velocity * (factor / previous);
                } else {
                    double v = active_command_.velocity * factor;
                    if(joint_window_.segments_[s].kind == WindowKind::arc) {
                        const double junction = std::fmin(
                            joint_window_.segments_[s].limits.max_acceleration,
                            joint_window_.segments_[s].limits.max_deceleration);
                        const double centripetal =
                            std::sqrt(junction * joint_window_.segments_[s].arc_geom.radius);
                        if(centripetal < v) {
                            v = centripetal;
                        }
                    }
                    joint_window_.segments_[s].limits.max_velocity = v;
                }
                joint_window_.segments_[s].limits.max_acceleration =
                    previous_acc > 0.0
                        ? joint_window_.segments_[s].limits.max_acceleration *
                              (acc_factor / previous_acc)
                        : active_command_.acceleration * acc_factor;
                joint_window_.segments_[s].limits.max_deceleration =
                    previous_acc > 0.0
                        ? joint_window_.segments_[s].limits.max_deceleration *
                              (acc_factor / previous_acc)
                        : active_command_.deceleration * acc_factor;
                joint_window_.segments_[s].limits.max_jerk =
                    previous_jerk > 0.0
                        ? joint_window_.segments_[s].limits.max_jerk *
                              (jerk_factor / previous_jerk)
                        : active_command_.jerk * jerk_factor;
            }
            if(joint_window_.index_ + 1 < joint_window_.segments_.size()) {
                bool late = false;
                window_rebuild(late);
            }
            return rt::ErrorCode::ok;
        }
        if(!active_ || active_path_length_ <= 0.0) {
            return rt::ErrorCode::ok;
        }
        const otg::State1D state =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        const double remaining = active_path_length_ - state.position;
        if(remaining <= 0.0) {
            return rt::ErrorCode::ok;
        }
        if(factor == 0.0) {
            const double deceleration = active_command_.deceleration;
            const double jerk = active_command_.jerk;
            const otg::Limits1D halt_limits{active_command_.velocity,
                                            active_command_.acceleration,
                                            deceleration, jerk};
            double brake_velocity = state.velocity;
            double brake_shift = 0.0;
            if(state.acceleration != 0.0) {
                const double zero_cycles =
                    std::ceil(std::fabs(state.acceleration) / jerk);
                brake_velocity += 0.5 * state.acceleration * zero_cycles;
                brake_shift += state.velocity * zero_cycles +
                               state.acceleration * zero_cycles * zero_cycles / 3.0;
            }
            if(brake_velocity < 0.0) { brake_velocity = 0.0; }
            const double stop_position = state.position + brake_shift +
                otg::detail::ramp_between(brake_velocity, 0.0, halt_limits).distance;
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                state, {stop_position, 0.0, 0.0}, halt_limits);
            if(!profile) {
                group_override_ = previous;
                group_acc_override_ = previous_acc;
                group_jerk_override_ = previous_jerk;
                return profile.error();
            }
            active_profile_ = profile.value();
            active_tick_ = 0;
            active_duration_ = active_profile_.duration_cycles();
            override_paused_ = true;
            return rt::ErrorCode::ok;
        }
        if(override_paused_) {
            override_paused_ = false;
        }
        const otg::Limits1D limits{
            active_command_.velocity * factor,
            active_command_.acceleration * acc_factor,
            active_command_.deceleration * acc_factor,
            active_command_.jerk * jerk_factor};
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            state, {active_path_length_, 0.0, 0.0}, limits);
        if(!profile) {
            group_override_ = previous;
            group_acc_override_ = previous_acc;
            group_jerk_override_ = previous_jerk;
            return profile.error();
        }
        active_profile_ = profile.value();
        active_tick_ = 0;
        active_duration_ = active_profile_.duration_cycles();
        return rt::ErrorCode::ok;
    }

    double group_override() const
    {
        return group_override_;
    }

    double group_acc_override() const { return group_acc_override_; }

    double group_jerk_override() const { return group_jerk_override_; }

    // MoveDirect remains independent-member PTP for Aborting/Buffered. An
    // explicit blending transition is a coordinated line and therefore enters
    // the existing A4/A5 look-ahead planner instead of being silently ignored.
    rt::Result<std::uint32_t> submit_direct(GroupCommand command);

    // Legacy direct entry retained for internal callers; the public FB uses
    // submit_group_home so Position/CoordSystem/BufferMode enter the group
    // command queue and have an observable lifecycle.
    rt::ErrorCode group_home()
    {
        if(status_ != GroupStatus::standby || !queue_.empty()) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!members_ready_for_group_motion()) {
            return rt::ErrorCode::invalid_argument;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const rt::ErrorCode homed = axes_[i]->home_direct(0.0);
            if(homed != rt::ErrorCode::ok) {
                abort_motion();
                set_group_error(homed);
                return homed;
            }
        }
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_group_home(const GroupPosition &position,
                                                CoordSystem coordinate_system,
                                                BufferMode buffer_mode)
    {
        if(buffer_mode != BufferMode::aborting && buffer_mode != BufferMode::buffered) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           status_ == GroupStatus::interrupted || axes_.empty()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        GroupPosition requested = position;
        if(requested.size == 0) requested.size = axes_.size();
        GroupPosition resolved{};
        bool singular = false;
        const rt::ErrorCode transformed = transform_position(
            requested, coordinate_system, CoordSystem::acs, resolved, singular);
        if(transformed != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(transformed);
        }
        if(singular) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        const rt::ErrorCode limited = preflight_member_targets(resolved);
        if(limited != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(limited);
        }

        GroupCommand command{};
        command.kind = GroupCommandKind::home;
        command.target = resolved;
        command.buffer_mode = buffer_mode;
        command.command_id = next_command_id_++;
        if(buffer_mode == BufferMode::aborting) {
            abort_wait();
            abort_halt();
            abort_motion();
            status_ = GroupStatus::standby;
        }
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    bool group_homing() const
    {
        if(active_management_kind_ == GroupCommandKind::home) return true;
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].kind == GroupCommandKind::home) return true;
        }
        return false;
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

    bool management_command_aborted(std::uint32_t command_id) const
    {
        for(std::size_t i = 0; i < aborted_management_ids_.size(); ++i) {
            if(aborted_management_ids_[i] == command_id) return true;
        }
        return false;
    }

    bool management_command_active(std::uint32_t command_id) const
    {
        return active_management_id_ == command_id;
    }

    bool direct_motion_active() const;
    bool direct_command_done(std::uint32_t command_id) const;
    bool direct_command_aborted(std::uint32_t command_id) const;
    bool direct_command_active(std::uint32_t command_id) const;
    bool direct_command_busy(std::uint32_t command_id) const;
    std::uint32_t last_completed_direct_command() const;
    std::uint32_t last_aborted_direct_command() const;

    // Approved coordinate matrix (B1 v1): the workpiece frame (PCS over MCS)
    // and the tool offset are group configuration; they may only change at
    // standby with an empty queue — changing frames mid-motion has no
    // defined semantics.
    rt::ErrorCode set_workpiece_frame(double x, double y, double z, double rot_z);

    // Orientation batch (approved matrix, decision #4): the full rigid
    // workpiece frame; the Z-only setter above stays as its special case.
    rt::ErrorCode set_workpiece_frame_rpy(double x,
                                          double y,
                                          double z,
                                          double roll,
                                          double pitch,
                                          double yaw);

    // Orientation batch (approved matrix, decision #5): the flange-to-TCP
    // rigid transform for the pose pipeline (the translational pipeline
    // keeps its own set_tool_offset; the two never read each other).
    rt::ErrorCode set_tool_transform_rpy(double x,
                                         double y,
                                         double z,
                                         double roll,
                                         double pitch,
                                         double yaw);

    // Orientation batch (approved matrix, decisions #2/#3): the 6-DOF pose
    // plugin, mutually exclusive with the translational plugin.
    rt::ErrorCode set_pose_kinematics(const kin::PoseKinematics *plugin,
                                      double min_singularity_margin,
                                      double max_joint_step);

    // Readback batch (approved matrix decision #7): configuration getters
    // echo the original set values — never a matrix-to-RPY inversion of a
    // configured frame.
    void workpiece_frame_rpy(double out[6]) const;
    void tool_transform_rpy(double out[6]) const;
    geom::Vec3 tool_offset() const;

    // Readback batch (approved matrix decisions #1-#3): per-frame Cartesian
    // and pose readback, a pure const query mirroring the submit-side
    // conversion slot for slot — a read-back value is a resubmittable
    // target. Any motion state may read (readback is not configuration);
    // only WCS/FCS/TCS and a memberless/disabled group reject. Pose groups
    // report the TCP pose ([0..2] position, [3..5] RPY via extract_rpy with
    // the declared gimbal convention); translational groups report the TCP
    // point plus higher-axis ACS pass-through.
    rt::ErrorCode read_cartesian(CoordSystem cs,
                                 PositionSource source,
                                 GroupPosition &out,
                                 bool *gimbal_lock = nullptr) const;

    rt::ErrorCode set_tool_offset(double x, double y, double z);

    // BS3.6 (approved kinematics matrix follow-up): conservative dual-space
    // velocity limiting. With a kinematics plugin the segment interpolates
    // in joint space, so the Cartesian speed along it varies; at submit the
    // joint-space chord is sampled through the forward solution and the
    // command velocity is scaled down so the worst sampled Cartesian speed
    // stays under this limit (0 disables; linear segments only in v1).
    rt::ErrorCode set_cartesian_velocity_limit(double limit);

    // Approved kinematics matrix (B2 v1): the plugin upgrades the declared
    // identity ACS<->MCS mapping to a real mechanism. The caller owns the
    // plugin lifetime; nullptr restores the identity. v1 requires the joint
    // count to equal both the Cartesian coordinate count (2 or 3) and the
    // group axis count; the 6R batch lifts this.
    rt::ErrorCode set_kinematics(const kin::Kinematics *plugin,
                                 double min_singularity_margin = 0.0);

    rt::Result<std::uint32_t> submit_kinematics(
        const KinTransformRef &kin_transform,
        double min_singularity_margin,
        double max_joint_step,
        ExecutionMode mode)
    {
        const rt::ErrorCode valid = validate_kinematics(
            kin_transform, min_singularity_margin, max_joint_step);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        if(mode == ExecutionMode::immediately) {
            if(status_ != GroupStatus::standby || !queue_.empty()) {
                return rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::invalid_argument);
            }
            return complete_immediate(apply_kinematics(
                kin_transform, min_singularity_margin, max_joint_step));
        }
        GroupCommand command{};
        command.kind = GroupCommandKind::set_kinematics;
        command.kin_transform = kin_transform;
        command.min_singularity_margin = min_singularity_margin;
        command.max_joint_step = max_joint_step;
        return enqueue_management(command, mode);
    }

    const kin::Kinematics *kinematics_plugin() const;
    const kin::PoseKinematics *pose_kinematics_plugin() const;
    KinTransformRef kin_transform() const;

    rt::ErrorCode set_coordinate_transform(CoordSystem coordinate_system,
                                           const ToolData &transform,
                                           ExecutionMode execution_mode);
    rt::Result<std::uint32_t> submit_coordinate_transform(
        CoordSystem coordinate_system,
        const ToolData &transform,
        ExecutionMode execution_mode)
    {
        const rt::ErrorCode valid = validate_coordinate_transform(
            coordinate_system, transform);
        if(valid != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(valid);
        }
        if(execution_mode == ExecutionMode::immediately) {
            return complete_immediate(set_coordinate_transform(
                coordinate_system, transform, execution_mode));
        }
        GroupCommand command{};
        command.kind = GroupCommandKind::set_coordinate_transform;
        command.coord_system = coordinate_system;
        command.tool_data = transform;
        return enqueue_management(command, execution_mode);
    }
    rt::ErrorCode coordinate_transform(CoordSystem coordinate_system,
                                       ToolData &transform) const;
    rt::ErrorCode transform_position(const GroupPosition &position,
                                     CoordSystem source,
                                     CoordSystem target,
                                     GroupPosition &output,
                                     bool &singular_position) const;

    rt::ErrorCode set_group_position(const GroupPosition &position,
                                     bool relative,
                                     CoordSystem coordinate_system,
                                     ExecutionMode execution_mode)
    {
        if(execution_mode != ExecutionMode::immediately ||
           status_ != GroupStatus::standby || !configuration_writable()) {
            return rt::ErrorCode::unsupported;
        }
        GroupPosition resolved{};
        bool singular = false;
        const rt::ErrorCode transformed = transform_position(
            position, coordinate_system, CoordSystem::acs, resolved, singular);
        if(transformed != rt::ErrorCode::ok) return transformed;
        if(singular) return rt::ErrorCode::precondition_failed;
        if(relative) {
            for(std::size_t i = 0; i < resolved.size; ++i) {
                resolved.value[i] += axes_[i]->snapshot().command_position;
            }
        }
        const rt::ErrorCode preflight = preflight_member_targets(resolved);
        if(preflight != rt::ErrorCode::ok) return preflight;
        for(std::size_t i = 0; i < resolved.size; ++i) {
            const rt::ErrorCode result = axes_[i]->set_position(resolved.value[i]);
            if(result != rt::ErrorCode::ok) return result;
        }
        return rt::ErrorCode::ok;
    }

    // Look-ahead v2 addendum (approved 2026-07-06): runtime cap on the
    // window depth; the declared capacity_exceeded semantics fire at the
    // configured value. Default stays the full 64 (replay guard).
    rt::ErrorCode set_window_depth(std::size_t depth)
    {
        if(status_ != GroupStatus::standby || !queue_.empty() || joint_window_.active_ ||
           depth < 2 || depth > WindowCapacity) {
            return rt::ErrorCode::invalid_argument;
        }
        joint_window_.depth_ = depth;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_linear(GroupCommand command)
    {
        pending_dynamic_pcs_ = false;
        if(command.buffer_mode != BufferMode::aborting && has_queued_management()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(jog_active_ && command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        command.tool_number = selected_tool_;
        command.payload_number = selected_payload_;
        command.tool_inverse = pose_frames_.pose_tool_inverse_;
        const rt::ErrorCode dynamics = resolve_dynamics(command);
        if(dynamics != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(dynamics);
        }
        const bool halt_takeover = halt_command_live_ &&
                                   status_ == GroupStatus::stopping &&
                                   command.buffer_mode == BufferMode::aborting;
        if(status_ == GroupStatus::interrupted) {
            if(command.buffer_mode != BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            interrupted_plain_ = false;
            interrupted_window_ = false;
            interrupting_ = false;
        }
        // MoveDirect is driven by member base profiles. Until a coordinated
        // takeover can cancel every member atomically, accepting a path here
        // would leave AxisGroup::cycle() and AxisModel::cycle() as two writers.
        if(direct_path_.direct_active_ && !halt_takeover) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if((status_ != GroupStatus::standby && status_ != GroupStatus::moving &&
              status_ != GroupStatus::interrupted && !halt_takeover) ||
           axes_.size() < 2 ||
           command.target.size != axes_.size() || command.velocity <= 0.0 ||
           !std::isfinite(command.velocity) || command.acceleration <= 0.0 ||
           !std::isfinite(command.acceleration) || command.deceleration <= 0.0 ||
           !std::isfinite(command.deceleration) || command.jerk <= 0.0 ||
           !std::isfinite(command.jerk) || !finite(command.target)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::ErrorCode orientation = select_orientation_interpolation(command);
        if(orientation != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(orientation);
        }
        const bool blending_buffer = command.buffer_mode == BufferMode::blending_low ||
                                     command.buffer_mode == BufferMode::blending_high;
        if(!std::isfinite(command.transition_velocity) ||
           command.transition_velocity < 0.0 ||
           command.transition_velocity > command.velocity ||
           (command.transition_velocity > 0.0 && !blending_buffer)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(!members_ready_for_group_motion()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(tracking_kind_ != TrackingKind::none && command.coord_system == CoordSystem::pcs) {
            if(command.buffer_mode != BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            command.dynamic_pcs = true;
            pending_dynamic_pcs_ = true;
            pose_frames_.pending_dynamic_reference_frame_ = pose_frames_.workpiece_frame_;
        }
        // Cartesian-interpolation batch (approved matrix): resolve the
        // opt-in segment before the frame collapse — pre-validation either
        // rejects here or leaves the ACS endpoint joints plus the cycle
        // geometry in command.cart.
        if(command.interpolation_space == InterpolationSpace::cartesian) {
            // Cartesian v2-C (approved addendum): a blending successor with a
            // corner tolerance fuses with the active Cartesian line here;
            // every other blending shape stays unsupported via the guards.
            if((command.buffer_mode == BufferMode::blending_low ||
                command.buffer_mode == BufferMode::blending_high) &&
               command.transition_mode == TransitionMode::max_corner_deviation &&
               std::isfinite(command.transition_parameter) &&
               command.transition_parameter > 0.0) {
                // v3 window (approved addendum): translational plugin groups;
                // pose groups keep the KB-049 single-successor chain.
                if(pose_frames_.kinematics_ != nullptr) {
                    return submit_cartesian_window(command);
                }
                return submit_cartesian_blend(command);
            }
            const rt::ErrorCode prepared = prepare_cartesian_linear(command);
            if(prepared != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(prepared);
            }
        }

        const rt::ErrorCode framed = apply_coordinate_frame(command);
        if(framed != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(framed);
        }

        // TransitionMode combination matrix (approved blending matrix):
        // unlisted combinations are explicit errors, never silent downgrades.
        if(command.transition_mode == TransitionMode::none) {
            if(command.transition_parameter != 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            if(blending_buffer) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        } else if(command.transition_mode == TransitionMode::max_corner_deviation) {
            if(!std::isfinite(command.transition_parameter) ||
               command.transition_parameter <= 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            if(command.buffer_mode == BufferMode::aborting) {
                // Aborting semantics and pre-blended queues are mutually
                // exclusive (approved matrix).
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            if(!blending_buffer) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        } else {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }

        // submit_linear always drives a linear path regardless of any stray
        // circular fields on the command struct (the cartesian marker set by
        // the pre-validation above is the one exception).
        if(command.interpolation_space != InterpolationSpace::cartesian) {
            command.path_kind = GroupPathKind::linear;
        }
        command.arc = geom::ArcSegment{};

        const GroupCommand normalized = normalize(command);
        const rt::ErrorCode limited = preflight_member_targets(normalized.target);
        if(limited != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(limited);
        }

        if(command.buffer_mode == BufferMode::aborting) {
            // Y7/Y7b1/Y7b2a: capture the live member state before
            // abort_motion() destroys an eligible joint path or a plain
            // Cartesian LINE source. The target kind below is also part of
            // the Cartesian-source scope gate.
            if(active_ && !joint_window_.active_ && !cartesian_.window_active_) {
                capture_takeover_velocity(
                    command.path_kind == GroupPathKind::linear &&
                    !command.dynamic_pcs);
            }
            if(halt_takeover && direct_path_.direct_active_) abort_direct_members();
            abort_halt();
            abort_wait();
            abort_motion();
        }
        command = normalized;

        if(blending_buffer) {
            return submit_blend(command);
        }

        if(command.buffer_mode == BufferMode::aborting ||
           (!active_ && !joint_window_.active_ && !wait_blocks_motion_start())) {
            const rt::ErrorCode started = start(command);
            if(started != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(started);
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        // A buffered command behind an active look-ahead window queues
        // normally: the window terminates at rest (approved A5 matrix).
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    // MC_MoveCircularAbsolute/Relative (approved circular matrix, A3 v1):
    // three-point BORDER arcs in the plane of the first two axes, remaining
    // axes follow the path parameter linearly. Degenerate geometry is an
    // explicit error before any motion state is touched.
    rt::Result<std::uint32_t> submit_circular(GroupCommand command)
    {
        pending_dynamic_pcs_ = false;
        if(command.buffer_mode != BufferMode::aborting && has_queued_management()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(jog_active_ && command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        command.tool_number = selected_tool_;
        command.payload_number = selected_payload_;
        command.tool_inverse = pose_frames_.pose_tool_inverse_;
        const rt::ErrorCode dynamics = resolve_dynamics(command);
        if(dynamics != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(dynamics);
        }
        const bool halt_takeover = halt_command_live_ &&
                                   status_ == GroupStatus::stopping &&
                                   command.buffer_mode == BufferMode::aborting;
        if(status_ == GroupStatus::interrupted) {
            if(command.buffer_mode != BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            interrupted_plain_ = false;
            interrupted_window_ = false;
            interrupting_ = false;
        }
        if(direct_path_.direct_active_ && !halt_takeover) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if((status_ != GroupStatus::standby && status_ != GroupStatus::moving &&
              status_ != GroupStatus::interrupted && !halt_takeover) ||
           axes_.size() < 2 || command.target.size != axes_.size() ||
           command.aux.size != axes_.size() || command.velocity <= 0.0 ||
           !std::isfinite(command.velocity) || command.acceleration <= 0.0 ||
           !std::isfinite(command.acceleration) || command.deceleration <= 0.0 ||
           !std::isfinite(command.deceleration) || command.jerk <= 0.0 ||
           !std::isfinite(command.jerk) || !finite(command.target) || !finite(command.aux) ||
           !std::isfinite(command.tolerance) || command.tolerance < 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::ErrorCode orientation = select_orientation_interpolation(command);
        if(orientation != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(orientation);
        }
        const bool arc_blending = command.buffer_mode == BufferMode::blending_low ||
                                  command.buffer_mode == BufferMode::blending_high;
        if(!std::isfinite(command.transition_velocity) ||
           command.transition_velocity < 0.0 ||
           command.transition_velocity > command.velocity ||
           (command.transition_velocity > 0.0 && !arc_blending)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(command.transition_mode == TransitionMode::none) {
            if(command.transition_parameter != 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
        } else if(command.transition_mode == TransitionMode::max_corner_deviation) {
            if(!std::isfinite(command.transition_parameter) ||
               command.transition_parameter <= 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            if(command.buffer_mode == BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            if(!arc_blending) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        } else {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(!members_ready_for_group_motion()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(tracking_kind_ != TrackingKind::none && command.coord_system == CoordSystem::pcs) {
            if(command.buffer_mode != BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            command.dynamic_pcs = true;
            pending_dynamic_pcs_ = true;
            pose_frames_.pending_dynamic_reference_frame_ = pose_frames_.workpiece_frame_;
        }
        if(command.circ_mode != CircMode::border) {
            // CENTER/RADIUS are declared unsupported in v1, not approximated.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        // Cartesian v2-B (approved addendum): opt-in Cartesian-domain arcs
        // resolve here and commit directly — the joint-domain construction
        // below never sees them.
        if(command.interpolation_space == InterpolationSpace::cartesian) {
            if(arc_blending) {
                // Cartesian circular transition-window geometry is not
                // implemented; reject instead of queuing a fake blend.
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            const rt::ErrorCode prepared = prepare_cartesian_circular(command);
            if(prepared != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(prepared);
            }
            if(command.command_id == 0) {
                command.command_id = next_command_id_++;
            }
            if(command.buffer_mode == BufferMode::aborting) {
                if(halt_takeover && direct_path_.direct_active_) abort_direct_members();
                abort_halt();
                abort_wait();
                abort_motion();
            }
            if(command.buffer_mode == BufferMode::aborting ||
               (!active_ && !joint_window_.active_ && !wait_blocks_motion_start())) {
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
        // Orientation batch (approved matrix, decision #6): the pose pipeline
        // carries no joint-domain circular semantics; ACS arcs stay available
        // (decision #8 passthrough).
        if(pose_frames_.pose_kinematics_ != nullptr && command.coord_system != CoordSystem::acs) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        const rt::ErrorCode framed = apply_coordinate_frame(command);
        if(framed != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(framed);
        }
        // Deterministic start point: the live commanded position for an
        // aborting takeover, otherwise the committed finish of the queue tail.
        // Geometry is validated before any abort so a rejected command never
        // destroys the active motion.
        const bool aborting = command.buffer_mode == BufferMode::aborting;
        std::array<double, MaxAxes> start_point{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            start_point[i] = aborting ? axes_[i]->snapshot().command_position
                                      : queued_finish(i);
        }
        if(command.relative) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                command.target.value[i] += start_point[i];
                command.aux.value[i] += start_point[i];
            }
            command.relative = false;
        }

        const rt::ErrorCode limited = preflight_member_targets(command.target);
        if(limited != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(limited);
        }

        const geom::Vec3 plane_start{start_point[0], start_point[1], 0.0};
        const geom::Vec3 plane_aux{command.aux.value[0], command.aux.value[1], 0.0};
        const geom::Vec3 plane_finish{command.target.value[0], command.target.value[1], 0.0};
        // Coincident points (zero arc length / zero radius) and the closed
        // start==finish circle are rejected in v1 (full circles are v2 scope).
        constexpr double PointTolerance = 1e-12;
        if(geom::norm(plane_aux - plane_start) <= PointTolerance ||
           geom::norm(plane_finish - plane_aux) <= PointTolerance ||
           geom::norm(plane_finish - plane_start) <= PointTolerance) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        const rt::Result<geom::ArcSegment> arc =
            geom::make_arc(plane_start, plane_aux, plane_finish);
        if(!arc) {
            // Collinear three points do not degrade to a line (approved matrix).
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        // Nearly collinear pathological arcs: curvature radius beyond
        // chord x 1e6 is rejected instead of sampled (T8 numeric boundary).
        if(arc.value().radius > geom::norm(plane_finish - plane_start) * 1e6) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        // BORDER determines the direction; a conflicting PathChoice input is
        // an explicit contract violation.
        const CircPathChoice derived = arc.value().sweep >= 0.0
                                           ? CircPathChoice::counter_clockwise
                                           : CircPathChoice::clockwise;
        if(derived != command.path_choice) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(command.tolerance > 0.0) {
            const double via_sweep = geom::normalize_sweep(
                geom::angle_of(plane_aux, arc.value().center) -
                    arc.value().start_angle,
                arc.value().sweep);
            const double fraction = via_sweep / arc.value().sweep;
            for(std::size_t i = 2; i < axes_.size(); ++i) {
                const double expected = start_point[i] +
                                        (command.target.value[i] - start_point[i]) *
                                            fraction;
                if(std::fabs(command.aux.value[i] - expected) > command.tolerance) {
                    return rt::Result<std::uint32_t>::failure(
                        rt::ErrorCode::invalid_argument);
                }
            }
        }
        command.path_kind = GroupPathKind::circular;
        command.arc = arc.value();

        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }
        if(arc_blending) {
            // A5 v2 (KB-033): the arc joins the look-ahead window when the
            // junction is tangent-continuous, otherwise the request degrades
            // to a BUFFERED full-stop join (reported).
            return submit_blend_arc(command, start_point);
        }
        if(aborting) {
            // Y7b1/Y7b2a-2: geometry is fully validated before the live plain
            // path is captured, so an invalid arc never disturbs the active
            // command. Dynamic PCS targets do not open the Cartesian source gate.
            if(active_ && !joint_window_.active_ &&
               !cartesian_.window_active_) {
                capture_takeover_velocity(!command.dynamic_pcs);
            }
            if(halt_takeover && direct_path_.direct_active_) abort_direct_members();
            abort_halt();
            abort_wait();
            abort_motion();
        }
        if(aborting || (!active_ && !joint_window_.active_ && !wait_blocks_motion_start())) {
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

    // Degradation of a blending request to a plain BUFFERED join is reported
    // through this query, never silently (approved blending matrix).
    // Cartesian-interpolation batch (approved matrix decision #7): the
    // error that tripped the cycle-path errorstop, ok when none did.
    rt::ErrorCode last_cartesian_error() const
    {
        return cartesian_.last_error_;
    }

    rt::ErrorCode group_error() const
    {
        return status_ == GroupStatus::errorstop ? group_error_id_ : rt::ErrorCode::ok;
    }

    std::uint32_t last_blend_degraded_command() const
    {
        return last_blend_degraded_id_;
    }

    GroupMotionState motion_state() const
    {
        GroupMotionState result{};
        result.tracking = tracking_kind_ != TrackingKind::none;
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop) {
            result.in_position = false;
            result.standstill = false;
            return result;
        }
        double velocity = 0.0;
        double acceleration = 0.0;
        if(direct_path_.direct_active_) {
            result.active_command_id = direct_path_.direct_command_id_;
            result.in_position = false;
            result.standstill = false;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const AxisSnapshot &snapshot = axes_[i]->snapshot();
                if(std::fabs(snapshot.command_velocity) > std::fabs(velocity)) {
                    velocity = snapshot.command_velocity;
                }
                if(std::fabs(snapshot.command_acceleration) >
                   std::fabs(acceleration)) {
                    acceleration = snapshot.command_acceleration;
                }
            }
        } else if(joint_window_.active_ && !joint_window_.segments_.empty()) {
            result.active_command_id = joint_window_.segments_[joint_window_.index_].command_id;
            result.in_position = false;
            result.standstill = false;
            double position = 0.0;
            window_live_state(position, velocity, acceleration);
        } else if(active_) {
            result.active_command_id = active_command_.command_id;
            result.in_position = false;
            result.standstill = false;
            const otg::State1D state = otg::sample(
                active_profile_, rt::CycleTick::from_cycles(active_tick_));
            velocity = state.velocity;
            acceleration = state.acceleration;
        }
        constexpr double MotionTolerance = 1e-12;
        if(result.active_command_id != 0) {
            result.accelerating = acceleration > MotionTolerance;
            result.decelerating = acceleration < -MotionTolerance;
            result.constant_velocity = !result.accelerating &&
                                       !result.decelerating &&
                                       std::fabs(velocity) > MotionTolerance;
        }
        return result;
    }

    double path_derivative(bool acceleration) const
    {
        if(joint_window_.active_ && !joint_window_.segments_.empty()) {
            double position = 0.0;
            double velocity = 0.0;
            double accel = 0.0;
            window_live_state(position, velocity, accel);
            return acceleration ? accel : velocity;
        }
        if(active_) {
            const otg::State1D state = otg::sample(
                active_profile_, rt::CycleTick::from_cycles(active_tick_));
            return acceleration ? state.acceleration : state.velocity;
        }
        return 0.0;
    }

    rt::Result<GroupCommandInfo> command_info(std::uint32_t command_id) const
    {
        if(command_id == 0) {
            return rt::Result<GroupCommandInfo>::failure(rt::ErrorCode::out_of_range);
        }
        if(command_id == wait_command_id_ &&
           wait_state_ != GroupWaitState::idle && wait_state_ != GroupWaitState::aborted) {
            GroupCommandInfo result{};
            result.state = wait_state_ == GroupWaitState::queued ||
                                   wait_state_ == GroupWaitState::stopping
                               ? GroupCommandState::accepted
                               : GroupCommandState::active;
            result.elapsed_cycles = wait_elapsed_cycles_;
            result.remaining_cycles = wait_duration_cycles_ > wait_elapsed_cycles_
                                          ? wait_duration_cycles_ - wait_elapsed_cycles_
                                          : 0;
            result.progress = wait_duration_cycles_ > 0
                                  ? static_cast<double>(wait_elapsed_cycles_) /
                                        static_cast<double>(wait_duration_cycles_)
                                  : 1.0;
            if(result.progress > 1.0) result.progress = 1.0;
            return rt::Result<GroupCommandInfo>::success(result);
        }
        if(command_id == halt_command_id_ && halt_command_live_) {
            GroupCommandInfo result{};
            result.state = status_ == GroupStatus::standby ? GroupCommandState::accepted
                                                            : GroupCommandState::active;
            return rt::Result<GroupCommandInfo>::success(result);
        }
        if(cartesian_.window_active_) {
            return rt::Result<GroupCommandInfo>::failure(
                rt::ErrorCode::unsupported);
        }
        if(direct_path_.direct_active_ && command_id == direct_path_.direct_command_id_) {
            GroupCommandInfo result{};
            result.state = GroupCommandState::active;
            return rt::Result<GroupCommandInfo>::success(result);
        }
        if(joint_window_.active_) {
            for(std::size_t i = joint_window_.index_; i < joint_window_.segments_.size(); ++i) {
                if(joint_window_.segments_[i].command_id != command_id) continue;
                GroupCommandInfo result{};
                result.state = i == joint_window_.index_ ? GroupCommandState::active
                                                  : GroupCommandState::accepted;
                if(i == joint_window_.index_) {
                    result.elapsed_cycles = joint_window_.tick_;
                    result.remaining_cycles =
                        joint_window_.segments_[i].profile.duration_cycles() - joint_window_.tick_;
                    double position = 0.0;
                    double velocity = 0.0;
                    double acceleration = 0.0;
                    window_live_state(position, velocity, acceleration);
                    const double length = joint_window_.segments_[i].line_length();
                    result.remaining_distance = length > position
                                                    ? length - position
                                                    : 0.0;
                    result.progress = length > 0.0 ? position / length : 1.0;
                }
                return rt::Result<GroupCommandInfo>::success(result);
            }
        }
        if(active_ && active_command_.command_id == command_id) {
            GroupCommandInfo result{};
            result.state = GroupCommandState::active;
            result.elapsed_cycles = active_tick_;
            result.remaining_cycles = active_duration_ - active_tick_;
            const otg::State1D state = otg::sample(
                active_profile_, rt::CycleTick::from_cycles(active_tick_));
            result.remaining_distance = active_path_length_ > state.position
                                            ? active_path_length_ - state.position
                                            : 0.0;
            result.progress = active_path_length_ > 0.0
                                  ? state.position / active_path_length_
                                  : 1.0;
            return rt::Result<GroupCommandInfo>::success(result);
        }
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].command_id == command_id) {
                GroupCommandInfo result{};
                result.state = GroupCommandState::accepted;
                return rt::Result<GroupCommandInfo>::success(result);
            }
        }
        return rt::Result<GroupCommandInfo>::failure(rt::ErrorCode::out_of_range);
    }

    void cycle()
    {
        if(cycle_wait()) return;
        update_tracking_transform();
        apply_tracking_hold();
        update_path_odometer();
        cycle_axis_to_group_sync();
        if(group_sync_active_) {
            cycle_group_to_axis_sync();
            return;
        }
        if(status_ == GroupStatus::errorstop || status_ == GroupStatus::disabled ||
           status_ == GroupStatus::interrupted) {
            return;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->powered() || axes_[i]->status() == AxisStatus::errorstop) {
                if(direct_path_.direct_active_) {
                    direct_path_.last_aborted_direct_id_ = direct_path_.direct_command_id_;
                    abort_direct_members();
                }
                abort_motion();
                interrupting_ = false;
                set_group_error(rt::ErrorCode::precondition_failed);
                return;
            }
        }

        if(jog_active_) {
            jog_cycle();
            return;
        }

        if(direct_path_.direct_active_) {
            bool all_done = true;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                if(axes_[i]->status() != AxisStatus::standstill) {
                    all_done = false;
                    break;
                }
            }
            if(all_done) {
                if(direct_path_.direct_stopping_) {
                    abort_direct(direct_path_.direct_command_id_);
                } else {
                    complete_direct(direct_path_.direct_command_id_);
                }
                direct_path_.direct_active_ = false;
                direct_path_.direct_stopping_ = false;
                status_ = GroupStatus::standby;
                start_next_queued();
            }
            return;
        }

        if(cartesian_.window_active_) {
            cart_window_cycle();
            return;
        }
        if(joint_window_.active_) {
            window_cycle();
            return;
        }

        if(!active_) {
            if(status_ == GroupStatus::stopping) {
                if(interrupting_) {
                    interrupting_ = false;
                    status_ = GroupStatus::interrupted;
                } else {
                    status_ = GroupStatus::standby;
                }
            }
            if(status_ == GroupStatus::standby && !queue_.empty()) {
                start_next_queued();
            }
            return;
        }

        ++active_tick_;
        double ratio = 1.0;
        if(active_path_length_ > 0.0) {
            const otg::State1D state =
                otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
            ratio = state.position / active_path_length_;
            if(active_kind_ != GroupPathKind::linear) {
                ratio = ratio < 0.0 ? 0.0 : ratio;
            }
            ratio = ratio > 1.0 ? 1.0 : ratio;
        }
        if(active_kind_ == GroupPathKind::circular) {
            // Arc-length parameterized sampling: the first two axes trace the
            // arc, remaining axes follow the path parameter linearly.
            std::array<double, MaxAxes> connector_offsets{};
            connector_.sample_lateral_offsets(active_tick_, connector_offsets,
                                              axes_.size());
            const geom::Vec3 point = geom::sample(active_arc_, ratio * active_path_length_);
            axes_[0]->set_synchronized_position(point.x + connector_offsets[0]);
            axes_[1]->set_synchronized_position(point.y + connector_offsets[1]);
            for(std::size_t i = 2; i < axes_.size(); ++i) {
                const double position =
                    active_start_[i] +
                    (active_finish_[i] - active_start_[i]) * ratio +
                    connector_offsets[i];
                axes_[i]->set_synchronized_position(position);
            }
        } else if(active_kind_ == GroupPathKind::cartesian_linear) {
            if(!cartesian_cycle(ratio)) {
                return;
            }
        } else {
            // Y7 (KB-051): during the connector, the output is the
            // along-path position plus the lateral decay offset. After
            // the lateral profile completes, its position is zero and
            // the motion continues as pure along-path interpolation.
            const bool vector_connector = connector_.vector_mode();
            std::array<double, MaxAxes> connector_offsets{};
            double lat_offset = 0.0;
            if(vector_connector) {
                connector_.sample_lateral_offsets(active_tick_,
                                                  connector_offsets,
                                                  axes_.size());
            } else {
                lat_offset = connector_.sample_lateral_offset(active_tick_);
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                double position =
                    active_start_[i] + (active_finish_[i] - active_start_[i]) * ratio;
                if(vector_connector) {
                    position += connector_offsets[i];
                } else if(lat_offset != 0.0) {
                    position += connector_.lateral_dir(i) * lat_offset;
                }
                axes_[i]->set_synchronized_position(position);
            }
        }

        if(!apply_dynamic_tracking_to_active()) {
            tracking_error_ = rt::ErrorCode::precondition_failed;
            abort_motion();
            set_group_error(tracking_error_);
            return;
        }

        if(active_tick_ >= active_duration_) {
            finish_active();
        }
    }

    rt::ErrorCode reset()
    {
        if(status_ != GroupStatus::errorstop) {
            return rt::ErrorCode::invalid_argument;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(axes_[i]->status() == AxisStatus::errorstop) {
                const rt::ErrorCode reset = axes_[i]->reset_error();
                if(reset != rt::ErrorCode::ok) {
                    return reset;
                }
            }
        }
        status_ = GroupStatus::standby;
        group_error_id_ = rt::ErrorCode::ok;
        return rt::ErrorCode::ok;
    }

private:
    void set_group_error(rt::ErrorCode error)
    {
        group_error_id_ = error;
        status_ = GroupStatus::errorstop;
    }

    rt::ErrorCode validate_group_parameter(GroupParameter parameter, double value) const
    {
        if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        if(parameter == GroupParameter::dynamics_mode) {
            return value == static_cast<double>(DynamicsMode::absolute) ||
                           value == static_cast<double>(DynamicsMode::percentage)
                       ? rt::ErrorCode::ok
                       : rt::ErrorCode::invalid_argument;
        }
        if(parameter == GroupParameter::transition_reference_point) {
            if(value == static_cast<double>(TransitionReferencePoint::start_point)) {
                return rt::ErrorCode::unsupported;
            }
            return value == static_cast<double>(TransitionReferencePoint::end_point)
                       ? rt::ErrorCode::ok
                       : rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::unsupported;
    }

    rt::ErrorCode validate_tool_data(std::size_t number, const ToolData &data) const
    {
        if(number == 0) return rt::ErrorCode::precondition_failed;
        if(number >= ToolCapacity) return rt::ErrorCode::out_of_range;
        for(double value : data.value) {
            if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode validate_group_sw_limits(const GroupSWLimits &limits) const
    {
        if(limits.count != axes_.size()) return rt::ErrorCode::invalid_argument;
        for(std::size_t i = 0; i < limits.count; ++i) {
            const GroupSWLimit &entry = limits.value[i];
            if(!std::isfinite(entry.minimum) || !std::isfinite(entry.maximum) ||
               entry.minimum > entry.maximum) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        return rt::ErrorCode::ok;
    }
    rt::ErrorCode validate_kinematics(const KinTransformRef &transform,
                                      double min_singularity_margin,
                                      double max_joint_step) const;
    rt::ErrorCode apply_kinematics(const KinTransformRef &transform,
                                   double min_singularity_margin,
                                   double max_joint_step);
    rt::ErrorCode validate_coordinate_transform(CoordSystem coordinate_system,
                                                const ToolData &transform) const;
    rt::ErrorCode apply_coordinate_transform(CoordSystem coordinate_system,
                                             const ToolData &transform);

    rt::Result<std::uint32_t> complete_immediate(rt::ErrorCode result)
    {
        if(result != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(result);
        }
        const std::uint32_t command_id = next_command_id_++;
        remember_management_result(command_id, rt::ErrorCode::ok);
        return rt::Result<std::uint32_t>::success(command_id);
    }

    rt::Result<std::uint32_t> enqueue_management(GroupCommand command,
                                                 ExecutionMode mode)
    {
        if(mode != ExecutionMode::queued) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(status_ != GroupStatus::standby && status_ != GroupStatus::moving &&
           status_ != GroupStatus::stopping) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::precondition_failed);
        }
        command.command_id = next_command_id_++;
        const rt::ErrorCode queued = queue_.push_back(command);
        return queued == rt::ErrorCode::ok
                   ? rt::Result<std::uint32_t>::success(command.command_id)
                   : rt::Result<std::uint32_t>::failure(queued);
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

    void remember_management_aborted(std::uint32_t command_id)
    {
        if(command_id == 0) return;
        if(aborted_management_ids_.size() == QueueCapacity) {
            for(std::size_t i = 1; i < aborted_management_ids_.size(); ++i) {
                aborted_management_ids_[i - 1] = aborted_management_ids_[i];
            }
            aborted_management_ids_.pop_back();
        }
        aborted_management_ids_.push_back(command_id);
    }

    bool command_queued(std::uint32_t command_id) const
    {
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].command_id == command_id) return true;
        }
        return false;
    }

    bool has_queued_management() const
    {
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].kind != GroupCommandKind::motion &&
               queue_[i].kind != GroupCommandKind::direct) return true;
        }
        return false;
    }

    void abort_queued_management()
    {
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].kind != GroupCommandKind::motion) {
                remember_management_aborted(queue_[i].command_id);
                if(queue_[i].kind == GroupCommandKind::halt &&
                   halt_command_id_ == queue_[i].command_id) {
                    last_aborted_halt_id_ = queue_[i].command_id;
                    halt_command_live_ = false;
                }
            }
        }
    }

    rt::ErrorCode execute_management(const GroupCommand &command)
    {
        active_management_id_ = command.command_id;
        active_management_kind_ = command.kind;
        rt::ErrorCode result = rt::ErrorCode::ok;
        switch(command.kind) {
        case GroupCommandKind::home:
            if(status_ != GroupStatus::standby || !members_ready_for_group_motion()) {
                result = rt::ErrorCode::precondition_failed;
                break;
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                result = axes_[i]->home_direct(command.target.value[i]);
                if(result != rt::ErrorCode::ok) break;
            }
            if(result != rt::ErrorCode::ok) set_group_error(result);
            break;
        case GroupCommandKind::halt:
            abort_queued_management();
            queue_.clear();
            last_completed_halt_id_ = command.command_id;
            halt_command_id_ = command.command_id;
            halt_command_live_ = true;
            break;
        case GroupCommandKind::set_kinematics:
            result = apply_kinematics(command.kin_transform,
                                      command.min_singularity_margin,
                                      command.max_joint_step);
            break;
        case GroupCommandKind::set_coordinate_transform:
            result = apply_coordinate_transform(command.coord_system, command.tool_data);
            break;
        case GroupCommandKind::write_parameter:
            result = validate_group_parameter(command.parameter, command.parameter_value);
            if(result == rt::ErrorCode::ok) {
                if(command.parameter == GroupParameter::dynamics_mode) {
                    dynamics_mode_ = static_cast<DynamicsMode>(
                        static_cast<int>(command.parameter_value));
                } else {
                    transition_reference_point_ = TransitionReferencePoint::end_point;
                }
            }
            break;
        case GroupCommandKind::write_sw_limits:
            result = validate_group_sw_limits(command.sw_limits);
            if(result == rt::ErrorCode::ok) {
                for(std::size_t i = 0; i < command.sw_limits.count; ++i) {
                    MotionLimits next = axes_[i]->limits_;
                    next.min_position = command.sw_limits.value[i].minimum;
                    next.max_position = command.sw_limits.value[i].maximum;
                    next.min_position_enabled = command.sw_limits.value[i].minimum_enabled;
                    next.max_position_enabled = command.sw_limits.value[i].maximum_enabled;
                    axes_[i]->limits_ = next;
                }
            }
            break;
        case GroupCommandKind::write_tool_data:
            result = validate_tool_data(command.tool_number, command.tool_data);
            if(result == rt::ErrorCode::ok) {
                tools_[command.tool_number] = command.tool_data;
                tool_defined_[command.tool_number] = true;
            }
            break;
        case GroupCommandKind::motion:
        case GroupCommandKind::direct:
            result = rt::ErrorCode::invalid_argument;
            break;
        }
        remember_management_result(command.command_id, result);
        active_management_id_ = 0;
        active_management_kind_ = GroupCommandKind::motion;
        return result;
    }

    bool cycle_wait()
    {
        if((wait_state_ == GroupWaitState::queued ||
            wait_state_ == GroupWaitState::stopping) &&
           status_ == GroupStatus::standby) {
            wait_state_ = GroupWaitState::active;
        }
        if(wait_state_ != GroupWaitState::active) return false;
        ++wait_elapsed_cycles_;
        if(wait_elapsed_cycles_ >= wait_duration_cycles_) {
            wait_elapsed_cycles_ = wait_duration_cycles_;
            wait_state_ = GroupWaitState::completed;
            start_next_queued();
        }
        return true;
    }

    void abort_wait()
    {
        if(wait_state_ == GroupWaitState::queued ||
           wait_state_ == GroupWaitState::stopping ||
           wait_state_ == GroupWaitState::active) {
            last_aborted_wait_id_ = wait_command_id_;
            wait_state_ = GroupWaitState::aborted;
        }
    }

    bool wait_blocks_motion_start() const
    {
        return wait_state_ == GroupWaitState::queued ||
               wait_state_ == GroupWaitState::stopping ||
               wait_state_ == GroupWaitState::active;
    }

    void abort_halt()
    {
        if(halt_command_live_) {
            if(halt_command_done(halt_command_id_)) {
                last_completed_halt_id_ = halt_command_id_;
            } else {
                last_aborted_halt_id_ = halt_command_id_;
            }
            halt_command_live_ = false;
        }
    }
    bool solve_tracking_tcp(const geom::RigidTransform &tcp)
    {
        double seed[MaxAxes] = {};
        double solved[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            seed[i] = axes_[i]->snapshot().command_position;
            solved[i] = seed[i];
        }
        rt::ErrorCode result = rt::ErrorCode::ok;
        if(pose_frames_.pose_kinematics_ != nullptr) {
            const geom::RigidTransform flange =
                geom::compose(tcp, pose_frames_.active_pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange.translation.x;
            pose.position[1] = flange.translation.y;
            pose.position[2] = flange.translation.z;
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    pose.rotation[row][column] = flange.rotation[row][column];
                }
            }
            result = pose_frames_.pose_kinematics_->inverse(
                pose, seed, pose_frames_.pose_max_joint_step_, solved);
        } else {
            const geom::Vec3 flange = tcp.translation - pose_frames_.active_tool_offset_;
            if(pose_frames_.kinematics_ != nullptr) {
                result = pose_frames_.kinematics_->inverse(flange, seed, axes_.size(), solved);
            } else {
                if(axes_.size() > 0) solved[0] = flange.x;
                if(axes_.size() > 1) solved[1] = flange.y;
                if(axes_.size() > 2) solved[2] = flange.z;
            }
        }
        if(result != rt::ErrorCode::ok) return false;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double previous_velocity = axes_[i]->snapshot().command_velocity;
            const double velocity = solved[i] - seed[i];
            axes_[i]->set_synchronized_state(
                solved[i], velocity, velocity - previous_velocity);
        }
        return true;
    }

    bool current_tracking_tcp(geom::RigidTransform &tcp) const
    {
        double joints[MaxAxes] = {};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            joints[i] = axes_[i]->snapshot().command_position;
        }
        if(pose_frames_.pose_kinematics_ != nullptr) {
            kin::Pose6 flange{};
            pose_frames_.pose_kinematics_->forward(joints, flange);
            geom::RigidTransform flange_transform{};
            flange_transform.translation =
                {flange.position[0], flange.position[1], flange.position[2]};
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    flange_transform.rotation[row][column] = flange.rotation[row][column];
                }
            }
            tcp = geom::compose(flange_transform, pose_frames_.active_pose_tool_);
            return true;
        }
        geom::Vec3 point{};
        if(pose_frames_.kinematics_ != nullptr) {
            if(pose_frames_.kinematics_->forward(joints, axes_.size(), point) != rt::ErrorCode::ok) {
                return false;
            }
        } else {
            if(axes_.size() > 0) point.x = joints[0];
            if(axes_.size() > 1) point.y = joints[1];
            if(axes_.size() > 2) point.z = joints[2];
        }
        tcp.translation = point + pose_frames_.active_tool_offset_;
        return true;
    }

    bool apply_dynamic_tracking_to_active()
    {
        if(!active_command_.dynamic_pcs) return true;
        geom::RigidTransform base_tcp{};
        if(!current_tracking_tcp(base_tcp)) return false;
        const geom::RigidTransform delta = geom::compose(
            pose_frames_.workpiece_frame_, geom::invert(pose_frames_.active_dynamic_reference_frame_));
        const geom::RigidTransform tracked_tcp = geom::compose(delta, base_tcp);
        if(!solve_tracking_tcp(tracked_tcp)) return false;
        pose_frames_.tracking_hold_pose_ =
            geom::compose(geom::invert(pose_frames_.workpiece_frame_), tracked_tcp);
        tracking_following_ = true;
        tracking_motion_seen_ = true;
        return true;
    }

    void apply_tracking_hold()
    {
        if(!tracking_following_ || tracking_kind_ == TrackingKind::none || active_ ||
           joint_window_.active_ || cartesian_.window_active_ || direct_path_.direct_active_ || jog_active_ ||
           group_sync_active_) {
            return;
        }
        const geom::RigidTransform target =
            geom::compose(pose_frames_.workpiece_frame_, pose_frames_.tracking_hold_pose_);
        if(!solve_tracking_tcp(target)) {
            tracking_error_ = rt::ErrorCode::precondition_failed;
            set_group_error(tracking_error_);
        }
    }

    void update_path_odometer()
    {
        if(!path_odometer_initialized_) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                path_odometer_position_[i] = axes_[i]->snapshot().command_position;
                path_odometer_member_velocity_[i] = 0.0;
            }
            path_odometer_initialized_ = true;
            return;
        }
        double squared_distance = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double position = axes_[i]->snapshot().command_position;
            const double delta = position - path_odometer_position_[i];
            squared_distance += delta * delta;
            path_odometer_member_velocity_[i] = delta;
            path_odometer_position_[i] = position;
        }
        const double distance = std::sqrt(squared_distance);
        path_odometer_acceleration_ = distance - path_odometer_velocity_;
        path_odometer_velocity_ = distance;
        path_odometer_ += distance;
    }

    static bool valid_tracking_pose(const ToolData &pose)
    {
        for(double value : pose.value) {
            if(!std::isfinite(value)) return false;
        }
        return true;
    }

    static geom::RigidTransform tracking_pose(const ToolData &pose)
    {
        return geom::make_rpy_transform(pose.value[0], pose.value[1], pose.value[2],
                                        pose.value[3], pose.value[4], pose.value[5]);
    }

    rt::Result<std::uint32_t> begin_tracking(TrackingKind kind,
                                             CoordSystem coord_system,
                                             BufferMode buffer_mode)
    {
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           coord_system != CoordSystem::pcs || buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(tracking_kind_ != TrackingKind::none) {
            last_aborted_tracking_id_ = tracking_command_id_;
        }
        tracking_kind_ = kind;
        tracking_command_id_ = next_command_id_++;
        tracking_master_group_ = nullptr;
        tracking_master_axis_ = nullptr;
        tracking_motion_seen_ = status_ == GroupStatus::moving;
        tracking_following_ = false;
        tracking_error_ = rt::ErrorCode::ok;
        return rt::Result<std::uint32_t>::success(tracking_command_id_);
    }

    void cancel_tracking()
    {
        if(tracking_kind_ == TrackingKind::none) return;
        last_aborted_tracking_id_ = tracking_command_id_;
        tracking_kind_ = TrackingKind::none;
        tracking_command_id_ = 0;
        tracking_master_group_ = nullptr;
        tracking_master_axis_ = nullptr;
        tracking_motion_seen_ = false;
        tracking_following_ = false;
        tracking_error_ = rt::ErrorCode::ok;
    }
    void set_tracking_frame(const geom::RigidTransform &frame)
    {
        pose_frames_.workpiece_frame_ = frame;
        pose_frames_.workpiece_frame_rpy_[0] = frame.translation.x;
        pose_frames_.workpiece_frame_rpy_[1] = frame.translation.y;
        pose_frames_.workpiece_frame_rpy_[2] = frame.translation.z;
        geom::extract_rpy(frame.rotation, pose_frames_.workpiece_frame_rpy_[3],
                          pose_frames_.workpiece_frame_rpy_[4], pose_frames_.workpiece_frame_rpy_[5]);
    }
    void update_tracking_transform()
    {
        if(tracking_kind_ == TrackingKind::none) return;
        if(status_ == GroupStatus::moving) tracking_motion_seen_ = true;

        if(tracking_kind_ == TrackingKind::dynamic_group) {
            GroupPosition master_position{};
            if(tracking_master_group_ == nullptr ||
               tracking_master_group_->read_cartesian(
                   CoordSystem::mcs, PositionSource::actual, master_position) !=
                   rt::ErrorCode::ok ||
               master_position.size < 3) {
                tracking_error_ = rt::ErrorCode::precondition_failed;
                return;
            }
            ToolData master_pose{};
            for(std::size_t i = 0; i < 6 && i < master_position.size; ++i) {
                master_pose.value[i] = master_position.value[i];
            }
            set_tracking_frame(
                geom::compose(tracking_pose(master_pose), tracking_pose(tracking_transform_)));
            return;
        }

        if(tracking_master_axis_ == nullptr || !tracking_master_axis_->powered() ||
           tracking_master_axis_->status() == AxisStatus::errorstop) {
            tracking_error_ = rt::ErrorCode::precondition_failed;
            return;
        }
        const double delta = tracking_master_axis_->snapshot().actual_position -
                             tracking_master_origin_;
        const geom::RigidTransform origin = tracking_pose(tracking_origin_);
        const geom::RigidTransform object = tracking_pose(tracking_transform_);
        const geom::RigidTransform motion =
            tracking_kind_ == TrackingKind::conveyor
                ? geom::make_rpy_transform(delta, 0.0, 0.0, 0.0, 0.0, 0.0)
                : geom::make_rpy_transform(0.0, 0.0, 0.0, 0.0, 0.0, delta);
        set_tracking_frame(geom::compose(geom::compose(origin, motion), object));
    }

    static double move_towards(double current, double target, double maximum_step)
    {
        if(maximum_step <= 0.0) return target;
        if(target > current + maximum_step) return current + maximum_step;
        if(target < current - maximum_step) return current - maximum_step;
        return target;
    }

    void cycle_axis_to_group_sync()
    {
        if(path_sync_slave_ == nullptr) return;
        if(path_sync_slave_->sync_kind_ != SyncKind::group_path ||
           path_sync_slave_->sync_id_ != path_sync_id_) {
            path_sync_slave_ = nullptr;
            path_sync_id_ = 0;
            path_sync_in_sync_ = false;
            return;
        }

        const double desired_position =
            path_sync_slave_origin_ + (path_odometer_ - path_sync_origin_) * path_sync_ratio_;
        const double desired_velocity = path_odometer_velocity_ * path_sync_ratio_;
        if(path_sync_acceleration_ == 0.0 && path_sync_deceleration_ == 0.0 &&
           path_sync_jerk_ == 0.0) {
            path_sync_velocity_ = desired_velocity;
            path_sync_acceleration_state_ = path_odometer_acceleration_ * path_sync_ratio_;
            path_sync_in_sync_ = true;
        } else if(!path_sync_in_sync_) {
            const double position_error =
                desired_position - path_sync_slave_->snapshot().command_position;
            const double entry_velocity = desired_velocity + position_error;
            const double acceleration_limit =
                std::fabs(entry_velocity) >= std::fabs(path_sync_velocity_)
                    ? path_sync_acceleration_
                    : path_sync_deceleration_;
            const double requested_acceleration = entry_velocity - path_sync_velocity_;
            const double jerk_limited = move_towards(
                path_sync_acceleration_state_, requested_acceleration, path_sync_jerk_);
            path_sync_acceleration_state_ =
                move_towards(0.0, jerk_limited, acceleration_limit);
            path_sync_velocity_ += path_sync_acceleration_state_;
            const bool velocity_reached =
                std::fabs(path_sync_velocity_ - desired_velocity) <= 1e-12;
            const bool position_reached =
                std::fabs(position_error) <= std::fabs(path_sync_velocity_) + 1e-12;
            path_sync_in_sync_ = velocity_reached && position_reached;
            if(path_sync_in_sync_) {
                path_sync_slave_->sync_phase_ = SyncPhase::engaged;
            }
        }

        double position = desired_position;
        if(!path_sync_in_sync_) {
            position = path_sync_slave_->snapshot().command_position + path_sync_velocity_;
        }
        path_sync_slave_->snapshot_.active_command_id = path_sync_id_;
        path_sync_slave_->set_synchronized_state(
            position, path_sync_velocity_, path_sync_acceleration_state_);
    }

    void cycle_group_to_axis_sync()
    {
        if(group_sync_master_ == nullptr || !group_sync_master_->powered() ||
           group_sync_master_->status() == AxisStatus::errorstop) {
            last_aborted_group_sync_id_ = group_sync_id_;
            group_sync_active_ = false;
            set_group_error(rt::ErrorCode::precondition_failed);
            return;
        }
        for(std::size_t axis_index = 0; axis_index < axes_.size(); ++axis_index) {
            if(!axes_[axis_index]->powered() ||
               axes_[axis_index]->status() == AxisStatus::errorstop) {
                last_aborted_group_sync_id_ = group_sync_id_;
                group_sync_active_ = false;
                set_group_error(rt::ErrorCode::precondition_failed);
                return;
            }
        }

        const double total = group_sync_cumulative_[group_sync_count_ - 1];
        double path_position =
            group_sync_master_->snapshot().command_position - group_sync_master_origin_;
        if(group_sync_mode_ == PathMode::periodic) {
            path_position = std::fmod(path_position, total);
            if(path_position < 0.0) path_position += total;
        } else {
            if(path_position < 0.0) path_position = 0.0;
            if(path_position > total) path_position = total;
        }

        std::size_t segment = 1;
        while(segment + 1 < group_sync_count_ &&
              path_position > group_sync_cumulative_[segment]) {
            ++segment;
        }
        const double begin = group_sync_cumulative_[segment - 1];
        const double length = group_sync_cumulative_[segment] - begin;
        const double ratio = length > 0.0 ? (path_position - begin) / length : 1.0;
        for(std::size_t axis_index = 0; axis_index < axes_.size(); ++axis_index) {
            const double position =
                group_sync_waypoints_[segment - 1].value[axis_index] +
                (group_sync_waypoints_[segment].value[axis_index] -
                 group_sync_waypoints_[segment - 1].value[axis_index]) *
                    ratio;
            const double previous_velocity = axes_[axis_index]->snapshot().command_velocity;
            const double velocity = position - group_sync_previous_.value[axis_index];
            axes_[axis_index]->set_synchronized_state(
                position, velocity, velocity - previous_velocity);
            group_sync_previous_.value[axis_index] = position;
        }
    }

    static bool any_direction(const GroupPosition &direction)
    {
        for(std::size_t i = 0; i < direction.size; ++i) {
            if(direction.value[i] != 0.0) return true;
        }
        return false;
    }

    rt::ErrorCode validate_jog(CoordSystem coord_system,
                               const GroupPosition &direction,
                               const JogCommandOptions &options) const
    {
        if(!std::isfinite(options.velocity_override) ||
           options.velocity_override < 0.0 || options.velocity_override > 1.0 ||
           !std::isfinite(options.acceleration_override) ||
           options.acceleration_override < 0.0 ||
           options.acceleration_override > 1.0 ||
           !std::isfinite(options.max_linear_distance) ||
           options.max_linear_distance < 0.0 ||
           !std::isfinite(options.max_angular_distance) ||
           options.max_angular_distance < 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(axes_.empty() || direction.size == 0 || !finite(direction)) {
            return rt::ErrorCode::invalid_argument;
        }
        if(jogging_dynamics_.size != axes_.size()) {
            return rt::ErrorCode::precondition_failed;
        }
        const PathDynamics &path = jogging_dynamics_.path;
        if(!std::isfinite(path.velocity) || !std::isfinite(path.acceleration) ||
           !std::isfinite(path.deceleration) || !std::isfinite(path.jerk) ||
           path.velocity <= 0.0 || path.acceleration <= 0.0 ||
           path.deceleration <= 0.0 || path.jerk <= 0.0) {
            return rt::ErrorCode::precondition_failed;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!std::isfinite(jogging_dynamics_.axis_velocity[i]) ||
               !std::isfinite(jogging_dynamics_.axis_acceleration[i]) ||
               !std::isfinite(jogging_dynamics_.axis_deceleration[i]) ||
               !std::isfinite(jogging_dynamics_.axis_jerk[i]) ||
               jogging_dynamics_.axis_velocity[i] <= 0.0 ||
               jogging_dynamics_.axis_acceleration[i] <= 0.0 ||
               jogging_dynamics_.axis_deceleration[i] <= 0.0 ||
               jogging_dynamics_.axis_jerk[i] <= 0.0) {
                return rt::ErrorCode::precondition_failed;
            }
        }
        if(coord_system == CoordSystem::acs) {
            if(options.max_linear_distance > 0.0 ||
               options.max_angular_distance > 0.0) {
                return rt::ErrorCode::unsupported;
            }
            return direction.size == axes_.size() ? rt::ErrorCode::ok
                                                  : rt::ErrorCode::invalid_argument;
        }
        if(coord_system != CoordSystem::mcs && coord_system != CoordSystem::pcs) {
            return rt::ErrorCode::unsupported;
        }
        if(pose_frames_.kinematics_ == nullptr && pose_frames_.pose_kinematics_ == nullptr) {
            return rt::ErrorCode::precondition_failed;
        }
        if(options.max_angular_distance > 0.0 && pose_frames_.pose_kinematics_ == nullptr) {
            return rt::ErrorCode::unsupported;
        }
        return direction.size >= 3 ? rt::ErrorCode::ok
                                   : rt::ErrorCode::invalid_argument;
    }

    static double approach(double current, double target, double step)
    {
        if(current < target) return current + step > target ? target : current + step;
        if(current > target) return current - step < target ? target : current - step;
        return current;
    }

    void advance_jog_state(std::size_t index, double target_velocity)
    {
        const double acceleration_limit =
            target_velocity == 0.0
                ? (jog_stop_deceleration_ > 0.0 ? jog_stop_deceleration_
                                                : jogging_dynamics_.axis_deceleration[index] *
                                                      jog_options_.acceleration_override)
                : jogging_dynamics_.axis_acceleration[index] *
                      jog_options_.acceleration_override;
        const double desired_acceleration =
            target_velocity > jog_velocity_[index]
                ? acceleration_limit
                : (target_velocity < jog_velocity_[index] ? -acceleration_limit : 0.0);
        jog_acceleration_[index] =
            approach(jog_acceleration_[index], desired_acceleration,
                     jog_stop_jerk_ > 0.0 && target_velocity == 0.0
                         ? jog_stop_jerk_
                         : jogging_dynamics_.axis_jerk[index]);
        const double previous = jog_velocity_[index];
        jog_velocity_[index] += jog_acceleration_[index];
        if((target_velocity - previous) * (target_velocity - jog_velocity_[index]) <= 0.0) {
            jog_velocity_[index] = target_velocity;
            jog_acceleration_[index] = 0.0;
        }
    }

    bool all_jog_stopped() const
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(jog_velocity_[i] != 0.0 || jog_acceleration_[i] != 0.0) return false;
        }
        return true;
    }

    void advance_cart_jog_state(std::size_t index, double target_velocity)
    {
        const double acceleration_limit =
            target_velocity == 0.0 && jog_stop_deceleration_ > 0.0
                ? jog_stop_deceleration_
                : (target_velocity == 0.0
                       ? jogging_dynamics_.path.deceleration *
                             jog_options_.acceleration_override
                       : jogging_dynamics_.path.acceleration *
                             jog_options_.acceleration_override);
        const double desired = target_velocity > jog_cart_velocity_[index]
                                   ? acceleration_limit
                                   : (target_velocity < jog_cart_velocity_[index]
                                          ? -acceleration_limit
                                          : 0.0);
        const double jerk = target_velocity == 0.0 && jog_stop_jerk_ > 0.0
                                ? jog_stop_jerk_
                                : jogging_dynamics_.path.jerk;
        jog_cart_acceleration_[index] =
            approach(jog_cart_acceleration_[index], desired, jerk);
        const double previous = jog_cart_velocity_[index];
        jog_cart_velocity_[index] += jog_cart_acceleration_[index];
        if((target_velocity - previous) *
               (target_velocity - jog_cart_velocity_[index]) <=
           0.0) {
            jog_cart_velocity_[index] = target_velocity;
            jog_cart_acceleration_[index] = 0.0;
        }
    }

    bool cart_jog_stopped() const
    {
        for(std::size_t i = 0; i < jog_cart_velocity_.size(); ++i) {
            if(jog_cart_velocity_[i] != 0.0 || jog_cart_acceleration_[i] != 0.0) {
                return false;
            }
        }
        return true;
    }

    void apply_jog_distance_limits(GroupPosition &next)
    {
        if(jog_options_.max_linear_distance > 0.0 && next.size >= 3) {
            const double dx = next.value[0] - jog_cartesian_start_[0];
            const double dy = next.value[1] - jog_cartesian_start_[1];
            const double dz = next.value[2] - jog_cartesian_start_[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if(distance >= jog_options_.max_linear_distance && distance > 0.0) {
                const double scale = jog_options_.max_linear_distance / distance;
                next.value[0] = jog_cartesian_start_[0] + dx * scale;
                next.value[1] = jog_cartesian_start_[1] + dy * scale;
                next.value[2] = jog_cartesian_start_[2] + dz * scale;
                for(std::size_t i = 0; i < 3; ++i) {
                    jog_direction_.value[i] = 0.0;
                    jog_cart_velocity_[i] = 0.0;
                    jog_cart_acceleration_[i] = 0.0;
                }
            }
        }
        if(jog_options_.max_angular_distance > 0.0 && next.size >= 6) {
            const geom::RigidTransform start = geom::make_rpy_transform(
                0.0, 0.0, 0.0, jog_cartesian_start_[3],
                jog_cartesian_start_[4], jog_cartesian_start_[5]);
            geom::RigidTransform candidate = geom::make_rpy_transform(
                0.0, 0.0, 0.0, next.value[3], next.value[4], next.value[5]);
            double axis[3] = {};
            double angle = 0.0;
            geom::relative_axis_angle(start.rotation, candidate.rotation, axis, angle);
            if(angle >= jog_options_.max_angular_distance && angle > 0.0) {
                double delta[3][3] = {};
                geom::rodrigues(axis, jog_options_.max_angular_distance, delta);
                geom::rotation_multiply(start.rotation, delta, candidate.rotation);
                geom::extract_rpy(candidate.rotation, next.value[3], next.value[4],
                                  next.value[5]);
                for(std::size_t i = 3; i < 6; ++i) {
                    jog_direction_.value[i] = 0.0;
                    jog_cart_velocity_[i] = 0.0;
                    jog_cart_acceleration_[i] = 0.0;
                }
            }
        }
    }

    bool solve_jog_cartesian(GroupPosition &target)
    {
        double q[MaxAxes] = {};
        const bool pcs = jog_coord_system_ == CoordSystem::pcs;
        if(pose_frames_.pose_kinematics_ != nullptr) {
            geom::RigidTransform tcp = geom::make_rpy_transform(
                target.value[0], target.value[1], target.value[2], target.value[3],
                target.value[4], target.value[5]);
            if(pcs) tcp = geom::compose(pose_frames_.workpiece_frame_, tcp);
            const geom::RigidTransform flange = geom::compose(tcp, pose_frames_.jog_pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange.translation.x;
            pose.position[1] = flange.translation.y;
            pose.position[2] = flange.translation.z;
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    pose.rotation[row][column] = flange.rotation[row][column];
                }
            }
            const rt::ErrorCode solved = pose_frames_.pose_kinematics_->inverse(
                pose, jog_position_.data(), pose_frames_.pose_max_joint_step_, q);
            if(solved != rt::ErrorCode::ok ||
               pose_frames_.pose_kinematics_->singularity_margin(q) < pose_frames_.pose_min_margin_) {
                jog_error_ = solved == rt::ErrorCode::ok
                                 ? rt::ErrorCode::precondition_failed
                                 : solved;
                return false;
            }
        } else {
            geom::Vec3 point = cartesian_part(target);
            if(pcs) point = geom::transform_point(pose_frames_.workpiece_frame_, point);
            point = point - pose_frames_.jog_tool_offset_;
            const rt::ErrorCode solved =
                pose_frames_.kinematics_->inverse(point, jog_position_.data(), axes_.size(), q);
            if(solved != rt::ErrorCode::ok ||
               pose_frames_.kinematics_->singularity_margin(q, axes_.size()) <
                   pose_frames_.kinematics_min_margin_) {
                jog_error_ = solved == rt::ErrorCode::ok
                                 ? rt::ErrorCode::precondition_failed
                                 : solved;
                return false;
            }
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->target_inside_limits(q[i])) {
                jog_error_ = rt::ErrorCode::out_of_range;
                return false;
            }
            jog_position_[i] = q[i];
        }
        return true;
    }

    void jog_cycle()
    {
        if(jog_coord_system_ == CoordSystem::acs) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double target = jog_direction_.value[i] *
                                      jogging_dynamics_.axis_velocity[i] *
                                      jog_options_.velocity_override;
                advance_jog_state(i, target);
                const double next = jog_position_[i] + jog_velocity_[i];
                if(!axes_[i]->target_inside_limits(next)) {
                    jog_error_ = rt::ErrorCode::out_of_range;
                    for(std::size_t slot = 0; slot < jog_direction_.size; ++slot) {
                        jog_direction_.value[slot] = 0.0;
                    }
                    jog_releasing_ = true;
                    advance_jog_state(i, 0.0);
                } else {
                    jog_position_[i] = next;
                }
                axes_[i]->set_synchronized_state(jog_position_[i], jog_velocity_[i],
                                                 jog_acceleration_[i]);
            }
        } else {
            double linear_norm = 0.0;
            double angular_norm = 0.0;
            for(std::size_t i = 0; i < 3 && i < jog_direction_.size; ++i) {
                linear_norm += jog_direction_.value[i] * jog_direction_.value[i];
            }
            for(std::size_t i = 3; i < 6 && i < jog_direction_.size; ++i) {
                angular_norm += jog_direction_.value[i] * jog_direction_.value[i];
            }
            linear_norm = std::sqrt(linear_norm);
            angular_norm = std::sqrt(angular_norm);
            const double linear_scale = linear_norm > 1.0 ? 1.0 / linear_norm : 1.0;
            const double angular_scale = angular_norm > 1.0 ? 1.0 / angular_norm : 1.0;
            for(std::size_t i = 0; i < jog_cart_velocity_.size(); ++i) {
                const double scale = i < 3 ? linear_scale : angular_scale;
                const double component = i < jog_direction_.size
                                             ? jog_direction_.value[i] * scale
                                             : 0.0;
                advance_cart_jog_state(
                    i, component * jogging_dynamics_.path.velocity *
                           jog_options_.velocity_override);
            }
            GroupPosition next = jog_cartesian_position_;
            for(std::size_t i = 0; i < 6 && i < next.size; ++i) {
                next.value[i] += jog_cart_velocity_[i];
            }
            apply_jog_distance_limits(next);
            if(!solve_jog_cartesian(next)) {
                jog_direction_ = {};
                jog_direction_.size = next.size;
                jog_releasing_ = true;
            } else {
                jog_cartesian_position_ = next;
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double previous_position = axes_[i]->snapshot().command_position;
                const double previous_velocity = axes_[i]->snapshot().command_velocity;
                const double velocity = jog_position_[i] - previous_position;
                axes_[i]->set_synchronized_state(jog_position_[i], velocity,
                                                 velocity - previous_velocity);
            }
        }
        const bool stopped = jog_coord_system_ == CoordSystem::acs
                                 ? all_jog_stopped()
                                 : cart_jog_stopped();
        if(jog_releasing_ && stopped) {
            jog_active_ = false;
            jog_releasing_ = false;
            for(std::size_t i = 0; i < axes_.size(); ++i) axes_[i]->clear_synchronized();
            status_ = GroupStatus::standby;
            return;
        }
        status_ = any_direction(jog_direction_) || !stopped
                      ? (jog_releasing_ ? GroupStatus::stopping : GroupStatus::moving)
                      : GroupStatus::standby;
    }
    void apply_selected_tool()
    {
        const ToolData &tool = tools_[selected_tool_];
        pose_frames_.pose_tool_ = geom::make_rpy_transform(tool.value[0], tool.value[1], tool.value[2],
                                              tool.value[3], tool.value[4], tool.value[5]);
        pose_frames_.pose_tool_inverse_ = geom::invert(pose_frames_.pose_tool_);
        pose_frames_.tool_offset_ = {tool.value[0], tool.value[1], tool.value[2]};
        for(std::size_t i = 0; i < tool.value.size(); ++i) {
            pose_frames_.tool_transform_rpy_[i] = tool.value[i];
        }
    }
    void snapshot_selections()
    {
        active_tool_ = selected_tool_;
        active_payload_ = selected_payload_;
    }
    void snapshot_active_tool_transform();
    bool active_tool_transform_applies() const;

    rt::ErrorCode preflight_member_targets(const GroupPosition &target) const
    {
        if(target.size != axes_.size()) return rt::ErrorCode::invalid_argument;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->target_inside_limits(target.value[i])) {
                return rt::ErrorCode::out_of_range;
            }
        }
        return rt::ErrorCode::ok;
    }

    bool configuration_writable() const
    {
        if(status_ != GroupStatus::disabled && status_ != GroupStatus::standby) {
            return false;
        }
        if(active_ || direct_path_.direct_active_ || joint_window_.active_ || cartesian_.window_active_ || jog_active_ ||
           !queue_.empty()) {
            return false;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(axes_[i]->status() == AxisStatus::synchronized_motion ||
               axes_[i]->has_standalone_motion()) {
                return false;
            }
        }
        return true;
    }

    static rt::ErrorCode apply_path_update(PathDynamics &target,
                                           const PathDynamics &update)
    {
        const double values[4] = {update.velocity, update.acceleration,
                                  update.deceleration, update.jerk};
        for(double value : values) {
            if(!std::isfinite(value)) return rt::ErrorCode::invalid_argument;
        }
        if(update.velocity > 0.0) target.velocity = update.velocity;
        if(update.acceleration > 0.0) target.acceleration = update.acceleration;
        if(update.deceleration > 0.0) target.deceleration = update.deceleration;
        if(update.jerk > 0.0) target.jerk = update.jerk;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode update_path_dynamics(PathDynamics &target,
                                       const PathDynamics &update)
    {
        if(!configuration_writable()) return rt::ErrorCode::precondition_failed;
        PathDynamics next = target;
        const rt::ErrorCode result = apply_path_update(next, update);
        if(result == rt::ErrorCode::ok) target = next;
        return result;
    }

    rt::ErrorCode resolve_dynamics(GroupCommand &command) const
    {
        if(command.use_default_dynamics) {
            command.velocity = default_dynamics_.velocity;
            command.acceleration = default_dynamics_.acceleration;
            command.deceleration = default_dynamics_.deceleration;
            command.jerk = default_dynamics_.jerk;
            return rt::ErrorCode::ok;
        }
        if(dynamics_mode_ == DynamicsMode::percentage) {
            if(!std::isfinite(command.velocity) || !std::isfinite(command.acceleration) ||
               !std::isfinite(command.deceleration) || !std::isfinite(command.jerk) ||
               command.velocity < 0.0 || command.velocity > 100.0 ||
               command.acceleration < 0.0 || command.acceleration > 100.0 ||
               command.deceleration < 0.0 || command.deceleration > 100.0 ||
               command.jerk < 0.0 || command.jerk > 100.0) {
                return rt::ErrorCode::invalid_argument;
            }
            command.velocity = reference_dynamics_.velocity * command.velocity / 100.0;
            command.acceleration =
                reference_dynamics_.acceleration * command.acceleration / 100.0;
            command.deceleration =
                reference_dynamics_.deceleration * command.deceleration / 100.0;
            command.jerk = reference_dynamics_.jerk * command.jerk / 100.0;
        }
        return rt::ErrorCode::ok;
    }

    void abort_direct_members();
    rt::ErrorCode start_direct(const GroupCommand &command);
    void complete_direct(std::uint32_t command_id);
    void abort_direct(std::uint32_t command_id);
    rt::ErrorCode stop_direct_members(double deceleration, double jerk);

    bool members_ready_for_group_motion() const
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const AxisStatus member_status = axes_[i]->status();
            if(!axes_[i]->powered() || member_status == AxisStatus::errorstop ||
               (status_ == GroupStatus::standby &&
                (member_status != AxisStatus::standstill ||
                 axes_[i]->has_standalone_motion()))) {
                return false;
            }
        }
        return true;
    }

    std::size_t find(const AxisModel &axis) const
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(axes_[i] == &axis) {
                return i;
            }
        }
        return axes_.size();
    }

    std::size_t find(IdentInGroup ident) const
    {
        for(std::size_t i = 0; i < member_idents_.size(); ++i) {
            if(member_idents_[i].index == ident.index) {
                return i;
            }
        }
        return member_idents_.size();
    }

    static bool finite(const GroupPosition &position)
    {
        for(std::size_t i = 0; i < position.size; ++i) {
            if(!std::isfinite(position.value[i])) {
                return false;
            }
        }
        return true;
    }

    static geom::Vec3 cartesian_part(const GroupPosition &position);

    rt::ErrorCode select_orientation_interpolation(GroupCommand &command) const;

    static void store_cartesian_part(GroupPosition &position, geom::Vec3 point);
    // Approved coordinate matrix (B1 v1): MCS/PCS targets convert to ACS at
    // submit time on the first three coordinates (higher axes pass through in
    // ACS); ACS commands never see the frames. Absolute points go through the
    // workpiece frame (PCS) and then subtract the tool offset (MCS and PCS);
    // relative distances only rotate — translation and tool offset cancel
    // between two TCP positions. The v1 ACS<->MCS mapping is the declared
    // identity (Cartesian rig; kinematics plugins arrive with B2).
    rt::ErrorCode apply_coordinate_frame(GroupCommand &command) const;

    // One Cartesian target through frame, tool offset, and inverse solution.
    // Relative displacements only rotate (translation and tool offset cancel
    // between two TCP positions) and resolve against the flange position of
    // the seed joints.
    rt::ErrorCode solve_cartesian_target(GroupPosition &position,
                                         bool relative,
                                         bool pcs,
                                         const double *seed) const;

    // Cartesian-interpolation batch (approved matrix decisions #1-#6 and
    // the approved v2 addendum): submit-side resolution of opt-in segments.
    // Undefined combinations reject, 33 chord samples run the seed-chained
    // inverse (the step gate lives inside the inverse; margin checked per
    // sample), the command velocity scales so the worst per-cycle joint
    // step stays inside half the step gate (pose pipeline; translational
    // groups are bounded by the margin entry ban), and the ACS endpoint
    // joints plus the cycle geometry stay behind.
    // Cartesian v3 window (approved addendum, KB-050): consecutive
    // Cartesian blending successors on translational plugin groups form a
    // look-ahead window in the plugin Cartesian space — lines joined by
    // quintic corners, node velocities from the bidirectional jerk-exact
    // scan capped by corner curvature, one jerk-limited profile per line,
    // corners ridden at constant node velocity (terminal sub-cycle
    // quantization declared). The cycle path samples the window geometry
    // and runs one analytic inverse (KB-044 machinery); failures are the
    // declared group errorstop.
    static constexpr std::size_t CartWindowPieces =
        GroupCartesianState<MaxAxes>::WindowCapacity;
    using CartPiece = GroupCartesianState<MaxAxes>::Piece;

    rt::Result<std::uint32_t> submit_cartesian_window(GroupCommand command);
    void cart_window_convert(const GroupCommand &command,
                             double trim,
                             bool passthrough);
    bool cart_window_rebuild();
    geom::Vec3 cart_piece_point(const CartPiece &piece, double s) const;
    bool cart_window_emit(geom::Vec3 point);
    void cart_window_cycle();
    geom::Vec3 cart_window_point_at(double composite) const;
    rt::ErrorCode cart_window_stop(double deceleration, double jerk);
    void cart_window_reset();
    rt::Result<std::uint32_t> submit_cartesian_blend(GroupCommand command);
    rt::ErrorCode prepare_cartesian_linear(GroupCommand &command);
    rt::ErrorCode prepare_cartesian_circular(GroupCommand &command);
    rt::ErrorCode cartesian_guards(const GroupCommand &command) const;
    void segment_start_joints(const GroupCommand &command, double *chain) const;
    geom::RigidTransform pose_start_tcp(const double *chain) const;
    rt::ErrorCode prevalidate_cartesian(GroupCommand &command,
                                        CartesianSegment &segment,
                                        double *chain);
    geom::Vec3 cartesian_point_at(const CartesianSegment &segment,
                                  double fraction) const;
    void cartesian_pose_at(const CartesianSegment &segment,
                           double fraction,
                           geom::RigidTransform &tcp) const;
    bool cartesian_cycle(double ratio);
    double queued_finish(std::size_t axis_index) const
    {
        double finish =
            joint_window_.active_
                ? joint_window_.segments_[joint_window_.segments_.size() - 1].target[axis_index]
                : (cartesian_.window_active_
                       ? cart_tail_joints_[axis_index]
                       : (direct_path_.direct_active_
                              ? active_finish_[axis_index]
                              : (active_ ? active_finish_[axis_index]
                                         : axes_[axis_index]->snapshot().command_position)));
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            const GroupCommand &queued = queue_[i];
            if(queued.kind != GroupCommandKind::motion &&
               queued.kind != GroupCommandKind::direct) continue;
            finish = queued.relative ? finish + queued.target.value[axis_index]
                                     : queued.target.value[axis_index];
        }
        return finish;
    }

    GroupCommand normalize(GroupCommand command) const
    {
        if(!command.relative) {
            return command;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            command.target.value[i] = queued_finish(i) + command.target.value[i];
        }
        command.relative = false;
        return command;
    }

    rt::ErrorCode start(GroupCommand command)
    {
        active_tool_ = command.tool_number;
        active_payload_ = command.payload_number;
        pose_frames_.active_pose_tool_inverse_ = command.tool_inverse;
        pose_frames_.active_pose_tool_ = geom::invert(command.tool_inverse);
        pose_frames_.active_tool_offset_ = {pose_frames_.active_pose_tool_.translation.x,
                               pose_frames_.active_pose_tool_.translation.y,
                               pose_frames_.active_pose_tool_.translation.z};
        active_command_ = command;
        if(command.dynamic_pcs && pending_dynamic_pcs_) {
            pose_frames_.active_dynamic_reference_frame_ = pose_frames_.pending_dynamic_reference_frame_;
        }
        pending_dynamic_pcs_ = false;
        if(!command.dynamic_pcs) tracking_following_ = false;
        active_tick_ = 0;
        connector_.deactivate();
        double longest = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            active_start_[i] = axes_[i]->snapshot().command_position;
            active_finish_[i] = command.target.value[i];
            const double distance = std::fabs(active_finish_[i] - active_start_[i]);
            if(distance > longest) {
                longest = distance;
            }
        }

        // Shared scalar path: one jerk-limited 1D profile drives the path
        // parameter, so members stay on the commanded geometry by construction
        // and the group honors the full command dynamics (KB-027). Linear
        // paths reference the longest member travel; circular paths use the
        // in-plane arc length (approved circular matrix).
        active_kind_ = command.path_kind;
        active_arc_ = command.arc;
        if(command.path_kind == GroupPathKind::circular) {
            longest = command.arc.length;
        }
        cartesian_.active_segment_ = command.cart;
        if(command.path_kind == GroupPathKind::cartesian_linear) {
            longest = command.cart.length;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                cartesian_.joints_[i] = active_start_[i];
            }
        }
        active_path_length_ = longest;

        // Y7/Y7b1/Y7b2a: joint-domain linear/circular targets may accept a
        // captured member state from a plain Cartesian LINE source; Cartesian
        // targets never consume this connector.
        const bool connector_target =
            (command.path_kind == GroupPathKind::linear ||
             command.path_kind == GroupPathKind::circular) &&
            !command.dynamic_pcs;
        const bool vector_capture = connector_.captured_vector_state();
        const bool try_connector = connector_.take_captured_velocity() &&
                                   connector_target;

        if(try_connector) {
            const otg::Limits1D limits{
                command.velocity, command.acceleration, command.deceleration,
                command.jerk};
            rt::ErrorCode planned = rt::ErrorCode::ok;
            if(command.path_kind == GroupPathKind::circular) {
                planned = connector_.plan_circular(
                    limits, longest, axes_.size(), active_start_,
                    active_finish_, active_arc_, active_profile_,
                    active_duration_);
            } else if(vector_capture) {
                planned = connector_.plan_linear_vector(
                    limits, longest, axes_.size(), active_start_,
                    active_finish_, active_profile_, active_duration_);
            } else {
                planned = connector_.plan(
                    limits, longest, axes_.size(), active_start_,
                    active_finish_, active_profile_, active_duration_);
            }
            if(planned == rt::ErrorCode::ok) {
                active_ = true;
                status_ = GroupStatus::moving;
                return rt::ErrorCode::ok;
            }
            // Connector planning failed; fall through to the rest-start path.
            connector_.deactivate();
        }

        if(longest > 0.0) {
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {0.0, 0.0, 0.0},
                {longest, 0.0, 0.0},
                {command.velocity, command.acceleration, command.deceleration, command.jerk});
            if(!profile) {
                return profile.error();
            }
            active_profile_ = profile.value();
            active_duration_ = active_profile_.duration_cycles();
        } else {
            active_profile_ = otg::Profile1D{};
            active_duration_ = 1;
        }
        active_ = true;
        status_ = GroupStatus::moving;
        return rt::ErrorCode::ok;
    }

    void finish_active()
    {
        if(override_paused_) {
            return;
        }
        if(interrupting_) {
            const otg::State1D state =
                otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
            interrupt_ratio_ = state.position / active_path_length_;
            if(interrupt_ratio_ > 1.0) { interrupt_ratio_ = 1.0; }
        }
        if(!interrupting_ && active_command_.direct_semantics) {
            complete_direct(active_command_.command_id);
        }
        active_ = false;
        connector_.deactivate();
        if(!interrupting_) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                axes_[i]->clear_synchronized();
            }
            status_ = GroupStatus::standby;
            start_next_queued();
        }
    }

    void start_next_queued()
    {
        if(queue_.empty()) {
            return;
        }
        const GroupCommand next = queue_[0];
        for(std::size_t i = 1; i < queue_.size(); ++i) {
            queue_[i - 1] = queue_[i];
        }
        queue_.pop_back();
        if(next.kind == GroupCommandKind::motion) {
            start(next);
        } else if(next.kind == GroupCommandKind::direct) {
            const rt::ErrorCode started = start_direct(next);
            if(started != rt::ErrorCode::ok) abort_direct(next.command_id);
        } else {
            execute_management(next);
        }
    }

    void abort_motion()
    {
        if(direct_path_.direct_active_) abort_direct(direct_path_.direct_command_id_);
        if(active_ && active_command_.direct_semantics) {
            abort_direct(active_command_.command_id);
        }
        if(joint_window_.active_) {
            for(std::size_t i = joint_window_.index_; i < joint_window_.segments_.size(); ++i) {
                if(joint_window_.segments_[i].kind == WindowKind::direct_line) {
                    abort_direct(joint_window_.segments_[i].command_id);
                }
            }
        }
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            if(queue_[i].direct_semantics) {
                abort_direct(queue_[i].command_id);
            }
        }
        if(group_sync_active_) {
            last_aborted_group_sync_id_ = group_sync_id_;
            group_sync_active_ = false;
            group_sync_master_ = nullptr;
            group_sync_id_ = 0;
        }
        if(jog_active_) {
            last_aborted_jog_id_ = jog_command_id_;
            jog_active_ = false;
            jog_releasing_ = false;
        }
        active_ = false;
        connector_.deactivate();
        direct_path_.direct_active_ = false;
        direct_path_.direct_stopping_ = false;
        override_paused_ = false;
        interrupting_ = false;
        interrupted_plain_ = false;
        interrupted_window_ = false;
        window_reset();
        cart_window_reset();
        abort_queued_management();
        queue_.clear();
        active_tick_ = 0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
    }

    // Y7/Y7b1/Y7b2a: capture the live member state before abort_motion()
    // clears it. Cartesian capture is opt-in only for a plain joint
    // LINE/circular target and a plain Cartesian LINE source.
    void current_member_output_state(
        std::array<double, MaxAxes> &velocity,
        std::array<double, MaxAxes> &acceleration) const
    {
        velocity.fill(0.0);
        acceleration.fill(0.0);
        if(!path_odometer_initialized_) {
            return;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            velocity[i] = axes_[i]->snapshot().command_position -
                          path_odometer_position_[i];
            acceleration[i] =
                velocity[i] - path_odometer_member_velocity_[i];
        }
    }

    void capture_takeover_velocity(bool allow_cartesian_source = false)
    {
        if(active_command_.dynamic_pcs) {
            connector_.discard_capture();
            return;
        }
        if(active_kind_ == GroupPathKind::cartesian_linear) {
            if(!allow_cartesian_source ||
               cartesian_.active_segment_.arc_path ||
               cartesian_.active_segment_.chain ||
               !path_odometer_initialized_) {
                connector_.discard_capture();
                return;
            }
            std::array<double, MaxAxes> output_velocity{};
            std::array<double, MaxAxes> output_acceleration{};
            current_member_output_state(output_velocity, output_acceleration);
            connector_.capture_output_history(
                active_, output_velocity, output_acceleration, axes_.size());
            return;
        }
        if(active_kind_ == GroupPathKind::circular) {
            connector_.capture_circular(
                active_, active_arc_, active_path_length_, active_profile_,
                active_tick_, active_start_, active_finish_, axes_.size());
        } else {
            connector_.capture(active_, active_kind_ == GroupPathKind::linear,
                               active_path_length_, active_profile_, active_tick_,
                               active_start_, active_finish_, axes_.size());
        }
        std::array<double, MaxAxes> output_velocity{};
        std::array<double, MaxAxes> output_acceleration{};
        current_member_output_state(output_velocity, output_acceleration);
        connector_.set_captured_output_state(
            output_velocity, output_acceleration, axes_.size());
    }

    // A5 look-ahead window (KB-032, approved A5 matrix): consecutive blending
    // successors form a window of linear segments joined by quintic corner
    // curves. Node velocities come from a trapezoid-level bidirectional scan
    // capped by the corner curvature bound; every segment runs its own
    // jerk-limited profile between node velocities (the OTG nonzero-target
    // cruise domain), so straight parts are no longer dragged down to the
    // sharpest corner speed. All planning happens synchronously at submit;
    // the cycle path only samples precomputed data.
    static constexpr std::size_t BlendTableSize =
        GroupLookaheadWindow<MaxAxes>::BlendTableSize;
    static constexpr std::size_t WindowCapacity =
        GroupLookaheadWindow<MaxAxes>::Capacity;
    using WindowNode = GroupLookaheadWindow<MaxAxes>::Node;
    using WindowKind = GroupLookaheadWindow<MaxAxes>::Kind;
    using WindowSegment = GroupLookaheadWindow<MaxAxes>::Segment;

    // N-dimensional unit tangent at a segment boundary: lines use dir; arcs
    // combine the plane tangent with the linear following of higher axes.
    void window_tangent(const WindowSegment &seg, bool at_exit,
                        std::array<double, MaxAxes> &out) const;
    rt::Result<std::uint32_t> submit_blend(GroupCommand command);
    bool convert_active_linear_to_window();
    rt::Result<std::uint32_t> submit_blend_arc(
        GroupCommand command,
        const std::array<double, MaxAxes> &start_point);
    bool window_rebuild(bool &late);
    std::int64_t window_remaining_cycles() const;
    void window_cycle();
    void sample_window_segment(const WindowSegment &seg, double arclength);
    void sample_window_curve(const WindowNode &node, double arclength);
    void sample_window_arclength(double arclength);
    void window_live_state(double &s_live, double &v_live, double &a_live) const;
    void window_reset();
    rt::Result<std::uint32_t> degrade_blend(GroupCommand command);
    void blend_point(const std::array<std::array<double, MaxAxes>, 6> &control,
                     double u,
                     std::array<double, MaxAxes> &out) const;
    double blend_curvature(
        const std::array<std::array<double, MaxAxes>, 6> &control,
        double u) const;

    void clear_axes_synchronized()
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
    }

    GroupPoseFramesState pose_frames_{};
    double active_path_length_ = 0.0;
    double path_odometer_ = 0.0;
    double path_odometer_velocity_ = 0.0;
    double path_odometer_acceleration_ = 0.0;
    double path_sync_origin_ = 0.0;
    double path_sync_slave_origin_ = 0.0;
    double path_sync_ratio_ = 1.0;
    double path_sync_acceleration_ = 0.0;
    double path_sync_deceleration_ = 0.0;
    double path_sync_jerk_ = 0.0;
    double path_sync_velocity_ = 0.0;
    double path_sync_acceleration_state_ = 0.0;
    double tracking_master_origin_ = 0.0;
    std::int64_t active_tick_ = 0;
    std::int64_t active_duration_ = 1;
    double group_override_ = 1.0;
    double group_acc_override_ = 1.0;
    double group_jerk_override_ = 1.0;
    double interrupt_ratio_ = 0.0;
    double jog_stop_deceleration_ = 0.0;
    double jog_stop_jerk_ = 0.0;
    std::array<double, MaxAxes> active_start_{};
    std::array<double, MaxAxes> active_finish_{};
    std::array<double, MaxAxes> path_odometer_position_{};
    std::array<double, MaxAxes> path_odometer_member_velocity_{};
    std::array<double, SyncPathCapacity> group_sync_cumulative_{};
    std::array<GroupPosition, SyncPathCapacity> group_sync_waypoints_{};
    std::array<double, MaxAxes> jog_position_{};
    std::array<double, MaxAxes> jog_velocity_{};
    std::array<double, MaxAxes> jog_acceleration_{};
    std::array<double, 6> jog_cart_velocity_{};
    std::array<double, 6> jog_cart_acceleration_{};
    std::array<ToolData, ToolCapacity> tools_{};
    std::array<PayloadData, PayloadCapacity> payloads_{};
    std::array<bool, ToolCapacity> tool_defined_{{true}};
    std::array<bool, PayloadCapacity> payload_defined_{{true}};
    RigidBodyDynamics rigid_body_dynamics_{};
    bool rigid_body_dynamics_defined_ = false;
    // Cartesian look-ahead and Jog are mutually exclusive AxisGroup states;
    // overlay their submit-domain seed storage so Jog inputs do not enlarge
    // the already large fixed-capacity look-ahead object.
    union
    {
        double cart_tail_joints_[MaxAxes] = {};
        double jog_cartesian_start_[MaxAxes];
    };
    GroupTakeoverConnector<MaxAxes> connector_{};
    GroupLookaheadWindow<MaxAxes> joint_window_{};
    rt::StaticVector<AxisModel *, MaxAxes> axes_{};
    rt::StaticVector<IdentInGroup, MaxAxes> member_idents_{};
    AxisModel *path_sync_slave_ = nullptr;
    AxisModel *group_sync_master_ = nullptr;
    AxisModel *tracking_master_axis_ = nullptr;
    AxisGroup *tracking_master_group_ = nullptr;
    geom::ArcSegment active_arc_{};
    GroupCommand active_command_{};
    ToolData tracking_origin_{};
    ToolData tracking_transform_{};
    GroupPosition jog_direction_{};
    GroupPosition jog_cartesian_position_{};
    otg::Profile1D active_profile_{};
    rt::StaticVector<GroupCommand, QueueCapacity> queue_{};
    rt::StaticVector<GroupManagementResult, QueueCapacity> management_results_{};
    rt::StaticVector<std::uint32_t, QueueCapacity> aborted_management_ids_{};
    GroupCartesianState<MaxAxes> cartesian_{};
    int domain_id_ = 0;
    GroupStatus status_ = GroupStatus::disabled;
    GroupPathKind active_kind_ = GroupPathKind::linear;
    CoordSystem jog_coord_system_ = CoordSystem::acs;
    rt::ErrorCode group_error_id_ = rt::ErrorCode::ok;
    rt::ErrorCode jog_error_ = rt::ErrorCode::ok;
    rt::ErrorCode tracking_error_ = rt::ErrorCode::ok;
    std::uint32_t last_blend_degraded_id_ = 0;
    std::uint32_t next_command_id_ = 1;
    std::uint32_t active_management_id_ = 0;
    GroupCommandKind active_management_kind_ = GroupCommandKind::motion;
    GroupDirectPathState direct_path_{};
    std::uint32_t jog_command_id_ = 0;
    std::uint32_t last_aborted_jog_id_ = 0;
    std::uint32_t path_sync_id_ = 0;
    std::uint32_t group_sync_id_ = 0;
    std::uint32_t last_aborted_group_sync_id_ = 0;
    std::uint32_t tracking_command_id_ = 0;
    std::uint32_t last_aborted_tracking_id_ = 0;
    std::uint32_t halt_command_id_ = 0;
    std::uint32_t last_completed_halt_id_ = 0;
    std::uint32_t last_aborted_halt_id_ = 0;
    std::uint32_t wait_command_id_ = 0;
    std::uint32_t last_aborted_wait_id_ = 0;
    std::int64_t wait_duration_cycles_ = 0;
    std::int64_t wait_elapsed_cycles_ = 0;
    std::int64_t task_cycle_period_ns_ = 1000000;
    std::size_t selected_tool_ = 0;
    std::size_t active_tool_ = 0;
    std::size_t selected_payload_ = 0;
    std::size_t active_payload_ = 0;
    std::size_t group_sync_count_ = 0;
    bool active_ = false;
    bool override_paused_ = false;
    bool interrupting_ = false;
    bool interrupted_plain_ = false;
    bool interrupted_window_ = false;
    bool numbered_tool_mode_ = false;
    bool jog_active_ = false;
    bool jog_releasing_ = false;
    bool path_odometer_initialized_ = false;
    bool path_sync_in_sync_ = false;
    bool group_sync_active_ = false;
    bool tracking_motion_seen_ = false;
    bool tracking_following_ = false;
    bool pending_dynamic_pcs_ = false;
    bool halt_command_live_ = false;
    double group_sync_master_origin_ = 0.0;
    // Group-to-axis synchronization is aborted before Jog acquires the group;
    // both states therefore share their fixed command scratch storage.
    union
    {
        GroupPosition group_sync_previous_{};
        JogCommandOptions jog_options_;
    };
    PathMode group_sync_mode_ = PathMode::non_periodic;
    TrackingKind tracking_kind_ = TrackingKind::none;
    GroupWaitState wait_state_ = GroupWaitState::idle;
    GroupKinematicsInfo kinematics_info_{};
    PathDynamics reference_dynamics_{};
    PathDynamics default_dynamics_{};
    JoggingDynamics jogging_dynamics_{};
    DynamicsMode dynamics_mode_ = DynamicsMode::absolute;
    TransitionReferencePoint transition_reference_point_ =
        TransitionReferencePoint::end_point;
    bool kinematics_info_bound_ = false;
    bool kinematics_info_frozen_ = false;
};

} // namespace plcopen::core::axis

#include "axis/group_pose_frames_impl.h"
#include "axis/group_cartesian_impl.h"
#include "axis/group_window_impl.h"
#include "axis/group_direct_path_impl.h"
