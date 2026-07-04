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
    const double ax = start.x;
    const double ay = start.y;
    const double bx = via.x;
    const double by = via.y;
    const double cx = finish.x;
    const double cy = finish.y;
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

    if(!std::isfinite(d) || std::fabs(d) <= 1e-12) {
        return rt::Result<ArcSegment>::failure(rt::ErrorCode::invalid_argument);
    }

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const Vec3 center{
        (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d,
        (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d,
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

inline rt::Result<QuadraticBlendSegment> make_quadratic_blend(Vec3 start,
                                                              Vec3 control,
                                                              Vec3 finish,
                                                              double tolerance)
{
    if(tolerance <= 0.0) {
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

enum class SegmentKind
{
    line,
    arc,
    cubic_bezier,
    quadratic_blend,
};

struct PathSegment
{
    SegmentKind kind = SegmentKind::line;
    LineSegment line{};
    ArcSegment arc{};
    CubicBezierSegment cubic{};
    QuadraticBlendSegment blend{};

    double length() const
    {
        if(kind == SegmentKind::line) {
            return line.length;
        }
        if(kind == SegmentKind::arc) {
            return arc.length;
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
        return kind == SegmentKind::cubic_bezier ? geom::tangent(cubic, arclength)
                                                 : geom::tangent(blend, arclength);
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
