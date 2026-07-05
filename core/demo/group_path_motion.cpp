// Demo: Part 4 path motion on the rewrite core — a linear approach, an A4
// quintic-blended corner (MaxCornerDeviation), and an A3 three-point BORDER
// arc, driven through the PLCopen function-block facades.
//
// Prints one CSV-ish sample line every few cycles; pipe it into a plotting
// tool to see the blended corner and the arc.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "fb/motion.h"

using namespace plcopen::core;

namespace
{

int run_until_standby(axis::AxisGroup &group,
                      axis::AxisModel &x,
                      axis::AxisModel &y,
                      std::int64_t &tick,
                      int limit = 20000)
{
    for(int i = 0; i < limit; ++i) {
        group.cycle();
        ++tick;
        if(tick % 8 == 0) {
            std::printf("%lld,%.6f,%.6f\n", static_cast<long long>(tick),
                        x.snapshot().command_position, y.snapshot().command_position);
        }
        if(group.status() == axis::GroupStatus::standby) {
            return 0;
        }
    }
    return 1;
}

} // namespace

int main()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    std::int64_t tick = 0;
    std::printf("tick,x,y\n");

    // Leg 1: linear approach to (2, 0).
    fb::FbMoveLinearAbsolute leg1;
    leg1.group_ref = &group;
    leg1.position.size = 2;
    leg1.position.value[0] = 2.0;
    leg1.velocity = 0.02;
    leg1.acceleration = 0.0008;
    leg1.deceleration = 0.0008;
    leg1.jerk = 0.0008;
    leg1.execute = true;
    leg1.call();
    if(!leg1.outputs.command_accepted) {
        std::printf("demo: leg1 rejected\n");
        return 1;
    }
    for(int i = 0; i < 5; ++i) {
        group.cycle();
        ++tick;
    }

    // Leg 2: shallow corner blended with a 0.05 corner-deviation tolerance —
    // the chain passes (2, 0) without stopping (KB-031).
    fb::FbMoveLinearAbsolute leg2;
    leg2.group_ref = &group;
    leg2.position.size = 2;
    leg2.position.value[0] = 3.73205080756888; // 2 + 2*cos(30 deg)
    leg2.position.value[1] = 1.0;              // 2*sin(30 deg)
    leg2.velocity = 0.02;
    leg2.acceleration = 0.0008;
    leg2.deceleration = 0.0008;
    leg2.jerk = 0.0008;
    leg2.buffer_mode = axis::BufferMode::blending_high;
    leg2.transition_mode = axis::TransitionMode::max_corner_deviation;
    leg2.transition_parameter = 0.05;
    leg2.execute = true;
    leg2.call();
    if(!leg2.outputs.command_accepted) {
        std::printf("demo: blended leg rejected\n");
        return 1;
    }
    if(group.last_blend_degraded_command() == leg2.outputs.command_id) {
        std::printf("demo: blend degraded to buffered (still correct, corner stop)\n");
    }
    if(run_until_standby(group, x, y, tick) != 0) {
        std::printf("demo: blended chain did not finish\n");
        return 1;
    }

    // Leg 3: A3 BORDER arc back up — quarter circle around (3.732, 2.0).
    fb::FbMoveCircularAbsolute arc;
    arc.group_ref = &group;
    arc.aux_point.size = 2;
    arc.aux_point.value[0] = 3.73205080756888 + std::sqrt(0.5) - 0.0;
    arc.aux_point.value[1] = 2.0 - std::sqrt(0.5);
    arc.end_point.size = 2;
    arc.end_point.value[0] = 4.73205080756888;
    arc.end_point.value[1] = 2.0;
    arc.velocity = 0.02;
    arc.acceleration = 0.0008;
    arc.deceleration = 0.0008;
    arc.jerk = 0.0008;
    arc.path_choice = axis::CircPathChoice::counter_clockwise;
    arc.execute = true;
    arc.call();
    if(!arc.outputs.command_accepted) {
        std::printf("demo: arc rejected (error=%d)\n",
                    static_cast<int>(arc.outputs.error_id));
        return 1;
    }
    if(run_until_standby(group, x, y, tick) != 0) {
        std::printf("demo: arc did not finish\n");
        return 1;
    }

    std::printf("demo: path motion complete at (%.6f, %.6f) after %lld cycles\n",
                x.snapshot().command_position, y.snapshot().command_position,
                static_cast<long long>(tick));
    return 0;
}
