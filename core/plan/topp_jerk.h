#pragma once

// TOPP-RA Layer 2 (jerk-aware) path parameterization.
//
// Given a path q(s) with known q_s, q_ss, q_sss:
//   |q_s_i · ṡ|                               ≤ v_max_i   (velocity)
//   |q_ss_i · ṡ² + q_s_i · s̈|                 ≤ a_max_i   (acceleration)
//   |q_sss_i · ṡ³ + 3·q_ss_i·ṡ·s̈ + q_s_i·s⃛| ≤ j_max_i   (jerk, linear in s⃛)
//
// Method: forward S-curve from start + backward S-curve from end (deceleration
// envelope on time-reversed path), pointwise minimum, trapezoidal time
// integration. Planning-domain only.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "geom/geometry.h"
#include "plan/topp.h"
#include "rt/error.h"

namespace plcopen::core::plan
{

struct ToppJerkAxisLimits
{
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
    double max_jerk = 0.0;
};

namespace topp_jerk_detail
{

struct Interval
{
    double lo = -1e30;
    double hi = 1e30;
    bool feasible() const { return lo <= hi + 1e-15; }
};

inline Interval acceleration_interval(
    const double q_s[3], const double q_ss[3],
    double x, const ToppJerkAxisLimits limits[3])
{
    Interval iv;
    for(int i = 0; i < 3; ++i) {
        const double abs_qs = std::fabs(q_s[i]);
        if(abs_qs <= 1e-15) {
            if(std::fabs(q_ss[i] * x) > limits[i].max_acceleration + 1e-10) {
                return {1.0, -1.0};
            }
            continue;
        }
        double lo_i = (-limits[i].max_acceleration - q_ss[i] * x) / q_s[i];
        double hi_i = (+limits[i].max_acceleration - q_ss[i] * x) / q_s[i];
        if(q_s[i] < 0.0) {
            std::swap(lo_i, hi_i);
        }
        iv.lo = std::max(iv.lo, lo_i);
        iv.hi = std::min(iv.hi, hi_i);
    }
    return iv;
}

inline Interval jerk_interval(
    const double q_s[3], const double q_ss[3], const double q_sss[3],
    double x, double a, const ToppJerkAxisLimits limits[3])
{
    const double v = std::sqrt(std::max(x, 0.0));
    Interval iv;
    for(int i = 0; i < 3; ++i) {
        const double centripetal = q_sss[i] * v * v * v + 3.0 * q_ss[i] * v * a;
        const double abs_qs = std::fabs(q_s[i]);
        if(abs_qs <= 1e-15) {
            if(std::fabs(centripetal) > limits[i].max_jerk + 1e-10) {
                return {1.0, -1.0};
            }
            continue;
        }
        double lo_i = (-limits[i].max_jerk - centripetal) / q_s[i];
        double hi_i = (+limits[i].max_jerk - centripetal) / q_s[i];
        if(q_s[i] < 0.0) {
            std::swap(lo_i, hi_i);
        }
        iv.lo = std::max(iv.lo, lo_i);
        iv.hi = std::min(iv.hi, hi_i);
    }
    return iv;
}

inline double effective_jerk_max(const double q_s[3], const ToppJerkAxisLimits limits[3])
{
    double j_eff = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double abs_qs = std::fabs(q_s[i]);
        if(abs_qs > 1e-15 && limits[i].max_jerk > 0.0) {
            j_eff = std::min(j_eff, limits[i].max_jerk / abs_qs);
        }
    }
    return j_eff;
}

inline double effective_accel_max(const double q_s[3], const ToppJerkAxisLimits limits[3])
{
    double a_eff = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double abs_qs = std::fabs(q_s[i]);
        if(abs_qs > 1e-15 && limits[i].max_acceleration > 0.0) {
            a_eff = std::min(a_eff, limits[i].max_acceleration / abs_qs);
        }
    }
    return a_eff;
}

struct StartupResult
{
    double x;
    double a;
};

// S-curve startup from rest: time-domain solution for the first grid step
// where da/ds = j/v is singular at v=0.
inline StartupResult scurve_startup(double ds, double j_eff, double a_eff)
{
    const double t_jerk = std::cbrt(6.0 * ds / j_eff);
    const double a_at_end = j_eff * t_jerk;

    if(a_at_end <= a_eff) {
        const double v_exit = 0.5 * j_eff * t_jerk * t_jerk;
        return {v_exit * v_exit, a_at_end};
    }

    const double t1 = a_eff / j_eff;
    const double v1 = 0.5 * j_eff * t1 * t1;
    const double ds_remain = ds - j_eff * t1 * t1 * t1 / 6.0;
    if(ds_remain <= 0.0) {
        return {v1 * v1, a_eff};
    }
    const double disc = v1 * v1 + 2.0 * a_eff * ds_remain;
    const double v_exit = std::sqrt(std::max(disc, 0.0));
    return {v_exit * v_exit, a_eff};
}

} // namespace topp_jerk_detail

inline rt::Result<ToppResult> solve_topp_ra_jerk(const geom::PathSegment &path,
                                                  const ToppJerkAxisLimits limits[3],
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
    double qs_arr[MaxGrid][3];
    double qss_arr[MaxGrid][3];
    double qsss_arr[MaxGrid][3];

    for(int k = 0; k <= N; ++k) {
        const double s = ds * static_cast<double>(k);
        const geom::Vec3 q_s = path.path_derivative(s);
        const geom::Vec3 q_ss = path.path_second_derivative(s);
        const geom::Vec3 q_sss = path.path_third_derivative(s);

        qs_arr[k][0] = q_s.x;
        qs_arr[k][1] = q_s.y;
        qs_arr[k][2] = q_s.z;
        qss_arr[k][0] = q_ss.x;
        qss_arr[k][1] = q_ss.y;
        qss_arr[k][2] = q_ss.z;
        qsss_arr[k][0] = q_sss.x;
        qsss_arr[k][1] = q_sss.y;
        qsss_arr[k][2] = q_sss.z;

        ToppAxisLimits l1[3];
        for(int i = 0; i < 3; ++i) {
            l1[i] = {limits[i].max_velocity, limits[i].max_acceleration};
        }
        double mvc = topp_detail::velocity_limit_squared(q_s, l1);
        const double cs[3] = {q_s.x, q_s.y, q_s.z};
        const double css[3] = {q_ss.x, q_ss.y, q_ss.z};
        for(int i = 0; i < 3; ++i) {
            mvc = std::min(mvc,
                           topp_detail::acceleration_velocity_limit(
                               cs[i], css[i], l1[i].max_acceleration));
        }
        mvc = std::min(mvc, topp_detail::cross_axis_velocity_limit(cs, css, l1));
        x_mvc[k] = mvc;
    }

    // Forward S-curve: accelerate from rest with max jerk, clamped to MVC.
    double x_fwd[MaxGrid];
    double a_fwd[MaxGrid];
    x_fwd[0] = 0.0;
    a_fwd[0] = 0.0;

    for(int k = 0; k < N; ++k) {
        const double v_k = std::sqrt(std::max(x_fwd[k], 0.0));
        double x_next, a_next;

        if(v_k < 1e-15) {
            const double j_eff =
                topp_jerk_detail::effective_jerk_max(qs_arr[k], limits);
            const double a_eff =
                topp_jerk_detail::effective_accel_max(qs_arr[k], limits);

            if(j_eff <= 1e-30 || a_eff <= 1e-30) {
                return rt::Result<ToppResult>::failure(rt::ErrorCode::infeasible);
            }

            const auto su = topp_jerk_detail::scurve_startup(ds, j_eff, a_eff);
            x_next = su.x;
            a_next = su.a;
        } else {
            x_next = x_fwd[k] + 2.0 * a_fwd[k] * ds;

            const auto ji = topp_jerk_detail::jerk_interval(
                qs_arr[k], qss_arr[k], qsss_arr[k],
                x_fwd[k], a_fwd[k], limits);
            const double j = ji.feasible() ? ji.hi : 0.0;
            a_next = a_fwd[k] + j * ds / v_k;
        }

        if(x_next > x_mvc[k + 1]) {
            x_next = x_mvc[k + 1];
            a_next = (x_next - x_fwd[k]) / (2.0 * ds);
        }

        const auto ai = topp_jerk_detail::acceleration_interval(
            qs_arr[k + 1], qss_arr[k + 1], std::max(x_next, 0.0), limits);
        if(ai.feasible()) {
            a_next = std::max(a_next, ai.lo);
            a_next = std::min(a_next, ai.hi);
        }

        x_fwd[k + 1] = std::max(x_next, 0.0);
        a_fwd[k + 1] = a_next;
    }

    // Deceleration envelope: forward S-curve from end on time-reversed path.
    // Reversed derivatives: q_s' = -q_s, q_ss' = q_ss, q_sss' = -q_sss.
    double x_dec[MaxGrid];
    double a_dec[MaxGrid];
    x_dec[N] = 0.0;
    a_dec[N] = 0.0;

    for(int k = N - 1; k >= 0; --k) {
        const double v_prev = std::sqrt(std::max(x_dec[k + 1], 0.0));
        double x_next, a_next;

        if(v_prev < 1e-15) {
            const double j_eff =
                topp_jerk_detail::effective_jerk_max(qs_arr[k + 1], limits);
            const double a_eff =
                topp_jerk_detail::effective_accel_max(qs_arr[k + 1], limits);

            if(j_eff <= 1e-30 || a_eff <= 1e-30) {
                return rt::Result<ToppResult>::failure(rt::ErrorCode::infeasible);
            }

            const auto su = topp_jerk_detail::scurve_startup(ds, j_eff, a_eff);
            x_next = su.x;
            a_next = su.a;
        } else {
            x_next = x_dec[k + 1] + 2.0 * a_dec[k + 1] * ds;

            const double qs_r[3] = {
                -qs_arr[k + 1][0], -qs_arr[k + 1][1], -qs_arr[k + 1][2]};
            const double qsss_r[3] = {
                -qsss_arr[k + 1][0], -qsss_arr[k + 1][1], -qsss_arr[k + 1][2]};
            const auto ji = topp_jerk_detail::jerk_interval(
                qs_r, qss_arr[k + 1], qsss_r,
                x_dec[k + 1], a_dec[k + 1], limits);
            const double j = ji.feasible() ? ji.hi : 0.0;
            a_next = a_dec[k + 1] + j * ds / v_prev;
        }

        if(x_next > x_mvc[k]) {
            x_next = x_mvc[k];
            a_next = (x_next - x_dec[k + 1]) / (2.0 * ds);
        }

        const double qs_out[3] = {
            -qs_arr[k][0], -qs_arr[k][1], -qs_arr[k][2]};
        const auto ai = topp_jerk_detail::acceleration_interval(
            qs_out, qss_arr[k], std::max(x_next, 0.0), limits);
        if(ai.feasible()) {
            a_next = std::max(a_next, ai.lo);
            a_next = std::min(a_next, ai.hi);
        }

        x_dec[k] = std::max(x_next, 0.0);
        a_dec[k] = a_next;
    }

    // Time integration using pointwise min of forward and backward profiles.
    double total_time = 0.0;
    for(int k = 0; k < N; ++k) {
        const double x_k = std::min(x_fwd[k], x_dec[k]);
        const double x_k1 = std::min(x_fwd[k + 1], x_dec[k + 1]);
        const double sd_k = std::sqrt(std::max(x_k, 0.0));
        const double sd_k1 = std::sqrt(std::max(x_k1, 0.0));
        const double sum = sd_k + sd_k1;
        if(sum < 1e-30) {
            return rt::Result<ToppResult>::failure(rt::ErrorCode::infeasible);
        }
        total_time += 2.0 * ds / sum;
    }

    return rt::Result<ToppResult>::success(ToppResult{total_time});
}

} // namespace plcopen::core::plan
