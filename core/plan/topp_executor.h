#pragma once

// Y3 TOPP-RA executor integration (algorithm contract §3).
//
// Bridge between TOPP planning-domain solvers (return optimal_time) and
// the RT-domain executor (needs Profile1D with integer-tick segments).
//
// Pipeline:
//   1. TOPP solver → ToppTimedResult (optimal_time, velocity profile)
//   2. build_topp_timed_profile() → Profile1D via solve_fixed_time
//   3. verify_joint_limits() → check per-axis limits along the path
//   4. sample_profiled_path() → RT sampling (existing infrastructure)
//
// The scalar Profile1D distributes time uniformly (curvature-unaware).
// TOPP's value is the globally optimal duration — joint-limit safety
// is verified post-hoc at grid points, with fallback to scalar OTG.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "plan/topp.h"
#include "plan/topp_jerk.h"
#include "rt/error.h"

namespace plcopen::core::plan
{

struct ToppVelocityProfile
{
    static constexpr int MaxGrid = 1024;
    double sdot_sq[MaxGrid] = {};
    int grid_points = 0;
    double ds = 0.0;
    double path_length = 0.0;
    double optimal_time = 0.0;

    double sdot_at(int k) const
    {
        if(k < 0 || k >= grid_points) {
            return 0.0;
        }
        return std::sqrt(std::max(sdot_sq[k], 0.0));
    }

    double interpolate_sdot(double s) const
    {
        if(ds <= 0.0 || grid_points < 2) {
            return 0.0;
        }
        const double fk = s / ds;
        const int k = static_cast<int>(fk);
        if(k < 0) {
            return sdot_at(0);
        }
        if(k >= grid_points - 1) {
            return sdot_at(grid_points - 1);
        }
        const double t = fk - static_cast<double>(k);
        return (1.0 - t) * sdot_at(k) + t * sdot_at(k + 1);
    }
};

namespace topp_executor_detail
{

inline rt::Result<ToppVelocityProfile> solve_topp_profile_l1(
    const geom::PathSegment &path,
    const ToppAxisLimits limits[3],
    int grid_size)
{
    if(grid_size < 2) {
        return rt::Result<ToppVelocityProfile>::failure(
            rt::ErrorCode::invalid_argument);
    }
    const double L = path.length();
    if(L <= 0.0) {
        ToppVelocityProfile vp{};
        vp.grid_points = 1;
        return rt::Result<ToppVelocityProfile>::success(vp);
    }

    constexpr int MaxGrid = ToppVelocityProfile::MaxGrid;
    if(grid_size + 1 > MaxGrid) {
        return rt::Result<ToppVelocityProfile>::failure(
            rt::ErrorCode::invalid_argument);
    }

    const int N = grid_size;
    const double ds = L / static_cast<double>(N);

    double x_mvc[MaxGrid];
    geom::Vec3 qs[MaxGrid];
    geom::Vec3 qss[MaxGrid];

    for(int k = 0; k <= N; ++k) {
        const double s = ds * static_cast<double>(k);
        qs[k] = path.path_derivative(s);
        qss[k] = path.path_second_derivative(s);

        double mvc = topp_detail::velocity_limit_squared(qs[k], limits);
        const double cs[3] = {qs[k].x, qs[k].y, qs[k].z};
        const double css[3] = {qss[k].x, qss[k].y, qss[k].z};
        for(int i = 0; i < 3; ++i) {
            mvc = std::min(mvc,
                           topp_detail::acceleration_velocity_limit(
                               cs[i], css[i], limits[i].max_acceleration));
        }
        mvc = std::min(mvc,
                       topp_detail::cross_axis_velocity_limit(cs, css, limits));
        x_mvc[k] = mvc;
    }

    double x_back[MaxGrid];
    x_back[N] = 0.0;
    for(int k = N - 1; k >= 0; --k) {
        const double cs[3] = {qs[k].x, qs[k].y, qs[k].z};
        const double css[3] = {qss[k].x, qss[k].y, qss[k].z};
        double x_max = 1e30;
        for(int i = 0; i < 3; ++i) {
            x_max = std::min(x_max,
                             topp_detail::axis_backward_reach(
                                 cs[i], css[i], limits[i].max_acceleration,
                                 x_back[k + 1], ds));
        }
        x_max = std::min(x_max, x_mvc[k]);
        x_back[k] = std::max(x_max, 0.0);
    }

    double x_fwd[MaxGrid];
    x_fwd[0] = 0.0;
    for(int k = 0; k < N; ++k) {
        const double cs[3] = {qs[k].x, qs[k].y, qs[k].z};
        const double css[3] = {qss[k].x, qss[k].y, qss[k].z};
        double x_next = 1e30;
        for(int i = 0; i < 3; ++i) {
            x_next = std::min(x_next,
                              topp_detail::axis_forward_reach(
                                  cs[i], css[i], limits[i].max_acceleration,
                                  x_fwd[k], ds));
        }
        x_next = std::min(x_next, x_back[k + 1]);
        x_next = std::min(x_next, x_mvc[k + 1]);
        x_fwd[k + 1] = std::max(x_next, 0.0);
    }

    double total_time = 0.0;
    for(int k = 0; k < N; ++k) {
        const double sd_k = std::sqrt(std::max(x_fwd[k], 0.0));
        const double sd_k1 = std::sqrt(std::max(x_fwd[k + 1], 0.0));
        const double sum = sd_k + sd_k1;
        if(sum < 1e-30) {
            return rt::Result<ToppVelocityProfile>::failure(
                rt::ErrorCode::infeasible);
        }
        total_time += 2.0 * ds / sum;
    }

    ToppVelocityProfile vp{};
    for(int k = 0; k <= N; ++k) {
        vp.sdot_sq[k] = x_fwd[k];
    }
    vp.grid_points = N + 1;
    vp.ds = ds;
    vp.path_length = L;
    vp.optimal_time = total_time;

    return rt::Result<ToppVelocityProfile>::success(vp);
}

} // namespace topp_executor_detail

// Compute effective scalar limits from per-axis limits and path geometry.
// Uses the midpoint tangent (same method as the shadow oracle baseline).
inline otg::Limits1D effective_scalar_limits(
    const geom::PathSegment &path,
    const ToppAxisLimits limits[3])
{
    const geom::Vec3 q_s = path.path_derivative(path.length() * 0.5);
    const double qs[3] = {q_s.x, q_s.y, q_s.z};

    double v_eff = 1e30;
    double a_eff = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double a = std::fabs(qs[i]);
        if(a > 1e-15) {
            if(limits[i].max_velocity > 0.0) {
                v_eff = std::min(v_eff, limits[i].max_velocity / a);
            }
            a_eff = std::min(a_eff, limits[i].max_acceleration / a);
        }
    }
    if(v_eff > 1e29) {
        v_eff = 1.0;
    }
    if(a_eff > 1e29) {
        a_eff = 1.0;
    }

    return {v_eff, a_eff, a_eff, a_eff * a_eff / std::max(v_eff * 0.01, 1e-15)};
}

inline otg::Limits1D effective_scalar_limits(
    const geom::PathSegment &path,
    const ToppJerkAxisLimits limits[3])
{
    const geom::Vec3 q_s = path.path_derivative(path.length() * 0.5);
    const double qs[3] = {q_s.x, q_s.y, q_s.z};

    double v_eff = 1e30;
    double a_eff = 1e30;
    double j_eff = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double a = std::fabs(qs[i]);
        if(a > 1e-15) {
            if(limits[i].max_velocity > 0.0) {
                v_eff = std::min(v_eff, limits[i].max_velocity / a);
            }
            a_eff = std::min(a_eff, limits[i].max_acceleration / a);
            j_eff = std::min(j_eff, limits[i].max_jerk / a);
        }
    }
    if(v_eff > 1e29) {
        v_eff = 1.0;
    }
    if(a_eff > 1e29) {
        a_eff = 1.0;
    }
    if(j_eff > 1e29) {
        j_eff = a_eff * 10.0;
    }

    return {v_eff, a_eff, a_eff, j_eff};
}

// Verify that the Profile1D trajectory respects per-axis joint limits
// when mapped through the given path geometry.
// Returns true if all sampled points are within tolerance.
inline bool verify_joint_limits(
    const otg::Profile1D &profile,
    const geom::PathSegment &path,
    const ToppAxisLimits limits[3],
    int num_samples = 64,
    double tolerance = 1.01)
{
    const std::int64_t total = profile.duration_cycles();
    if(total <= 0) {
        return true;
    }

    const int step = std::max(
        static_cast<int>(total / static_cast<std::int64_t>(num_samples)), 1);

    for(std::int64_t t = 0; t <= total; t += step) {
        const otg::State1D st =
            otg::sample(profile, rt::CycleTick::from_cycles(t));
        const double s = st.position;
        const double sdot = st.velocity;

        if(s < 0.0 || s > path.length() * 1.001) {
            continue;
        }

        const geom::Vec3 q_s = path.path_derivative(
            std::min(s, path.length()));
        const double qs[3] = {q_s.x, q_s.y, q_s.z};

        for(int i = 0; i < 3; ++i) {
            const double vi = std::fabs(qs[i] * sdot);
            if(limits[i].max_velocity > 0.0 &&
               vi > limits[i].max_velocity * tolerance) {
                return false;
            }
        }
    }
    return true;
}

struct ToppProfileResult
{
    otg::Profile1D profile{};
    double topp_time = 0.0;
    std::int64_t quantized_cycles = 0;
    bool curvature_verified = false;
};

// Full TOPP executor pipeline: solve TOPP → quantize → build Profile1D.
//
// For acceleration-limited TOPP (Layer 1). Uses solve_fixed_time with
// ceil(T_topp) cycles as the prescribed duration. The resulting Profile1D
// traverses path_length in exactly that many cycles.
//
// If the TOPP time is shorter than the scalar OTG minimum (rare — happens
// when effective scalar limits are more conservative than the per-axis
// TOPP limits at non-midpoint sections), the scalar OTG minimum wins.
inline rt::Result<ToppProfileResult> plan_topp_profiled(
    const geom::PathSegment &path,
    const ToppAxisLimits limits[3],
    int grid_size = 128)
{
    const auto topp = topp_executor_detail::solve_topp_profile_l1(
        path, limits, grid_size);
    if(!topp) {
        return rt::Result<ToppProfileResult>::failure(topp.error());
    }

    const ToppVelocityProfile &vp = topp.value();
    if(vp.path_length <= 0.0) {
        ToppProfileResult r{};
        r.curvature_verified = true;
        return rt::Result<ToppProfileResult>::success(r);
    }

    const otg::Limits1D scalar = effective_scalar_limits(path, limits);

    const auto scalar_min = otg::plan_time_optimal(
        {0.0, 0.0, 0.0}, {vp.path_length, 0.0, 0.0}, scalar);
    const std::int64_t t_scalar_min =
        scalar_min ? scalar_min.value().duration_cycles() : 1;

    std::int64_t total_cycles =
        static_cast<std::int64_t>(std::ceil(vp.optimal_time));
    if(total_cycles < t_scalar_min) {
        total_cycles = t_scalar_min;
    }
    if(total_cycles < 1) {
        total_cycles = 1;
    }

    const auto profile = otg::solve_fixed_time(
        {0.0, 0.0, 0.0},
        {vp.path_length, 0.0, 0.0},
        scalar,
        total_cycles);
    if(!profile) {
        return rt::Result<ToppProfileResult>::failure(profile.error());
    }

    ToppProfileResult r{};
    r.profile = profile.value();
    r.topp_time = vp.optimal_time;
    r.quantized_cycles = total_cycles;
    r.curvature_verified = verify_joint_limits(
        r.profile, path, limits);

    return rt::Result<ToppProfileResult>::success(r);
}

// Full TOPP executor pipeline with jerk-aware solver (Layer 2).
inline rt::Result<ToppProfileResult> plan_topp_profiled_jerk(
    const geom::PathSegment &path,
    const ToppJerkAxisLimits limits[3],
    int grid_size = 128)
{
    const auto topp = solve_topp_ra_jerk(path, limits, grid_size);
    if(!topp) {
        return rt::Result<ToppProfileResult>::failure(topp.error());
    }

    const double T = topp.value().optimal_time;
    const double L = path.length();
    if(L <= 0.0) {
        ToppProfileResult r{};
        r.curvature_verified = true;
        return rt::Result<ToppProfileResult>::success(r);
    }

    const otg::Limits1D scalar = effective_scalar_limits(path, limits);

    const auto scalar_min = otg::plan_time_optimal(
        {0.0, 0.0, 0.0}, {L, 0.0, 0.0}, scalar);
    const std::int64_t t_scalar_min =
        scalar_min ? scalar_min.value().duration_cycles() : 1;

    std::int64_t total_cycles =
        static_cast<std::int64_t>(std::ceil(T));
    if(total_cycles < t_scalar_min) {
        total_cycles = t_scalar_min;
    }
    if(total_cycles < 1) {
        total_cycles = 1;
    }

    const auto profile = otg::solve_fixed_time(
        {0.0, 0.0, 0.0},
        {L, 0.0, 0.0},
        scalar,
        total_cycles);
    if(!profile) {
        return rt::Result<ToppProfileResult>::failure(profile.error());
    }

    ToppAxisLimits verify_limits[3];
    for(int i = 0; i < 3; ++i) {
        verify_limits[i] = {limits[i].max_velocity, limits[i].max_acceleration};
    }

    ToppProfileResult r{};
    r.profile = profile.value();
    r.topp_time = T;
    r.quantized_cycles = total_cycles;
    r.curvature_verified = verify_joint_limits(
        r.profile, path, verify_limits);

    return rt::Result<ToppProfileResult>::success(r);
}

} // namespace plcopen::core::plan
