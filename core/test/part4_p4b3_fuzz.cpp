#include <cmath>
#include <cstdint>
#include <cstdio>

#include "axis/group.h"

namespace
{

using namespace plcopen::core;

std::uint64_t state = 0x504C434F50454EULL;

std::uint32_t next_u32()
{
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return static_cast<std::uint32_t>(state >> 16U);
}

double sample(double scale)
{
    return static_cast<double>(next_u32() % 10001U) * scale / 10000.0;
}

int fail(const char *name, int iteration)
{
    std::printf("FAIL %s iteration=%d seed=%llu\n", name, iteration,
                static_cast<unsigned long long>(state));
    return 1;
}

int fuzz_rigid_body_dynamics()
{
    axis::AxisGroup group;
    for(int iteration = 0; iteration < 2000; ++iteration) {
        axis::RigidBodyDynamics input{};
        input.count = 1U + next_u32() % input.value.size();
        for(std::size_t body = 0; body < input.count; ++body) {
            for(double &value : input.value[body].center_of_gravity.value) {
                value = sample(20.0) - 10.0;
            }
            input.value[body].mass = sample(100.0);
            input.value[body].ix = sample(10.0);
            input.value[body].iy = sample(10.0);
            input.value[body].iz = sample(10.0);
        }
        if(group.write_rigid_body_dynamics(input) != rt::ErrorCode::ok) {
            return fail("rigid write", iteration);
        }
        const rt::Result<axis::RigidBodyDynamics> output =
            group.rigid_body_dynamics();
        if(!output || output.value().count != input.count ||
           output.value().value[input.count - 1].mass !=
               input.value[input.count - 1].mass) {
            return fail("rigid readback", iteration);
        }
    }
    return 0;
}

int fuzz_conveyor_transform()
{
    axis::AxisModel members[3];
    axis::AxisModel belt;
    axis::AxisGroup group;
    belt.set_power(true);
    for(auto &member : members) {
        member.set_power(true);
        if(group.add_axis(member) != rt::ErrorCode::ok) return fail("add", 0);
    }
    if(group.enable() != rt::ErrorCode::ok) return fail("enable", 0);

    for(int iteration = 0; iteration < 2000; ++iteration) {
        const double origin = sample(100.0) - 50.0;
        const double object = sample(20.0) - 10.0;
        const double position = sample(200.0) - 100.0;
        belt.set_position(0.0);
        axis::ToolData origin_pose{};
        axis::ToolData object_pose{};
        origin_pose.value[0] = origin;
        object_pose.value[0] = object;
        const rt::Result<std::uint32_t> begun = group.track_conveyor(
            belt, origin_pose, object_pose, axis::CoordSystem::pcs,
            axis::BufferMode::aborting);
        if(!begun) return fail("conveyor begin", iteration);
        belt.set_position(position);
        group.cycle();
        double frame[6] = {};
        group.workpiece_frame_rpy(frame);
        if(!std::isfinite(frame[0]) ||
           std::fabs(frame[0] - (origin + object + position)) > 1e-10) {
            return fail("conveyor frame", iteration);
        }
    }
    return 0;
}

} // namespace

int main()
{
    if(fuzz_rigid_body_dynamics() != 0) return 1;
    if(fuzz_conveyor_transform() != 0) return 1;
    std::printf("part4 P4-B3 deterministic fuzz passed\n");
    return 0;
}
