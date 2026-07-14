#include <cmath>
#include <cstdio>

#include "exec/sampler.h"
#include "exec/sync.h"
#include "geom/geometry.h"
#include "plan/path.h"

namespace
{

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_line_geometry()
{
    using namespace plcopen::core;
    const rt::Result<geom::LineSegment> line = geom::make_line({0.0, 0.0, 0.0}, {3.0, 4.0, 0.0});
    if(!line || !near(line.value().length, 5.0, 1e-12)) {
        return fail("line length");
    }
    const geom::Vec3 mid = geom::sample(line.value(), 2.5);
    if(!near(mid.x, 1.5, 1e-12) || !near(mid.y, 2.0, 1e-12)) {
        return fail("line sample");
    }
    if(geom::make_line({1.0, 1.0, 1.0}, {1.0, 1.0, 1.0})) {
        return fail("zero line rejected");
    }
    if(geom::norm(geom::normalize({})) != 0.0 ||
       geom::make_line({0.0, 0.0, 0.0}, {NAN, 0.0, 0.0}).error() !=
           rt::ErrorCode::invalid_argument) {
        return fail("line non-finite boundary");
    }
    return 0;
}

int check_arc_geometry()
{
    using namespace plcopen::core;
    const rt::Result<geom::ArcSegment> arc =
        geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 0.5}, {-1.0, 0.0, 1.0});
    if(!arc || !near(arc.value().radius, 1.0, 1e-12) ||
       !near(arc.value().length, 3.14159265358979323846, 1e-12)) {
        return fail("arc length");
    }
    const geom::Vec3 mid = geom::sample(arc.value(), arc.value().length * 0.5);
    if(!near(mid.x, 0.0, 1e-12) || !near(mid.y, 1.0, 1e-12) ||
       !near(mid.z, 0.5, 1e-12)) {
        return fail("arc sample");
    }
    if(geom::make_arc({0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 2.0, 0.0})) {
        return fail("collinear arc rejected");
    }
    if(geom::make_arc({NAN, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 0.0, 0.0}) ||
       geom::make_arc({1e308, 0.0, 0.0}, {0.0, 1e308, 0.0},
                      {-1e308, 0.0, 0.0})) {
        return fail("non-finite arc rejected");
    }

    const rt::Result<geom::ArcSegment> clockwise =
        geom::make_arc({1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!clockwise || geom::tangent(clockwise.value(), 0.0).y >= 0.0) {
        return fail("clockwise arc tangent direction");
    }

    geom::ArcLengthTable<8> table;
    if(table.build(geom::as_path_segment(arc.value())) != rt::ErrorCode::ok ||
       !near(table.parameter_at_length(arc.value().length * 0.25), 0.25, 1e-12)) {
        return fail("arc length table");
    }
    return 0;
}

int check_spline_and_blending()
{
    using namespace plcopen::core;
    const rt::Result<geom::CubicBezierSegment> spline = geom::make_cubic_bezier(
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 1.0, 0.0});
    if(!spline || spline.value().length <= 2.0) {
        return fail("cubic bezier length");
    }
    const geom::Vec3 midpoint = geom::sample(spline.value(), spline.value().length * 0.5);
    if(midpoint.x <= 0.9 || midpoint.x >= 1.1 || midpoint.y <= 0.4 || midpoint.y >= 0.6) {
        return fail("cubic bezier sample");
    }

    const geom::LineSegment before =
        geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).value();
    const geom::LineSegment after =
        geom::make_line({1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}).value();
    const plan::BlendDecision blend =
        plan::decide_blend(geom::as_path_segment(before), geom::as_path_segment(after), 0.1);
    if(!blend.enabled || blend.curve.kind != geom::SegmentKind::quintic_blend ||
       blend.curve.quintic.max_deviation > 0.1 ||
       blend.curve.quintic.max_deviation < 0.08) {
        return fail("quintic blend curve deviation and utilization");
    }
    const geom::Vec3 start = blend.curve.start();
    const geom::Vec3 finish = blend.curve.finish();
    if(!near(start.y, 0.0, 1e-12) || !near(finish.x, 1.0, 1e-12)) {
        return fail("blend endpoints stay on source segments");
    }
    // C2 against the straight lines: tangent along the lines and near-zero
    // curvature at both junctions.
    const geom::Vec3 tangent_in = blend.curve.tangent(0.0);
    const geom::Vec3 tangent_out = blend.curve.tangent(blend.curve.length());
    if(!near(tangent_in.x, 1.0, 1e-9) || !near(tangent_out.y, 1.0, 1e-9)) {
        return fail("blend junction tangents");
    }
    const geom::Vec3 d2_start = geom::quintic_second_derivative(blend.curve.quintic, 0.0);
    const geom::Vec3 d2_finish = geom::quintic_second_derivative(blend.curve.quintic, 1.0);
    if(geom::norm(d2_start) > 1e-9 || geom::norm(d2_finish) > 1e-9) {
        return fail("blend junction curvature zero");
    }
    if(geom::make_cubic_bezier({}, {}, {}, {}).error() != rt::ErrorCode::invalid_argument ||
       geom::make_cubic_bezier({}, {NAN, 0.0, 0.0}, {}, {}).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quadratic_blend({}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}, 0.0).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quadratic_blend({}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}, NAN).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quadratic_blend({}, {NAN, 0.0, 0.0}, {1.0, 0.0, 0.0}, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quadratic_blend({}, {}, {}, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quadratic_blend({}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}, 0.1).error() !=
           rt::ErrorCode::out_of_range ||
       geom::make_quintic_blend({}, {1.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, NAN).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quintic_blend({}, {1.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, 0.0).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quintic_blend({}, {1.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, INFINITY).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quintic_blend({}, {}, {1.0, 0.0, 0.0}, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       geom::make_quintic_blend({}, {1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 1.0).error() !=
           rt::ErrorCode::invalid_argument) {
        return fail("curve construction boundary errors");
    }

    geom::ArcLengthTable<4> table;
    if(table.total_length() != 0.0 || table.parameter_at_length(1.0) != 0.0 ||
       table.build(geom::as_path_segment(spline.value())) != rt::ErrorCode::ok ||
       table.size() != 4 || !near(table.total_length(), spline.value().length, 1e-12) ||
       table.parameter_at_length(-1.0) != 0.0 ||
       table.parameter_at_length(spline.value().length + 1.0) != 1.0) {
        return fail("arc-length table public boundaries");
    }

    // Collinear pass-through and reflex degradation are explicit outcomes.
    const geom::LineSegment straight_on =
        geom::make_line({1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}).value();
    const plan::BlendDecision collinear = plan::decide_blend(
        geom::as_path_segment(before), geom::as_path_segment(straight_on), 0.1);
    if(collinear.enabled || !collinear.passthrough || collinear.degraded_to_buffered) {
        return fail("collinear passthrough");
    }
    const geom::LineSegment reverse =
        geom::make_line({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}).value();
    const plan::BlendDecision reflex = plan::decide_blend(
        geom::as_path_segment(before), geom::as_path_segment(reverse), 0.1);
    if(reflex.enabled || reflex.passthrough || !reflex.degraded_to_buffered) {
        return fail("reflex degrades to buffered");
    }

    const geom::QuadraticBlendSegment quadratic =
        geom::make_quadratic_blend({0.0, 0.0, 0.0}, {0.5, 0.1, 0.0},
                                   {1.0, 0.0, 0.0}, 0.1)
            .value();
    const geom::PathSegment quadratic_path = geom::as_path_segment(quadratic);
    const geom::Vec3 quadratic_mid = quadratic_path.sample(quadratic.length * 0.5);
    const geom::Vec3 quadratic_tangent = quadratic_path.tangent(quadratic.length * 0.5);
    const geom::Vec3 quadratic_d1 = quadratic_path.path_derivative(quadratic.length * 0.5);
    const geom::Vec3 quadratic_d2 = quadratic_path.path_second_derivative(quadratic.length * 0.5);
    const geom::Vec3 quadratic_d3 = quadratic_path.path_third_derivative(quadratic.length * 0.5);
    if(!near(quadratic_path.length(), quadratic.length, 1e-12) ||
       !near(quadratic_mid.x, 0.5, 1e-12) || quadratic_mid.y <= 0.0 ||
       geom::norm(quadratic_tangent) <= 0.0 || geom::norm(quadratic_d1) <= 0.0 ||
       !std::isfinite(geom::norm(quadratic_d2)) || !std::isfinite(geom::norm(quadratic_d3))) {
        return fail("quadratic path dispatch");
    }

    const geom::PathSegment cubic_path = geom::as_path_segment(spline.value());
    const geom::PathSegment quintic_path = blend.curve;
    if(cubic_path.length() <= 0.0 || geom::norm(cubic_path.tangent(0.5)) <= 0.0 ||
       !std::isfinite(geom::norm(cubic_path.path_derivative(0.5))) ||
       !std::isfinite(geom::norm(cubic_path.path_second_derivative(0.5))) ||
       !std::isfinite(geom::norm(cubic_path.path_third_derivative(0.5))) ||
       quintic_path.length() <= 0.0 || geom::norm(quintic_path.tangent(0.5)) <= 0.0 ||
       !std::isfinite(geom::norm(quintic_path.path_derivative(0.5))) ||
       !std::isfinite(geom::norm(quintic_path.path_second_derivative(0.5))) ||
       !std::isfinite(geom::norm(quintic_path.path_third_derivative(0.5)))) {
        return fail("cubic and quintic path dispatch");
    }

    geom::QuinticBlendSegment stationary{};
    stationary.length = 1.0;
    if(geom::norm(geom::path_derivative(stationary, 0.5)) != 0.0 ||
       geom::norm(geom::path_second_derivative(stationary, 0.5)) != 0.0 ||
       geom::norm(geom::path_third_derivative(stationary, 0.5)) != 0.0) {
        return fail("stationary quintic derivative boundaries");
    }
    return 0;
}

int check_path_buffer_and_lookahead()
{
    using namespace plcopen::core;
    const geom::LineSegment a = geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).value();
    const geom::LineSegment b = geom::make_line({1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}).value();
    plan::PathBuffer<2> path;
    if(path.consume_front() != rt::ErrorCode::out_of_range) {
        return fail("path underflow");
    }
    if(path.push(geom::as_path_segment(a)) != rt::ErrorCode::ok ||
       path.push(geom::as_path_segment(b)) != rt::ErrorCode::ok ||
       path.push(geom::as_path_segment(a)) != rt::ErrorCode::capacity_exceeded) {
        return fail("path capacity");
    }
    if(!near(path.total_length(), 2.0, 1e-12)) {
        return fail("path total length");
    }
    const geom::Vec3 p = path.sample(1.5);
    if(!near(p.x, 1.0, 1e-12) || !near(p.y, 0.5, 1e-12)) {
        return fail("path sample");
    }
    const geom::Vec3 beyond = path.sample(3.0);
    if(!near(beyond.x, 1.0, 1e-12) || !near(beyond.y, 1.0, 1e-12)) {
        return fail("path sample clamps to finish");
    }

    const rt::Result<plan::LookAheadPlan<2>> speeds = plan::compute_lookahead(path, 2.0, 1.0, 2);
    if(!speeds || speeds.value().entry_speed.size() != 2 || speeds.value().entry_speed[0] != 0.0 ||
       speeds.value().exit_speed[1] != 0.0) {
        return fail("lookahead boundary speeds");
    }

    plan::PathBuffer<2> empty;
    const geom::Vec3 empty_sample = empty.sample(1.0);
    if(!empty.empty() || empty.full() || empty.size() != 0 || empty.total_length() != 0.0 ||
       empty_sample.x != 0.0 || empty_sample.y != 0.0 || empty_sample.z != 0.0) {
        return fail("empty path state");
    }
    if(plan::compute_lookahead(empty, 2.0, 1.0, 2).value().entry_speed.size() != 0 ||
       plan::compute_lookahead(path, 0.0, 1.0, 2).error() != rt::ErrorCode::invalid_argument ||
       plan::compute_lookahead(path, 2.0, 0.0, 2).error() != rt::ErrorCode::invalid_argument ||
       plan::compute_lookahead(path, 2.0, 1.0, 0).error() != rt::ErrorCode::invalid_argument) {
        return fail("lookahead boundary validation");
    }
    const rt::Result<plan::LookAheadPlan<2>> one = plan::compute_lookahead(path, 10.0, 1.0, 1);
    if(!one || one.value().entry_speed.size() != 1 || one.value().exit_speed[0] != 0.0) {
        return fail("lookahead window truncation");
    }
    if(path.consume_front() != rt::ErrorCode::ok || path.size() != 1 ||
       !near(path.sample(0.5).x, 1.0, 1e-12) || !near(path.sample(0.5).y, 0.5, 1e-12)) {
        return fail("path consume front");
    }

    const plan::BlendDecision blend =
        plan::decide_blend(geom::as_path_segment(a), geom::as_path_segment(b), 0.1);
    if(!blend.enabled || blend.radius > 0.5 || blend.allowed_deviation != 0.1 ||
       blend.curve.length() <= 0.0) {
        return fail("blend decision");
    }
    return 0;
}

int check_sampler()
{
    using namespace plcopen::core;
    const geom::LineSegment line = geom::make_line({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}).value();
    exec::CommittedPath<1> path;
    if(path.push(geom::as_path_segment(line)) != rt::ErrorCode::ok) {
        return fail("committed path push");
    }

    const rt::Result<otg::Profile1D> profile =
        otg::plan({0.0, 0.0, 0.0}, {path.total_length(), 0.0, 0.0}, {2.0, 1.0, 1.0, 1.0});
    if(!profile) {
        return fail("sampler profile");
    }
    const geom::Vec3 finish = exec::sample_profiled_path(
        path, profile.value(), rt::CycleTick::from_cycles(profile.value().duration_cycles()));
    if(!near(finish.x, 4.0, 1e-9) || !near(finish.y, 0.0, 1e-12)) {
        return fail("profiled path finish");
    }

    exec::CommittedPath<2> empty;
    const geom::Vec3 empty_sample = empty.sample_arclength(1.0);
    if(empty.size() != 0 || empty.total_length() != 0.0 || empty_sample.x != 0.0 ||
       empty_sample.y != 0.0 || empty_sample.z != 0.0) {
        return fail("empty committed path");
    }
    const geom::LineSegment second =
        geom::make_line({4.0, 0.0, 0.0}, {4.0, 2.0, 0.0}).value();
    if(empty.push(geom::as_path_segment(line)) != rt::ErrorCode::ok ||
       empty.push(geom::as_path_segment(second)) != rt::ErrorCode::ok ||
       empty.push(geom::as_path_segment(line)) != rt::ErrorCode::capacity_exceeded) {
        return fail("committed path capacity");
    }
    const geom::Vec3 first_half = empty.sample_arclength(2.0);
    const geom::Vec3 second_half = empty.sample_arclength(5.0);
    const geom::Vec3 clamped = empty.sample_arclength(10.0);
    if(!near(first_half.x, 2.0, 1e-12) || !near(second_half.x, 4.0, 1e-12) ||
       !near(second_half.y, 1.0, 1e-12) || !near(clamped.x, 4.0, 1e-12) ||
       !near(clamped.y, 2.0, 1e-12)) {
        return fail("committed path multi-segment sampling");
    }
    return 0;
}

int check_sync_primitives()
{
    using namespace plcopen::core;
    const double geared = exec::sample_gear(2.0, {3.0, -1.0});
    if(!near(geared, 5.0, 1e-12)) {
        return fail("gear map");
    }

    exec::CamTable<4> cam;
    if(cam.push({0.0, 0.0}) != rt::ErrorCode::ok || cam.push({1.0, 2.0}) != rt::ErrorCode::ok ||
       cam.push({0.5, 1.0}) != rt::ErrorCode::invalid_argument) {
        return fail("cam table ordering");
    }
    const rt::Result<double> slave = cam.sample(0.25);
    if(!slave || !near(slave.value(), 0.5, 1e-12)) {
        return fail("cam interpolation");
    }

    const geom::Vec3 overlaid = exec::apply_overlay({1.0, 2.0, 3.0}, {0.5, -0.5, 1.0});
    if(!near(overlaid.x, 1.5, 1e-12) || !near(overlaid.y, 1.5, 1e-12) ||
       !near(overlaid.z, 4.0, 1e-12)) {
        return fail("overlay");
    }
    return 0;
}

int check_profile_storage_and_envelope_boundaries()
{
    using namespace plcopen::core;
    rt::StaticVector<int, 2> values;
    if(!values.empty() || values.full() || values.capacity() != 2 ||
       values.pop_back() != rt::ErrorCode::out_of_range ||
       values.push_back(3) != rt::ErrorCode::ok ||
       values.push_back(5) != rt::ErrorCode::ok || !values.full() ||
       values.push_back(7) != rt::ErrorCode::capacity_exceeded ||
       values[0] != 3 || values.data()[1] != 5) {
        return fail("static vector storage boundaries");
    }
    const rt::StaticVector<int, 2> &constant_values = values;
    if(constant_values.data()[0] != 3 || constant_values[1] != 5 ||
       values.pop_back() != rt::ErrorCode::ok) {
        return fail("static vector const and pop boundaries");
    }
    values.clear();
    if(!values.empty()) return fail("static vector clear");

    const otg::State1D start{1.0, 0.0, 0.0};
    const otg::Target1D finish{2.0, 0.0, 0.0};
    const otg::Segment1D segment = otg::make_quintic_segment(start, finish, 10);
    otg::Profile1D profile;
    for(std::size_t index = 0; index < otg::Profile1D::MaxSegments; ++index) {
        if(profile.add_segment(segment) != rt::ErrorCode::ok) {
            return fail("profile fills segment capacity");
        }
    }
    if(profile.add_segment(segment) != rt::ErrorCode::capacity_exceeded ||
       profile.segment_count() != otg::Profile1D::MaxSegments ||
       profile.duration_cycles() != 160 ||
       profile.translate(NAN) != rt::ErrorCode::invalid_argument ||
       profile.translate(4.0) != rt::ErrorCode::ok ||
       !near(profile.segment(0).start.position, 5.0, 1e-12) ||
       !near(profile.segment(0).finish.position, 6.0, 1e-12)) {
        return fail("profile capacity and translation boundaries");
    }
    otg::Profile1D empty;
    if(otg::sample(empty, rt::CycleTick::from_cycles(3)).position != 0.0 ||
       !near(otg::sample(profile, rt::CycleTick::from_cycles(-1)).position, 5.0, 1e-12) ||
       !near(otg::sample(profile, rt::CycleTick::from_cycles(1000)).position, 6.0, 1e-12)) {
        return fail("profile sample boundaries");
    }

    const otg::Limits1D limits{1.0, 1.0, 1.0, 1.0};
    otg::Segment1D invalid = segment;
    invalid.c1 = 2.0;
    if(otg::within_limits(invalid, limits)) return fail("profile velocity envelope");
    invalid = segment;
    invalid.c2 = 1.0;
    if(otg::within_limits(invalid, limits)) return fail("profile acceleration envelope");
    invalid = segment;
    invalid.c2 = -1.0;
    if(otg::within_limits(invalid, limits)) return fail("profile deceleration envelope");
    invalid = segment;
    invalid.c3 = 1.0;
    if(otg::within_limits(invalid, limits)) return fail("profile jerk envelope");
    if(otg::state_within_limits({0.0, 2.0, 0.0}, limits) ||
       otg::state_within_limits({0.0, 0.0, 2.0}, limits) ||
       otg::state_within_limits({0.0, 0.0, -2.0}, limits)) {
        return fail("state envelope boundaries");
    }
    return 0;
}

int check_planning_numeric_boundary_matrix()
{
    using namespace plcopen::core;
    if(!near(geom::normalize_sweep(-7.0, 1.0), 5.5663706143591725, 1e-12) ||
       !near(geom::normalize_sweep(7.0, -1.0), -5.5663706143591725, 1e-12)) {
        return fail("arc sweep multi-turn normalization");
    }
    if(plan::jerk_reachable_speed(0.5, 0.0, 1.0, 1.0) != 0.5 ||
       plan::jerk_reachable_speed(0.5, 1.0, 0.0, 1.0) != 0.5 ||
       plan::jerk_reachable_speed(0.5, 1.0, 1.0, 0.0) != 0.5) {
        return fail("jerk reachability invalid dynamics");
    }
    const geom::LineSegment line =
        geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).value();
    plan::PathBuffer<2> path;
    path.push(geom::as_path_segment(line));
    if(plan::compute_lookahead(path, -1.0, 1.0, 1).error() !=
           rt::ErrorCode::invalid_argument ||
       plan::compute_lookahead(path, 1.0, -1.0, 1).error() !=
           rt::ErrorCode::invalid_argument ||
       plan::decide_blend(geom::as_path_segment(line), geom::as_path_segment(line), -1.0)
           .enabled ||
       plan::decide_blend(geom::as_path_segment(line), geom::as_path_segment(line), NAN)
           .enabled) {
        return fail("planning numeric rejection matrix");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_line_geometry() != 0 || check_arc_geometry() != 0 ||
       check_spline_and_blending() != 0 || check_path_buffer_and_lookahead() != 0 ||
       check_sampler() != 0 || check_sync_primitives() != 0 ||
       check_profile_storage_and_envelope_boundaries() != 0 ||
       check_planning_numeric_boundary_matrix() != 0) {
        return 1;
    }
    std::printf("PASS r2 motion tests\n");
    return 0;
}
