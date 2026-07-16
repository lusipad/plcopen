#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "axis/group.h"
#include "fb/group.h"
#include "fb/path_table.h"

namespace
{

struct Lcg
{
    unsigned int state = 0xC3000068U;
    unsigned int next()
    {
        state = state * 1664525U + 1013904223U;
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
        group.add_axis(member);
        member.set_power(true);
    }
    group.enable();

    fb::FbSetCoordinateTransform set_transform;
    set_transform.group_ref = &group;
    fb::FbGroupTransformPosition transform;
    transform.group_ref = &group;
    fb::FbGroupSetPosition set_position;
    set_position.group_ref = &group;

    Lcg random{};
    const int count = iterations(argc, argv);
    for(int i = 0; i < count; ++i) {
        const unsigned int bits = random.next();
        const double finite = static_cast<double>(static_cast<int>(bits % 2001) - 1000) /
                              100.0;
        const double value = (bits & 1U) != 0
                                 ? finite
                                 : std::numeric_limits<double>::quiet_NaN();

        set_transform.execute = (bits & 2U) != 0;
        set_transform.coordinate_system = static_cast<axis::CoordSystem>(
            (bits >> 3) % 6);
        set_transform.execution_mode = static_cast<axis::ExecutionMode>(
            (bits >> 7) % 2);
        set_transform.transform.value[(bits >> 10) % 6] = value;
        set_transform.call();

        transform.enable = (bits & 4U) != 0;
        transform.source = static_cast<axis::CoordSystem>((bits >> 12) % 6);
        transform.target = static_cast<axis::CoordSystem>((bits >> 16) % 6);
        transform.position.size = (bits >> 20) % 5;
        transform.position.value[(bits >> 23) % 3] = value;
        transform.call();

        set_position.execute = (bits & 8U) != 0;
        set_position.relative = (bits & 16U) != 0;
        set_position.coordinate_system = static_cast<axis::CoordSystem>((bits >> 25) % 6);
        set_position.execution_mode = static_cast<axis::ExecutionMode>((bits >> 29) % 2);
        set_position.position.size = (bits >> 18) % 5;
        set_position.position.value[(bits >> 14) % 3] = value;
        set_position.call();
    }
    std::printf("PASS Part 4 C3 fuzz (%d inputs, zero crash)\n", count);
    return 0;
}
