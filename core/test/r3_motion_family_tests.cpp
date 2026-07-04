#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/motion.h"

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

axis::AxisCommand make_move(axis::CommandKind kind, double value, double velocity)
{
    axis::AxisCommand move{};
    move.kind = kind;
    move.value = value;
    move.velocity = velocity;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    return move;
}

int check_move_additive()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Aborting additive resolves against the previous commanded endpoint, not
    // the aborted position.
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 4.0, 0.1))) {
        return fail("additive base move accepted");
    }
    for(int i = 0; i < 5; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::discrete_motion ||
       axis.snapshot().command_position >= 4.0) {
        return fail("additive base move active");
    }

    axis::AxisCommand additive = make_move(axis::CommandKind::move_additive, 2.0, 0.5);
    additive.buffer_mode = axis::BufferMode::aborting;
    if(!axis.submit(additive)) {
        return fail("additive aborting accepted");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 6.0, 1e-8)) {
        return fail("additive aborting resolves previous endpoint");
    }

    // Buffered additive queues and extends the queued endpoint.
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 8.0, 0.5))) {
        return fail("additive buffered base accepted");
    }
    axis::AxisCommand buffered = make_move(axis::CommandKind::move_additive, 1.0, 0.5);
    buffered.buffer_mode = axis::BufferMode::buffered;
    if(!axis.submit(buffered)) {
        return fail("additive buffered accepted");
    }
    for(int i = 0; i < 800 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 9.0, 1e-8)) {
        return fail("additive buffered endpoint");
    }

    fb::FbMoveAdditive facade;
    facade.axis_ref = &axis;
    facade.distance = -2.0;
    facade.velocity = 0.5;
    facade.execute = true;
    facade.call();
    if(!facade.outputs.command_accepted) {
        return fail("additive facade accepted");
    }
    for(int i = 0; i < 800 && !facade.outputs.done; ++i) {
        axis.cycle();
        facade.call();
    }
    if(!facade.outputs.done || !near(axis.snapshot().command_position, 7.0, 1e-8)) {
        return fail("additive facade endpoint");
    }

    return 0;
}

int check_move_superimposed()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute base;
    base.axis_ref = &axis;
    base.position = 5.0;
    base.velocity = 0.05;
    base.execute = true;
    base.call();
    for(int i = 0; i < 5; ++i) {
        axis.cycle();
        base.call();
    }
    if(!base.outputs.busy) {
        return fail("superimposed base active");
    }

    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &axis;
    superimposed.distance = 2.0;
    superimposed.velocity = 0.05;
    superimposed.execute = true;
    superimposed.call();
    if(!superimposed.outputs.command_accepted) {
        return fail("superimposed accepted");
    }

    bool ran_together = false;
    for(int i = 0; i < 2000 && !(base.outputs.done && superimposed.outputs.done); ++i) {
        axis.cycle();
        base.call();
        superimposed.call();
        if(base.outputs.busy && superimposed.outputs.busy) {
            ran_together = true;
        }
        if(base.outputs.command_aborted || superimposed.outputs.command_aborted) {
            return fail("superimposed must not abort the base move");
        }
    }
    if(!base.outputs.done || !superimposed.outputs.done || !ran_together) {
        return fail("superimposed and base completion");
    }
    if(!near(axis.snapshot().command_position, 7.0, 1e-6)) {
        return fail("superimposed final position is base plus offset");
    }
    if(axis.status() != axis::AxisStatus::standstill) {
        return fail("superimposed returns standstill");
    }

    return 0;
}

int check_halt_superimposed()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute base;
    base.axis_ref = &axis;
    base.position = 8.0;
    base.velocity = 0.05;
    base.execute = true;
    base.call();

    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &axis;
    superimposed.distance = 6.0;
    superimposed.velocity = 0.02;
    superimposed.execute = true;
    superimposed.call();
    if(!superimposed.outputs.command_accepted) {
        return fail("halt-superimposed setup accepted");
    }

    for(int i = 0; i < 40; ++i) {
        axis.cycle();
        base.call();
        superimposed.call();
    }
    if(!superimposed.outputs.busy || !base.outputs.busy) {
        return fail("halt-superimposed both active");
    }
    const double position_before_halt = axis.snapshot().command_position;

    fb::FbHaltSuperimposed halt;
    halt.axis_ref = &axis;
    halt.execute = true;
    halt.call();
    axis.cycle();
    base.call();
    superimposed.call();
    halt.call();
    if(!halt.outputs.done || halt.outputs.error) {
        return fail("halt-superimposed done");
    }
    if(!superimposed.outputs.command_aborted) {
        return fail("halt-superimposed aborts only the offset");
    }
    if(base.outputs.command_aborted || !base.outputs.busy) {
        return fail("halt-superimposed keeps base running");
    }

    for(int i = 0; i < 2000 && !base.outputs.done; ++i) {
        axis.cycle();
        base.call();
    }
    const double final_position = axis.snapshot().command_position;
    if(!base.outputs.done || final_position < 8.0 || final_position >= 14.0 ||
       final_position <= position_before_halt) {
        return fail("halt-superimposed base continues to target plus partial offset");
    }

    return 0;
}

int check_move_continuous()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveContinuousAbsolute continuous;
    continuous.axis_ref = &axis;
    continuous.position = 4.0;
    continuous.velocity = 0.2;
    continuous.end_velocity = 0.05;
    continuous.execute = true;
    continuous.call();
    if(!continuous.outputs.command_accepted) {
        return fail("continuous absolute accepted");
    }

    for(int i = 0; i < 2000 && !continuous.outputs.done; ++i) {
        axis.cycle();
        continuous.call();
    }
    if(!continuous.outputs.done || !continuous.outputs.busy || !continuous.outputs.active) {
        return fail("continuous absolute done with busy held");
    }
    if(axis.status() != axis::AxisStatus::continuous_motion) {
        return fail("continuous absolute stays in continuous motion");
    }
    const double at_done = axis.snapshot().command_position;
    if(!near(at_done, 4.0, 0.2)) {
        return fail("continuous absolute reaches target");
    }
    axis.cycle();
    continuous.call();
    axis.cycle();
    continuous.call();
    if(axis.snapshot().command_position <= at_done ||
       !near(axis.snapshot().command_velocity, 0.05, 1e-9)) {
        return fail("continuous absolute holds end velocity");
    }

    // A takeover aborts the hold; the FB reports CommandAborted.
    axis::AxisCommand takeover = make_move(axis::CommandKind::move_absolute, 3.0, 0.5);
    if(!axis.submit(takeover)) {
        return fail("continuous takeover accepted");
    }
    axis.cycle();
    continuous.call();
    if(!continuous.outputs.command_aborted) {
        return fail("continuous reports aborted after takeover");
    }

    return 0;
}

int check_move_continuous_relative_and_update()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(1.0);

    fb::FbMoveContinuousRelative relative;
    relative.axis_ref = &axis;
    relative.distance = 2.0;
    relative.velocity = 0.2;
    relative.end_velocity = 0.05;
    relative.execute = true;
    relative.call();
    if(!relative.outputs.command_accepted) {
        return fail("continuous relative accepted");
    }
    for(int i = 0; i < 2000 && !relative.outputs.done; ++i) {
        axis.cycle();
        relative.call();
    }
    if(!relative.outputs.done || !near(axis.snapshot().command_position, 3.0, 0.2)) {
        return fail("continuous relative reaches start plus distance");
    }

    axis::AxisModel updated_axis;
    updated_axis.set_power(true);
    fb::FbMoveContinuousAbsolute updated;
    updated.axis_ref = &updated_axis;
    updated.position = 4.0;
    updated.velocity = 0.1;
    updated.end_velocity = 0.02;
    updated.continuous_update = true;
    updated.execute = true;
    updated.call();
    for(int i = 0; i < 10; ++i) {
        updated_axis.cycle();
        updated.call();
    }
    if(updated.outputs.done) {
        return fail("continuous update still moving before target change");
    }
    updated.position = 7.0;
    for(int i = 0; i < 4000 && !updated.outputs.done; ++i) {
        updated_axis.cycle();
        updated.call();
    }
    if(!updated.outputs.done || !near(updated_axis.snapshot().command_position, 7.0, 0.3)) {
        return fail("continuous update retargets active command");
    }

    fb::FbMoveContinuousAbsolute invalid;
    invalid.axis_ref = &axis;
    invalid.position = 5.0;
    invalid.velocity = 0.2;
    invalid.end_velocity = 0.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("continuous rejects zero end velocity");
    }

    return 0;
}

int check_superimposed_boundaries()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Superimposed rejects unpowered/invalid input at the model level.
    if(axis.submit_superimposed(2.0, 0.0, 1.0, 1.0, 1.0).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("superimposed rejects non-positive velocity");
    }

    // An aborting base command clears the running superimposed offset.
    if(!axis.submit_superimposed(4.0, 0.05, 1.0, 1.0, 1.0)) {
        return fail("superimposed standalone accepted");
    }
    for(int i = 0; i < 10; ++i) {
        axis.cycle();
    }
    if(!axis.superimposed_active() || axis.snapshot().command_position <= 0.0) {
        return fail("superimposed standalone runs");
    }
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 0.5, 0.5))) {
        return fail("superimposed abort takeover accepted");
    }
    if(axis.superimposed_active()) {
        return fail("aborting base command clears superimposed");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_move_additive() != 0 || check_move_superimposed() != 0 ||
       check_halt_superimposed() != 0 || check_move_continuous() != 0 ||
       check_move_continuous_relative_and_update() != 0 ||
       check_superimposed_boundaries() != 0) {
        return 1;
    }
    std::printf("PASS r3 motion family tests\n");
    return 0;
}
