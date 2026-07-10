// T24 quintic fast path oracle (algorithm contract §4).
//
// Acceptance criterion (stream-fastpath-design §5):
// "解析极值校验 vs 稠密采样 (1e4 点/剖面) 对照 fuzz — 任何被接受剖面
//  零越限（零容忍，这是包络证明的实测背书）"
//
// Tests:
// 1. Boundary condition correctness (6 conditions exact)
// 2. Analytical extrema vs 10000-point dense sampling (zero false accepts)
// 3. Known accept: well-behaved profiles within limits
// 4. Known reject: profiles that violate limits
// 5. Cubic root solver correctness (Cardano edge cases)
// 6. Horner evaluation matches naive polynomial

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "otg/profile1d.h"
#include "stream/quintic_fast_path.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

struct Lcg
{
    std::uint64_t state = 0x1234567890abcdef;
    double next()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state >> 11) / static_cast<double>(1ULL << 53);
    }
    double range(double lo, double hi)
    {
        return lo + next() * (hi - lo);
    }
};

int check_boundary_conditions()
{
    stream::QuinticProfile qp;
    otg::State1D from{1.5, 0.3, -0.1};
    const double p1 = 3.0;
    const double v1 = 0.5;
    const std::int64_t h = 20;

    if(!stream::solve_quintic(from, p1, v1, h, qp)) {
        return fail("boundary solve");
    }

    const otg::State1D s0 = stream::sample_quintic(qp, 0);
    if(!near(s0.position, from.position, 1e-12) ||
       !near(s0.velocity, from.velocity, 1e-12) ||
       !near(s0.acceleration, from.acceleration, 1e-12)) {
        std::printf("  σ=0: p=%.12f v=%.12f a=%.12f\n",
                    s0.position, s0.velocity, s0.acceleration);
        return fail("boundary at σ=0");
    }

    const otg::State1D s1 = stream::sample_quintic(qp, h);
    if(!near(s1.position, p1, 1e-10) ||
       !near(s1.velocity, v1, 1e-10) ||
       !near(s1.acceleration, 0.0, 1e-10)) {
        std::printf("  σ=1: p=%.12f(%.12f) v=%.12f(%.12f) a=%.12f(0)\n",
                    s1.position, p1, s1.velocity, v1, s1.acceleration);
        return fail("boundary at σ=1");
    }

    std::printf("  PASS boundary_conditions\n");
    return 0;
}

bool dense_sample_check(const stream::QuinticProfile &qp,
                        const otg::Limits1D &limits, int n_points)
{
    constexpr double tol = 1.0 + 1e-6;
    for(int i = 0; i <= n_points; ++i) {
        const double sigma =
            static_cast<double>(i) / static_cast<double>(n_points);
        const double h = static_cast<double>(qp.h);
        const double s = sigma;

        const double v_norm =
            qp.d[1] +
            s * (2.0 * qp.d[2] +
                 s * (3.0 * qp.d[3] +
                      s * (4.0 * qp.d[4] + s * 5.0 * qp.d[5])));
        const double v_phys = v_norm / h;
        if(std::fabs(v_phys) > limits.max_velocity * tol) {
            return false;
        }

        const double a_norm =
            2.0 * qp.d[2] +
            s * (6.0 * qp.d[3] +
                 s * (12.0 * qp.d[4] + s * 20.0 * qp.d[5]));
        const double a_phys = a_norm / (h * h);
        if(a_phys > limits.max_acceleration * tol) {
            return false;
        }
        if(a_phys < -limits.max_deceleration * tol) {
            return false;
        }

        const double j_norm =
            6.0 * qp.d[3] + s * (24.0 * qp.d[4] + s * 60.0 * qp.d[5]);
        const double j_phys = j_norm / (h * h * h);
        if(std::fabs(j_phys) > limits.max_jerk * tol) {
            return false;
        }
    }
    return true;
}

int check_analytical_vs_sampling_fuzz()
{
    Lcg rng;
    constexpr int N = 10000;
    constexpr int dense_points = 10000;
    int accepted = 0;
    int rejected = 0;
    int false_accept = 0;

    otg::Limits1D limits{2.0, 5.0, 5.0, 15.0};

    for(int i = 0; i < N; ++i) {
        const double p0 = rng.range(-5.0, 5.0);
        const double v0 = rng.range(-1.5, 1.5);
        const double a0 = rng.range(-3.0, 3.0);
        const double p1 = p0 + rng.range(-3.0, 3.0);
        const double v1 = rng.range(-1.5, 1.5);
        const std::int64_t h =
            static_cast<std::int64_t>(rng.range(3.0, 50.0));

        stream::QuinticProfile qp;
        if(!stream::solve_quintic({p0, v0, a0}, p1, v1, h, qp)) {
            continue;
        }

        const bool analytical_ok =
            stream::check_quintic_limits(qp, limits);

        if(analytical_ok) {
            ++accepted;
            if(!dense_sample_check(qp, limits, dense_points)) {
                ++false_accept;
                if(false_accept <= 3) {
                    std::printf("  FALSE ACCEPT i=%d p0=%.3f v0=%.3f a0=%.3f "
                                "p1=%.3f v1=%.3f h=%lld\n",
                                i, p0, v0, a0, p1, v1, (long long)h);
                }
            }
        } else {
            ++rejected;
        }
    }

    std::printf("  N=%d accepted=%d rejected=%d false_accept=%d "
                "hit_rate=%.1f%%\n",
                N, accepted, rejected, false_accept,
                100.0 * accepted / N);

    if(false_accept > 0) {
        return fail("analytical_vs_sampling (zero false accepts required)");
    }

    std::printf("  PASS analytical_vs_sampling_fuzz\n");
    return 0;
}

int check_asymmetric_limits_fuzz()
{
    Lcg rng;
    rng.state = 0xfedcba9876543210;
    constexpr int N = 5000;
    constexpr int dense_points = 10000;
    int false_accept = 0;
    int accepted = 0;

    otg::Limits1D limits{3.0, 8.0, 4.0, 20.0};

    for(int i = 0; i < N; ++i) {
        const double p0 = rng.range(-3.0, 3.0);
        const double v0 = rng.range(-2.0, 2.0);
        const double a0 = rng.range(-3.0, 3.0);
        const double p1 = p0 + rng.range(-4.0, 4.0);
        const double v1 = rng.range(-2.0, 2.0);
        const std::int64_t h =
            static_cast<std::int64_t>(rng.range(3.0, 40.0));

        stream::QuinticProfile qp;
        if(!stream::solve_quintic({p0, v0, a0}, p1, v1, h, qp)) {
            continue;
        }
        if(stream::check_quintic_limits(qp, limits)) {
            ++accepted;
            if(!dense_sample_check(qp, limits, dense_points)) {
                ++false_accept;
            }
        }
    }

    std::printf("  N=%d accepted=%d false_accept=%d\n", N, accepted,
                false_accept);
    if(false_accept > 0) {
        return fail("asymmetric_limits (zero false accepts)");
    }

    std::printf("  PASS asymmetric_limits_fuzz\n");
    return 0;
}

int check_known_accept()
{
    otg::Limits1D limits{5.0, 10.0, 10.0, 30.0};

    stream::QuinticProfile qp;
    stream::solve_quintic({0.0, 0.0, 0.0}, 0.1, 0.0, 20, qp);
    if(!stream::check_quintic_limits(qp, limits)) {
        return fail("known_accept: gentle move");
    }

    stream::solve_quintic({1.0, 0.5, 0.0}, 1.6, 0.5, 10, qp);
    if(!stream::check_quintic_limits(qp, limits)) {
        return fail("known_accept: cruise continuation");
    }

    stream::solve_quintic({0.0, 0.0, 0.0}, 0.0, 0.0, 5, qp);
    if(!stream::check_quintic_limits(qp, limits)) {
        return fail("known_accept: stationary");
    }

    std::printf("  PASS known_accept\n");
    return 0;
}

int check_known_reject()
{
    otg::Limits1D limits{1.0, 2.0, 2.0, 5.0};

    stream::QuinticProfile qp;

    stream::solve_quintic({0.0, 0.0, 0.0}, 10.0, 0.0, 3, qp);
    if(stream::check_quintic_limits(qp, limits)) {
        return fail("known_reject: huge distance in 3 cycles");
    }

    stream::solve_quintic({0.0, 0.0, 0.0}, 1.0, 0.0, 2, qp);
    if(stream::check_quintic_limits(qp, limits)) {
        return fail("known_reject: short horizon forcing high accel");
    }

    std::printf("  PASS known_reject\n");
    return 0;
}

int check_cubic_solver()
{
    double roots[3];

    int n = stream::quintic_detail::solve_cubic(1.0, 0.0, 0.0, 0.0, roots);
    if(n < 1 || !near(roots[0], 0.0, 1e-10)) {
        return fail("cubic: x³=0");
    }

    n = stream::quintic_detail::solve_cubic(1.0, 0.0, -3.0, 2.0, roots);
    bool found_1 = false;
    bool found_neg2 = false;
    for(int i = 0; i < n; ++i) {
        if(near(roots[i], 1.0, 1e-8)) {
            found_1 = true;
        }
        if(near(roots[i], -2.0, 1e-8)) {
            found_neg2 = true;
        }
    }
    if(!found_1 || !found_neg2) {
        return fail("cubic: x³-3x+2=0 roots at 1,-2");
    }

    n = stream::quintic_detail::solve_cubic(1.0, -6.0, 11.0, -6.0, roots);
    bool found[3] = {};
    for(int i = 0; i < n; ++i) {
        for(int j = 0; j < 3; ++j) {
            if(near(roots[i], static_cast<double>(j + 1), 1e-8)) {
                found[j] = true;
            }
        }
    }
    if(!found[0] || !found[1] || !found[2]) {
        return fail("cubic: (x-1)(x-2)(x-3)=0");
    }

    n = stream::quintic_detail::solve_cubic(1.0, 0.0, 0.0, -8.0, roots);
    if(n < 1 || !near(roots[0], 2.0, 1e-8)) {
        return fail("cubic: x³=8");
    }

    n = stream::quintic_detail::solve_cubic(0.0, 1.0, -5.0, 6.0, roots);
    if(n < 2) {
        return fail("cubic: degenerate quadratic count");
    }
    bool f2 = false, f3 = false;
    for(int i = 0; i < n; ++i) {
        if(near(roots[i], 2.0, 1e-8)) {
            f2 = true;
        }
        if(near(roots[i], 3.0, 1e-8)) {
            f3 = true;
        }
    }
    if(!f2 || !f3) {
        return fail("cubic: x²-5x+6=0");
    }

    n = stream::quintic_detail::solve_cubic(0.0, 0.0, 2.0, -6.0, roots);
    if(n != 1 || !near(roots[0], 3.0, 1e-10)) {
        return fail("cubic: degenerate linear root");
    }

    std::printf("  PASS cubic_solver\n");
    return 0;
}

int check_invalid_profile_inputs()
{
    stream::QuinticProfile qp;
    if(stream::solve_quintic({0.0, 0.0, 0.0}, 1.0, 0.0, 0, qp)) {
        return fail("quintic: zero horizon rejected");
    }
    const otg::Limits1D limits{1.0, 1.0, 1.0, 1.0};
    if(stream::check_quintic_limits(qp, limits)) {
        return fail("quintic: empty profile rejected");
    }
    return 0;
}

int check_horner_vs_naive()
{
    stream::QuinticProfile qp;
    stream::solve_quintic({1.0, 0.3, -0.05}, 2.5, 0.2, 15, qp);

    for(std::int64_t t = 0; t <= qp.h; ++t) {
        const otg::State1D horner = stream::sample_quintic(qp, t);

        const double s =
            static_cast<double>(t) / static_cast<double>(qp.h);
        double s_pow = 1.0;
        double p_naive = 0.0;
        for(int k = 0; k < 6; ++k) {
            p_naive += qp.d[k] * s_pow;
            s_pow *= s;
        }

        if(!near(horner.position, p_naive, 1e-10)) {
            std::printf("  t=%lld horner=%.12f naive=%.12f\n",
                        (long long)t, horner.position, p_naive);
            return fail("horner position");
        }
    }

    std::printf("  PASS horner_vs_naive\n");
    return 0;
}

int check_edge_h_equals_1()
{
    otg::Limits1D limits{10.0, 50.0, 50.0, 200.0};

    stream::QuinticProfile qp;
    stream::solve_quintic({0.0, 0.0, 0.0}, 0.001, 0.0, 1, qp);
    stream::check_quintic_limits(qp, limits);

    const otg::State1D s0 = stream::sample_quintic(qp, 0);
    const otg::State1D s1 = stream::sample_quintic(qp, 1);

    if(!near(s0.position, 0.0, 1e-12) || !near(s1.position, 0.001, 1e-10)) {
        return fail("h=1 boundary");
    }

    std::printf("  PASS edge_h_equals_1\n");
    return 0;
}

int check_short_horizon_fuzz()
{
    Lcg rng;
    rng.state = 0xabcdef0123456789;
    constexpr int N = 5000;
    constexpr int dense_points = 10000;
    int false_accept = 0;
    int accepted = 0;

    otg::Limits1D limits{2.0, 5.0, 5.0, 15.0};

    for(int i = 0; i < N; ++i) {
        const double p0 = rng.range(-2.0, 2.0);
        const double v0 = rng.range(-1.0, 1.0);
        const double a0 = rng.range(-2.0, 2.0);
        const double p1 = p0 + rng.range(-0.5, 0.5);
        const double v1 = rng.range(-1.0, 1.0);
        const std::int64_t h =
            static_cast<std::int64_t>(rng.range(1.0, 5.0));

        stream::QuinticProfile qp;
        if(!stream::solve_quintic({p0, v0, a0}, p1, v1, h, qp)) {
            continue;
        }
        if(stream::check_quintic_limits(qp, limits)) {
            ++accepted;
            if(!dense_sample_check(qp, limits, dense_points)) {
                ++false_accept;
            }
        }
    }

    std::printf("  N=%d accepted=%d false_accept=%d\n", N, accepted,
                false_accept);
    if(false_accept > 0) {
        return fail("short_horizon (zero false accepts)");
    }

    std::printf("  PASS short_horizon_fuzz\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("T24 quintic fast path oracle\n");
    int failures = 0;
    failures += check_boundary_conditions();
    failures += check_cubic_solver();
    failures += check_invalid_profile_inputs();
    failures += check_horner_vs_naive();
    failures += check_known_accept();
    failures += check_known_reject();
    failures += check_edge_h_equals_1();
    failures += check_analytical_vs_sampling_fuzz();
    failures += check_asymmetric_limits_fuzz();
    failures += check_short_horizon_fuzz();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
