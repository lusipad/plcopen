#include "axis/group.h"
#include "fb/motion.h"

#if PLCOPEN_SMOKE_REQUIRE_H1
#include "stream/joint_group.h"
#define PLCOPEN_SMOKE_HAS_H1 1
#else
#define PLCOPEN_SMOKE_HAS_H1 0
#endif

#if PLCOPEN_SMOKE_REQUIRE_SERIAL_CHAIN
#include "kin/serial_chain.h"
#define PLCOPEN_SMOKE_HAS_SERIAL_CHAIN 1
#elif __has_include("kin/serial_chain.h")
#include "kin/serial_chain.h"
#define PLCOPEN_SMOKE_HAS_SERIAL_CHAIN 1
#else
#define PLCOPEN_SMOKE_HAS_SERIAL_CHAIN 0
#endif

#include <cmath>

namespace
{

bool verify_h1_joint_stream()
{
#if PLCOPEN_SMOKE_HAS_H1
    using namespace plcopen::core;

    stream::JointStreamGroupConfig config{};
    config.joint_count = 1;
    config.mode = stream::JointFrameMode::direct;
    config.gain_ramp_cycles = 2;
    config.joints[0].filter.limits = {1.0, 0.1, 0.1, 0.01};
    config.joints[0].filter.timeout_cycles = 10;
    config.joints[0].filter.extrapolation_cycles = 4;
    config.joints[0].max_abs_tau_ff = 2.0;
    config.joints[0].max_kp = 20.0;
    config.joints[0].max_kd = 5.0;
    config.joints[0].safe_kp = 1.0;
    config.joints[0].safe_kd = 0.5;

    stream::JointStreamGroup stream_group;
    if(stream_group.configure_frame(config) != rt::ErrorCode::ok ||
       stream_group.reset(0, {}) != rt::ErrorCode::ok) {
        return false;
    }

    stream::JointCommandFrame frame{};
    frame.joint_count = 1;
    frame.timestamp_cycles = 1;
    frame.joints[0] = {0.25, 0.1, 0.5, 5.0, 2.0};
    if(stream_group.push_frame(frame) != rt::ErrorCode::ok) {
        return false;
    }
    stream_group.cycle();

    const stream::JointSetpointFrame &setpoint = stream_group.read_setpoint_frame();
    return setpoint.joint_count == 1 && setpoint.frame_sequence == 1 &&
           setpoint.producer_timestamp_cycles == 1 &&
           std::fabs(setpoint.joints[0].position - 0.25) < 1e-12 &&
           std::fabs(setpoint.joints[0].velocity - 0.1) < 1e-12 &&
           std::fabs(setpoint.joints[0].tau_ff - 0.5) < 1e-12;
#else
    return true;
#endif
}

} // namespace

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

#if PLCOPEN_SMOKE_HAS_SERIAL_CHAIN
    kin::SerialChainSpec chain_spec{};
    chain_spec.joint_count = 1;
    chain_spec.links[0] = {1.0, 0.0, 0.0, 0.0, -1.0, 1.0};
    const kin::SerialChain chain(chain_spec);
    const double joint[1] = {0.25};
    kin::Pose6 pose{};
    chain.forward(joint, pose);
    if(chain.joint_count() != 1 ||
       std::fabs(pose.position[0] - std::cos(joint[0])) > 1e-12 ||
       std::fabs(pose.position[1] - std::sin(joint[0])) > 1e-12) {
        return 4;
    }
#endif

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

    return move.outputs.done && !move.outputs.error && verify_h1_joint_stream() &&
                   std::fabs(x.snapshot().command_position - 3.0) < 1e-8 &&
                   std::fabs(y.snapshot().command_position - 4.0) < 1e-8
               ? 0
               : 5;
}
