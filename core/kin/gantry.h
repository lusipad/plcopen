#pragma once

// Reference kinematics #1 (approved matrix, decision #7): Cartesian gantry —
// a per-axis linear map joint = (cartesian - offset) / scale. The simplest
// mechanism above the identity, singularity-free by construction.

#include <cmath>
#include <cstddef>

#include "kin/kinematics.h"

namespace plcopen::core::kin
{

class CartesianGantry final : public Kinematics
{
public:
    // axis_count in [2, 3]; scale must be nonzero per axis.
    CartesianGantry(std::size_t axis_count,
                    const double *scale,
                    const double *offset)
        : count_(axis_count)
    {
        for(std::size_t i = 0; i < count_ && i < 3; ++i) {
            scale_[i] = scale[i];
            offset_[i] = offset[i];
        }
    }

    std::size_t joint_count() const override
    {
        return count_;
    }

    std::size_t cartesian_count() const override
    {
        return count_;
    }

    rt::ErrorCode forward(const double *joints,
                          std::size_t joint_count,
                          geom::Vec3 &cartesian) const override
    {
        if(joint_count != count_) {
            return rt::ErrorCode::invalid_argument;
        }
        const double x = scale_[0] * joints[0] + offset_[0];
        const double y = count_ > 1 ? scale_[1] * joints[1] + offset_[1] : 0.0;
        const double z = count_ > 2 ? scale_[2] * joints[2] + offset_[2] : 0.0;
        cartesian = geom::Vec3{x, y, z};
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3 cartesian,
                          const double *,
                          std::size_t joint_count,
                          double *joints_out) const override
    {
        if(joint_count != count_) {
            return rt::ErrorCode::invalid_argument;
        }
        const double coordinates[3] = {cartesian.x, cartesian.y, cartesian.z};
        for(std::size_t i = 0; i < count_; ++i) {
            if(scale_[i] == 0.0 || !std::isfinite(coordinates[i])) {
                return rt::ErrorCode::invalid_argument;
            }
            joints_out[i] = (coordinates[i] - offset_[i]) / scale_[i];
        }
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *, std::size_t) const override
    {
        return 1e30; // linear mechanism: no singular configurations
    }

private:
    std::size_t count_ = 0;
    double scale_[3] = {1.0, 1.0, 1.0};
    double offset_[3] = {0.0, 0.0, 0.0};
};

} // namespace plcopen::core::kin
