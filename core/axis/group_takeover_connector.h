#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "rt/cycle.h"
#include "rt/error.h"

// Y7 aborting-takeover connector (KB-051/052/053, approved v2.1 matrix,
// linear group scope). Extracted from AxisGroup as the first behavior
// cluster of the split plan (doc/planning/axis-group-split-plan-2026-07-09):
// state and planning/sampling logic move verbatim; AxisGroup keeps its
// public API, error codes and cycle outputs bit-identical. The class owns
// the captured pre-takeover velocity/acceleration vectors, the lateral
// decay profile and the tolerance-tube radius; planning runs at submit
// (planning domain), sampling is O(1) per cycle.

namespace plcopen::core::axis
{

template <std::size_t MaxAxes>
class GroupTakeoverConnector
{
public:
    bool active() const
    {
        return active_;
    }

    double tube_radius() const
    {
        return active_ ? r_tube_ : 0.0;
    }

    void deactivate()
    {
        active_ = false;
    }

    // Single-shot handoff of the captured takeover state: reports whether a
    // capture was pending and always clears the flag (the pre-refactor code
    // cleared it unconditionally before deciding on the connector).
    bool take_captured_velocity()
    {
        const bool had = has_velocity_;
        has_velocity_ = false;
        return had;
    }

    // Y7 (KB-051/052 fix): capture the per-axis velocity AND acceleration
    // vectors of the current plain linear motion before abort_motion()
    // destroys it. KB-052: acceleration capture ensures a_s0 = <qdd, t_hat>
    // continuity (v2.1 decision #1). During a connector, the composite
    // state (along-path + lateral) is returned so re-entrant aborting
    // decomposes correctly.
    void capture(bool motion_active, bool linear_path, double path_length,
                 const otg::Profile1D &along_profile, std::int64_t tick,
                 const std::array<double, MaxAxes> &start,
                 const std::array<double, MaxAxes> &finish,
                 std::size_t axis_count)
    {
        has_velocity_ = false;
        if(!motion_active || !linear_path || path_length <= 0.0) {
            return;
        }
        const otg::State1D along =
            otg::sample(along_profile, rt::CycleTick::from_cycles(tick));
        for(std::size_t i = 0; i < axis_count; ++i) {
            const double dir_i = (finish[i] - start[i]) / path_length;
            velocity_[i] = along.velocity * dir_i;
            acceleration_[i] = along.acceleration * dir_i;
        }
        if(active_) {
            const otg::State1D lat = otg::sample(
                lateral_profile_, rt::CycleTick::from_cycles(tick));
            for(std::size_t i = 0; i < axis_count; ++i) {
                velocity_[i] += lat.velocity * lateral_dir_[i];
                acceleration_[i] += lat.acceleration * lateral_dir_[i];
            }
        }
        has_velocity_ = true;
    }

    // Y7 connector planner: decomposes the takeover velocity into along-path
    // and lateral components, plans both profiles with beta-split limits.
    // KB-052: projects acceleration onto new tangent for a_s0 continuity.
    // KB-053: rejects connector when stopping distance exceeds path length.
    // On the aligned path (zero connector) only the group's active profile
    // is replanned and this object stays inactive.
    rt::ErrorCode plan(const otg::Limits1D &limits, double longest,
                       std::size_t axis_count,
                       const std::array<double, MaxAxes> &start,
                       const std::array<double, MaxAxes> &finish,
                       otg::Profile1D &active_profile,
                       std::int64_t &active_duration)
    {
        constexpr double kBeta = 0.5;
        constexpr double kAlignedThreshold = 1e-9;

        // New path tangent (unit direction). For zero-distance moves the
        // tangent is undefined; all velocity is lateral.
        std::array<double, MaxAxes> t_hat{};
        if(longest > 0.0) {
            for(std::size_t i = 0; i < axis_count; ++i) {
                t_hat[i] = (finish[i] - start[i]) / longest;
            }
        }

        // Project takeover velocity and acceleration onto new path tangent.
        double s_dot_0 = 0.0;
        double a_s0 = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            s_dot_0 += velocity_[i] * t_hat[i];
            a_s0 += acceleration_[i] * t_hat[i];
        }

        // Lateral residual velocity.
        double lat_speed_sq = 0.0;
        std::array<double, MaxAxes> lat_vel{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            lat_vel[i] = velocity_[i] - s_dot_0 * t_hat[i];
            lat_speed_sq += lat_vel[i] * lat_vel[i];
        }
        const double lat_speed = std::sqrt(lat_speed_sq);

        if(lat_speed <= kAlignedThreshold) {
            // Aligned takeover: no lateral residual, plan along-path with
            // full limits from the projected velocity+acceleration (zero
            // connector). KB-052: a_s0 included.
            if(longest > 0.0) {
                const rt::Result<otg::Profile1D> along = otg::plan_time_optimal(
                    {0.0, s_dot_0, a_s0}, {longest, 0.0, 0.0}, limits);
                if(!along) {
                    return along.error();
                }
                active_profile = along.value();
                active_duration = active_profile.duration_cycles();
            } else {
                active_profile = otg::Profile1D{};
                active_duration = 1;
            }
            return rt::ErrorCode::ok;
        }

        // Non-aligned takeover: plan both profiles with beta-split limits.
        for(std::size_t i = 0; i < axis_count; ++i) {
            lateral_dir_[i] = lat_vel[i] / lat_speed;
        }

        // Lateral decay: from (pos=0, vel=lat_speed) to (pos=0, vel=0).
        // The profile overshoots, peaks at R_tube, then returns to zero.
        const otg::Limits1D lat_limits{
            (1.0 - kBeta) * limits.max_velocity,
            (1.0 - kBeta) * limits.max_acceleration,
            (1.0 - kBeta) * limits.max_deceleration,
            (1.0 - kBeta) * limits.max_jerk,
        };
        const rt::Result<otg::Profile1D> lat =
            otg::plan_time_optimal({0.0, lat_speed, 0.0}, {0.0, 0.0, 0.0},
                                   lat_limits);
        if(!lat) {
            return lat.error();
        }
        lateral_profile_ = lat.value();
        duration_ = lateral_profile_.duration_cycles();

        // R_tube = peak lateral displacement during the decay profile.
        double max_disp = 0.0;
        for(std::int64_t t = 1; t <= duration_; ++t) {
            const otg::State1D s =
                otg::sample(lateral_profile_, rt::CycleTick::from_cycles(t));
            const double d = std::fabs(s.position);
            if(d > max_disp) {
                max_disp = d;
            }
        }
        r_tube_ = max_disp;

        // Along-path: from (pos=0, vel=s_dot_0, acc=a_s0) to
        // (pos=longest, vel=0, acc=0) with beta-split limits.
        // KB-052: a_s0 included for acceleration continuity.
        const otg::Limits1D along_limits{
            kBeta * limits.max_velocity,
            kBeta * limits.max_acceleration,
            kBeta * limits.max_deceleration,
            kBeta * limits.max_jerk,
        };

        // Clamp a_s0 to the beta-split acceleration limits so the OTG
        // entry state is admissible under the split budget.
        if(a_s0 > along_limits.max_acceleration) {
            a_s0 = along_limits.max_acceleration;
        } else if(a_s0 < -along_limits.max_deceleration) {
            a_s0 = -along_limits.max_deceleration;
        }

        // KB-053: reject the connector when the along-path stopping
        // distance far exceeds the path length. The rest-start fallback
        // is safe (starts from zero velocity, no cliff).
        if(longest > 0.0) {
            const double stop_dist =
                otg::detail::ramp_between(s_dot_0, 0.0, along_limits).distance;
            if(stop_dist > longest * 1.5) {
                return rt::ErrorCode::infeasible;
            }
            const rt::Result<otg::Profile1D> along = otg::plan_time_optimal(
                {0.0, s_dot_0, a_s0}, {longest, 0.0, 0.0}, along_limits);
            if(!along) {
                return along.error();
            }
            active_profile = along.value();
            active_duration =
                std::max(active_profile.duration_cycles(), duration_);
        } else {
            // Zero-distance: all velocity is lateral, along-path is trivial.
            active_profile = otg::Profile1D{};
            active_duration = duration_;
        }
        active_ = true;
        return rt::ErrorCode::ok;
    }

    // Y7 (KB-051): during the connector, the cycle output is the along-path
    // position plus this lateral decay offset. After the lateral profile
    // completes, its position is zero and the connector deactivates; the
    // motion continues as pure along-path interpolation. O(1) per cycle.
    double sample_lateral_offset(std::int64_t tick)
    {
        if(!active_) {
            return 0.0;
        }
        const otg::State1D lat =
            otg::sample(lateral_profile_, rt::CycleTick::from_cycles(tick));
        if(tick >= duration_) {
            active_ = false;
        }
        return lat.position;
    }

    double lateral_dir(std::size_t index) const
    {
        return lateral_dir_[index];
    }

private:
    std::int64_t duration_ = 0;
    double r_tube_ = 0.0;
    std::array<double, MaxAxes> lateral_dir_{};
    std::array<double, MaxAxes> velocity_{};
    std::array<double, MaxAxes> acceleration_{};
    otg::Profile1D lateral_profile_{};
    bool active_ = false;
    bool has_velocity_ = false;
};

} // namespace plcopen::core::axis
