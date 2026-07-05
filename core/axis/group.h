#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "axis/state.h"
#include "geom/geometry.h"
#include "otg/profile1d.h"
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

        if(command.buffer_mode == BufferMode::aborting || !active_) {
            const rt::ErrorCode started = start(command);
            if(started != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(started);
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }

        if(blend_chain_) {
            // v1 boundary (KB-031): a committed blend chain cannot be
            // extended with further buffered commands.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
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
        if(command.buffer_mode != BufferMode::aborting &&
           command.buffer_mode != BufferMode::buffered) {
            // Geometric blending onto arcs awaits the linear-circular spec
            // extension; reject explicitly.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }
        if(command.transition_mode != TransitionMode::none ||
           command.transition_parameter != 0.0) {
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
        if(aborting) {
            abort_motion();
        }
        if(aborting || !active_) {
            const rt::ErrorCode started = start(command);
            if(started != rt::ErrorCode::ok) {
                return rt::Result<std::uint32_t>::failure(started);
            }
            return rt::Result<std::uint32_t>::success(command.command_id);
        }
        if(blend_chain_) {
            // v1 boundary (KB-031): a committed blend chain cannot be extended.
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
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
        if(blend_chain_) {
            sample_chain(ratio * active_path_length_);
        } else if(active_kind_ == GroupPathKind::circular) {
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

    double queued_finish(std::size_t axis_index) const
    {
        double finish =
            active_ ? active_finish_[axis_index] : axes_[axis_index]->snapshot().command_position;
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
        blend_chain_ = false;
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
        blend_chain_ = false;
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
        blend_chain_ = false;
        queue_.clear();
        active_tick_ = 0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
        }
    }

    // A4 v1 geometric blending (KB-031): the accepted chain fuses the rest of
    // the active linear segment, the quintic corner curve, and the successor
    // segment into ONE Euclidean arc-length path driven by ONE jerk-limited
    // profile whose velocity limit is corner-safe (min of both commands and
    // the curvature bound). Planning happens synchronously at submit; the
    // cycle path only samples precomputed data. The chain commits only when
    // it beats the full-stop baseline, otherwise the request degrades to
    // BUFFERED and the degradation is reported.
    static constexpr std::size_t BlendTableSize = 33;

    rt::Result<std::uint32_t> submit_blend(GroupCommand command)
    {
        // v1 declared boundary (KB-031): blending applies onto the active
        // linear command with an empty queue; other configurations are
        // explicit errors.
        if(!active_ || active_kind_ != GroupPathKind::linear || !queue_.empty() ||
           blend_chain_ || status_ != GroupStatus::moving || active_path_length_ <= 0.0) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::unsupported);
        }

        // Euclidean geometry of the predecessor and successor segments.
        double length1 = 0.0;
        double length2 = 0.0;
        std::array<double, MaxAxes> u1{};
        std::array<double, MaxAxes> u2{};
        double longest2 = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            const double d1 = active_finish_[i] - active_start_[i];
            const double d2 = command.target.value[i] - active_finish_[i];
            length1 += d1 * d1;
            length2 += d2 * d2;
            u1[i] = d1;
            u2[i] = d2;
            const double travel2 = std::fabs(d2);
            if(travel2 > longest2) {
                longest2 = travel2;
            }
        }
        length1 = std::sqrt(length1);
        length2 = std::sqrt(length2);
        if(length1 <= 1e-12 || length2 <= 1e-12) {
            return rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument);
        }
        double alignment = 0.0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            u1[i] /= length1;
            u2[i] /= length2;
            alignment += u1[i] * u2[i];
        }

        // Euclidean conversions: KB-027 states dynamics in the longest-member
        // metric; the chain runs in Euclidean arc length.
        const double scale1 = length1 / active_path_length_;
        const double scale2 = longest2 > 0.0 ? length2 / longest2 : 1.0;
        const otg::State1D raw =
            otg::sample(active_profile_, rt::CycleTick::from_cycles(active_tick_));
        const otg::State1D now{raw.position * scale1, raw.velocity * scale1,
                               raw.acceleration * scale1};
        const otg::Limits1D limits1{active_command_.velocity * scale1,
                                    active_command_.acceleration * scale1,
                                    active_command_.deceleration * scale1,
                                    active_command_.jerk * scale1};
        const otg::Limits1D limits2{command.velocity * scale2,
                                    command.acceleration * scale2,
                                    command.deceleration * scale2,
                                    command.jerk * scale2};

        if(alignment < -0.999) {
            // Reflex corner: degrade to a BUFFERED full stop, reported.
            return degrade_blend(command);
        }

        double distance = 0.0;
        double blend_length = 0.0;
        double corner_velocity =
            limits1.max_velocity < limits2.max_velocity ? limits1.max_velocity
                                                        : limits2.max_velocity;
        bool has_curve = false;
        std::array<std::array<double, MaxAxes>, 6> control{};
        std::array<double, BlendTableSize> cumulative{};

        if(alignment <= 0.999) {
            double turn = 0.0;
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double diff = u2[i] - u1[i];
                turn += diff * diff;
            }
            turn = std::sqrt(turn);
            distance = command.transition_parameter * 96.0 / (23.0 * turn);
            if(distance > length1 * 0.5) {
                distance = length1 * 0.5;
            }
            if(distance > length2 * 0.5) {
                distance = length2 * 0.5;
            }
            if(now.position >= length1 - distance) {
                // Already inside (or past) the would-be transition region.
                return degrade_blend(command);
            }

            for(std::size_t i = 0; i < axes_.size(); ++i) {
                const double corner = active_finish_[i];
                control[0][i] = corner - u1[i] * distance;
                control[1][i] = corner - u1[i] * (distance * 2.0 / 3.0);
                control[2][i] = corner - u1[i] * (distance / 3.0);
                control[3][i] = corner + u2[i] * (distance / 3.0);
                control[4][i] = corner + u2[i] * (distance * 2.0 / 3.0);
                control[5][i] = corner + u2[i] * distance;
            }

            // Arc-length table and peak curvature (planning-phase work).
            double accumulated = 0.0;
            std::array<double, MaxAxes> previous{};
            blend_point(control, 0.0, previous);
            cumulative[0] = 0.0;
            for(std::size_t step = 1; step < BlendTableSize; ++step) {
                const double u =
                    static_cast<double>(step) / static_cast<double>(BlendTableSize - 1);
                std::array<double, MaxAxes> point{};
                blend_point(control, u, point);
                double chord = 0.0;
                for(std::size_t i = 0; i < axes_.size(); ++i) {
                    const double diff = point[i] - previous[i];
                    chord += diff * diff;
                }
                accumulated += std::sqrt(chord);
                cumulative[step] = accumulated;
                previous = point;
            }
            blend_length = accumulated;
            if(!std::isfinite(blend_length) || blend_length <= 0.0) {
                return degrade_blend(command);
            }
            double max_curvature = 0.0;
            for(std::size_t step = 0; step <= 64; ++step) {
                const double u = static_cast<double>(step) / 64.0;
                const double curvature = blend_curvature(control, u);
                if(curvature > max_curvature) {
                    max_curvature = curvature;
                }
            }
            if(max_curvature > 0.0) {
                double junction_acceleration =
                    limits1.max_acceleration < limits1.max_deceleration
                        ? limits1.max_acceleration
                        : limits1.max_deceleration;
                if(limits2.max_acceleration < junction_acceleration) {
                    junction_acceleration = limits2.max_acceleration;
                }
                if(limits2.max_deceleration < junction_acceleration) {
                    junction_acceleration = limits2.max_deceleration;
                }
                const double geometric = std::sqrt(junction_acceleration / max_curvature);
                if(geometric < corner_velocity) {
                    corner_velocity = geometric;
                }
            }
            has_curve = true;
        }
        // Collinear pass-through keeps has_curve false: the segments join
        // directly at the corner and the chain cruises through it.

        // One profile over the composite chain, with the corner-safe velocity
        // limit and the conservative envelope of both commands (KB-031).
        const otg::Limits1D chain_limits{
            corner_velocity,
            limits1.max_acceleration < limits2.max_acceleration ? limits1.max_acceleration
                                                                : limits2.max_acceleration,
            limits1.max_deceleration < limits2.max_deceleration ? limits1.max_deceleration
                                                                : limits2.max_deceleration,
            limits1.max_jerk < limits2.max_jerk ? limits1.max_jerk : limits2.max_jerk,
        };
        const double chain_total = (length1 - distance) + blend_length + (length2 - distance);
        const rt::Result<otg::Profile1D> chain = otg::plan_time_optimal(
            now, {chain_total, 0.0, 0.0}, chain_limits);
        if(!chain) {
            return degrade_blend(command);
        }

        // Constructive cycle-time gate: commit only when the chain beats the
        // full-stop baseline, otherwise degrade (reported).
        const rt::Result<otg::Profile1D> stop_leg = otg::plan_time_optimal(
            now, {length1, 0.0, 0.0}, limits1);
        const rt::Result<otg::Profile1D> next_leg = otg::plan_time_optimal(
            {0.0, 0.0, 0.0}, {length2, 0.0, 0.0}, limits2);
        if(stop_leg && next_leg &&
           chain.value().duration_cycles() >=
               stop_leg.value().duration_cycles() + next_leg.value().duration_cycles()) {
            return degrade_blend(command);
        }

        // Commit. The active profile and dynamics switch to the chain.
        blend_chain_ = true;
        blend_has_curve_ = has_curve;
        blend_ctrl_ = control;
        blend_cumulative_ = cumulative;
        blend_length_ = blend_length;
        chain_s1_ = length1 - distance;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            chain_u1_[i] = u1[i];
            chain_u2_[i] = u2[i];
            chain_leg2_start_[i] =
                has_curve ? control[5][i] : active_finish_[i];
        }
        active_profile_ = chain.value();
        active_tick_ = 0;
        active_duration_ = active_profile_.duration_cycles();
        active_path_length_ = chain_total;
        active_command_ = command;
        active_command_.velocity = chain_limits.max_velocity;
        active_command_.acceleration = chain_limits.max_acceleration;
        active_command_.deceleration = chain_limits.max_deceleration;
        active_command_.jerk = chain_limits.max_jerk;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            // active_start_ keeps the leg1 origin: chain arc length zero is
            // the original segment start. active_finish_ is the chain target.
            active_finish_[i] = command.target.value[i];
        }
        active_kind_ = GroupPathKind::linear;
        return rt::Result<std::uint32_t>::success(command.command_id);
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

    void sample_chain(double arclength)
    {
        const double s2_boundary = chain_s1_ + blend_length_;
        if(arclength <= chain_s1_ || (!blend_has_curve_ && arclength <= chain_s1_)) {
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                axes_[i]->set_synchronized_position(active_start_[i] +
                                                    chain_u1_[i] * arclength);
            }
            return;
        }
        if(blend_has_curve_ && arclength <= s2_boundary) {
            double target = arclength - chain_s1_;
            if(target > blend_length_) {
                target = blend_length_;
            }
            std::size_t low = 0;
            for(std::size_t i = 1; i < BlendTableSize; ++i) {
                if(blend_cumulative_[i] >= target) {
                    low = i - 1;
                    break;
                }
                low = i - 1;
            }
            const double segment = blend_cumulative_[low + 1] - blend_cumulative_[low];
            const double fraction =
                segment > 0.0 ? (target - blend_cumulative_[low]) / segment : 0.0;
            const double u = (static_cast<double>(low) + fraction) /
                             static_cast<double>(BlendTableSize - 1);
            std::array<double, MaxAxes> point{};
            blend_point(blend_ctrl_, u, point);
            for(std::size_t i = 0; i < axes_.size(); ++i) {
                axes_[i]->set_synchronized_position(point[i]);
            }
            return;
        }
        const double leg2 = arclength - s2_boundary;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->set_synchronized_position(chain_leg2_start_[i] +
                                                chain_u2_[i] * leg2);
        }
    }

    int domain_id_ = 0;
    GroupStatus status_ = GroupStatus::disabled;
    rt::StaticVector<AxisModel *, MaxAxes> axes_{};
    rt::StaticVector<GroupCommand, QueueCapacity> queue_{};
    GroupCommand active_command_{};
    std::array<double, MaxAxes> active_start_{};
    std::array<double, MaxAxes> active_finish_{};
    GroupPathKind active_kind_ = GroupPathKind::linear;
    geom::ArcSegment active_arc_{};
    bool blend_chain_ = false;
    bool blend_has_curve_ = false;
    std::array<std::array<double, MaxAxes>, 6> blend_ctrl_{};
    std::array<double, BlendTableSize> blend_cumulative_{};
    double blend_length_ = 0.0;
    double chain_s1_ = 0.0;
    std::array<double, MaxAxes> chain_u1_{};
    std::array<double, MaxAxes> chain_u2_{};
    std::array<double, MaxAxes> chain_leg2_start_{};
    std::uint32_t last_blend_degraded_id_ = 0;
    otg::Profile1D active_profile_{};
    double active_path_length_ = 0.0;
    std::int64_t active_tick_ = 0;
    std::int64_t active_duration_ = 1;
    std::uint32_t next_command_id_ = 1;
    bool active_ = false;
};

} // namespace plcopen::core::axis
