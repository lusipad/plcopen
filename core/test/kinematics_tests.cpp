// B2 kinematics plugin acceptance tests (approved kinematics matrix v1,
// BS3.2-BS3.4): the conformance harness over both reference mechanisms,
// SCARA branch/workspace/singularity semantics, and the group integration
// oracle — a kinematics-configured group must reproduce hand-solved ACS
// twins cycle by cycle, and the identity-gantry group must match the plain
// KB-036 pipeline exactly.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "kin/gantry.h"
#include "kin/scara.h"
#include "kin/verify.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int check_gantry_conformance()
{
    const double scale[3] = {2.0, -0.5, 1.25};
    const double offset[3] = {0.1, -0.2, 0.3};
    const kin::CartesianGantry gantry(3, scale, offset);
    const kin::VerifyRange ranges[3] = {{-5.0, 5.0}, {-5.0, 5.0}, {-5.0, 5.0}};
    kin::VerifyReport report{};
    if(kin::verify_kinematics(gantry, ranges, 20000, 1e-9, report) != rt::ErrorCode::ok) {
        return fail("gantry conformance");
    }
    if(report.round_trips != 20000 || report.branch_steps == 0) {
        return fail("gantry harness coverage");
    }
    return 0;
}

int check_scara_conformance()
{
    const kin::Scara scara(0.4, 0.3, true);
    // Positive-elbow branch, clear of both singularities.
    const kin::VerifyRange positive[3] = {{-2.5, 2.5}, {0.2, 2.9}, {-0.5, 0.5}};
    kin::VerifyReport report{};
    if(kin::verify_kinematics(scara, positive, 20000, 1e-9, report) != rt::ErrorCode::ok) {
        return fail("scara conformance positive elbow");
    }
    const kin::VerifyRange negative[3] = {{-2.5, 2.5}, {-2.9, -0.2}, {-0.5, 0.5}};
    kin::VerifyReport negative_report{};
    if(kin::verify_kinematics(scara, negative, 20000, 1e-9, negative_report) !=
       rt::ErrorCode::ok) {
        return fail("scara conformance negative elbow");
    }
    return 0;
}

int check_scara_semantics()
{
    const kin::Scara scara(0.4, 0.3, true);
    const double seed[3] = {0.0, 1.0, 0.0};
    double joints[3] = {};

    // Outside the annular workspace: too far and too close.
    if(scara.inverse({1.0, 0.0, 0.0}, seed, 3, joints) != rt::ErrorCode::infeasible ||
       scara.inverse({0.05, 0.0, 0.0}, seed, 3, joints) != rt::ErrorCode::infeasible) {
        return fail("scara workspace rejection");
    }

    // Singularity margin: near-stretched configurations approach zero.
    const double stretched[3] = {0.3, 0.01, 0.0};
    const double healthy[3] = {0.3, 1.5, 0.0};
    if(!(scara.singularity_margin(stretched, 3) < 0.02) ||
       !(scara.singularity_margin(healthy, 3) > 1.0)) {
        return fail("scara singularity margin");
    }
    return 0;
}

struct GroupRig
{
    axis::AxisModel a0;
    axis::AxisModel a1;
    axis::AxisModel a2;
    axis::AxisGroup group;

    GroupRig()
    {
        a0.set_power(true);
        a1.set_power(true);
        a2.set_power(true);
        group.add_axis(a0);
        group.add_axis(a1);
        group.add_axis(a2);
        group.enable();
    }

    double position(std::size_t index) const
    {
        const axis::AxisModel *axes[3] = {&a0, &a1, &a2};
        return axes[index]->snapshot().command_position;
    }
};

axis::GroupCommand linear_command(double tx, double ty, double tz)
{
    axis::GroupCommand command{};
    command.target.size = 3;
    command.target.value[0] = tx;
    command.target.value[1] = ty;
    command.target.value[2] = tz;
    command.velocity = 0.05;
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    return command;
}

int run_pair(const char *name, GroupRig &probe, GroupRig &oracle)
{
    for(int tick = 0; tick < 8000; ++tick) {
        probe.group.cycle();
        oracle.group.cycle();
        for(std::size_t i = 0; i < 3; ++i) {
            if(!near(probe.position(i), oracle.position(i), 1e-9)) {
                std::printf("FAIL %s diverges at tick %d axis %zu (%.12f vs %.12f)\n", name,
                            tick, i, probe.position(i), oracle.position(i));
                return 1;
            }
        }
        if(probe.group.status() == axis::GroupStatus::standby &&
           oracle.group.status() == axis::GroupStatus::standby) {
            return 0;
        }
    }
    std::printf("FAIL %s did not settle\n", name);
    return 1;
}

// An identity gantry must reproduce the plain KB-036 pipeline exactly.
int check_identity_gantry_equivalence()
{
    const double scale[3] = {1.0, 1.0, 1.0};
    const double offset[3] = {0.0, 0.0, 0.0};
    const kin::CartesianGantry identity(3, scale, offset);

    GroupRig probe;
    GroupRig oracle;
    if(probe.group.set_workpiece_frame(0.3, -0.2, 0.1, 0.5) != rt::ErrorCode::ok ||
       oracle.group.set_workpiece_frame(0.3, -0.2, 0.1, 0.5) != rt::ErrorCode::ok ||
       probe.group.set_tool_offset(0.02, 0.01, -0.03) != rt::ErrorCode::ok ||
       oracle.group.set_tool_offset(0.02, 0.01, -0.03) != rt::ErrorCode::ok ||
       probe.group.set_kinematics(&identity) != rt::ErrorCode::ok) {
        return fail("identity gantry setup");
    }
    axis::GroupCommand pcs = linear_command(0.8, 0.4, 0.2);
    pcs.coord_system = axis::CoordSystem::pcs;
    if(!probe.group.submit_linear(pcs) || !oracle.group.submit_linear(pcs)) {
        return fail("identity gantry submit");
    }
    return run_pair("identity gantry", probe, oracle);
}

// A scaled/offset gantry against the hand-inverted ACS twin.
int check_scaled_gantry_oracle()
{
    const double scale[3] = {2.0, 0.5, -1.0};
    const double offset[3] = {0.1, -0.1, 0.2};
    const kin::CartesianGantry gantry(3, scale, offset);

    GroupRig probe;
    if(probe.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
        return fail("scaled gantry setup");
    }
    axis::GroupCommand mcs = linear_command(1.0, 0.5, -0.4);
    mcs.coord_system = axis::CoordSystem::mcs;
    if(!probe.group.submit_linear(mcs)) {
        return fail("scaled gantry submit");
    }

    GroupRig oracle;
    if(!oracle.group.submit_linear(linear_command((1.0 - 0.1) / 2.0, (0.5 + 0.1) / 0.5,
                                                  (-0.4 - 0.2) / -1.0))) {
        return fail("scaled gantry oracle submit");
    }
    return run_pair("scaled gantry", probe, oracle);
}

// SCARA endpoints land on the hand-solved joint targets; the in-segment path
// is joint-space by the declared v1 boundary, so only endpoints compare.
int check_scara_group_endpoint()
{
    const kin::Scara scara(0.4, 0.3, true);
    GroupRig rig;
    if(rig.group.set_kinematics(&scara, 0.05) != rt::ErrorCode::ok) {
        return fail("scara group setup");
    }

    const geom::Vec3 target{0.5, 0.2, 0.1};
    axis::GroupCommand mcs = linear_command(target.x, target.y, target.z);
    mcs.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(mcs)) {
        return fail("scara group submit");
    }
    for(int tick = 0; tick < 8000 && rig.group.status() != axis::GroupStatus::standby;
        ++tick) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("scara group settle");
    }

    const double seed[3] = {0.0, 0.0, 0.0};
    double expected[3] = {};
    if(scara.inverse(target, seed, 3, expected) != rt::ErrorCode::ok) {
        return fail("scara group hand inverse");
    }
    for(std::size_t i = 0; i < 3; ++i) {
        if(!near(rig.position(i), expected[i], 1e-8)) {
            return fail("scara group endpoint");
        }
    }

    // Forward-check: the reached joints map back onto the Cartesian target.
    geom::Vec3 reached{};
    const double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
    if(scara.forward(joints, 3, reached) != rt::ErrorCode::ok ||
       !near(reached.x, target.x, 1e-8) || !near(reached.y, target.y, 1e-8) ||
       !near(reached.z, target.z, 1e-8)) {
        return fail("scara group forward check");
    }
    return 0;
}

int check_rejections()
{
    const kin::Scara scara(0.4, 0.3, true);
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    const kin::CartesianGantry two_axis(2, scale, offset);

    GroupRig rig;
    // Joint count mismatch (2-joint plugin on a 3-axis group).
    if(rig.group.set_kinematics(&two_axis) != rt::ErrorCode::invalid_argument) {
        return fail("joint count mismatch rejected");
    }
    if(rig.group.set_kinematics(&scara, 0.3) != rt::ErrorCode::ok) {
        return fail("rejection setup");
    }

    // Unreachable Cartesian target reports infeasible.
    axis::GroupCommand far = linear_command(5.0, 0.0, 0.0);
    far.coord_system = axis::CoordSystem::mcs;
    if(rig.group.submit_linear(far).error() != rt::ErrorCode::infeasible) {
        return fail("unreachable rejected");
    }

    // Near-stretched endpoint violates the singularity margin threshold.
    axis::GroupCommand singular = linear_command(0.6999, 0.0, 0.0);
    singular.coord_system = axis::CoordSystem::mcs;
    if(rig.group.submit_linear(singular).error() != rt::ErrorCode::precondition_failed) {
        return fail("singularity margin rejected");
    }

    // ACS commands bypass the plugin entirely (raw joint domain, declared).
    if(!rig.group.submit_linear(linear_command(0.2, 1.2, 0.05))) {
        return fail("acs passthrough with kinematics");
    }
    rig.group.cycle();
    // Kinematics may not change while moving.
    if(rig.group.set_kinematics(nullptr) != rt::ErrorCode::invalid_argument) {
        return fail("kinematics change rejected while moving");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_gantry_conformance() != 0 || check_scara_conformance() != 0 ||
       check_scara_semantics() != 0 || check_identity_gantry_equivalence() != 0 ||
       check_scaled_gantry_oracle() != 0 || check_scara_group_endpoint() != 0 ||
       check_rejections() != 0) {
        return 1;
    }
    std::printf("PASS kinematics tests\n");
    return 0;
}
