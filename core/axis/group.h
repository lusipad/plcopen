#pragma once

#include <array>
#include <cmath>
#include <cstdint>

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

enum class GroupStatus
{
    disabled,
    standby,
    moving,
    stopping,
    errorstop,
};

struct GroupPosition
{
    static constexpr std::size_t MaxAxes = 8;
    std::array<double, MaxAxes> value{};
    std::size_t size = 0;
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
};

class AxisGroup
{
public:
    static constexpr std::size_t MaxAxes = GroupPosition::MaxAxes;
    static constexpr std::size_t QueueCapacity = 8;

    explicit AxisGroup(int domain_id = 0)
        : domain_id_(domain_id)
    {
    }

    GroupStatus status() const
    {
        return status_;
    }

    std::size_t member_count() const
    {
        return axes_.size();
    }

    const AxisModel *member(std::size_t index) const
    {
        return index < axes_.size() ? axes_[index] : nullptr;
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
        axis.set_group_owner(this);
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
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode disable()
    {
        abort_motion();
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
        if(status_ == GroupStatus::disabled || status_ == GroupStatus::errorstop ||
           !std::isfinite(deceleration) || deceleration <= 0.0 || !std::isfinite(jerk) ||
           jerk <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(status_ != GroupStatus::moving) {
            return rt::ErrorCode::ok;
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
        status_ = GroupStatus::stopping;
        return rt::ErrorCode::ok;
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
        workpiece_frame_ = geom::make_rpy_transform(x, y, z, roll, pitch, yaw);
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
        if(status_ != GroupStatus::standby || !queue_.empty() || !std::isfinite(x) ||
           !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(roll) ||
           !std::isfinite(pitch) || !std::isfinite(yaw)) {
            return rt::ErrorCode::invalid_argument;
        }
        pose_tool_inverse_ = geom::invert(geom::make_rpy_transform(x, y, z, roll, pitch, yaw));
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

    rt::ErrorCode set_tool_offset(double x, double y, double z)
    {
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

    rt::Result<std::uint32_t> submit_linear(GroupCommand command)
    {
        if((status_ != GroupStatus::standby && status_ != GroupStatus::moving) || axes_.size() < 2 ||
           command.target.size != axes_.size() || command.velocity <= 0.0 ||
           !std::isfinite(command.velocity) || command.acceleration <= 0.0 ||
           !std::isfinite(command.acceleration) || command.deceleration <= 0.0 ||
           !std::isfinite(command.deceleration) || command.jerk <= 0.0 ||
           !std::isfinite(command.jerk) || !finite(command.target)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->powered() || axes_[i]->status() == AxisStatus::errorstop) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
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
        // circular fields on the command struct.
        command.path_kind = GroupPathKind::linear;
        command.arc = geom::ArcSegment{};

        if(command.buffer_mode == BufferMode::aborting) {
            abort_motion();
        }
        command = normalize(command);

        if(blending_buffer) {
            return submit_blend(command);
        }

        if(command.buffer_mode == BufferMode::aborting || (!active_ && !window_active_)) {
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
        if((status_ != GroupStatus::standby && status_ != GroupStatus::moving) ||
           axes_.size() < 2 || command.target.size != axes_.size() ||
           command.aux.size != axes_.size() || command.velocity <= 0.0 ||
           !std::isfinite(command.velocity) || command.acceleration <= 0.0 ||
           !std::isfinite(command.acceleration) || command.deceleration <= 0.0 ||
           !std::isfinite(command.deceleration) || command.jerk <= 0.0 ||
           !std::isfinite(command.jerk) || !finite(command.target) || !finite(command.aux)) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(!axes_[i]->powered() || axes_[i]->status() == AxisStatus::errorstop) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
            }
        }
        if(command.circ_mode != CircMode::border) {
            // CENTER/RADIUS are declared unsupported in v1, not approximated.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        // Orientation batch (approved matrix, decision #6): the pose pipeline
        // carries no circular semantics in v1; ACS joint-domain arcs stay
        // available (decision #8 passthrough). Guarded here so the rejection
        // never depends on the caller-set path_kind flag.
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
            abort_motion();
        }
        if(aborting || (!active_ && !window_active_)) {
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
    std::uint32_t last_blend_degraded_command() const
    {
        return last_blend_degraded_id_;
    }

    void cycle()
    {
        if(status_ == GroupStatus::errorstop || status_ == GroupStatus::disabled) {
            return;
        }
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            if(axes_[i]->status() == AxisStatus::errorstop) {
                abort_motion();
                status_ = GroupStatus::errorstop;
                return;
            }
        }

        if(window_active_) {
            window_cycle();
            return;
        }

        if(!active_) {
            if(status_ == GroupStatus::stopping) {
                status_ = GroupStatus::standby;
            }
            return;
        }

        ++active_tick_;
        double ratio = 1.0;
        if(active_path_length_ > 0.0) {
            const otg::State1D state =
                otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
            ratio = state.position / active_path_length_;
            ratio = ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);
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
        } else {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double position =
                    active_start_[i] + (active_finish_[i] - active_start_[i]) * ratio;
                axes_[i]->set_synchronized_position(position);
            }
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
        return rt::ErrorCode::ok;
    }

private:
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

    double queued_finish(std::size_t axis_index) const
    {
        double finish = window_active_
                            ? window_[window_.size() - 1].target[axis_index]
                            : (active_ ? active_finish_[axis_index]
                                       : axes_[axis_index]->snapshot().command_position);
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
        active_command_ = command;
        active_tick_ = 0;
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
        active_path_length_ = longest;
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
        active_ = false;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
        status_ = GroupStatus::standby;
        start_next_queued();
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
        active_ = false;
        window_reset();
        queue_.clear();
        active_tick_ = 0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
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
        if(!window_active_) {
            if(!active_ || active_kind_ != GroupPathKind::linear ||
               status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        }
        if(window_active_ && window_.full()) {
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
        if(window_.push_back(fresh) != rt::ErrorCode::ok) {
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
        if(!window_active_) {
            if(!active_ || active_kind_ != GroupPathKind::linear ||
               status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
                // Converting an active circular command is a declared v2
                // boundary: only linear actives seed a window.
                return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
            }
        }
        if(window_active_ && window_.full()) {
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
        if(window_.push_back(fresh) != rt::ErrorCode::ok) {
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
        ++window_tick_;
        if(window_stop_) {
            const otg::State1D st = otg::sample(window_stop_profile_,
                                                rt::CycleTick::from_cycles(window_tick_));
            sample_window_arclength(window_stop_origin_ + st.position);
            if(window_tick_ >= window_stop_profile_.duration_cycles()) {
                window_reset();
                clear_axes_synchronized();
                status_ = GroupStatus::standby;
                start_next_queued();
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

    int domain_id_ = 0;
    GroupStatus status_ = GroupStatus::disabled;
    geom::RigidTransform workpiece_frame_{};
    geom::Vec3 tool_offset_{};
    geom::RigidTransform pose_tool_inverse_{};
    const kin::PoseKinematics *pose_kinematics_ = nullptr;
    double pose_min_margin_ = 0.0;
    double pose_max_joint_step_ = 0.0;
    const kin::Kinematics *kinematics_ = nullptr;
    double kinematics_min_margin_ = 0.0;
    double cartesian_velocity_limit_ = 0.0;
    rt::StaticVector<AxisModel *, MaxAxes> axes_{};
    rt::StaticVector<GroupCommand, QueueCapacity> queue_{};
    GroupCommand active_command_{};
    std::array<double, MaxAxes> active_start_{};
    std::array<double, MaxAxes> active_finish_{};
    GroupPathKind active_kind_ = GroupPathKind::linear;
    geom::ArcSegment active_arc_{};
    rt::StaticVector<WindowSegment, WindowCapacity> window_{};
    bool window_active_ = false;
    bool window_in_curve_ = false;
    bool window_stop_ = false;
    std::size_t window_index_ = 0;
    std::int64_t window_tick_ = 0;
    otg::Profile1D window_stop_profile_{};
    double window_stop_origin_ = 0.0;
    otg::State1D window_seed_state_{};
    std::uint32_t last_blend_degraded_id_ = 0;
    otg::Profile1D active_profile_{};
    double active_path_length_ = 0.0;
    std::int64_t active_tick_ = 0;
    std::int64_t active_duration_ = 1;
    std::uint32_t next_command_id_ = 1;
    bool active_ = false;
};

} // namespace plcopen::core::axis
