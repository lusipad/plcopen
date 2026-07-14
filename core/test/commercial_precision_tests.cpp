#include <algorithm>
#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
#include "geom/geometry.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name, double measured = 0.0)
{
    std::printf("FAIL %s measured=%.12g\n", name, measured);
    return 1;
}

struct Rig
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisGroup group;

    Rig()
    {
        x.set_power(true);
        y.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        group.enable();
    }
};

int run_to_standstill(axis::AxisGroup &group, int limit = 20000)
{
    for(int cycle = 0; cycle < limit; ++cycle) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return cycle + 1;
        }
    }
    return -1;
}

int check_quintic_blend_speed_ripple()
{
    const rt::Result<geom::QuinticBlendSegment> made = geom::make_quintic_blend(
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, 0.35);
    if(!made) {
        return fail("quintic blend construction");
    }

    const geom::QuinticBlendSegment &blend = made.value();
    constexpr int Samples = 256;
    const double step = blend.length / Samples;
    geom::Vec3 previous = geom::sample(blend, 0.0);
    double min_speed = step;
    double max_speed = 0.0;
    for(int sample = 1; sample <= Samples; ++sample) {
        const geom::Vec3 current = geom::sample(blend, step * sample);
        const double speed = geom::norm(current - previous);
        min_speed = std::min(min_speed, speed);
        max_speed = std::max(max_speed, speed);
        previous = current;
    }
    const double ripple = (max_speed - min_speed) / step;
    if(ripple >= 1e-3) {
        return fail("quintic blend speed ripple < 0.1%", ripple);
    }
    std::printf("quintic_blend_speed_ripple=%.9g\n", ripple);
    return 0;
}

int check_circular_speed_ripple()
{
    Rig rig;
    axis::GroupCommand approach{};
    approach.target.size = 2;
    approach.target.value[0] = 2.0;
    approach.velocity = 0.5;
    if(!rig.group.submit_linear(approach) || run_to_standstill(rig.group) < 0) {
        return fail("circular approach");
    }

    axis::GroupCommand arc{};
    arc.target.size = 2;
    arc.aux.size = 2;
    arc.aux.value[1] = 2.0;
    arc.target.value[0] = -2.0;
    arc.velocity = 0.02;
    arc.acceleration = 0.01;
    arc.deceleration = 0.01;
    arc.jerk = 0.01;
    arc.path_choice = axis::CircPathChoice::counter_clockwise;
    if(!rig.group.submit_circular(arc)) {
        return fail("circular precision command");
    }

    double previous_x = rig.x.snapshot().command_position;
    double previous_y = rig.y.snapshot().command_position;
    double min_speed = arc.velocity;
    double max_speed = 0.0;
    int samples = 0;
    for(int cycle = 0; cycle < 20000; ++cycle) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double speed = std::hypot(x - previous_x, y - previous_y);
        if(speed > arc.velocity * 0.999 && speed <= arc.velocity) {
            min_speed = std::min(min_speed, speed);
            max_speed = std::max(max_speed, speed);
            ++samples;
        }
        previous_x = x;
        previous_y = y;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(samples < 50) {
        return fail("circular cruise samples", samples);
    }
    const double ripple = (max_speed - min_speed) / arc.velocity;
    if(ripple >= 1e-3) {
        return fail("circular speed ripple < 0.1%", ripple);
    }
    std::printf("circular_speed_ripple=%.9g samples=%d\n", ripple, samples);
    return 0;
}

int check_cam_phase_error()
{
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);

    exec::CamTable<4> table;
    table.push({0.0, 0.0});
    table.push({4.0, 4.0});
    table.push({8.0, 8.0});
    axis::CamInCommand cam{};
    cam.master = &master;
    cam.table = table.view();
    cam.interpolation = exec::CamInterpolation::spline;
    if(!slave.cam_in(cam)) {
        return fail("cam precision engage");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.01;
    velocity.acceleration = 0.002;
    velocity.deceleration = 0.002;
    velocity.jerk = 0.002;
    if(!master.submit(velocity)) {
        return fail("cam precision master motion");
    }

    double previous_master = master.snapshot().command_position;
    double max_phase_cycles = 0.0;
    int moving_samples = 0;
    for(int cycle = 0; cycle < 600; ++cycle) {
        master.cycle();
        slave.cycle();
        const double master_position = master.snapshot().command_position;
        const double master_step = std::fabs(master_position - previous_master);
        if(master_step > 1e-12) {
            const double phase_cycles =
                std::fabs(slave.snapshot().command_position - master_position) / master_step;
            max_phase_cycles = std::max(max_phase_cycles, phase_cycles);
            ++moving_samples;
        }
        previous_master = master_position;
    }
    if(moving_samples < 100 || max_phase_cycles >= 1.0) {
        return fail("cam phase error < 1 cycle", max_phase_cycles);
    }
    std::printf("cam_phase_error_cycles=%.9g samples=%d\n", max_phase_cycles,
                moving_samples);
    return 0;
}

int check_blending_tolerance()
{
    constexpr double Tolerance = 0.05;
    Rig rig;
    axis::GroupCommand first{};
    first.target.size = 2;
    first.target.value[0] = 2.0;
    first.velocity = 0.02;
    first.acceleration = 0.0004;
    first.deceleration = 0.0004;
    first.jerk = 0.0004;
    if(!rig.group.submit_linear(first)) {
        return fail("blend precision first leg");
    }
    for(int cycle = 0; cycle < 5; ++cycle) {
        rig.group.cycle();
    }

    axis::GroupCommand second = first;
    second.target.value[0] = 3.73205080756888;
    second.target.value[1] = 1.0;
    second.buffer_mode = axis::BufferMode::blending_high;
    second.transition_mode = axis::TransitionMode::max_corner_deviation;
    second.transition_parameter = Tolerance;
    const rt::Result<std::uint32_t> accepted = rig.group.submit_linear(second);
    if(!accepted || rig.group.last_blend_degraded_command() == accepted.value()) {
        return fail("blend precision accepted");
    }

    double closest = 1.0;
    double minimum_corner_speed = 1.0;
    double previous_x = rig.x.snapshot().command_position;
    double previous_y = rig.y.snapshot().command_position;
    for(int cycle = 0; cycle < 20000; ++cycle) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double corner_distance = std::hypot(x - 2.0, y);
        closest = std::min(closest, corner_distance);
        if(corner_distance < 0.3) {
            minimum_corner_speed =
                std::min(minimum_corner_speed, std::hypot(x - previous_x, y - previous_y));
        }
        previous_x = x;
        previous_y = y;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby || closest > Tolerance + 0.01 ||
       closest < Tolerance * 0.8 || minimum_corner_speed <= 1e-6) {
        return fail("blending tolerance and non-stop", closest);
    }
    std::printf("blending_deviation=%.9g tolerance=%.9g utilization=%.3f%%\n", closest,
                Tolerance, closest / Tolerance * 100.0);
    return 0;
}

} // namespace

int main()
{
    if(check_quintic_blend_speed_ripple() != 0 || check_circular_speed_ripple() != 0 ||
       check_cam_phase_error() != 0 || check_blending_tolerance() != 0) {
        return 1;
    }
    std::printf("PASS commercial precision tests\n");
    return 0;
}
