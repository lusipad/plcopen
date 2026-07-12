// A2 freeze-window allocation assertion (rewrite-plan DoD section 5 item 3,
// software side): after the setup phase every motion cycle path must be
// heap-allocation free. The test replaces the global allocation operators
// with counting wrappers, freezes, then drives the widest per-cycle branch
// set in the rewrite core; any allocation inside the frozen window is a
// failure. --cycles N scales the frozen window for the nightly soak tier
// (the 72h on-target run reuses this binary).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/homing.h"

namespace
{

bool g_frozen = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_frozen) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void *operator new[](std::size_t size)
{
    if(g_frozen) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void operator delete(void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

long long parse_cycles(int argc, char **argv)
{
    long long cycles = 20000;
    for(int i = 1; i + 1 < argc; ++i) {
        if(std::strcmp(argv[i], "--cycles") == 0) {
            cycles = std::atoll(argv[i + 1]);
        }
    }
    return cycles < 1 ? 1 : cycles;
}

} // namespace

int main(int argc, char **argv)
{
    const long long frozen_cycles = parse_cycles(argc, argv);

    // ---- Setup phase (allocation is allowed here) ----------------------

    // Single-axis branch set: discrete profile + superimposed offset + armed
    // probe, gear-synchronized slave (the widest per-cycle set from R3).
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    {
        axis::AxisCommand move{};
        move.kind = axis::CommandKind::move_absolute;
        move.value = 1.0e12;
        move.velocity = 0.001;
        move.acceleration = 0.001;
        move.deceleration = 0.001;
        move.jerk = 0.001;
        master.submit(move);
        master.submit_superimposed(1.0e12, 0.0005, 0.001, 0.001, 0.001);
        master.arm_touch_probe(0, false, 0.0, 0.0);
        axis::GearInCommand gear{};
        gear.master = &master;
        gear.ratio_numerator = 2.0;
        slave.gear_in(gear);
    }

    // Group with a blended look-ahead window ending in a long circular arc:
    // covers the window line/curve sampling and the arc-length branch.
    axis::AxisModel gx;
    axis::AxisModel gy;
    gx.set_power(true);
    gy.set_power(true);
    axis::AxisGroup group;
    group.add_axis(gx);
    group.add_axis(gy);
    group.enable();
    {
        axis::GroupCommand first{};
        first.target.size = 2;
        first.target.value[0] = 2.0;
        first.velocity = 0.0002;
        first.acceleration = 0.00002;
        first.deceleration = 0.00002;
        first.jerk = 0.00002;
        group.submit_linear(first);
        for(int i = 0; i < 5; ++i) {
            group.cycle();
        }
        axis::GroupCommand blend = first;
        blend.target.value[0] = 3.73205080756888;
        blend.target.value[1] = 1.0;
        blend.buffer_mode = axis::BufferMode::blending_high;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.05;
        if(!group.submit_linear(blend)) {
            return fail("setup window accepted");
        }
    }

    axis::AxisModel cx;
    axis::AxisModel cy;
    cx.set_power(true);
    cy.set_power(true);
    axis::AxisGroup circle_group;
    circle_group.add_axis(cx);
    circle_group.add_axis(cy);
    circle_group.enable();
    {
        axis::GroupCommand approach{};
        approach.target.size = 2;
        approach.target.value[0] = 1.0;
        approach.velocity = 0.5;
        circle_group.submit_linear(approach);
        for(int i = 0; i < 100 && circle_group.status() != axis::GroupStatus::standby; ++i) {
            circle_group.cycle();
        }
        axis::GroupCommand arc{};
        arc.target.size = 2;
        arc.aux.size = 2;
        arc.aux.value[0] = 0.70710678118654752;
        arc.aux.value[1] = 0.70710678118654752;
        arc.target.value[0] = 0.0;
        arc.target.value[1] = 1.0;
        arc.velocity = 1.0e-9; // stays inside the arc for the whole window
        arc.path_choice = axis::CircPathChoice::counter_clockwise;
        if(!circle_group.submit_circular(arc)) {
            return fail("setup arc accepted");
        }
    }

    // Warm up stdio before freezing (first printf may allocate its buffer).
    std::printf("a2 alloc guard: frozen window of %lld cycles\n", frozen_cycles);

    axis::AxisModel homing_axis;
    homing_axis.set_power(true);
    fb::FbStepBlock step_block;
    step_block.axis_ref = &homing_axis;
    step_block.execute = true;
    step_block.velocity = 0.001;
    step_block.torque_limit = 1.0;
    step_block.call();
    fb::FbStepReferenceFlyingRefPulse flying;
    flying.axis_ref = &master;
    flying.execute = true;
    flying.trigger_input = 1;
    flying.call();

    // ---- Frozen window: any heap allocation is a defect -----------------
    g_frozen_allocations = 0;
    g_frozen = true;
    for(long long i = 0; i < frozen_cycles; ++i) {
        master.cycle();
        slave.cycle();
        group.cycle();
        circle_group.cycle();
        homing_axis.cycle();
        step_block.call();
        flying.call();
    }
    g_frozen = false;

    if(g_frozen_allocations != 0) {
        std::printf("FAIL frozen cycle path allocated %llu time(s)\n", g_frozen_allocations);
        return 1;
    }

    // Sanity: the guard itself works (an allocation while frozen is counted).
    // The probe calls ::operator new directly: C++14 allocation elision only
    // applies to new/delete *expressions*, and clang at -O2 really does elide
    // a paired `new int`/`delete`, which silently disarmed this self-check on
    // the Linux clang lane. A direct call is an ordinary function call and
    // cannot be elided.
    g_frozen = true;
    void *probe = ::operator new(sizeof(int));
    g_frozen = false;
    ::operator delete(probe);
    if(g_frozen_allocations != 1) {
        return fail("allocation guard self-check");
    }

    std::printf("PASS a2 alloc guard (%lld frozen cycles, zero allocations)\n",
                frozen_cycles);
    return 0;
}
