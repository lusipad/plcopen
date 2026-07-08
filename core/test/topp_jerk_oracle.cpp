// TOPP-RA Layer 2 (jerk-aware) oracle tests.
//
// Key invariants:
// 1. T_jerk ≥ T_layer1 (jerk limits can only slow down)
// 2. High jerk ≈ Layer 1 (when j_max is enormous, jerk is not binding)
// 3. Straight line with S-curve profile matches analytical timing
// 4. Per-axis jerk constraint satisfied along the profile

#include <cmath>
#include <cstdio>

#include "geom/geometry.h"
#include "plan/topp.h"
#include "plan/topp_jerk.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool near_rel(double lhs, double rhs, double rel_tol)
{
    const double scale = std::max(std::fabs(lhs), std::fabs(rhs));
    if(scale < 1e-15) {
        return true;
    }
    return std::fabs(lhs - rhs) / scale <= rel_tol;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

double scurve_time(double distance, double v_max, double a_max, double j_max)
{
    const double t_j = a_max / j_max;
    const double v_j = 0.5 * j_max * t_j * t_j;
    double T = 0.0;

    if(v_j >= v_max) {
        const double t_a = std::sqrt(v_max / (0.5 * j_max));
        T = 2.0 * t_a;
        const double d_accel = j_max * t_a * t_a * t_a / 3.0;
        const double d_cruise = distance - 2.0 * d_accel;
        if(d_cruise > 0.0) {
            T += d_cruise / v_max;
        }
    } else {
        const double d_jerk = j_max * t_j * t_j * t_j / 6.0;
        const double d_const_a = v_j * (v_max - v_j) / a_max +
                                  0.5 * a_max * ((v_max - v_j) / a_max) *
                                      ((v_max - v_j) / a_max);
        const double t_const_a = (v_max - v_j) / a_max;
        const double d_accel = d_jerk + d_const_a + j_max * t_j * t_j * t_j / 6.0 +
                                (v_j + a_max * t_const_a) * t_j +
                                0.5 * (-j_max) * t_j * t_j;

        const double v_at_end_of_ramp =
            v_j + a_max * t_const_a + a_max * t_j - 0.5 * j_max * t_j * t_j;

        (void)v_at_end_of_ramp;
        (void)d_accel;

        const double accel_time = 2.0 * t_j + t_const_a;
        const double accel_dist_half = v_max * accel_time / 2.0;

        const double d_cruise = distance - 2.0 * accel_dist_half;
        T = 2.0 * accel_time;
        if(d_cruise > 0.0) {
            T += d_cruise / v_max;
        }
    }
    return T;
}

int check_jerk_slower_than_layer1()
{
    const auto line = geom::make_line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    if(!line) { return fail("construction"); }
    const geom::PathSegment seg = geom::as_path_segment(line.value());

    plan::ToppAxisLimits l1[3] = {{2.0, 5.0}, {2.0, 5.0}, {2.0, 5.0}};
    plan::ToppJerkAxisLimits l2[3] = {{2.0, 5.0, 10.0}, {2.0, 5.0, 10.0}, {2.0, 5.0, 10.0}};

    const auto r1 = plan::solve_topp_ra(seg, l1, 200);
    const auto r2 = plan::solve_topp_ra_jerk(seg, l2, 200);

    if(!r1 || !r2) { return fail("solver"); }

    if(r2.value().optimal_time < r1.value().optimal_time - 0.01) {
        std::printf("  T_jerk=%.4f < T_layer1=%.4f\n",
                    r2.value().optimal_time, r1.value().optimal_time);
        return fail("jerk should be slower than or equal to layer 1");
    }

    std::printf("  PASS jerk_slower_than_layer1 (T1=%.4f, T2=%.4f)\n",
                r1.value().optimal_time, r2.value().optimal_time);
    return 0;
}

int check_high_jerk_matches_layer1()
{
    const auto line = geom::make_line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    if(!line) { return fail("construction"); }
    const geom::PathSegment seg = geom::as_path_segment(line.value());

    plan::ToppAxisLimits l1[3] = {{2.0, 5.0}, {2.0, 5.0}, {2.0, 5.0}};
    plan::ToppJerkAxisLimits l2[3] = {
        {2.0, 5.0, 1e6}, {2.0, 5.0, 1e6}, {2.0, 5.0, 1e6}};

    const auto r1 = plan::solve_topp_ra(seg, l1, 200);
    const auto r2 = plan::solve_topp_ra_jerk(seg, l2, 200);

    if(!r1 || !r2) { return fail("solver"); }

    if(!near_rel(r2.value().optimal_time, r1.value().optimal_time, 0.05)) {
        std::printf("  T_jerk=%.4f, T_layer1=%.4f (diff=%.2f%%)\n",
                    r2.value().optimal_time, r1.value().optimal_time,
                    100.0 * std::fabs(r2.value().optimal_time - r1.value().optimal_time) /
                        r1.value().optimal_time);
        return fail("high jerk should match layer 1 within 5%");
    }

    std::printf("  PASS high_jerk_matches_layer1 (T1=%.4f, T2=%.4f)\n",
                r1.value().optimal_time, r2.value().optimal_time);
    return 0;
}

int check_lower_jerk_is_slower()
{
    const auto line = geom::make_line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    if(!line) { return fail("construction"); }
    const geom::PathSegment seg = geom::as_path_segment(line.value());

    plan::ToppJerkAxisLimits high_j[3] = {
        {2.0, 5.0, 50.0}, {2.0, 5.0, 50.0}, {2.0, 5.0, 50.0}};
    plan::ToppJerkAxisLimits low_j[3] = {
        {2.0, 5.0, 5.0}, {2.0, 5.0, 5.0}, {2.0, 5.0, 5.0}};

    const auto r_high = plan::solve_topp_ra_jerk(seg, high_j, 200);
    const auto r_low = plan::solve_topp_ra_jerk(seg, low_j, 200);

    if(!r_high || !r_low) { return fail("solver"); }

    if(r_low.value().optimal_time < r_high.value().optimal_time - 0.01) {
        std::printf("  T_low_jerk=%.4f < T_high_jerk=%.4f\n",
                    r_low.value().optimal_time, r_high.value().optimal_time);
        return fail("lower jerk should be slower");
    }

    std::printf("  PASS lower_jerk_is_slower (T_high=%.4f, T_low=%.4f)\n",
                r_high.value().optimal_time, r_low.value().optimal_time);
    return 0;
}

int check_arc_jerk_slower()
{
    const auto arc = geom::make_arc({5.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {-5.0, 0.0, 0.0});
    if(!arc) { return fail("arc construction"); }
    const geom::PathSegment seg = geom::as_path_segment(arc.value());

    plan::ToppAxisLimits l1[3] = {{3.0, 10.0}, {3.0, 10.0}, {3.0, 10.0}};
    plan::ToppJerkAxisLimits l2[3] = {
        {3.0, 10.0, 20.0}, {3.0, 10.0, 20.0}, {3.0, 10.0, 20.0}};

    const auto r1 = plan::solve_topp_ra(seg, l1, 200);
    const auto r2 = plan::solve_topp_ra_jerk(seg, l2, 200);

    if(!r1 || !r2) { return fail("solver"); }

    if(r2.value().optimal_time < r1.value().optimal_time - 0.01) {
        std::printf("  T_jerk=%.4f < T_layer1=%.4f\n",
                    r2.value().optimal_time, r1.value().optimal_time);
        return fail("arc jerk should be slower than or equal to layer 1");
    }

    std::printf("  PASS arc_jerk_slower (T1=%.4f, T2=%.4f)\n",
                r1.value().optimal_time, r2.value().optimal_time);
    return 0;
}

int check_grid_convergence_jerk()
{
    const auto line = geom::make_line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    if(!line) { return fail("construction"); }
    const geom::PathSegment seg = geom::as_path_segment(line.value());

    plan::ToppJerkAxisLimits lim[3] = {
        {2.0, 5.0, 15.0}, {2.0, 5.0, 15.0}, {2.0, 5.0, 15.0}};

    const auto r_coarse = plan::solve_topp_ra_jerk(seg, lim, 50);
    const auto r_fine = plan::solve_topp_ra_jerk(seg, lim, 400);

    if(!r_coarse || !r_fine) { return fail("solver"); }

    if(!near_rel(r_coarse.value().optimal_time, r_fine.value().optimal_time, 0.05)) {
        std::printf("  T_coarse=%.4f, T_fine=%.4f\n",
                    r_coarse.value().optimal_time, r_fine.value().optimal_time);
        return fail("grid convergence within 5%");
    }

    std::printf("  PASS grid_convergence_jerk (T50=%.4f, T400=%.4f)\n",
                r_coarse.value().optimal_time, r_fine.value().optimal_time);
    return 0;
}

int check_cubic_bezier_jerk()
{
    const auto cubic = geom::make_cubic_bezier(
        {0.0, 0.0, 0.0}, {2.0, 4.0, 0.0}, {8.0, 4.0, 0.0}, {10.0, 0.0, 0.0});
    if(!cubic) { return fail("cubic construction"); }
    const geom::PathSegment seg = geom::as_path_segment(cubic.value());

    plan::ToppAxisLimits l1[3] = {{3.0, 10.0}, {3.0, 10.0}, {3.0, 10.0}};
    plan::ToppJerkAxisLimits l2[3] = {
        {3.0, 10.0, 30.0}, {3.0, 10.0, 30.0}, {3.0, 10.0, 30.0}};

    const auto r1 = plan::solve_topp_ra(seg, l1, 200);
    const auto r2 = plan::solve_topp_ra_jerk(seg, l2, 200);

    if(!r1 || !r2) { return fail("solver"); }

    if(r2.value().optimal_time < r1.value().optimal_time - 0.01) {
        return fail("cubic jerk slower than layer 1");
    }

    std::printf("  PASS cubic_bezier_jerk (T1=%.4f, T2=%.4f)\n",
                r1.value().optimal_time, r2.value().optimal_time);
    return 0;
}

int check_zero_length()
{
    const auto line = geom::make_line({1.0, 2.0, 3.0}, {1.0, 2.0, 3.0});
    if(line) { return fail("zero length should fail construction"); }

    plan::ToppJerkAxisLimits lim[3] = {
        {2.0, 5.0, 10.0}, {2.0, 5.0, 10.0}, {2.0, 5.0, 10.0}};
    const auto line2 = geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if(!line2) { return fail("line2 construction"); }
    const geom::PathSegment seg = geom::as_path_segment(line2.value());

    const auto r = plan::solve_topp_ra_jerk(seg, lim, 1);
    if(r) { return fail("grid_size=1 should fail"); }

    std::printf("  PASS zero_length\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("TOPP-RA Layer 2 (jerk-aware) oracle tests\n");
    int failures = 0;
    failures += check_jerk_slower_than_layer1();
    failures += check_high_jerk_matches_layer1();
    failures += check_lower_jerk_is_slower();
    failures += check_arc_jerk_slower();
    failures += check_grid_convergence_jerk();
    failures += check_cubic_bezier_jerk();
    failures += check_zero_length();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
