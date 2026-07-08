#pragma once

// T24 quintic Hermite fast path (algorithm contract §4).
//
// Closed-form quintic from (p,v,a) to (p_aim,v_aim,0) over h cycles.
// Analytical extrema check: velocity (cubic roots), acceleration
// (quadratic roots), jerk (linear root) + endpoints. No sampling,
// no margins — the check IS the envelope proof.
//
// Accept → ~0.5µs per joint. Reject → fall back to full OTG (~8.6µs).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "otg/profile1d.h"
#include "rt/error.h"

namespace plcopen::core::stream
{

struct QuinticProfile
{
    double d[6] = {};
    std::int64_t h = 0;
};

namespace quintic_detail
{

inline int solve_quadratic(double a, double b, double c, double roots[3])
{
    if(std::fabs(a) < 1e-30) {
        if(std::fabs(b) < 1e-30) {
            return 0;
        }
        roots[0] = -c / b;
        return 1;
    }
    const double disc = b * b - 4.0 * a * c;
    if(disc < 0.0) {
        return 0;
    }
    const double sqrt_disc = std::sqrt(disc);
    roots[0] = (-b - sqrt_disc) / (2.0 * a);
    roots[1] = (-b + sqrt_disc) / (2.0 * a);
    return disc > 1e-30 ? 2 : 1;
}

inline int solve_cubic(double a3, double a2, double a1, double a0, double roots[3])
{
    if(std::fabs(a3) < 1e-30) {
        return solve_quadratic(a2, a1, a0, roots);
    }

    const double B = a2 / a3;
    const double C = a1 / a3;
    const double D = a0 / a3;
    const double p = C - B * B / 3.0;
    const double q = 2.0 * B * B * B / 27.0 - B * C / 3.0 + D;
    const double offset = -B / 3.0;

    if(std::fabs(p) < 1e-30) {
        roots[0] = -std::cbrt(q) + offset;
        return 1;
    }

    const double disc = -4.0 * p * p * p - 27.0 * q * q;

    if(disc > 1e-20) {
        const double m = 2.0 * std::sqrt(-p / 3.0);
        const double cos_arg =
            std::max(-1.0, std::min(1.0, 3.0 * q / (p * m)));
        const double theta = std::acos(cos_arg) / 3.0;
        constexpr double two_pi_3 = 2.09439510239319526;
        roots[0] = m * std::cos(theta) + offset;
        roots[1] = m * std::cos(theta - two_pi_3) + offset;
        roots[2] = m * std::cos(theta - 2.0 * two_pi_3) + offset;
        return 3;
    }

    const double half_q = q / 2.0;
    const double r = half_q * half_q + p * p * p / 27.0;
    const double scale =
        std::fabs(half_q * half_q) + std::fabs(p * p * p / 27.0) + 1e-30;
    if(std::fabs(r) < scale * 1e-8) {
        const double u = std::cbrt(-half_q);
        roots[0] = 2.0 * u + offset;
        roots[1] = -u + offset;
        return 2;
    }
    const double sqrt_r = std::sqrt(std::max(r, 0.0));
    roots[0] =
        std::cbrt(-half_q + sqrt_r) + std::cbrt(-half_q - sqrt_r) + offset;
    return 1;
}

} // namespace quintic_detail

inline bool solve_quintic(otg::State1D from, double target_position,
                          double target_velocity, std::int64_t h,
                          QuinticProfile &out)
{
    if(h <= 0) {
        return false;
    }
    const double hd = static_cast<double>(h);

    out.h = h;
    out.d[0] = from.position;
    out.d[1] = from.velocity * hd;
    out.d[2] = from.acceleration * hd * hd / 2.0;

    const double R0 = target_position - out.d[0] - out.d[1] - out.d[2];
    const double R1 = target_velocity * hd - out.d[1] - 2.0 * out.d[2];
    const double R2 = -2.0 * out.d[2];

    out.d[3] = (20.0 * R0 - 8.0 * R1 + R2) / 2.0;
    out.d[4] = (-30.0 * R0 + 14.0 * R1 - 2.0 * R2) / 2.0;
    out.d[5] = (12.0 * R0 - 6.0 * R1 + R2) / 2.0;
    return true;
}

inline otg::State1D sample_quintic(const QuinticProfile &q, std::int64_t tick)
{
    const double h = static_cast<double>(q.h);
    const double s = static_cast<double>(tick) / h;

    const double p = q.d[0] + s * (q.d[1] + s * (q.d[2] +
                     s * (q.d[3] + s * (q.d[4] + s * q.d[5]))));
    const double v = (q.d[1] + s * (2.0 * q.d[2] + s * (3.0 * q.d[3] +
                      s * (4.0 * q.d[4] + s * 5.0 * q.d[5])))) / h;
    const double a = (2.0 * q.d[2] + s * (6.0 * q.d[3] +
                      s * (12.0 * q.d[4] + s * 20.0 * q.d[5]))) / (h * h);
    return {p, v, a};
}

inline bool check_quintic_limits(const QuinticProfile &qp,
                                 const otg::Limits1D &limits)
{
    const double h = static_cast<double>(qp.h);
    if(h <= 0.0) {
        return false;
    }

    constexpr double tol = 1.0 + 1e-9;

    // --- Velocity extrema: q''(σ) = 0 roots (cubic) + endpoints ---
    {
        double roots[3];
        const int n = quintic_detail::solve_cubic(
            20.0 * qp.d[5], 12.0 * qp.d[4],
            6.0 * qp.d[3], 2.0 * qp.d[2], roots);
        for(int i = -2; i < n; ++i) {
            double sigma;
            if(i == -2) {
                sigma = 0.0;
            } else if(i == -1) {
                sigma = 1.0;
            } else {
                sigma = roots[i];
                if(sigma < 0.0 || sigma > 1.0) {
                    continue;
                }
            }
            const double v_norm =
                qp.d[1] +
                sigma * (2.0 * qp.d[2] +
                         sigma * (3.0 * qp.d[3] +
                                  sigma * (4.0 * qp.d[4] +
                                           sigma * 5.0 * qp.d[5])));
            if(std::fabs(v_norm / h) > limits.max_velocity * tol) {
                return false;
            }
        }
    }

    // --- Acceleration extrema: q'''(σ) = 0 roots (quadratic) + endpoints ---
    {
        double roots[3];
        const int n = quintic_detail::solve_quadratic(
            60.0 * qp.d[5], 24.0 * qp.d[4], 6.0 * qp.d[3], roots);
        for(int i = -2; i < n; ++i) {
            double sigma;
            if(i == -2) {
                sigma = 0.0;
            } else if(i == -1) {
                sigma = 1.0;
            } else {
                sigma = roots[i];
                if(sigma < 0.0 || sigma > 1.0) {
                    continue;
                }
            }
            const double a_norm =
                2.0 * qp.d[2] +
                sigma * (6.0 * qp.d[3] +
                         sigma * (12.0 * qp.d[4] + sigma * 20.0 * qp.d[5]));
            const double a_phys = a_norm / (h * h);
            if(a_phys > limits.max_acceleration * tol) {
                return false;
            }
            if(a_phys < -limits.max_deceleration * tol) {
                return false;
            }
        }
    }

    // --- Jerk extrema: q''''(σ) = 0 root (linear) + endpoints ---
    {
        double sigma_crit = -1.0;
        if(std::fabs(qp.d[5]) > 1e-30) {
            sigma_crit = -qp.d[4] / (5.0 * qp.d[5]);
        }
        for(int i = -2; i < 1; ++i) {
            double sigma;
            if(i == -2) {
                sigma = 0.0;
            } else if(i == -1) {
                sigma = 1.0;
            } else {
                sigma = sigma_crit;
                if(sigma < 0.0 || sigma > 1.0) {
                    continue;
                }
            }
            const double j_norm =
                6.0 * qp.d[3] +
                sigma * (24.0 * qp.d[4] + sigma * 60.0 * qp.d[5]);
            if(std::fabs(j_norm / (h * h * h)) > limits.max_jerk * tol) {
                return false;
            }
        }
    }
    return true;
}

} // namespace plcopen::core::stream
