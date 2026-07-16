// Readback-batch acceptance tests (approved matrix:
// doc/compliance/readback-semantics.md): group Cartesian/pose readback in
// ACS/MCS/PCS for command and actual positions — the read side mirrors the
// submit side slot for slot, the matrix-to-RPY inversion is exercised by a
// 100k-case rebuild oracle including directed gimbal-band sampling, and the
// configuration getters echo the original set values.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/group.h"
#include "geom/frame.h"
#include "kin/gantry.h"
#include "kin/wrist6r.h"

namespace
{

using namespace plcopen::core;

constexpr double Pi = 3.14159265358979323846;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance) { return std::fabs(lhs - rhs) <= tolerance; }

struct Lcg
{
    std::uint32_t state = 0x9E3779B9u;

    double range(double minimum, double maximum)
    {
        state = state * 1664525u + 1013904223u;
        return minimum + (static_cast<double>(state >> 8) / 16777216.0) * (maximum - minimum);
    }
};

// Element-wise distance between a rotation and the rebuild of extracted RPY.
double rebuild_error(const double rotation[3][3])
{
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    geom::extract_rpy(rotation, roll, pitch, yaw);
    const geom::RigidTransform rebuilt = geom::make_rpy_transform(0.0, 0.0, 0.0, roll, pitch, yaw);
    double worst = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const double difference = std::fabs(rebuilt.rotation[i][j] - rotation[i][j]);
            if (difference > worst)
            {
                worst = difference;
            }
        }
    }
    return worst;
}

// 100k random RPY triples in the regular domain: extraction ranges hold and
// the rebuilt matrix matches element-wise to 1e-12.
int check_rpy_inversion_oracle()
{
    Lcg rng;
    for (int i = 0; i < 100000; ++i)
    {
        const double roll = rng.range(-Pi + 1e-6, Pi);
        const double pitch = rng.range(-Pi / 2.0 + 0.01, Pi / 2.0 - 0.01);
        const double yaw = rng.range(-Pi + 1e-6, Pi);
        const geom::RigidTransform transform =
            geom::make_rpy_transform(0.0, 0.0, 0.0, roll, pitch, yaw);
        double out_roll = 0.0;
        double out_pitch = 0.0;
        double out_yaw = 0.0;
        const bool gimbal = geom::extract_rpy(transform.rotation, out_roll, out_pitch, out_yaw);
        if (gimbal)
        {
            return fail("rpy oracle spurious gimbal");
        }
        if (!near(out_roll, roll, 1e-9) || !near(out_pitch, pitch, 1e-9) ||
            !near(out_yaw, yaw, 1e-9))
        {
            std::printf("rpy mismatch at %d: (%.12f %.12f %.12f) vs (%.12f %.12f %.12f)\n", i,
                        out_roll, out_pitch, out_yaw, roll, pitch, yaw);
            return fail("rpy oracle identity");
        }
        if (out_pitch < -Pi / 2.0 || out_pitch > Pi / 2.0 || out_roll <= -Pi || out_roll > Pi ||
            out_yaw <= -Pi || out_yaw > Pi)
        {
            return fail("rpy oracle ranges");
        }
        if (rebuild_error(transform.rotation) > 1e-12)
        {
            return fail("rpy oracle rebuild");
        }
    }
    return 0;
}

// Directed gimbal-band sampling: at and near pitch = +/- pi/2 the declared
// convention (roll = 0, everything folded into yaw) still rebuilds the
// matrix to 1e-9, the flag fires inside the declared band and stays off
// outside it.
int check_rpy_gimbal_band()
{
    Lcg rng;
    const double offsets[] = {0.0, 1e-13, 1e-11};
    for (int sign = -1; sign <= 1; sign += 2)
    {
        for (const double offset : offsets)
        {
            for (int i = 0; i < 50; ++i)
            {
                const double roll = rng.range(-Pi + 1e-6, Pi);
                const double yaw = rng.range(-Pi + 1e-6, Pi);
                const double pitch = sign * (Pi / 2.0 - offset);
                const geom::RigidTransform transform =
                    geom::make_rpy_transform(0.0, 0.0, 0.0, roll, pitch, yaw);
                double out_roll = 0.0;
                double out_pitch = 0.0;
                double out_yaw = 0.0;
                const bool gimbal =
                    geom::extract_rpy(transform.rotation, out_roll, out_pitch, out_yaw);
                if (!gimbal)
                {
                    return fail("gimbal flag missing in band");
                }
                if (out_roll != 0.0)
                {
                    return fail("gimbal roll convention");
                }
                if (rebuild_error(transform.rotation) > 1e-9)
                {
                    return fail("gimbal rebuild");
                }
            }
        }
    }
    // Outside the band the flag stays off.
    double out_roll = 0.0;
    double out_pitch = 0.0;
    double out_yaw = 0.0;
    const geom::RigidTransform outside =
        geom::make_rpy_transform(0.0, 0.0, 0.0, 0.4, Pi / 2.0 - 1e-3, -0.7);
    if (geom::extract_rpy(outside.rotation, out_roll, out_pitch, out_yaw))
    {
        return fail("gimbal flag outside band");
    }
    return 0;
}

struct TriRig
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel z;
    axis::AxisGroup group;

    TriRig()
    {
        x.set_power(true);
        y.set_power(true);
        z.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        group.add_axis(z);
        group.enable();
    }
};

struct PoseRig
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    PoseRig()
    {
        for (auto &axis_model : axes)
        {
            axis_model.set_power(true);
            group.add_axis(axis_model);
        }
        group.enable();
    }
};

axis::GroupCommand linear_command(std::size_t size, const double *target)
{
    axis::GroupCommand command{};
    command.target.size = size;
    for (std::size_t i = 0; i < size; ++i)
    {
        command.target.value[i] = target[i];
    }
    command.velocity = 0.05;
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    return command;
}

int settle(axis::AxisGroup &group)
{
    for (int tick = 0; tick < 20000; ++tick)
    {
        group.cycle();
        if (group.status() == axis::GroupStatus::standby)
        {
            return 0;
        }
    }
    return 1;
}

// Identity translational group with a full-RPY workpiece frame and a tool
// offset: PCS readback returns the submitted local target, MCS readback the
// TCP point, mirroring the submit-side conversion (hand oracle 1e-9).
int check_translational_readback()
{
    static TriRig rig;
    if (rig.group.set_workpiece_frame_rpy(0.2, -0.1, 0.05, 0.15, -0.25, 0.6) != rt::ErrorCode::ok ||
        rig.group.set_tool_offset(0.02, -0.03, 0.05) != rt::ErrorCode::ok)
    {
        return fail("translational setup");
    }
    const geom::RigidTransform frame = geom::make_rpy_transform(0.2, -0.1, 0.05, 0.15, -0.25, 0.6);
    const geom::Vec3 local{0.4, 0.2, 0.3};
    const geom::Vec3 tcp = geom::transform_point(frame, local);

    const double target[3] = {local.x, local.y, local.z};
    axis::GroupCommand command = linear_command(3, target);
    command.coord_system = axis::CoordSystem::pcs;
    if (!rig.group.submit_linear(command) || settle(rig.group) != 0)
    {
        return fail("translational settle");
    }

    axis::GroupPosition out{};
    if (rig.group.read_cartesian(axis::CoordSystem::pcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::ok ||
        out.size != 3 || !near(out.value[0], local.x, 1e-9) || !near(out.value[1], local.y, 1e-9) ||
        !near(out.value[2], local.z, 1e-9))
    {
        return fail("translational pcs readback");
    }
    if (rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::ok ||
        !near(out.value[0], tcp.x, 1e-9) || !near(out.value[1], tcp.y, 1e-9) ||
        !near(out.value[2], tcp.z, 1e-9))
    {
        return fail("translational mcs readback");
    }
    // ACS stays the raw member slots (TCP minus the tool offset in base).
    if (rig.group.read_cartesian(axis::CoordSystem::acs, axis::PositionSource::command, out) !=
            rt::ErrorCode::ok ||
        !near(out.value[0], tcp.x - 0.02, 1e-9) || !near(out.value[1], tcp.y + 0.03, 1e-9) ||
        !near(out.value[2], tcp.z - 0.05, 1e-9))
    {
        return fail("translational acs readback");
    }
    // The actual source matches the command source once settled (the pure
    // model mirrors actual = command each cycle; set_feedback is the
    // hardware entry).
    axis::GroupPosition actual{};
    if (rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::actual, actual) !=
            rt::ErrorCode::ok ||
        !near(actual.value[0], tcp.x, 1e-9) || !near(actual.value[1], tcp.y, 1e-9) ||
        !near(actual.value[2], tcp.z, 1e-9))
    {
        return fail("translational actual readback");
    }
    return 0;
}

// Kinematics-configured group: MCS readback runs the forward solution.
int check_plugin_readback()
{
    const double scale[3] = {1.0, 1.0, 1.0};
    const double offset[3] = {0.1, -0.2, 0.3};
    static const kin::CartesianGantry gantry(3, scale, offset);
    static TriRig rig;
    if (rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok)
    {
        return fail("plugin setup");
    }
    const double target[3] = {0.5, 0.4, 0.6};
    axis::GroupCommand command = linear_command(3, target);
    command.coord_system = axis::CoordSystem::mcs;
    if (!rig.group.submit_linear(command) || settle(rig.group) != 0)
    {
        return fail("plugin settle");
    }
    axis::GroupPosition out{};
    if (rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::ok ||
        !near(out.value[0], 0.5, 1e-9) || !near(out.value[1], 0.4, 1e-9) ||
        !near(out.value[2], 0.6, 1e-9))
    {
        return fail("plugin mcs readback");
    }
    return 0;
}

// Pose group round trip: after the KB-042 pipeline settles, MCS/PCS pose
// readback reproduces the commanded target (position 1e-8, rebuilt rotation
// 1e-8), closing the command/readback loop.
int check_pose_readback()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);

    // MCS scenario, no tool: readback == commanded pose.
    {
        static PoseRig rig;
        if (rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok)
        {
            return fail("pose mcs setup");
        }
        const double target[6] = {0.35, 0.15, 0.55, 0.3, -0.5, 1.2};
        axis::GroupCommand command = linear_command(6, target);
        command.coord_system = axis::CoordSystem::mcs;
        if (!rig.group.submit_linear(command) || settle(rig.group) != 0)
        {
            return fail("pose mcs settle");
        }
        axis::GroupPosition out{};
        bool gimbal = true;
        if (rig.group.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command, out,
                                     &gimbal) != rt::ErrorCode::ok ||
            out.size != 6 || gimbal)
        {
            return fail("pose mcs readback");
        }
        if (!near(out.value[0], 0.35, 1e-8) || !near(out.value[1], 0.15, 1e-8) ||
            !near(out.value[2], 0.55, 1e-8))
        {
            return fail("pose mcs position");
        }
        const geom::RigidTransform expected =
            geom::make_rpy_transform(0.0, 0.0, 0.0, 0.3, -0.5, 1.2);
        const geom::RigidTransform rebuilt =
            geom::make_rpy_transform(0.0, 0.0, 0.0, out.value[3], out.value[4], out.value[5]);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                if (!near(rebuilt.rotation[i][j], expected.rotation[i][j], 1e-8))
                {
                    return fail("pose mcs rotation");
                }
            }
        }
    }

    // PCS scenario with workpiece frame and tool transform: readback in PCS
    // reproduces the submitted local pose.
    {
        static PoseRig rig;
        if (rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok ||
            rig.group.set_workpiece_frame_rpy(0.1, -0.05, 0.02, 0.1, 0.2, 0.3) !=
                rt::ErrorCode::ok ||
            rig.group.set_tool_transform_rpy(0.01, 0.02, 0.03, -0.2, 0.1, 0.4) != rt::ErrorCode::ok)
        {
            return fail("pose pcs setup");
        }
        const double target[6] = {0.3, 0.1, 0.5, -0.4, 0.25, 0.8};
        axis::GroupCommand command = linear_command(6, target);
        command.coord_system = axis::CoordSystem::pcs;
        if (!rig.group.submit_linear(command) || settle(rig.group) != 0)
        {
            return fail("pose pcs settle");
        }
        axis::GroupPosition out{};
        if (rig.group.read_cartesian(axis::CoordSystem::pcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::ok)
        {
            return fail("pose pcs readback");
        }
        if (!near(out.value[0], 0.3, 1e-8) || !near(out.value[1], 0.1, 1e-8) ||
            !near(out.value[2], 0.5, 1e-8))
        {
            return fail("pose pcs position");
        }
        const geom::RigidTransform expected =
            geom::make_rpy_transform(0.0, 0.0, 0.0, -0.4, 0.25, 0.8);
        const geom::RigidTransform rebuilt =
            geom::make_rpy_transform(0.0, 0.0, 0.0, out.value[3], out.value[4], out.value[5]);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                if (!near(rebuilt.rotation[i][j], expected.rotation[i][j], 1e-8))
                {
                    return fail("pose pcs rotation");
                }
            }
        }
    }
    return 0;
}

// Decision #7: configuration getters echo the original set values bitwise.
int check_config_echo()
{
    static PoseRig rig;
    if (rig.group.set_workpiece_frame_rpy(0.2, -0.1, 0.05, 0.15, -0.25, 0.6) != rt::ErrorCode::ok ||
        rig.group.set_tool_transform_rpy(0.01, 0.02, 0.03, -0.2, 0.1, 0.4) != rt::ErrorCode::ok ||
        rig.group.set_tool_offset(0.02, -0.03, 0.05) != rt::ErrorCode::ok)
    {
        return fail("echo setup");
    }
    double frame[6] = {};
    double tool[6] = {};
    rig.group.workpiece_frame_rpy(frame);
    rig.group.tool_transform_rpy(tool);
    const double expected_frame[6] = {0.2, -0.1, 0.05, 0.15, -0.25, 0.6};
    const double expected_tool[6] = {0.01, 0.02, 0.03, -0.2, 0.1, 0.4};
    for (int i = 0; i < 6; ++i)
    {
        if (frame[i] != expected_frame[i] || tool[i] != expected_tool[i])
        {
            return fail("echo values");
        }
    }
    const geom::Vec3 offset = rig.group.tool_offset();
    if (offset.x != 0.02 || offset.y != -0.03 || offset.z != 0.05)
    {
        return fail("echo tool offset");
    }
    // The legacy Z-only setter echoes as its RPY special case.
    static TriRig legacy;
    if (legacy.group.set_workpiece_frame(0.5, -0.25, 0.1, 0.7) != rt::ErrorCode::ok)
    {
        return fail("echo legacy setup");
    }
    double legacy_frame[6] = {};
    legacy.group.workpiece_frame_rpy(legacy_frame);
    if (legacy_frame[0] != 0.5 || legacy_frame[1] != -0.25 || legacy_frame[2] != 0.1 ||
        legacy_frame[3] != 0.0 || legacy_frame[4] != 0.0 || legacy_frame[5] != 0.7)
    {
        return fail("echo legacy values");
    }
    return 0;
}

// The standard FB face reads actual values by default; MCS routes through
// read_cartesian and WCS reports the error.
int check_fb_face()
{
    static TriRig rig;
    if (rig.group.set_tool_offset(0.02, -0.03, 0.05) != rt::ErrorCode::ok)
    {
        return fail("fb setup");
    }
    const double target[3] = {0.4, 0.2, 0.3};
    if (!rig.group.submit_linear(linear_command(3, target)) || settle(rig.group) != 0)
    {
        return fail("fb settle");
    }

    fb::FbGroupReadPosition read{};
    read.group_ref = &rig.group;
    read.enable = true;
    read.source = axis::GroupValueSource::actual;
    read.call();
    if (!read.valid || read.error)
    {
        return fail("fb default read");
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (read.position.value[i] != rig.group.member(i)->snapshot().actual_position)
        {
            return fail("fb default slots");
        }
    }

    read.coord_system = axis::CoordSystem::mcs;
    read.call();
    if (!read.valid || read.error || read.gimbal_lock ||
        !near(read.position.value[0], 0.4 + 0.02, 1e-9) ||
        !near(read.position.value[1], 0.2 - 0.03, 1e-9) ||
        !near(read.position.value[2], 0.3 + 0.05, 1e-9))
    {
        return fail("fb mcs read");
    }

    read.coord_system = axis::CoordSystem::wcs;
    read.call();
    if (read.valid || !read.error || read.error_id != rt::ErrorCode::unsupported)
    {
        return fail("fb wcs rejection");
    }
    return 0;
}

// Rejection rules: WCS/FCS/TCS unsupported, disabled group invalid.
int check_rejections()
{
    static TriRig rig;
    axis::GroupPosition out{};
    if (rig.group.read_cartesian(axis::CoordSystem::wcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::unsupported ||
        rig.group.read_cartesian(axis::CoordSystem::fcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::unsupported ||
        rig.group.read_cartesian(axis::CoordSystem::tcs, axis::PositionSource::command, out) !=
            rt::ErrorCode::unsupported)
    {
        return fail("reject wcs family");
    }
    static axis::AxisGroup empty;
    if (empty.read_cartesian(axis::CoordSystem::mcs, axis::PositionSource::command, out) !=
        rt::ErrorCode::invalid_argument)
    {
        return fail("reject disabled group");
    }
    return 0;
}

int check_frame_numeric_boundaries()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (int field = 0; field < 4; ++field)
    {
        double values[4] = {1.0, 2.0, 3.0, 0.5};
        values[field] = nan;
        if (geom::make_frame(values[0], values[1], values[2], values[3]))
        {
            return fail("frame rejects each nonfinite field");
        }
    }

    double identity[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    double malformed_high[3][3] = {{3.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.0, 0.0, 3.0}};
    double malformed_low[3][3] = {{-3.0, 0.0, 0.0}, {0.0, -3.0, 0.0}, {0.0, 0.0, -3.0}};
    double zero[3][3] = {};
    double axis[3]{};
    double angle = 0.0;
    geom::relative_axis_angle(identity, identity, axis, angle);
    if (angle != 0.0 || axis[2] != 1.0)
        return fail("frame zero rotation boundary");
    geom::relative_axis_angle(identity, malformed_high, axis, angle);
    if (angle != 0.0)
        return fail("frame cosine upper clamp");
    geom::relative_axis_angle(identity, malformed_low, axis, angle);
    if (!near(angle, Pi, 1e-12))
        return fail("frame cosine lower clamp");
    geom::relative_axis_angle(identity, zero, axis, angle);
    if (!std::isfinite(angle) || !std::isfinite(axis[0]) || !std::isfinite(axis[1]) ||
        !std::isfinite(axis[2]))
    {
        return fail("frame degenerate rotation stays finite");
    }
    return 0;
}

} // namespace

int main()
{
    int failures = 0;
    failures += check_rpy_inversion_oracle();
    failures += check_rpy_gimbal_band();
    failures += check_translational_readback();
    failures += check_plugin_readback();
    failures += check_pose_readback();
    failures += check_config_echo();
    failures += check_fb_face();
    failures += check_rejections();
    failures += check_frame_numeric_boundaries();
    if (failures == 0)
    {
        std::printf("readback tests passed\n");
    }
    return failures;
}
