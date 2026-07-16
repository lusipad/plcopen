#include <cmath>
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
    unsigned int state = 0x4B200021u;
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

} // namespace

int main(int argc, char **argv)
{
    using namespace plcopen::core;
    axis::AxisModel axes[3];
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    axis::JoggingDynamics dynamics{};
    dynamics.size = 3;
    dynamics.path = {0.4, 0.2, 0.2, 0.1};
    for(std::size_t i = 0; i < 3; ++i) {
        dynamics.axis_velocity[i] = 0.5;
        dynamics.axis_acceleration[i] = 0.2;
        dynamics.axis_deceleration[i] = 0.2;
        dynamics.axis_jerk[i] = 0.1;
    }
    group.write_jogging_dynamics(dynamics);

    fb::FbGroupWriteToolData write_tool;
    fb::FbGroupReadToolData read_tool;
    fb::FbGroupSelectTool select_tool;
    fb::FbGroupReadTool current_tool;
    fb::FbGroupWritePayloadData write_payload;
    fb::FbGroupReadPayloadData read_payload;
    fb::FbGroupSelectPayload select_payload;
    fb::FbGroupReadPayload current_payload;
    fb::FbGroupJog jog;
    write_tool.group_ref = &group;
    read_tool.group_ref = &group;
    select_tool.group_ref = &group;
    current_tool.group_ref = &group;
    write_payload.group_ref = &group;
    read_payload.group_ref = &group;
    select_payload.group_ref = &group;
    current_payload.group_ref = &group;
    jog.group_ref = &group;

    Lcg random{};
    const int count = iterations(argc, argv);
    for(int i = 0; i < count; ++i) {
        const unsigned int bits = random.next();
        const std::size_t number = (bits >> 4) % 20;
        const double finite = static_cast<double>(static_cast<int>(bits % 17) - 8) / 10.0;
        const double special = (bits & 1u) != 0
                                   ? finite
                                   : std::numeric_limits<double>::quiet_NaN();

        write_tool.execute = (bits & 2u) != 0;
        write_tool.tool_number = number;
        write_tool.tool_data.value[(bits >> 9) % 6] = special;
        write_tool.call();
        read_tool.enable = (bits & 4u) != 0;
        read_tool.tool_number = number;
        read_tool.call();
        select_tool.execute = (bits & 8u) != 0;
        select_tool.tool_number = number;
        select_tool.call();
        current_tool.enable = (bits & 16u) != 0;
        current_tool.source = static_cast<axis::SelectionSource>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
            (bits >> 14) % 3);
        current_tool.call();

        write_payload.execute = (bits & 32u) != 0;
        write_payload.payload_number = number;
        write_payload.payload_data.center.value[(bits >> 16) % 6] = special;
        write_payload.payload_data.mass = (bits & 64u) != 0 ? std::fabs(finite) : finite;
        write_payload.payload_data.ix = std::fabs(finite);
        write_payload.payload_data.iy = std::fabs(finite);
        write_payload.payload_data.iz = std::fabs(finite);
        write_payload.call();
        read_payload.enable = (bits & 128u) != 0;
        read_payload.payload_number = number;
        read_payload.call();
        select_payload.execute = (bits & 256u) != 0;
        select_payload.payload_number = number;
        select_payload.call();
        current_payload.enable = (bits & 512u) != 0;
        current_payload.source = static_cast<axis::SelectionSource>((bits >> 20) % 3);
        current_payload.call();

        jog.enable = (bits & 1024u) != 0;
        jog.coord_system = static_cast<axis::CoordSystem>((bits >> 22) % 8);
        jog.jog_positive.count = (bits >> 25) % 5;
        jog.jog_negative.count = (bits >> 27) % 5;
        for(std::size_t slot = 0; slot < 3; ++slot) {
            jog.jog_positive.value[slot] = (bits & (1u << slot)) != 0;
            jog.jog_negative.value[slot] = (bits & (1u << (slot + 3))) != 0;
        }
        jog.call();
        group.cycle();
        for(auto &member : axes) member.cycle();
        if((bits & 0x80000000u) != 0) group.stop(0.2, 0.1);
    }
    std::printf("PASS P4-B2 fuzz (%d inputs, zero crash)\n", count);
    return 0;
}
