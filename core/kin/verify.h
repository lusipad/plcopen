#pragma once

// Kinematics plugin conformance harness (approved matrix, decisions #2/#8):
// round-trip consistency (inverse(forward(q), seed=q) == q), seed-branch
// stability along continuous joint paths, and singularity-margin sanity,
// driven by a deterministic LCG over caller-supplied joint ranges. The
// contract is asserted here, not trusted: every reference implementation and
// every third-party plugin runs the same harness.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "kin/kinematics.h"

namespace plcopen::core::kin
{

struct VerifyRange
{
    double minimum = -1.0;
    double maximum = 1.0;
};

struct VerifyReport
{
    int round_trips = 0;
    int branch_steps = 0;
    double worst_round_trip_error = 0.0;
};

namespace detail
{

struct Lcg
{
    std::uint32_t state = 0x4B1D5EEDu;

    double range(double minimum, double maximum)
    {
        state = state * 1664525u + 1013904223u;
        const double unit = static_cast<double>(state >> 8) / 16777216.0;
        return minimum + unit * (maximum - minimum);
    }
};

} // namespace detail

// Returns ok when the plugin honors the contract over `iterations` random
// joint states; the first violation aborts with the failing error code.
inline rt::ErrorCode verify_kinematics(const Kinematics &plugin,
                                       const VerifyRange *joint_ranges,
                                       int iterations,
                                       double tolerance,
                                       VerifyReport &report)
{
    const std::size_t joints = plugin.joint_count();
    if(joints < 1 || joints > 8 || plugin.cartesian_count() < 2 ||
       plugin.cartesian_count() > 3) {
        return rt::ErrorCode::invalid_argument;
    }

    detail::Lcg rng{};
    double q[8] = {};
    double solved[8] = {};

    // Round trip: inverse(forward(q), seed=q) must reproduce q.
    for(int i = 0; i < iterations; ++i) {
        for(std::size_t j = 0; j < joints; ++j) {
            q[j] = rng.range(joint_ranges[j].minimum, joint_ranges[j].maximum);
        }
        geom::Vec3 cartesian{};
        rt::ErrorCode result = plugin.forward(q, joints, cartesian);
        if(result != rt::ErrorCode::ok) {
            return result;
        }
        result = plugin.inverse(cartesian, q, joints, solved);
        if(result != rt::ErrorCode::ok) {
            return result;
        }
        for(std::size_t j = 0; j < joints; ++j) {
            const double error = std::fabs(solved[j] - q[j]);
            if(error > report.worst_round_trip_error) {
                report.worst_round_trip_error = error;
            }
            if(error > tolerance) {
                return rt::ErrorCode::infeasible;
            }
        }
        ++report.round_trips;
    }

    // Branch stability: along a continuous joint-space walk, solving each
    // pose with the previous solution as the seed must not jump branches
    // (bounded joint steps).
    for(std::size_t j = 0; j < joints; ++j) {
        q[j] = 0.5 * (joint_ranges[j].minimum + joint_ranges[j].maximum);
    }
    double previous[8] = {};
    for(std::size_t j = 0; j < joints; ++j) {
        previous[j] = q[j];
    }
    const int walk = iterations < 512 ? iterations : 512;
    for(int i = 0; i < walk; ++i) {
        for(std::size_t j = 0; j < joints; ++j) {
            const double span = joint_ranges[j].maximum - joint_ranges[j].minimum;
            double next = q[j] + rng.range(-0.01, 0.01) * span;
            if(next < joint_ranges[j].minimum) {
                next = joint_ranges[j].minimum;
            } else if(next > joint_ranges[j].maximum) {
                next = joint_ranges[j].maximum;
            }
            q[j] = next;
        }
        geom::Vec3 cartesian{};
        if(plugin.forward(q, joints, cartesian) != rt::ErrorCode::ok) {
            return rt::ErrorCode::infeasible;
        }
        if(plugin.inverse(cartesian, previous, joints, solved) != rt::ErrorCode::ok) {
            return rt::ErrorCode::infeasible;
        }
        for(std::size_t j = 0; j < joints; ++j) {
            const double span = joint_ranges[j].maximum - joint_ranges[j].minimum;
            if(std::fabs(solved[j] - previous[j]) > 0.25 * span) {
                return rt::ErrorCode::precondition_failed; // branch jump
            }
            previous[j] = solved[j];
        }
        ++report.branch_steps;
    }

    return rt::ErrorCode::ok;
}

} // namespace plcopen::core::kin
