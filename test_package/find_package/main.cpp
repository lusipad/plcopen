#include "axis/group.h"
#include "fb/motion.h"

#include <cmath>

int main()
{
    using namespace plcopen::core;

    axis::AxisModel x;
    axis::AxisModel y;
    if(x.set_power(true) != rt::ErrorCode::ok || y.set_power(true) != rt::ErrorCode::ok) {
        return 1;
    }

    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok || group.add_axis(y) != rt::ErrorCode::ok) {
        return 2;
    }

    fb::FbGroupEnable enable;
    enable.group_ref = &group;
    enable.execute = true;
    enable.call();
    if(!enable.outputs.done) {
        return 3;
    }

    fb::FbMoveLinearAbsolute move;
    move.group_ref = &group;
    move.position.size = 2;
    move.position.value[0] = 3.0;
    move.position.value[1] = 4.0;
    move.velocity = 2.0;
    move.execute = true;
    move.call();
    for(int cycle = 0; cycle < 100 && !move.outputs.done; ++cycle) {
        group.cycle();
        move.call();
    }

    return move.outputs.done && !move.outputs.error &&
                   std::fabs(x.snapshot().command_position - 3.0) < 1e-8 &&
                   std::fabs(y.snapshot().command_position - 4.0) < 1e-8
               ? 0
               : 4;
}
