#pragma once

#include <cmath>
#include <cstddef>

#include "geom/geometry.h"
#include "otg/time_optimal.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::plan
{

// Approved look-ahead v2 (jerk correction): the largest exit speed whose
// jerk-limited ramp from `entry` fits inside `distance`. The trapezoid value
// sqrt(entry^2 + 2*bound*distance) bounds the bisection from above — jerk
// phases only add ramp distance per delta-v — so the v2 scan is never more
// optimistic than the v1 trapezoid scan (monotone tightening by
// construction). Planning-domain only: the scans run at submit time.
inline double jerk_reachable_speed(double entry,
                                   double distance,
                                   double bound,
                                   double jerk)
{
    if(distance <= 0.0 || bound <= 0.0 || jerk <= 0.0) {
        return entry;
    }
    const double trapezoid = std::sqrt(entry * entry + 2.0 * bound * distance);
    const otg::Limits1D limits{trapezoid + 1.0, bound, bound, jerk};
    if(otg::detail::ramp_between(entry, trapezoid, limits).distance <= distance) {
        return trapezoid;
    }
    double low = entry;
    double high = trapezoid;
    for(int iteration = 0; iteration < 48; ++iteration) {
        const double middle = 0.5 * (low + high);
        if(otg::detail::ramp_between(entry, middle, limits).distance <= distance) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

template <std::size_t Capacity> class PathBuffer
{
public:
    rt::ErrorCode push(geom::PathSegment segment)
    {
        return segments_.push_back(segment);
    }

    rt::ErrorCode consume_front()
    {
        if(segments_.empty()) {
            return rt::ErrorCode::out_of_range;
        }
        for(std::size_t i = 1; i < segments_.size(); ++i) {
            segments_[i - 1] = segments_[i];
        }
        return segments_.pop_back();
    }

    bool empty() const
    {
        return segments_.empty();
    }

    bool full() const
    {
        return segments_.full();
    }

    std::size_t size() const
    {
        return segments_.size();
    }

    const geom::PathSegment &segment(std::size_t index) const
    {
        return segments_[index];
    }

    double total_length() const
    {
        double total = 0.0;
        for(std::size_t i = 0; i < segments_.size(); ++i) {
            total += segments_[i].length();
        }
        return total;
    }

    geom::Vec3 sample(double arclength) const
    {
        double remaining = arclength;
        for(std::size_t i = 0; i < segments_.size(); ++i) {
            const double length = segments_[i].length();
            if(remaining <= length) {
                return segments_[i].sample(remaining);
            }
            remaining -= length;
        }

        if(segments_.empty()) {
            return {};
        }
        return segments_[segments_.size() - 1].sample(segments_[segments_.size() - 1].length());
    }

private:
    rt::StaticVector<geom::PathSegment, Capacity> segments_{};
};

template <std::size_t Capacity> struct LookAheadPlan
{
    rt::StaticVector<double, Capacity> entry_speed{};
    rt::StaticVector<double, Capacity> exit_speed{};
};

template <std::size_t Capacity>
rt::Result<LookAheadPlan<Capacity>> compute_lookahead(const PathBuffer<Capacity> &path,
                                                      double max_speed,
                                                      double acceleration_limit,
                                                      std::size_t window)
{
    if(max_speed <= 0.0 || acceleration_limit <= 0.0 || window == 0) {
        return rt::Result<LookAheadPlan<Capacity>>::failure(rt::ErrorCode::invalid_argument);
    }

    LookAheadPlan<Capacity> plan{};
    const std::size_t count = path.size() < window ? path.size() : window;
    for(std::size_t i = 0; i < count; ++i) {
        plan.entry_speed.push_back(max_speed);
        plan.exit_speed.push_back(max_speed);
    }

    if(count == 0) {
        return rt::Result<LookAheadPlan<Capacity>>::success(plan);
    }

    plan.entry_speed[0] = 0.0;
    plan.exit_speed[count - 1] = 0.0;

    for(std::size_t i = 0; i < count; ++i) {
        const double reachable =
            std::sqrt(plan.entry_speed[i] * plan.entry_speed[i] +
                      2.0 * acceleration_limit * path.segment(i).length());
        if(plan.exit_speed[i] > reachable) {
            plan.exit_speed[i] = reachable;
        }
        if(i + 1 < count && plan.entry_speed[i + 1] > plan.exit_speed[i]) {
            plan.entry_speed[i + 1] = plan.exit_speed[i];
        }
    }

    for(std::size_t reverse = count; reverse > 0; --reverse) {
        const std::size_t i = reverse - 1;
        const double reachable =
            std::sqrt(plan.exit_speed[i] * plan.exit_speed[i] +
                      2.0 * acceleration_limit * path.segment(i).length());
        if(plan.entry_speed[i] > reachable) {
            plan.entry_speed[i] = reachable;
        }
        if(i > 0 && plan.exit_speed[i - 1] > plan.entry_speed[i]) {
            plan.exit_speed[i - 1] = plan.entry_speed[i];
        }
    }

    return rt::Result<LookAheadPlan<Capacity>>::success(plan);
}

struct BlendDecision
{
    bool enabled = false;
    // Collinear junction: no curve is needed and the pass-through join keeps
    // a non-zero corner speed (approved blending matrix).
    bool passthrough = false;
    // Reflex corner (~180 degrees): geometric blending degrades to a BUFFERED
    // full stop; the degradation is reported, never silent.
    bool degraded_to_buffered = false;
    // Blend distance from the corner along each adjacent segment.
    double radius = 0.0;
    double allowed_deviation = 0.0;
    geom::PathSegment curve{};
};

// A4 v1 (approved blending matrix): tolerance-band corner blending with a
// symmetric quintic Bezier (C2). The blend distance is sized so the closed
// form midpoint deviation (23/96)*d*|t1-t0| meets the tolerance exactly, then
// truncated to half of the shorter adjacent segment (the actual deviation
// only shrinks, never exceeds the tolerance).
inline BlendDecision decide_blend(const geom::PathSegment &before,
                                  const geom::PathSegment &after,
                                  double tolerance)
{
    if(tolerance <= 0.0 || !std::isfinite(tolerance)) {
        return {};
    }

    const geom::Vec3 t0 = before.tangent(before.length());
    const geom::Vec3 t1 = after.tangent(0.0);
    const double alignment = geom::dot(t0, t1);
    if(alignment > 0.999) {
        BlendDecision decision{};
        decision.passthrough = true;
        return decision;
    }
    if(alignment < -0.999) {
        BlendDecision decision{};
        decision.degraded_to_buffered = true;
        return decision;
    }

    const double turn = geom::norm(t1 - t0);
    if(turn <= 1e-12) {
        BlendDecision decision{};
        decision.passthrough = true;
        return decision;
    }
    const double exact_distance = tolerance * 96.0 / (23.0 * turn);
    const double shortest = before.length() < after.length() ? before.length() : after.length();
    const double distance = exact_distance < shortest * 0.5 ? exact_distance : shortest * 0.5;
    const geom::Vec3 corner = before.finish();
    const geom::Vec3 start = corner - t0 * distance;
    const geom::Vec3 finish = corner + t1 * distance;
    const rt::Result<geom::QuinticBlendSegment> curve =
        geom::make_quintic_blend(start, corner, finish, tolerance);
    if(!curve) {
        BlendDecision decision{};
        decision.degraded_to_buffered = true;
        return decision;
    }

    BlendDecision decision{};
    decision.enabled = true;
    decision.allowed_deviation = tolerance;
    decision.radius = distance;
    decision.curve = geom::as_path_segment(curve.value());
    return decision;
}

} // namespace plcopen::core::plan
