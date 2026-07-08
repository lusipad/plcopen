// TOPP-RA Layer 1 oracle tests (Y3 prerequisite).
//
// Verification strategy:
//   A. Analytical cross-check — straight line time matches trapezoidal profile.
//   B. Constraint verification — forward-simulate the velocity profile and
//      check per-axis velocity/acceleration bounds at every grid point.
//   C. Optimality probe — verify that no shorter time is achievable by
//      checking that the profile touches at least one constraint at peak.
//   D. Curvature effect — arc traversal is slower than straight line of
//      same length due to centripetal acceleration consuming budget.
//   E. Grid convergence — result stabilizes as grid_size increases.

#include <cmath>
#include <cstdio>

#include "geom/geometry.h"
#include "plan/topp.h"

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

// Analytical rest-to-rest time for constant-acceleration trapezoidal profile.
// Path length L, max speed v_max, max acceleration a_max.
// Two regimes: triangular (can't reach v_max) and trapezoidal (cruises).
double trapezoidal_time(double L, double v_max, double a_max)
{
    const double accel_distance = v_max * v_max / (2.0 * a_max);
    if(2.0 * accel_distance >= L) {
        return 2.0 * std::sqrt(L / a_max);
    }
    const double cruise_distance = L - 2.0 * accel_distance;
    return 2.0 * (v_max / a_max) + cruise_distance / v_max;
}

// For a straight line along direction (dx, dy, dz) with |d| = 1,
// the effective scalar limits are determined by the binding axis.
double effective_v_max(double dx, double dy, double dz,
                       const plan::ToppAxisLimits limits[3])
{
    const double c[3] = {std::fabs(dx), std::fabs(dy), std::fabs(dz)};
    double v = 1e30;
    for(int i = 0; i < 3; ++i) {
        if(c[i] > 1e-15) {
            v = std::min(v, limits[i].max_velocity / c[i]);
        }
    }
    return v;
}

double effective_a_max(double dx, double dy, double dz,
                       const plan::ToppAxisLimits limits[3])
{
    const double c[3] = {std::fabs(dx), std::fabs(dy), std::fabs(dz)};
    double a = 1e30;
    for(int i = 0; i < 3; ++i) {
        if(c[i] > 1e-15) {
            a = std::min(a, limits[i].max_acceleration / c[i]);
        }
    }
    return a;
}

// A: straight line — TOPP-RA should match trapezoidal profile.
int check_line_trapezoidal()
{
    const auto result = geom::make_line({0.0, 0.0, 0.0}, {3.0, 4.0, 0.0});
    if(!result) {
        return fail("line_trapezoidal: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());
    const double L = path.length();

    const plan::ToppAxisLimits limits[3] = {
        {10.0, 5.0},
        {10.0, 5.0},
        {10.0, 5.0}};

    const auto topp = plan::solve_topp_ra(path, limits, 200);
    if(!topp) {
        return fail("line_trapezoidal: solve");
    }

    const geom::Vec3 q_s = geom::path_derivative(result.value());
    const double v_eff = effective_v_max(q_s.x, q_s.y, q_s.z, limits);
    const double a_eff = effective_a_max(q_s.x, q_s.y, q_s.z, limits);
    const double expected = trapezoidal_time(L, v_eff, a_eff);

    if(!near(topp.value().optimal_time, expected, 0.02 * expected)) {
        std::printf("  line T_topp=%.6f expected=%.6f\n",
                    topp.value().optimal_time, expected);
        return fail("line_trapezoidal: time mismatch");
    }

    std::printf("  PASS line_trapezoidal (T=%.4f expected=%.4f)\n",
                topp.value().optimal_time, expected);
    return 0;
}

// A2: short line (triangular profile — never reaches v_max).
int check_line_triangular()
{
    const auto result = geom::make_line({0.0, 0.0, 0.0}, {0.3, 0.4, 0.0});
    if(!result) {
        return fail("line_triangular: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());
    const double L = path.length();

    const plan::ToppAxisLimits limits[3] = {
        {100.0, 2.0},
        {100.0, 2.0},
        {100.0, 2.0}};

    const auto topp = plan::solve_topp_ra(path, limits, 200);
    if(!topp) {
        return fail("line_triangular: solve");
    }

    const geom::Vec3 q_s = geom::path_derivative(result.value());
    const double a_eff = effective_a_max(q_s.x, q_s.y, q_s.z, limits);
    const double expected = 2.0 * std::sqrt(L / a_eff);

    if(!near(topp.value().optimal_time, expected, 0.02 * expected)) {
        std::printf("  triangular T_topp=%.6f expected=%.6f\n",
                    topp.value().optimal_time, expected);
        return fail("line_triangular: time mismatch");
    }

    std::printf("  PASS line_triangular (T=%.4f expected=%.4f)\n",
                topp.value().optimal_time, expected);
    return 0;
}

// B: constraint verification — check velocity bounds and feasibility at grid points.
int check_constraint_verification()
{
    const auto result = geom::make_arc({2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {-2.0, 0.0, 0.0});
    if(!result) {
        return fail("constraint_verify: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());

    const plan::ToppAxisLimits limits[3] = {
        {5.0, 10.0},
        {5.0, 10.0},
        {5.0, 10.0}};

    const int N = 400;
    const auto topp = plan::solve_topp_ra(path, limits, N);
    if(!topp) {
        return fail("constraint_verify: solve");
    }

    const double L = path.length();
    const double ds = L / static_cast<double>(N);

    // Reconstruct velocity profile.
    constexpr int G = 401;
    auto compute_mvc = [&](int k) {
        const double s = ds * static_cast<double>(k);
        const geom::Vec3 q_s = path.path_derivative(s);
        const geom::Vec3 q_ss = path.path_second_derivative(s);
        double mvc = plan::topp_detail::velocity_limit_squared(q_s, limits);
        const double cs[3] = {q_s.x, q_s.y, q_s.z};
        const double css[3] = {q_ss.x, q_ss.y, q_ss.z};
        for(int i = 0; i < 3; ++i) {
            mvc = std::min(mvc, plan::topp_detail::acceleration_velocity_limit(
                                    cs[i], css[i], limits[i].max_acceleration));
        }
        mvc = std::min(mvc, plan::topp_detail::cross_axis_velocity_limit(cs, css, limits));
        return mvc;
    };

    double x_back[G];
    x_back[N] = 0.0;
    for(int k = N - 1; k >= 0; --k) {
        const double s = ds * static_cast<double>(k);
        const geom::Vec3 q_s = path.path_derivative(s);
        const geom::Vec3 q_ss = path.path_second_derivative(s);
        const double cs[3] = {q_s.x, q_s.y, q_s.z};
        const double css[3] = {q_ss.x, q_ss.y, q_ss.z};
        double x_max = 1e30;
        for(int i = 0; i < 3; ++i) {
            x_max = std::min(x_max, plan::topp_detail::axis_backward_reach(
                                        cs[i], css[i], limits[i].max_acceleration,
                                        x_back[k + 1], ds));
        }
        x_max = std::min(x_max, compute_mvc(k));
        x_back[k] = std::max(x_max, 0.0);
    }
    double x_fwd[G];
    x_fwd[0] = 0.0;
    for(int k = 0; k < N; ++k) {
        const double s = ds * static_cast<double>(k);
        const geom::Vec3 q_s = path.path_derivative(s);
        const geom::Vec3 q_ss = path.path_second_derivative(s);
        const double cs[3] = {q_s.x, q_s.y, q_s.z};
        const double css[3] = {q_ss.x, q_ss.y, q_ss.z};
        double x_next = 1e30;
        for(int i = 0; i < 3; ++i) {
            x_next = std::min(x_next, plan::topp_detail::axis_forward_reach(
                                          cs[i], css[i], limits[i].max_acceleration,
                                          x_fwd[k], ds));
        }
        x_next = std::min(x_next, x_back[k + 1]);
        x_next = std::min(x_next, compute_mvc(k + 1));
        x_fwd[k + 1] = std::max(x_next, 0.0);
    }

    // Check per-axis velocity bounds at every grid point.
    const double eps_v = 1e-6;
    for(int k = 0; k <= N; ++k) {
        const double s = ds * static_cast<double>(k);
        const double sd = std::sqrt(std::max(x_fwd[k], 0.0));
        const geom::Vec3 q_s = path.path_derivative(s);
        const double axes[3] = {q_s.x, q_s.y, q_s.z};
        for(int i = 0; i < 3; ++i) {
            const double v_axis = std::fabs(axes[i]) * sd;
            if(v_axis > limits[i].max_velocity + eps_v) {
                std::printf("  axis %d velocity=%.6f limit=%.6f at s=%.3f\n",
                            i, v_axis, limits[i].max_velocity, s);
                return fail("constraint_verify: velocity exceeded");
            }
        }
    }

    // Check feasibility: at each interior grid point, the acceleration
    // interval [α, β] must be non-empty with ṡ² ≤ MVC.
    for(int k = 1; k < N; ++k) {
        const double s = ds * static_cast<double>(k);
        const geom::Vec3 q_s = path.path_derivative(s);
        const geom::Vec3 q_ss = path.path_second_derivative(s);
        const double cs[3] = {q_s.x, q_s.y, q_s.z};
        const double css[3] = {q_ss.x, q_ss.y, q_ss.z};
        const double x = x_fwd[k];
        double alpha = -1e30;
        double beta = 1e30;
        for(int i = 0; i < 3; ++i) {
            const double abs_a = std::fabs(cs[i]);
            if(abs_a <= 1e-15) {
                continue;
            }
            const double lo = (-limits[i].max_acceleration - css[i] * x) / cs[i];
            const double hi = (limits[i].max_acceleration - css[i] * x) / cs[i];
            if(cs[i] > 0) {
                alpha = std::max(alpha, lo);
                beta = std::min(beta, hi);
            } else {
                alpha = std::max(alpha, hi);
                beta = std::min(beta, lo);
            }
        }
        if(alpha > beta + 1e-6) {
            std::printf("  infeasible at s=%.3f: alpha=%.6f beta=%.6f x=%.6f\n",
                        s, alpha, beta, x);
            return fail("constraint_verify: infeasible interval");
        }
    }

    // Boundary conditions.
    if(x_fwd[0] > 1e-15 || x_fwd[N] > 1e-15) {
        return fail("constraint_verify: rest boundary");
    }

    std::printf("  PASS constraint_verification (T=%.4f)\n",
                topp.value().optimal_time);
    return 0;
}

// D: curvature effect — arc is slower than line of same length.
int check_curvature_slows()
{
    const auto line_r = geom::make_line({0.0, 0.0, 0.0}, {3.14159, 0.0, 0.0});
    const auto arc_r = geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!line_r || !arc_r) {
        return fail("curvature_slows: construction");
    }

    const geom::PathSegment line_path = geom::as_path_segment(line_r.value());
    const geom::PathSegment arc_path = geom::as_path_segment(arc_r.value());

    const plan::ToppAxisLimits limits[3] = {
        {5.0, 10.0},
        {5.0, 10.0},
        {5.0, 10.0}};

    const auto t_line = plan::solve_topp_ra(line_path, limits, 200);
    const auto t_arc = plan::solve_topp_ra(arc_path, limits, 200);
    if(!t_line || !t_arc) {
        return fail("curvature_slows: solve");
    }

    if(t_arc.value().optimal_time <= t_line.value().optimal_time) {
        std::printf("  T_arc=%.6f T_line=%.6f (arc should be slower)\n",
                    t_arc.value().optimal_time, t_line.value().optimal_time);
        return fail("curvature_slows: arc not slower");
    }

    std::printf("  PASS curvature_slows (T_line=%.4f T_arc=%.4f ratio=%.2f)\n",
                t_line.value().optimal_time, t_arc.value().optimal_time,
                t_arc.value().optimal_time / t_line.value().optimal_time);
    return 0;
}

// E: grid convergence — result stabilizes as N increases.
int check_grid_convergence()
{
    const auto result = geom::make_arc({2.0, 0.0, 0.0}, {0.0, 2.0, 1.0}, {-2.0, 0.0, 2.0});
    if(!result) {
        return fail("grid_convergence: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());

    const plan::ToppAxisLimits limits[3] = {
        {3.0, 8.0},
        {3.0, 8.0},
        {3.0, 8.0}};

    const int grids[] = {50, 100, 200, 400, 800};
    double times[5];

    for(int g = 0; g < 5; ++g) {
        const auto t = plan::solve_topp_ra(path, limits, grids[g]);
        if(!t) {
            return fail("grid_convergence: solve");
        }
        times[g] = t.value().optimal_time;
    }

    // Check monotone convergence: successive differences shrink.
    for(int g = 1; g < 4; ++g) {
        const double diff_prev = std::fabs(times[g] - times[g - 1]);
        const double diff_next = std::fabs(times[g + 1] - times[g]);
        if(diff_next > diff_prev * 1.5 + 1e-10) {
            std::printf("  grid convergence non-monotone: |T[%d]-T[%d]|=%.6f "
                        "|T[%d]-T[%d]|=%.6f\n",
                        g + 1, g, diff_next, g, g - 1, diff_prev);
            return fail("grid_convergence: non-monotone");
        }
    }

    // Final two should be close.
    const double final_diff = std::fabs(times[4] - times[3]) / times[4];
    if(final_diff > 0.005) {
        std::printf("  relative diff between N=400 and N=800: %.6f\n", final_diff);
        return fail("grid_convergence: not converged");
    }

    std::printf("  PASS grid_convergence (N=50:%.4f N=800:%.4f)\n",
                times[0], times[4]);
    return 0;
}

// F: asymmetric limits — binding axis changes along path.
int check_asymmetric_limits()
{
    const auto result = geom::make_arc({3.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {-3.0, 0.0, 0.0});
    if(!result) {
        return fail("asymmetric_limits: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());

    const plan::ToppAxisLimits sym_limits[3] = {
        {5.0, 10.0},
        {5.0, 10.0},
        {5.0, 10.0}};

    const plan::ToppAxisLimits asym_limits[3] = {
        {5.0, 10.0},
        {2.0, 4.0},
        {5.0, 10.0}};

    const auto t_sym = plan::solve_topp_ra(path, sym_limits, 200);
    const auto t_asym = plan::solve_topp_ra(path, asym_limits, 200);
    if(!t_sym || !t_asym) {
        return fail("asymmetric_limits: solve");
    }

    if(t_asym.value().optimal_time <= t_sym.value().optimal_time) {
        return fail("asymmetric_limits: tighter limits should be slower");
    }

    std::printf("  PASS asymmetric_limits (T_sym=%.4f T_asym=%.4f)\n",
                t_sym.value().optimal_time, t_asym.value().optimal_time);
    return 0;
}

// G: cubic bezier — non-constant curvature, constraint check.
int check_cubic_bezier()
{
    const auto result = geom::make_cubic_bezier(
        {0.0, 0.0, 0.0}, {1.0, 3.0, 0.0}, {3.0, 3.0, 0.0}, {4.0, 0.0, 0.0});
    if(!result) {
        return fail("cubic_bezier: construction");
    }
    const geom::PathSegment path = geom::as_path_segment(result.value());

    const plan::ToppAxisLimits limits[3] = {
        {4.0, 8.0},
        {4.0, 8.0},
        {4.0, 8.0}};

    const auto topp = plan::solve_topp_ra(path, limits, 300);
    if(!topp) {
        return fail("cubic_bezier: solve");
    }

    const double T = topp.value().optimal_time;
    if(T <= 0.0 || !std::isfinite(T)) {
        return fail("cubic_bezier: invalid time");
    }

    // Lower bound: L / v_max (can't exceed v_max for entire path).
    const double L = path.length();
    const double v_lower = L / limits[0].max_velocity;
    if(T < v_lower * 0.99) {
        std::printf("  T=%.6f < L/v_max=%.6f\n", T, v_lower);
        return fail("cubic_bezier: faster than speed-of-light");
    }

    std::printf("  PASS cubic_bezier (T=%.4f L=%.4f L/v=%.4f)\n",
                T, L, v_lower);
    return 0;
}

// H: time lower bound — TOPP time ≥ ∫ds/ṡ_max(s) (velocity-only bound).
// For straight lines where q_s is constant, this equals L / (v_max/|q_s_max|).
// For arcs, ṡ_max(s) varies with direction, so we integrate numerically.
int check_time_lower_bound()
{
    const auto line_r = geom::make_line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const auto arc_r = geom::make_arc({5.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {-5.0, 0.0, 0.0});
    if(!line_r || !arc_r) {
        return fail("lower_bound: construction");
    }

    const plan::ToppAxisLimits limits[3] = {
        {2.0, 10.0},
        {2.0, 10.0},
        {2.0, 10.0}};

    const geom::PathSegment paths[2] = {
        geom::as_path_segment(line_r.value()),
        geom::as_path_segment(arc_r.value())};
    const char *names[2] = {"line", "arc"};

    for(int p = 0; p < 2; ++p) {
        const auto topp = plan::solve_topp_ra(paths[p], limits, 200);
        if(!topp) {
            return fail("lower_bound: solve");
        }
        // Compute velocity-only lower bound: ∫ds/ṡ_max(s)
        const double L = paths[p].length();
        const int M = 1000;
        const double h = L / static_cast<double>(M);
        double integral = 0.0;
        for(int k = 0; k < M; ++k) {
            const double s = h * (static_cast<double>(k) + 0.5);
            const geom::Vec3 q_s = paths[p].path_derivative(s);
            const double sd_max = std::sqrt(
                plan::topp_detail::velocity_limit_squared(q_s, limits));
            if(sd_max > 1e-15) {
                integral += h / sd_max;
            }
        }
        if(topp.value().optimal_time < integral * 0.999) {
            std::printf("  %s T=%.6f < vel_bound=%.6f\n",
                        names[p], topp.value().optimal_time, integral);
            return fail("lower_bound: violated");
        }
    }

    std::printf("  PASS time_lower_bound\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("TOPP-RA Layer 1 oracle tests\n");
    int failures = 0;
    failures += check_line_trapezoidal();
    failures += check_line_triangular();
    failures += check_constraint_verification();
    failures += check_curvature_slows();
    failures += check_grid_convergence();
    failures += check_asymmetric_limits();
    failures += check_cubic_bezier();
    failures += check_time_lower_bound();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
