#pragma once

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

    rt::Result<double> sample(double master_position) const
    {
        if(points_.size() < 2) {
            return rt::Result<double>::failure(rt::ErrorCode::invalid_argument);
        }
        if(master_position <= points_[0].master) {
            return rt::Result<double>::success(points_[0].slave);
        }
        for(std::size_t i = 1; i < points_.size(); ++i) {
            if(master_position <= points_[i].master) {
                const CamPoint before = points_[i - 1];
                const CamPoint after = points_[i];
                const double ratio =
                    (master_position - before.master) / (after.master - before.master);
                return rt::Result<double>::success(before.slave +
                                                   (after.slave - before.slave) * ratio);
            }
        }
        return rt::Result<double>::success(points_[points_.size() - 1].slave);
    }

private:
    static_assert(Capacity >= 2, "CamTable capacity must be at least two");
    rt::StaticVector<CamPoint, Capacity> points_{};
};

inline geom::Vec3 apply_overlay(geom::Vec3 base, geom::Vec3 overlay)
{
    return base + overlay;
}

} // namespace plcopen::core::exec
