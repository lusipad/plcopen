// Path third-derivative contract tests (q_sss prerequisite for TOPP-RA Layer 2).
//
// Oracle: central finite differences of path_second_derivative() verify
// analytical path_third_derivative() for all segment types.
// Cross-check: arc q_sss magnitude = curvature / radius = 1/R².

#include <cmath>
#include <cstdio>

#include "geom/geometry.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool vec_near(geom::Vec3 a, geom::Vec3 b, double tolerance)
{
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) &&
           near(a.z, b.z, tolerance);
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

template <typename F>
geom::Vec3 numerical_derivative(F fn, double s, double h)
{
    const geom::Vec3 forward = fn(s + h);
    const geom::Vec3 backward = fn(s - h);
    return (forward - backward) * (0.5 / h);
}

int check_line_q_sss()
{
    const auto result = geom::make_line({1.0, 2.0, 3.0}, {4.0, 6.0, 3.0});
    if(!result) { return fail("line construction"); }
    const geom::LineSegment line = result.value();

    const geom::Vec3 q_sss = geom::path_third_derivative(line);
    if(!vec_near(q_sss, {0.0, 0.0, 0.0}, 1e-15)) {
        return fail("line q_sss zero");
    }

    std::printf("  PASS line_q_sss\n");
    return 0;
}

int check_arc_q_sss()
{
    const auto result = geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 1.0}, {-1.0, 0.0, 2.0});
    if(!result) { return fail("arc construction"); }
    const geom::ArcSegment arc = result.value();

    const double h = 1e-5;
    auto q_ss_fn = [&](double s) { return geom::path_second_derivative(arc, s); };

    for(int k = 1; k <= 4; ++k) {
        const double s = arc.length * k / 5.0;
        const geom::Vec3 q_sss = geom::path_third_derivative(arc, s);
        const geom::Vec3 num_d = numerical_derivative(q_ss_fn, s, h);

        if(!vec_near(q_sss, num_d, 1e-4)) {
            std::printf("  arc q_sss at s=%.3f: analytical=(%.6f,%.6f,%.6f) "
                        "numerical=(%.6f,%.6f,%.6f)\n",
                        s, q_sss.x, q_sss.y, q_sss.z, num_d.x, num_d.y, num_d.z);
            return fail("arc q_sss vs numerical");
        }
    }

    std::printf("  PASS arc_q_sss\n");
    return 0;
}

int check_arc_q_sss_magnitude()
{
    const auto result = geom::make_arc({2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {-2.0, 0.0, 0.0});
    if(!result) { return fail("magnitude arc construction"); }
    const geom::ArcSegment arc = result.value();

    const double expected = 1.0 / (arc.radius * arc.radius);
    for(int k = 1; k <= 4; ++k) {
        const double s = arc.length * k / 5.0;
        const geom::Vec3 q_sss = geom::path_third_derivative(arc, s);
        const double mag = geom::norm(q_sss);
        if(!near(mag, expected, 1e-10)) {
            std::printf("  |q_sss|=%.10f expected=%.10f at s=%.3f\n", mag, expected, s);
            return fail("arc q_sss magnitude = 1/R^2");
        }
    }

    std::printf("  PASS arc_q_sss_magnitude\n");
    return 0;
}

int check_cubic_bezier_q_sss()
{
    const auto result = geom::make_cubic_bezier(
        {0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {3.0, 2.0, 0.0}, {4.0, 0.0, 0.0});
    if(!result) { return fail("cubic construction"); }
    const geom::CubicBezierSegment curve = result.value();

    const geom::Vec3 q_sss_a = geom::path_third_derivative(curve, curve.length * 0.3);
    const geom::Vec3 q_sss_b = geom::path_third_derivative(curve, curve.length * 0.7);
    if(!vec_near(q_sss_a, q_sss_b, 1e-12)) {
        return fail("cubic q_sss constant");
    }

    const double h = 1e-5;
    auto q_ss_fn = [&](double s) { return geom::path_second_derivative(curve, s); };
    const geom::Vec3 num_d = numerical_derivative(q_ss_fn, curve.length * 0.5, h);

    if(!vec_near(q_sss_a, num_d, 1e-3)) {
        std::printf("  cubic q_sss: analytical=(%.6f,%.6f,%.6f) "
                    "numerical=(%.6f,%.6f,%.6f)\n",
                    q_sss_a.x, q_sss_a.y, q_sss_a.z, num_d.x, num_d.y, num_d.z);
        return fail("cubic q_sss vs numerical");
    }

    std::printf("  PASS cubic_bezier_q_sss\n");
    return 0;
}

int check_quadratic_q_sss()
{
    const auto result = geom::make_quadratic_blend(
        {0.0, 0.0, 0.0}, {2.0, 3.0, 0.0}, {4.0, 0.0, 0.0}, 10.0);
    if(!result) { return fail("quadratic construction"); }
    const geom::QuadraticBlendSegment blend = result.value();

    const geom::Vec3 q_sss = geom::path_third_derivative(blend, blend.length * 0.5);
    if(!vec_near(q_sss, {0.0, 0.0, 0.0}, 1e-15)) {
        return fail("quadratic q_sss zero");
    }

    std::printf("  PASS quadratic_q_sss\n");
    return 0;
}

int check_quintic_q_sss()
{
    const auto result = geom::make_quintic_blend(
        {0.0, 0.0, 0.0}, {5.0, 5.0, 0.0}, {10.0, 0.0, 0.0}, 10.0);
    if(!result) { return fail("quintic construction"); }
    const geom::QuinticBlendSegment blend = result.value();

    const double h = 1e-4;
    auto q_ss_fn = [&](double s) { return geom::path_second_derivative(blend, s); };

    for(int k = 1; k <= 6; ++k) {
        const double s = blend.length * k / 7.0;
        const geom::Vec3 q_sss = geom::path_third_derivative(blend, s);
        const geom::Vec3 num_d = numerical_derivative(q_ss_fn, s, h);

        if(!vec_near(q_sss, num_d, 0.5)) {
            std::printf("  quintic q_sss at s=%.3f: analytical=(%.6f,%.6f,%.6f) "
                        "numerical=(%.6f,%.6f,%.6f)\n",
                        s, q_sss.x, q_sss.y, q_sss.z, num_d.x, num_d.y, num_d.z);
            return fail("quintic q_sss vs numerical");
        }
    }

    std::printf("  PASS quintic_q_sss\n");
    return 0;
}

int check_path_segment_dispatcher_q_sss()
{
    const auto line_r = geom::make_line({0.0, 0.0, 0.0}, {3.0, 4.0, 0.0});
    if(!line_r) { return fail("segment line"); }
    geom::PathSegment seg = geom::as_path_segment(line_r.value());

    const geom::Vec3 q_sss = seg.path_third_derivative(2.5);
    if(!vec_near(q_sss, {0.0, 0.0, 0.0}, 1e-15)) {
        return fail("segment dispatcher q_sss line");
    }

    const auto arc_r = geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!arc_r) { return fail("segment arc"); }
    seg = geom::as_path_segment(arc_r.value());
    const geom::Vec3 arc_q_sss = seg.path_third_derivative(arc_r.value().length * 0.5);
    if(geom::norm(arc_q_sss) < 0.1) {
        return fail("segment arc q_sss nonzero");
    }

    std::printf("  PASS path_segment_dispatcher_q_sss\n");
    return 0;
}

int check_arc_frenet_identity()
{
    const auto result = geom::make_arc({2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {-2.0, 0.0, 0.0});
    if(!result) { return fail("frenet arc"); }
    const geom::ArcSegment arc = result.value();

    for(int k = 1; k <= 4; ++k) {
        const double s = arc.length * k / 5.0;
        const geom::Vec3 q_s = geom::path_derivative(arc, s);
        const geom::Vec3 q_ss = geom::path_second_derivative(arc, s);
        const geom::Vec3 q_sss = geom::path_third_derivative(arc, s);
        const double lhs = geom::dot(q_s, q_sss);
        const double rhs = -geom::dot(q_ss, q_ss);
        if(!near(lhs, rhs, 1e-10)) {
            std::printf("  q_s·q_sss=%.10f, -|q_ss|²=%.10f at s=%.3f\n", lhs, rhs, s);
            return fail("arc Frenet identity q_s·q_sss = -|q_ss|^2");
        }
    }

    std::printf("  PASS arc_frenet_identity\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("Geometry path third-derivative tests\n");
    int failures = 0;
    failures += check_line_q_sss();
    failures += check_arc_q_sss();
    failures += check_arc_q_sss_magnitude();
    failures += check_cubic_bezier_q_sss();
    failures += check_quadratic_q_sss();
    failures += check_quintic_q_sss();
    failures += check_path_segment_dispatcher_q_sss();
    failures += check_arc_frenet_identity();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
