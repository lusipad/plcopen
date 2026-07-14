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

double point_to_segment(geom::Vec3 p, geom::Vec3 a, geom::Vec3 b)
{
    const geom::Vec3 d = b - a;
    const double len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if(len2 <= 0.0) {
        return geom::norm(p - a);
    }
    double t = ((p.x - a.x) * d.x + (p.y - a.y) * d.y + (p.z - a.z) * d.z) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const geom::Vec3 foot{a.x + d.x * t, a.y + d.y * t, a.z + d.z * t};
    return geom::norm(p - foot);
}

// Cartesian v2-C: the fused chain rides line-corner-line inside the
// tolerance band without stopping at the corner; reflex successors degrade
// to BUFFERED (reported); committed chains are not extensible; joint-mode
// actives reject cartesian blending successors.
int check_cartesian_blend()
{
    static const kin::Scara scara(0.4, 0.3, true);
    const geom::Vec3 p0{0.35, 0.25, 0.1};
    const geom::Vec3 corner{0.15, 0.45, 0.3};
    const geom::Vec3 p2{0.0497, 0.6382, 0.4305}; // gentle ~15 degree turn
    const double tolerance = 0.02;

    static TriRig rig;
    if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
        return fail("blend setup");
    }
    const double approach[3] = {p0.x, p0.y, p0.z};
    axis::GroupCommand first = command_for(3, approach);
    first.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(first) || settle(rig.group) != 0) {
        return fail("blend approach");
    }
    const double leg[3] = {corner.x, corner.y, corner.z};
    axis::GroupCommand a = command_for(3, leg);
    a.coord_system = axis::CoordSystem::mcs;
    a.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(a)) {
        return fail("blend leg a");
    }
    for(int tick = 0; tick < 10; ++tick) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("blend still moving");
    }

    const double succ[3] = {p2.x, p2.y, p2.z};
    axis::GroupCommand b = command_for(3, succ);
    b.coord_system = axis::CoordSystem::mcs;
    b.interpolation_space = axis::InterpolationSpace::cartesian;
    b.buffer_mode = axis::BufferMode::blending_low;
    b.transition_mode = axis::TransitionMode::max_corner_deviation;
    b.transition_parameter = tolerance;
    const rt::Result<std::uint32_t> fused = rig.group.submit_linear(b);
    if(!fused) {
        return fail("blend submit");
    }
    if(rig.group.last_blend_degraded_command() == fused.value()) {
        return fail("blend unexpectedly degraded");
    }

    // v3 window: the committed geometry IS extensible now — append a third
    // gentle segment and expect acceptance (not degraded).
    const geom::Vec3 p3{-0.0123, 0.6932, 0.4865};
    axis::GroupCommand extend = b;
    extend.target.value[0] = p3.x;
    extend.target.value[1] = p3.y;
    extend.target.value[2] = p3.z;
    const rt::Result<std::uint32_t> extended = rig.group.submit_linear(extend);
    if(!extended ||
       rig.group.last_blend_degraded_command() == extended.value()) {
        return fail("window extend");
    }

    double min_speed_near_corner = 1e9;
    geom::Vec3 previous_point{};
    bool have_previous = false;
    for(int tick = 0; tick < 60000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            return fail("blend errorstop");
        }
        double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
        geom::Vec3 point{};
        if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
            return fail("blend forward");
        }
        const double off1 = point_to_segment(point, p0, corner);
        const double off2 = point_to_segment(point, corner, p2);
        const double off3 = point_to_segment(point, p2, p3);
        double off = off1 < off2 ? off1 : off2;
        off = off < off3 ? off : off3;
        if(off > tolerance + 1e-9) {
            std::printf("blend off-path %.3e at tick %d\n", off, tick);
            return fail("blend tolerance band");
        }
        if(have_previous && geom::norm(point - corner) < 0.05) {
            const double speed = geom::norm(point - previous_point);
            if(speed < min_speed_near_corner) {
                min_speed_near_corner = speed;
            }
        }
        previous_point = point;
        have_previous = true;
        if(rig.group.status() == axis::GroupStatus::standby) {
            if(geom::norm(point - p3) > 1e-8) {
                return fail("blend endpoint");
            }
            break;
        }
    }
    if(min_speed_near_corner < 1e-4) {
        std::printf("corner speed %.3e\n", min_speed_near_corner);
        return fail("blend corner stopped");
    }

    // Reflex successor degrades to BUFFERED and still completes.
    {
        static TriRig reflex_rig;
        if(reflex_rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("reflex setup");
        }
        axis::GroupCommand start = command_for(3, approach);
        start.coord_system = axis::CoordSystem::mcs;
        if(!reflex_rig.group.submit_linear(start) ||
           settle(reflex_rig.group) != 0) {
            return fail("reflex approach");
        }
        axis::GroupCommand leg_a = command_for(3, leg);
        leg_a.coord_system = axis::CoordSystem::mcs;
        leg_a.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!reflex_rig.group.submit_linear(leg_a)) {
            return fail("reflex leg");
        }
        for(int tick = 0; tick < 10; ++tick) {
            reflex_rig.group.cycle();
        }
        axis::GroupCommand back = command_for(3, approach);
        back.coord_system = axis::CoordSystem::mcs;
        back.interpolation_space = axis::InterpolationSpace::cartesian;
        back.buffer_mode = axis::BufferMode::blending_low;
        back.transition_mode = axis::TransitionMode::max_corner_deviation;
        back.transition_parameter = tolerance;
        const rt::Result<std::uint32_t> degraded =
            reflex_rig.group.submit_linear(back);
        if(!degraded ||
           reflex_rig.group.last_blend_degraded_command() != degraded.value()) {
            return fail("reflex not degraded");
        }
        if(settle(reflex_rig.group) != 0) {
            return fail("reflex settle");
        }
        double joints[3] = {reflex_rig.position(0), reflex_rig.position(1),
                            reflex_rig.position(2)};
        geom::Vec3 point{};
        if(scara.forward(joints, 3, point) != rt::ErrorCode::ok ||
           geom::norm(point - p0) > 1e-8) {
            return fail("reflex endpoint");
        }
    }

    // Mixed-mode: a joint-space active rejects a cartesian blending successor.
    {
        static TriRig mixed;
        if(mixed.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("mixed setup");
        }
        axis::GroupCommand start = command_for(3, approach);
        start.coord_system = axis::CoordSystem::mcs;
        if(!mixed.group.submit_linear(start) || settle(mixed.group) != 0) {
            return fail("mixed approach");
        }
        axis::GroupCommand joint_leg = command_for(3, leg);
        joint_leg.coord_system = axis::CoordSystem::mcs;
        if(!mixed.group.submit_linear(joint_leg)) {
            return fail("mixed leg");
        }
        for(int tick = 0; tick < 10; ++tick) {
            mixed.group.cycle();
        }
        axis::GroupCommand cart_blend = command_for(3, succ);
        cart_blend.coord_system = axis::CoordSystem::mcs;
        cart_blend.interpolation_space = axis::InterpolationSpace::cartesian;
        cart_blend.buffer_mode = axis::BufferMode::blending_low;
        cart_blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        cart_blend.transition_parameter = tolerance;
        const rt::Result<std::uint32_t> rejected =
            mixed.group.submit_linear(cart_blend);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("mixed rejection");
        }
        if(settle(mixed.group) != 0) {
            return fail("mixed settle");
        }
    }
    return 0;
}

// Cartesian v3 window: a zigzag polyline of gentle corners beats the
// full-stop baseline by the spec margin; a 90-degree corner is accepted
// (slowed at the node, not degraded); GroupStop brakes on the geometry;
// an unreachable extension rejects without disturbing the window.
int check_cartesian_window()
{
    static const kin::Scara scara(0.4, 0.3, true);
    const double tolerance = 0.02;
    constexpr int Points = 7;
    geom::Vec3 pts[Points];
    for(int k = 0; k < Points; ++k) {
        const double theta = 0.6 + 0.35 * static_cast<double>(k);
        pts[k] = geom::Vec3{0.45 * std::cos(theta), 0.45 * std::sin(theta),
                            0.1 + 0.03 * static_cast<double>(k)};
    }

    // Baseline: sequential buffered Cartesian segments (full stop at each).
    long baseline_cycles = 0;
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("window baseline setup");
        }
        const double first[3] = {pts[0].x, pts[0].y, pts[0].z};
        axis::GroupCommand approach = command_for(3, first);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) || settle(rig.group) != 0) {
            return fail("window baseline approach");
        }
        for(int k = 1; k < Points; ++k) {
            const double target[3] = {pts[k].x, pts[k].y, pts[k].z};
            axis::GroupCommand seg = command_for(3, target);
            seg.coord_system = axis::CoordSystem::mcs;
            seg.interpolation_space = axis::InterpolationSpace::cartesian;
            seg.buffer_mode = axis::BufferMode::buffered;
            if(!rig.group.submit_linear(seg)) {
                return fail("window baseline segment");
            }
        }
        for(long tick = 0; tick < 200000; ++tick) {
            rig.group.cycle();
            ++baseline_cycles;
            if(rig.group.status() == axis::GroupStatus::standby) {
                break;
            }
        }
    }

    // Probe: the same polyline as one look-ahead window.
    long window_cycles = 0;
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("window setup");
        }
        if(rig.group.set_cartesian_velocity_limit(0.009) != rt::ErrorCode::ok) {
            return fail("window Cartesian velocity limit");
        }
        const double first[3] = {pts[0].x, pts[0].y, pts[0].z};
        axis::GroupCommand approach = command_for(3, first);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) || settle(rig.group) != 0) {
            return fail("window approach");
        }
        const double leg1[3] = {pts[1].x, pts[1].y, pts[1].z};
        axis::GroupCommand seg = command_for(3, leg1);
        seg.coord_system = axis::CoordSystem::mcs;
        seg.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(seg)) {
            return fail("window first leg");
        }
        for(int tick = 0; tick < 5; ++tick) {
            rig.group.cycle();
            ++window_cycles;
        }
        for(int k = 2; k < Points; ++k) {
            const double target[3] = {pts[k].x, pts[k].y, pts[k].z};
            axis::GroupCommand blend = command_for(3, target);
            blend.coord_system = axis::CoordSystem::mcs;
            blend.interpolation_space = axis::InterpolationSpace::cartesian;
            blend.buffer_mode = axis::BufferMode::blending_low;
            blend.transition_mode = axis::TransitionMode::max_corner_deviation;
            blend.transition_parameter = tolerance;
            const rt::Result<std::uint32_t> accepted =
                rig.group.submit_linear(blend);
            if(!accepted ||
               rig.group.last_blend_degraded_command() == accepted.value()) {
                std::printf("window segment %d degraded\n", k);
                return fail("window extension degraded");
            }
        }
        // Unreachable extension rejects and leaves the window running.
        {
            const double far[3] = {0.9, 0.0, 0.0};
            axis::GroupCommand bad = command_for(3, far);
            bad.coord_system = axis::CoordSystem::mcs;
            bad.interpolation_space = axis::InterpolationSpace::cartesian;
            bad.buffer_mode = axis::BufferMode::blending_low;
            bad.transition_mode = axis::TransitionMode::max_corner_deviation;
            bad.transition_parameter = tolerance;
            const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(bad);
            if(rejected || rejected.error() != rt::ErrorCode::infeasible) {
                return fail("window unreachable extension");
            }
        }
        for(long tick = 0; tick < 200000; ++tick) {
            rig.group.cycle();
            ++window_cycles;
            if(rig.group.status() == axis::GroupStatus::errorstop) {
                return fail("window errorstop");
            }
            double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
            geom::Vec3 point{};
            if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
                return fail("window forward");
            }
            double off = 1e9;
            for(int k = 0; k + 1 < Points; ++k) {
                const double d = point_to_segment(point, pts[k], pts[k + 1]);
                off = d < off ? d : off;
            }
            if(off > tolerance + 1e-9) {
                std::printf("window off-path %.3e\n", off);
                return fail("window tolerance band");
            }
            if(rig.group.status() == axis::GroupStatus::standby) {
                if(geom::norm(point - pts[Points - 1]) > 1e-8) {
                    return fail("window endpoint");
                }
                break;
            }
        }
    }
    if(static_cast<double>(window_cycles) >
       0.8 * static_cast<double>(baseline_cycles)) {
        std::printf("window %ld vs baseline %ld cycles\n", window_cycles,
                    baseline_cycles);
        return fail("window beats baseline");
    }

    // A 90-degree corner joins the window (slowed, not degraded).
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("sharp setup");
        }
        const geom::Vec3 a{0.45, 0.15, 0.1};
        const geom::Vec3 b{0.45, 0.45, 0.1};
        const geom::Vec3 c{0.15, 0.45, 0.1};
        const double first[3] = {a.x, a.y, a.z};
        axis::GroupCommand approach = command_for(3, first);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) || settle(rig.group) != 0) {
            return fail("sharp approach");
        }
        const double leg[3] = {b.x, b.y, b.z};
        axis::GroupCommand seg = command_for(3, leg);
        seg.coord_system = axis::CoordSystem::mcs;
        seg.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(seg)) {
            return fail("sharp leg");
        }
        for(int tick = 0; tick < 5; ++tick) {
            rig.group.cycle();
        }
        const double succ[3] = {c.x, c.y, c.z};
        axis::GroupCommand blend = command_for(3, succ);
        blend.coord_system = axis::CoordSystem::mcs;
        blend.interpolation_space = axis::InterpolationSpace::cartesian;
        blend.buffer_mode = axis::BufferMode::blending_high;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.01;
        const rt::Result<std::uint32_t> accepted = rig.group.submit_linear(blend);
        if(!accepted ||
           rig.group.last_blend_degraded_command() == accepted.value()) {
            return fail("sharp corner degraded");
        }
        if(settle(rig.group) != 0) {
            return fail("sharp settle");
        }
        double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
        geom::Vec3 point{};
        if(scara.forward(joints, 3, point) != rt::ErrorCode::ok ||
           geom::norm(point - c) > 1e-8) {
            return fail("sharp endpoint");
        }
    }

    // PCS commands use the same Cartesian window machinery after the
    // workpiece transform is resolved at submit time.
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok ||
           rig.group.set_workpiece_frame(0.02, -0.01, 0.0, 0.0) != rt::ErrorCode::ok) {
            return fail("PCS window setup");
        }
        const double a[3] = {0.40, 0.10, 0.10};
        const double b[3] = {0.38, 0.18, 0.12};
        const double c[3] = {0.32, 0.24, 0.14};
        axis::GroupCommand approach = command_for(3, a);
        approach.coord_system = axis::CoordSystem::pcs;
        if(!rig.group.submit_linear(approach) || settle(rig.group) != 0) {
            return fail("PCS window approach");
        }
        axis::GroupCommand first = command_for(3, b);
        first.coord_system = axis::CoordSystem::pcs;
        first.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(first)) return fail("PCS window first leg");
        for(int tick = 0; tick < 5; ++tick) rig.group.cycle();
        axis::GroupCommand blend = command_for(3, c);
        blend.coord_system = axis::CoordSystem::pcs;
        blend.interpolation_space = axis::InterpolationSpace::cartesian;
        blend.buffer_mode = axis::BufferMode::blending_low;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.01;
        if(!rig.group.submit_linear(blend) || settle(rig.group) != 0) {
            return fail("PCS window blend");
        }
        const double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
        geom::Vec3 reached{};
        const geom::Vec3 expected{c[0] + 0.02, c[1] - 0.01, c[2]};
        if(scara.forward(joints, 3, reached) != rt::ErrorCode::ok ||
           geom::norm(reached - expected) > 1e-8) {
            return fail("PCS window endpoint");
        }
    }

    // GroupStop mid-window: controlled stop on the geometry.
    {
        static TriRig rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("stop setup");
        }
        const double first[3] = {pts[0].x, pts[0].y, pts[0].z};
        axis::GroupCommand approach = command_for(3, first);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) || settle(rig.group) != 0) {
            return fail("stop approach");
        }
        const double leg1[3] = {pts[1].x, pts[1].y, pts[1].z};
        axis::GroupCommand seg = command_for(3, leg1);
        seg.coord_system = axis::CoordSystem::mcs;
        seg.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(seg)) {
            return fail("stop leg");
        }
        for(int tick = 0; tick < 5; ++tick) {
            rig.group.cycle();
        }
        for(int k = 2; k < 5; ++k) {
            const double target[3] = {pts[k].x, pts[k].y, pts[k].z};
            axis::GroupCommand blend = command_for(3, target);
            blend.coord_system = axis::CoordSystem::mcs;
            blend.interpolation_space = axis::InterpolationSpace::cartesian;
            blend.buffer_mode = axis::BufferMode::blending_low;
            blend.transition_mode = axis::TransitionMode::max_corner_deviation;
            blend.transition_parameter = tolerance;
            if(!rig.group.submit_linear(blend)) {
                return fail("stop extension");
            }
        }
        for(int tick = 0; tick < 40; ++tick) {
            rig.group.cycle();
        }
        if(rig.group.interrupt(0.002, 0.002) != rt::ErrorCode::unsupported ||
           rig.group.status() != axis::GroupStatus::moving) {
            return fail("cartesian window rejects interrupt");
        }
        if(rig.group.stop(std::nan(""), 0.002) != rt::ErrorCode::invalid_argument ||
           rig.group.stop(0.0, 0.002) != rt::ErrorCode::invalid_argument ||
           rig.group.stop(0.002, std::nan("")) != rt::ErrorCode::invalid_argument ||
           rig.group.stop(0.002, 0.0) != rt::ErrorCode::invalid_argument ||
           rig.group.status() != axis::GroupStatus::moving) {
            return fail("cartesian window rejects invalid stop dynamics");
        }
        if(rig.group.stop(0.002, 0.002) != rt::ErrorCode::ok ||
           rig.group.status() != axis::GroupStatus::stopping) {
            return fail("stop request");
        }
        if(rig.group.stop(0.002, 0.002) != rt::ErrorCode::ok ||
           rig.group.status() != axis::GroupStatus::stopping) {
            return fail("cartesian window stop is idempotent");
        }
        for(long tick = 0; tick < 200000; ++tick) {
            rig.group.cycle();
            if(rig.group.status() == axis::GroupStatus::errorstop) {
                return fail("stop errorstop");
            }
            double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
            geom::Vec3 point{};
            if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
                return fail("stop forward");
            }
            double off = 1e9;
            for(int k = 0; k + 1 < Points; ++k) {
                const double d = point_to_segment(point, pts[k], pts[k + 1]);
                off = d < off ? d : off;
            }
            if(off > tolerance + 1e-9) {
                return fail("stop off geometry");
            }
            if(rig.group.status() == axis::GroupStatus::standby) {
                return 0;
            }
        }
        return fail("stop never settled");
    }
}

// Cartesian v2-C on the pose pipeline: the whole chain rides one geodesic —
// every mid-chain orientation shares the fixed relative rotation axis.
int check_pose_cartesian_blend()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("pose blend setup");
    }
    const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.9, 0.2};
    if(!rig.group.submit_linear(command_for(6, q0)) || settle(rig.group) != 0) {
        return fail("pose blend approach");
    }
    kin::Pose6 start_pose{};
    {
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        arm.forward(joints, start_pose);
    }
    const geom::Vec3 p0{start_pose.position[0], start_pose.position[1],
                        start_pose.position[2]};
    const geom::Vec3 corner{p0.x - 0.1, p0.y + 0.06, p0.z + 0.04};
    const geom::Vec3 p2{p0.x - 0.05, p0.y + 0.16, p0.z + 0.02};

    double c_roll = 0.0;
    double c_pitch = 0.0;
    double c_yaw = 0.0;
    geom::extract_rpy(start_pose.rotation, c_roll, c_pitch, c_yaw);
    const double leg_target[6] = {corner.x, corner.y, corner.z,
                                  c_roll, c_pitch, c_yaw};
    axis::GroupCommand a = command_for(6, leg_target);
    a.coord_system = axis::CoordSystem::mcs;
    a.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(a)) {
        return fail("pose blend leg");
    }
    for(int tick = 0; tick < 10; ++tick) {
        rig.group.cycle();
    }
    const double succ_target[6] = {p2.x, p2.y, p2.z, 0.5, -0.3, 0.9};
    const geom::RigidTransform final_tcp =
        geom::make_rpy_transform(p2.x, p2.y, p2.z, 0.5, -0.3, 0.9);
    axis::GroupCommand b = command_for(6, succ_target);
    b.coord_system = axis::CoordSystem::mcs;
    b.interpolation_space = axis::InterpolationSpace::cartesian;
    b.buffer_mode = axis::BufferMode::blending_high;
    b.transition_mode = axis::TransitionMode::max_corner_deviation;
    b.transition_parameter = 0.015;
    const rt::Result<std::uint32_t> fused = rig.group.submit_linear(b);
    if(!fused || rig.group.last_blend_degraded_command() == fused.value()) {
        return fail("pose blend submit");
    }

    // The chain geodesic axis: relative rotation from the live start.
    kin::Pose6 live_pose{};
    {
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        arm.forward(joints, live_pose);
    }
    double chain_axis[3];
    double chain_angle = 0.0;
    geom::relative_axis_angle(live_pose.rotation, final_tcp.rotation, chain_axis,
                              chain_angle);

    for(int tick = 0; tick < 120000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            return fail("pose blend errorstop");
        }
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        kin::Pose6 reached{};
        arm.forward(joints, reached);
        double axis_now[3];
        double angle_now = 0.0;
        geom::relative_axis_angle(live_pose.rotation, reached.rotation, axis_now,
                                  angle_now);
        if(angle_now > 1e-4) {
            const double align = axis_now[0] * chain_axis[0] +
                                 axis_now[1] * chain_axis[1] +
                                 axis_now[2] * chain_axis[2];
            if(align < 1.0 - 1e-6) {
                return fail("pose blend geodesic axis");
            }
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            for(int i = 0; i < 3; ++i) {
                for(int j = 0; j < 3; ++j) {
                    if(!near(reached.rotation[i][j], final_tcp.rotation[i][j],
                             1e-8)) {
                        return fail("pose blend final orientation");
                    }
                }
            }
            return 0;
        }
    }
    return fail("pose blend settle");
}

// Circumcenter of three XY points (test-side oracle).
bool circumcenter_xy(geom::Vec3 a, geom::Vec3 b, geom::Vec3 c, double &cx,
                     double &cy, double &radius)
{
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) +
                            c.x * (a.y - b.y));
    if(std::fabs(d) < 1e-12) {
        return false;
    }
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    cx = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    cy = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    radius = std::sqrt((a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy));
    return true;
}

axis::CircPathChoice derived_choice(geom::Vec3 a, geom::Vec3 b, geom::Vec3 c)
{
    const double orientation =
        (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    return orientation >= 0.0 ? axis::CircPathChoice::counter_clockwise
                              : axis::CircPathChoice::clockwise;
}

// Cartesian v2-B: a SCARA arc rides the Cartesian circle through the
// per-cycle inverse — per-cycle radius error <= 1e-8, endpoint exact.
int check_scara_cartesian_arc()
{
    static const kin::Scara scara(0.4, 0.3, true);
    static TriRig rig;
    if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
        return fail("cart arc setup");
    }
    const geom::Vec3 p0{0.35, 0.25, 0.1};
    const geom::Vec3 aux{0.28, 0.36, 0.2};
    const geom::Vec3 p1{0.15, 0.45, 0.3};
    const double approach[3] = {p0.x, p0.y, p0.z};
    axis::GroupCommand first = command_for(3, approach);
    first.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(first) || settle(rig.group) != 0) {
        return fail("cart arc approach");
    }
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    if(!circumcenter_xy(p0, aux, p1, cx, cy, radius)) {
        return fail("cart arc oracle degenerate");
    }

    axis::GroupCommand arc{};
    arc.target.size = 3;
    arc.aux.size = 3;
    arc.target.value[0] = p1.x;
    arc.target.value[1] = p1.y;
    arc.target.value[2] = p1.z;
    arc.aux.value[0] = aux.x;
    arc.aux.value[1] = aux.y;
    arc.aux.value[2] = aux.z;
    arc.velocity = 0.01;
    arc.acceleration = 0.002;
    arc.deceleration = 0.002;
    arc.jerk = 0.002;
    arc.coord_system = axis::CoordSystem::mcs;
    arc.interpolation_space = axis::InterpolationSpace::cartesian;
    arc.path_choice = derived_choice(p0, aux, p1);
    if(!rig.group.submit_circular(arc)) {
        return fail("cart arc submit");
    }
    for(int tick = 0; tick < 60000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            return fail("cart arc errorstop");
        }
        double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
        geom::Vec3 point{};
        if(scara.forward(joints, 3, point) != rt::ErrorCode::ok) {
            return fail("cart arc forward");
        }
        const double r = std::sqrt((point.x - cx) * (point.x - cx) +
                                   (point.y - cy) * (point.y - cy));
        if(std::fabs(r - radius) > 1e-8) {
            std::printf("radius error %.3e at tick %d\n", r - radius, tick);
            return fail("cart arc radius");
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            if(!near(point.x, p1.x, 1e-8) || !near(point.y, p1.y, 1e-8) ||
               !near(point.z, p1.z, 1e-8)) {
                return fail("cart arc endpoint");
            }
            return 0;
        }
    }
    return fail("cart arc settle");
}

int check_buffered_scara_cartesian_arc()
{
    static const kin::Scara scara(0.4, 0.3, true);
    static TriRig rig;
    if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
        return fail("buffered cart arc setup");
    }
    const geom::Vec3 p0{0.42, 0.12, 0.10};
    const geom::Vec3 start{0.36, 0.22, 0.14};
    const geom::Vec3 via{0.29, 0.31, 0.18};
    const geom::Vec3 finish{0.20, 0.36, 0.22};
    const double approach[3] = {p0.x, p0.y, p0.z};
    axis::GroupCommand initial = command_for(3, approach);
    initial.coord_system = axis::CoordSystem::mcs;
    if(!rig.group.submit_linear(initial) || settle(rig.group) != 0) {
        return fail("buffered cart arc approach");
    }
    const double line_target[3] = {start.x, start.y, start.z};
    axis::GroupCommand line = command_for(3, line_target);
    line.coord_system = axis::CoordSystem::mcs;
    line.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(line)) return fail("buffered cart arc active line");

    axis::GroupCommand arc{};
    arc.target.size = 3;
    arc.aux.size = 3;
    arc.target.value[0] = finish.x;
    arc.target.value[1] = finish.y;
    arc.target.value[2] = finish.z;
    arc.aux.value[0] = via.x;
    arc.aux.value[1] = via.y;
    arc.aux.value[2] = via.z;
    arc.velocity = 0.01;
    arc.acceleration = 0.002;
    arc.deceleration = 0.002;
    arc.jerk = 0.002;
    arc.buffer_mode = axis::BufferMode::buffered;
    arc.coord_system = axis::CoordSystem::mcs;
    arc.interpolation_space = axis::InterpolationSpace::cartesian;
    arc.path_choice = derived_choice(start, via, finish);
    if(!rig.group.submit_circular(arc) || settle(rig.group) != 0) {
        return fail("buffered cart arc queued execution");
    }
    const double joints[3] = {rig.position(0), rig.position(1), rig.position(2)};
    geom::Vec3 reached{};
    if(scara.forward(joints, 3, reached) != rt::ErrorCode::ok ||
       geom::norm(reached - finish) > 1e-8) {
        return fail("buffered cart arc endpoint");
    }
    return 0;
}

// Cartesian v2-B on the pose pipeline: position rides the circle, the
// orientation rides the start-to-target geodesic in the sweep fraction
// (independent quaternion slerp oracle).
int check_pose_cartesian_arc()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("pose arc setup");
    }
    const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.9, 0.2};
    if(!rig.group.submit_linear(command_for(6, q0)) || settle(rig.group) != 0) {
        return fail("pose arc approach");
    }
    kin::Pose6 start_pose{};
    {
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        arm.forward(joints, start_pose);
    }
    const geom::Vec3 p0{start_pose.position[0], start_pose.position[1],
                        start_pose.position[2]};
    const geom::Vec3 aux{p0.x - 0.05, p0.y + 0.06, p0.z + 0.02};
    const geom::Vec3 p1{p0.x - 0.11, p0.y + 0.08, p0.z + 0.05};
    const geom::RigidTransform target_tcp =
        geom::make_rpy_transform(p1.x, p1.y, p1.z, 0.5, -0.3, 0.9);

    axis::GroupCommand arc{};
    arc.target.size = 6;
    arc.aux.size = 6;
    arc.target.value[0] = p1.x;
    arc.target.value[1] = p1.y;
    arc.target.value[2] = p1.z;
    arc.target.value[3] = 0.5;
    arc.target.value[4] = -0.3;
    arc.target.value[5] = 0.9;
    arc.aux.value[0] = aux.x;
    arc.aux.value[1] = aux.y;
    arc.aux.value[2] = aux.z;
    arc.velocity = 0.005;
    arc.acceleration = 0.001;
    arc.deceleration = 0.001;
    arc.jerk = 0.001;
    arc.coord_system = axis::CoordSystem::mcs;
    arc.interpolation_space = axis::InterpolationSpace::cartesian;
    arc.path_choice = derived_choice(p0, aux, p1);
    if(!rig.group.submit_circular(arc)) {
        return fail("pose arc submit");
    }

    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    if(!circumcenter_xy(p0, aux, p1, cx, cy, radius)) {
        return fail("pose arc oracle degenerate");
    }
    const Quat qa = quat_from(start_pose.rotation);
    const Quat qb = quat_from(target_tcp.rotation);
    const double sweep_start = std::atan2(p0.y - cy, p0.x - cx);
    double sweep_total = std::atan2(p1.y - cy, p1.x - cx) - sweep_start;
    const double sign = derived_choice(p0, aux, p1) ==
                                axis::CircPathChoice::counter_clockwise
                            ? 1.0
                            : -1.0;
    const double two_pi = 2.0 * 3.14159265358979323846;
    while(sweep_total * sign < 0.0) {
        sweep_total += sign * two_pi;
    }

    for(int tick = 0; tick < 120000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            return fail("pose arc errorstop");
        }
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
        }
        kin::Pose6 reached{};
        arm.forward(joints, reached);
        const double r = std::sqrt((reached.position[0] - cx) *
                                       (reached.position[0] - cx) +
                                   (reached.position[1] - cy) *
                                       (reached.position[1] - cy));
        if(std::fabs(r - radius) > 1e-8) {
            return fail("pose arc radius");
        }
        double swept = std::atan2(reached.position[1] - cy,
                                  reached.position[0] - cx) -
                       sweep_start;
        while(swept * sign < -1e-9) {
            swept += sign * two_pi;
        }
        double t = sweep_total != 0.0 ? swept / sweep_total : 1.0;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        double oracle[3][3];
        slerp(qa, qb, t, oracle);
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                if(!near(reached.rotation[i][j], oracle[i][j], 1e-8)) {
                    return fail("pose arc geodesic");
                }
            }
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            return 0;
        }
    }
    return fail("pose arc settle");
}

// Cartesian arc rejection rows.
int check_cartesian_arc_rejections()
{
    static const kin::Scara scara(0.4, 0.3, true);
    static TriRig rig;
    if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
        return fail("arc reject setup");
    }
    axis::GroupCommand arc{};
    arc.target.size = 3;
    arc.aux.size = 3;
    arc.target.value[0] = 0.15;
    arc.target.value[1] = 0.45;
    arc.aux.value[0] = 0.28;
    arc.aux.value[1] = 0.36;
    arc.velocity = 0.01;
    arc.acceleration = 0.002;
    arc.deceleration = 0.002;
    arc.jerk = 0.002;
    arc.coord_system = axis::CoordSystem::mcs;
    arc.interpolation_space = axis::InterpolationSpace::cartesian;

    axis::GroupCommand relative = arc;
    relative.relative = true;
    rt::Result<std::uint32_t> rejected = rig.group.submit_circular(relative);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("arc reject relative");
    }
    axis::GroupCommand blending = arc;
    blending.buffer_mode = axis::BufferMode::blending_low;
    rejected = rig.group.submit_circular(blending);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("arc reject blending");
    }
    static TriRig identity;
    rejected = identity.group.submit_circular(arc);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("arc reject identity");
    }
    axis::GroupCommand wrong = arc;
    wrong.path_choice = axis::CircPathChoice::clockwise;
    axis::GroupCommand right = arc;
    right.path_choice = axis::CircPathChoice::counter_clockwise;
    const rt::Result<std::uint32_t> wrong_result = rig.group.submit_circular(wrong);
    const rt::Result<std::uint32_t> right_result = rig.group.submit_circular(right);
    if(bool(wrong_result) == bool(right_result)) {
        return fail("arc reject path choice");
    }
    if(settle(rig.group) != 0) {
        return fail("arc reject settle");
    }
    return 0;
}

// Wrist-singularity pass-through (approved cartesian v2-A): a Cartesian
// segment whose geodesic sweeps q5 through zero completes without
// errorstop, stays on the line, and keeps joint steps inside the gate.
int check_wrist_singularity_pass()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static PoseRig rig;
    const double gate = 1.0;
    if(rig.group.set_pose_kinematics(&arm, 0.0, gate) != rt::ErrorCode::ok) {
        return fail("wrist pass setup");
    }
    const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.4, 0.2};
    if(!rig.group.submit_linear(command_for(6, q0)) || settle(rig.group) != 0) {
        return fail("wrist pass approach");
    }
    // Target = same arm, wrist pitch mirrored: the geodesic crosses q5 = 0.
    double qt[6] = {0.3, 0.6, 1.0, -0.4, -0.4, 0.2};
    kin::Pose6 target_pose{};
    arm.forward(qt, target_pose);
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    geom::extract_rpy(target_pose.rotation, roll, pitch, yaw);
    const double target[6] = {target_pose.position[0], target_pose.position[1],
                              target_pose.position[2], roll, pitch, yaw};
    axis::GroupCommand segment = command_for(6, target);
    segment.coord_system = axis::CoordSystem::mcs;
    segment.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(segment)) {
        return fail("wrist pass submit");
    }
    kin::Pose6 start_pose{};
    arm.forward(q0, start_pose);
    const geom::Vec3 a{start_pose.position[0], start_pose.position[1],
                       start_pose.position[2]};
    const geom::Vec3 b{target_pose.position[0], target_pose.position[1],
                       target_pose.position[2]};
    double previous[6];
    for(std::size_t i = 0; i < 6; ++i) {
        previous[i] = rig.position(i);
    }
    bool crossed = false;
    for(int tick = 0; tick < 120000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::errorstop) {
            return fail("wrist pass errorstop");
        }
        double joints[6];
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = rig.position(i);
            // The singular reorientation concentrates in a measure-zero
            // parameter interval, so the 0.5 budget cannot cover it; the
            // step gate itself is the declared bound there (v2-A record).
            if(std::fabs(joints[i] - previous[i]) > gate) {
                std::printf("step joint %zu tick %d: %.6f -> %.6f (q5=%.6f)\n", i,
                            tick, previous[i], joints[i], rig.position(4));
                return fail("wrist pass joint step");
            }
            previous[i] = joints[i];
        }
        if(joints[4] < 0.0) {
            crossed = true;
        }
        kin::Pose6 reached{};
        arm.forward(joints, reached);
        const geom::Vec3 point{reached.position[0], reached.position[1],
                               reached.position[2]};
        if(cross_track(point, a, b) > 1e-8) {
            return fail("wrist pass linearity");
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            if(!crossed) {
                return fail("wrist pass never crossed");
            }
            return 0;
        }
    }
    return fail("wrist pass settle");
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
    failures += check_cartesian_blend();
    failures += check_cartesian_window();
    failures += check_pose_cartesian_blend();
    failures += check_scara_cartesian_arc();
    failures += check_buffered_scara_cartesian_arc();
    failures += check_pose_cartesian_arc();
    failures += check_cartesian_arc_rejections();
    failures += check_wrist_singularity_pass();
    failures += check_mid_segment_fault();
    failures += check_takeover_continuity();
    failures += check_rejections();
    if(failures == 0) {
        std::printf("cartesian tests passed\n");
    }
    return failures;
}
