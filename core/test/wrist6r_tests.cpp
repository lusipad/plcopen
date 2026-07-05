// BS3.5/BS3.6 acceptance tests (approved kinematics matrix follow-up
// batches): the spherical-wrist 6R analytic inverse under the round-trip
// oracle (forward -> inverse(seed) == identity), perturbed-seed branch
// stability, the no-branch-flip gate, singularity margins, and the
// dual-space Cartesian velocity limiter on the group.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "axis/group.h"
#include "kin/gantry.h"
#include "kin/wrist6r.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

struct Lcg
{
    std::uint32_t state = 0x6A09E667u;

    double range(double minimum, double maximum)
    {
        state = state * 1664525u + 1013904223u;
        return minimum + (static_cast<double>(state >> 8) / 16777216.0) *
                             (maximum - minimum);
    }
};

// Joint ranges clear of the wrist (q5), elbow (q3), and shoulder
// singularities so the round trip is exact on the seed branch.
void random_joints(Lcg &rng, double *q)
{
    q[0] = rng.range(-2.5, 2.5);
    q[1] = rng.range(0.3, 1.2);
    q[2] = rng.range(0.4, 2.4);
    q[3] = rng.range(-2.5, 2.5);
    q[4] = rng.range(0.3, 2.6);
    q[5] = rng.range(-2.5, 2.5);
}

int check_roundtrip_fuzz()
{
    const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    Lcg rng;
    double q[6];
    double solved[6];
    for(int i = 0; i < 20000; ++i) {
        random_joints(rng, q);
        kin::Pose6 pose{};
        arm.forward(q, pose);
        if(arm.inverse(pose, q, 0.5, solved) != rt::ErrorCode::ok) {
            std::printf("roundtrip infeasible at %d\n", i);
            return fail("6r roundtrip solves");
        }
        for(int j = 0; j < 6; ++j) {
            if(std::fabs(solved[j] - q[j]) > 1e-8) {
                std::printf("joint %d: %.12f vs %.12f at %d\n", j, solved[j], q[j], i);
                return fail("6r roundtrip identity");
            }
        }
    }
    return 0;
}

int check_perturbed_seed()
{
    const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    Lcg rng;
    double q[6];
    double seed[6];
    double solved[6];
    for(int i = 0; i < 5000; ++i) {
        random_joints(rng, q);
        for(int j = 0; j < 6; ++j) {
            seed[j] = q[j] + rng.range(-0.05, 0.05);
        }
        kin::Pose6 pose{};
        arm.forward(q, pose);
        if(arm.inverse(pose, seed, 0.5, solved) != rt::ErrorCode::ok) {
            return fail("6r perturbed seed solves");
        }
        for(int j = 0; j < 6; ++j) {
            if(std::fabs(solved[j] - q[j]) > 1e-8) {
                return fail("6r perturbed seed recovers");
            }
        }
    }
    return 0;
}

int check_branch_gate_and_workspace()
{
    const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    const double q[6] = {0.5, 0.8, 1.2, 0.4, 1.0, -0.3};
    kin::Pose6 pose{};
    arm.forward(q, pose);

    // A seed on the other elbow branch with a tight step bound: no implicit
    // branch flip, the call reports infeasible.
    double flipped_seed[6] = {q[0], q[1], -q[2], q[3], q[4], q[5]};
    double solved[6];
    if(arm.inverse(pose, flipped_seed, 0.3, solved) != rt::ErrorCode::infeasible) {
        return fail("6r branch gate");
    }

    // Out of the reachable workspace.
    kin::Pose6 far = pose;
    far.position[0] = 5.0;
    if(arm.inverse(far, q, 3.0, solved) != rt::ErrorCode::infeasible) {
        return fail("6r workspace rejection");
    }
    return 0;
}

int check_singularity_margin()
{
    const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    const double healthy[6] = {0.5, 0.8, 1.2, 0.4, 1.2, -0.3};
    const double wrist_singular[6] = {0.5, 0.8, 1.2, 0.4, 0.01, -0.3};
    const double elbow_singular[6] = {0.5, 0.8, 0.01, 0.4, 1.2, -0.3};
    if(!(arm.singularity_margin(healthy) > 0.3) ||
       !(arm.singularity_margin(wrist_singular) < 0.02) ||
       !(arm.singularity_margin(elbow_singular) < 0.02)) {
        return fail("6r singularity margin");
    }
    return 0;
}

// BS3.6: a gantry with scale 2 doubles the Cartesian speed per path
// parameter; the limiter must halve the effective command velocity so the
// per-cycle Cartesian displacement stays under the limit.
int check_dual_space_limit()
{
    const double scale[2] = {2.0, 2.0};
    const double offset[2] = {0.0, 0.0};
    const kin::CartesianGantry gantry(2, scale, offset);

    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    if(group.set_kinematics(&gantry) != rt::ErrorCode::ok ||
       group.set_cartesian_velocity_limit(0.05) != rt::ErrorCode::ok) {
        return fail("dual-space setup");
    }

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 1.0; // Cartesian target -> joints 0.5
    command.target.value[1] = 0.5;
    command.velocity = 0.05; // path-parameter velocity before scaling
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    command.coord_system = axis::CoordSystem::mcs;
    if(!group.submit_linear(command)) {
        return fail("dual-space submit");
    }

    double previous_x = 0.0;
    double previous_y = 0.0;
    double worst = 0.0;
    for(int tick = 0; tick < 6000 && group.status() != axis::GroupStatus::standby;
        ++tick) {
        group.cycle();
        const double cart_x = 2.0 * x.snapshot().command_position;
        const double cart_y = 2.0 * y.snapshot().command_position;
        const double step = std::sqrt((cart_x - previous_x) * (cart_x - previous_x) +
                                      (cart_y - previous_y) * (cart_y - previous_y));
        if(step > worst) {
            worst = step;
        }
        previous_x = cart_x;
        previous_y = cart_y;
    }
    if(group.status() != axis::GroupStatus::standby) {
        return fail("dual-space settle");
    }
    if(worst > 0.05 + 1e-9) {
        std::printf("worst cartesian step %.6f\n", worst);
        return fail("dual-space limit holds");
    }
    // The limiter must actually bite: without it the same command would run
    // at 2x the Cartesian speed of the path parameter (0.1 > limit).
    if(worst < 0.03) {
        return fail("dual-space limiter not overly conservative");
    }

    // Guard: the limit only changes at standby.
    axis::GroupCommand second = command;
    second.target.value[0] = 2.0;
    if(!group.submit_linear(second)) {
        return fail("dual-space second submit");
    }
    group.cycle();
    if(group.set_cartesian_velocity_limit(0.1) != rt::ErrorCode::invalid_argument) {
        return fail("dual-space standby guard");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_roundtrip_fuzz() != 0 || check_perturbed_seed() != 0 ||
       check_branch_gate_and_workspace() != 0 || check_singularity_margin() != 0 ||
       check_dual_space_limit() != 0) {
        return 1;
    }
    std::printf("PASS wrist6r tests\n");
    return 0;
}
