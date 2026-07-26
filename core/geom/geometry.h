#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::geom
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3 operator*(Vec3 value, double scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

inline double dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline double norm(Vec3 value)
{
    return std::sqrt(dot(value, value));
}

inline Vec3 normalize(Vec3 value)
{
    const double length = norm(value);
    if(length <= 0.0) {
        return {};
    }
    return value * (1.0 / length);
}

inline double clamp_arclength(double value, double length)
{
    if(value <= 0.0) {
        return 0.0;
    }
    if(value >= length) {
        return length;
    }
    return value;
}

struct LineSegment
{
    Vec3 start{};
    Vec3 finish{};
    double length = 0.0;
};

inline rt::Result<LineSegment> make_line(Vec3 start, Vec3 finish)
{
    const double length = norm(finish - start);
    if(!std::isfinite(length) || length <= 1e-12) {
        return rt::Result<LineSegment>::failure(rt::ErrorCode::invalid_argument);
    }
    return rt::Result<LineSegment>::success({start, finish, length});
}

inline Vec3 sample(LineSegment line, double arclength)
{
    const double ratio = clamp_arclength(arclength, line.length) / line.length;
    return line.start + (line.finish - line.start) * ratio;
}

inline Vec3 tangent(LineSegment line)
{
    return normalize(line.finish - line.start);
}

inline Vec3 path_derivative(LineSegment line)
{
    return (line.finish - line.start) * (1.0 / line.length);
}

inline Vec3 path_second_derivative(LineSegment)
{
    return {0.0, 0.0, 0.0};
}

inline Vec3 path_third_derivative(LineSegment)
{
    return {0.0, 0.0, 0.0};
}

struct ArcSegment
{
    Vec3 start{};
    Vec3 via{};
    Vec3 finish{};
    Vec3 center{};
    double radius = 0.0;
    double start_angle = 0.0;
    double sweep = 0.0;
    double length = 0.0;
};

struct CubicBezierSegment
{
    Vec3 p0{};
    Vec3 p1{};
    Vec3 p2{};
    Vec3 p3{};
    double length = 0.0;
};

struct QuadraticBlendSegment
{
    Vec3 start{};
    Vec3 control{};
    Vec3 finish{};
    double length = 0.0;
    double max_deviation = 0.0;
};

// A4 corner blend: symmetric quintic Bezier with collinear control triples
// (P0,P1,P2 on the incoming line, P3,P4,P5 on the outgoing line), which makes
// the curve C2 against straight lines: tangent along the lines and zero
// curvature at both junctions. Sampling runs through an embedded arc-length
// table so the spatial speed stays continuous across the junctions.
struct QuinticBlendSegment
{
    static constexpr std::size_t TableSize = 65;
    Vec3 p0{};
    Vec3 p1{};
    Vec3 p2{};
    Vec3 p3{};
    Vec3 p4{};
    Vec3 p5{};
    double length = 0.0;
    double max_deviation = 0.0;
    double max_curvature = 0.0;
    double cumulative[TableSize] = {};
};

inline double angle_of(Vec3 point, Vec3 center)
{
    return std::atan2(point.y - center.y, point.x - center.x);
}

inline double normalize_sweep(double sweep, double orientation)
{
    constexpr double Tau = 6.28318530717958647692;
    if(orientation >= 0.0) {
        while(sweep < 0.0) {
            sweep += Tau;
        }
    } else {
        while(sweep > 0.0) {
            sweep -= Tau;
        }
    }
    return sweep;
}

inline rt::Result<ArcSegment> make_arc(Vec3 start, Vec3 via, Vec3 finish)
{
    // Solve the circumcenter in centroid-relative coordinates: the |p|^2
    // terms then cancel on the triangle's own scale instead of against the
    // absolute origin, so the fit stays conditioned at machine offsets
    // (PCS translation, tool offsets). Mathematically identical to the
    // absolute-coordinate determinant form.
    const double gx = (start.x + via.x + finish.x) / 3.0;
    const double gy = (start.y + via.y + finish.y) / 3.0;
    const double ax = start.x - gx;
    const double ay = start.y - gy;
    const double bx = via.x - gx;
    const double by = via.y - gy;
    const double cx = finish.x - gx;
    const double cy = finish.y - gy;
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

    if(!std::isfinite(d) || std::fabs(d) <= 1e-12) {
        return rt::Result<ArcSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const Vec3 center{
        (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d + gx,
        (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d + gy,
        0.0,
    };
    const double radius = norm({start.x - center.x, start.y - center.y, 0.0});
    if(!std::isfinite(radius) || radius <= 1e-12) {
        return rt::Result<ArcSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    const double orientation =
        (via.x - start.x) * (finish.y - via.y) - (via.y - start.y) * (finish.x - via.x);
    const double start_angle = angle_of(start, center);
    const double raw_sweep = angle_of(finish, center) - start_angle;
    const double sweep = normalize_sweep(raw_sweep, orientation);
    if(std::fabs(sweep) <= 1e-12) {
        return rt::Result<ArcSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    ArcSegment arc{};
    arc.start = start;
    arc.via = via;
    arc.finish = finish;
    arc.center = center;
    arc.radius = radius;
    arc.start_angle = start_angle;
    arc.sweep = sweep;
    arc.length = std::fabs(sweep) * radius;
    return rt::Result<ArcSegment>::success(arc);
}

inline Vec3 sample(ArcSegment arc, double arclength)
{
    const double ratio = clamp_arclength(arclength, arc.length) / arc.length;
    const double angle = arc.start_angle + arc.sweep * ratio;
    return {
        arc.center.x + std::cos(angle) * arc.radius,
        arc.center.y + std::sin(angle) * arc.radius,
        arc.start.z + (arc.finish.z - arc.start.z) * ratio,
    };
}

inline Vec3 tangent(ArcSegment arc, double arclength)
{
    const double ratio = clamp_arclength(arclength, arc.length) / arc.length;
    const double angle = arc.start_angle + arc.sweep * ratio;
    const double direction = arc.sweep >= 0.0 ? 1.0 : -1.0;
    return {-std::sin(angle) * direction, std::cos(angle) * direction, 0.0};
}

inline Vec3 path_derivative(ArcSegment arc, double arclength)
{
    const double ratio = clamp_arclength(arclength, arc.length) / arc.length;
    const double angle = arc.start_angle + arc.sweep * ratio;
    const double omega = arc.sweep / arc.length;
    return {-arc.radius * std::sin(angle) * omega,
            arc.radius * std::cos(angle) * omega,
            (arc.finish.z - arc.start.z) / arc.length};
}

inline Vec3 path_second_derivative(ArcSegment arc, double arclength)
{
    const double ratio = clamp_arclength(arclength, arc.length) / arc.length;
    const double angle = arc.start_angle + arc.sweep * ratio;
    const double omega2 = (arc.sweep / arc.length) * (arc.sweep / arc.length);
    return {-arc.radius * std::cos(angle) * omega2,
            -arc.radius * std::sin(angle) * omega2,
            0.0};
}

inline Vec3 path_third_derivative(ArcSegment arc, double arclength)
{
    const double ratio = clamp_arclength(arclength, arc.length) / arc.length;
    const double angle = arc.start_angle + arc.sweep * ratio;
    const double omega = arc.sweep / arc.length;
    const double omega3 = omega * omega * omega;
    return {arc.radius * std::sin(angle) * omega3,
            -arc.radius * std::cos(angle) * omega3,
            0.0};
}

inline Vec3 bezier_point(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, double u)
{
    const double one_minus = 1.0 - u;
    return p0 * (one_minus * one_minus * one_minus) +
           p1 * (3.0 * one_minus * one_minus * u) +
           p2 * (3.0 * one_minus * u * u) +
           p3 * (u * u * u);
}

inline Vec3 quadratic_point(Vec3 p0, Vec3 p1, Vec3 p2, double u)
{
    const double one_minus = 1.0 - u;
    return p0 * (one_minus * one_minus) + p1 * (2.0 * one_minus * u) + p2 * (u * u);
}

template <typename SampleFn> double approximate_length(SampleFn sample_fn)
{
    constexpr int Samples = 32;
    double length = 0.0;
    Vec3 previous = sample_fn(0.0);
    for(int i = 1; i <= Samples; ++i) {
        const double u = static_cast<double>(i) / Samples;
        const Vec3 current = sample_fn(u);
        length += norm(current - previous);
        previous = current;
    }
    return length;
}

inline rt::Result<CubicBezierSegment> make_cubic_bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3)
{
    const double length =
        approximate_length([&](double u) { return bezier_point(p0, p1, p2, p3, u); });
    if(!std::isfinite(length) || length <= 1e-12) {
        return rt::Result<CubicBezierSegment>::failure(rt::ErrorCode::invalid_argument);
    }
    return rt::Result<CubicBezierSegment>::success({p0, p1, p2, p3, length});
}

inline Vec3 sample(CubicBezierSegment curve, double arclength)
{
    const double u = clamp_arclength(arclength, curve.length) / curve.length;
    return bezier_point(curve.p0, curve.p1, curve.p2, curve.p3, u);
}

inline Vec3 tangent(CubicBezierSegment curve, double arclength)
{
    const double u = clamp_arclength(arclength, curve.length) / curve.length;
    const double one_minus = 1.0 - u;
    const Vec3 derivative =
        (curve.p1 - curve.p0) * (3.0 * one_minus * one_minus) +
        (curve.p2 - curve.p1) * (6.0 * one_minus * u) +
        (curve.p3 - curve.p2) * (3.0 * u * u);
    return normalize(derivative);
}

inline Vec3 path_derivative(CubicBezierSegment curve, double arclength)
{
    const double u = clamp_arclength(arclength, curve.length) / curve.length;
    const double one_minus = 1.0 - u;
    const Vec3 du = (curve.p1 - curve.p0) * (3.0 * one_minus * one_minus) +
                    (curve.p2 - curve.p1) * (6.0 * one_minus * u) +
                    (curve.p3 - curve.p2) * (3.0 * u * u);
    return du * (1.0 / curve.length);
}

inline Vec3 path_second_derivative(CubicBezierSegment curve, double arclength)
{
    const double u = clamp_arclength(arclength, curve.length) / curve.length;
    const Vec3 d2u = (curve.p2 - curve.p1 * 2.0 + curve.p0) * (6.0 * (1.0 - u)) +
                     (curve.p3 - curve.p2 * 2.0 + curve.p1) * (6.0 * u);
    const double inv_L2 = 1.0 / (curve.length * curve.length);
    return d2u * inv_L2;
}

inline Vec3 path_third_derivative(CubicBezierSegment curve, double)
{
    const Vec3 d3u = (curve.p3 - curve.p2 * 3.0 + curve.p1 * 3.0 - curve.p0) * 6.0;
    const double inv_L3 = 1.0 / (curve.length * curve.length * curve.length);
    return d3u * inv_L3;
}

inline rt::Result<QuadraticBlendSegment> make_quadratic_blend(Vec3 start,
                                                              Vec3 control,
                                                              Vec3 finish,
                                                              double tolerance)
{
    if(tolerance <= 0.0 || !std::isfinite(tolerance)) {
        return rt::Result<QuadraticBlendSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    const double length =
        approximate_length([&](double u) { return quadratic_point(start, control, finish, u); });
    const Vec3 chord = finish - start;
    const double chord_length = norm(chord);
    if(!std::isfinite(length) || length <= 1e-12 || chord_length <= 1e-12) {
        return rt::Result<QuadraticBlendSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    const double deviation =
        norm(control - (start + chord * (dot(control - start, chord) / dot(chord, chord)))) * 0.5;
    if(deviation > tolerance + 1e-12) {
        return rt::Result<QuadraticBlendSegment>::failure(rt::ErrorCode::out_of_range);
    }

    return rt::Result<QuadraticBlendSegment>::success({start, control, finish, length, deviation});
}

inline Vec3 sample(QuadraticBlendSegment blend, double arclength)
{
    const double u = clamp_arclength(arclength, blend.length) / blend.length;
    return quadratic_point(blend.start, blend.control, blend.finish, u);
}

inline Vec3 tangent(QuadraticBlendSegment blend, double arclength)
{
    const double u = clamp_arclength(arclength, blend.length) / blend.length;
    const Vec3 derivative =
        (blend.control - blend.start) * (2.0 * (1.0 - u)) +
        (blend.finish - blend.control) * (2.0 * u);
    return normalize(derivative);
}

inline Vec3 path_derivative(QuadraticBlendSegment blend, double arclength)
{
    const double u = clamp_arclength(arclength, blend.length) / blend.length;
    const Vec3 du = (blend.control - blend.start) * (2.0 * (1.0 - u)) +
                    (blend.finish - blend.control) * (2.0 * u);
    return du * (1.0 / blend.length);
}

inline Vec3 path_second_derivative(QuadraticBlendSegment blend, double)
{
    const Vec3 d2u = (blend.finish - blend.control * 2.0 + blend.start) * 2.0;
    const double inv_L2 = 1.0 / (blend.length * blend.length);
    return d2u * inv_L2;
}

inline Vec3 path_third_derivative(QuadraticBlendSegment, double)
{
    return {0.0, 0.0, 0.0};
}

inline Vec3 quintic_point(const QuinticBlendSegment &blend, double u)
{
    const double v = 1.0 - u;
    const double v2 = v * v;
    const double u2 = u * u;
    return blend.p0 * (v2 * v2 * v) + blend.p1 * (5.0 * v2 * v2 * u) +
           blend.p2 * (10.0 * v2 * v * u2) + blend.p3 * (10.0 * v2 * u2 * u) +
           blend.p4 * (5.0 * v * u2 * u2) + blend.p5 * (u2 * u2 * u);
}

inline Vec3 quintic_derivative(const QuinticBlendSegment &blend, double u)
{
    const double v = 1.0 - u;
    const double v2 = v * v;
    const double u2 = u * u;
    return (blend.p1 - blend.p0) * (5.0 * v2 * v2) +
           (blend.p2 - blend.p1) * (20.0 * v2 * v * u) +
           (blend.p3 - blend.p2) * (30.0 * v2 * u2) +
           (blend.p4 - blend.p3) * (20.0 * v * u2 * u) +
           (blend.p5 - blend.p4) * (5.0 * u2 * u2);
}

inline Vec3 quintic_second_derivative(const QuinticBlendSegment &blend, double u)
{
    const double v = 1.0 - u;
    const Vec3 d0 = blend.p2 - blend.p1 * 2.0 + blend.p0;
    const Vec3 d1 = blend.p3 - blend.p2 * 2.0 + blend.p1;
    const Vec3 d2 = blend.p4 - blend.p3 * 2.0 + blend.p2;
    const Vec3 d3 = blend.p5 - blend.p4 * 2.0 + blend.p3;
    return d0 * (20.0 * v * v * v) + d1 * (60.0 * v * v * u) + d2 * (60.0 * v * u * u) +
           d3 * (20.0 * u * u * u);
}

inline Vec3 quintic_third_derivative(const QuinticBlendSegment &blend, double u)
{
    const double v = 1.0 - u;
    const Vec3 e0 = blend.p3 - blend.p2 * 3.0 + blend.p1 * 3.0 - blend.p0;
    const Vec3 e1 = blend.p4 - blend.p3 * 3.0 + blend.p2 * 3.0 - blend.p1;
    const Vec3 e2 = blend.p5 - blend.p4 * 3.0 + blend.p3 * 3.0 - blend.p2;
    return e0 * (60.0 * v * v) + e1 * (120.0 * v * u) + e2 * (60.0 * u * u);
}

inline Vec3 cross(Vec3 lhs, Vec3 rhs)
{
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

// start/finish are the truncated junction points on the adjacent segments;
// corner is the original path corner. The maximum deviation of the symmetric
// uniform-thirds construction is closed-form: |B(1/2) - corner| =
// (23/96)|(finish-corner) - (corner-start)|.
inline rt::Result<QuinticBlendSegment> make_quintic_blend(Vec3 start,
                                                          Vec3 corner,
                                                          Vec3 finish,
                                                          double tolerance)
{
    if(tolerance <= 0.0 || !std::isfinite(tolerance)) {
        return rt::Result<QuinticBlendSegment>::failure(rt::ErrorCode::invalid_argument);
    }
    const Vec3 in = corner - start;
    const Vec3 out = finish - corner;
    if(norm(in) <= 1e-12 || norm(out) <= 1e-12) {
        return rt::Result<QuinticBlendSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    QuinticBlendSegment blend{};
    blend.p0 = start;
    blend.p1 = corner - in * (2.0 / 3.0);
    blend.p2 = corner - in * (1.0 / 3.0);
    blend.p3 = corner + out * (1.0 / 3.0);
    blend.p4 = corner + out * (2.0 / 3.0);
    blend.p5 = finish;

    blend.max_deviation = norm(quintic_point(blend, 0.5) - corner);
    if(blend.max_deviation > tolerance + 1e-12) {
        return rt::Result<QuinticBlendSegment>::failure(rt::ErrorCode::out_of_range);
    }

    // Arc-length table (planning-phase work) for junction-continuous sampling.
    double accumulated = 0.0;
    Vec3 previous = quintic_point(blend, 0.0);
    blend.cumulative[0] = 0.0;
    for(std::size_t i = 1; i < QuinticBlendSegment::TableSize; ++i) {
        const double u =
            static_cast<double>(i) / static_cast<double>(QuinticBlendSegment::TableSize - 1);
        const Vec3 current = quintic_point(blend, u);
        accumulated += norm(current - previous);
        blend.cumulative[i] = accumulated;
        previous = current;
    }
    blend.length = accumulated;
    if(!std::isfinite(blend.length) || blend.length <= 1e-12) {
        return rt::Result<QuinticBlendSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    double max_curvature = 0.0;
    for(std::size_t i = 0; i <= 64; ++i) {
        const double u = static_cast<double>(i) / 64.0;
        const Vec3 d1 = quintic_derivative(blend, u);
        const Vec3 d2 = quintic_second_derivative(blend, u);
        const double speed = norm(d1);
        if(speed <= 1e-12) {
            continue;
        }
        const double curvature = norm(cross(d1, d2)) / (speed * speed * speed);
        if(curvature > max_curvature) {
            max_curvature = curvature;
        }
    }
    blend.max_curvature = max_curvature;
    return rt::Result<QuinticBlendSegment>::success(blend);
}

inline double quintic_parameter_at_length(const QuinticBlendSegment &blend, double arclength)
{
    const double target = clamp_arclength(arclength, blend.length);
    constexpr std::size_t Last = QuinticBlendSegment::TableSize - 1;
    std::size_t low = 0;
    for(std::size_t i = 1; i <= Last; ++i) {
        if(blend.cumulative[i] >= target) {
            low = i - 1;
            break;
        }
        low = i - 1;
    }
    const double segment = blend.cumulative[low + 1] - blend.cumulative[low];
    const double fraction = segment > 0.0 ? (target - blend.cumulative[low]) / segment : 0.0;
    return (static_cast<double>(low) + fraction) / static_cast<double>(Last);
}

inline Vec3 sample(const QuinticBlendSegment &blend, double arclength)
{
    return quintic_point(blend, quintic_parameter_at_length(blend, arclength));
}

inline Vec3 tangent(const QuinticBlendSegment &blend, double arclength)
{
    return normalize(quintic_derivative(blend, quintic_parameter_at_length(blend, arclength)));
}

inline Vec3 path_derivative(const QuinticBlendSegment &blend, double arclength)
{
    const double u = quintic_parameter_at_length(blend, arclength);
    const Vec3 d1 = quintic_derivative(blend, u);
    const double sigma = norm(d1);
    if(sigma <= 1e-15) {
        return {0.0, 0.0, 0.0};
    }
    return d1 * (1.0 / sigma);
}

inline Vec3 path_second_derivative(const QuinticBlendSegment &blend, double arclength)
{
    const double u = quintic_parameter_at_length(blend, arclength);
    const Vec3 d1 = quintic_derivative(blend, u);
    const Vec3 d2 = quintic_second_derivative(blend, u);
    const double sigma2 = dot(d1, d1);
    if(sigma2 <= 1e-30) {
        return {0.0, 0.0, 0.0};
    }
    return (d2 - d1 * (dot(d2, d1) / sigma2)) * (1.0 / sigma2);
}

inline Vec3 path_third_derivative(const QuinticBlendSegment &blend, double arclength)
{
    const double u = quintic_parameter_at_length(blend, arclength);
    const Vec3 d1 = quintic_derivative(blend, u);
    const Vec3 d2 = quintic_second_derivative(blend, u);
    const Vec3 d3 = quintic_third_derivative(blend, u);
    const double sigma2 = dot(d1, d1);
    if(sigma2 <= 1e-30) {
        return {0.0, 0.0, 0.0};
    }
    const double alpha = dot(d1, d2);
    const double beta = dot(d2, d2) + dot(d1, d3);
    const double sigma5 = sigma2 * sigma2 * std::sqrt(sigma2);
    return (d3 * sigma2 - d2 * (3.0 * alpha) + d1 * (4.0 * alpha * alpha / sigma2 - beta)) *
           (1.0 / sigma5);
}

enum class SegmentKind
{
    line,
    arc,
    cubic_bezier,
    quadratic_blend,
    quintic_blend,
};

struct PathSegment
{
    SegmentKind kind = SegmentKind::line;
    LineSegment line{};
    ArcSegment arc{};
    CubicBezierSegment cubic{};
    QuadraticBlendSegment blend{};
    QuinticBlendSegment quintic{};

    double length() const
    {
        if(kind == SegmentKind::line) {
            return line.length;
        }
        if(kind == SegmentKind::arc) {
            return arc.length;
        }
        if(kind == SegmentKind::quintic_blend) {
            return quintic.length;
        }
        return kind == SegmentKind::cubic_bezier ? cubic.length : blend.length;
    }

    Vec3 sample(double arclength) const
    {
        if(kind == SegmentKind::line) {
            return geom::sample(line, arclength);
        }
        if(kind == SegmentKind::arc) {
            return geom::sample(arc, arclength);
        }
        if(kind == SegmentKind::quintic_blend) {
            return geom::sample(quintic, arclength);
        }
        return kind == SegmentKind::cubic_bezier ? geom::sample(cubic, arclength)
                                                 : geom::sample(blend, arclength);
    }

    Vec3 tangent(double arclength) const
    {
        if(kind == SegmentKind::line) {
            return geom::tangent(line);
        }
        if(kind == SegmentKind::arc) {
            return geom::tangent(arc, arclength);
        }
        if(kind == SegmentKind::quintic_blend) {
            return geom::tangent(quintic, arclength);
        }
        return kind == SegmentKind::cubic_bezier ? geom::tangent(cubic, arclength)
                                                 : geom::tangent(blend, arclength);
    }

    Vec3 path_derivative(double arclength) const
    {
        if(kind == SegmentKind::line) {
            return geom::path_derivative(line);
        }
        if(kind == SegmentKind::arc) {
            return geom::path_derivative(arc, arclength);
        }
        if(kind == SegmentKind::quintic_blend) {
            return geom::path_derivative(quintic, arclength);
        }
        return kind == SegmentKind::cubic_bezier ? geom::path_derivative(cubic, arclength)
                                                  : geom::path_derivative(blend, arclength);
    }

    Vec3 path_second_derivative(double arclength) const
    {
        if(kind == SegmentKind::line) {
            return geom::path_second_derivative(line);
        }
        if(kind == SegmentKind::arc) {
            return geom::path_second_derivative(arc, arclength);
        }
        if(kind == SegmentKind::quintic_blend) {
            return geom::path_second_derivative(quintic, arclength);
        }
        return kind == SegmentKind::cubic_bezier
                   ? geom::path_second_derivative(cubic, arclength)
                   : geom::path_second_derivative(blend, arclength);
    }

    Vec3 path_third_derivative(double arclength) const
    {
        if(kind == SegmentKind::line) {
            return geom::path_third_derivative(line);
        }
        if(kind == SegmentKind::arc) {
            return geom::path_third_derivative(arc, arclength);
        }
        if(kind == SegmentKind::quintic_blend) {
            return geom::path_third_derivative(quintic, arclength);
        }
        return kind == SegmentKind::cubic_bezier
                   ? geom::path_third_derivative(cubic, arclength)
                   : geom::path_third_derivative(blend, arclength);
    }

    Vec3 start() const
    {
        return sample(0.0);
    }

    Vec3 finish() const
    {
        return sample(length());
    }
};

inline PathSegment as_path_segment(LineSegment line)
{
    PathSegment segment{};
    segment.kind = SegmentKind::line;
    segment.line = line;
    return segment;
}

inline PathSegment as_path_segment(ArcSegment arc)
{
    PathSegment segment{};
    segment.kind = SegmentKind::arc;
    segment.arc = arc;
    return segment;
}

inline PathSegment as_path_segment(CubicBezierSegment cubic)
{
    PathSegment segment{};
    segment.kind = SegmentKind::cubic_bezier;
    segment.cubic = cubic;
    return segment;
}

inline PathSegment as_path_segment(QuadraticBlendSegment blend)
{
    PathSegment segment{};
    segment.kind = SegmentKind::quadratic_blend;
    segment.blend = blend;
    return segment;
}

inline PathSegment as_path_segment(const QuinticBlendSegment &blend)
{
    PathSegment segment{};
    segment.kind = SegmentKind::quintic_blend;
    segment.quintic = blend;
    return segment;
}

template <std::size_t Capacity> class ArcLengthTable
{
public:
    rt::ErrorCode build(const PathSegment &segment)
    {
        lengths_.clear();
        for(std::size_t i = 0; i < Capacity; ++i) {
            const double ratio = static_cast<double>(i) / static_cast<double>(Capacity - 1);
            const rt::ErrorCode pushed = lengths_.push_back(segment.length() * ratio);
            if(pushed != rt::ErrorCode::ok) {
                return pushed;
            }
        }
        return rt::ErrorCode::ok;
    }

    std::size_t size() const
    {
        return lengths_.size();
    }

    double total_length() const
    {
        return lengths_.empty() ? 0.0 : lengths_[lengths_.size() - 1];
    }

    double parameter_at_length(double arclength) const
    {
        if(lengths_.empty() || total_length() <= 0.0) {
            return 0.0;
        }
        return clamp_arclength(arclength, total_length()) / total_length();
    }

private:
    static_assert(Capacity >= 2, "ArcLengthTable capacity must be at least two");
    rt::StaticVector<double, Capacity> lengths_{};
};

} // namespace plcopen::core::geom
