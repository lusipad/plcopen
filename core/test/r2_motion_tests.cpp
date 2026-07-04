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
    if(!blend.enabled || blend.curve.kind != geom::SegmentKind::quadratic_blend ||
       blend.curve.blend.max_deviation > 0.1) {
        return fail("quadratic blend curve");
    }
    const geom::Vec3 start = blend.curve.start();
    const geom::Vec3 finish = blend.curve.finish();
    if(!near(start.y, 0.0, 1e-12) || !near(finish.x, 1.0, 1e-12)) {
        return fail("blend endpoints stay on source segments");
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

    const rt::Result<plan::LookAheadPlan<2>> speeds = plan::compute_lookahead(path, 2.0, 1.0, 2);
    if(!speeds || speeds.value().entry_speed.size() != 2 || speeds.value().entry_speed[0] != 0.0 ||
       speeds.value().exit_speed[1] != 0.0) {
        return fail("lookahead boundary speeds");
    }

    const plan::BlendDecision blend =
        plan::decide_blend(geom::as_path_segment(a), geom::as_path_segment(b), 0.1);
    if(!blend.enabled || blend.radius > 0.1 || blend.allowed_deviation != 0.1 ||
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

} // namespace

int main()
{
    if(check_line_geometry() != 0 || check_arc_geometry() != 0 ||
       check_spline_and_blending() != 0 || check_path_buffer_and_lookahead() != 0 ||
       check_sampler() != 0 || check_sync_primitives() != 0) {
        return 1;
    }
    std::printf("PASS r2 motion tests\n");
    return 0;
}
