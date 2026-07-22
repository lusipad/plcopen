#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "rt/cycle.h"
#include "rt/error.h"

// Y7 aborting-takeover connector (KB-051/052/053 and Y7b1 joint-domain
// circular extension). Extracted from AxisGroup as the first behavior
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

    bool vector_mode() const
    {
        return active_ && vector_mode_;
    }

    bool captured_vector_state() const
    {
        return captured_vector_state_;
    }

    void deactivate()
    {
        active_ = false;
        vector_mode_ = false;
    }

    void discard_capture()
    {
        has_velocity_ = false;
        captured_vector_state_ = false;
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

    // AxisGroup emits positions at integer cycles. Keep the last two output
    // differences separate from the analytical path derivatives: the latter
    // seed the continuous profiles, while these values validate the first
    // post-takeover discrete acceleration and jerk without a false boundary
    // mismatch.
    void set_captured_output_state(
        const std::array<double, MaxAxes> &velocity,
        const std::array<double, MaxAxes> &acceleration,
        std::size_t axis_count)
    {
        if(!has_velocity_) {
            return;
        }
        for(std::size_t i = 0; i < axis_count; ++i) {
            output_velocity_[i] = velocity[i];
            output_acceleration_[i] = acceleration[i];
        }
    }

    // Y7b2a: Cartesian sources have no cross-plugin analytical member
    // derivatives, but AxisGroup already records their exact discrete member
    // output history. Capture that history as both the connector initial state
    // and its first-sample validation baseline. The joint-domain target still
    // supplies the analytical path derivatives used by the planner.
    void capture_output_history(
        bool motion_active,
        const std::array<double, MaxAxes> &velocity,
        const std::array<double, MaxAxes> &acceleration,
        std::size_t axis_count)
    {
        has_velocity_ = false;
        captured_vector_state_ = false;
        if(!motion_active) {
            return;
        }
        velocity_.fill(0.0);
        acceleration_.fill(0.0);
        output_velocity_.fill(0.0);
        output_acceleration_.fill(0.0);
        for(std::size_t i = 0; i < axis_count; ++i) {
            velocity_[i] = velocity[i];
            acceleration_[i] = acceleration[i];
            output_velocity_[i] = velocity[i];
            output_acceleration_[i] = acceleration[i];
        }
        captured_vector_state_ = true;
        has_velocity_ = true;
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
        captured_vector_state_ = false;
        if(!motion_active || !linear_path || path_length <= 0.0) {
            return;
        }
        const otg::State1D along =
            otg::sample(along_profile, rt::CycleTick::from_cycles(tick));
        std::array<double, MaxAxes> q_s{};
        std::array<double, MaxAxes> q_ss{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            q_s[i] = (finish[i] - start[i]) / path_length;
        }
        captured_vector_state_ = active_ && vector_mode_;
        capture_state(along, q_s, q_ss, tick, axis_count);
    }

    // Y7b1: circular paths are also scalar arc-length paths. The first two
    // members use the analytical arc derivatives; higher members follow the
    // arc fraction linearly.
    void capture_circular(bool motion_active, const geom::ArcSegment &arc,
                          double path_length,
                          const otg::Profile1D &along_profile,
                          std::int64_t tick,
                          const std::array<double, MaxAxes> &start,
                          const std::array<double, MaxAxes> &finish,
                          std::size_t axis_count)
    {
        has_velocity_ = false;
        captured_vector_state_ = false;
        if(!motion_active || path_length <= 0.0 || axis_count < 2) {
            return;
        }
        const otg::State1D along =
            otg::sample(along_profile, rt::CycleTick::from_cycles(tick));
        std::array<double, MaxAxes> q_s{};
        std::array<double, MaxAxes> q_ss{};
        circular_derivatives(arc, along.position, path_length, start, finish,
                             axis_count, q_s, q_ss, nullptr);
        captured_vector_state_ = true;
        capture_state(along, q_s, q_ss, tick, axis_count);
    }

private:
    void capture_state(const otg::State1D &along,
                       const std::array<double, MaxAxes> &q_s,
                       const std::array<double, MaxAxes> &q_ss,
                       std::int64_t tick, std::size_t axis_count)
    {
        for(std::size_t i = 0; i < axis_count; ++i) {
            velocity_[i] = along.velocity * q_s[i];
            acceleration_[i] = q_ss[i] * along.velocity * along.velocity +
                               q_s[i] * along.acceleration;
        }
        if(active_ && vector_mode_) {
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lat = otg::sample(
                    lateral_profiles_[i], rt::CycleTick::from_cycles(tick));
                velocity_[i] += lat.velocity;
                acceleration_[i] += lat.acceleration;
            }
        } else if(active_) {
            const otg::State1D lat = otg::sample(
                lateral_profile_, rt::CycleTick::from_cycles(tick));
            for(std::size_t i = 0; i < axis_count; ++i) {
                velocity_[i] += lat.velocity * lateral_dir_[i];
                acceleration_[i] += lat.acceleration * lateral_dir_[i];
            }
        }
        output_velocity_ = velocity_;
        output_acceleration_ = acceleration_;
        has_velocity_ = true;
    }

    rt::ErrorCode plan_synchronized_residual_profiles(
        const otg::Limits1D &limits,
        const std::array<otg::State1D, MaxAxes> &residuals,
        std::size_t axis_count, double state_threshold,
        std::array<otg::Profile1D, MaxAxes> &profiles,
        std::int64_t &duration) const
    {
        duration = 0;
        std::array<bool, MaxAxes> active_residuals{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            const otg::State1D residual = residuals[i];
            if(std::fabs(residual.position) <= state_threshold &&
               std::fabs(residual.velocity) <= state_threshold &&
               std::fabs(residual.acceleration) <= state_threshold) {
                continue;
            }
            active_residuals[i] = true;
            const rt::Result<otg::Profile1D> optimal =
                otg::plan_time_optimal(residual, {0.0, 0.0, 0.0}, limits);
            if(!optimal) {
                return optimal.error();
            }
            duration = std::max(duration, optimal.value().duration_cycles());
        }
        if(duration == 0) {
            profiles = {};
            return rt::ErrorCode::ok;
        }

        std::array<otg::Profile1D, MaxAxes> planned{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            if(!active_residuals[i]) {
                continue;
            }
            const rt::Result<otg::Profile1D> fixed =
                otg::solve_fixed_time(residuals[i], {0.0, 0.0, 0.0}, limits,
                                      duration);
            if(!fixed) {
                return fixed.error();
            }
            planned[i] = fixed.value();
        }
        profiles = planned;
        return rt::ErrorCode::ok;
    }

public:

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

        // New scalar-path derivative. Linear group paths use the longest-axis
        // parameter (KB-054), so this vector is not necessarily Euclidean
        // unit length.
        std::array<double, MaxAxes> q_s{};
        if(longest > 0.0) {
            for(std::size_t i = 0; i < axis_count; ++i) {
                q_s[i] = (finish[i] - start[i]) / longest;
            }
        }

        // Least-squares projection onto q_s. Dividing by |q_s|^2 is required
        // for diagonal longest-axis paths, where q_s is not unit length.
        double q_s_norm_sq = 0.0;
        double s_dot_0 = 0.0;
        double a_s0 = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            q_s_norm_sq += q_s[i] * q_s[i];
            s_dot_0 += velocity_[i] * q_s[i];
            a_s0 += acceleration_[i] * q_s[i];
        }
        if(q_s_norm_sq > 0.0) {
            s_dot_0 /= q_s_norm_sq;
            a_s0 /= q_s_norm_sq;
        }

        // Lateral residual velocity.
        double lat_speed_sq = 0.0;
        std::array<double, MaxAxes> lat_vel{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            lat_vel[i] = velocity_[i] - s_dot_0 * q_s[i];
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

    // A curved source can carry member acceleration that is not parallel to
    // its tangent. When its successor is linear, preserve that full vector in
    // per-member residual profiles; the legacy scalar linear connector remains
    // unchanged for plain linear sources and their replay contract.
    rt::ErrorCode plan_linear_vector(
        const otg::Limits1D &limits, double path_length,
        std::size_t axis_count,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        otg::Profile1D &active_profile,
        std::int64_t &active_duration)
    {
        constexpr double kBeta = 0.5;
        constexpr double kStateThreshold = 1e-12;

        active_ = false;
        vector_mode_ = true;
        duration_ = 0;
        r_tube_ = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            lateral_profiles_[i] = otg::Profile1D{};
        }
        if(path_length <= 0.0) {
            vector_mode_ = false;
            return rt::ErrorCode::invalid_argument;
        }

        std::array<double, MaxAxes> q_s{};
        double q_s_norm_sq = 0.0;
        double s_dot_0 = 0.0;
        double a_s0 = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            q_s[i] = (finish[i] - start[i]) / path_length;
            q_s_norm_sq += q_s[i] * q_s[i];
            s_dot_0 += velocity_[i] * q_s[i];
            a_s0 += acceleration_[i] * q_s[i];
        }
        if(q_s_norm_sq <= 1e-18) {
            vector_mode_ = false;
            return rt::ErrorCode::infeasible;
        }
        s_dot_0 /= q_s_norm_sq;
        a_s0 /= q_s_norm_sq;

        bool has_residual = false;
        for(std::size_t i = 0; i < axis_count; ++i) {
            const double residual_velocity =
                velocity_[i] - q_s[i] * s_dot_0;
            const double residual_acceleration =
                acceleration_[i] - q_s[i] * a_s0;
            if(std::fabs(residual_velocity) > kStateThreshold ||
               std::fabs(residual_acceleration) > kStateThreshold) {
                has_residual = true;
                break;
            }
        }
        if(!has_residual) {
            const rt::Result<otg::Profile1D> along = otg::plan_time_optimal(
                {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0}, limits);
            if(!along) {
                vector_mode_ = false;
                return along.error();
            }
            const std::int64_t along_duration =
                along.value().duration_cycles();
            if(!validate_linear_profiles(
                   limits, path_length, q_s, axis_count, along.value(),
                   lateral_profiles_, output_velocity_, output_acceleration_,
                   along_duration)) {
                vector_mode_ = false;
                return rt::ErrorCode::infeasible;
            }
            active_profile = along.value();
            active_duration = along_duration;
            vector_mode_ = false;
            return rt::ErrorCode::ok;
        }

        const otg::Limits1D along_limits{
            kBeta * limits.max_velocity,
            kBeta * limits.max_acceleration,
            kBeta * limits.max_deceleration,
            kBeta * limits.max_jerk,
        };
        if(a_s0 > along_limits.max_acceleration) {
            a_s0 = along_limits.max_acceleration;
        } else if(a_s0 < -along_limits.max_deceleration) {
            a_s0 = -along_limits.max_deceleration;
        }
        const double stop_dist =
            otg::detail::ramp_between(s_dot_0, 0.0, along_limits).distance;
        if(stop_dist > path_length) {
            vector_mode_ = false;
            return rt::ErrorCode::infeasible;
        }
        const rt::Result<otg::Profile1D> along = otg::plan_time_optimal(
            {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0}, along_limits);
        if(!along) {
            vector_mode_ = false;
            return along.error();
        }
        active_profile = along.value();
        active_duration = active_profile.duration_cycles();

        const otg::Limits1D lateral_limits{
            (1.0 - kBeta) * limits.max_velocity,
            (1.0 - kBeta) * limits.max_acceleration,
            (1.0 - kBeta) * limits.max_deceleration,
            (1.0 - kBeta) * limits.max_jerk,
        };
        std::array<otg::State1D, MaxAxes> residuals{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            residuals[i] = {
                0.0,
                velocity_[i] - q_s[i] * s_dot_0,
                acceleration_[i] - q_s[i] * a_s0,
            };
        }
        std::array<otg::Profile1D, MaxAxes> planned_lateral{};
        std::int64_t planned_duration = 0;
        const rt::ErrorCode lateral_error = plan_synchronized_residual_profiles(
            lateral_limits, residuals, axis_count, kStateThreshold,
            planned_lateral, planned_duration);
        if(lateral_error != rt::ErrorCode::ok) {
            vector_mode_ = false;
            return lateral_error;
        }
        lateral_profiles_ = planned_lateral;
        duration_ = planned_duration;
        active_duration = std::max(active_duration, duration_);
        if(!validate_linear_profiles(limits, path_length, q_s, axis_count,
                                     active_profile, lateral_profiles_,
                                     output_velocity_, output_acceleration_,
                                     active_duration)) {
            otg::Limits1D bounded_limits = along_limits;
            bounded_limits.max_velocity = std::max(
                bounded_limits.max_velocity, std::fabs(s_dot_0));
            otg::Profile1D bounded_profile{};
            rt::ErrorCode bounded = make_bounded_quintic(
                {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0},
                bounded_limits, bounded_profile);
            std::int64_t bounded_duration = std::max(
                bounded_profile.duration_cycles(), duration_);
            bool bounded_valid =
                bounded == rt::ErrorCode::ok &&
                validate_linear_profiles(
                    limits, path_length, q_s, axis_count, bounded_profile,
                    lateral_profiles_, output_velocity_, output_acceleration_,
                    bounded_duration);
            if(!bounded_valid) {
                bounded = make_bounded_stop_then_go(
                    {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0},
                    bounded_limits, bounded_profile);
                bounded_duration = std::max(
                    bounded_profile.duration_cycles(), duration_);
                bounded_valid =
                    bounded == rt::ErrorCode::ok &&
                    validate_linear_profiles(
                        limits, path_length, q_s, axis_count,
                        bounded_profile, lateral_profiles_, output_velocity_,
                        output_acceleration_, bounded_duration);
            }
            if(!bounded_valid) {
                vector_mode_ = false;
                return rt::ErrorCode::infeasible;
            }
            active_profile = bounded_profile;
            active_duration = bounded_duration;
        }
        for(std::int64_t tick = 1; tick <= duration_; ++tick) {
            double radius_sq = 0.0;
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles_[i], rt::CycleTick::from_cycles(tick));
                radius_sq += lateral.position * lateral.position;
            }
            r_tube_ = std::max(r_tube_, std::sqrt(radius_sq));
        }
        active_ = duration_ > 0;
        return rt::ErrorCode::ok;
    }

    // Y7b1 circular planner. The base path keeps the analytical curved
    // derivatives while each member's orthogonal residual returns to zero in
    // a fixed-capacity Profile1D. All planning and chain-rule validation runs
    // at submit time; the cycle path only samples the resulting profiles.
    rt::ErrorCode plan_circular(
        const otg::Limits1D &limits, double path_length,
        std::size_t axis_count,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        const geom::ArcSegment &arc,
        otg::Profile1D &active_profile,
        std::int64_t &active_duration)
    {
        constexpr double kBeta = 0.5;
        constexpr double kStateThreshold = 1e-12;

        active_ = false;
        vector_mode_ = true;
        duration_ = 0;
        r_tube_ = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            lateral_profiles_[i] = otg::Profile1D{};
        }
        if(path_length <= 0.0 || axis_count < 2) {
            vector_mode_ = false;
            return rt::ErrorCode::invalid_argument;
        }

        std::array<double, MaxAxes> q_s{};
        std::array<double, MaxAxes> q_ss{};
        circular_derivatives(arc, 0.0, path_length, start, finish, axis_count,
                             q_s, q_ss, nullptr);
        double q_s_norm_sq = 0.0;
        double s_dot_0 = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            q_s_norm_sq += q_s[i] * q_s[i];
            s_dot_0 += velocity_[i] * q_s[i];
        }
        if(q_s_norm_sq <= 1e-18) {
            vector_mode_ = false;
            return rt::ErrorCode::infeasible;
        }
        s_dot_0 /= q_s_norm_sq;
        // Arc sampling is defined only on [0,L]. A negative projection stays
        // in the residual connector instead of driving a clamped base path.
        if(s_dot_0 < 0.0) {
            s_dot_0 = 0.0;
        }

        double a_s0 = 0.0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            a_s0 += (acceleration_[i] -
                     q_ss[i] * s_dot_0 * s_dot_0) * q_s[i];
        }
        a_s0 /= q_s_norm_sq;
        if(s_dot_0 <= kStateThreshold && a_s0 < 0.0) {
            // The arc has no negative-length extension. Keep an outward
            // boundary acceleration in the residual instead of asking the
            // scalar profile to move into a clamped region.
            a_s0 = 0.0;
        }

        const otg::Limits1D along_limits = circular_along_limits(
            limits, kBeta, arc, path_length, start, finish, axis_count);
        if(along_limits.max_velocity <= 0.0 ||
           along_limits.max_acceleration <= 0.0 ||
           along_limits.max_deceleration <= 0.0 ||
           along_limits.max_jerk <= 0.0) {
            vector_mode_ = false;
            return rt::ErrorCode::infeasible;
        }
        if(a_s0 > along_limits.max_acceleration) {
            a_s0 = along_limits.max_acceleration;
        } else if(a_s0 < -along_limits.max_deceleration) {
            a_s0 = -along_limits.max_deceleration;
        }

        const double stop_dist =
            otg::detail::ramp_between(s_dot_0, 0.0, along_limits).distance;
        if(stop_dist > path_length) {
            vector_mode_ = false;
            return rt::ErrorCode::infeasible;
        }
        const rt::Result<otg::Profile1D> along = otg::plan_time_optimal(
            {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0}, along_limits);
        if(!along) {
            vector_mode_ = false;
            return along.error();
        }
        active_profile = along.value();
        active_duration = active_profile.duration_cycles();

        const otg::Limits1D lateral_limits{
            (1.0 - kBeta) * limits.max_velocity,
            (1.0 - kBeta) * limits.max_acceleration,
            (1.0 - kBeta) * limits.max_deceleration,
            (1.0 - kBeta) * limits.max_jerk,
        };
        std::array<otg::State1D, MaxAxes> residuals{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            const double base_acceleration =
                q_ss[i] * s_dot_0 * s_dot_0 + q_s[i] * a_s0;
            residuals[i] = {
                0.0,
                velocity_[i] - q_s[i] * s_dot_0,
                acceleration_[i] - base_acceleration,
            };
        }
        std::array<otg::Profile1D, MaxAxes> planned_lateral{};
        std::int64_t planned_duration = 0;
        const rt::ErrorCode lateral_error = plan_synchronized_residual_profiles(
            lateral_limits, residuals, axis_count, kStateThreshold,
            planned_lateral, planned_duration);
        if(lateral_error != rt::ErrorCode::ok) {
            vector_mode_ = false;
            return lateral_error;
        }
        lateral_profiles_ = planned_lateral;
        duration_ = planned_duration;
        active_duration = std::max(active_duration, duration_);

        if(!validate_circular_profiles(limits, path_length, axis_count,
                                       start, finish, arc, active_profile,
                                       lateral_profiles_, output_velocity_,
                                       output_acceleration_, active_duration)) {
            // Integer-quantized time-optimal corrections can cross the finite
            // arc endpoint with non-zero speed before returning to the target.
            // The runtime clamp would turn that into an acceleration cliff.
            // Build a scalar-feasible quintic at the correct duration scale
            // (doubling+bisection for zero boundary acceleration, bounded
            // geometric growth otherwise), then accept it only if the complete
            // member-space output validates at every cycle.
            otg::Limits1D bounded_limits = along_limits;
            bounded_limits.max_velocity = std::max(
                bounded_limits.max_velocity, std::fabs(s_dot_0));
            otg::Profile1D bounded_profile{};
            rt::ErrorCode bounded = make_bounded_quintic(
                {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0},
                bounded_limits, bounded_profile);
            std::int64_t bounded_duration = std::max(
                bounded_profile.duration_cycles(), duration_);
            bool bounded_valid =
                bounded == rt::ErrorCode::ok &&
                validate_circular_profiles(
                    limits, path_length, axis_count, start, finish, arc,
                    bounded_profile, lateral_profiles_, output_velocity_,
                    output_acceleration_, bounded_duration);
            if(!bounded_valid) {
                bounded = make_bounded_stop_then_go(
                    {0.0, s_dot_0, a_s0}, {path_length, 0.0, 0.0},
                    bounded_limits, bounded_profile);
                bounded_duration = std::max(
                    bounded_profile.duration_cycles(), duration_);
                bounded_valid =
                    bounded == rt::ErrorCode::ok &&
                    validate_circular_profiles(
                        limits, path_length, axis_count, start, finish, arc,
                        bounded_profile, lateral_profiles_, output_velocity_,
                        output_acceleration_, bounded_duration);
            }
            if(!bounded_valid) {
                vector_mode_ = false;
                return rt::ErrorCode::infeasible;
            }
            active_profile = bounded_profile;
            active_duration = bounded_duration;
        }

        for(std::int64_t tick = 1; tick <= duration_; ++tick) {
            double radius_sq = 0.0;
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles_[i], rt::CycleTick::from_cycles(tick));
                radius_sq += lateral.position * lateral.position;
            }
            r_tube_ = std::max(r_tube_, std::sqrt(radius_sq));
        }
        active_ = duration_ > 0;
        return rt::ErrorCode::ok;
    }

    // Re-plan both parts of a live vector connector on a linear target for
    // GroupStop. A stop that cannot fit before the finite target is rejected;
    // AxisGroup then freezes at the last commanded point instead of discarding
    // the member residual.
    rt::ErrorCode plan_linear_vector_stop(
        const otg::Limits1D &limits, double path_length,
        std::size_t axis_count,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        const otg::Profile1D &current_along_profile,
        std::int64_t current_tick,
        const std::array<double, MaxAxes> &initial_output_velocity,
        const std::array<double, MaxAxes> &initial_output_acceleration,
        otg::Profile1D &halt_profile,
        std::int64_t &halt_duration)
    {
        constexpr double kBeta = 0.5;
        constexpr double kStateThreshold = 1e-12;
        if(!active_ || !vector_mode_ || path_length <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }

        std::array<double, MaxAxes> q_s{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            q_s[i] = (finish[i] - start[i]) / path_length;
        }
        const otg::Limits1D along_limits{
            kBeta * limits.max_velocity,
            kBeta * limits.max_acceleration,
            kBeta * limits.max_deceleration,
            kBeta * limits.max_jerk,
        };
        const otg::State1D along_state = otg::sample(
            current_along_profile, rt::CycleTick::from_cycles(current_tick));
        std::array<otg::State1D, MaxAxes> live_lateral{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            live_lateral[i] = otg::sample(
                lateral_profiles_[i], rt::CycleTick::from_cycles(current_tick));
        }

        otg::Profile1D stopped_along{};
        otg::State1D reduced_along = along_state;
        rt::ErrorCode reduced = append_acceleration_zeroing(
            stopped_along, reduced_along, along_limits.max_jerk);
        if(reduced != rt::ErrorCode::ok) {
            return reduced;
        }
        double brake_velocity = reduced_along.velocity;
        if(brake_velocity < 0.0) {
            brake_velocity = 0.0;
        }
        const double stop_position =
            reduced_along.position +
            otg::detail::ramp_between(brake_velocity, 0.0, along_limits)
                .distance;
        const double path_tolerance =
            1e-12 * std::max(1.0, path_length);
        if(stop_position > path_length + path_tolerance) {
            return rt::ErrorCode::infeasible;
        }
        const rt::Result<otg::Profile1D> along_tail = otg::plan_time_optimal(
            reduced_along, {stop_position, 0.0, 0.0}, along_limits);
        if(!along_tail) {
            return along_tail.error();
        }
        reduced = append_profile(stopped_along, along_tail.value());
        if(reduced != rt::ErrorCode::ok) {
            return reduced;
        }

        // A residual can reverse while the composed member velocity still
        // brakes. Use the stricter signed bound so the independent residual
        // stop cannot exceed either requested member acceleration envelope.
        const double lateral_acceleration_limit =
            (1.0 - kBeta) *
            std::min(limits.max_acceleration, limits.max_deceleration);
        const otg::Limits1D lateral_limits{
            (1.0 - kBeta) * limits.max_velocity,
            lateral_acceleration_limit,
            lateral_acceleration_limit,
            (1.0 - kBeta) * limits.max_jerk,
        };
        std::array<otg::Profile1D, MaxAxes> stopped_lateral{};
        std::int64_t lateral_duration = 0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            const otg::State1D live = live_lateral[i];
            if(std::fabs(live.position) <= kStateThreshold &&
               std::fabs(live.velocity) <= kStateThreshold &&
               std::fabs(live.acceleration) <= kStateThreshold) {
                continue;
            }
            otg::State1D reduced_lateral = live;
            reduced = append_acceleration_zeroing(
                stopped_lateral[i], reduced_lateral,
                lateral_limits.max_jerk);
            if(reduced != rt::ErrorCode::ok) {
                return reduced;
            }
            const rt::Result<otg::Profile1D> lateral_tail =
                otg::plan_time_optimal(
                    reduced_lateral, {0.0, 0.0, 0.0}, lateral_limits);
            if(!lateral_tail) {
                return lateral_tail.error();
            }
            reduced = append_profile(stopped_lateral[i], lateral_tail.value());
            if(reduced != rt::ErrorCode::ok) {
                return reduced;
            }
            lateral_duration = std::max(
                lateral_duration, stopped_lateral[i].duration_cycles());
        }

        const std::int64_t total_duration = std::max(
            stopped_along.duration_cycles(), lateral_duration);
        otg::Limits1D validation_limits = limits;
        for(std::size_t i = 0; i < axis_count; ++i) {
            const double analytic_acceleration =
                q_s[i] * along_state.acceleration +
                live_lateral[i].acceleration;
            validation_limits.max_acceleration = std::max(
                validation_limits.max_acceleration,
                std::max(initial_output_acceleration[i],
                         analytic_acceleration));
            validation_limits.max_deceleration = std::max(
                validation_limits.max_deceleration,
                std::max(-initial_output_acceleration[i],
                         -analytic_acceleration));
        }
        if(!validate_linear_profiles(
               validation_limits, path_length, q_s, axis_count,
               stopped_along, stopped_lateral, initial_output_velocity,
               initial_output_acceleration, total_duration)) {
            return rt::ErrorCode::infeasible;
        }

        halt_profile = stopped_along;
        halt_duration = total_duration;
        lateral_profiles_ = stopped_lateral;
        duration_ = lateral_duration;
        r_tube_ = 0.0;
        for(std::int64_t tick = 0; tick <= duration_; ++tick) {
            double radius_sq = 0.0;
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles_[i], rt::CycleTick::from_cycles(tick));
                radius_sq += lateral.position * lateral.position;
            }
            r_tube_ = std::max(r_tube_, std::sqrt(radius_sq));
        }
        active_ = duration_ > 0;
        vector_mode_ = active_;
        return rt::ErrorCode::ok;
    }

    // Re-plan both parts of a live circular connector for GroupStop. The
    // scalar brake and every member residual start from their sampled state,
    // so resetting AxisGroup's active tick does not discard or rewind the
    // tolerance-tube offset.
    rt::ErrorCode plan_circular_stop(
        const otg::Limits1D &limits, double path_length,
        std::size_t axis_count,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        const geom::ArcSegment &arc,
        const otg::Profile1D &current_along_profile,
        std::int64_t current_tick,
        const std::array<double, MaxAxes> &initial_output_velocity,
        const std::array<double, MaxAxes> &initial_output_acceleration,
        otg::Profile1D &halt_profile,
        std::int64_t &halt_duration)
    {
        constexpr double kBeta = 0.5;
        constexpr double kStateThreshold = 1e-12;
        if(!active_ || !vector_mode_ || path_length <= 0.0 || axis_count < 2) {
            return rt::ErrorCode::invalid_argument;
        }

        const otg::State1D along_state = otg::sample(
            current_along_profile, rt::CycleTick::from_cycles(current_tick));
        const otg::Limits1D along_limits = circular_along_limits(
            limits, kBeta, arc, path_length, start, finish, axis_count);
        if(along_limits.max_velocity <= 0.0 ||
           along_limits.max_acceleration <= 0.0 ||
           along_limits.max_deceleration <= 0.0 ||
           along_limits.max_jerk <= 0.0) {
            return rt::ErrorCode::infeasible;
        }

        std::array<otg::State1D, MaxAxes> live_lateral{};
        for(std::size_t i = 0; i < axis_count; ++i) {
            live_lateral[i] = otg::sample(
                lateral_profiles_[i], rt::CycleTick::from_cycles(current_tick));
        }

        otg::Profile1D stopped_along{};
        otg::State1D reduced_along = along_state;
        rt::ErrorCode reduced = append_acceleration_zeroing(
            stopped_along, reduced_along, along_limits.max_jerk);
        if(reduced != rt::ErrorCode::ok) {
            return reduced;
        }
        double brake_velocity = reduced_along.velocity;
        if(brake_velocity < 0.0) {
            brake_velocity = 0.0;
        }
        const double stop_position =
            reduced_along.position +
            otg::detail::ramp_between(brake_velocity, 0.0, along_limits)
                .distance;
        if(stop_position < -1e-9 || stop_position > path_length + 1e-9) {
            return rt::ErrorCode::infeasible;
        }
        const rt::Result<otg::Profile1D> along_tail = otg::plan_time_optimal(
            reduced_along, {stop_position, 0.0, 0.0}, along_limits);
        if(!along_tail) {
            return along_tail.error();
        }
        reduced = append_profile(stopped_along, along_tail.value());
        if(reduced != rt::ErrorCode::ok) {
            return reduced;
        }

        const otg::Limits1D lateral_limits{
            (1.0 - kBeta) * limits.max_velocity,
            (1.0 - kBeta) * limits.max_acceleration,
            (1.0 - kBeta) * limits.max_deceleration,
            (1.0 - kBeta) * limits.max_jerk,
        };
        std::array<otg::Profile1D, MaxAxes> stopped_lateral{};
        std::int64_t lateral_duration = 0;
        for(std::size_t i = 0; i < axis_count; ++i) {
            const otg::State1D live = live_lateral[i];
            if(std::fabs(live.position) <= kStateThreshold &&
               std::fabs(live.velocity) <= kStateThreshold &&
               std::fabs(live.acceleration) <= kStateThreshold) {
                continue;
            }
            otg::State1D reduced_lateral = live;
            reduced = append_acceleration_zeroing(
                stopped_lateral[i], reduced_lateral,
                lateral_limits.max_jerk);
            if(reduced != rt::ErrorCode::ok) {
                return reduced;
            }
            const rt::Result<otg::Profile1D> lateral_tail =
                otg::plan_time_optimal(
                    reduced_lateral, {0.0, 0.0, 0.0}, lateral_limits);
            if(!lateral_tail) {
                return lateral_tail.error();
            }
            reduced = append_profile(stopped_lateral[i], lateral_tail.value());
            if(reduced != rt::ErrorCode::ok) {
                return reduced;
            }
            lateral_duration = std::max(
                lateral_duration, stopped_lateral[i].duration_cycles());
        }

        const std::int64_t total_duration = std::max(
            stopped_along.duration_cycles(), lateral_duration);
        otg::Limits1D validation_limits = limits;
        std::array<double, MaxAxes> q_s{};
        std::array<double, MaxAxes> q_ss{};
        circular_derivatives(arc, along_state.position, path_length, start,
                             finish, axis_count, q_s, q_ss, nullptr);
        for(std::size_t i = 0; i < axis_count; ++i) {
            const double analytic_acceleration =
                q_ss[i] * along_state.velocity * along_state.velocity +
                q_s[i] * along_state.acceleration +
                live_lateral[i].acceleration;
            validation_limits.max_acceleration = std::max(
                validation_limits.max_acceleration,
                std::max(initial_output_acceleration[i],
                         analytic_acceleration));
            validation_limits.max_deceleration = std::max(
                validation_limits.max_deceleration,
                std::max(-initial_output_acceleration[i],
                         -analytic_acceleration));
        }
        if(!validate_circular_profiles(
               validation_limits, path_length, axis_count, start, finish, arc,
               stopped_along, stopped_lateral, initial_output_velocity,
               initial_output_acceleration, total_duration)) {
            return rt::ErrorCode::infeasible;
        }

        halt_profile = stopped_along;
        halt_duration = total_duration;
        lateral_profiles_ = stopped_lateral;
        duration_ = lateral_duration;
        r_tube_ = 0.0;
        for(std::int64_t tick = 0; tick <= duration_; ++tick) {
            double radius_sq = 0.0;
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles_[i], rt::CycleTick::from_cycles(tick));
                radius_sq += lateral.position * lateral.position;
            }
            r_tube_ = std::max(r_tube_, std::sqrt(radius_sq));
        }
        active_ = duration_ > 0;
        vector_mode_ = active_;
        return rt::ErrorCode::ok;
    }

    void sample_lateral_offsets(std::int64_t tick,
                                std::array<double, MaxAxes> &offsets,
                                std::size_t axis_count)
    {
        offsets.fill(0.0);
        if(!active_ || !vector_mode_) {
            return;
        }
        for(std::size_t i = 0; i < axis_count; ++i) {
            offsets[i] = otg::sample(
                lateral_profiles_[i], rt::CycleTick::from_cycles(tick)).position;
        }
        if(tick >= duration_) {
            active_ = false;
        }
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
    static rt::ErrorCode make_bounded_quintic(
        const otg::State1D &start, const otg::Target1D &finish,
        const otg::Limits1D &limits, otg::Profile1D &profile)
    {
        profile = otg::Profile1D{};
        return otg::detail::push_quintic_correction(
            profile, start, finish, limits);
    }

    static rt::ErrorCode make_bounded_stop_then_go(
        const otg::State1D &start, const otg::Target1D &finish,
        const otg::Limits1D &limits, otg::Profile1D &profile)
    {
        constexpr double kTolerance = 1e-12;
        if(finish.velocity != 0.0 || finish.acceleration != 0.0) {
            return rt::ErrorCode::invalid_argument;
        }

        profile = otg::Profile1D{};
        otg::State1D stopped = start;
        rt::ErrorCode planned = append_acceleration_zeroing(
            profile, stopped, limits.max_jerk);
        if(planned != rt::ErrorCode::ok) {
            return planned;
        }
        planned = otg::detail::push_ramp(
            profile, stopped, 0.0, limits,
            otg::detail::RampRounding::exact);
        if(planned != rt::ErrorCode::ok) {
            return planned;
        }

        const double position_tolerance =
            kTolerance * std::max(1.0, std::fabs(finish.position));
        if(stopped.position > finish.position + position_tolerance) {
            return rt::ErrorCode::infeasible;
        }
        return otg::detail::push_quintic_correction(
            profile, stopped, finish, limits);
    }

    static rt::ErrorCode append_profile(otg::Profile1D &destination,
                                        const otg::Profile1D &source)
    {
        for(std::size_t i = 0; i < source.segment_count(); ++i) {
            const rt::ErrorCode added = destination.add_segment(source.segment(i));
            if(added != rt::ErrorCode::ok) {
                return added;
            }
        }
        return rt::ErrorCode::ok;
    }

    static rt::ErrorCode append_acceleration_zeroing(
        otg::Profile1D &profile, otg::State1D &state, double max_jerk)
    {
        if(state.acceleration == 0.0) {
            return rt::ErrorCode::ok;
        }
        const double cycles =
            std::ceil(std::fabs(state.acceleration) / max_jerk);
        return otg::detail::push_cubic_phase(
            profile, state, -state.acceleration / cycles, cycles);
    }

    static bool validate_linear_profiles(
        const otg::Limits1D &limits,
        double path_length,
        const std::array<double, MaxAxes> &q_s,
        std::size_t axis_count,
        const otg::Profile1D &along_profile,
        const std::array<otg::Profile1D, MaxAxes> &lateral_profiles,
        const std::array<double, MaxAxes> &initial_velocity,
        const std::array<double, MaxAxes> &initial_acceleration,
        std::int64_t total_duration)
    {
        constexpr double kTolerance = 1e-9;
        const double path_tolerance =
            1e-12 * std::max(1.0, path_length);
        std::array<double, MaxAxes> previous_lateral_acceleration{};
        std::array<double, MaxAxes> previous_position{};
        std::array<double, MaxAxes> previous_velocity = initial_velocity;
        std::array<double, MaxAxes> previous_acceleration =
            initial_acceleration;
        otg::State1D previous_along{};
        bool terminal_clamp_entered = false;
        for(std::int64_t tick = 0; tick <= total_duration; ++tick) {
            const otg::State1D along = otg::sample(
                along_profile, rt::CycleTick::from_cycles(tick));
            if(terminal_clamp_entered &&
               along.position < path_length - path_tolerance) {
                return false;
            }
            if(along.position >= path_length - path_tolerance) {
                terminal_clamp_entered = true;
            }
            const double sampled_path =
                std::min(along.position, path_length);
            const double scalar_jerk =
                tick == 0
                    ? 0.0
                    : along.acceleration - previous_along.acceleration;
            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles[i], rt::CycleTick::from_cycles(tick));
                const double lateral_jerk =
                    tick == 0
                        ? 0.0
                        : lateral.acceleration -
                              previous_lateral_acceleration[i];
                const double velocity =
                    q_s[i] * along.velocity + lateral.velocity;
                const double acceleration =
                    q_s[i] * along.acceleration + lateral.acceleration;
                const double jerk =
                    q_s[i] * scalar_jerk + lateral_jerk;
                if(along.position <= path_length + path_tolerance) {
                    if(std::fabs(velocity) >
                           limits.max_velocity + kTolerance ||
                       acceleration >
                           limits.max_acceleration + kTolerance ||
                       acceleration <
                           -limits.max_deceleration - kTolerance ||
                       (tick > 0 &&
                        std::fabs(jerk) >
                            limits.max_jerk + kTolerance)) {
                        return false;
                    }
                }

                const double position =
                    q_s[i] * sampled_path + lateral.position;
                if(tick > 0) {
                    const double actual_velocity =
                        position - previous_position[i];
                    const double actual_acceleration =
                        actual_velocity - previous_velocity[i];
                    const double actual_jerk =
                        actual_acceleration - previous_acceleration[i];
                    if(std::fabs(actual_velocity) >
                           limits.max_velocity + kTolerance ||
                       actual_acceleration >
                           limits.max_acceleration + kTolerance ||
                       actual_acceleration <
                           -limits.max_deceleration - kTolerance ||
                        std::fabs(actual_jerk) >
                            limits.max_jerk + kTolerance) {
                        return false;
                    }
                    previous_velocity[i] = actual_velocity;
                    previous_acceleration[i] = actual_acceleration;
                }
                previous_position[i] = position;
                previous_lateral_acceleration[i] = lateral.acceleration;
            }
            previous_along = along;
        }
        return true;
    }

    static void circular_derivatives(
        const geom::ArcSegment &arc, double arclength, double path_length,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        std::size_t axis_count,
        std::array<double, MaxAxes> &q_s,
        std::array<double, MaxAxes> &q_ss,
        std::array<double, MaxAxes> *q_sss)
    {
        q_s.fill(0.0);
        q_ss.fill(0.0);
        if(q_sss != nullptr) {
            q_sss->fill(0.0);
        }
        const geom::Vec3 first = geom::path_derivative(arc, arclength);
        const geom::Vec3 second = geom::path_second_derivative(arc, arclength);
        q_s[0] = first.x;
        q_s[1] = first.y;
        q_ss[0] = second.x;
        q_ss[1] = second.y;
        if(q_sss != nullptr) {
            const geom::Vec3 third = geom::path_third_derivative(arc, arclength);
            (*q_sss)[0] = third.x;
            (*q_sss)[1] = third.y;
        }
        for(std::size_t i = 2; i < axis_count; ++i) {
            q_s[i] = (finish[i] - start[i]) / path_length;
        }
    }

    static otg::Limits1D circular_along_limits(
        const otg::Limits1D &limits, double beta,
        const geom::ArcSegment &arc, double path_length,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        std::size_t axis_count)
    {
        double max_qs = 1.0;
        for(std::size_t i = 2; i < axis_count; ++i) {
            max_qs = std::max(max_qs,
                              std::fabs((finish[i] - start[i]) / path_length));
        }
        const double max_qss = 1.0 / arc.radius;
        const double max_qsss = max_qss * max_qss;
        const double curvature_acceleration =
            0.5 * beta * std::min(limits.max_acceleration,
                                  limits.max_deceleration);
        const double jerk_budget = beta * limits.max_jerk;

        otg::Limits1D result{
            beta * limits.max_velocity / max_qs,
            0.5 * beta * limits.max_acceleration / max_qs,
            0.5 * beta * limits.max_deceleration / max_qs,
            jerk_budget / (3.0 * max_qs),
        };
        result.max_velocity = std::min(
            result.max_velocity,
            std::sqrt(curvature_acceleration / max_qss));
        result.max_velocity = std::min(
            result.max_velocity,
            std::cbrt(jerk_budget / (3.0 * max_qsss)));
        if(result.max_velocity > 1e-15) {
            const double cross_term_cap =
                jerk_budget / (9.0 * max_qss * result.max_velocity);
            result.max_acceleration =
                std::min(result.max_acceleration, cross_term_cap);
            result.max_deceleration =
                std::min(result.max_deceleration, cross_term_cap);
        }
        return result;
    }

    bool validate_circular_profiles(
        const otg::Limits1D &limits, double path_length,
        std::size_t axis_count,
        const std::array<double, MaxAxes> &start,
        const std::array<double, MaxAxes> &finish,
        const geom::ArcSegment &arc,
        const otg::Profile1D &along_profile,
        const std::array<otg::Profile1D, MaxAxes> &lateral_profiles,
        const std::array<double, MaxAxes> &initial_velocity,
        const std::array<double, MaxAxes> &initial_acceleration,
        std::int64_t total_duration) const
    {
        constexpr double kTolerance = 1e-9;
        const double path_tolerance =
            1e-12 * std::max(1.0, path_length);
        std::array<double, MaxAxes> previous_lateral_acceleration{};
        std::array<double, MaxAxes> previous_position{};
        std::array<double, MaxAxes> previous_velocity = initial_velocity;
        std::array<double, MaxAxes> previous_acceleration =
            initial_acceleration;
        otg::State1D previous_along{};
        bool terminal_clamp_entered = false;
        for(std::int64_t tick = 0; tick <= total_duration; ++tick) {
            const otg::State1D along = otg::sample(
                along_profile, rt::CycleTick::from_cycles(tick));
            if(along.position < -path_tolerance ||
               (terminal_clamp_entered &&
                along.position < path_length - path_tolerance)) {
                return false;
            }
            if(along.position >= path_length - path_tolerance) {
                terminal_clamp_entered = true;
            }
            const double sampled_path =
                std::min(std::max(along.position, 0.0), path_length);
            const geom::Vec3 point = geom::sample(arc, sampled_path);
            std::array<double, MaxAxes> q_s{};
            std::array<double, MaxAxes> q_ss{};
            std::array<double, MaxAxes> q_sss{};
            circular_derivatives(arc, sampled_path, path_length, start,
                                  finish, axis_count, q_s, q_ss, &q_sss);
            const double scalar_jerk =
                tick == 0 ? 0.0 : along.acceleration - previous_along.acceleration;

            for(std::size_t i = 0; i < axis_count; ++i) {
                const otg::State1D lateral = otg::sample(
                    lateral_profiles[i], rt::CycleTick::from_cycles(tick));
                const double lateral_jerk =
                    tick == 0 ? 0.0
                              : lateral.acceleration -
                                    previous_lateral_acceleration[i];
                const double velocity = q_s[i] * along.velocity +
                                        lateral.velocity;
                const double acceleration =
                    q_ss[i] * along.velocity * along.velocity +
                    q_s[i] * along.acceleration + lateral.acceleration;
                const double jerk =
                    q_sss[i] * along.velocity * along.velocity * along.velocity +
                    3.0 * q_ss[i] * along.velocity * along.acceleration +
                    q_s[i] * scalar_jerk + lateral_jerk;
                if(along.position <= path_length + path_tolerance) {
                    if(std::fabs(velocity) >
                           limits.max_velocity + kTolerance ||
                       acceleration >
                           limits.max_acceleration + kTolerance ||
                       acceleration <
                           -limits.max_deceleration - kTolerance ||
                       (tick > 0 &&
                        std::fabs(jerk) >
                            limits.max_jerk + kTolerance)) {
                        return false;
                    }
                }

                double position = 0.0;
                if(i == 0) {
                    position = point.x;
                } else if(i == 1) {
                    position = point.y;
                } else {
                    position = start[i] +
                               (finish[i] - start[i]) *
                                   (sampled_path / path_length);
                }
                position += lateral.position;
                if(tick > 0) {
                    const double actual_velocity =
                        position - previous_position[i];
                    const double actual_acceleration =
                        actual_velocity - previous_velocity[i];
                    const double actual_jerk =
                        actual_acceleration - previous_acceleration[i];
                    if(std::fabs(actual_velocity) >
                           limits.max_velocity + kTolerance ||
                       actual_acceleration >
                           limits.max_acceleration + kTolerance ||
                       actual_acceleration <
                           -limits.max_deceleration - kTolerance ||
                        std::fabs(actual_jerk) >
                            limits.max_jerk + kTolerance) {
                        return false;
                    }
                    previous_velocity[i] = actual_velocity;
                    previous_acceleration[i] = actual_acceleration;
                }
                previous_position[i] = position;
                previous_lateral_acceleration[i] = lateral.acceleration;
            }
            previous_along = along;
        }
        return true;
    }

    std::int64_t duration_ = 0;
    double r_tube_ = 0.0;
    std::array<double, MaxAxes> lateral_dir_{};
    std::array<double, MaxAxes> velocity_{};
    std::array<double, MaxAxes> acceleration_{};
    std::array<double, MaxAxes> output_velocity_{};
    std::array<double, MaxAxes> output_acceleration_{};
    std::array<otg::Profile1D, MaxAxes> lateral_profiles_{};
    otg::Profile1D lateral_profile_{};
    bool active_ = false;
    bool has_velocity_ = false;
    bool vector_mode_ = false;
    bool captured_vector_state_ = false;
};

} // namespace plcopen::core::axis
