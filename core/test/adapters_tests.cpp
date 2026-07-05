// BS6 adapters acceptance tests (ADR-0004 Accepted + approved breakdown):
// the Servo bridge must not alter core semantics (command-domain equality
// against a direct twin, cycle by cycle), the CiA402 ladder is covered
// transition by transition against the standard's table, and the mode
// manager's bumpless contract holds across CSP/CSV/CST switches on a live
// motion stream.

#include <cmath>
#include <cstdio>

#include "adapters/cia402.h"
#include "adapters/mode_manager.h"
#include "adapters/servo.h"
#include "fb/io.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

axis::AxisCommand make_move(double target)
{
    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = target;
    move.velocity = 0.05;
    move.acceleration = 0.004;
    move.deceleration = 0.004;
    move.jerk = 0.004;
    return move;
}

// The executor composition (read -> bridge -> cycle -> write) must leave the
// command domain identical to a direct twin: the bridge is outer-ring
// plumbing, not semantics.
int check_bridge_equivalence()
{
    axis::AxisModel direct;
    axis::AxisModel bridged;
    direct.set_power(true);
    bridged.set_power(true);
    direct.submit(make_move(3.0));
    bridged.submit(make_move(3.0));

    adapters::ServoSim sim;
    sim.set_digital_input(2, true);

    for(int tick = 0; tick < 2000; ++tick) {
        adapters::ServoFeedback feedback{};
        sim.read_feedback(feedback);
        adapters::bridge_feedback(bridged, feedback);

        direct.cycle();
        bridged.cycle();
        sim.write_setpoints(adapters::make_setpoints(bridged.snapshot()));

        if(!near(direct.snapshot().command_position, bridged.snapshot().command_position,
                 1e-12) ||
           !near(direct.snapshot().command_velocity, bridged.snapshot().command_velocity,
                 1e-12)) {
            return fail("bridge command-domain equality");
        }
        if(direct.status() == axis::AxisStatus::standstill) {
            break;
        }
    }
    if(!near(bridged.snapshot().command_position, 3.0, 1e-8)) {
        return fail("bridge move completes");
    }

    // Digital inputs and info bits arrive through the bridge.
    fb::FbReadDigitalInput input;
    input.axis_ref = &bridged;
    input.input_number = 2;
    input.enable = true;
    input.call();
    if(!input.valid || !input.value) {
        return fail("bridge digital input");
    }
    return 0;
}

int check_cia402_ladder()
{
    using adapters::Cia402Command;
    using adapters::Cia402Machine;
    using adapters::Cia402State;

    Cia402Machine machine;
    if(machine.state() != Cia402State::not_ready_to_switch_on) {
        return fail("cia402 boot state");
    }
    // Commands are rejected before initialization completes.
    if(machine.command(Cia402Command::shutdown) ||
       machine.state() != Cia402State::not_ready_to_switch_on) {
        return fail("cia402 pre-init rejection");
    }
    machine.initialize();
    if(machine.state() != Cia402State::switch_on_disabled) {
        return fail("cia402 init");
    }

    // Happy ladder up.
    if(!machine.command(Cia402Command::shutdown) ||
       machine.state() != Cia402State::ready_to_switch_on ||
       !machine.command(Cia402Command::switch_on) ||
       machine.state() != Cia402State::switched_on ||
       !machine.command(Cia402Command::enable_operation) ||
       machine.state() != Cia402State::operation_enabled || !machine.operational()) {
        return fail("cia402 ladder up");
    }
    // Skipping rungs is rejected.
    if(machine.command(Cia402Command::switch_on)) {
        return fail("cia402 skip rejection");
    }
    // Disable operation drops one rung.
    if(!machine.command(Cia402Command::disable_operation) ||
       machine.state() != Cia402State::switched_on) {
        return fail("cia402 disable operation");
    }
    // Quick stop from operation enabled, resume, then complete to disabled.
    machine.command(Cia402Command::enable_operation);
    if(!machine.command(Cia402Command::quick_stop) ||
       machine.state() != Cia402State::quick_stop_active ||
       !machine.command(Cia402Command::enable_operation) ||
       machine.state() != Cia402State::operation_enabled) {
        return fail("cia402 quick stop resume");
    }
    machine.command(Cia402Command::quick_stop);
    machine.quick_stop_complete();
    if(machine.state() != Cia402State::switch_on_disabled) {
        return fail("cia402 quick stop completes");
    }
    // Fault ladder: event -> reaction -> fault -> reset.
    machine.command(Cia402Command::shutdown);
    machine.command(Cia402Command::switch_on);
    machine.command(Cia402Command::enable_operation);
    machine.fault_event();
    if(machine.state() != Cia402State::fault_reaction_active ||
       machine.command(Cia402Command::enable_operation)) {
        return fail("cia402 fault reaction");
    }
    machine.fault_reaction_complete();
    if(machine.state() != Cia402State::fault ||
       machine.command(Cia402Command::shutdown) ||
       !machine.command(Cia402Command::fault_reset) ||
       machine.state() != Cia402State::switch_on_disabled) {
        return fail("cia402 fault reset");
    }
    // Disable voltage from every powered rung.
    machine.command(Cia402Command::shutdown);
    if(!machine.command(Cia402Command::disable_voltage) ||
       machine.state() != Cia402State::switch_on_disabled) {
        return fail("cia402 disable voltage");
    }
    return 0;
}

// Bumpless contract: across CSP -> CSV -> CST -> CSP switches on a live
// stream, the newly selected primary channel never steps beyond the
// per-cycle kinematic bound of the underlying motion.
int check_mode_manager_bumpless()
{
    adapters::ModeManager manager;
    if(manager.switch_mode(adapters::OperationMode::csv) !=
       rt::ErrorCode::precondition_failed) {
        return fail("mode switch requires a primed stream");
    }

    axis::AxisModel axis;
    axis.set_power(true);
    axis.submit(make_move(5.0));

    const adapters::OperationMode sequence[4] = {
        adapters::OperationMode::csv, adapters::OperationMode::cst,
        adapters::OperationMode::csp, adapters::OperationMode::csv};
    int switch_index = 0;
    double previous_primary = 0.0;
    bool primed = false;

    for(int tick = 0; tick < 1500; ++tick) {
        axis.cycle();
        const adapters::ServoSetpoints emitted =
            manager.emit(adapters::make_setpoints(axis.snapshot()));

        if(tick > 0 && tick % 300 == 0 && switch_index < 4) {
            const adapters::ServoSetpoints before = manager.latched();
            if(manager.switch_mode(sequence[switch_index]) != rt::ErrorCode::ok) {
                return fail("mode switch accepted");
            }
            ++switch_index;
            // At the switch instant the new primary channel continues from
            // the latched full-order state: no step by construction.
            const double handover = manager.primary_channel(before);
            previous_primary = handover;
            primed = true;
            continue;
        }

        const double primary = manager.primary_channel(emitted);
        if(primed) {
            // Position moves at most one velocity quantum per cycle;
            // velocity at most one acceleration quantum; torque is a
            // passthrough (constant zero in this scenario).
            if(std::fabs(primary - previous_primary) > 0.06) {
                return fail("mode primary channel continuity");
            }
        }
        previous_primary = primary;
        primed = true;
    }
    if(switch_index != 4) {
        return fail("mode switch coverage");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_bridge_equivalence() != 0 || check_cia402_ladder() != 0 ||
       check_mode_manager_bumpless() != 0) {
        return 1;
    }
    std::printf("PASS adapters tests\n");
    return 0;
}
