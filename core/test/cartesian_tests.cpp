// Cartesian-interpolation acceptance tests (approved matrix:
// doc/compliance/cartesian-interpolation-semantics.md): opt-in per-cycle
// inverse kinematics on linear segments — the TCP rides a true Cartesian
// line (SCARA/6R linearity oracles), orientation rides the geodesic
// (independent quaternion-slerp oracle), joint steps stay inside the
// velocity budget, mid-segment solver failure is an explicit group
// errorstop, and the whole rejection table reports precise codes.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "geom/frame.h"
#include "kin/scara.h"
#include "kin/wrist6r.h"

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

    double position(std::size_t index) const
    {
        const axis::AxisModel *axes[3] = {&x, &y, &z};
        return axes[index]->snapshot().command_position;
    }
};

struct PoseRig
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    PoseRig()
    {
        for(auto &axis_model : axes) {
            axis_model.set_power(true);
            group.add_axis(axis_model);
        }
        group.enable();
    }

    double position(std::size_t index) const
    {
        return axes[index].snapshot().command_position;
    }
};

axis::GroupCommand command_for(std::size_t size, const double *target)
{
    axis::GroupCommand command{};
    command.target.size = size;
    for(std::size_t i = 0; i < size; ++i) {
        command.target.value[i] = target[i];
    }
    command.velocity = 0.01;
    command.acceleration = 0.002;
    command.deceleration = 0.002;
    command.jerk = 0.002;
    return command;
}

int settle(axis::AxisGroup &group)
{
    for(int tick = 0; tick < 60000; ++tick) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return 0;
        }
    }
    return 1;
}

// Distance from a point to the segment line through a/b.
double cross_track(geom::Vec3 point, geom::Vec3 a, geom::Vec3 b)
{
    const geom::Vec3 d = b - a;
    const double len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if(len2 <= 0.0) {
        return geom::norm(point - a);
    }
    const geom::Vec3 r = point - a;
    const double t = (r.x * d.x + r.y * d.y + r.z * d.z) / len2;
    const geom::Vec3 foot{a.x + d.x * t, a.y + d.y * t, a.z + d.z * t};
    return geom::norm(point - foot);
}

// Minimal quaternion utilities for the independent slerp oracle.
struct Quat
{
    double w, x, y, z;
};

Quat quat_from(const double r[3][3])
{
    Quat q{};
    const double trace = r[0][0] + r[1][1] + r[2][2];
    if(trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    } else if(r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if(r[1][1] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25 * s;
    }
    return q;
}

void quat_to_matrix(Quat q, double r[3][3])
{
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    q.w /= n;
    q.x /= n;
    q.y /= n;
    q.z /= n;
    r[0][0] = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    r[0][1] = 2.0 * (q.x * q.y - q.z * q.w);
    r[0][2] = 2.0 * (q.x * q.z + q.y * q.w);
    r[1][0] = 2.0 * (q.x * q.y + q.z * q.w);
    r[1][1] = 1.0 - 2.0 * (q.x * q.x + q.z * q.z);
    r[1][2] = 2.0 * (q.y * q.z - q.x * q.w);
    r[2][0] = 2.0 * (q.x * q.z - q.y * q.w);
    r[2][1] = 2.0 * (q.y * q.z + q.x * q.w);
    r[2][2] = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
}

void slerp(const Quat &a, Quat b, double t, double r[3][3])
{
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if(dot < 0.0) {
        b.w = -b.w;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        dot = -dot;
    }
    double wa;
    double wb;
    if(dot > 1.0 - 1e-12) {
        wa = 1.0 - t;
        wb = t;
    } else {
        const double theta = std::acos(dot);
        wa = std::sin((1.0 - t) * theta) / std::sin(theta);
        wb = std::sin(t * theta) / std::sin(theta);
    }
    const Quat mix{wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y,
                   wa * a.z + wb * b.z};
    quat_to_matrix(mix, r);
}

// SCARA linearity: with cartesian interpolation the per-cycle TCP stays on
// the commanded line to 1e-8; the default joint-space path measurably bows
// away on the same geometry (proving the flag changes the physics).
int check_scara_linearity()
{
    static const kin::Scara scara(0.4, 0.3, true);
    const geom::Vec3 p0{0.35, 0.25, 0.1};
    const geom::Vec3 p1{0.15, 0.45, 0.3};

    // Reference: joint-space bow on the same segment.
    double joint_bow = 0.0;
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("scara joint setup");
        }
        const double approach[3] = {p0.x, p0.y, p0.z};
        axis::GroupCommand first = command_for(3, approach);
        first.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(first) || settle(rig.group) != 0) {
            return fail("scara joint approach");
        }
        const double target[3] = {p1.x, p1.y, p1.z};
        axis::GroupCommand segment = command_for(3, target);
        segment.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(segment)) {
            return fail("scara joint segment");
        }
        for(int tick = 0; tick < 60000; ++tick) {
            rig.group.cycle();
            double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
            geom::Vec3 point{};
            if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
                return fail("scara joint forward");
            }
            const double bow = cross_track(point, p0, p1);
            if(bow > joint_bow) {
                joint_bow = bow;
            }
            if(rig.group.status() == axis::GroupStatus::standby) {
                break;
            }
        }
        if(joint_bow < 1e-6) {
            return fail("scara joint bow sanity");
        }
    }

    // Probe: cartesian interpolation stays on the line every cycle.
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("scara cart setup");
        }
        const double approach[3] = {p0.x, p0.y, p0.z};
        axis::GroupCommand first = command_for(3, approach);
        first.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(first) || settle(rig.group) != 0) {
            return fail("scara cart approach");
        }
        const double target[3] = {p1.x, p1.y, p1.z};
        axis::GroupCommand segment = command_for(3, target);
        segment.coord_system = axis::CoordSystem::mcs;
        segment.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(segment)) {
            return fail("scara cart segment");
        }
        bool stopped_checked = false;
        for(int tick = 0; tick < 60000; ++tick) {
            rig.group.cycle();
            double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
            geom::Vec3 point{};
            if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
                return fail("scara cart forward");
            }
            if(cross_track(point, p0, p1) > 1e-8) {
                std::printf("off line at tick %d: %.3e\n", tick,
                            cross_track(point, p0, p1));
                return fail("scara cart linearity");
            }
            // GroupStop stays on the line too (decision #10): brake once
            // mid-segment, then resume with a fresh cartesian command.
            if(!stopped_checked && tick == 15 &&
               rig.group.status() == axis::GroupStatus::moving) {
                if(rig.group.stop(0.002, 0.002) != rt::ErrorCode::ok) {
                    return fail("scara cart stop");
                }
                stopped_checked = true;
            }
            if(rig.group.status() == axis::GroupStatus::standby) {
                break;
            }
        }
        if(!stopped_checked) {
            return fail("scara cart stop never armed");
        }
        // Finish the segment after the stop.
        axis::GroupCommand resume = command_for(3, target);
        resume.coord_system = axis::CoordSystem::mcs;
        resume.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(resume) || settle(rig.group) != 0) {
            return fail("scara cart resume");
        }
        double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
        geom::Vec3 point{};
        if(scara.forward(joints, 3, point) != rt::ErrorCode::ok ||
           geom::norm(point - p1) > 1e-8) {
            return fail("scara cart endpoint");
        }
    }
    return 0;
}

// 6R pose segment: TCP position on the line, orientation on the geodesic
// (independent quaternion slerp oracle), joint steps inside the budget.
int check_pose_geodesic()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static PoseRig rig;
    const double step_gate = 0.5;
    if(rig.group.set_pose_kinematics(&arm, 0.0, step_gate) != rt::ErrorCode::ok ||
       rig.group.set_tool_transform_rpy(0.01, 0.02, 0.03, -0.2, 0.1, 0.4) !=
           rt::ErrorCode::ok) {
        return fail("pose cart setup");
    }
    const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.9, 0.2};
    if(!rig.group.submit_linear(command_for(6, q0)) || settle(rig.group) != 0) {
        return fail("pose cart approach");
    }

    // Start TCP from the settled joints; target TCP as an RPY command.
    kin::Pose6 start_flange{};
    {
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        arm.forward(joints, start_flange);
    }
    const geom::RigidTransform tool =
        geom::make_rpy_transform(0.01, 0.02, 0.03, -0.2, 0.1, 0.4);
    geom::RigidTransform start_tcp{};
    start_tcp.translation = geom::Vec3{start_flange.position[0],
                                       start_flange.position[1],
                                       start_flange.position[2]};
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            start_tcp.rotation[i][j] = start_flange.rotation[i][j];
        }
    }
    start_tcp = geom::compose(start_tcp, tool);

    const double target[6] = {0.32, 0.18, 0.5, 0.5, -0.3, 0.9};
    const geom::RigidTransform target_tcp =
        geom::make_rpy_transform(0.32, 0.18, 0.5, 0.5, -0.3, 0.9);
    axis::GroupCommand segment = command_for(6, target);
    segment.coord_system = axis::CoordSystem::mcs;
    segment.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(segment)) {
        return fail("pose cart segment");
    }

    const Quat qa = quat_from(start_tcp.rotation);
    const Quat qb = quat_from(target_tcp.rotation);
    const geom::Vec3 a = start_tcp.translation;
    const geom::Vec3 b = target_tcp.translation;
    const double total = geom::norm(b - a);

    double previous[6];
    for(std::size_t i = 0; i < 6; ++i) {
        previous[i] = rig.position(i);
    }
    for(int tick = 0; tick < 120000; ++tick) {
        rig.group.cycle();
        double joints[6];
        double worst_step = 0.0;
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
            const double step = std::fabs(joints[i] - previous[i]);
            if(step > worst_step) {
                worst_step = step;
            }
            previous[i] = joints[i];
        }
        if(worst_step > 0.5 * step_gate) {
            return fail("pose cart joint budget");
        }
        kin::Pose6 flange{};
        arm.forward(joints, flange);
        geom::RigidTransform tcp{};
        tcp.translation =
            geom::Vec3{flange.position[0], flange.position[1], flange.position[2]};
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                tcp.rotation[i][j] = flange.rotation[i][j];
            }
        }
        tcp = geom::compose(tcp, tool);
        if(cross_track(tcp.translation, a, b) > 1e-8) {
            return fail("pose cart linearity");
        }
        // Geodesic check: slerp parameter from the travelled fraction.
        const double t =
            total > 0.0 ? geom::norm(tcp.translation - a) / total : 1.0;
        double oracle[3][3];
        slerp(qa, qb, t, oracle);
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                if(!near(tcp.rotation[i][j], oracle[i][j], 1e-8)) {
                    return fail("pose cart geodesic");
                }
            }
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            if(geom::norm(tcp.translation - b) > 1e-8) {
                return fail("pose cart endpoint");
            }
            return 0;
        }
    }
    return fail("pose cart settle");
}

// Mock pose plugin whose inverse fails in a band strictly between the 33
// pre-validation samples: submit passes, the cycle path hits the band and
// the group reports the declared errorstop semantics.
class BandFailPlugin final : public kin::PoseKinematics
{
public:
    double band_low = 0.0;
    double band_high = 0.0;

    std::size_t joint_count() const override
    {
        return 6;
    }

    void forward(const double *joints, kin::Pose6 &pose) const override
    {
        pose = kin::Pose6{};
        pose.position[0] = joints[0];
        pose.position[1] = joints[1];
        pose.position[2] = joints[2];
    }

    rt::ErrorCode inverse(const kin::Pose6 &pose,
                          const double *seed,
                          double max_joint_step,
                          double *joints_out) const override
    {
        (void)seed;
        (void)max_joint_step;
        if(pose.position[0] > band_low && pose.position[0] < band_high) {
            return rt::ErrorCode::infeasible;
        }
        joints_out[0] = pose.position[0];
        joints_out[1] = pose.position[1];
        joints_out[2] = pose.position[2];
        joints_out[3] = 0.0;
        joints_out[4] = 0.0;
        joints_out[5] = 0.0;
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *) const override
    {
        return 1.0;
    }
};

int check_mid_segment_fault()
{
    static BandFailPlugin plugin;
    // Segment from x=0 to x=1: samples sit at k/32; the band hides strictly
    // between 10/32 and 11/32 and is wider than the 0.01 cruise step so the
    // cycle path cannot step over it.
    plugin.band_low = 10.0 / 32.0 + 0.002;
    plugin.band_high = 10.75 / 32.0 - 0.002;

    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&plugin, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("fault setup");
    }
    const double target[6] = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    axis::GroupCommand segment = command_for(6, target);
    segment.coord_system = axis::CoordSystem::mcs;
    segment.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(segment)) {
        return fail("fault submit (pre-validation should pass)");
    }
    double last_x = 0.0;
    for(int tick = 0; tick < 120000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            // Members hold the last good setpoint, strictly before the band.
            if(rig.position(0) >= plugin.band_low || rig.position(0) < 0.2) {
                return fail("fault hold position");
            }
            if(rig.group.last_cartesian_error() != rt::ErrorCode::infeasible) {
                return fail("fault error code");
            }
            if(rig.position(0) != last_x) {
                return fail("fault setpoint frozen");
            }
            // Recoverable through the normal reset path.
            if(rig.group.reset() != rt::ErrorCode::ok) {
                return fail("fault reset");
            }
            return 0;
        }
        last_x = rig.position(0);
    }
    return fail("fault never tripped");
}

// Takeover continuity: aborting a joint-mode motion with a cartesian
// segment (and vice versa) keeps member positions continuous — the fresh
// profile starts from rest, so per-cycle steps stay tiny across the switch.
int check_takeover_continuity()
{
    static const kin::Scara scara(0.4, 0.3, true);
    static TriRig rig;
    if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
        return fail("takeover setup");
    }
    const double approach[3] = {0.35, 0.25, 0.1};
    axis::GroupCommand first = command_for(3, approach);
    first.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(first) || settle(rig.group) != 0) {
        return fail("takeover approach");
    }

    // Joint-mode motion, aborted mid-flight by a cartesian segment.
    const double away[3] = {0.15, 0.45, 0.3};
    axis::GroupCommand joint_move = command_for(3, away);
    joint_move.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(joint_move)) {
        return fail("takeover joint move");
    }
    for(int tick = 0; tick < 30; ++tick) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("takeover still moving");
    }
    double before[3] = {rig.position(0), rig.position(1), rig.position(2)};
    const double back[3] = {0.35, 0.25, 0.1};
    axis::GroupCommand takeover = command_for(3, back);
    takeover.coord_system = axis::CoordSystem::mcs;
    takeover.interpolation_space = axis::InterpolationSpace::cartesian;
    takeover.buffer_mode = axis::BufferMode::aborting;
    if(!rig.group.submit_linear(takeover)) {
        return fail("takeover cart submit");
    }
    rig.group.cycle();
    for(std::size_t i = 0; i < 3; ++i) {
        if(std::fabs(rig.position(i) - before[i]) > 1e-3) {
            return fail("takeover continuity");
        }
    }
    if(settle(rig.group) != 0) {
        return fail("takeover settle");
    }
    return 0;
}

// The rejection table: precise codes, zero silent downgrades.
int check_rejections()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static const kin::Scara scara(0.4, 0.3, true);

    // No plugin (identity group): unsupported.
    {
        static TriRig identity;
        const double target[3] = {0.1, 0.1, 0.1};
        axis::GroupCommand command = command_for(3, target);
        command.coord_system = axis::CoordSystem::mcs;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = identity.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject identity group");
        }
    }

    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("reject rig setup");
    }
    const double good[6] = {0.35, 0.15, 0.55, 0.3, -0.5, 1.2};

    // ACS + cartesian: unsupported.
    {
        axis::GroupCommand command = command_for(6, good);
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject acs");
        }
    }
    // Relative: unsupported.
    {
        axis::GroupCommand command = command_for(6, good);
        command.coord_system = axis::CoordSystem::mcs;
        command.relative = true;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject relative");
        }
    }
    // Blending transition: unsupported.
    {
        axis::GroupCommand command = command_for(6, good);
        command.coord_system = axis::CoordSystem::mcs;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        command.buffer_mode = axis::BufferMode::blending_low;
        command.transition_mode = axis::TransitionMode::max_corner_deviation;
        command.transition_parameter = 0.005;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject blending");
        }
    }
    // Circular + cartesian: unsupported.
    {
        axis::GroupCommand arc{};
        arc.target.size = 6;
        arc.aux.size = 6;
        arc.target.value[0] = 0.2;
        arc.aux.value[0] = 0.1;
        arc.aux.value[1] = 0.1;
        arc.velocity = 0.01;
        arc.acceleration = 0.002;
        arc.deceleration = 0.002;
        arc.jerk = 0.002;
        arc.coord_system = axis::CoordSystem::acs;
        arc.path_kind = axis::GroupPathKind::circular;
        arc.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_circular(arc);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject circular");
        }
    }
    // Unreachable target: infeasible from pre-validation.
    {
        const double far[6] = {5.0, 5.0, 5.0, 0.0, 0.0, 0.0};
        axis::GroupCommand command = command_for(6, far);
        command.coord_system = axis::CoordSystem::mcs;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::infeasible) {
            return fail("reject unreachable");
        }
    }
    // Margin violation: precondition_failed.
    {
        static PoseRig margined;
        if(margined.group.set_pose_kinematics(&arm, 10.0, 3.0) != rt::ErrorCode::ok) {
            return fail("reject margin setup");
        }
        axis::GroupCommand command = command_for(6, good);
        command.coord_system = axis::CoordSystem::mcs;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = margined.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::precondition_failed) {
            return fail("reject margin");
        }
    }
    // 180-degree flip: invalid_argument (geodesic not unique).
    {
        static PoseRig flip;
        if(flip.group.set_pose_kinematics(&arm, 0.0, 6.0) != rt::ErrorCode::ok) {
            return fail("reject flip setup");
        }
        const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.9, 0.2};
        if(!flip.group.submit_linear(command_for(6, q0)) || settle(flip.group) != 0) {
            return fail("reject flip approach");
        }
        // Target = start TCP rotated exactly pi about Z, same position.
        kin::Pose6 flange{};
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = flip.group.member(i)->snapshot().command_position;
        }
        arm.forward(joints, flange);
        double roll = 0.0;
        double pitch = 0.0;
        double yaw = 0.0;
        geom::extract_rpy(flange.rotation, roll, pitch, yaw);
        geom::RigidTransform flipped = geom::compose(
            geom::make_rpy_transform(0.0, 0.0, 0.0, 0.0, 0.0, 3.14159265358979323846),
            geom::make_rpy_transform(0.0, 0.0, 0.0, roll, pitch, yaw));
        double f_roll = 0.0;
        double f_pitch = 0.0;
        double f_yaw = 0.0;
        geom::extract_rpy(flipped.rotation, f_roll, f_pitch, f_yaw);
        const double target[6] = {flange.position[0], flange.position[1],
                                  flange.position[2], f_roll, f_pitch, f_yaw};
        axis::GroupCommand command = command_for(6, target);
        command.coord_system = axis::CoordSystem::mcs;
        command.interpolation_space = axis::InterpolationSpace::cartesian;
        const rt::Result<std::uint32_t> rejected = flip.group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
            return fail("reject flip");
        }
    }
    // Default joint mode stays accepted on the same rig (regression guard).
    {
        axis::GroupCommand command = command_for(6, good);
        command.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(command) || settle(rig.group) != 0) {
            return fail("reject default-joint regression");
        }
    }
    return 0;
}

} // namespace

int main()
{
    int failures = 0;
    failures += check_scara_linearity();
    failures += check_pose_geodesic();
    failures += check_mid_segment_fault();
    failures += check_takeover_continuity();
    failures += check_rejections();
    if(failures == 0) {
        std::printf("cartesian tests passed\n");
    }
    return failures;
}
