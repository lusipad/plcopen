#pragma once

// TOPP-RA Layer 1 (acceleration-limited) path parameterization.
//
// Algorithm contract §3: TOPP-RA on scalar path parameter s, state = ṡ,
// constraints = per-axis velocity and acceleration limits.
//
// Given a path q(s) with known q_s = dq/ds and q_ss = d²q/ds², computes
// the time-optimal rest-to-rest traversal time subject to:
//   |q_s_i · ṡ| ≤ v_max_i   (velocity)
//   |q_ss_i · ṡ² + q_s_i · s̈| ≤ a_max_i   (acceleration, linear in s̈)
//
// Method: discretize path, backward reachability pass, forward integration.
// Planning-domain only; no RT constraints.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "geom/geometry.h"
#include "rt/error.h"

namespace plcopen::core::plan
{

struct ToppAxisLimits
{
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
};

struct ToppResult
{
    double optimal_time = 0.0;
};

namespace topp_detail
{

inline double velocity_limit_squared(geom::Vec3 q_s, const ToppAxisLimits limits[3])
{
    double x_max = 1e30;
    const double c[3] = {q_s.x, q_s.y, q_s.z};
    for(int i = 0; i < 3; ++i) {
        const double a = std::fabs(c[i]);
        if(a > 1e-15 && limits[i].max_velocity > 0.0) {
            const double bound = limits[i].max_velocity / a;
            x_max = std::min(x_max, bound * bound);
        }
    }
    return x_max;
}

inline double acceleration_velocity_limit(double q_s_i, double q_ss_i, double a_max_i)
{
    if(std::fabs(q_s_i) > 1e-15) {
        return 1e30;
    }
    if(std::fabs(q_ss_i) <= 1e-15) {
        return 1e30;
    }
    return a_max_i / std::fabs(q_ss_i);
}

// Cross-axis feasibility velocity limit.
// When two axes have different q_ss/q_s ratios, their acceleration bounds
// on s̈ diverge with increasing ṡ², eventually becoming incompatible.
// Returns the maximum ṡ² at which the intersection [α, β] is non-empty.
inline double cross_axis_velocity_limit(
    const double q_s[3], const double q_ss[3],
    const ToppAxisLimits limits[3])
{
    double x_max = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double abs_ai = std::fabs(q_s[i]);
        if(abs_ai <= 1e-15) {
            continue;
        }
        const double ci = q_ss[i] / q_s[i];
        for(int j = i + 1; j < 3; ++j) {
            const double abs_aj = std::fabs(q_s[j]);
            if(abs_aj <= 1e-15) {
                continue;
            }
            const double cj = q_ss[j] / q_s[j];
            const double dc = std::fabs(ci - cj);
            if(dc <= 1e-15) {
                continue;
            }
            const double R = limits[i].max_acceleration / abs_ai +
                             limits[j].max_acceleration / abs_aj;
            x_max = std::min(x_max, R / dc);
        }
    }
    return x_max;
}

// Per-axis backward reachable ṡ² from x_next over interval ds.
// Uses minimum forward acceleration (maximum deceleration):
//   u_lo = -a_max/|q_s| - (q_ss/q_s)·x
//   x_next = x + 2·u_lo·ds
// Solves for x.
inline double axis_backward_reach(double q_s_i, double q_ss_i, double a_max_i,
                                  double x_next, double ds)
{
    const double abs_a = std::fabs(q_s_i);
    if(abs_a <= 1e-15) {
        return 1e30;
    }
    const double c0 = -a_max_i / abs_a;
    const double c1 = -q_ss_i / q_s_i;
    const double denom = 1.0 + 2.0 * c1 * ds;
    if(denom <= 1e-15) {
        return 1e30;
    }
    const double result = (x_next - 2.0 * c0 * ds) / denom;
    return std::max(result, 0.0);
}

// Per-axis forward reachable ṡ² from x_k over interval ds.
// Uses maximum forward acceleration:
//   u_hi = a_max/|q_s| - (q_ss/q_s)·x
//   x_next = x + 2·u_hi·ds
inline double axis_forward_reach(double q_s_i, double q_ss_i, double a_max_i,
                                 double x_k, double ds)
{
    const double abs_a = std::fabs(q_s_i);
    if(abs_a <= 1e-15) {
        return 1e30;
    }
    const double d0 = a_max_i / abs_a;
    const double d1 = -q_ss_i / q_s_i;
    return x_k * (1.0 + 2.0 * d1 * ds) + 2.0 * d0 * ds;
}

} // namespace topp_detail

// Compute acceleration-limited TOPP-RA for a single path segment.
// Boundary conditions: rest-to-rest.
// limits: per-axis [x, y, z] velocity and acceleration limits.
// grid_size: number of intervals (grid points = grid_size + 1, max 1023).
inline rt::Result<ToppResult> solve_topp_ra(const geom::PathSegment &path,
                                            const ToppAxisLimits limits[3],
                                            int grid_size)
{
    if(grid_size < 2) {
        return rt::Result<ToppResult>::failure(rt::ErrorCode::invalid_argument);
    }
    const double L = path.length();
    if(L <= 0.0) {
        return rt::Result<ToppResult>::success(ToppResult{0.0});
    }

    constexpr int MaxGrid = 1024;
    if(grid_size + 1 > MaxGrid) {
        return rt::Result<ToppResult>::failure(rt::ErrorCode::invalid_argument);
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
        mvc = std::min(mvc, topp_detail::cross_axis_velocity_limit(cs, css, limits));
        x_mvc[k] = mvc;
    }

    // Backward pass: controllable velocity profile.
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

    // Forward pass: optimal velocity profile.
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

    // Time integration (trapezoidal rule in s).
    double total_time = 0.0;
    for(int k = 0; k < N; ++k) {
        const double sd_k = std::sqrt(std::max(x_fwd[k], 0.0));
        const double sd_k1 = std::sqrt(std::max(x_fwd[k + 1], 0.0));
        const double sum = sd_k + sd_k1;
        if(sum < 1e-30) {
            return rt::Result<ToppResult>::failure(rt::ErrorCode::infeasible);
        }
        total_time += 2.0 * ds / sum;
    }

    return rt::Result<ToppResult>::success(ToppResult{total_time});
}

} // namespace plcopen::core::plan
