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

inline geom::Vec3 apply_overlay(geom::Vec3 base, geom::Vec3 overlay)
{
    return base + overlay;
}

} // namespace plcopen::core::exec
