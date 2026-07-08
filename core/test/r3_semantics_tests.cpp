#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "fb/base.h"
#include "fb/basic.h"
#include "fb/motion.h"

namespace
{

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_basic_fb_contracts()
{
    using namespace plcopen::core::fb;

    RTrig rising;
    rising.clk = true;
    rising.cycle();
    if(!rising.q) {
        return fail("r_trig rising pulse");
    }
    rising.cycle();
    if(rising.q) {
        return fail("r_trig one cycle");
    }

    FTrig falling;
    falling.clk = true;
    falling.cycle();
    falling.clk = false;
    falling.cycle();
    if(!falling.q) {
        return fail("f_trig falling pulse");
    }

    SR sr;
    sr.set = true;
    sr.reset = true;
    sr.cycle();
    if(!sr.q) {
        return fail("sr set dominant");
    }
    RS rs;
    rs.set = true;
    rs.reset = true;
    rs.cycle();
    if(rs.q) {
        return fail("rs reset dominant");
    }

    TON ton;
    ton.set_cycle_time(10);
    ton.pt = 20;
    ton.in = true;
    ton.cycle();
    if(ton.q || ton.et != 10) {
        return fail("ton before pt");
    }
    ton.cycle();
    if(!ton.q || ton.et != 20) {
        return fail("ton reaches pt");
    }
    ton.in = false;
    ton.cycle();
    if(ton.q || ton.et != 0) {
        return fail("ton clears");
    }

    TOF tof;
    tof.set_cycle_time(10);
    tof.pt = 20;
    tof.in = true;
    tof.cycle();
    tof.in = false;
    tof.cycle();
    if(!tof.q || tof.et != 10) {
        return fail("tof holds");
    }
    tof.cycle();
    if(tof.q || tof.et != 20) {
        return fail("tof drops");
    }

    CTUD counter;
    counter.pv = 2;
    counter.cu = true;
    counter.cd = true;
    counter.cycle();
    if(counter.cv != 0 || !counter.qd) {
        return fail("ctud simultaneous edges cancel");
    }
    counter.cd = false;
    counter.cycle();
    counter.cu = false;
    counter.cycle();
    counter.cu = true;
    counter.cycle();
    if(counter.cv != 1 || counter.qu) {
        return fail("ctud counts after cold start");
    }

    RTC rtc;
    rtc.set_cycle_time(5);
    rtc.pdt = 100;
    rtc.enable = true;
    rtc.cycle();
    rtc.cycle();
    if(!rtc.q || rtc.dt != 105) {
        return fail("rtc advances");
    }
    rtc.enable = false;
    rtc.cycle();
    if(rtc.q || rtc.dt != 0) {
        return fail("rtc clears");
    }

    return 0;
}

int check_base_latches()
{
    using namespace plcopen::core;

    fb::ExecuteLatch execute;
    execute.cycle(true, fb::ExecuteStep::busy);
    if(!execute.busy || !execute.active) {
        return fail("execute busy");
    }
    execute.cycle(true, fb::ExecuteStep::done);
    if(!execute.done || execute.busy || execute.active) {
        return fail("execute done hold");
    }
    execute.cycle(false, fb::ExecuteStep::busy);
    if(execute.done || execute.error || execute.command_aborted) {
        return fail("execute falling edge clears");
    }
    execute.cycle(true, fb::ExecuteStep::error, rt::ErrorCode::out_of_range);
    if(!execute.error || execute.error_id != rt::ErrorCode::out_of_range || execute.busy) {
        return fail("execute error");
    }

    fb::ReadInfoLatch read;
    read.cycle(true, false, rt::ErrorCode::invalid_argument);
    if(!read.error || read.valid) {
        return fail("read-info error");
    }
    read.cycle(false, true);
    if(read.error || read.valid) {
        return fail("read-info disable clears");
    }

    fb::StartSyncPulse pulse;
    pulse.complete(true);
    if(!pulse.start_sync) {
        return fail("start-sync first pulse");
    }
    pulse.complete(true);
    if(pulse.start_sync) {
        return fail("start-sync one cycle");
    }

    return 0;
}

int check_axis_state_and_motion()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    if(axis.status() != axis::AxisStatus::disabled) {
        return fail("axis initial disabled");
    }
    if(axis.set_power(true) != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("axis power standstill");
    }

    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 4.0;
    move.velocity = 2.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted || accepted.value() == 0 || axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("axis move accepted");
    }
    for(int i = 0; i < 200 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 4.0, 1e-8)) {
        return fail("axis move complete");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = -1.0;
    velocity.velocity = 0.5;
    const double before_velocity = axis.snapshot().command_position;
    if(!axis.submit(velocity)) {
        return fail("axis velocity accepted");
    }
    axis.cycle();
    if(axis.status() != axis::AxisStatus::continuous_motion ||
       axis.snapshot().command_position >= before_velocity) {
        return fail("axis velocity direction");
    }

    axis::AxisCommand halt{};
    halt.kind = axis::CommandKind::halt;
    if(!axis.submit(halt)) {
        return fail("axis halt accepted");
    }
    // Controlled halt: the axis decelerates from the takeover velocity
    // instead of stopping within one cycle.
    axis.cycle();
    if(axis.status() != axis::AxisStatus::stopping) {
        return fail("axis halt enters stopping");
    }
    for(int i = 0; i < 100 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill) {
        return fail("axis halt standstill");
    }

    if(axis.trigger_error() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::errorstop ||
       axis.reset_error() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("axis error recovery");
    }

    return 0;
}

int check_axis_buffering_and_limits()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_velocity = 2.0;
    limits.max_acceleration = 1.0;
    limits.max_deceleration = 1.0;
    limits.max_jerk = 1.0;
    limits.max_position = 10.0;
    limits.max_position_enabled = true;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok || axis.set_power(true) != rt::ErrorCode::ok) {
        return fail("axis limits setup");
    }

    axis::AxisCommand first{};
    first.kind = axis::CommandKind::move_absolute;
    first.value = 1.0;
    first.velocity = 1.0;
    first.buffer_mode = axis::BufferMode::aborting;
    axis::AxisCommand second = first;
    second.kind = axis::CommandKind::move_relative;
    second.value = 2.0;
    second.buffer_mode = axis::BufferMode::buffered;
    axis::AxisCommand third = second;
    third.value = 3.0;
    if(!axis.submit(first) || !axis.submit(second) || !axis.submit(third)) {
        return fail("axis buffered queue");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 6.0, 1e-8)) {
        return fail("axis buffered relative endpoint");
    }

    first.value = 4.0;
    if(!axis.submit(first)) {
        return fail("axis aborting setup");
    }
    axis.cycle();
    const double abort_base = axis.snapshot().command_position;
    axis::AxisCommand aborting_relative = second;
    aborting_relative.value = 1.0;
    aborting_relative.buffer_mode = axis::BufferMode::aborting;
    if(!axis.submit(aborting_relative)) {
        return fail("axis aborting relative");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, abort_base + 1.0, 1e-8)) {
        return fail("axis aborting relative base");
    }

    axis::AxisCommand rejected = first;
    rejected.value = 11.0;
    if(axis.submit(rejected).error() != rt::ErrorCode::out_of_range) {
        return fail("axis limit rejection");
    }
    return 0;
}

int check_group_linear_contract()
{
    using namespace plcopen::core;

    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);

    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok || group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("group enable");
    }

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 3.0;
    linear.target.value[1] = 4.0;
    linear.velocity = 1.0;
    const rt::Result<std::uint32_t> accepted = group.submit_linear(linear);
    if(!accepted || accepted.value() == 0 || group.status() != axis::GroupStatus::moving) {
        return fail("group linear accepted");
    }
    bool saw_collinear = false;
    for(int i = 0; i < 20 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::moving && y.snapshot().command_position > 0.0) {
            saw_collinear =
                near(4.0 * x.snapshot().command_position,
                     3.0 * y.snapshot().command_position,
                     1e-9);
        }
    }
    if(!saw_collinear || group.status() != axis::GroupStatus::standby ||
       !near(x.snapshot().command_position, 3.0, 1e-9) ||
       !near(y.snapshot().command_position, 4.0, 1e-9)) {
        return fail("group linear completion");
    }

    // A long move so the braking distance is clearly shorter than the path.
    linear.target.value[0] = 15.0;
    linear.target.value[1] = 4.0;
    linear.buffer_mode = axis::BufferMode::aborting;
    if(!group.submit_linear(linear)) {
        return fail("group second command");
    }
    group.cycle();
    if(group.stop() != rt::ErrorCode::ok) {
        return fail("group stop accepted");
    }
    // Controlled stop: the group decelerates along the path before standby.
    if(group.status() != axis::GroupStatus::stopping) {
        return fail("group stop enters stopping");
    }
    for(int i = 0; i < 200 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
    }
    if(group.status() != axis::GroupStatus::standby) {
        return fail("group stop returns standby");
    }
    if(x.snapshot().command_position >= 14.0 || x.snapshot().command_position <= 3.0) {
        return fail("group stop halts short of the aborted target");
    }

    if(!group.submit_linear(linear)) {
        return fail("group error setup");
    }
    x.trigger_error();
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop || group.reset() != rt::ErrorCode::ok ||
       group.status() != axis::GroupStatus::standby) {
        return fail("group error reset");
    }

    return 0;
}

int check_motion_facades()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    fb::FbPower power;
    power.axis_ref = &axis;
    power.enable = true;
    power.call();
    if(!power.valid || !power.status) {
        return fail("fb power");
    }

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.execute = true;
    move.position = 2.0;
    move.velocity = 1.0;
    move.call();
    if(!move.outputs.command_accepted || move.outputs.command_id == 0) {
        return fail("fb move accepted");
    }
    for(int i = 0; i < 300 && !move.outputs.done; ++i) {
        axis.cycle();
        move.call();
    }
    if(!move.outputs.done || !near(axis.snapshot().command_position, 2.0, 1e-8)) {
        return fail("fb move done");
    }
    move.execute = false;
    move.call();
    if(move.outputs.done) {
        return fail("fb move falling edge");
    }

    fb::FbTorqueControl torque;
    torque.axis_ref = &axis;
    torque.execute = true;
    torque.torque = 3.0;
    torque.call();
    if(!torque.in_torque || !near(axis.snapshot().actual_torque, 3.0, 1e-12)) {
        return fail("fb torque");
    }
    torque.execute = false;
    torque.call();
    if(torque.in_torque || !near(axis.snapshot().actual_torque, 0.0, 1e-12)) {
        return fail("fb torque clears");
    }

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
    if(!enable.outputs.done || group.status() != axis::GroupStatus::standby) {
        return fail("fb group enable");
    }

    fb::FbMoveLinearAbsolute linear;
    linear.group_ref = &group;
    linear.execute = true;
    linear.position.size = 2;
    linear.position.value[0] = 1.0;
    linear.position.value[1] = 2.0;
    linear.call();
    if(!linear.outputs.command_accepted) {
        return fail("fb linear accepted");
    }
    for(int i = 0; i < 100 && !linear.outputs.done; ++i) {
        group.cycle();
        linear.call();
    }
    if(!linear.outputs.done || !near(x.snapshot().command_position, 1.0, 1e-9) ||
       !near(y.snapshot().command_position, 2.0, 1e-9)) {
        return fail("fb linear done");
    }

    return 0;
}

// MC_Power is level-controlled: a PLC program calls it every scan cycle.
// Holding Enable high on an already-powered axis must be a no-op — it must
// not abort the active command, and the tracking move FB must not observe a
// fabricated Done or CommandAborted.
int check_cyclic_power_keeps_motion()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    if(axis.set_power(true) != rt::ErrorCode::ok || axis.set_power(true) != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("cyclic power idempotent");
    }

    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 4.0;
    move.velocity = 0.2;
    move.acceleration = 0.1;
    move.deceleration = 0.1;
    move.jerk = 0.1;
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted) {
        return fail("cyclic power move accepted");
    }
    axis.cycle();
    if(axis.set_power(true) != rt::ErrorCode::ok ||
       axis.snapshot().active_command_id != accepted.value() ||
       axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("cyclic power keeps active command");
    }

    // FB-facade form of the same contract: MC_Power and the move FB called
    // together every cycle, the way a scan task drives them.
    axis::AxisModel scanned;
    fb::FbPower power;
    power.axis_ref = &scanned;
    power.enable = true;
    power.call();

    fb::FbMoveAbsolute fb_move;
    fb_move.axis_ref = &scanned;
    fb_move.position = 2.0;
    fb_move.velocity = 0.1;
    fb_move.acceleration = 0.05;
    fb_move.deceleration = 0.05;
    fb_move.jerk = 0.05;
    fb_move.execute = true;
    fb_move.call();
    if(!fb_move.outputs.command_accepted) {
        return fail("cyclic power fb move accepted");
    }
    for(int i = 0; i < 400 && !fb_move.outputs.done; ++i) {
        scanned.cycle();
        power.call();
        fb_move.call();
        if(fb_move.outputs.command_aborted) {
            return fail("cyclic power fb move aborted");
        }
    }
    if(!fb_move.outputs.done || !near(scanned.snapshot().command_position, 2.0, 1e-8)) {
        return fail("cyclic power fb move done at target");
    }

    return 0;
}

int check_basic_fb_edge_cases()
{
    using namespace plcopen::core;

    // TimerBase::set_cycle_time rejects zero and negative
    {
        fb::TON ton;
        if(ton.set_cycle_time(0) || ton.set_cycle_time(-1)) {
            return fail("timer set_cycle_time rejects non-positive");
        }
    }

    // TON with pt==0: immediate output
    {
        fb::TON ton;
        ton.pt = 0;
        ton.in = true;
        ton.cycle();
        if(!ton.q || ton.et != 0) {
            return fail("ton pt=0 immediate");
        }
    }

    // TOF with pt==0: immediate clear
    {
        fb::TOF tof;
        tof.in = true;
        tof.cycle();
        tof.in = false;
        tof.pt = 0;
        tof.cycle();
        if(tof.q || tof.et != 0) {
            return fail("tof pt=0 immediate clear");
        }
    }

    // CTUD: reset, load, down count
    {
        fb::CTUD ctud;
        ctud.pv = 5;
        ctud.cycle();
        ctud.cu = true;
        ctud.cycle();
        if(ctud.cv != 1) {
            return fail("ctud initial up");
        }
        ctud.cu = false;
        ctud.cycle();
        ctud.cu = true;
        ctud.cycle();
        if(ctud.cv != 2) {
            return fail("ctud second up");
        }

        // Load
        ctud.cu = false;
        ctud.load = true;
        ctud.cycle();
        if(ctud.cv != 5) {
            return fail("ctud load");
        }
        ctud.load = false;

        // Down count
        ctud.cd = true;
        ctud.cycle();
        if(ctud.cv != 4) {
            return fail("ctud down");
        }

        // Reset
        ctud.cd = false;
        ctud.reset = true;
        ctud.cycle();
        if(ctud.cv != 0) {
            return fail("ctud reset");
        }
    }

    // RTC: overflow clamping
    {
        fb::RTC rtc;
        rtc.enable = true;
        rtc.pdt = std::numeric_limits<std::int64_t>::max() - 1;
        rtc.cycle();
        if(!rtc.q || rtc.dt != std::numeric_limits<std::int64_t>::max() - 1) {
            return fail("rtc initial dt");
        }
        rtc.cycle();
        if(rtc.dt != std::numeric_limits<std::int64_t>::max()) {
            return fail("rtc overflow clamp");
        }
        rtc.cycle();
        if(rtc.dt != std::numeric_limits<std::int64_t>::max()) {
            return fail("rtc stays at max");
        }
    }

    return 0;
}

} // namespace

int main()
{
    if(check_basic_fb_contracts() != 0 || check_base_latches() != 0 ||
       check_axis_state_and_motion() != 0 || check_axis_buffering_and_limits() != 0 ||
       check_group_linear_contract() != 0 || check_motion_facades() != 0 ||
       check_cyclic_power_keeps_motion() != 0 || check_basic_fb_edge_cases() != 0) {
        return 1;
    }
    std::printf("PASS r3 semantics tests\n");
    return 0;
}
