#pragma once

// Shared non-degenerate H2 acceptance/benchmark fixture. The alternating
// twists distribute the redundant degree of freedom across the seven-link
// arm instead of adding a zero-length axis coaxial with the wrist.

#include "kin/serial_chain.h"

namespace plcopen::core::test_support
{

inline kin::SerialChainSpec seven_dof_arm_spec()
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double Lower = -2.8;
    constexpr double Upper = 2.8;

    kin::SerialChainSpec spec{};
    spec.joint_count = 7;
    spec.convention = kin::DhConvention::standard;
    spec.links[0] = {0.0, -Pi * 0.5, 0.34, 0.0, Lower, Upper};
    spec.links[1] = {0.0, Pi * 0.5, 0.0, 0.0, Lower, Upper};
    spec.links[2] = {0.0, Pi * 0.5, 0.4, 0.0, Lower, Upper};
    spec.links[3] = {0.0, -Pi * 0.5, 0.0, 0.0, Lower, Upper};
    spec.links[4] = {0.0, -Pi * 0.5, 0.4, 0.0, Lower, Upper};
    spec.links[5] = {0.0, Pi * 0.5, 0.0, 0.0, Lower, Upper};
    spec.links[6] = {0.0, 0.0, 0.126, 0.0, Lower, Upper};
    return spec;
}

} // namespace plcopen::core::test_support
