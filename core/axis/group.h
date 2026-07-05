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
            // Geometric blending onto arcs is Phase A4 scope; reject explicitly.
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
        const rt::ErrorCode queued = queue_.push_back(command);
        if(queued != rt::ErrorCode::ok) {
            return rt::Result<std::uint32_t>::failure(queued);
        }
        return rt::Result<std::uint32_t>::success(command.command_id);
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
        queue_.clear();
        active_tick_ = 0;
        for(std::size_t i = 0; i < axes_.size(); ++i) {
            axes_[i]->clear_synchronized();
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
    otg::Profile1D active_profile_{};
    double active_path_length_ = 0.0;
    std::int64_t active_tick_ = 0;
    std::int64_t active_duration_ = 1;
    std::uint32_t next_command_id_ = 1;
    bool active_ = false;
};

} // namespace plcopen::core::axis
