#pragma once

// Reference kinematics #2 (approved matrix, decision #7): SCARA — a planar
// 2R arm (theta1, theta2) plus an optional prismatic Z joint. Closed-form
// analytic inverse with the elbow branch selected by the seed (decision #4):
// the solution keeps the seed's elbow sign; a pose only reachable on the
// other branch reports `infeasible`. Singularities sit at the stretched
// (theta2 = 0) and folded (theta2 = +-pi) configurations; the margin is the
// angular distance to the nearest one (decision #3).

#include <cmath>
#include <cstddef>

#include "kin/kinematics.h"

namespace plcopen::core::kin
{

class Scara final : public Kinematics
{
public:
    // with_z selects the 3-joint (theta1, theta2, z) variant; link lengths
    // must be positive.
    Scara(double link1, double link2, bool with_z)
        : link1_(link1)
        , link2_(link2)
        , with_z_(with_z)
    {
    }

    std::size_t joint_count() const override
    {
        return with_z_ ? 3 : 2;
    }

    std::size_t cartesian_count() const override
    {
        return with_z_ ? 3 : 2;
    }

    rt::ErrorCode forward(const double *joints,
                          std::size_t joint_count,
                          geom::Vec3 &cartesian) const override
    {
        if(joint_count != this->joint_count()) {
            return rt::ErrorCode::invalid_argument;
        }
        const double reach = joints[0] + joints[1];
        cartesian = geom::Vec3{link1_ * std::cos(joints[0]) + link2_ * std::cos(reach),
                               link1_ * std::sin(joints[0]) + link2_ * std::sin(reach),
                               with_z_ ? joints[2] : 0.0};
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3 cartesian,
                          const double *seed_joints,
                          std::size_t joint_count,
                          double *joints_out) const override
    {
        if(joint_count != this->joint_count() || !std::isfinite(cartesian.x) ||
           !std::isfinite(cartesian.y)) {
            return rt::ErrorCode::invalid_argument;
        }

        const double radius_squared = cartesian.x * cartesian.x + cartesian.y * cartesian.y;
        const double cos_theta2 =
            (radius_squared - link1_ * link1_ - link2_ * link2_) / (2.0 * link1_ * link2_);
        if(cos_theta2 > 1.0 + 1e-12 || cos_theta2 < -1.0 - 1e-12) {
            return rt::ErrorCode::infeasible; // outside the annular workspace
        }
        const double clamped = cos_theta2 > 1.0 ? 1.0 : (cos_theta2 < -1.0 ? -1.0 : cos_theta2);
        const double magnitude = std::acos(clamped);

        // Seed-branch selection: keep the seed's elbow sign; a zero seed
        // takes the positive elbow by declaration.
        const double elbow = seed_joints[1] < 0.0 ? -magnitude : magnitude;
        const double theta2 = elbow;
        double theta1 =
            std::atan2(cartesian.y, cartesian.x) -
            std::atan2(link2_ * std::sin(theta2), link1_ + link2_ * std::cos(theta2));
        // Revolute continuity: wrap theta1 into the seed's turn so the
        // atan2 branch cut never produces a 2*pi jump against the seed.
        const double two_pi = 6.28318530717958647692;
        theta1 += two_pi * std::floor((seed_joints[0] - theta1) / two_pi + 0.5);

        joints_out[0] = theta1;
        joints_out[1] = theta2;
        if(with_z_) {
            if(!std::isfinite(cartesian.z)) {
                return rt::ErrorCode::invalid_argument;
            }
            joints_out[2] = cartesian.z;
        }
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *joints, std::size_t joint_count) const override
    {
        if(joint_count != this->joint_count()) {
            return 0.0;
        }
        const double pi = 3.14159265358979323846;
        double folded = std::fmod(std::fabs(joints[1]), 2.0 * pi);
        if(folded > pi) {
            folded = 2.0 * pi - folded;
        }
        const double stretched_margin = folded;      // distance to theta2 = 0
        const double folded_margin = pi - folded;    // distance to theta2 = +-pi
        return stretched_margin < folded_margin ? stretched_margin : folded_margin;
    }

private:
    double link1_ = 1.0;
    double link2_ = 1.0;
    bool with_z_ = false;
};

} // namespace plcopen::core::kin
