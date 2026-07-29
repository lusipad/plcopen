// Y3 TOPP-RA executor integration tests (algorithm contract §3).
//
// Verifies the full pipeline: TOPP solver → quantized Profile1D →
// executor sampling, for both Layer 1 (accel-limited) and Layer 2
// (jerk-aware).
//
// Test strategy:
//   A. Straight line: TOPP time ≈ scalar OTG time (no curvature)
//   B. Arc: TOPP time > scalar baseline (centripetal cost)
//   C. Profile correctness: duration == ceil(T_topp), endpoints exact
//   D. Joint limits verified at sampled points
//   E. Executor sampling: sample_profiled_path returns correct positions
//   F. Quantization ceiling: profile is never shorter than TOPP time

#include <cmath>
#include <cstdio>

#include "exec/sampler.h"
#include "geom/geometry.h"
#include "plan/topp_executor.h"

namespace
{

using namespace plcopen::core;

bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

// A: straight line — TOPP profile matches scalar OTG closely.
int check_line_profile()
{
    const auto seg = geom::make_line({0.0, 0.0, 0.0}, {3.0, 4.0, 0.0});
    if(!seg) {
        return fail("line_profile: make_line failed");
    }
    const geom::PathSegment path = geom::as_path_segment(seg.value());

    const plan::ToppAxisLimits limits[3] = {
        {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};

    const auto result = plan::plan_topp_profiled(path, limits);
    if(!result) {
        return fail("line_profile: plan_topp_profiled failed");
    }

    const auto &r = result.value();
    if(r.quantized_cycles <= 0) {
        return fail("line_profile: zero duration");
    }

    const otg::State1D start =
        otg::sample(r.profile, rt::CycleTick::from_cycles(0));
    const otg::State1D finish =
        otg::sample(r.profile, rt::CycleTick::from_cycles(r.quantized_cycles));

    if(!near(start.position, 0.0, 1e-9)) {
        return fail("line_profile: start position not zero");
    }
    if(!near(finish.position, path.length(), 1e-6)) {
        std::printf("  finish.position=%.9f, expected=%.9f\n",
                    finish.position, path.length());
        return fail("line_profile: finish position mismatch");
    }
    if(!near(finish.velocity, 0.0, 1e-6)) {
        return fail("line_profile: finish velocity not zero");
    }

    if(r.profile.duration_cycles() != r.quantized_cycles) {
        return fail("line_profile: duration != quantized_cycles");
    }

    if(!r.curvature_verified) {
        return fail("line_profile: joint limits violated on a straight line");
    }

    std::printf("PASS line_profile (T_topp=%.3f, cycles=%lld)\n",
                r.topp_time, static_cast<long long>(r.quantized_cycles));
    return 0;
}

// B: arc — TOPP time should be larger than straight line of same length.
// Uses high velocity limits so centripetal acceleration constrains the arc
// (with low v_max the velocity constraint binds first and the arc's
// per-axis splitting actually makes it faster than a single-axis line).
int check_arc_slower()
{
    const auto arc = geom::make_arc(
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!arc) {
        return fail("arc_slower: make_arc failed");
    }
    const geom::PathSegment arc_path = geom::as_path_segment(arc.value());

    const auto line = geom::make_line(
        {0.0, 0.0, 0.0}, {arc_path.length(), 0.0, 0.0});
    if(!line) {
        return fail("arc_slower: make_line failed");
    }
    const geom::PathSegment line_path = geom::as_path_segment(line.value());

    const plan::ToppAxisLimits limits[3] = {
        {10.0, 2.0}, {10.0, 2.0}, {10.0, 2.0}};

    const auto arc_result = plan::plan_topp_profiled(arc_path, limits);
    const auto line_result = plan::plan_topp_profiled(line_path, limits);

    if(!arc_result || !line_result) {
        return fail("arc_slower: solve failed");
    }

    if(arc_result.value().topp_time <= line_result.value().topp_time) {
        std::printf("  arc_time=%.3f, line_time=%.3f\n",
                    arc_result.value().topp_time,
                    line_result.value().topp_time);
        return fail("arc_slower: arc not slower than line");
    }

    std::printf("PASS arc_slower (arc=%.3f, line=%.3f, ratio=%.2f)\n",
                arc_result.value().topp_time,
                line_result.value().topp_time,
                arc_result.value().topp_time / line_result.value().topp_time);
    return 0;
}

// C: quantization ceiling — profile duration >= TOPP time.
int check_quantization_ceiling()
{
    const auto seg = geom::make_line({0.0, 0.0, 0.0}, {2.0, 3.0, 0.0});
    if(!seg) {
        return fail("quantization_ceiling: make_line failed");
    }
    const geom::PathSegment path = geom::as_path_segment(seg.value());

    const plan::ToppAxisLimits limits[3] = {
        {0.5, 1.0}, {0.8, 1.5}, {1.0, 2.0}};

    const auto result = plan::plan_topp_profiled(path, limits);
    if(!result) {
        return fail("quantization_ceiling: solve failed");
    }

    const auto &r = result.value();
    if(static_cast<double>(r.quantized_cycles) < r.topp_time - 1e-9) {
        std::printf("  cycles=%lld < topp_time=%.3f\n",
                    static_cast<long long>(r.quantized_cycles), r.topp_time);
        return fail("quantization_ceiling: profile shorter than TOPP");
    }

    std::printf("PASS quantization_ceiling (T_topp=%.3f, cycles=%lld)\n",
                r.topp_time, static_cast<long long>(r.quantized_cycles));
    return 0;
}

// D: executor sampling — sample_profiled_path returns path positions.
int check_executor_sampling()
{
    const auto seg = geom::make_line({1.0, 2.0, 0.0}, {4.0, 6.0, 0.0});
    if(!seg) {
        return fail("executor_sampling: make_line failed");
    }
    const geom::PathSegment path = geom::as_path_segment(seg.value());

    const plan::ToppAxisLimits limits[3] = {
        {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};

    const auto result = plan::plan_topp_profiled(path, limits);
    if(!result) {
        return fail("executor_sampling: solve failed");
    }

    exec::CommittedPath<4> committed;
    committed.push(path);

    const geom::Vec3 start = exec::sample_profiled_path(
        committed, result.value().profile, rt::CycleTick::from_cycles(0));
    if(!near(start.x, 1.0, 1e-6) || !near(start.y, 2.0, 1e-6)) {
        std::printf("  start=(%.6f, %.6f)\n", start.x, start.y);
        return fail("executor_sampling: start position wrong");
    }

    const geom::Vec3 finish = exec::sample_profiled_path(
        committed, result.value().profile,
        rt::CycleTick::from_cycles(result.value().quantized_cycles));
    if(!near(finish.x, 4.0, 1e-6) || !near(finish.y, 6.0, 1e-6)) {
        std::printf("  finish=(%.6f, %.6f)\n", finish.x, finish.y);
        return fail("executor_sampling: finish position wrong");
    }

    std::printf("PASS executor_sampling\n");
    return 0;
}

// E: jerk-aware pipeline — Layer 2 produces valid profile.
int check_jerk_aware_pipeline()
{
    const auto seg = geom::make_line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    if(!seg) {
        return fail("jerk_aware: make_line failed");
    }
    const geom::PathSegment path = geom::as_path_segment(seg.value());

    const plan::ToppJerkAxisLimits limits[3] = {
        {1.0, 2.0, 5.0}, {1.0, 2.0, 5.0}, {1.0, 2.0, 5.0}};

    const auto result = plan::plan_topp_profiled_jerk(path, limits);
    if(!result) {
        return fail("jerk_aware: plan_topp_profiled_jerk failed");
    }

    const auto &r = result.value();
    if(r.quantized_cycles <= 0) {
        return fail("jerk_aware: zero duration");
    }

    const otg::State1D finish =
        otg::sample(r.profile, rt::CycleTick::from_cycles(r.quantized_cycles));
    if(!near(finish.position, path.length(), 1e-6)) {
        return fail("jerk_aware: finish position mismatch");
    }

    std::printf("PASS jerk_aware (T_topp=%.3f, cycles=%lld)\n",
                r.topp_time, static_cast<long long>(r.quantized_cycles));
    return 0;
}

// F: velocity profile stored correctly in ToppVelocityProfile.
int check_velocity_profile_storage()
{
    const auto seg = geom::make_line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    if(!seg) {
        return fail("velocity_profile: make_line failed");
    }
    const geom::PathSegment path = geom::as_path_segment(seg.value());

    const plan::ToppAxisLimits limits[3] = {
        {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};

    const auto result = plan::topp_executor_detail::solve_topp_profile_l1(
        path, limits, 64);
    if(!result) {
        return fail("velocity_profile: solve failed");
    }

    const auto &vp = result.value();
    if(vp.grid_points != 65) {
        std::printf("  grid_points=%d, expected 65\n", vp.grid_points);
        return fail("velocity_profile: wrong grid size");
    }

    if(!near(vp.sdot_at(0), 0.0, 1e-9)) {
        return fail("velocity_profile: start velocity not zero");
    }
    if(!near(vp.sdot_at(vp.grid_points - 1), 0.0, 1e-9)) {
        return fail("velocity_profile: end velocity not zero");
    }

    double peak = 0.0;
    for(int k = 0; k < vp.grid_points; ++k) {
        peak = std::max(peak, vp.sdot_at(k));
    }
    if(peak <= 0.0) {
        return fail("velocity_profile: no positive velocity");
    }
    if(peak > limits[0].max_velocity + 1e-9) {
        std::printf("  peak=%.9f, limit=%.9f\n", peak, limits[0].max_velocity);
        return fail("velocity_profile: peak exceeds limit");
    }

    double t_from_profile = 0.0;
    for(int k = 0; k < vp.grid_points - 1; ++k) {
        const double sum = vp.sdot_at(k) + vp.sdot_at(k + 1);
        if(sum > 1e-30) {
            t_from_profile += 2.0 * vp.ds / sum;
        }
    }
    if(!near(t_from_profile, vp.optimal_time, 1e-9)) {
        return fail("velocity_profile: recomputed time mismatch");
    }

    std::printf("PASS velocity_profile (peak_v=%.3f, T=%.3f)\n",
                peak, vp.optimal_time);
    return 0;
}

// G: arc curvature verification — line passes, arc with tight limits may warn.
int check_arc_curvature_verification()
{
    const auto arc = geom::make_arc(
        {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!arc) {
        return fail("arc_curvature: make_arc failed");
    }
    const geom::PathSegment path = geom::as_path_segment(arc.value());

    const plan::ToppAxisLimits limits[3] = {
        {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};

    const auto result = plan::plan_topp_profiled(path, limits);
    if(!result) {
        return fail("arc_curvature: solve failed");
    }

    std::printf("PASS arc_curvature (verified=%s, T_topp=%.3f, cycles=%lld)\n",
                result.value().curvature_verified ? "true" : "false",
                result.value().topp_time,
                static_cast<long long>(result.value().quantized_cycles));
    return 0;
}

int check_public_boundaries()
{
    plan::ToppVelocityProfile vp{};
    vp.grid_points = 3;
    vp.ds = 1.0;
    vp.sdot_sq[0] = 0.0;
    vp.sdot_sq[1] = 4.0;
    vp.sdot_sq[2] = 0.0;
    if(vp.sdot_at(-1) != 0.0 || vp.sdot_at(3) != 0.0 ||
       !near(vp.interpolate_sdot(0.5), 1.0, 1e-12) ||
       vp.interpolate_sdot(-1.0) != 0.0 || vp.interpolate_sdot(3.0) != 0.0) {
        return fail("public_boundaries: velocity profile sampling");
    }
    plan::ToppVelocityProfile invalid{};
    if(invalid.interpolate_sdot(0.0) != 0.0) {
        return fail("public_boundaries: invalid velocity profile");
    }

    const geom::PathSegment empty{};
    const plan::ToppAxisLimits limits[3] = {{1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}};
    const auto grid_line = geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if(!grid_line) {
        return fail("public_boundaries: grid fixture");
    }
    if(plan::topp_executor_detail::solve_topp_profile_l1(empty, limits, 1).error() !=
           rt::ErrorCode::invalid_argument ||
       plan::topp_executor_detail::solve_topp_profile_l1(
           geom::as_path_segment(grid_line.value()), limits,
           plan::ToppVelocityProfile::MaxGrid).error() !=
           rt::ErrorCode::invalid_argument) {
        return fail("public_boundaries: grid validation");
    }
    const auto zero = plan::plan_topp_profiled(empty, limits);
    if(!zero || zero.value().quantized_cycles != 0 || !zero.value().curvature_verified) {
        return fail("public_boundaries: empty path");
    }

    const otg::Limits1D scalar = plan::effective_scalar_limits(empty, limits);
    const plan::ToppJerkAxisLimits jerk_limits[3] = {
        {1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}};
    const otg::Limits1D jerk_scalar = plan::effective_scalar_limits(empty, jerk_limits);
    if(!near(scalar.max_velocity, 1.0, 1e-12) ||
       !near(scalar.max_acceleration, 1.0, 1e-12) ||
       !near(jerk_scalar.max_velocity, 1.0, 1e-12) ||
       !near(jerk_scalar.max_jerk, 10.0, 1e-12)) {
        return fail("public_boundaries: scalar limit fallback");
    }

    const auto line = geom::make_line({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    const auto profile = otg::plan_time_optimal(
        {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 2.0, 2.0, 2.0});
    if(!line || !profile) {
        return fail("public_boundaries: fixture planning");
    }
    const geom::PathSegment path = geom::as_path_segment(line.value());
    const plan::ToppAxisLimits tight[3] = {
        {0.01, 2.0}, {0.0, 2.0}, {0.0, 2.0}};
    if(!plan::verify_joint_limits(otg::Profile1D{}, path, tight) ||
       plan::verify_joint_limits(profile.value(), path, tight, 64, 1.0)) {
        return fail("public_boundaries: joint limit verification");
    }
    const plan::ToppAxisLimits zero_limits[3] = {};
    if(plan::solve_topp_ra(path, limits, 1).error() !=
           rt::ErrorCode::invalid_argument ||
       !plan::solve_topp_ra(empty, limits, 2) ||
       plan::solve_topp_ra(path, limits, 1024).error() !=
           rt::ErrorCode::invalid_argument ||
       plan::solve_topp_ra(path, zero_limits, 8).error() !=
           rt::ErrorCode::infeasible) {
        return fail("public_boundaries: direct TOPP validation");
    }
    const plan::ToppJerkAxisLimits tiny_jerk_limits[3] = {
        {1.0, 1.0, 1e-31}, {1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}};
    if(plan::solve_topp_ra_jerk(path, jerk_limits, 1).error() !=
           rt::ErrorCode::invalid_argument ||
       !plan::solve_topp_ra_jerk(empty, jerk_limits, 2) ||
       plan::solve_topp_ra_jerk(path, jerk_limits, 1024).error() !=
           rt::ErrorCode::invalid_argument ||
       plan::solve_topp_ra_jerk(path, tiny_jerk_limits, 8).error() !=
           rt::ErrorCode::infeasible) {
        return fail("public_boundaries: direct jerk TOPP validation");
    }
    return 0;
}

} // namespace

int main()
{
    int failures = 0;

    failures += check_line_profile();
    failures += check_arc_slower();
    failures += check_quantization_ceiling();
    failures += check_executor_sampling();
    failures += check_jerk_aware_pipeline();
    failures += check_velocity_profile_storage();
    failures += check_arc_curvature_verification();
    failures += check_public_boundaries();

    std::printf("\n=== %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
