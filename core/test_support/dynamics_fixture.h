#pragma once

// Shared H3 acceptance/benchmark fixture. Parameters are synthetic and kept
// in-repository; they do not represent a vendor robot.

#include "dyn/fixed_base_chain.h"

namespace plcopen::core::test_support
{

inline dyn::FixedBaseChainSpec eight_joint_dynamics_spec()
{
    dyn::FixedBaseChainSpec spec{};
    spec.joint_count = 8;
    for (std::size_t joint = 0; joint < spec.joint_count; ++joint)
    {
        dyn::RevoluteBody body{};
        if (joint != 0)
        {
            const double index = static_cast<double>(joint);
            body.parent_from_joint_zero =
                geom::make_rpy_transform(0.12 + 0.01 * index, 0.01 * (index - 3.0),
                                         0.015 * (index - 2.0), 0.02 * index,
                                         -0.015 * index, 0.01 * index);
        }
        switch (joint % 3)
        {
        case 0:
            body.joint_axis = {1.0, 0.0, 0.0};
            break;
        case 1:
            body.joint_axis = {0.0, 1.0, 0.0};
            break;
        default:
            body.joint_axis = {0.0, 0.0, 1.0};
            break;
        }
        const double index = static_cast<double>(joint);
        body.mass = 1.0 + 0.15 * index;
        body.center_of_mass = {0.04 + 0.002 * index, -0.01 + 0.001 * index,
                               0.015 - 0.0005 * index};
        body.inertia_com[0][0] = 0.020 + 0.0010 * index;
        body.inertia_com[1][1] = 0.024 + 0.0012 * index;
        body.inertia_com[2][2] = 0.028 + 0.0014 * index;
        body.inertia_com[0][1] = body.inertia_com[1][0] = 0.0004;
        body.inertia_com[0][2] = body.inertia_com[2][0] = -0.0003;
        body.inertia_com[1][2] = body.inertia_com[2][1] = 0.0002;
        spec.bodies[joint] = body;
    }
    return spec;
}

} // namespace plcopen::core::test_support
