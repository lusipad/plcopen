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

class VerifyProbe final : public kin::Kinematics
{
public:
    std::size_t joints = 2;
    std::size_t cartesian = 2;
    rt::ErrorCode forward_result = rt::ErrorCode::ok;
    rt::ErrorCode inverse_result = rt::ErrorCode::ok;
    double inverse_offset = 0.0;
    mutable int forward_calls = 0;
    mutable int inverse_calls = 0;
    int fail_forward_after = 0;
    int fail_inverse_after = 0;
    int jump_inverse_after = 0;

    std::size_t joint_count() const override { return joints; }
    std::size_t cartesian_count() const override { return cartesian; }
    rt::ErrorCode forward(const double *q, std::size_t count,
                          geom::Vec3 &point) const override
    {
        ++forward_calls;
        if(fail_forward_after > 0 && forward_calls >= fail_forward_after) {
            return rt::ErrorCode::out_of_range;
        }
        if(forward_result != rt::ErrorCode::ok) return forward_result;
        point = {q[0], count > 1 ? q[1] : 0.0, 0.0};
        return rt::ErrorCode::ok;
    }
    rt::ErrorCode inverse(geom::Vec3 point, const double *, std::size_t count,
                          double *q) const override
    {
        ++inverse_calls;
        if(fail_inverse_after > 0 && inverse_calls >= fail_inverse_after) {
            return rt::ErrorCode::out_of_range;
        }
        if(inverse_result != rt::ErrorCode::ok) return inverse_result;
        q[0] = point.x + inverse_offset +
               (jump_inverse_after > 0 && inverse_calls >= jump_inverse_after ? 1.0 : 0.0);
        if(count > 1) q[1] = point.y;
        return rt::ErrorCode::ok;
    }
    double singularity_margin(const double *, std::size_t) const override { return 1.0; }
};

int check_verify_failure_contracts()
{
    const kin::VerifyRange ranges[2] = {{-1.0, 1.0}, {-1.0, 1.0}};
    kin::VerifyReport report{};
    VerifyProbe probe;
    for(std::size_t joints : {std::size_t{0}, std::size_t{9}}) {
        probe.joints = joints;
        if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
           rt::ErrorCode::invalid_argument) {
            return fail("verify rejects joint dimensions");
        }
    }
    probe.joints = 2;
    for(std::size_t cartesian : {std::size_t{1}, std::size_t{4}}) {
        probe.cartesian = cartesian;
        if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
           rt::ErrorCode::invalid_argument) {
            return fail("verify rejects Cartesian dimensions");
        }
    }
    probe.cartesian = 2;
    probe.forward_result = rt::ErrorCode::out_of_range;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::out_of_range) {
        return fail("verify propagates forward failure");
    }
    probe.forward_result = rt::ErrorCode::ok;
    probe.inverse_result = rt::ErrorCode::precondition_failed;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::precondition_failed) {
        return fail("verify propagates inverse failure");
    }
    probe.inverse_result = rt::ErrorCode::ok;
    probe.inverse_offset = 0.5;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::infeasible) {
        return fail("verify rejects round-trip error");
    }
    probe = VerifyProbe{};
    probe.fail_forward_after = 2;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::infeasible) {
        return fail("verify rejects walk forward failure");
    }
    probe = VerifyProbe{};
    probe.fail_inverse_after = 2;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::infeasible) {
        return fail("verify rejects walk inverse failure");
    }
    probe = VerifyProbe{};
    probe.jump_inverse_after = 2;
    if(kin::verify_kinematics(probe, ranges, 1, 1e-9, report) !=
       rt::ErrorCode::precondition_failed) {
        return fail("verify rejects walk branch jump");
    }
    return 0;
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
    const kin::Scara planar(0.4, 0.3, false);
    const double planar_joints[2] = {0.2, -0.4};
    const double planar_seed[2] = {0.0, -1.0};
    double planar_out[2] = {};
    geom::Vec3 pose{};
    if(planar.joint_count() != 2 || planar.cartesian_count() != 2 ||
       planar.forward(planar_joints, 3, pose) != rt::ErrorCode::invalid_argument ||
       planar.forward(planar_joints, 2, pose) != rt::ErrorCode::ok || pose.z != 0.0 ||
       planar.inverse({NAN, 0.0, 0.0}, planar_seed, 2, planar_out) !=
           rt::ErrorCode::invalid_argument ||
       planar.inverse({0.4, 0.1, 0.0}, planar_seed, 3, planar_out) !=
           rt::ErrorCode::invalid_argument ||
       scara.inverse({0.4, 0.1, NAN}, seed, 3, joints) !=
           rt::ErrorCode::invalid_argument ||
       planar.singularity_margin(planar_joints, 3) != 0.0) {
        return fail("scara public boundary validation");
    }
    const double wrapped[2] = {0.0, 4.0};
    if(!(planar.singularity_margin(wrapped, 2) > 0.0)) {
        return fail("scara folded-angle normalization");
    }
    return 0;
}

int check_gantry_boundaries()
{
    const double scale[2] = {2.0, -0.5};
    const double offset[2] = {1.0, -1.0};
    const kin::CartesianGantry gantry(2, scale, offset);
    const double joints[2] = {2.0, 4.0};
    double solved[2] = {};
    geom::Vec3 pose{};
    if(gantry.forward(joints, 3, pose) != rt::ErrorCode::invalid_argument ||
       gantry.inverse({}, nullptr, 3, solved) != rt::ErrorCode::invalid_argument ||
       gantry.forward(joints, 2, pose) != rt::ErrorCode::ok || pose.z != 0.0 ||
       gantry.inverse(pose, nullptr, 2, solved) != rt::ErrorCode::ok ||
       !near(solved[0], joints[0], 1e-12) || !near(solved[1], joints[1], 1e-12)) {
        return fail("two-axis gantry boundaries");
    }
    const double bad_scale[2] = {1.0, 0.0};
    const kin::CartesianGantry singular(2, bad_scale, offset);
    if(singular.inverse({}, nullptr, 2, solved) != rt::ErrorCode::invalid_argument ||
       gantry.inverse({0.0, NAN, 0.0}, nullptr, 2, solved) !=
           rt::ErrorCode::invalid_argument) {
        return fail("gantry invalid inverse input");
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
       check_rejections() != 0 || check_gantry_boundaries() != 0 ||
       check_verify_failure_contracts() != 0) {
        return 1;
    }
    std::printf("PASS kinematics tests\n");
    return 0;
}
