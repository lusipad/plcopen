// Path derivative contract tests (Y3 prerequisite: geom q_s/q_ss).
//
// Oracle: central finite differences of sample() verify analytical
// path_derivative() and path_second_derivative() for all segment types.

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
geom::Vec3 numerical_first_derivative(F sample_fn, double s, double h)
{
    const geom::Vec3 forward = sample_fn(s + h);
    const geom::Vec3 backward = sample_fn(s - h);
    return (forward - backward) * (0.5 / h);
}

template <typename F>
geom::Vec3 numerical_second_derivative(F sample_fn, double s, double h)
{
    const geom::Vec3 forward = sample_fn(s + h);
    const geom::Vec3 center = sample_fn(s);
    const geom::Vec3 backward = sample_fn(s - h);
    return (forward - center * 2.0 + backward) * (1.0 / (h * h));
}

int check_line_derivatives()
{
    const auto result = geom::make_line({1.0, 2.0, 3.0}, {4.0, 6.0, 3.0});
    if(!result) { return fail("line construction"); }
    const geom::LineSegment line = result.value();

    const geom::Vec3 q_s = geom::path_derivative(line);
    const double expected_length = 5.0;
    if(!near(line.length, expected_length, 1e-12)) {
        return fail("line length");
    }
    if(!vec_near(q_s, {3.0 / 5.0, 4.0 / 5.0, 0.0}, 1e-12)) {
        return fail("line q_s value");
    }
    if(!near(geom::norm(q_s), 1.0, 1e-12)) {
        return fail("line q_s unit magnitude");
    }
    const geom::Vec3 q_ss = geom::path_second_derivative(line);
    if(!vec_near(q_ss, {0.0, 0.0, 0.0}, 1e-15)) {
        return fail("line q_ss zero");
    }

    const double h = 1e-6;
    auto sample_fn = [&](double s) { return geom::sample(line, s); };
    const geom::Vec3 num_d1 = numerical_first_derivative(sample_fn, line.length * 0.4, h);
    if(!vec_near(q_s, num_d1, 1e-6)) {
        return fail("line q_s vs numerical");
    }

    std::printf("  PASS line_derivatives\n");
    return 0;
}

int check_arc_derivatives()
{
    const auto result = geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 1.0}, {-1.0, 0.0, 2.0});
    if(!result) { return fail("arc construction"); }
    const geom::ArcSegment arc = result.value();

    const double h1 = 1e-7;
    const double h2 = 1e-4;
    auto sample_fn = [&](double s) { return geom::sample(arc, s); };

    for(int k = 1; k <= 4; ++k) {
        const double s = arc.length * k / 5.0;
        const geom::Vec3 q_s = geom::path_derivative(arc, s);
        const geom::Vec3 q_ss = geom::path_second_derivative(arc, s);

        const geom::Vec3 num_d1 = numerical_first_derivative(sample_fn, s, h1);
        const geom::Vec3 num_d2 = numerical_second_derivative(sample_fn, s, h2);

        if(!vec_near(q_s, num_d1, 1e-5)) {
            return fail("arc q_s vs numerical");
        }
        if(!vec_near(q_ss, num_d2, 1e-4)) {
            return fail("arc q_ss vs numerical");
        }
    }

    const geom::Vec3 q_s_start = geom::path_derivative(arc, 0.0);
    const geom::Vec3 tangent_start = geom::tangent(arc, 0.0);
    const geom::Vec3 q_s_xy = {q_s_start.x, q_s_start.y, 0.0};
    const double alignment = geom::dot(geom::normalize(q_s_xy), tangent_start);
    if(!near(std::fabs(alignment), 1.0, 1e-10)) {
        return fail("arc q_s XY aligned with tangent");
    }

    std::printf("  PASS arc_derivatives\n");
    return 0;
}

int check_cubic_bezier_derivatives()
{
    const auto result = geom::make_cubic_bezier(
        {0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {3.0, 2.0, 0.0}, {4.0, 0.0, 0.0});
    if(!result) { return fail("cubic construction"); }
    const geom::CubicBezierSegment curve = result.value();

    const double h1 = 1e-7;
    const double h2 = 1e-4;
    auto sample_fn = [&](double s) { return geom::sample(curve, s); };

    for(int k = 1; k <= 4; ++k) {
        const double s = curve.length * k / 5.0;
        const geom::Vec3 q_s = geom::path_derivative(curve, s);
        const geom::Vec3 q_ss = geom::path_second_derivative(curve, s);

        const geom::Vec3 num_d1 = numerical_first_derivative(sample_fn, s, h1);
        const geom::Vec3 num_d2 = numerical_second_derivative(sample_fn, s, h2);

        if(!vec_near(q_s, num_d1, 1e-4)) {
            return fail("cubic q_s vs numerical");
        }
        if(!vec_near(q_ss, num_d2, 1e-3)) {
            return fail("cubic q_ss vs numerical");
        }
    }

    const geom::Vec3 tangent_mid = geom::tangent(curve, curve.length * 0.5);
    const geom::Vec3 q_s_mid = geom::path_derivative(curve, curve.length * 0.5);
    const double alignment = geom::dot(geom::normalize(q_s_mid), tangent_mid);
    if(!near(std::fabs(alignment), 1.0, 1e-10)) {
        return fail("cubic q_s direction matches tangent");
    }

    std::printf("  PASS cubic_bezier_derivatives\n");
    return 0;
}

int check_quadratic_blend_derivatives()
{
    const auto result = geom::make_quadratic_blend(
        {0.0, 0.0, 0.0}, {2.0, 3.0, 0.0}, {4.0, 0.0, 0.0}, 10.0);
    if(!result) { return fail("quadratic construction"); }
    const geom::QuadraticBlendSegment blend = result.value();

    const double h1 = 1e-7;
    const double h2 = 1e-4;
    auto sample_fn = [&](double s) { return geom::sample(blend, s); };

    for(int k = 1; k <= 4; ++k) {
        const double s = blend.length * k / 5.0;
        const geom::Vec3 q_s = geom::path_derivative(blend, s);
        const geom::Vec3 q_ss = geom::path_second_derivative(blend, s);

        const geom::Vec3 num_d1 = numerical_first_derivative(sample_fn, s, h1);
        const geom::Vec3 num_d2 = numerical_second_derivative(sample_fn, s, h2);

        if(!vec_near(q_s, num_d1, 1e-4)) {
            return fail("quadratic q_s vs numerical");
        }
        if(!vec_near(q_ss, num_d2, 1e-3)) {
            return fail("quadratic q_ss vs numerical");
        }
    }

    const geom::Vec3 q_ss_const1 = geom::path_second_derivative(blend, blend.length * 0.2);
    const geom::Vec3 q_ss_const2 = geom::path_second_derivative(blend, blend.length * 0.8);
    if(!vec_near(q_ss_const1, q_ss_const2, 1e-12)) {
        return fail("quadratic q_ss constant");
    }

    std::printf("  PASS quadratic_blend_derivatives\n");
    return 0;
}

int check_quintic_blend_derivatives()
{
    const auto result = geom::make_quintic_blend(
        {0.0, 0.0, 0.0}, {5.0, 5.0, 0.0}, {10.0, 0.0, 0.0}, 10.0);
    if(!result) { return fail("quintic construction"); }
    const geom::QuinticBlendSegment blend = result.value();

    const double h1 = 1e-7;
    const double h2 = 1e-4;
    auto sample_fn = [&](double s) { return geom::sample(blend, s); };

    for(int k = 1; k <= 8; ++k) {
        const double s = blend.length * k / 9.0;
        const geom::Vec3 q_s = geom::path_derivative(blend, s);
        const geom::Vec3 q_ss = geom::path_second_derivative(blend, s);

        const geom::Vec3 num_d1 = numerical_first_derivative(sample_fn, s, h1);
        const geom::Vec3 num_d2 = numerical_second_derivative(sample_fn, s, h2);

        if(!vec_near(q_s, num_d1, 2e-3)) {
            std::printf("  quintic q_s at s=%.3f: analytical=(%.6f,%.6f,%.6f) "
                        "numerical=(%.6f,%.6f,%.6f)\n",
                        s, q_s.x, q_s.y, q_s.z, num_d1.x, num_d1.y, num_d1.z);
            return fail("quintic q_s vs numerical");
        }
        if(!vec_near(q_ss, num_d2, 1e-2)) {
            std::printf("  quintic q_ss at s=%.3f: analytical=(%.6f,%.6f,%.6f) "
                        "numerical=(%.6f,%.6f,%.6f)\n",
                        s, q_ss.x, q_ss.y, q_ss.z, num_d2.x, num_d2.y, num_d2.z);
            return fail("quintic q_ss vs numerical");
        }
    }

    const geom::Vec3 q_s_unit = geom::path_derivative(blend, blend.length * 0.5);
    if(!near(geom::norm(q_s_unit), 1.0, 1e-3)) {
        return fail("quintic q_s approximately unit");
    }

    std::printf("  PASS quintic_blend_derivatives\n");
    return 0;
}

int check_path_segment_dispatcher()
{
    const auto line_r = geom::make_line({0.0, 0.0, 0.0}, {3.0, 4.0, 0.0});
    if(!line_r) { return fail("segment line"); }
    geom::PathSegment seg = geom::as_path_segment(line_r.value());

    const geom::Vec3 q_s = seg.path_derivative(2.5);
    const geom::Vec3 q_ss = seg.path_second_derivative(2.5);
    if(!vec_near(q_s, {0.6, 0.8, 0.0}, 1e-12)) {
        return fail("segment dispatcher q_s");
    }
    if(!vec_near(q_ss, {0.0, 0.0, 0.0}, 1e-15)) {
        return fail("segment dispatcher q_ss");
    }

    const auto arc_r = geom::make_arc({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0});
    if(!arc_r) { return fail("segment arc"); }
    seg = geom::as_path_segment(arc_r.value());
    const geom::Vec3 arc_q_s = seg.path_derivative(arc_r.value().length * 0.5);
    if(geom::norm(arc_q_s) < 0.5) {
        return fail("segment arc q_s nonzero");
    }

    std::printf("  PASS path_segment_dispatcher\n");
    return 0;
}

int check_arc_curvature_from_q_ss()
{
    const auto result = geom::make_arc({2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {-2.0, 0.0, 0.0});
    if(!result) { return fail("curvature arc"); }
    const geom::ArcSegment arc = result.value();

    const geom::Vec3 q_ss = geom::path_second_derivative(arc, arc.length * 0.5);
    const double curvature = geom::norm(q_ss);
    const double expected_curvature = 1.0 / arc.radius;

    if(!near(curvature, expected_curvature, 1e-10)) {
        std::printf("  curvature=%.10f expected=%.10f\n", curvature, expected_curvature);
        return fail("arc curvature from q_ss");
    }

    std::printf("  PASS arc_curvature_from_q_ss\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("Geometry path derivative tests\n");
    int failures = 0;
    failures += check_line_derivatives();
    failures += check_arc_derivatives();
    failures += check_cubic_bezier_derivatives();
    failures += check_quadratic_blend_derivatives();
    failures += check_quintic_blend_derivatives();
    failures += check_path_segment_dispatcher();
    failures += check_arc_curvature_from_q_ss();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
