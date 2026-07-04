#pragma once

#include <cstddef>

#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::exec
{

template <std::size_t Capacity> class CommittedPath
{
public:
    rt::ErrorCode push(geom::PathSegment segment)
    {
        return segments_.push_back(segment);
    }

    std::size_t size() const
    {
        return segments_.size();
    }

    double total_length() const
    {
        double total = 0.0;
        for(std::size_t i = 0; i < segments_.size(); ++i) {
            total += segments_[i].length();
        }
        return total;
    }

    geom::Vec3 sample_arclength(double arclength) const
    {
        double remaining = arclength;
        for(std::size_t i = 0; i < segments_.size(); ++i) {
            const double length = segments_[i].length();
            if(remaining <= length) {
                return segments_[i].sample(remaining);
            }
            remaining -= length;
        }

        if(segments_.empty()) {
            return {};
        }
        return segments_[segments_.size() - 1].sample(segments_[segments_.size() - 1].length());
    }

private:
    rt::StaticVector<geom::PathSegment, Capacity> segments_{};
};

template <std::size_t Capacity>
geom::Vec3 sample_profiled_path(const CommittedPath<Capacity> &path,
                                const otg::Profile1D &profile,
                                rt::CycleTick tick)
{
    const otg::State1D scalar = otg::sample(profile, tick);
    return path.sample_arclength(scalar.position);
}

} // namespace plcopen::core::exec
