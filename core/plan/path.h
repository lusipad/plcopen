#pragma once

#include <cmath>
#include <cstddef>

#include "geom/geometry.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::plan
{

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
    double radius = 0.0;
    double allowed_deviation = 0.0;
    geom::PathSegment curve{};
};

inline BlendDecision decide_blend(const geom::PathSegment &before,
                                  const geom::PathSegment &after,
                                  double tolerance)
{
    if(tolerance <= 0.0) {
        return {};
    }

    const geom::Vec3 t0 = before.tangent(before.length());
    const geom::Vec3 t1 = after.tangent(0.0);
    const double alignment = geom::dot(t0, t1);
    if(alignment > 0.999) {
        return {};
    }

    const double shortest = before.length() < after.length() ? before.length() : after.length();
    const double radius = tolerance < shortest * 0.5 ? tolerance : shortest * 0.5;
    const geom::Vec3 corner = before.finish();
    const geom::Vec3 start = corner - t0 * radius;
    const geom::Vec3 finish = corner + t1 * radius;
    const rt::Result<geom::QuadraticBlendSegment> curve =
        geom::make_quadratic_blend(start, corner, finish, tolerance);
    if(!curve) {
        return {};
    }

    BlendDecision decision{};
    decision.enabled = true;
    decision.allowed_deviation = tolerance;
    decision.radius = radius;
    decision.curve = geom::as_path_segment(curve.value());
    return decision;
}

} // namespace plcopen::core::plan
