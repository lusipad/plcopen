#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "axis/group_takeover_connector.h"
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

struct ToolData
{
    std::array<double, 6> value{};
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

// Internal: precomputed Cartesian segment geometry, resolved by the
// submit_linear pre-validation. Not a user input.
struct CartesianSegment
{
    bool pose = false;
    bool angle_driven = false;
    bool arc_path = false;
    geom::ArcSegment arc{};
    bool chain = false;
    double line1 = 0.0;
    double line2 = 0.0;
    geom::Vec3 dir1{};
    geom::Vec3 dir2{};
    geom::Vec3 exit_point{};
    geom::QuinticBlendSegment corner{};
    geom::Vec3 start{};
    geom::Vec3 delta{};
    double rotation_start[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    double axis[3] = {0.0, 0.0, 1.0};
    double angle = 0.0;
    double length = 0.0;
};

struct GroupCommand
{
    GroupPosition target{};
    bool relative = false;
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
    double transition_parameter = 0.0;

    // Circular-only inputs (ignored by submit_linear).
    GroupPathKind path_kind = GroupPathKind::linear;
    CircMode circ_mode = CircMode::border;
    CircPathChoice path_choice = CircPathChoice::counter_clockwise;
    GroupPosition aux{};
    // Internal: the validated arc geometry, resolved by submit_circular from
    // the command's deterministic start point. Not a user input.
    geom::ArcSegment arc{};
    CartesianSegment cart{};
    bool use_default_dynamics = false;
    std::size_t tool_number = 0;
    std::size_t payload_number = 0;
    geom::RigidTransform tool_inverse{};
    bool dynamic_pcs = false;
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
                                        const GroupPosition &direction)
    {
        const rt::ErrorCode valid = validate_jog(coord_system, direction);
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
            jog_tool_offset_ = tool_offset_;
            jog_pose_tool_inverse_ = pose_tool_inverse_;
            jog_cart_velocity_.fill(0.0);
            jog_cart_acceleration_.fill(0.0);
        }
        status_ = any_direction(direction) ? GroupStatus::moving
                                           : GroupStatus::standby;
        return rt::Result<std::uint32_t>::success(jog_command_id_);
    }

    rt::ErrorCode update_jog(std::uint32_t command_id, const GroupPosition &direction)
    {
        if(!jog_active_ || command_id == 0 || command_id != jog_command_id_) {
            return rt::ErrorCode::precondition_failed;
        }
        if(jog_error_ != rt::ErrorCode::ok) return jog_error_;
        const rt::ErrorCode valid = validate_jog(jog_coord_system_, direction);
        if(valid != rt::ErrorCode::ok) return valid;
        jog_direction_ = direction;
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

    bool contains(const AxisModel &axis) const
    {
        return find(axis) < axes_.size();
    }

    rt::ErrorCode add_axis(AxisModel &axis)
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

        const rt::ErrorCode pushed = axes_.push_back(&axis);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
        axis.set_group_owner(this, &status_);
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
        }
        axes_.pop_back();
        axis.set_group_owner(nullptr);
        return rt::ErrorCode::ok;
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
        if(direct_active_) {
            last_aborted_direct_id_ = direct_command_id_;
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
    // the minimal braking point, so members stay collinear on the commanded
    // line while stopping (KB-027).
    rt::ErrorCode stop(double deceleration = 1.0, double jerk = 1.0)
    {
        if(cart_window_active_ && !cart_window_stopping_) {
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
        if(direct_active_) {
            return stop_direct_members(deceleration, jerk);
        }
        queue_.clear();
        if(window_active_) {
            // Controlled stop along the committed window geometry (KB-032):
            // one halt profile over the composite arc length; not-yet-started
            // commands are cleared (they never execute), the geometry is kept
            // for braking. Mirrors the KB-027 clamp trick: the halt target may
            // lie past the path end, sampling clamps at the terminal point.
            if(window_stop_) {
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
            const WindowSegment &seg = window_[window_index_];
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
            window_stop_ = true;
            window_stop_profile_ = halt.value();
            window_stop_origin_ = s_live;
            window_tick_ = 0;
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

    rt::Result<std::uint32_t> halt(double deceleration = 1.0, double jerk = 1.0)
    {
        abort_wait();
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
               status_ != GroupStatus::standby;
    }

    bool halt_command_aborted(std::uint32_t command_id) const
    {
        return command_id != 0 && command_id == last_aborted_halt_id_;
    }

    rt::Result<std::uint32_t> submit_wait(std::int64_t duration_cycles,
                                         BufferMode buffer_mode)
    {
        if(duration_cycles <= 0 || status_ == GroupStatus::disabled ||
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
        wait_duration_cycles_ = duration_cycles;
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
        if(direct_active_) {
            return rt::ErrorCode::unsupported;
        }
        if(cart_window_active_ || cart_window_stopping_) {
            return rt::ErrorCode::unsupported;
        }
        if(window_active_) {
            if(window_stop_) {
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
            const WindowSegment &seg = window_[window_index_];
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
            window_stop_ = true;
            window_stop_profile_ = halt.value();
            window_stop_origin_ = s_live;
            window_tick_ = 0;
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
            const double factor = group_override_;
            const otg::Limits1D limits{active_command_.velocity * factor,
                                       active_command_.acceleration,
                                       active_command_.deceleration,
                                       active_command_.jerk};
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
            if(!window_active_ || window_.empty()) {
                interrupted_window_ = false;
                interrupted_plain_ = false;
                status_ = GroupStatus::standby;
                clear_axes_synchronized();
                start_next_queued();
                return rt::ErrorCode::ok;
            }
            window_stop_ = false;
            window_in_curve_ = false;
            window_tick_ = 0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                window_[window_index_].entry[i] = axes_[i]->snapshot().command_position;
            }
            const double factor = group_override_;
            for(std::size_t s = window_index_; s < window_.size(); ++s) {
                window_[s].limits.max_velocity *= factor;
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

    // MC_GroupSetOverride: group-level velocity factor ∈ [0,1]. factor=0
    // freezes position (velocity target zero, group stays moving).
    rt::ErrorCode set_group_override(double factor)
    {
        if(!std::isfinite(factor) || factor < 0.0 || factor > 1.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop) {
            return rt::ErrorCode::invalid_argument;
        }
        if(direct_active_) {
            return rt::ErrorCode::unsupported;
        }
        const double previous = group_override_;
        group_override_ = factor;
        if(previous == factor) {
            return rt::ErrorCode::ok;
        }
        if(status_ != GroupStatus::moving) {
            return rt::ErrorCode::ok;
        }
        if(cart_window_active_) {
            return rt::ErrorCode::ok;
        }
        if(window_active_ && !window_stop_) {
            if(factor == 0.0) {
                const rt::ErrorCode paused = interrupt(active_command_.deceleration,
                                                        active_command_.jerk);
                if(paused != rt::ErrorCode::ok) {
                    group_override_ = previous;
                    return paused;
                }
                window_override_paused_ = true;
                status_ = GroupStatus::moving;
                return rt::ErrorCode::ok;
            }
            if(window_override_paused_) {
                window_override_paused_ = false;
                status_ = GroupStatus::interrupted;
                group_override_ = 1.0;
                const rt::ErrorCode resumed = continue_motion();
                if(resumed != rt::ErrorCode::ok || status_ != GroupStatus::moving) {
                    group_override_ = factor;
                    return resumed;
                }
                return set_group_override(factor);
            }
            if(previous > 0.0) {
                for(std::size_t s = window_index_ + 1; s < window_.size(); ++s) {
                    window_[s].limits.max_velocity =
                        window_[s].limits.max_velocity * (factor / previous);
                }
            } else {
                for(std::size_t s = window_index_ + 1; s < window_.size(); ++s) {
                    double v = active_command_.velocity * factor;
                    if(window_[s].kind == WindowKind::arc) {
                        const double junction = std::fmin(
                            window_[s].limits.max_acceleration,
                            window_[s].limits.max_deceleration);
                        const double centripetal =
                            std::sqrt(junction * window_[s].arc_geom.radius);
                        if(centripetal < v) {
                            v = centripetal;
                        }
                    }
                    window_[s].limits.max_velocity = v;
                }
            }
            if(window_index_ + 1 < window_.size()) {
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
        const otg::Limits1D limits{active_command_.velocity * factor,
                                   active_command_.acceleration,
                                   active_command_.deceleration,
                                   active_command_.jerk};
        const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
            state, {active_path_length_, 0.0, 0.0}, limits);
        if(!profile) {
            group_override_ = previous;
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

    // MC_MoveDirectAbsolute/Relative (KB-068): non-coordinated PTP. Each member gets
    // an independent jerk-limited profile with the shared dynamics; members
    // arrive at different times. Done when all members reach standstill.
    rt::Result<std::uint32_t> submit_direct(GroupPosition target,
                                            bool relative,
                                            double velocity,
                                            double acceleration,
                                            double deceleration,
                                            double jrk)
    {
        if(direct_active_) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(status_ != GroupStatus::standby && status_ != GroupStatus::moving) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(target.size != axes_.size() || !std::isfinite(velocity) || velocity <= 0.0 ||
           !std::isfinite(acceleration) || acceleration <= 0.0 ||
           !std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jrk) || jrk <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!std::isfinite(target.value[i])) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
        }
        if(!members_ready_for_group_motion()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        snapshot_selections();
        snapshot_active_tool_transform();

        std::array<AxisCommand, MaxAxes> direct_commands{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            AxisCommand &cmd = direct_commands[i];
            cmd.kind = relative ? CommandKind::move_relative : CommandKind::move_absolute;
            cmd.value = target.value[i];
            cmd.velocity = velocity;
            cmd.acceleration = acceleration;
            cmd.deceleration = deceleration;
            cmd.jerk = jrk;
            const rt::ErrorCode preflight = axes_[i]->preflight_group_owned(cmd);
            if(preflight != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(preflight);
            }
        }

        abort_motion();
        const std::uint32_t cmd_id = next_command_id_++;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            AxisCommand &cmd = direct_commands[i];
            const rt::Result<std::uint32_t> result = axes_[i]->submit_group_owned(cmd);
            if(!result) {
                abort_direct_members();
                abort_motion();
                set_group_error(result.error());
                return rt::Result<std::uint32_t>::failure(result.error());
            }
        }
        direct_active_ = true;
        direct_stopping_ = false;
        direct_command_id_ = cmd_id;
        status_ = GroupStatus::moving;
        return rt::Result<std::uint32_t>::success(cmd_id);
    }

    // MC_GroupHome: parallel homing of all members. Group must be standby
    // with empty queue. Each member calls home_direct(0.0). All must
    // succeed; any failure triggers group errorstop.
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

    bool direct_motion_active() const
    {
        return direct_active_;
    }

    std::uint32_t last_completed_direct_command() const
    {
        return last_completed_direct_id_;
    }

    std::uint32_t last_aborted_direct_command() const
    {
        return last_aborted_direct_id_;
    }

    // Approved coordinate matrix (B1 v1): the workpiece frame (PCS over MCS)
    // and the tool offset are group configuration; they may only change at
    // standby with an empty queue — changing frames mid-motion has no
    // defined semantics.
    rt::ErrorCode set_workpiece_frame(double x, double y, double z, double rot_z)
    {
        return set_workpiece_frame_rpy(x, y, z, 0.0, 0.0, rot_z);
    }

    // Orientation batch (approved matrix, decision #4): the full rigid
    // workpiece frame; the Z-only setter above stays as its special case.
    rt::ErrorCode set_workpiece_frame_rpy(double x,
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
        workpiece_frame_ = geom::make_rpy_transform(x, y, z, roll, pitch, yaw);
        const double echo[6] = {x, y, z, roll, pitch, yaw};
        for(int i = 0; i < 6; ++i) {
            workpiece_frame_rpy_[i] = echo[i];
        }
        return rt::ErrorCode::ok;
    }

    // Orientation batch (approved matrix, decision #5): the flange-to-TCP
    // rigid transform for the pose pipeline (the translational pipeline
    // keeps its own set_tool_offset; the two never read each other).
    rt::ErrorCode set_tool_transform_rpy(double x,
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
        pose_tool_ = geom::make_rpy_transform(x, y, z, roll, pitch, yaw);
        pose_tool_inverse_ = geom::invert(pose_tool_);
        const double echo[6] = {x, y, z, roll, pitch, yaw};
        for(int i = 0; i < 6; ++i) {
            tool_transform_rpy_[i] = echo[i];
        }
        return rt::ErrorCode::ok;
    }

    // Orientation batch (approved matrix, decisions #2/#3): the 6-DOF pose
    // plugin, mutually exclusive with the translational plugin.
    rt::ErrorCode set_pose_kinematics(const kin::PoseKinematics *plugin,
                                      double min_singularity_margin,
                                      double max_joint_step)
    {
        if(status_ != GroupStatus::standby || !queue_.empty() ||
           !std::isfinite(min_singularity_margin) || min_singularity_margin < 0.0 ||
           !std::isfinite(max_joint_step) || max_joint_step <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(plugin != nullptr && (kinematics_ != nullptr || axes_.size() != 6 ||
                                 plugin->joint_count() != 6)) {
            return rt::ErrorCode::invalid_argument;
        }
        pose_kinematics_ = plugin;
        pose_min_margin_ = min_singularity_margin;
        pose_max_joint_step_ = max_joint_step;
        return rt::ErrorCode::ok;
    }

    // Readback batch (approved matrix decision #7): configuration getters
    // echo the original set values — never a matrix-to-RPY inversion of a
    // configured frame.
    void workpiece_frame_rpy(double out[6]) const
    {
        for(int i = 0; i < 6; ++i) {
            out[i] = workpiece_frame_rpy_[i];
        }
    }

    void tool_transform_rpy(double out[6]) const
    {
        for(int i = 0; i < 6; ++i) {
            out[i] = tool_transform_rpy_[i];
        }
    }

    geom::Vec3 tool_offset() const
    {
        return tool_offset_;
    }

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
                                 bool *gimbal_lock = nullptr) const
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

        if(pose_kinematics_ != nullptr) {
            kin::Pose6 flange{};
            pose_kinematics_->forward(joints, flange);
            geom::RigidTransform pose{};
            pose.translation = geom::Vec3{flange.position[0], flange.position[1],
                                          flange.position[2]};
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    pose.rotation[i][j] = flange.rotation[i][j];
                }
            }
            const geom::RigidTransform &tool = active_tool_transform_applies()
                                                   ? active_pose_tool_
                                                   : pose_tool_;
            pose = geom::compose(pose, tool);
            if(cs == CoordSystem::pcs) {
                pose = geom::compose(geom::invert(workpiece_frame_), pose);
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
        if(kinematics_ != nullptr) {
            const rt::ErrorCode forwarded =
                kinematics_->forward(joints, axes_.size(), point);
            if(forwarded != rt::ErrorCode::ok) {
                return forwarded;
            }
        } else {
            point = cartesian_part(out);
        }
        point = point + (active_tool_transform_applies() ? active_tool_offset_
                                                         : tool_offset_);
        if(cs == CoordSystem::pcs) {
            point = geom::transform_point(geom::invert(workpiece_frame_), point);
        }
        store_cartesian_part(out, point);
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode set_tool_offset(double x, double y, double z)
    {
        if(numbered_tool_mode_) return rt::ErrorCode::precondition_failed;
        if(status_ != GroupStatus::standby || !queue_.empty()) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return rt::ErrorCode::invalid_argument;
        }
        tool_offset_ = geom::Vec3{x, y, z};
        return rt::ErrorCode::ok;
    }

    // BS3.6 (approved kinematics matrix follow-up): conservative dual-space
    // velocity limiting. With a kinematics plugin the segment interpolates
    // in joint space, so the Cartesian speed along it varies; at submit the
    // joint-space chord is sampled through the forward solution and the
    // command velocity is scaled down so the worst sampled Cartesian speed
    // stays under this limit (0 disables; linear segments only in v1).
    rt::ErrorCode set_cartesian_velocity_limit(double limit)
    {
        if(status_ != GroupStatus::standby || !queue_.empty() || !std::isfinite(limit) ||
           limit < 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        cartesian_velocity_limit_ = limit;
        return rt::ErrorCode::ok;
    }

    // Approved kinematics matrix (B2 v1): the plugin upgrades the declared
    // identity ACS<->MCS mapping to a real mechanism. The caller owns the
    // plugin lifetime; nullptr restores the identity. v1 requires the joint
    // count to equal both the Cartesian coordinate count (2 or 3) and the
    // group axis count; the 6R batch lifts this.
    rt::ErrorCode set_kinematics(const kin::Kinematics *plugin,
                                  double min_singularity_margin = 0.0)
    {
        if(status_ != GroupStatus::standby || !queue_.empty() ||
           !std::isfinite(min_singularity_margin) || min_singularity_margin < 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(plugin != nullptr &&
           (pose_kinematics_ != nullptr || plugin->joint_count() != axes_.size() ||
            plugin->cartesian_count() != plugin->joint_count() ||
            plugin->cartesian_count() < 2 || plugin->cartesian_count() > 3)) {
            return rt::ErrorCode::invalid_argument;
        }
        kinematics_ = plugin;
        kinematics_min_margin_ = min_singularity_margin;
        return rt::ErrorCode::ok;
    }

    const kin::Kinematics *kinematics_plugin() const { return kinematics_; }
    const kin::PoseKinematics *pose_kinematics_plugin() const { return pose_kinematics_; }

    rt::ErrorCode set_coordinate_transform(CoordSystem coordinate_system,
                                           const ToolData &transform,
                                           ExecutionMode execution_mode)
    {
        if(execution_mode != ExecutionMode::immediately) {
            return rt::ErrorCode::unsupported;
        }
        if(coordinate_system != CoordSystem::pcs) return rt::ErrorCode::unsupported;
        return set_workpiece_frame_rpy(transform.value[0], transform.value[1],
                                       transform.value[2], transform.value[3],
                                       transform.value[4], transform.value[5]);
    }

    rt::ErrorCode coordinate_transform(CoordSystem coordinate_system,
                                       ToolData &transform) const
    {
        if(coordinate_system != CoordSystem::pcs) return rt::ErrorCode::unsupported;
        workpiece_frame_rpy(transform.value.data());
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode transform_position(const GroupPosition &position,
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
            if(pose_kinematics_ != nullptr) {
                kin::Pose6 flange{};
                pose_kinematics_->forward(position.value.data(), flange);
                geom::RigidTransform transform{};
                transform.translation = {flange.position[0], flange.position[1],
                                         flange.position[2]};
                for(int row = 0; row < 3; ++row) {
                    for(int column = 0; column < 3; ++column) {
                        transform.rotation[row][column] = flange.rotation[row][column];
                    }
                }
                const geom::RigidTransform &tool = active_tool_transform_applies()
                                                       ? active_pose_tool_
                                                       : pose_tool_;
                transform = geom::compose(transform, tool);
                mcs.value[0] = transform.translation.x;
                mcs.value[1] = transform.translation.y;
                mcs.value[2] = transform.translation.z;
                geom::extract_rpy(transform.rotation, mcs.value[3], mcs.value[4],
                                  mcs.value[5]);
            } else {
                geom::Vec3 point{};
                if(kinematics_ != nullptr) {
                    const rt::ErrorCode result = kinematics_->forward(
                        position.value.data(), position.size, point);
                    if(result != rt::ErrorCode::ok) return result;
                } else {
                    point = {position.value[0], position.value[1], position.value[2]};
                }
                point = point + (active_tool_transform_applies() ? active_tool_offset_
                                                                 : tool_offset_);
                mcs.value[0] = point.x;
                mcs.value[1] = point.y;
                mcs.value[2] = point.z;
            }
        } else if(source == CoordSystem::pcs) {
            if(pose_kinematics_ != nullptr) {
                geom::RigidTransform transform = geom::make_rpy_transform(
                    position.value[0], position.value[1], position.value[2],
                    position.value[3], position.value[4], position.value[5]);
                transform = geom::compose(workpiece_frame_, transform);
                mcs.value[0] = transform.translation.x;
                mcs.value[1] = transform.translation.y;
                mcs.value[2] = transform.translation.z;
                geom::extract_rpy(transform.rotation, mcs.value[3], mcs.value[4],
                                  mcs.value[5]);
            } else {
                const geom::Vec3 point = geom::transform_point(
                    workpiece_frame_, {position.value[0], position.value[1],
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
            if(pose_kinematics_ != nullptr) {
                geom::RigidTransform transform = geom::make_rpy_transform(
                    mcs.value[0], mcs.value[1], mcs.value[2], mcs.value[3],
                    mcs.value[4], mcs.value[5]);
                transform = geom::compose(geom::invert(workpiece_frame_), transform);
                output.value[0] = transform.translation.x;
                output.value[1] = transform.translation.y;
                output.value[2] = transform.translation.z;
                geom::extract_rpy(transform.rotation, output.value[3], output.value[4],
                                  output.value[5]);
            } else {
                const geom::Vec3 point = geom::transform_point(
                    geom::invert(workpiece_frame_),
                    {mcs.value[0], mcs.value[1], mcs.value[2]});
                output.value[0] = point.x;
                output.value[1] = point.y;
                output.value[2] = point.z;
            }
            return rt::ErrorCode::ok;
        }

        output = mcs;
        if(pose_kinematics_ != nullptr) {
            geom::RigidTransform tcp = geom::make_rpy_transform(
                mcs.value[0], mcs.value[1], mcs.value[2], mcs.value[3], mcs.value[4],
                mcs.value[5]);
            const geom::RigidTransform &tool_inverse = active_tool_transform_applies()
                                                           ? active_pose_tool_inverse_
                                                           : pose_tool_inverse_;
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
            const rt::ErrorCode result = pose_kinematics_->inverse(
                pose, seed, pose_max_joint_step_, output.value.data());
            if(result != rt::ErrorCode::ok) return result;
            singular_position =
                pose_kinematics_->singularity_margin(output.value.data()) < pose_min_margin_;
        } else {
            const geom::Vec3 tool = active_tool_transform_applies() ? active_tool_offset_
                                                                    : tool_offset_;
            const geom::Vec3 point{mcs.value[0] - tool.x, mcs.value[1] - tool.y,
                                   mcs.value[2] - tool.z};
            if(kinematics_ != nullptr) {
                double seed[MaxAxes] = {};
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    seed[i] = axes_[i]->snapshot().command_position;
                }
                const rt::ErrorCode result = kinematics_->inverse(
                    point, seed, axes_.size(), output.value.data());
                if(result != rt::ErrorCode::ok) return result;
                singular_position = kinematics_->singularity_margin(
                                        output.value.data(), axes_.size()) <
                                    kinematics_min_margin_;
            } else {
                output.value[0] = point.x;
                output.value[1] = point.y;
                output.value[2] = point.z;
            }
        }
        return rt::ErrorCode::ok;
    }

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
        if(status_ != GroupStatus::standby || !queue_.empty() || window_active_ ||
           depth < 2 || depth > WindowCapacity) {
            return rt::ErrorCode::invalid_argument;
        }
        window_depth_ = depth;
        return rt::ErrorCode::ok;
    }

    rt::Result<std::uint32_t> submit_linear(GroupCommand command)
    {
        pending_dynamic_pcs_ = false;
        if(jog_active_ && command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        command.tool_number = selected_tool_;
        command.payload_number = selected_payload_;
        command.tool_inverse = pose_tool_inverse_;
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
        if(direct_active_ && !halt_takeover) {
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
        if(!members_ready_for_group_motion()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if(tracking_kind_ != TrackingKind::none && command.coord_system == CoordSystem::pcs) {
            if(command.buffer_mode != BufferMode::aborting) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            command.dynamic_pcs = true;
            pending_dynamic_pcs_ = true;
            pending_dynamic_reference_frame_ = workpiece_frame_;
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
                if(kinematics_ != nullptr) {
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
        const bool blending_buffer = command.buffer_mode == BufferMode::blending_low ||
                                     command.buffer_mode == BufferMode::blending_high;
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
            // Y7 (KB-051): capture the pre-takeover velocity vector before
            // abort_motion() destroys it. Only plain linear motions qualify
            // for the connector (approved v2.1 scope = linear group only).
            if(active_ && !window_active_ && !cart_window_active_) {
                capture_takeover_velocity();
            }
            if(halt_takeover && direct_active_) abort_direct_members();
            abort_halt();
            abort_wait();
            abort_motion();
        }
        command = normalized;

        if(blending_buffer) {
            return submit_blend(command);
        }

        if(command.buffer_mode == BufferMode::aborting ||
           (!active_ && !window_active_ && !wait_blocks_motion_start())) {
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
        if(jog_active_ && command.buffer_mode != BufferMode::aborting) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        command.tool_number = selected_tool_;
        command.payload_number = selected_payload_;
        command.tool_inverse = pose_tool_inverse_;
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
        if(direct_active_ && !halt_takeover) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        if((status_ != GroupStatus::standby && status_ != GroupStatus::moving &&
              status_ != GroupStatus::interrupted && !halt_takeover) ||
           axes_.size() < 2 || command.target.size != axes_.size() ||
           command.aux.size != axes_.size() || command.velocity <= 0.0 ||
           !std::isfinite(command.velocity) || command.acceleration <= 0.0 ||
           !std::isfinite(command.acceleration) || command.deceleration <= 0.0 ||
           !std::isfinite(command.deceleration) || command.jerk <= 0.0 ||
           !std::isfinite(command.jerk) || !finite(command.target) || !finite(command.aux)) {
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
            pending_dynamic_reference_frame_ = workpiece_frame_;
        }
        if(command.circ_mode != CircMode::border) {
            // CENTER/RADIUS are declared unsupported in v1, not approximated.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        // Cartesian v2-B (approved addendum): opt-in Cartesian-domain arcs
        // resolve here and commit directly — the joint-domain construction
        // below never sees them.
        if(command.interpolation_space == InterpolationSpace::cartesian) {
            const rt::ErrorCode prepared = prepare_cartesian_circular(command);
            if(prepared != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(prepared);
            }
            if(command.command_id == 0) {
                command.command_id = next_command_id_++;
            }
            if(command.buffer_mode == BufferMode::aborting) {
                if(halt_takeover && direct_active_) abort_direct_members();
                abort_halt();
                abort_wait();
                abort_motion();
            }
            if(command.buffer_mode == BufferMode::aborting ||
               (!active_ && !window_active_ && !wait_blocks_motion_start())) {
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
        if(pose_kinematics_ != nullptr && command.coord_system != CoordSystem::acs) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        const rt::ErrorCode framed = apply_coordinate_frame(command);
        if(framed != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(framed);
        }
        const bool arc_blending = command.buffer_mode == BufferMode::blending_low ||
                                  command.buffer_mode == BufferMode::blending_high;
        if(command.transition_mode != TransitionMode::none ||
           command.transition_parameter != 0.0) {
            // Tolerance-band line-arc transition curves are v3 scope; arc
            // window entry rides on tangent continuity alone (approved A5 v2).
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
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
            if(halt_takeover && direct_active_) abort_direct_members();
            abort_halt();
            abort_wait();
            abort_motion();
        }
        if(aborting || (!active_ && !window_active_ && !wait_blocks_motion_start())) {
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
        return last_cartesian_error_;
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
        if(direct_active_) {
            result.active_command_id = direct_command_id_;
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
        } else if(window_active_ && !window_.empty()) {
            result.active_command_id = window_[window_index_].command_id;
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
        if(window_active_ && !window_.empty()) {
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
        if(cart_window_active_) {
            return rt::Result<GroupCommandInfo>::failure(
                rt::ErrorCode::unsupported);
        }
        if(direct_active_ && command_id == direct_command_id_) {
            GroupCommandInfo result{};
            result.state = GroupCommandState::active;
            return rt::Result<GroupCommandInfo>::success(result);
        }
        if(window_active_) {
            for(std::size_t i = window_index_; i < window_.size(); ++i) {
                if(window_[i].command_id != command_id) continue;
                GroupCommandInfo result{};
                result.state = i == window_index_ ? GroupCommandState::active
                                                  : GroupCommandState::accepted;
                if(i == window_index_) {
                    result.elapsed_cycles = window_tick_;
                    result.remaining_cycles =
                        window_[i].profile.duration_cycles() - window_tick_;
                    double position = 0.0;
                    double velocity = 0.0;
                    double acceleration = 0.0;
                    window_live_state(position, velocity, acceleration);
                    const double length = window_[i].line_length();
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
                if(direct_active_) {
                    last_aborted_direct_id_ = direct_command_id_;
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

        if(direct_active_) {
            bool all_done = true;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                if(axes_[i]->status() != AxisStatus::standstill) {
                    all_done = false;
                    break;
                }
            }
            if(all_done) {
                if(direct_stopping_) {
                    last_aborted_direct_id_ = direct_command_id_;
                } else {
                    last_completed_direct_id_ = direct_command_id_;
                }
                direct_active_ = false;
                direct_stopping_ = false;
                status_ = GroupStatus::standby;
            }
            return;
        }

        if(cart_window_active_) {
            cart_window_cycle();
            return;
        }
        if(window_active_) {
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
            const geom::Vec3 point = geom::sample(active_arc_, ratio * active_path_length_);
            axes_[0]->set_synchronized_position(point.x);
            axes_[1]->set_synchronized_position(point.y);
            for(std::size_t i = 2; i < axes_.size(); ++i) {
                const double position =
                    active_start_[i] + (active_finish_[i] - active_start_[i]) * ratio;
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
            const double lat_offset =
                connector_.sample_lateral_offset(active_tick_);
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                double position =
                    active_start_[i] + (active_finish_[i] - active_start_[i]) * ratio;
                if(lat_offset != 0.0) {
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
        if(pose_kinematics_ != nullptr) {
            const geom::RigidTransform flange =
                geom::compose(tcp, active_pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange.translation.x;
            pose.position[1] = flange.translation.y;
            pose.position[2] = flange.translation.z;
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    pose.rotation[row][column] = flange.rotation[row][column];
                }
            }
            result = pose_kinematics_->inverse(
                pose, seed, pose_max_joint_step_, solved);
        } else {
            const geom::Vec3 flange = tcp.translation - active_tool_offset_;
            if(kinematics_ != nullptr) {
                result = kinematics_->inverse(flange, seed, axes_.size(), solved);
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
        if(pose_kinematics_ != nullptr) {
            kin::Pose6 flange{};
            pose_kinematics_->forward(joints, flange);
            geom::RigidTransform flange_transform{};
            flange_transform.translation =
                {flange.position[0], flange.position[1], flange.position[2]};
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    flange_transform.rotation[row][column] = flange.rotation[row][column];
                }
            }
            tcp = geom::compose(flange_transform, active_pose_tool_);
            return true;
        }
        geom::Vec3 point{};
        if(kinematics_ != nullptr) {
            if(kinematics_->forward(joints, axes_.size(), point) != rt::ErrorCode::ok) {
                return false;
            }
        } else {
            if(axes_.size() > 0) point.x = joints[0];
            if(axes_.size() > 1) point.y = joints[1];
            if(axes_.size() > 2) point.z = joints[2];
        }
        tcp.translation = point + active_tool_offset_;
        return true;
    }

    bool apply_dynamic_tracking_to_active()
    {
        if(!active_command_.dynamic_pcs) return true;
        geom::RigidTransform base_tcp{};
        if(!current_tracking_tcp(base_tcp)) return false;
        const geom::RigidTransform delta = geom::compose(
            workpiece_frame_, geom::invert(active_dynamic_reference_frame_));
        const geom::RigidTransform tracked_tcp = geom::compose(delta, base_tcp);
        if(!solve_tracking_tcp(tracked_tcp)) return false;
        tracking_hold_pose_ =
            geom::compose(geom::invert(workpiece_frame_), tracked_tcp);
        tracking_following_ = true;
        tracking_motion_seen_ = true;
        return true;
    }

    void apply_tracking_hold()
    {
        if(!tracking_following_ || tracking_kind_ == TrackingKind::none || active_ ||
           window_active_ || cart_window_active_ || direct_active_ || jog_active_ ||
           group_sync_active_) {
            return;
        }
        const geom::RigidTransform target =
            geom::compose(workpiece_frame_, tracking_hold_pose_);
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
            }
            path_odometer_initialized_ = true;
            return;
        }
        double squared_distance = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double position = axes_[i]->snapshot().command_position;
            const double delta = position - path_odometer_position_[i];
            squared_distance += delta * delta;
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
        workpiece_frame_ = frame;
        workpiece_frame_rpy_[0] = frame.translation.x;
        workpiece_frame_rpy_[1] = frame.translation.y;
        workpiece_frame_rpy_[2] = frame.translation.z;
        geom::extract_rpy(frame.rotation, workpiece_frame_rpy_[3],
                          workpiece_frame_rpy_[4], workpiece_frame_rpy_[5]);
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
                               const GroupPosition &direction) const
    {
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
            return direction.size == axes_.size() ? rt::ErrorCode::ok
                                                  : rt::ErrorCode::invalid_argument;
        }
        if(coord_system != CoordSystem::mcs && coord_system != CoordSystem::pcs) {
            return rt::ErrorCode::unsupported;
        }
        if(kinematics_ == nullptr && pose_kinematics_ == nullptr) {
            return rt::ErrorCode::precondition_failed;
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
                                                : jogging_dynamics_.axis_deceleration[index])
                : jogging_dynamics_.axis_acceleration[index];
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
                : (target_velocity == 0.0 ? jogging_dynamics_.path.deceleration
                                          : jogging_dynamics_.path.acceleration);
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

    bool solve_jog_cartesian(GroupPosition &target)
    {
        double q[MaxAxes] = {};
        const bool pcs = jog_coord_system_ == CoordSystem::pcs;
        if(pose_kinematics_ != nullptr) {
            geom::RigidTransform tcp = geom::make_rpy_transform(
                target.value[0], target.value[1], target.value[2], target.value[3],
                target.value[4], target.value[5]);
            if(pcs) tcp = geom::compose(workpiece_frame_, tcp);
            const geom::RigidTransform flange = geom::compose(tcp, jog_pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange.translation.x;
            pose.position[1] = flange.translation.y;
            pose.position[2] = flange.translation.z;
            for(int row = 0; row < 3; ++row) {
                for(int column = 0; column < 3; ++column) {
                    pose.rotation[row][column] = flange.rotation[row][column];
                }
            }
            const rt::ErrorCode solved = pose_kinematics_->inverse(
                pose, jog_position_.data(), pose_max_joint_step_, q);
            if(solved != rt::ErrorCode::ok ||
               pose_kinematics_->singularity_margin(q) < pose_min_margin_) {
                jog_error_ = solved == rt::ErrorCode::ok
                                 ? rt::ErrorCode::precondition_failed
                                 : solved;
                return false;
            }
        } else {
            geom::Vec3 point = cartesian_part(target);
            if(pcs) point = geom::transform_point(workpiece_frame_, point);
            point = point - jog_tool_offset_;
            const rt::ErrorCode solved =
                kinematics_->inverse(point, jog_position_.data(), axes_.size(), q);
            if(solved != rt::ErrorCode::ok ||
               kinematics_->singularity_margin(q, axes_.size()) <
                   kinematics_min_margin_) {
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
                                      jogging_dynamics_.axis_velocity[i];
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
                advance_cart_jog_state(i, component * jogging_dynamics_.path.velocity);
            }
            GroupPosition next = jog_cartesian_position_;
            for(std::size_t i = 0; i < 6 && i < next.size; ++i) {
                next.value[i] += jog_cart_velocity_[i];
            }
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
        pose_tool_ = geom::make_rpy_transform(tool.value[0], tool.value[1], tool.value[2],
                                              tool.value[3], tool.value[4], tool.value[5]);
        pose_tool_inverse_ = geom::invert(pose_tool_);
        tool_offset_ = {tool.value[0], tool.value[1], tool.value[2]};
        for(std::size_t i = 0; i < tool.value.size(); ++i) {
            tool_transform_rpy_[i] = tool.value[i];
        }
    }

    void snapshot_selections()
    {
        active_tool_ = selected_tool_;
        active_payload_ = selected_payload_;
    }

    void snapshot_active_tool_transform()
    {
        active_pose_tool_ = pose_tool_;
        active_pose_tool_inverse_ = pose_tool_inverse_;
        active_tool_offset_ = tool_offset_;
    }

    bool active_tool_transform_applies() const
    {
        return status_ == GroupStatus::moving || status_ == GroupStatus::stopping ||
               status_ == GroupStatus::interrupted;
    }

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
        if(active_ || direct_active_ || window_active_ || cart_window_active_ || jog_active_ ||
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

    void abort_direct_members()
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->abort_group_owned_motion();
        }
    }

    rt::ErrorCode stop_direct_members(double deceleration, double jerk)
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
                last_aborted_direct_id_ = direct_command_id_;
                abort_direct_members();
                abort_motion();
                status_ = GroupStatus::standby;
                return submitted.error();
            }
        }
        direct_stopping_ = true;
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
    }

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

    static bool finite(const GroupPosition &position)
    {
        for(std::size_t i = 0; i < position.size; ++i) {
            if(!std::isfinite(position.value[i])) {
                return false;
            }
        }
        return true;
    }

    static geom::Vec3 cartesian_part(const GroupPosition &position)
    {
        return geom::Vec3{position.value[0],
                          position.size > 1 ? position.value[1] : 0.0,
                          position.size > 2 ? position.value[2] : 0.0};
    }

    static void store_cartesian_part(GroupPosition &position, geom::Vec3 point)
    {
        position.value[0] = point.x;
        if(position.size > 1) {
            position.value[1] = point.y;
        }
        if(position.size > 2) {
            position.value[2] = point.z;
        }
    }

    // Approved coordinate matrix (B1 v1): MCS/PCS targets convert to ACS at
    // submit time on the first three coordinates (higher axes pass through in
    // ACS); ACS commands never see the frames. Absolute points go through the
    // workpiece frame (PCS) and then subtract the tool offset (MCS and PCS);
    // relative distances only rotate — translation and tool offset cancel
    // between two TCP positions. The v1 ACS<->MCS mapping is the declared
    // identity (Cartesian rig; kinematics plugins arrive with B2).
    rt::ErrorCode apply_coordinate_frame(GroupCommand &command) const
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
        if(pose_kinematics_ != nullptr) {
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
                target = geom::compose(workpiece_frame_, target);
            }
            const geom::RigidTransform flange = geom::compose(target, pose_tool_inverse_);

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
                pose_kinematics_->inverse(pose, seed, pose_max_joint_step_, joints);
            if(inverted != rt::ErrorCode::ok) {
                return inverted;
            }
            if(pose_kinematics_->singularity_margin(joints) < pose_min_margin_) {
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
        if(kinematics_ != nullptr) {
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
            if(cartesian_velocity_limit_ > 0.0 && !circular) {
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
                            kinematics_->forward(joints, axes_.size(), cartesian);
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
                        const double allowed = cartesian_velocity_limit_ / worst_ratio;
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
                direction = geom::transform_rotate(workpiece_frame_, direction);
            }
            store_cartesian_part(command.target, direction);
            if(circular) {
                geom::Vec3 aux = cartesian_part(command.aux);
                if(pcs) {
                    aux = geom::transform_rotate(workpiece_frame_, aux);
                }
                store_cartesian_part(command.aux, aux);
            }
        } else {
            geom::Vec3 point = cartesian_part(command.target);
            if(pcs) {
                point = geom::transform_point(workpiece_frame_, point);
            }
            store_cartesian_part(command.target,
                                 point - tool_offset_);
            if(circular) {
                geom::Vec3 aux = cartesian_part(command.aux);
                if(pcs) {
                    aux = geom::transform_point(workpiece_frame_, aux);
                }
                store_cartesian_part(command.aux, aux - tool_offset_);
            }
        }
        command.coord_system = CoordSystem::acs;
        return rt::ErrorCode::ok;
    }

    // One Cartesian target through frame, tool offset, and inverse solution.
    // Relative displacements only rotate (translation and tool offset cancel
    // between two TCP positions) and resolve against the flange position of
    // the seed joints.
    rt::ErrorCode solve_cartesian_target(GroupPosition &position,
                                         bool relative,
                                         bool pcs,
                                         const double *seed) const
    {
        geom::Vec3 point = cartesian_part(position);
        if(relative) {
            if(pcs) {
                point = geom::transform_rotate(workpiece_frame_, point);
            }
            geom::Vec3 start{};
            const rt::ErrorCode forwarded =
                kinematics_->forward(seed, axes_.size(), start);
            if(forwarded != rt::ErrorCode::ok) {
                return forwarded;
            }
            point = start + point;
        } else {
            if(pcs) {
                point = geom::transform_point(workpiece_frame_, point);
            }
            point = point - tool_offset_;
        }

        double joints[MaxAxes] = {};
        const rt::ErrorCode inverted =
            kinematics_->inverse(point, seed, axes_.size(), joints);
        if(inverted != rt::ErrorCode::ok) {
            return inverted;
        }
        if(kinematics_->singularity_margin(joints, axes_.size()) < kinematics_min_margin_) {
            return rt::ErrorCode::precondition_failed;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            position.value[i] = joints[i];
        }
        return rt::ErrorCode::ok;
    }

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
    struct CartPiece
    {
        bool corner = false;
        geom::Vec3 start{};
        geom::Vec3 dir{};
        double length = 0.0;
        geom::QuinticBlendSegment blend{};
        double v_in = 0.0;
        double v_out = 0.0;
        double cap = 0.0;
        otg::Profile1D profile{};
        std::int64_t duration = 0;
    };
    static constexpr std::size_t CartWindowPieces = 32; // <= 16 segments

    rt::Result<std::uint32_t> submit_cartesian_window(GroupCommand command)
    {
        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }
        if(command.coord_system != CoordSystem::mcs &&
           command.coord_system != CoordSystem::pcs) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.relative || !queue_.empty() || window_active_) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.tool_number != active_tool_ ||
           command.payload_number != active_payload_) {
            last_blend_degraded_id_ = command.command_id;
            command.buffer_mode = BufferMode::buffered;
            command.transition_mode = TransitionMode::none;
            command.transition_parameter = 0.0;
            const rt::ErrorCode prepared = prepare_cartesian_linear(command);
            if(prepared != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(prepared);
            }
            const rt::ErrorCode queued = queue_.push_back(command);
            return queued == rt::ErrorCode::ok
                       ? rt::Result<std::uint32_t>::success(command.command_id)
                       : rt::Result<std::uint32_t>::failure(queued);
        }
        const bool extend = cart_window_active_;
        if(!extend) {
            // Conversion seed: an active, non-chain, non-arc Cartesian line.
            if(!active_ || status_ != GroupStatus::moving ||
               active_kind_ != GroupPathKind::cartesian_linear ||
               active_cart_.arc_path || active_cart_.chain ||
               active_cart_.pose || active_path_length_ <= 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        } else if(cart_window_stopping_) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        // Successor target in the plugin Cartesian domain.
        geom::Vec3 target_point = cartesian_part(command.target);
        if(command.coord_system == CoordSystem::pcs) {
            target_point = geom::transform_point(workpiece_frame_, target_point);
        }
        target_point = target_point - tool_offset_;

        // Tail geometry: the line the corner attaches to.
        geom::Vec3 tail_end{};
        geom::Vec3 tail_dir{};
        double tail_room = 0.0; // trimmable room on the tail line
        std::size_t tail_index = 0;
        if(!extend) {
            const otg::State1D live = otg::sample(
                active_profile_, rt::CycleTick::from_cycles(active_tick_));
            double s_live = live.position < 0.0 ? 0.0 : live.position;
            s_live = s_live > active_path_length_ ? active_path_length_ : s_live;
            tail_end = geom::Vec3{active_cart_.start.x + active_cart_.delta.x,
                                  active_cart_.start.y + active_cart_.delta.y,
                                  active_cart_.start.z + active_cart_.delta.z};
            const double len = geom::norm(active_cart_.delta);
            tail_dir = geom::Vec3{active_cart_.delta.x / len,
                                  active_cart_.delta.y / len,
                                  active_cart_.delta.z / len};
            tail_room = active_path_length_ - s_live;
        } else {
            tail_index = cart_window_.size() - 1;
            const CartPiece &tail = cart_window_[tail_index];
            tail_end = geom::Vec3{tail.start.x + tail.dir.x * tail.length,
                                  tail.start.y + tail.dir.y * tail.length,
                                  tail.start.z + tail.dir.z * tail.length};
            tail_dir = tail.dir;
            if(tail_index == cart_piece_index_) {
                const otg::State1D live = otg::sample(
                    cart_window_[tail_index].profile,
                    rt::CycleTick::from_cycles(cart_piece_tick_));
                tail_room = tail.length - live.position;
            } else if(tail_index > cart_piece_index_) {
                tail_room = tail.length;
            } else {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        }

        const geom::Vec3 out_vec = target_point - tail_end;
        const double len_b = geom::norm(out_vec);
        bool degrade = false;
        double trim = 0.0;
        double corner_cap = 0.0;
        geom::QuinticBlendSegment corner{};
        bool passthrough = false;
        if(len_b <= 1e-12) {
            degrade = true;
        } else {
            const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                                out_vec.z / len_b};
            const double dot =
                tail_dir.x * t1.x + tail_dir.y * t1.y + tail_dir.z * t1.z;
            if(dot <= -0.999) {
                degrade = true;
            } else if(dot >= 1.0 - 1e-9) {
                passthrough = true;
            } else {
                const geom::Vec3 diff{t1.x - tail_dir.x, t1.y - tail_dir.y,
                                      t1.z - tail_dir.z};
                const double turn = geom::norm(diff);
                trim = command.transition_parameter * 96.0 / (23.0 * turn);
                const double room = tail_room < len_b ? tail_room : len_b;
                if(trim > 0.5 * room) {
                    trim = 0.5 * room;
                }
                if(trim <= 1e-9 || tail_room <= trim) {
                    degrade = true;
                } else {
                    const geom::Vec3 entry{tail_end.x - tail_dir.x * trim,
                                           tail_end.y - tail_dir.y * trim,
                                           tail_end.z - tail_dir.z * trim};
                    const geom::Vec3 exit{tail_end.x + t1.x * trim,
                                          tail_end.y + t1.y * trim,
                                          tail_end.z + t1.z * trim};
                    const rt::Result<geom::QuinticBlendSegment> blend =
                        geom::make_quintic_blend(entry, tail_end, exit,
                                                 command.transition_parameter);
                    if(!blend) {
                        degrade = true;
                    } else {
                        corner = blend.value();
                        const double axis_accel =
                            command.acceleration < command.deceleration
                                ? command.acceleration
                                : command.deceleration;
                        corner_cap = corner.max_curvature > 1e-12
                                         ? std::sqrt(axis_accel /
                                                     corner.max_curvature)
                                         : command.velocity;
                    }
                }
            }
        }

        if(!degrade &&
           cart_window_.size() + (passthrough ? 1 : 2) > CartWindowPieces) {
            return rt::Result<std::uint32_t>::failure(
                rt::ErrorCode::capacity_exceeded);
        }

        if(!degrade) {
            // Pre-validation: seed-chain the inverse along the new line (the
            // corner stays inside the tolerance ball of the lines,
            // declared); update the window tail joints.
            double chain[MaxAxes] = {};
            if(!extend) {
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    chain[i] = active_finish_[i];
                }
            } else {
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    chain[i] = cart_tail_joints_[i];
                }
            }
            double q[MaxAxes] = {};
            constexpr int Samples = 32;
            bool valid = true;
            rt::ErrorCode failure = rt::ErrorCode::ok;
            for(int k = 0; k <= Samples && valid; ++k) {
                const double fraction =
                    static_cast<double>(k) / static_cast<double>(Samples);
                const geom::Vec3 sample{
                    tail_end.x + (target_point.x - tail_end.x) * fraction,
                    tail_end.y + (target_point.y - tail_end.y) * fraction,
                    tail_end.z + (target_point.z - tail_end.z) * fraction};
                const rt::ErrorCode solved =
                    kinematics_->inverse(sample, chain, axes_.size(), q);
                if(solved != rt::ErrorCode::ok) {
                    valid = false;
                    failure = solved;
                    break;
                }
                if(kinematics_->singularity_margin(q, axes_.size()) <
                   kinematics_min_margin_) {
                    valid = false;
                    failure = rt::ErrorCode::precondition_failed;
                    break;
                }
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    chain[i] = q[i];
                }
            }
            if(!valid) {
                return rt::Result<std::uint32_t>::failure(failure);
            }

            // Commit geometry. Extending while riding a corner piece is a
            // declared too-late degrade (transient, one corner long).
            if(extend && cart_window_[cart_piece_index_].corner) {
                degrade = true;
            }
            if(!degrade && !extend) {
                cart_window_convert(command, trim, passthrough);
            } else if(!degrade) {
                // Re-anchor the currently executing line piece to its live
                // state so the rebuild replans from reality.
                CartPiece &current = cart_window_[cart_piece_index_];
                const otg::State1D live = otg::sample(
                    current.profile, rt::CycleTick::from_cycles(cart_piece_tick_));
                double s_live = live.position < 0.0 ? 0.0 : live.position;
                s_live = s_live > current.length ? current.length : s_live;
                current.start = geom::Vec3{current.start.x + current.dir.x * s_live,
                                           current.start.y + current.dir.y * s_live,
                                           current.start.z + current.dir.z * s_live};
                current.length -= s_live;
                cart_piece_tick_ = 0;
                cart_window_entry_v_ = live.velocity < 0.0 ? 0.0 : live.velocity;
                cart_window_entry_a_ = live.acceleration;
                cart_window_[tail_index].length -= passthrough ? 0.0 : trim;
            }
            if(!degrade) {
            if(!passthrough) {
                CartPiece piece{};
                piece.corner = true;
                piece.blend = corner;
                piece.length = corner.length;
                piece.cap = corner_cap;
                cart_window_.push_back(piece);
            }
            CartPiece line{};
            const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                                out_vec.z / len_b};
            line.start = passthrough
                             ? tail_end
                             : geom::Vec3{tail_end.x + t1.x * trim,
                                          tail_end.y + t1.y * trim,
                                          tail_end.z + t1.z * trim};
            line.dir = t1;
            line.length = passthrough ? len_b : len_b - trim;
            line.cap = command.velocity;
            cart_window_.push_back(line);
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                cart_tail_joints_[i] = chain[i];
            }
            cart_window_acc_ = cart_window_acc_ < command.acceleration
                                   ? cart_window_acc_
                                   : command.acceleration;
            cart_window_dec_ = cart_window_dec_ < command.deceleration
                                   ? cart_window_dec_
                                   : command.deceleration;
            cart_window_jerk_ = cart_window_jerk_ < command.jerk
                                    ? cart_window_jerk_
                                    : command.jerk;
            if(!cart_window_rebuild()) {
                // The rebuild failing after commit would strand geometry;
                // fall back to an immediate errorstop-free degrade: brake.
                cart_window_reset();
                status_ = GroupStatus::standby;
                return rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::infeasible);
            }
            cart_window_last_id_ = command.command_id;
            return rt::Result<std::uint32_t>::success(command.command_id);
            }
        }

        // Reported degradation: plain buffered Cartesian segment behind the
        // window (or behind the active segment).
        last_blend_degraded_id_ = command.command_id;
        command.buffer_mode = BufferMode::buffered;
        command.transition_mode = TransitionMode::none;
        command.transition_parameter = 0.0;
        const rt::ErrorCode prepared = prepare_cartesian_linear(command);
        if(prepared != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(prepared);
        }
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    void cart_window_convert(const GroupCommand &command, double trim,
                             bool passthrough)
    {
        const otg::State1D live =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        double s_live = live.position < 0.0 ? 0.0 : live.position;
        s_live = s_live > active_path_length_ ? active_path_length_ : s_live;
        const double len = geom::norm(active_cart_.delta);
        const geom::Vec3 dir{active_cart_.delta.x / len,
                             active_cart_.delta.y / len,
                             active_cart_.delta.z / len};
        CartPiece first{};
        first.start = geom::Vec3{active_cart_.start.x + dir.x * s_live,
                                 active_cart_.start.y + dir.y * s_live,
                                 active_cart_.start.z + dir.z * s_live};
        first.dir = dir;
        first.length = (active_path_length_ - s_live) -
                       (passthrough ? 0.0 : trim);
        first.cap = active_command_.velocity;
        cart_window_.clear();
        cart_window_.push_back(first);
        cart_window_entry_v_ = live.velocity < 0.0 ? 0.0 : live.velocity;
        cart_window_entry_a_ = live.acceleration;
        cart_window_acc_ = active_command_.acceleration;
        cart_window_dec_ = active_command_.deceleration;
        cart_window_jerk_ = active_command_.jerk;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            cart_window_joints_[i] = cart_joints_[i];
        }
        (void)command;
        active_ = false;
        cart_window_active_ = true;
        cart_piece_index_ = 0;
        cart_piece_tick_ = 0;
        status_ = GroupStatus::moving;
    }

    // Bidirectional node scan and per-line profile planning over every
    // piece from the current one onward. Committed pieces before the
    // current index are never touched.
    bool cart_window_rebuild()
    {
        const double acc = cart_window_acc_;
        const double dec = cart_window_dec_;
        const double jerk = cart_window_jerk_;
        const std::size_t count = cart_window_.size();

        // Forward pass: reachable node velocities.
        double v = cart_window_entry_v_;
        for(std::size_t i = cart_piece_index_; i < count; ++i) {
            CartPiece &piece = cart_window_[i];
            if(piece.corner) {
                v = v < piece.cap ? v : piece.cap;
                piece.v_in = v;
                piece.v_out = v;
                continue;
            }
            piece.v_in = v;
            double reach = plan::jerk_reachable_speed(v, piece.length, acc, jerk);
            reach = reach < piece.cap ? reach : piece.cap;
            if(cartesian_velocity_limit_ > 0.0 &&
               reach > cartesian_velocity_limit_) {
                reach = cartesian_velocity_limit_;
            }
            piece.v_out = reach;
            v = reach;
        }
        // Backward pass: terminal rest.
        v = 0.0;
        for(std::size_t r = count; r > cart_piece_index_; --r) {
            CartPiece &piece = cart_window_[r - 1];
            if(piece.corner) {
                v = v < piece.cap ? v : piece.cap;
                piece.v_out = piece.v_out < v ? piece.v_out : v;
                piece.v_in = piece.v_out;
                v = piece.v_in;
                continue;
            }
            piece.v_out = piece.v_out < v ? piece.v_out : v;
            double reach =
                plan::jerk_reachable_speed(piece.v_out, piece.length, dec, jerk);
            piece.v_in = piece.v_in < reach ? piece.v_in : reach;
            v = piece.v_in;
        }
        for(std::size_t i = cart_piece_index_; i < count; ++i) {
            CartPiece &piece = cart_window_[i];
            if(piece.corner) {
                const double speed = piece.v_in > 1e-12 ? piece.v_in : 1e-12;
                piece.duration =
                    static_cast<std::int64_t>(piece.length / speed) + 1;
                continue;
            }
            const bool live_entry = i == cart_piece_index_;
            const double entry_v = live_entry ? cart_window_entry_v_ : piece.v_in;
            const double entry_a = live_entry ? cart_window_entry_a_ : 0.0;
            double cap = piece.cap;
            if(cartesian_velocity_limit_ > 0.0 && cap > cartesian_velocity_limit_) {
                cap = cartesian_velocity_limit_;
            }
            const otg::Limits1D lim{cap, acc, dec, jerk};
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {0.0, entry_v, entry_a}, {piece.length, piece.v_out, 0.0},
                lim);
            if(!profile) {
                return false;
            }
            piece.profile = profile.value();
            piece.duration = piece.profile.duration_cycles();
            const double avg_v = entry_v > piece.v_out
                ? entry_v : (piece.v_out > 1e-12 ? piece.v_out : entry_v);
            if(avg_v > 1e-12) {
                const std::int64_t ideal = static_cast<std::int64_t>(
                    std::ceil(piece.length / avg_v));
                if(piece.duration > ideal + 4) {
                    for(std::int64_t t = ideal; t <= ideal + 4; ++t) {
                        const rt::Result<otg::Profile1D> ft =
                            otg::solve_fixed_time(
                                {0.0, entry_v, entry_a},
                                {piece.length, piece.v_out, 0.0}, lim, t);
                        if(ft) {
                            piece.profile = ft.value();
                            piece.duration = ft.value().duration_cycles();
                            break;
                        }
                    }
                }
            }
        }
        return true;
    }

    geom::Vec3 cart_piece_point(const CartPiece &piece, double s) const
    {
        if(piece.corner) {
            const double u = geom::quintic_parameter_at_length(piece.blend, s);
            return geom::quintic_point(piece.blend, u);
        }
        const double clamped = s < 0.0 ? 0.0 : (s > piece.length ? piece.length : s);
        return geom::Vec3{piece.start.x + piece.dir.x * clamped,
                          piece.start.y + piece.dir.y * clamped,
                          piece.start.z + piece.dir.z * clamped};
    }

    bool cart_window_emit(geom::Vec3 point)
    {
        double q[MaxAxes] = {};
        rt::ErrorCode solved =
            kinematics_->inverse(point, cart_window_joints_, axes_.size(), q);
        if(solved == rt::ErrorCode::ok &&
           kinematics_->singularity_margin(q, axes_.size()) <
               kinematics_min_margin_) {
            solved = rt::ErrorCode::precondition_failed;
        }
        if(solved != rt::ErrorCode::ok) {
            last_cartesian_error_ = solved;
            abort_motion();
            set_group_error(solved);
            return false;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(q[i]);
            cart_window_joints_[i] = q[i];
        }
        return true;
    }

    void cart_window_cycle()
    {
        if(cart_window_stopping_) {
            ++cart_halt_tick_;
            const otg::State1D state = otg::sample(
                cart_halt_profile_, rt::CycleTick::from_cycles(cart_halt_tick_));
            const geom::Vec3 point =
                cart_window_point_at(cart_halt_origin_ + state.position);
            if(!cart_window_emit(point)) {
                return;
            }
            if(cart_halt_tick_ >= cart_halt_duration_) {
                cart_window_reset();
                status_ = GroupStatus::standby;
                start_next_queued();
            }
            return;
        }

        ++cart_piece_tick_;
        CartPiece &piece = cart_window_[cart_piece_index_];
        double s = 0.0;
        if(piece.corner) {
            s = piece.v_in * static_cast<double>(cart_piece_tick_);
            s = s > piece.length ? piece.length : s;
        } else {
            const otg::State1D state = otg::sample(
                piece.profile, rt::CycleTick::from_cycles(cart_piece_tick_));
            s = state.position;
        }
        if(!cart_window_emit(cart_piece_point(piece, s))) {
            return;
        }
        if(cart_piece_tick_ >= piece.duration) {
            if(cart_piece_index_ + 1 < cart_window_.size()) {
                ++cart_piece_index_;
                cart_piece_tick_ = 0;
                // Entry state for the freshly entered piece.
                const CartPiece &next = cart_window_[cart_piece_index_];
                cart_window_entry_v_ = next.v_in;
                cart_window_entry_a_ = 0.0;
            } else {
                cart_window_reset();
                status_ = GroupStatus::standby;
                start_next_queued();
            }
        }
    }

    // Composite arc-length lookup from the live point onward (halt walker).
    geom::Vec3 cart_window_point_at(double composite) const
    {
        double remaining = composite;
        for(std::size_t i = cart_piece_index_; i < cart_window_.size(); ++i) {
            const CartPiece &piece = cart_window_[i];
            double offset = 0.0;
            if(i == cart_piece_index_) {
                offset = cart_halt_piece_offset_;
            }
            const double available = piece.length - offset;
            if(remaining <= available) {
                return cart_piece_point(piece, offset + remaining);
            }
            remaining -= available;
        }
        const CartPiece &last = cart_window_[cart_window_.size() - 1];
        return cart_piece_point(last, last.length);
    }

    rt::ErrorCode cart_window_stop(double deceleration, double jerk)
    {
        if(!std::isfinite(deceleration) || deceleration <= 0.0 ||
           !std::isfinite(jerk) || jerk <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        CartPiece &piece = cart_window_[cart_piece_index_];
        otg::State1D state{};
        if(piece.corner) {
            double s = piece.v_in * static_cast<double>(cart_piece_tick_);
            s = s > piece.length ? piece.length : s;
            state = {s, piece.v_in, 0.0};
        } else {
            state = otg::sample(piece.profile,
                                rt::CycleTick::from_cycles(cart_piece_tick_));
        }
        double remaining = piece.length - state.position;
        for(std::size_t i = cart_piece_index_ + 1; i < cart_window_.size(); ++i) {
            remaining += cart_window_[i].length;
        }
        const otg::Limits1D halt_limits{state.velocity > 1e-12 ? state.velocity
                                                               : 1e-12,
                                        deceleration, deceleration, jerk};
        double target = state.velocity * state.velocity / (2.0 * deceleration) +
                        state.velocity * (deceleration / jerk);
        if(target > remaining) {
            target = remaining;
        }
        rt::Result<otg::Profile1D> halt =
            rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
        for(int attempt = 0; attempt < 8; ++attempt) {
            halt = otg::plan_time_optimal({0.0, state.velocity, state.acceleration},
                                          {target, 0.0, 0.0}, halt_limits);
            if(halt || target >= remaining) {
                break;
            }
            target = target * 1.5 < remaining ? target * 1.5 : remaining;
        }
        if(!halt) {
            // Immediate stop fallback (same as the group linear path).
            cart_window_reset();
            queue_.clear();
            status_ = GroupStatus::standby;
            return rt::ErrorCode::ok;
        }
        cart_halt_profile_ = halt.value();
        cart_halt_duration_ = cart_halt_profile_.duration_cycles();
        cart_halt_tick_ = 0;
        cart_halt_origin_ = 0.0;
        cart_halt_piece_offset_ = state.position;
        cart_window_stopping_ = true;
        queue_.clear();
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
    }

    void cart_window_reset()
    {
        cart_window_.clear();
        cart_window_active_ = false;
        cart_window_stopping_ = false;
        cart_piece_index_ = 0;
        cart_piece_tick_ = 0;
        cart_halt_tick_ = 0;
        cart_halt_duration_ = 0;
        cart_halt_origin_ = 0.0;
        cart_halt_piece_offset_ = 0.0;
    }

    // Cartesian v2-C (approved addendum): fuse the active Cartesian line,
    // a Cartesian-space quintic corner inside the tolerance band, and the
    // successor line into one chain driven by one profile planned from the
    // live path state. The chain velocity carries the corner curvature cap;
    // orientation rides a single geodesic over the whole chain (declared).
    // Reflex corners, too-late submissions, and chains that do not beat the
    // full-stop baseline degrade to BUFFERED and are reported.
    rt::Result<std::uint32_t> submit_cartesian_blend(GroupCommand command)
    {
        if(command.command_id == 0) {
            command.command_id = next_command_id_++;
        }
        if(command.coord_system != CoordSystem::mcs &&
           command.coord_system != CoordSystem::pcs) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.relative ||
           (kinematics_ == nullptr && pose_kinematics_ == nullptr)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        // Mixed-mode blending, arc/rotation-driven actives, committed
        // chains, windows, and non-empty queues are all outside the v1
        // fusion shape.
        if(!active_ || status_ != GroupStatus::moving ||
           active_kind_ != GroupPathKind::cartesian_linear ||
           active_cart_.arc_path || active_cart_.angle_driven ||
           active_cart_.chain || window_active_ || !queue_.empty() ||
           active_path_length_ <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        // Successor target in the Cartesian (TCP) domain.
        geom::Vec3 target_point = cartesian_part(command.target);
        geom::RigidTransform target_pose{};
        if(pose_kinematics_ != nullptr) {
            target_pose = geom::make_rpy_transform(
                command.target.value[0], command.target.value[1],
                command.target.value[2], command.target.value[3],
                command.target.value[4], command.target.value[5]);
            if(command.coord_system == CoordSystem::pcs) {
                target_pose = geom::compose(workpiece_frame_, target_pose);
            }
            target_point = target_pose.translation;
        } else {
            if(command.coord_system == CoordSystem::pcs) {
                target_point = geom::transform_point(workpiece_frame_, target_point);
            }
            target_point = target_point - tool_offset_;
        }

        // Live path state and geometry.
        const otg::State1D live =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        const double s_live = live.position < 0.0
                                  ? 0.0
                                  : (live.position > active_path_length_
                                         ? active_path_length_
                                         : live.position);
        const double remaining = active_path_length_ - s_live;
        const geom::Vec3 live_point =
            cartesian_point_at(active_cart_, s_live / active_path_length_);
        const geom::Vec3 corner_point = geom::Vec3{
            active_cart_.start.x + active_cart_.delta.x,
            active_cart_.start.y + active_cart_.delta.y,
            active_cart_.start.z + active_cart_.delta.z};
        const double active_len = geom::norm(active_cart_.delta);
        if(active_len <= 1e-12) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        const geom::Vec3 t0{active_cart_.delta.x / active_len,
                            active_cart_.delta.y / active_len,
                            active_cart_.delta.z / active_len};
        const geom::Vec3 out_vec = target_point - corner_point;
        const double len_b = geom::norm(out_vec);
        const double dot = len_b > 1e-12
                               ? (t0.x * out_vec.x + t0.y * out_vec.y +
                                  t0.z * out_vec.z) /
                                     len_b
                               : -1.0;

        bool degrade = false;
        double trim = 0.0;
        bool passthrough = false;
        if(len_b <= 1e-12 || dot <= -0.999) {
            degrade = true;
        } else if(dot >= 1.0 - 1e-9) {
            passthrough = true;
        } else {
            const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                                out_vec.z / len_b};
            const double turn = geom::norm(t1 - t0);
            trim = command.transition_parameter * 96.0 / (23.0 * turn);
            const double room = remaining < len_b ? remaining : len_b;
            if(trim > 0.5 * room) {
                trim = 0.5 * room;
            }
            if(trim <= 1e-9 || remaining <= trim) {
                degrade = true;
            }
        }

        CartesianSegment segment{};
        rt::Result<otg::Profile1D> chain_profile =
            rt::Result<otg::Profile1D>::failure(rt::ErrorCode::invalid_argument);
        if(!degrade) {
            const geom::Vec3 t1{out_vec.x / len_b, out_vec.y / len_b,
                                out_vec.z / len_b};
            segment.pose = active_cart_.pose;
            segment.chain = true;
            segment.start = live_point;
            segment.dir1 = t0;
            if(passthrough) {
                segment.line1 = remaining;
                segment.exit_point = corner_point;
            } else {
                const geom::Vec3 entry{corner_point.x - t0.x * trim,
                                       corner_point.y - t0.y * trim,
                                       corner_point.z - t0.z * trim};
                const geom::Vec3 exit{corner_point.x + t1.x * trim,
                                      corner_point.y + t1.y * trim,
                                      corner_point.z + t1.z * trim};
                const rt::Result<geom::QuinticBlendSegment> blend =
                    geom::make_quintic_blend(entry, corner_point, exit,
                                             command.transition_parameter);
                if(!blend) {
                    degrade = true;
                } else {
                    segment.corner = blend.value();
                    segment.line1 = remaining - trim;
                    segment.exit_point = exit;
                }
            }
            segment.dir2 = t1;
            segment.line2 = len_b - trim;
            segment.delta = target_point - live_point;
            segment.length = segment.line1 + segment.corner.length + segment.line2;

            if(!degrade && segment.pose) {
                const geom::RigidTransform live_tcp = pose_start_tcp(cart_joints_);
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        segment.rotation_start[i][j] = live_tcp.rotation[i][j];
                    }
                }
                geom::relative_axis_angle(live_tcp.rotation, target_pose.rotation,
                                          segment.axis, segment.angle);
                if(segment.angle >= 3.14159265358979323846 - 1e-6) {
                    return rt::Result<std::uint32_t>::failure(
                        rt::ErrorCode::invalid_argument);
                }
            }
        }

        if(!degrade) {
            // Chain envelope: both commands and the corner curvature cap.
            GroupCommand fused = command;
            fused.velocity = fused.velocity < active_command_.velocity
                                 ? fused.velocity
                                 : active_command_.velocity;
            fused.acceleration = fused.acceleration < active_command_.acceleration
                                     ? fused.acceleration
                                     : active_command_.acceleration;
            fused.deceleration = fused.deceleration < active_command_.deceleration
                                     ? fused.deceleration
                                     : active_command_.deceleration;
            fused.jerk =
                fused.jerk < active_command_.jerk ? fused.jerk : active_command_.jerk;
            if(segment.corner.max_curvature > 1e-12) {
                const double axis_accel =
                    fused.acceleration < fused.deceleration ? fused.acceleration
                                                            : fused.deceleration;
                const double cap = std::sqrt(axis_accel / segment.corner.max_curvature);
                if(cap < fused.velocity) {
                    fused.velocity = cap;
                }
            }
            double chain_seed[MaxAxes] = {};
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                chain_seed[i] = cart_joints_[i];
            }
            const rt::ErrorCode validated =
                prevalidate_cartesian(fused, segment, chain_seed);
            if(validated != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(validated);
            }
            chain_profile = otg::plan_time_optimal(
                {0.0, live.velocity, live.acceleration},
                {fused.cart.length, 0.0, 0.0},
                {fused.velocity, fused.acceleration, fused.deceleration, fused.jerk});
            if(!chain_profile) {
                degrade = true;
            } else {
                // Constructive gate: the fused chain must beat the full-stop
                // baseline (finish the active segment, then run the successor
                // from rest).
                const rt::Result<otg::Profile1D> tail = otg::plan_time_optimal(
                    {0.0, 0.0, 0.0}, {len_b, 0.0, 0.0},
                    {command.velocity, command.acceleration, command.deceleration,
                     command.jerk});
                if(tail) {
                    const std::int64_t baseline = (active_duration_ - active_tick_) +
                                                  tail.value().duration_cycles();
                    if(chain_profile.value().duration_cycles() >= baseline) {
                        degrade = true;
                    }
                } else {
                    degrade = true;
                }
            }
            if(!degrade) {
                active_command_ = fused;
                active_cart_ = fused.cart;
                active_kind_ = GroupPathKind::cartesian_linear;
                active_arc_ = geom::ArcSegment{};
                active_path_length_ = fused.cart.length;
                active_profile_ = chain_profile.value();
                active_tick_ = 0;
                active_duration_ = active_profile_.duration_cycles();
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    active_start_[i] = axes_[i]->snapshot().command_position;
                    active_finish_[i] = fused.target.value[i];
                }
                return rt::Result<std::uint32_t>::success(command.command_id);
            }
        }

        // Reported degradation to a plain buffered Cartesian segment.
        last_blend_degraded_id_ = command.command_id;
        command.buffer_mode = BufferMode::buffered;
        command.transition_mode = TransitionMode::none;
        command.transition_parameter = 0.0;
        const rt::ErrorCode prepared = prepare_cartesian_linear(command);
        if(prepared != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(prepared);
        }
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    rt::ErrorCode prepare_cartesian_linear(GroupCommand &command)
    {
        const rt::ErrorCode guarded = cartesian_guards(command);
        if(guarded != rt::ErrorCode::ok) {
            return guarded;
        }
        double chain[MaxAxes] = {};
        segment_start_joints(command, chain);

        CartesianSegment segment{};
        if(pose_kinematics_ != nullptr) {
            segment.pose = true;
            geom::RigidTransform target = geom::make_rpy_transform(
                command.target.value[0], command.target.value[1],
                command.target.value[2], command.target.value[3],
                command.target.value[4], command.target.value[5]);
            if(command.coord_system == CoordSystem::pcs) {
                target = geom::compose(workpiece_frame_, target);
            }
            const geom::RigidTransform start = pose_start_tcp(chain);
            segment.start = start.translation;
            segment.delta = target.translation - start.translation;
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    segment.rotation_start[i][j] = start.rotation[i][j];
                }
            }
            geom::relative_axis_angle(start.rotation, target.rotation, segment.axis,
                                      segment.angle);
            if(segment.angle >= 3.14159265358979323846 - 1e-6) {
                return rt::ErrorCode::invalid_argument;
            }
            segment.length = geom::norm(segment.delta);
            if(segment.length < 1e-12 && segment.angle > 0.0) {
                segment.angle_driven = true;
                segment.length = segment.angle;
            }
        } else {
            geom::Vec3 point = cartesian_part(command.target);
            if(command.coord_system == CoordSystem::pcs) {
                point = geom::transform_point(workpiece_frame_, point);
            }
            point = point - tool_offset_;
            geom::Vec3 start{};
            const rt::ErrorCode forwarded =
                kinematics_->forward(chain, axes_.size(), start);
            if(forwarded != rt::ErrorCode::ok) {
                return forwarded;
            }
            segment.start = start;
            segment.delta = point - start;
            segment.length = geom::norm(segment.delta);
        }
        return prevalidate_cartesian(command, segment, chain);
    }

    // Cartesian v2-B (approved addendum): three-point BORDER arcs in the
    // Cartesian XY plane, z following the path parameter linearly — the
    // KB-030 plane convention transplanted to the TCP domain (arbitrary
    // spatial arc planes stay a follow-up, recorded). Pose groups ride the
    // start-to-target geodesic along the arc fraction; aux orientation
    // slots are ignored (declared).
    rt::ErrorCode prepare_cartesian_circular(GroupCommand &command)
    {
        const rt::ErrorCode guarded = cartesian_guards(command);
        if(guarded != rt::ErrorCode::ok) {
            return guarded;
        }
        const bool pcs = command.coord_system == CoordSystem::pcs;
        double chain[MaxAxes] = {};
        segment_start_joints(command, chain);

        CartesianSegment segment{};
        geom::Vec3 start_point{};
        geom::Vec3 aux_point = cartesian_part(command.aux);
        geom::Vec3 target_point = cartesian_part(command.target);
        if(pose_kinematics_ != nullptr) {
            segment.pose = true;
            geom::RigidTransform target = geom::make_rpy_transform(
                command.target.value[0], command.target.value[1],
                command.target.value[2], command.target.value[3],
                command.target.value[4], command.target.value[5]);
            if(pcs) {
                target = geom::compose(workpiece_frame_, target);
                aux_point = geom::transform_point(workpiece_frame_, aux_point);
            }
            const geom::RigidTransform start = pose_start_tcp(chain);
            start_point = start.translation;
            target_point = target.translation;
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    segment.rotation_start[i][j] = start.rotation[i][j];
                }
            }
            geom::relative_axis_angle(start.rotation, target.rotation, segment.axis,
                                      segment.angle);
            if(segment.angle >= 3.14159265358979323846 - 1e-6) {
                return rt::ErrorCode::invalid_argument;
            }
        } else {
            if(pcs) {
                aux_point = geom::transform_point(workpiece_frame_, aux_point);
                target_point = geom::transform_point(workpiece_frame_, target_point);
            }
            aux_point = aux_point - tool_offset_;
            target_point = target_point - tool_offset_;
            geom::Vec3 start{};
            const rt::ErrorCode forwarded =
                kinematics_->forward(chain, axes_.size(), start);
            if(forwarded != rt::ErrorCode::ok) {
                return forwarded;
            }
            start_point = start;
        }

        const geom::Vec3 plane_start{start_point.x, start_point.y, 0.0};
        const geom::Vec3 plane_aux{aux_point.x, aux_point.y, 0.0};
        const geom::Vec3 plane_finish{target_point.x, target_point.y, 0.0};
        constexpr double PointTolerance = 1e-12;
        if(geom::norm(plane_aux - plane_start) <= PointTolerance ||
           geom::norm(plane_finish - plane_aux) <= PointTolerance ||
           geom::norm(plane_finish - plane_start) <= PointTolerance) {
            return rt::ErrorCode::invalid_argument;
        }
        const rt::Result<geom::ArcSegment> arc =
            geom::make_arc(plane_start, plane_aux, plane_finish);
        if(!arc) {
            return rt::ErrorCode::invalid_argument;
        }
        if(arc.value().radius > geom::norm(plane_finish - plane_start) * 1e6) {
            return rt::ErrorCode::invalid_argument;
        }
        const CircPathChoice derived = arc.value().sweep >= 0.0
                                           ? CircPathChoice::counter_clockwise
                                           : CircPathChoice::clockwise;
        if(derived != command.path_choice) {
            return rt::ErrorCode::invalid_argument;
        }
        segment.arc_path = true;
        segment.arc = arc.value();
        segment.start = start_point;
        segment.delta = target_point - start_point;
        segment.length = arc.value().length;
        return prevalidate_cartesian(command, segment, chain);
    }

    rt::ErrorCode cartesian_guards(const GroupCommand &command) const
    {
        if(command.coord_system != CoordSystem::mcs &&
           command.coord_system != CoordSystem::pcs) {
            return rt::ErrorCode::unsupported;
        }
        if(command.relative || command.buffer_mode == BufferMode::blending_low ||
           command.buffer_mode == BufferMode::blending_high ||
           command.transition_mode != TransitionMode::none ||
           command.transition_parameter != 0.0) {
            return rt::ErrorCode::unsupported;
        }
        if(kinematics_ == nullptr && pose_kinematics_ == nullptr) {
            return rt::ErrorCode::unsupported;
        }
        return rt::ErrorCode::ok;
    }

    void segment_start_joints(const GroupCommand &command, double *chain) const
    {
        const bool aborting = command.buffer_mode == BufferMode::aborting;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            chain[i] = aborting ? axes_[i]->snapshot().command_position
                                : queued_finish(i);
        }
    }

    geom::RigidTransform pose_start_tcp(const double *chain) const
    {
        kin::Pose6 flange{};
        pose_kinematics_->forward(chain, flange);
        geom::RigidTransform start{};
        start.translation = geom::Vec3{flange.position[0], flange.position[1],
                                       flange.position[2]};
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                start.rotation[i][j] = flange.rotation[i][j];
            }
        }
        return geom::compose(start, pose_tool_);
    }

    // Shared 33-sample pre-validation and command commit for every
    // Cartesian segment shape.
    rt::ErrorCode prevalidate_cartesian(GroupCommand &command,
                                        CartesianSegment &segment,
                                        double *chain)
    {
        constexpr int Samples = 32;
        double q[MaxAxes] = {};
        double worst_step = 0.0;

        for(int k = 0; k <= Samples; ++k) {
            const double fraction =
                static_cast<double>(k) / static_cast<double>(Samples);
            rt::ErrorCode solved = rt::ErrorCode::ok;
            if(segment.pose) {
                geom::RigidTransform tcp{};
                cartesian_pose_at(segment, fraction, tcp);
                const geom::RigidTransform flange_target =
                    geom::compose(tcp, pose_tool_inverse_);
                kin::Pose6 pose{};
                pose.position[0] = flange_target.translation.x;
                pose.position[1] = flange_target.translation.y;
                pose.position[2] = flange_target.translation.z;
                for(int i = 0; i < 3; ++i) {
                    for(int j = 0; j < 3; ++j) {
                        pose.rotation[i][j] = flange_target.rotation[i][j];
                    }
                }
                solved = pose_kinematics_->inverse(pose, chain,
                                                   pose_max_joint_step_, q);
                if(solved == rt::ErrorCode::ok &&
                   pose_kinematics_->singularity_margin(q) < pose_min_margin_) {
                    return rt::ErrorCode::precondition_failed;
                }
            } else {
                const geom::Vec3 sample = cartesian_point_at(segment, fraction);
                solved = kinematics_->inverse(sample, chain, axes_.size(), q);
                if(solved == rt::ErrorCode::ok &&
                   kinematics_->singularity_margin(q, axes_.size()) <
                       kinematics_min_margin_) {
                    return rt::ErrorCode::precondition_failed;
                }
            }
            if(solved != rt::ErrorCode::ok) {
                return solved;
            }
            if(k > 0) {
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    const double step = std::fabs(q[i] - chain[i]);
                    if(step > worst_step) {
                        worst_step = step;
                    }
                }
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                chain[i] = q[i];
            }
        }
        // Velocity budget (decision #6): the step gate doubles as the joint
        // velocity budget with a safety factor of two (pose pipeline).
        if(segment.pose && worst_step > 0.0 && segment.length > 0.0) {
            const double per_unit =
                worst_step / (segment.length / static_cast<double>(Samples));
            const double allowed = 0.5 * pose_max_joint_step_ / per_unit;
            if(allowed < command.velocity) {
                command.velocity = allowed;
            }
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            command.target.value[i] = q[i];
        }
        if(cartesian_velocity_limit_ > 0.0 &&
           cartesian_velocity_limit_ < command.velocity) {
            command.velocity = cartesian_velocity_limit_;
        }
        command.coord_system = CoordSystem::acs;
        command.path_kind = GroupPathKind::cartesian_linear;
        command.cart = segment;
        return preflight_member_targets(command.target);
    }

    geom::Vec3 cartesian_point_at(const CartesianSegment &segment,
                                  double fraction) const
    {
        if(segment.chain) {
            const double s = fraction * segment.length;
            if(s <= segment.line1) {
                return geom::Vec3{segment.start.x + segment.dir1.x * s,
                                  segment.start.y + segment.dir1.y * s,
                                  segment.start.z + segment.dir1.z * s};
            }
            const double in_corner = s - segment.line1;
            if(in_corner <= segment.corner.length) {
                const double u =
                    geom::quintic_parameter_at_length(segment.corner, in_corner);
                return geom::quintic_point(segment.corner, u);
            }
            const double tail = s - segment.line1 - segment.corner.length;
            return geom::Vec3{segment.exit_point.x + segment.dir2.x * tail,
                              segment.exit_point.y + segment.dir2.y * tail,
                              segment.exit_point.z + segment.dir2.z * tail};
        }
        if(segment.arc_path) {
            geom::Vec3 point =
                geom::sample(segment.arc, fraction * segment.arc.length);
            point.z = segment.start.z + segment.delta.z * fraction;
            return point;
        }
        return geom::Vec3{segment.start.x + segment.delta.x * fraction,
                          segment.start.y + segment.delta.y * fraction,
                          segment.start.z + segment.delta.z * fraction};
    }

    void cartesian_pose_at(const CartesianSegment &segment,
                           double fraction,
                           geom::RigidTransform &tcp) const
    {
        tcp.translation = cartesian_point_at(segment, fraction);
        double relative[3][3];
        geom::rodrigues(segment.axis, segment.angle * fraction, relative);
        geom::rotation_multiply(segment.rotation_start, relative, tcp.rotation);
    }

    // Cycle-path Cartesian sampling (approved matrix decisions #3/#4/#7):
    // one analytic inverse per cycle, seeded by the previous cycle's
    // joints. A failure between the pre-validation samples is the declared
    // group errorstop — members keep the last good setpoint, nothing
    // extrapolates, nothing flips branches.
    bool cartesian_cycle(double ratio)
    {
        double q[MaxAxes] = {};
        rt::ErrorCode solved = rt::ErrorCode::ok;
        if(active_cart_.pose) {
            geom::RigidTransform tcp{};
            cartesian_pose_at(active_cart_, ratio, tcp);
            const geom::RigidTransform flange_target =
                geom::compose(tcp, active_pose_tool_inverse_);
            kin::Pose6 pose{};
            pose.position[0] = flange_target.translation.x;
            pose.position[1] = flange_target.translation.y;
            pose.position[2] = flange_target.translation.z;
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    pose.rotation[i][j] = flange_target.rotation[i][j];
                }
            }
            solved = pose_kinematics_->inverse(pose, cart_joints_,
                                               pose_max_joint_step_, q);
            if(solved == rt::ErrorCode::ok &&
               pose_kinematics_->singularity_margin(q) < pose_min_margin_) {
                solved = rt::ErrorCode::precondition_failed;
            }
        } else {
            const geom::Vec3 point = cartesian_point_at(active_cart_, ratio);
            solved = kinematics_->inverse(point, cart_joints_, axes_.size(), q);
            if(solved == rt::ErrorCode::ok &&
               kinematics_->singularity_margin(q, axes_.size()) <
                   kinematics_min_margin_) {
                solved = rt::ErrorCode::precondition_failed;
            }
        }
        if(solved != rt::ErrorCode::ok) {
            last_cartesian_error_ = solved;
            abort_motion();
            set_group_error(solved);
            return false;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(q[i]);
            cart_joints_[i] = q[i];
        }
        return true;
    }

    double queued_finish(std::size_t axis_index) const
    {
        double finish =
            window_active_
                ? window_[window_.size() - 1].target[axis_index]
                : (cart_window_active_
                       ? cart_tail_joints_[axis_index]
                       : (active_ ? active_finish_[axis_index]
                                  : axes_[axis_index]->snapshot().command_position));
        for(std::size_t i = 0; i < queue_.size(); ++i) {
            const GroupCommand &queued = queue_[i];
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
        active_pose_tool_inverse_ = command.tool_inverse;
        active_pose_tool_ = geom::invert(command.tool_inverse);
        active_tool_offset_ = {active_pose_tool_.translation.x,
                               active_pose_tool_.translation.y,
                               active_pose_tool_.translation.z};
        active_command_ = command;
        if(command.dynamic_pcs && pending_dynamic_pcs_) {
            active_dynamic_reference_frame_ = pending_dynamic_reference_frame_;
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
        active_cart_ = command.cart;
        if(command.path_kind == GroupPathKind::cartesian_linear) {
            longest = command.cart.length;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                cart_joints_[i] = active_start_[i];
            }
        }
        active_path_length_ = longest;

        // Y7 (KB-051 fix): velocity-continuous aborting takeover via
        // tolerance-tube connector. The pre-takeover velocity is decomposed
        // into an along-path scalar (projected onto the new path tangent)
        // and a lateral residual that decays to zero via an independent
        // jerk-limited profile (approved v2.1 matrix, linear scope).
        const bool try_connector = connector_.take_captured_velocity() &&
                                   command.path_kind == GroupPathKind::linear;

        if(try_connector) {
            const rt::ErrorCode planned = connector_.plan(
                {command.velocity, command.acceleration, command.deceleration,
                 command.jerk},
                longest, axes_.size(), active_start_, active_finish_,
                active_profile_, active_duration_);
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
        start(next);
    }

    void abort_motion()
    {
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
        direct_active_ = false;
        direct_stopping_ = false;
        override_paused_ = false;
        interrupting_ = false;
        interrupted_plain_ = false;
        interrupted_window_ = false;
        window_reset();
        cart_window_reset();
        queue_.clear();
        active_tick_ = 0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
    }

    // Y7 (KB-051/052): forwards to the extracted connector cluster; only
    // plain linear motions qualify (approved v2.1 scope).
    void capture_takeover_velocity()
    {
        connector_.capture(active_, active_kind_ == GroupPathKind::linear,
                           active_path_length_, active_profile_, active_tick_,
                           active_start_, active_finish_, axes_.size());
    }

    // A5 look-ahead window (KB-032, approved A5 matrix): consecutive blending
    // successors form a window of linear segments joined by quintic corner
    // curves. Node velocities come from a trapezoid-level bidirectional scan
    // capped by the corner curvature bound; every segment runs its own
    // jerk-limited profile between node velocities (the OTG nonzero-target
    // cruise domain), so straight parts are no longer dragged down to the
    // sharpest corner speed. All planning happens synchronously at submit;
    // the cycle path only samples precomputed data.
    static constexpr std::size_t BlendTableSize = 33;
    static constexpr std::size_t WindowCapacity = 64;

    struct WindowNode
    {
        bool has_curve = false;
        std::array<std::array<double, MaxAxes>, 6> ctrl{};
        std::array<double, BlendTableSize> cumulative{};
        double curve_length = 0.0;
        double corner_cap = 0.0;
        double curve_velocity = 0.0;
        std::int64_t curve_cycles = 0;
    };

    enum class WindowKind
    {
        line,
        arc,
    };

    struct WindowSegment
    {
        WindowKind kind = WindowKind::line;
        std::array<double, MaxAxes> entry{}; // line start (after entry trim)
        std::array<double, MaxAxes> dir{};   // unit direction (line only)
        std::array<double, MaxAxes> target{};
        double full_length = 0.0;            // line: corner-to-corner; arc: arc length
        double trim_in = 0.0;                // arcs are never trimmed (v2)
        double trim_out = 0.0;
        geom::ArcSegment arc_geom{};         // arc only (KB-030 plane arc)
        otg::Limits1D limits{};              // euclidean dynamics (arc: velocity
                                             // already clamped to sqrt(a*R))
        std::uint32_t command_id = 0;
        double entry_velocity = 0.0;
        double exit_velocity = 0.0;
        otg::Profile1D profile{};            // path profile entry_v -> exit_v
        WindowNode node{};                   // corner to the NEXT segment

        double line_length() const
        {
            const double length = full_length - trim_in - trim_out;
            return length > 0.0 ? length : 0.0;
        }
    };

    // N-dimensional unit tangent at a segment boundary: lines use dir; arcs
    // combine the plane tangent with the linear following of higher axes.
    void window_tangent(const WindowSegment &seg, bool at_exit,
                        std::array<double, MaxAxes> &out) const
    {
        if(seg.kind == WindowKind::line) {
            out = seg.dir;
            return;
        }
        const geom::Vec3 plane =
            geom::tangent(seg.arc_geom, at_exit ? seg.arc_geom.length : 0.0);
        out[0] = plane.x;
        out[1] = plane.y;
        double norm = plane.x * plane.x + plane.y * plane.y;
        for(std::size_t i = 2; i < axes_.size(); ++i) {
            const double slope = (seg.target[i] - seg.entry[i]) / seg.full_length;
            out[i] = slope;
            norm += slope * slope;
        }
        norm = std::sqrt(norm);
        if(norm > 0.0) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                out[i] /= norm;
            }
        }
    }

    rt::Result<std::uint32_t> submit_blend(GroupCommand command)
    {
        // v1 declared boundary: blending extends the active window (or the
        // active plain linear command); a stopping window, plain queued
        // commands, or anything else is an explicit error.
        if(window_stop_ || !queue_.empty()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.tool_number != active_tool_ ||
           command.payload_number != active_payload_) {
            return degrade_blend(command);
        }
        if(!window_active_) {
            if(!active_ || active_kind_ != GroupPathKind::linear ||
               status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        }
        if(window_active_ && window_.size() >= window_depth_) {
            // Window capacity is a declared limit, never a silent drop.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }

        // Predecessor tail geometry (window tail or the active linear command).
        std::array<double, MaxAxes> pred_target{};
        std::array<double, MaxAxes> pred_dir{};
        double pred_full = 0.0;
        double pred_trim_out_room = 0.0; // half-length truncation budget
        bool pred_is_line = true;
        otg::Limits1D pred_limits{};
        if(window_active_) {
            const WindowSegment &tail = window_[window_.size() - 1];
            pred_target = tail.target;
            window_tangent(tail, true, pred_dir);
            pred_full = tail.full_length;
            pred_limits = tail.limits;
            pred_is_line = tail.kind == WindowKind::line;
        } else {
            double length1 = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double d = active_finish_[i] - active_start_[i];
                pred_dir[i] = d;
                pred_target[i] = active_finish_[i];
                length1 += d * d;
            }
            length1 = std::sqrt(length1);
            if(length1 <= 1e-12) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                pred_dir[i] /= length1;
            }
            pred_full = length1;
            const double scale1 = length1 / active_path_length_;
            pred_limits = otg::Limits1D{active_command_.velocity * scale1,
                                        active_command_.acceleration * scale1,
                                        active_command_.deceleration * scale1,
                                        active_command_.jerk * scale1};
        }
        pred_trim_out_room = pred_is_line ? pred_full * 0.5 : 0.0;

        // Successor geometry.
        double length2 = 0.0;
        double longest2 = 0.0;
        std::array<double, MaxAxes> u2{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double d = command.target.value[i] - pred_target[i];
            u2[i] = d;
            length2 += d * d;
            const double travel = std::fabs(d);
            if(travel > longest2) {
                longest2 = travel;
            }
        }
        length2 = std::sqrt(length2);
        if(length2 <= 1e-12) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        double alignment = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            u2[i] /= length2;
            alignment += pred_dir[i] * u2[i];
        }
        const double scale2 = longest2 > 0.0 ? length2 / longest2 : 1.0;
        const otg::Limits1D limits2{command.velocity * scale2,
                                    command.acceleration * scale2,
                                    command.deceleration * scale2, command.jerk * scale2};

        if(alignment < -0.999) {
            // Reflex corner: degrade to a BUFFERED full-stop join, reported.
            return degrade_blend(command);
        }

        // Corner construction (geometry frozen at creation).
        WindowNode node{};
        double trim = 0.0;
        double corner_cap =
            pred_limits.max_velocity < limits2.max_velocity ? pred_limits.max_velocity
                                                            : limits2.max_velocity;
        if(alignment <= 0.999 && !pred_is_line) {
            // No tolerance-band curve exists between an arc and a line (v3
            // scope); a non-tangent junction degrades to a full-stop join.
            return degrade_blend(command);
        }
        if(alignment <= 0.999) {
            double turn = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double diff = u2[i] - pred_dir[i];
                turn += diff * diff;
            }
            turn = std::sqrt(turn);
            trim = command.transition_parameter * 96.0 / (23.0 * turn);
            if(trim > pred_trim_out_room) {
                trim = pred_trim_out_room;
            }
            if(trim > length2 * 0.5) {
                trim = length2 * 0.5;
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double corner = pred_target[i];
                node.ctrl[0][i] = corner - pred_dir[i] * trim;
                node.ctrl[1][i] = corner - pred_dir[i] * (trim * 2.0 / 3.0);
                node.ctrl[2][i] = corner - pred_dir[i] * (trim / 3.0);
                node.ctrl[3][i] = corner + u2[i] * (trim / 3.0);
                node.ctrl[4][i] = corner + u2[i] * (trim * 2.0 / 3.0);
                node.ctrl[5][i] = corner + u2[i] * trim;
            }
            double accumulated = 0.0;
            std::array<double, MaxAxes> previous{};
            blend_point(node.ctrl, 0.0, previous);
            node.cumulative[0] = 0.0;
            for(std::size_t step = 1; step < BlendTableSize; ++step) {
                const double u =
                    static_cast<double>(step) / static_cast<double>(BlendTableSize - 1);
                std::array<double, MaxAxes> point{};
                blend_point(node.ctrl, u, point);
                double chord = 0.0;
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    const double diff = point[i] - previous[i];
                    chord += diff * diff;
                }
                accumulated += std::sqrt(chord);
                node.cumulative[step] = accumulated;
                previous = point;
            }
            node.curve_length = accumulated;
            if(!std::isfinite(node.curve_length) || node.curve_length <= 0.0) {
                return degrade_blend(command);
            }
            double max_curvature = 0.0;
            for(std::size_t step = 0; step <= 64; ++step) {
                const double u = static_cast<double>(step) / 64.0;
                const double curvature = blend_curvature(node.ctrl, u);
                if(curvature > max_curvature) {
                    max_curvature = curvature;
                }
            }
            if(max_curvature > 0.0) {
                double junction = pred_limits.max_acceleration;
                if(pred_limits.max_deceleration < junction) {
                    junction = pred_limits.max_deceleration;
                }
                if(limits2.max_acceleration < junction) {
                    junction = limits2.max_acceleration;
                }
                if(limits2.max_deceleration < junction) {
                    junction = limits2.max_deceleration;
                }
                const double geometric = std::sqrt(junction / max_curvature);
                if(geometric < corner_cap) {
                    corner_cap = geometric;
                }
            }
            node.has_curve = true;
        }
        node.corner_cap = corner_cap;

        // Baseline for the constructive cycle-time gate (captured pre-append).
        const std::int64_t old_remaining = window_active_ ? window_remaining_cycles() : -1;

        // Convert the active linear command into window segment zero.
        bool converted = false;
        if(!window_active_) {
            if(!convert_active_linear_to_window()) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            converted = true;
        }

        // Append: tail gains the corner, the new segment enters the window.
        WindowSegment &tail = window_[window_.size() - 1];
        const double saved_trim_out = tail.trim_out;
        const WindowNode saved_node = tail.node;
        tail.trim_out = trim;
        tail.node = node;

        WindowSegment fresh{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            fresh.entry[i] = node.has_curve ? node.ctrl[5][i] : pred_target[i];
            fresh.dir[i] = u2[i];
            fresh.target[i] = command.target.value[i];
        }
        fresh.full_length = length2;
        fresh.trim_in = trim;
        fresh.limits = limits2;
        fresh.command_id = command.command_id;
        if(window_.size() >= window_depth_ ||
           window_.push_back(fresh) != rt::ErrorCode::ok) {
            tail.trim_out = saved_trim_out;
            tail.node = saved_node;
            if(converted) {
                window_.clear();
            }
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }

        bool late = false;
        if(!window_rebuild(late)) {
            window_.pop_back();
            WindowSegment &restore = window_[window_.size() - 1];
            restore.trim_out = saved_trim_out;
            restore.node = saved_node;
            if(converted) {
                window_.clear();
            } else if(!window_rebuild(late)) {
                // Restoring the previous window must succeed; if the live
                // state has drifted past a boundary, stop safely.
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                abort_motion();
            }
            return degrade_blend(command);
        }

        // Constructive cycle-time gate (KB-031 carried over): the extended
        // window must beat "previous window then a standalone full-stop move".
        if(old_remaining >= 0) {
            const rt::Result<otg::Profile1D> standalone = otg::plan_time_optimal(
                {0.0, 0.0, 0.0}, {length2, 0.0, 0.0}, limits2);
            if(standalone &&
               window_remaining_cycles() >=
                   old_remaining + standalone.value().duration_cycles()) {
                window_.pop_back();
                WindowSegment &restore = window_[window_.size() - 1];
                restore.trim_out = saved_trim_out;
                restore.node = saved_node;
                if(!window_rebuild(late)) {
                    window_reset();
                    clear_axes_synchronized();
                    status_ = GroupStatus::standby;
                    abort_motion();
                }
                return degrade_blend(command);
            }
        }

        if(converted) {
            active_ = false;
            window_active_ = true;
        }
        status_ = GroupStatus::moving;
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    // Seed the window from the active linear command: segment zero carries
    // the euclidean geometry and the live state (captured for the rebuild).
    bool convert_active_linear_to_window()
    {
        double length1 = 0.0;
        std::array<double, MaxAxes> direction{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double d = active_finish_[i] - active_start_[i];
            direction[i] = d;
            length1 += d * d;
        }
        length1 = std::sqrt(length1);
        if(length1 <= 1e-12 || active_path_length_ <= 0.0) {
            return false;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            direction[i] /= length1;
        }
        const double scale1 = length1 / active_path_length_;

        WindowSegment seg0{};
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            seg0.entry[i] = active_start_[i];
            seg0.dir[i] = direction[i];
            seg0.target[i] = active_finish_[i];
        }
        seg0.full_length = length1;
        seg0.limits = otg::Limits1D{active_command_.velocity * scale1,
                                    active_command_.acceleration * scale1,
                                    active_command_.deceleration * scale1,
                                    active_command_.jerk * scale1};
        seg0.command_id = active_command_.command_id;
        const otg::State1D raw =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        seg0.entry_velocity = raw.velocity * scale1; // updated by rebuild
        seg0.profile = active_profile_;              // replaced by rebuild
        window_.clear();
        window_.push_back(seg0);
        window_seed_state_ = otg::State1D{raw.position * scale1, raw.velocity * scale1,
                                          raw.acceleration * scale1};
        window_index_ = 0;
        window_in_curve_ = false;
        window_tick_ = 0;
        return true;
    }

    // A5 v2 (KB-033): append a KB-030 BORDER arc to the look-ahead window.
    // The junction must be tangent-continuous (no tolerance-band curve exists
    // between lines and arcs until v3); anything else degrades to a BUFFERED
    // full-stop join, reported. The arc segment's velocity limit is clamped
    // to the centripetal bound sqrt(a*R) for the whole segment.
    rt::Result<std::uint32_t> submit_blend_arc(GroupCommand command,
                                               const std::array<double, MaxAxes> &start_point)
    {
        if(window_stop_ || !queue_.empty()) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.tool_number != active_tool_ ||
           command.payload_number != active_payload_) {
            return degrade_blend(command);
        }
        if(!window_active_) {
            if(!active_ || active_kind_ != GroupPathKind::linear ||
               status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
                // Converting an active circular command is a declared v2
                // boundary: only linear actives seed a window.
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        }
        if(window_active_ && window_.size() >= window_depth_) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }

        // Arc segment descriptor. KB-030 dynamics are already stated in the
        // plane arc-length domain, so no metric conversion applies; the
        // centripetal bound clamps the whole segment.
        WindowSegment fresh{};
        fresh.kind = WindowKind::arc;
        fresh.arc_geom = command.arc;
        fresh.full_length = command.arc.length;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            fresh.entry[i] = start_point[i];
            fresh.target[i] = command.target.value[i];
        }
        fresh.entry[0] = command.arc.start.x;
        fresh.entry[1] = command.arc.start.y;
        fresh.limits = otg::Limits1D{command.velocity, command.acceleration,
                                     command.deceleration, command.jerk};
        double junction = fresh.limits.max_acceleration;
        if(fresh.limits.max_deceleration < junction) {
            junction = fresh.limits.max_deceleration;
        }
        const double centripetal = std::sqrt(junction * command.arc.radius);
        if(centripetal < fresh.limits.max_velocity) {
            fresh.limits.max_velocity = centripetal;
        }
        fresh.command_id = command.command_id;

        // Predecessor exit tangent vs the arc entry tangent (N-dimensional).
        std::array<double, MaxAxes> pred_tangent{};
        otg::Limits1D pred_limits{};
        if(window_active_) {
            const WindowSegment &tail = window_[window_.size() - 1];
            window_tangent(tail, true, pred_tangent);
            pred_limits = tail.limits;
        } else {
            double length1 = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double d = active_finish_[i] - active_start_[i];
                pred_tangent[i] = d;
                length1 += d * d;
            }
            length1 = std::sqrt(length1);
            if(length1 <= 1e-12) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                pred_tangent[i] /= length1;
            }
            const double scale1 = length1 / active_path_length_;
            pred_limits = otg::Limits1D{active_command_.velocity * scale1,
                                        active_command_.acceleration * scale1,
                                        active_command_.deceleration * scale1,
                                        active_command_.jerk * scale1};
        }
        std::array<double, MaxAxes> arc_tangent{};
        window_tangent(fresh, false, arc_tangent);
        double alignment = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            alignment += pred_tangent[i] * arc_tangent[i];
        }
        if(alignment <= 0.999) {
            // Non-tangent junction: full-stop join, reported (approved v2).
            return degrade_blend(command);
        }

        // Pass-through node (no curve, no trims).
        WindowNode node{};
        node.corner_cap = pred_limits.max_velocity < fresh.limits.max_velocity
                              ? pred_limits.max_velocity
                              : fresh.limits.max_velocity;

        const std::int64_t old_remaining = window_active_ ? window_remaining_cycles() : -1;

        bool converted = false;
        if(!window_active_) {
            if(!convert_active_linear_to_window()) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
            converted = true;
        }

        WindowSegment &tail = window_[window_.size() - 1];
        const WindowNode saved_node = tail.node;
        tail.node = node;
        if(window_.size() >= window_depth_ ||
           window_.push_back(fresh) != rt::ErrorCode::ok) {
            tail.node = saved_node;
            if(converted) {
                window_.clear();
            }
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::capacity_exceeded);
        }

        bool late = false;
        if(!window_rebuild(late)) {
            window_.pop_back();
            WindowSegment &restore = window_[window_.size() - 1];
            restore.node = saved_node;
            if(converted) {
                window_.clear();
            } else if(!window_rebuild(late)) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                abort_motion();
            }
            return degrade_blend(command);
        }

        if(old_remaining >= 0) {
            const rt::Result<otg::Profile1D> standalone = otg::plan_time_optimal(
                {0.0, 0.0, 0.0}, {fresh.full_length, 0.0, 0.0}, fresh.limits);
            if(standalone &&
               window_remaining_cycles() >=
                   old_remaining + standalone.value().duration_cycles()) {
                window_.pop_back();
                WindowSegment &restore = window_[window_.size() - 1];
                restore.node = saved_node;
                if(!window_rebuild(late)) {
                    window_reset();
                    clear_axes_synchronized();
                    status_ = GroupStatus::standby;
                    abort_motion();
                }
                return degrade_blend(command);
            }
        }

        if(converted) {
            active_ = false;
            window_active_ = true;
        }
        status_ = GroupStatus::moving;
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    // Trapezoid-level bidirectional scan + per-segment profile planning over
    // the not-yet-started part of the window. Returns false when any segment
    // profile is infeasible (caller degrades).
    bool window_rebuild(bool &late)
    {
        late = false;
        const std::size_t count = window_.size();
        if(count == 0 || window_index_ >= count) {
            return false;
        }

        // Live state along the current line piece (euclidean, local coords).
        double s_live = 0.0;
        double v_live = 0.0;
        double a_live = 0.0;
        std::size_t first = window_index_;
        if(window_active_) {
            if(window_in_curve_) {
                // The current curve is committed; rebuild from the next line.
                const WindowNode &cur = window_[window_index_].node;
                double s_curve = cur.curve_velocity * static_cast<double>(window_tick_);
                if(s_curve > cur.curve_length) {
                    s_curve = cur.curve_length;
                }
                (void)s_curve;
                first = window_index_ + 1;
                if(first >= count) {
                    return false;
                }
                s_live = 0.0;
                v_live = cur.curve_velocity;
                a_live = 0.0;
            } else {
                const otg::State1D raw = otg::sample(
                    window_[window_index_].profile, rt::CycleTick::from_cycles(window_tick_));
                s_live = raw.position;
                v_live = raw.velocity;
                a_live = raw.acceleration;
            }
        } else {
            // Fresh conversion: seed state captured in submit_blend.
            s_live = window_seed_state_.position;
            v_live = window_seed_state_.velocity;
            a_live = window_seed_state_.acceleration;
        }

        // Late submission: already inside (or past) the tail transition region.
        const WindowSegment &first_seg = window_[first];
        if(first == count - 2 || count == 2) {
            // The newly trimmed segment is the live one: check the room.
        }
        if(first < count && window_[first].line_length() <= 0.0 && first + 1 < count) {
            // Fully consumed line between two corners is allowed only when
            // both node velocities agree; v1 degrades instead.
            return false;
        }
        if(!window_in_curve_ && s_live >= window_[first].line_length()) {
            late = true;
            return false;
        }
        (void)first_seg;

        // Forward pass (accelerating limit), then backward pass (braking).
        std::array<double, WindowCapacity> node_v{};
        double forward = v_live;
        for(std::size_t i = first; i < count; ++i) {
            const double length = i == first && !window_in_curve_
                                      ? window_[i].line_length() - s_live
                                      : window_[i].line_length();
            const double usable = length > 0.0 ? length : 0.0;
            // Approved look-ahead v2: exact jerk-limited reachability
            // replaces the trapezoid estimate (declared change, KB-039).
            double reachable = plan::jerk_reachable_speed(
                forward, usable, window_[i].limits.max_acceleration,
                window_[i].limits.max_jerk);
            if(i + 1 < count) {
                const double cap = window_[i].node.corner_cap;
                if(reachable > cap) {
                    reachable = cap;
                }
                node_v[i] = reachable;
                forward = reachable;
            } else {
                node_v[i] = 0.0; // terminal rest
            }
        }
        double backward = 0.0;
        for(std::size_t r = count; r > first; --r) {
            const std::size_t i = r - 1;
            const double length = i == first && !window_in_curve_
                                      ? window_[i].line_length() - s_live
                                      : window_[i].line_length();
            const double usable = length > 0.0 ? length : 0.0;
            if(i + 1 < count && backward < node_v[i]) {
                node_v[i] = backward;
            }
            backward = plan::jerk_reachable_speed(node_v[i], usable,
                                                  window_[i].limits.max_deceleration,
                                                  window_[i].limits.max_jerk);
            if(i + 1 < count) {
                backward = backward; // entry allowance of segment i
            }
        }

        // Quantize curve velocities and plan the per-segment profiles.
        double entry_velocity = v_live;
        double entry_acceleration = a_live;
        double entry_position = window_in_curve_ ? 0.0 : s_live;
        for(std::size_t i = first; i < count; ++i) {
            WindowSegment &seg = window_[i];
            double exit_velocity = 0.0;
            if(i + 1 < count) {
                if(seg.node.has_curve) {
                    double v = node_v[i];
                    if(v <= 1e-12) {
                        return false; // corner requires rest: degrade
                    }
                    std::int64_t cycles = static_cast<std::int64_t>(
                        std::ceil(seg.node.curve_length / v));
                    if(cycles < 1) {
                        cycles = 1;
                    }
                    seg.node.curve_velocity =
                        seg.node.curve_length / static_cast<double>(cycles);
                    seg.node.curve_cycles = cycles;
                    exit_velocity = seg.node.curve_velocity;
                } else {
                    exit_velocity = node_v[i];
                }
            }
            const rt::Result<otg::Profile1D> profile = otg::plan_time_optimal(
                {entry_position, entry_velocity, entry_acceleration},
                {seg.line_length(), exit_velocity, 0.0}, seg.limits);
            if(!profile) {
                return false;
            }
            seg.entry_velocity = entry_velocity;
            seg.exit_velocity = exit_velocity;
            seg.profile = profile.value();
            entry_velocity = exit_velocity;
            entry_acceleration = 0.0;
            entry_position = 0.0;
        }
        if(!window_in_curve_) {
            // The live line profile was replanned from the live state; its
            // tick restarts. A committed curve keeps its own progress.
            window_tick_ = 0;
        }
        return true;
    }

    std::int64_t window_remaining_cycles() const
    {
        std::int64_t total = 0;
        for(std::size_t i = window_index_; i < window_.size(); ++i) {
            if(!(i == window_index_ && window_in_curve_)) {
                total += window_[i].profile.duration_cycles();
            }
            if(i + 1 < window_.size() && window_[i].node.has_curve) {
                total += window_[i].node.curve_cycles;
            }
        }
        return total;
    }

    void window_cycle()
    {
        if(window_override_paused_ && !window_stop_) {
            return;
        }
        ++window_tick_;
        if(window_stop_) {
            const otg::State1D st = otg::sample(window_stop_profile_,
                                                rt::CycleTick::from_cycles(window_tick_));
            sample_window_arclength(window_stop_origin_ + st.position);
            if(window_tick_ >= window_stop_profile_.duration_cycles()) {
                if(window_override_paused_) {
                    interrupting_ = false;
                    window_stop_ = false;
                    status_ = GroupStatus::moving;
                } else if(interrupting_) {
                    interrupting_ = false;
                    window_stop_ = false;
                    status_ = GroupStatus::interrupted;
                } else {
                    window_reset();
                    clear_axes_synchronized();
                    status_ = GroupStatus::standby;
                    start_next_queued();
                }
            }
            return;
        }

        WindowSegment &seg = window_[window_index_];
        if(window_in_curve_) {
            double s = seg.node.curve_velocity * static_cast<double>(window_tick_);
            if(s > seg.node.curve_length) {
                s = seg.node.curve_length;
            }
            sample_window_curve(seg.node, s);
            if(window_tick_ >= seg.node.curve_cycles) {
                ++window_index_;
                window_in_curve_ = false;
                window_tick_ = 0;
            }
            return;
        }

        const otg::State1D st =
            otg::sample(seg.profile, rt::CycleTick::from_cycles(window_tick_));
        // No clamping: a terminal profile may legally overshoot the target by
        // a hair inside its envelope and come back; clamping would turn that
        // into a hard stop (acceleration step). The final sample lands on the
        // exact segment end by the profile's endpoint contract.
        const double s = st.position;
        sample_window_segment(seg, s);
        if(window_tick_ >= seg.profile.duration_cycles()) {
            if(window_index_ + 1 < window_.size()) {
                if(seg.node.has_curve) {
                    window_in_curve_ = true;
                    window_tick_ = 0;
                } else {
                    ++window_index_;
                    window_tick_ = 0;
                }
            } else {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                start_next_queued();
            }
        }
    }

    void sample_window_segment(const WindowSegment &seg, double arclength)
    {
        if(seg.kind == WindowKind::arc) {
            double s = arclength;
            if(s < 0.0) {
                s = 0.0;
            }
            if(s > seg.full_length) {
                s = seg.full_length;
            }
            const geom::Vec3 point = geom::sample(seg.arc_geom, s);
            axes_[0]->set_synchronized_position(point.x);
            axes_[1]->set_synchronized_position(point.y);
            const double ratio = seg.full_length > 0.0 ? s / seg.full_length : 1.0;
            for(std::size_t i = 2; i < axes_.size(); ++i) {
                axes_[i]->set_synchronized_position(
                    seg.entry[i] + (seg.target[i] - seg.entry[i]) * ratio);
            }
            return;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(seg.entry[i] + seg.dir[i] * arclength);
        }
    }

    void sample_window_curve(const WindowNode &node, double arclength)
    {
        double target = arclength;
        if(target < 0.0) {
            target = 0.0;
        }
        if(target > node.curve_length) {
            target = node.curve_length;
        }
        std::size_t low = 0;
        for(std::size_t i = 1; i < BlendTableSize; ++i) {
            if(node.cumulative[i] >= target) {
                low = i - 1;
                break;
            }
            low = i - 1;
        }
        const double segment = node.cumulative[low + 1] - node.cumulative[low];
        const double fraction =
            segment > 0.0 ? (target - node.cumulative[low]) / segment : 0.0;
        const double u = (static_cast<double>(low) + fraction) /
                         static_cast<double>(BlendTableSize - 1);
        std::array<double, MaxAxes> point{};
        blend_point(node.ctrl, u, point);
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(point[i]);
        }
    }

    // Composite arc length measured from the live piece at stop time: walks
    // the remaining pieces (bounded by the window capacity, simple compares).
    void sample_window_arclength(double arclength)
    {
        double remaining = arclength;
        bool in_curve = window_in_curve_;
        std::size_t index = window_index_;
        while(index < window_.size()) {
            const WindowSegment &seg = window_[index];
            if(!in_curve) {
                const double length = seg.line_length();
                if(remaining <= length || index + 1 >= window_.size()) {
                    const double s =
                        remaining < 0.0 ? 0.0 : (remaining > length ? length : remaining);
                    sample_window_segment(seg, s);
                    return;
                }
                remaining -= length;
                if(seg.node.has_curve) {
                    in_curve = true;
                } else {
                    ++index;
                }
            } else {
                if(remaining <= seg.node.curve_length) {
                    sample_window_curve(seg.node, remaining);
                    return;
                }
                remaining -= seg.node.curve_length;
                in_curve = false;
                ++index;
            }
        }
    }

    void window_live_state(double &s_live, double &v_live, double &a_live) const
    {
        if(window_in_curve_) {
            const WindowNode &node = window_[window_index_].node;
            double s = node.curve_velocity * static_cast<double>(window_tick_);
            if(s > node.curve_length) {
                s = node.curve_length;
            }
            s_live = s;
            v_live = node.curve_velocity;
            a_live = 0.0;
            return;
        }
        const otg::State1D raw = otg::sample(window_[window_index_].profile,
                                             rt::CycleTick::from_cycles(window_tick_));
        const double length = window_[window_index_].line_length();
        s_live = raw.position < 0.0 ? 0.0 : (raw.position > length ? length : raw.position);
        v_live = raw.velocity;
        a_live = raw.acceleration;
    }

    void window_reset()
    {
        window_active_ = false;
        window_stop_ = false;
        window_override_paused_ = false;
        window_in_curve_ = false;
        window_index_ = 0;
        window_tick_ = 0;
        window_.clear();
    }

    void clear_axes_synchronized()
    {
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
    }

    rt::Result<std::uint32_t> degrade_blend(GroupCommand command)
    {
        last_blend_degraded_id_ = command.command_id;
        command.buffer_mode = BufferMode::buffered;
        command.transition_mode = TransitionMode::none;
        command.transition_parameter = 0.0;
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
    }

    void blend_point(const std::array<std::array<double, MaxAxes>, 6> &control,
                     double u,
                     std::array<double, MaxAxes> &out) const
    {
        const double v = 1.0 - u;
        const double v2 = v * v;
        const double u2 = u * u;
        const double w0 = v2 * v2 * v;
        const double w1 = 5.0 * v2 * v2 * u;
        const double w2 = 10.0 * v2 * v * u2;
        const double w3 = 10.0 * v2 * u2 * u;
        const double w4 = 5.0 * v * u2 * u2;
        const double w5 = u2 * u2 * u;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            out[i] = control[0][i] * w0 + control[1][i] * w1 + control[2][i] * w2 +
                     control[3][i] * w3 + control[4][i] * w4 + control[5][i] * w5;
        }
    }

    double blend_curvature(const std::array<std::array<double, MaxAxes>, 6> &control,
                           double u) const
    {
        const double v = 1.0 - u;
        const double v2 = v * v;
        const double u2 = u * u;
        const double d1w0 = 5.0 * v2 * v2;
        const double d1w1 = 20.0 * v2 * v * u;
        const double d1w2 = 30.0 * v2 * u2;
        const double d1w3 = 20.0 * v * u2 * u;
        const double d1w4 = 5.0 * u2 * u2;
        const double d2w0 = 20.0 * v * v * v;
        const double d2w1 = 60.0 * v * v * u;
        const double d2w2 = 60.0 * v * u * u;
        const double d2w3 = 20.0 * u * u * u;
        double norm1 = 0.0;
        double norm2 = 0.0;
        double dot12 = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double e0 = control[1][i] - control[0][i];
            const double e1 = control[2][i] - control[1][i];
            const double e2 = control[3][i] - control[2][i];
            const double e3 = control[4][i] - control[3][i];
            const double e4 = control[5][i] - control[4][i];
            const double first = e0 * d1w0 + e1 * d1w1 + e2 * d1w2 + e3 * d1w3 + e4 * d1w4;
            const double f0 = e1 - e0;
            const double f1 = e2 - e1;
            const double f2 = e3 - e2;
            const double f3 = e4 - e3;
            const double second = f0 * d2w0 + f1 * d2w1 + f2 * d2w2 + f3 * d2w3;
            norm1 += first * first;
            norm2 += second * second;
            dot12 += first * second;
        }
        if(norm1 <= 1e-24) {
            return 0.0;
        }
        const double area = norm1 * norm2 - dot12 * dot12;
        if(area <= 0.0) {
            return 0.0;
        }
        return std::sqrt(area) / (norm1 * std::sqrt(norm1));
    }

    const kin::PoseKinematics *pose_kinematics_ = nullptr;
    double pose_min_margin_ = 0.0;
    double pose_max_joint_step_ = 0.0;
    const kin::Kinematics *kinematics_ = nullptr;
    double kinematics_min_margin_ = 0.0;
    double cartesian_velocity_limit_ = 0.0;
    std::size_t cart_piece_index_ = 0;
    std::int64_t cart_piece_tick_ = 0;
    std::int64_t cart_halt_tick_ = 0;
    std::int64_t cart_halt_duration_ = 0;
    double cart_halt_origin_ = 0.0;
    double cart_halt_piece_offset_ = 0.0;
    double cart_window_entry_v_ = 0.0;
    double cart_window_entry_a_ = 0.0;
    double cart_window_acc_ = 0.0;
    double cart_window_dec_ = 0.0;
    double cart_window_jerk_ = 0.0;
    std::size_t window_depth_ = WindowCapacity;
    std::size_t window_index_ = 0;
    std::int64_t window_tick_ = 0;
    double window_stop_origin_ = 0.0;
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
    double interrupt_ratio_ = 0.0;
    double jog_stop_deceleration_ = 0.0;
    double jog_stop_jerk_ = 0.0;
    geom::Vec3 tool_offset_{};
    otg::State1D window_seed_state_{};
    double workpiece_frame_rpy_[6] = {};
    double tool_transform_rpy_[6] = {};
    std::array<double, MaxAxes> active_start_{};
    std::array<double, MaxAxes> active_finish_{};
    std::array<double, MaxAxes> path_odometer_position_{};
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
    double cart_joints_[MaxAxes] = {};
    double cart_tail_joints_[MaxAxes] = {};
    double cart_window_joints_[MaxAxes] = {};
    GroupTakeoverConnector<MaxAxes> connector_{};
    rt::StaticVector<AxisModel *, MaxAxes> axes_{};
    AxisModel *path_sync_slave_ = nullptr;
    AxisModel *group_sync_master_ = nullptr;
    AxisModel *tracking_master_axis_ = nullptr;
    AxisGroup *tracking_master_group_ = nullptr;
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
    geom::ArcSegment active_arc_{};
    CartesianSegment active_cart_{};
    GroupCommand active_command_{};
    ToolData tracking_origin_{};
    ToolData tracking_transform_{};
    GroupPosition jog_direction_{};
    GroupPosition jog_cartesian_position_{};
    otg::Profile1D cart_halt_profile_{};
    otg::Profile1D window_stop_profile_{};
    otg::Profile1D active_profile_{};
    rt::StaticVector<GroupCommand, QueueCapacity> queue_{};
    rt::StaticVector<CartPiece, CartWindowPieces> cart_window_{};
    rt::StaticVector<WindowSegment, WindowCapacity> window_{};
    int domain_id_ = 0;
    GroupStatus status_ = GroupStatus::disabled;
    GroupPathKind active_kind_ = GroupPathKind::linear;
    CoordSystem jog_coord_system_ = CoordSystem::acs;
    rt::ErrorCode last_cartesian_error_ = rt::ErrorCode::ok;
    rt::ErrorCode group_error_id_ = rt::ErrorCode::ok;
    rt::ErrorCode jog_error_ = rt::ErrorCode::ok;
    rt::ErrorCode tracking_error_ = rt::ErrorCode::ok;
    std::uint32_t cart_window_last_id_ = 0;
    std::uint32_t last_blend_degraded_id_ = 0;
    std::uint32_t next_command_id_ = 1;
    std::uint32_t direct_command_id_ = 0;
    std::uint32_t last_completed_direct_id_ = 0;
    std::uint32_t last_aborted_direct_id_ = 0;
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
    std::size_t selected_tool_ = 0;
    std::size_t active_tool_ = 0;
    std::size_t selected_payload_ = 0;
    std::size_t active_payload_ = 0;
    std::size_t group_sync_count_ = 0;
    bool cart_window_active_ = false;
    bool cart_window_stopping_ = false;
    bool window_active_ = false;
    bool window_in_curve_ = false;
    bool window_stop_ = false;
    bool window_override_paused_ = false;
    bool active_ = false;
    bool override_paused_ = false;
    bool interrupting_ = false;
    bool interrupted_plain_ = false;
    bool interrupted_window_ = false;
    bool direct_active_ = false;
    bool direct_stopping_ = false;
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
    GroupPosition group_sync_previous_{};
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
