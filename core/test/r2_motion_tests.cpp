#include <cmath>
#include <cstdio>

#include "exec/sampler.h"
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
    if(!blend.enabled || blend.radius > 0.1 || blend.allowed_deviation != 0.1) {
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

} // namespace

int main()
{
    if(check_line_geometry() != 0 || check_arc_geometry() != 0 ||
       check_path_buffer_and_lookahead() != 0 || check_sampler() != 0) {
        return 1;
    }
    std::printf("PASS r2 motion tests\n");
    return 0;
}
