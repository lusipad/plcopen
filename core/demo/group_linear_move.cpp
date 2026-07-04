#include "axis/group.h"
#include "fb/motion.h"

#include <cmath>
#include <iostream>

int main()
{
    using namespace plcopen::core;

    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);

    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);

    fb::FbGroupEnable enable;
    enable.group_ref = &group;
    enable.execute = true;
    enable.call();

    fb::FbMoveLinearAbsolute move;
    move.group_ref = &group;
    move.position.size = 2;
    move.position.value[0] = 3.0;
    move.position.value[1] = 4.0;
    move.velocity = 1.0;
    move.execute = true;
    move.call();

    for(int cycle = 0; cycle < 100 && !move.outputs.done; ++cycle) {
        group.cycle();
        move.call();
    }

    const double x_position = x.snapshot().command_position;
    const double y_position = y.snapshot().command_position;
    std::cout << "group linear demo: x=" << x_position << ", y=" << y_position << '\n';
    return move.outputs.done && std::fabs(x_position - 3.0) < 1e-8 &&
                   std::fabs(y_position - 4.0) < 1e-8
               ? 0
               : 1;
}
