#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/probe.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

axis::AxisCommand make_move(double target, double velocity)
{
    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = target;
    move.velocity = velocity;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    return move;
}

int check_touch_probe_capture()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbTouchProbe probe;
    probe.axis_ref = &axis;
    probe.trigger_input = 0;
    probe.execute = true;
    probe.call();
    if(!probe.outputs.busy || probe.outputs.error) {
        return fail("probe armed busy");
    }

    if(!axis.submit(make_move(4.0, 0.05))) {
        return fail("probe move accepted");
    }
    bool fired = false;
    for(int i = 0; i < 400 && !probe.outputs.done; ++i) {
        if(!fired && axis.snapshot().command_position > 0.25) {
            axis.set_trigger_input(0, true);
            fired = true;
        }
        axis.cycle();
        probe.call();
    }
    if(!probe.outputs.done || probe.outputs.error || probe.recorded_position <= 0.0) {
        return fail("probe captures rising edge");
    }

    // Re-arming while the input is already high must wait for a fresh edge.
    probe.execute = false;
    probe.call();
    probe.execute = true;
    probe.call();
    axis.cycle();
    probe.call();
    if(probe.outputs.done || !probe.outputs.busy) {
        return fail("probe ignores already-high input at arm");
    }
    axis.set_trigger_input(0, false);
    axis.cycle();
    probe.call();
    axis.set_trigger_input(0, true);
    axis.cycle();
    probe.call();
    if(!probe.outputs.done) {
        return fail("probe captures fresh edge after re-arm");
    }

    fb::FbTouchProbe invalid_channel;
    invalid_channel.axis_ref = &axis;
    invalid_channel.trigger_input = 99;
    invalid_channel.execute = true;
    invalid_channel.call();
    if(!invalid_channel.outputs.error ||
       invalid_channel.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("probe rejects unsupported channel");
    }

    return 0;
}

int check_abort_trigger()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbTouchProbe probe;
    probe.axis_ref = &axis;
    probe.trigger_input = 0;
    probe.execute = true;
    probe.call();
    if(!probe.outputs.busy) {
        return fail("abort-trigger probe armed");
    }

    fb::FbAbortTrigger other;
    other.axis_ref = &axis;
    other.trigger_input = 1;
    other.execute = true;
    other.call();
    if(!other.outputs.done || other.outputs.error) {
        return fail("abort trigger on other input done");
    }
    axis.cycle();
    probe.call();
    if(!probe.outputs.busy || probe.outputs.command_aborted) {
        return fail("abort trigger leaves other probe armed");
    }

    fb::FbAbortTrigger matching;
    matching.axis_ref = &axis;
    matching.trigger_input = 0;
    matching.execute = true;
    matching.call();
    if(!matching.outputs.done || matching.outputs.error) {
        return fail("abort trigger matching done");
    }
    axis.cycle();
    probe.call();
    if(!probe.outputs.command_aborted || probe.outputs.busy) {
        return fail("abort trigger disarms matching probe");
    }

    fb::FbAbortTrigger unsupported;
    unsupported.axis_ref = &axis;
    unsupported.trigger_input = 99;
    unsupported.execute = true;
    unsupported.call();
    if(!unsupported.outputs.error ||
       unsupported.outputs.error_id != rt::ErrorCode::unsupported || unsupported.outputs.done) {
        return fail("abort trigger rejects unsupported channel");
    }

    return 0;
}

int check_independent_probes()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbTouchProbe first;
    first.axis_ref = &axis;
    first.trigger_input = 0;
    first.execute = true;
    first.call();

    fb::FbTouchProbe second;
    second.axis_ref = &axis;
    second.trigger_input = 1;
    second.execute = true;
    second.call();

    axis.set_trigger_input(1, true);
    axis.cycle();
    first.call();
    second.call();
    if(!first.outputs.busy || first.outputs.command_aborted || !second.outputs.done) {
        return fail("probes track inputs independently");
    }

    axis.set_trigger_input(0, true);
    axis.cycle();
    first.call();
    if(!first.outputs.done) {
        return fail("first probe captures after second");
    }

    return 0;
}

int check_window_only()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbTouchProbe probe;
    probe.axis_ref = &axis;
    probe.trigger_input = 0;
    probe.window_only = true;
    probe.first_position = 1.0;
    probe.last_position = 2.0;
    probe.execute = true;
    probe.call();
    if(!probe.outputs.busy) {
        return fail("window probe armed");
    }

    if(!axis.submit(make_move(4.0, 0.02))) {
        return fail("window move accepted");
    }

    // Rising edge before the window is ignored.
    bool fired_early = false;
    for(int i = 0; i < 400 && axis.snapshot().command_position < 1.2; ++i) {
        if(!fired_early && axis.snapshot().command_position > 0.25) {
            axis.set_trigger_input(0, true);
            fired_early = true;
        }
        axis.cycle();
        probe.call();
    }
    if(probe.outputs.done) {
        return fail("window probe ignores edge outside window");
    }

    // A fresh edge inside the window captures.
    axis.set_trigger_input(0, false);
    axis.cycle();
    probe.call();
    axis.set_trigger_input(0, true);
    axis.cycle();
    probe.call();
    if(!probe.outputs.done || probe.recorded_position < 1.0 || probe.recorded_position > 2.0) {
        return fail("window probe captures inside window");
    }

    fb::FbTouchProbe invalid_window;
    invalid_window.axis_ref = &axis;
    invalid_window.trigger_input = 1;
    invalid_window.window_only = true;
    invalid_window.first_position = 2.0;
    invalid_window.last_position = 1.0;
    invalid_window.execute = true;
    invalid_window.call();
    if(!invalid_window.outputs.error ||
       invalid_window.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("window probe rejects inverted window");
    }

    return 0;
}

int check_emergency_stop()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.submit(make_move(4.0, 0.5))) {
        return fail("emergency setup move");
    }
    axis.cycle();

    fb::FbEmergencyStop stop;
    stop.axis_ref = &axis;
    stop.execute = true;
    stop.call();
    if(!stop.outputs.done || stop.outputs.error) {
        return fail("emergency stop done");
    }
    if(axis.status() != axis::AxisStatus::errorstop || !axis.snapshot().error) {
        return fail("emergency stop drives errorstop");
    }
    if(axis.reset_error() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("emergency stop recoverable via reset");
    }

    fb::FbEmergencyStop missing;
    missing.execute = true;
    missing.call();
    if(!missing.outputs.error) {
        return fail("emergency stop missing axis rejected");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_touch_probe_capture() != 0 || check_abort_trigger() != 0 ||
       check_independent_probes() != 0 || check_window_only() != 0 ||
       check_emergency_stop() != 0) {
        return 1;
    }
    std::printf("PASS r3 probe tests\n");
    return 0;
}
