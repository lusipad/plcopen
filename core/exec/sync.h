#pragma once

#include <cmath>
#include <cstddef>

#include "geom/geometry.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::exec
{

struct GearMap
{
    double ratio = 1.0;
    double offset = 0.0;
};

inline double sample_gear(double master_position, GearMap map)
{
    return master_position * map.ratio + map.offset;
}

struct CamPoint
{
    double master = 0.0;
    double slave = 0.0;
};

// Cam v2 addendum (approved 2026-07-06, KB-046): classical rest-to-rest
// motion-law generation, offline/submit domain. cam_law_value evaluates the
// unit-domain law (x in [0,1] maps to s in [0,1]); the modified-sine
// constants come from the exact piecewise antiderivatives at run time, not
// copied from handbooks. generate_cam_law discretizes into a caller-owned
// point array consumable by the existing linear/spline reconstruction
// (strictly increasing master by construction).
enum class CamLaw
{
    cycloidal,
    modified_sine,
    poly345,
};

inline double cam_law_value(CamLaw law, double x)
{
    constexpr double Pi = 3.14159265358979323846;
    x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
    if(law == CamLaw::cycloidal) {
        return x - std::sin(2.0 * Pi * x) / (2.0 * Pi);
    }
    if(law == CamLaw::poly345) {
        return x * x * x * (10.0 + x * (-15.0 + 6.0 * x));
    }
    // Modified sine: acceleration sin(w1*x) on [0,1/8] and [7/8,1],
    // cos(w2*(x-1/8)) between; velocity/position from the exact
    // antiderivatives with continuity constants, normalized by the total.
    const double w1 = 4.0 * Pi;
    const double w2 = 4.0 * Pi / 3.0;
    const double v18 = 1.0 / w1;
    const double s18 = (0.125 - 1.0 / w1) / w1;
    const double s78 = s18 + v18 * 0.75 + 2.0 / (w2 * w2);
    const double total = s78 + v18 * 0.125 - 1.0 / (w1 * w1);
    double s = 0.0;
    if(x < 0.125) {
        s = (x - std::sin(w1 * x) / w1) / w1;
    } else if(x <= 0.875) {
        const double u = x - 0.125;
        s = s18 + v18 * u + (1.0 - std::cos(w2 * u)) / (w2 * w2);
    } else {
        s = s78 + v18 * (x - 0.875) - (std::sin(w1 * x) + 1.0) / (w1 * w1);
    }
    return s / total;
}

inline rt::ErrorCode generate_cam_law(CamLaw law,
                                      double master_span,
                                      double rise,
                                      CamPoint *points,
                                      std::size_t point_count)
{
    if(points == nullptr || point_count < 8 || point_count > 64 ||
       !std::isfinite(master_span) || master_span <= 0.0 || !std::isfinite(rise)) {
        return rt::ErrorCode::invalid_argument;
    }
    for(std::size_t i = 0; i < point_count; ++i) {
        const double x =
            static_cast<double>(i) / static_cast<double>(point_count - 1);
        points[i].master = master_span * x;
        points[i].slave = rise * cam_law_value(law, x);
    }
    return rt::ErrorCode::ok;
}

// Non-owning cam table handle: L5/L6 consumers keep the point storage alive.
struct CamTableView
{
    const CamPoint *points = nullptr;
    std::size_t size = 0;
    bool periodic = false;

    bool valid() const
    {
        if(points == nullptr || size < 2) {
            return false;
        }
        for(std::size_t i = 0; i < size; ++i) {
            if(!std::isfinite(points[i].master) || !std::isfinite(points[i].slave)) {
                return false;
            }
            if(i > 0 && points[i].master <= points[i - 1].master) {
                return false;
            }
        }
        return true;
    }

    // Full table validation is an engage-time concern (valid()); the cycle-path
    // sample only guards the cheap emptiness/finiteness preconditions.
    rt::Result<double> sample(double master_position) const
    {
        if(points == nullptr || size < 2 || !std::isfinite(master_position)) {
            return rt::Result<double>::failure(rt::ErrorCode::invalid_argument);
        }
        double position = master_position;
        if(periodic) {
            const double begin = points[0].master;
            const double span = points[size - 1].master - begin;
            position = std::fmod(position - begin, span);
            if(position < 0.0) {
                position += span;
            }
            position += begin;
        }
        if(position <= points[0].master) {
            return rt::Result<double>::success(points[0].slave);
        }
        for(std::size_t i = 1; i < size; ++i) {
            if(position <= points[i].master) {
                const CamPoint before = points[i - 1];
                const CamPoint after = points[i];
                const double ratio = (position - before.master) / (after.master - before.master);
                return rt::Result<double>::success(before.slave +
                                                   (after.slave - before.slave) * ratio);
            }
        }
        return rt::Result<double>::success(points[size - 1].slave);
    }
};

template <std::size_t Capacity> class CamTable
{
public:
    rt::ErrorCode push(CamPoint point)
    {
        if(points_.size() > 0 && point.master <= points_[points_.size() - 1].master) {
            return rt::ErrorCode::invalid_argument;
        }
        return points_.push_back(point);
    }

    std::size_t size() const
    {
        return points_.size();
    }

    void set_periodic(bool periodic)
    {
        periodic_ = periodic;
    }

    bool periodic() const
    {
        return periodic_;
    }

    CamTableView view() const
    {
        return CamTableView{points_.data(), points_.size(), periodic_};
    }

    rt::Result<double> sample(double master_position) const
    {
        return view().sample(master_position);
    }

private:
    static_assert(Capacity >= 2, "CamTable capacity must be at least two");
    rt::StaticVector<CamPoint, Capacity> points_{};
    bool periodic_ = false;
};

// Cam run-layer interpolation (approved cam matrix, decision #2): linear is
// the byte-identical C0 compatibility mode; spline is the C2 cubic
// reconstruction below.
enum class CamInterpolation
{
    linear,
    spline,
};

// C2 cubic-spline reconstruction over a cam table (approved cam matrix,
// decisions #1/#3/#4): natural boundary for aperiodic tables, fully periodic
// boundary (position/velocity/acceleration continuous across the wrap) for
// periodic ones. build() solves the tridiagonal moment system once in the
// planning domain; sample() locates the interval and evaluates one cubic, so
// the cycle-path cost stays in the C0 order of magnitude. Fixed storage, no
// allocation.
class CamSpline
{
public:
    static constexpr std::size_t MaxPoints = 64;

    rt::ErrorCode build(const CamTableView &table)
    {
        size_ = 0;
        if(!table.valid() || table.size > MaxPoints) {
            return rt::ErrorCode::invalid_argument;
        }
        if(table.periodic &&
           std::fabs(table.points[0].slave - table.points[table.size - 1].slave) > 1e-12) {
            // A periodic spline over a mismatched wrap has no definition.
            return rt::ErrorCode::invalid_argument;
        }
        for(std::size_t i = 0; i < table.size; ++i) {
            points_[i] = table.points[i];
        }
        size_ = table.size;
        periodic_ = table.periodic;

        const std::size_t n = size_;
        double h[MaxPoints] = {};
        for(std::size_t i = 0; i + 1 < n; ++i) {
            h[i] = points_[i + 1].master - points_[i].master;
        }

        if(!periodic_) {
            // Natural boundary: m[0] = m[n-1] = 0, Thomas elimination.
            double diagonal[MaxPoints] = {};
            double upper[MaxPoints] = {};
            double rhs[MaxPoints] = {};
            m2_[0] = 0.0;
            m2_[n - 1] = 0.0;
            if(n == 2) {
                return rt::ErrorCode::ok;
            }
            for(std::size_t i = 1; i + 1 < n; ++i) {
                diagonal[i] = (h[i - 1] + h[i]) / 3.0;
                upper[i] = h[i] / 6.0;
                rhs[i] = (points_[i + 1].slave - points_[i].slave) / h[i] -
                         (points_[i].slave - points_[i - 1].slave) / h[i - 1];
            }
            for(std::size_t i = 2; i + 1 < n; ++i) {
                const double factor = (h[i - 1] / 6.0) / diagonal[i - 1];
                diagonal[i] -= factor * upper[i - 1];
                rhs[i] -= factor * rhs[i - 1];
            }
            for(std::size_t i = n - 2; i >= 1; --i) {
                const double above = i + 2 < n ? upper[i] * m2_[i + 1] : 0.0;
                m2_[i] = (rhs[i] - above) / diagonal[i];
                if(i == 1) {
                    break;
                }
            }
            return rt::ErrorCode::ok;
        }

        // Periodic boundary: unknowns m[0..n-2] on a cyclic tridiagonal
        // equations (m[n-1] = m[0]); Sherman-Morrison over the Thomas solve
        // (needs >= 3 unknowns; the 1- and 2-unknown wraps solve directly).
        const std::size_t cyclic = n - 1;
        if(cyclic < 2) {
            m2_[0] = 0.0;
            m2_[n - 1] = 0.0;
            return rt::ErrorCode::ok;
        }
        if(cyclic == 2) {
            const double h0 = h[0];
            const double h1 = h[1];
            const double d0 = (points_[1].slave - points_[0].slave) / h0 -
                              (points_[0].slave - points_[1].slave) / h1;
            const double d1 = (points_[2].slave - points_[1].slave) / h1 -
                              (points_[1].slave - points_[0].slave) / h0;
            const double b0 = (h1 + h0) / 3.0;
            const double c0 = h0 / 6.0 + h1 / 6.0; // both couplings reach the other node
            const double determinant = b0 * b0 - c0 * c0;
            if(determinant == 0.0) {
                size_ = 0;
                return rt::ErrorCode::invalid_argument;
            }
            m2_[0] = (b0 * d0 - c0 * d1) / determinant;
            m2_[1] = (b0 * d1 - c0 * d0) / determinant;
            m2_[2] = m2_[0];
            return rt::ErrorCode::ok;
        }
        double a[MaxPoints] = {}; // sub-diagonal
        double b[MaxPoints] = {}; // diagonal
        double c[MaxPoints] = {}; // super-diagonal
        double d[MaxPoints] = {}; // right-hand side
        for(std::size_t j = 0; j < cyclic; ++j) {
            const std::size_t previous = j == 0 ? cyclic - 1 : j - 1;
            const double h_prev = h[previous];
            const double h_next = h[j];
            const double y_prev = points_[previous].slave;
            const double y_here = points_[j].slave;
            const double y_next = points_[j + 1].slave;
            a[j] = h_prev / 6.0;
            b[j] = (h_prev + h_next) / 3.0;
            c[j] = h_next / 6.0;
            d[j] = (y_next - y_here) / h_next - (y_here - y_prev) / h_prev;
        }

        // Sherman-Morrison: fold the two cyclic corners into u v^T.
        const double corner_first = a[0];       // couples m[0] to m[cyclic-1]
        const double corner_last = c[cyclic - 1]; // couples m[cyclic-1] to m[0]
        const double gamma = -b[0];
        double b_mod[MaxPoints] = {};
        for(std::size_t j = 0; j < cyclic; ++j) {
            b_mod[j] = b[j];
        }
        b_mod[0] -= gamma;
        b_mod[cyclic - 1] -= corner_first * corner_last / gamma;

        double x[MaxPoints] = {};
        double z[MaxPoints] = {};
        double u[MaxPoints] = {};
        u[0] = gamma;
        u[cyclic - 1] = corner_last;
        if(!solve_tridiagonal(a, b_mod, c, d, x, cyclic) ||
           !solve_tridiagonal(a, b_mod, c, u, z, cyclic)) {
            size_ = 0;
            return rt::ErrorCode::invalid_argument;
        }
        const double v_dot_x = x[0] + (corner_first / gamma) * x[cyclic - 1];
        const double v_dot_z = 1.0 + z[0] + (corner_first / gamma) * z[cyclic - 1];
        if(v_dot_z == 0.0) {
            size_ = 0;
            return rt::ErrorCode::invalid_argument;
        }
        for(std::size_t j = 0; j < cyclic; ++j) {
            m2_[j] = x[j] - z[j] * (v_dot_x / v_dot_z);
        }
        m2_[n - 1] = m2_[0];
        return rt::ErrorCode::ok;
    }

    bool valid() const
    {
        return size_ >= 2;
    }

    rt::Result<double> sample(double master_position) const
    {
        return evaluate(master_position, 0);
    }

    // Analytic derivatives with respect to the master position (order 1 or
    // 2); the acceptance tests assert C2 continuity through these.
    rt::Result<double> sample_derivative(double master_position, int order) const
    {
        return evaluate(master_position, order);
    }

private:
    static bool solve_tridiagonal(const double *a,
                                  const double *b,
                                  const double *c,
                                  const double *d,
                                  double *x,
                                  std::size_t n)
    {
        double diagonal[MaxPoints] = {};
        double rhs[MaxPoints] = {};
        for(std::size_t i = 0; i < n; ++i) {
            diagonal[i] = b[i];
            rhs[i] = d[i];
        }
        for(std::size_t i = 1; i < n; ++i) {
            if(diagonal[i - 1] == 0.0) {
                return false;
            }
            const double factor = a[i] / diagonal[i - 1];
            diagonal[i] -= factor * c[i - 1];
            rhs[i] -= factor * rhs[i - 1];
        }
        if(diagonal[n - 1] == 0.0) {
            return false;
        }
        x[n - 1] = rhs[n - 1] / diagonal[n - 1];
        for(std::size_t i = n - 1; i >= 1; --i) {
            x[i - 1] = (rhs[i - 1] - c[i - 1] * x[i]) / diagonal[i - 1];
            if(i == 1) {
                break;
            }
        }
        return true;
    }

    rt::Result<double> evaluate(double master_position, int order) const
    {
        if(size_ < 2 || !std::isfinite(master_position)) {
            return rt::Result<double>::failure(rt::ErrorCode::invalid_argument);
        }
        double position = master_position;
        if(periodic_) {
            const double begin = points_[0].master;
            const double span = points_[size_ - 1].master - begin;
            position = std::fmod(position - begin, span);
            if(position < 0.0) {
                position += span;
            }
            position += begin;
        } else if(position <= points_[0].master || position >= points_[size_ - 1].master) {
            // Aperiodic boundary contract: outside the table the slave holds
            // the endpoint value, so every master-derivative is exactly zero.
            // At the boundary nodes themselves the sample is in-domain and the
            // derivatives must match the one-sided interior limit — fall
            // through to the interval evaluation for exact node hits.
            if(order == 0) {
                const double endpoint =
                    position <= points_[0].master ? points_[0].slave : points_[size_ - 1].slave;
                return rt::Result<double>::success(endpoint);
            }
            if(position < points_[0].master || position > points_[size_ - 1].master) {
                return rt::Result<double>::success(0.0);
            }
        }

        std::size_t index = 1;
        while(index < size_ - 1 && position > points_[index].master) {
            ++index;
        }
        const CamPoint before = points_[index - 1];
        const CamPoint after = points_[index];
        const double interval = after.master - before.master;
        const double left = after.master - position;
        const double right = position - before.master;
        const double m_before = m2_[index - 1];
        const double m_after = m2_[index];

        if(order == 0) {
            const double value =
                m_before * left * left * left / (6.0 * interval) +
                m_after * right * right * right / (6.0 * interval) +
                (before.slave - m_before * interval * interval / 6.0) * left / interval +
                (after.slave - m_after * interval * interval / 6.0) * right / interval;
            return rt::Result<double>::success(value);
        }
        if(order == 1) {
            const double value =
                -m_before * left * left / (2.0 * interval) +
                m_after * right * right / (2.0 * interval) +
                (after.slave - before.slave) / interval -
                (m_after - m_before) * interval / 6.0;
            return rt::Result<double>::success(value);
        }
        const double value = (m_before * left + m_after * right) / interval;
        return rt::Result<double>::success(value);
    }

    CamPoint points_[MaxPoints] = {};
    double m2_[MaxPoints] = {};
    std::size_t size_ = 0;
    bool periodic_ = false;
};

inline geom::Vec3 apply_overlay(geom::Vec3 base, geom::Vec3 overlay)
{
    return base + overlay;
}

} // namespace plcopen::core::exec
