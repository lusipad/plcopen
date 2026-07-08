// TOPP-RA shadow oracle — algorithm contract §附注3.
//
// Quantifies timing gap between the trapezoid scan baseline (current
// planner's implicit model) and TOPP-RA path parameterization, per
// segment type. "数字定去留": these numbers decide whether TOPP-RA
// replaces the scan.
//
// Baseline: direction-adjusted trapezoid = rest-to-rest with effective
// v_eff/a_eff from the midpoint tangent. This represents the best a
// direction-aware but curvature-unaware scan can achieve.
//
// Metrics per scenario:
//   scan_gap  = (T_L1 - T_trap) / T_L1   positive = scan is optimistic
//   jerk_cost = (T_L2 - T_L1) / T_L1     jerk overhead
//
// Gate invariants:
//   1. Line: |scan_gap| < 3% (both are trapezoid profiles)
//   2. T_L2 ≥ T_L1 - ε (jerk only adds cost)
//   3. Curved: scan_gap > 0 expected (scan ignores centripetal limits)

#include <cmath>
#include <cstdio>

#include "geom/geometry.h"
#include "plan/topp.h"
#include "plan/topp_jerk.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

double trapezoid_time(double length, double v_max, double a_max)
{
    if(length <= 0.0 || v_max <= 0.0 || a_max <= 0.0) {
        return 0.0;
    }
    const double d_accel = v_max * v_max / (2.0 * a_max);
    if(2.0 * d_accel >= length) {
        return 2.0 * std::sqrt(length / a_max);
    }
    const double t_accel = v_max / a_max;
    const double d_cruise = length - 2.0 * d_accel;
    return 2.0 * t_accel + d_cruise / v_max;
}

void effective_limits(const geom::PathSegment &path,
                      const plan::ToppAxisLimits limits[3],
                      double &v_eff, double &a_eff)
{
    const geom::Vec3 q_s = path.path_derivative(path.length() * 0.5);
    const double qs[3] = {q_s.x, q_s.y, q_s.z};
    v_eff = 1e30;
    a_eff = 1e30;
    for(int i = 0; i < 3; ++i) {
        const double abs_qs = std::fabs(qs[i]);
        if(abs_qs > 1e-15) {
            v_eff = std::min(v_eff, limits[i].max_velocity / abs_qs);
            a_eff = std::min(a_eff, limits[i].max_acceleration / abs_qs);
        }
    }
}

struct ShadowResult
{
    const char *label;
    double length;
    double T_trap;
    double T_L1;
    double T_L2;
    bool solver_ok;
};

ShadowResult evaluate(const char *label,
                       const geom::PathSegment &path,
                       const plan::ToppAxisLimits l1[3],
                       const plan::ToppJerkAxisLimits l2[3],
                       int grid)
{
    ShadowResult r{label, path.length(), 0.0, 0.0, 0.0, false};

    double v_eff = 0.0, a_eff = 0.0;
    effective_limits(path, l1, v_eff, a_eff);
    r.T_trap = trapezoid_time(path.length(), v_eff, a_eff);

    const auto r1 = plan::solve_topp_ra(path, l1, grid);
    if(!r1) {
        return r;
    }
    r.T_L1 = r1.value().optimal_time;

    const auto r2 = plan::solve_topp_ra_jerk(path, l2, grid);
    if(!r2) {
        return r;
    }
    r.T_L2 = r2.value().optimal_time;
    r.solver_ok = true;
    return r;
}

void print_row(const ShadowResult &r)
{
    if(!r.solver_ok) {
        std::printf("  %-22s L=%6.2f  SOLVER FAILED\n", r.label, r.length);
        return;
    }
    const double scan_gap =
        r.T_L1 > 0 ? (r.T_L1 - r.T_trap) / r.T_L1 * 100.0 : 0.0;
    const double jerk_cost =
        r.T_L1 > 0 ? (r.T_L2 - r.T_L1) / r.T_L1 * 100.0 : 0.0;
    std::printf("  %-22s L=%6.2f  Ttrap=%6.3f  TL1=%6.3f  TL2=%6.3f  "
                "gap=%+6.1f%%  jerk=%+5.1f%%\n",
                r.label, r.length, r.T_trap, r.T_L1, r.T_L2,
                scan_gap, jerk_cost);
}

int check_shadow_oracle()
{
    plan::ToppAxisLimits l1[3] = {{3.0, 10.0}, {3.0, 10.0}, {3.0, 10.0}};
    plan::ToppJerkAxisLimits l2[3] = {
        {3.0, 10.0, 30.0}, {3.0, 10.0, 30.0}, {3.0, 10.0, 30.0}};
    constexpr int grid = 200;
    constexpr int MaxScenarios = 10;

    ShadowResult results[MaxScenarios];
    int n = 0;
    int line_count = 0;
    int failures = 0;

    // --- Lines (curvature = 0, scan should match TOPP L1) ---
    {
        const auto seg_r = geom::make_line({0, 0, 0}, {5, 0, 0});
        if(!seg_r) { return fail("line_5"); }
        results[n++] = evaluate("line_5_x",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
        ++line_count;
    }
    {
        const auto seg_r = geom::make_line({0, 0, 0}, {30, 0, 0});
        if(!seg_r) { return fail("line_30"); }
        results[n++] = evaluate("line_30_x",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
        ++line_count;
    }
    {
        const auto seg_r = geom::make_line({0, 0, 0}, {6, 8, 0});
        if(!seg_r) { return fail("line_diag"); }
        results[n++] = evaluate("line_10_diag",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
        ++line_count;
    }

    // --- Arcs (centripetal acceleration limits speed) ---
    {
        const auto seg_r =
            geom::make_arc({5, 0, 0}, {0, 5, 0}, {-5, 0, 0});
        if(!seg_r) { return fail("arc_R5"); }
        results[n++] = evaluate("arc_R5_180",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
    }
    {
        const auto seg_r =
            geom::make_arc({2, 0, 0}, {0, 2, 0}, {-2, 0, 0});
        if(!seg_r) { return fail("arc_R2"); }
        results[n++] = evaluate("arc_R2_180",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
    }
    {
        const auto seg_r =
            geom::make_arc({0.5, 0, 0}, {0, 0.5, 0}, {-0.5, 0, 0});
        if(!seg_r) { return fail("arc_R05"); }
        results[n++] = evaluate("arc_R0.5_180",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
    }

    // --- Cubic Bezier (curvature varies along path) ---
    {
        const auto seg_r = geom::make_cubic_bezier(
            {0, 0, 0}, {2, 4, 0}, {8, 4, 0}, {10, 0, 0});
        if(!seg_r) { return fail("cubic_arch"); }
        results[n++] = evaluate("cubic_arch",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
    }
    {
        const auto seg_r = geom::make_cubic_bezier(
            {0, 0, 0}, {0, 5, 0}, {5, 0, 0}, {5, 5, 0});
        if(!seg_r) { return fail("cubic_S"); }
        results[n++] = evaluate("cubic_S",
                                geom::as_path_segment(seg_r.value()), l1, l2, grid);
    }

    // --- Asymmetric limits (direction matters) ---
    {
        plan::ToppAxisLimits l1a[3] = {{5.0, 15.0}, {2.0, 8.0}, {3.0, 10.0}};
        plan::ToppJerkAxisLimits l2a[3] = {
            {5.0, 15.0, 40.0}, {2.0, 8.0, 20.0}, {3.0, 10.0, 30.0}};
        const auto seg_r = geom::make_line({0, 0, 0}, {6, 8, 0});
        if(!seg_r) { return fail("line_asym"); }
        results[n++] = evaluate("line_diag_asym",
                                geom::as_path_segment(seg_r.value()), l1a, l2a, grid);
    }

    // --- Print table ---
    std::printf("--- TOPP-RA shadow oracle ---\n");
    std::printf("  Default limits: v=3, a=10, j=30 per axis; grid=%d\n", grid);
    std::printf("  scan_gap = (TL1-Ttrap)/TL1: +%% = scan optimistic (wrong)\n");
    std::printf("  jerk_cost = (TL2-TL1)/TL1: +%% = jerk overhead\n\n");
    for(int i = 0; i < n; ++i) {
        print_row(results[i]);
    }

    // --- Gate 1: line scan_gap < 3% ---
    for(int i = 0; i < line_count; ++i) {
        if(!results[i].solver_ok) {
            ++failures;
            failures += fail("line solver");
            continue;
        }
        const double gap =
            std::fabs(results[i].T_L1 - results[i].T_trap) / results[i].T_L1;
        if(gap > 0.03) {
            std::printf("  line scan_gap=%.1f%% > 3%%\n", gap * 100.0);
            failures += fail("line scan gap");
        }
    }

    // --- Gate 2: T_L2 ≥ T_L1 for all ---
    for(int i = 0; i < n; ++i) {
        if(!results[i].solver_ok) {
            continue;
        }
        if(results[i].T_L2 < results[i].T_L1 - 0.01) {
            std::printf("  %s: T_L2=%.4f < T_L1=%.4f\n",
                        results[i].label, results[i].T_L2, results[i].T_L1);
            failures += fail("jerk faster than accel");
        }
    }

    // --- Gate 3: all solvers succeed ---
    for(int i = 0; i < n; ++i) {
        if(!results[i].solver_ok) {
            failures += fail("solver failure");
        }
    }

    if(failures == 0) {
        std::printf("\n  PASS shadow_oracle (%d scenarios)\n", n);
    }
    return failures;
}

} // namespace

int main()
{
    std::printf("TOPP-RA shadow oracle\n");
    int failures = check_shadow_oracle();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
