#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "axis/group.h"
#include "fb/group.h"

namespace
{

struct Lcg
{
    unsigned int state = 0x4B100019u;
    unsigned int next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
};

int iterations(int argc, char **argv)
{
    int count = 2000;
    for(int i = 1; i + 1 < argc; ++i) {
        if(std::strcmp(argv[i], "--iterations") == 0) count = std::atoi(argv[i + 1]);
    }
    return count > 0 ? count : 1;
}

void exercise_group_state_machine(int count)
{
    using namespace plcopen::core;
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();

    for(int i = 0; i < count; ++i) {
        const int action = i % 20;
        if(action == 0 || action == 1) {
            axis::GroupCommand command{};
            command.target.size = 2;
            command.target.value[0] = action == 0 ? 2.0 : -1.0;
            command.target.value[1] = action == 0 ? 1.0 : 2.0;
            command.velocity = 0.25;
            command.acceleration = 0.05;
            command.deceleration = 0.05;
            command.jerk = 0.01;
            command.buffer_mode = action == 0 ? axis::BufferMode::aborting
                                               : axis::BufferMode::buffered;
            group.submit_linear(command);
        } else if(action == 2) {
            group.set_group_override(0.5);
        } else if(action == 3) {
            group.set_group_override(0.0);
        } else if(action == 4) {
            group.set_group_override(1.0);
        } else if(action == 5) {
            group.interrupt(0.05, 0.01);
        } else if(action == 8) {
            group.continue_motion();
        } else if(action == 10) {
            group.stop(0.05, 0.01);
        } else if(action == 12) {
            axes[0].trigger_error();
        } else if(action == 14) {
            group.reset();
        } else if(action == 16) {
            group.disable();
        } else if(action == 17) {
            group.enable();
        } else if(action == 18) {
            group.command_info(static_cast<std::uint32_t>(i + 1));
        } else if(action == 19) {
            group.set_window_depth(2 + static_cast<std::size_t>(i % 8));
        }
        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
    }
}

} // namespace

int main(int argc, char **argv)
{
    using namespace plcopen::core;
    axis::AxisModel axes[8];
    axis::AxisGroup group;
    for(auto &axis : axes) group.add_axis(axis);
    fb::FbGroupReadConfiguration configuration;
    configuration.group_ref = &group;
    fb::FbGroupReadPosition position;
    position.group_ref = &group;
    fb::FbGroupReadVelocity velocity;
    velocity.group_ref = &group;
    fb::FbGroupWriteParameter parameter;
    parameter.group_ref = &group;
    fb::FbGroupWriteJoggingDynamics jogging;
    jogging.group_ref = &group;
    fb::FbGroupWriteSWLimits limits;
    limits.group_ref = &group;
    Lcg random{};
    const int count = iterations(argc, argv);
    exercise_group_state_machine(count);
    for(int i = 0; i < count; ++i) {
        const unsigned int bits = random.next();
        configuration.enable = (bits & 1u) != 0;
        configuration.ident.index = (bits >> 1) % 12;
        configuration.coord_system = static_cast<axis::CoordSystem>((bits >> 5) % 8);
        configuration.call();
        position.enable = (bits & 2u) != 0;
        position.source = static_cast<axis::GroupValueSource>((bits >> 8) % 4);
        position.coord_system = static_cast<axis::CoordSystem>((bits >> 10) % 8);
        position.call();
        velocity.enable = (bits & 4u) != 0;
        velocity.source = static_cast<axis::GroupValueSource>((bits >> 13) % 4);
        velocity.coord_system = static_cast<axis::CoordSystem>((bits >> 15) % 8);
        velocity.call();
        parameter.execute = (bits & 8u) != 0;
        parameter.parameter = static_cast<axis::GroupParameter>((bits >> 18) % 4);
        parameter.value = (bits & 16u) != 0
                              ? static_cast<double>((bits >> 20) % 5)
                              : std::numeric_limits<double>::quiet_NaN();
        parameter.call();
        jogging.execute = (bits & 32u) != 0;
        jogging.value.size = (bits >> 23) % 10;
        jogging.value.path.velocity = (bits & 64u) != 0 ? 1.0 : -1.0;
        jogging.value.axis_velocity[(bits >> 27) % 8] =
            (bits & 128u) != 0 ? 2.0 : std::numeric_limits<double>::infinity();
        jogging.call();
        limits.execute = (bits & 256u) != 0;
        limits.limit_values.count = (bits >> 16) % 10;
        const std::size_t index = (bits >> 28) % 8;
        limits.limit_values.value[index].minimum = static_cast<double>(i % 7) - 3.0;
        limits.limit_values.value[index].maximum = static_cast<double>(i % 5) - 2.0;
        limits.call();
    }
    std::printf("PASS P4-B1 fuzz (%d inputs, zero crash)\n", count);
    return 0;
}
