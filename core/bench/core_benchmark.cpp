#include <cstdio>
#include <ctime>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sampler.h"
#include "exec/sync.h"
#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "plan/path.h"
#include "rt/spsc_queue.h"
#include "rt/static_vector.h"
#include "stream/joint_group.h"

namespace
{

double millis_since(std::clock_t start)
{
    return 1000.0 * static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
}

} // namespace

int main()
{
    using namespace plcopen::core;

    rt::StaticVector<int, 8> values;
    rt::SpscQueue<int, 8> queue;

    const otg::State1D from{0.0, 0.0, 0.0};
    const otg::Target1D to{10.0, 0.0, 0.0};
    const otg::Limits1D limits{4.0, 2.0, 2.0, 3.0};
    const rt::Result<otg::Profile1D> planned = otg::plan(from, to, limits);
    if(!planned) {
        std::printf("BENCH_FAIL plan error=%d\n", static_cast<int>(planned.error()));
        return 1;
    }
    const otg::Profile1D profile = planned.value();
    const geom::LineSegment line_a = geom::make_line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}).value();
    const geom::LineSegment line_b = geom::make_line({5.0, 0.0, 0.0}, {5.0, 5.0, 0.0}).value();
    plan::PathBuffer<2> path_buffer;
    path_buffer.push(geom::as_path_segment(line_a));
    path_buffer.push(geom::as_path_segment(line_b));
    const rt::Result<plan::LookAheadPlan<2>> lookahead =
        plan::compute_lookahead(path_buffer, 4.0, 2.0, 2);
    if(!lookahead) {
        std::printf("BENCH_FAIL lookahead error=%d\n", static_cast<int>(lookahead.error()));
        return 1;
    }
    exec::CommittedPath<2> committed;
    committed.push(geom::as_path_segment(line_a));
    committed.push(geom::as_path_segment(line_b));
    const plan::BlendDecision blend =
        plan::decide_blend(geom::as_path_segment(line_a), geom::as_path_segment(line_b), 0.05);
    exec::CamTable<4> cam;
    cam.push({0.0, 0.0});
    cam.push({5.0, 10.0});

    constexpr int Iterations = 200000;
    int sink = 0;
    double position_sum = 0.0;

    std::clock_t start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        values.clear();
        values.push_back(i);
        values.push_back(i + 1);
        sink += values[0];
    }
    const double vector_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        queue.push(i);
        queue.pop(sink);
    }
    const double queue_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        const std::int64_t cycle = i % profile.duration_cycles();
        position_sum += otg::sample(profile, rt::CycleTick::from_cycles(cycle)).position;
    }
    const double sample_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        const std::int64_t cycle = i % profile.duration_cycles();
        position_sum +=
            exec::sample_profiled_path(committed, profile, rt::CycleTick::from_cycles(cycle)).x;
    }
    const double path_sample_ms = millis_since(start);

    // L5 cycle path: discrete profile + superimposed offset + armed probe on
    // the base axis, gear-synchronized slave sampling it. This is the widest
    // per-cycle branch set added in R3.
    axis::AxisModel bench_master;
    axis::AxisModel bench_slave;
    bench_master.set_power(true);
    bench_slave.set_power(true);
    {
        axis::AxisCommand move{};
        move.kind = axis::CommandKind::move_absolute;
        move.value = 1.0e9;
        move.velocity = 0.001;
        move.acceleration = 0.001;
        move.deceleration = 0.001;
        move.jerk = 0.001;
        bench_master.submit(move);
        bench_master.submit_superimposed(1.0e9, 0.0005, 0.001, 0.001, 0.001);
        bench_master.arm_touch_probe(0, false, 0.0, 0.0);
        axis::GearInCommand gear{};
        gear.master = &bench_master;
        gear.ratio_numerator = 2.0;
        bench_slave.gear_in(gear);
    }
    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        bench_master.cycle();
        bench_slave.cycle();
    }
    const double axis_cycle_ms = millis_since(start);
    position_sum += bench_slave.snapshot().command_position;

    // A3 group circular cycle path: per-cycle cost of the arc-length sampling
    // branch (KB-030).
    axis::AxisModel gx;
    axis::AxisModel gy;
    gx.set_power(true);
    gy.set_power(true);
    axis::AxisGroup bench_group;
    bench_group.add_axis(gx);
    bench_group.add_axis(gy);
    bench_group.enable();
    {
        axis::GroupCommand approach{};
        approach.target.size = 2;
        approach.target.value[0] = 1.0;
        approach.velocity = 0.5;
        bench_group.submit_linear(approach);
        for(int i = 0; i < 100 && bench_group.status() != axis::GroupStatus::standby; ++i) {
            bench_group.cycle();
        }
        axis::GroupCommand arc{};
        arc.target.size = 2;
        arc.aux.size = 2;
        arc.aux.value[0] = 0.70710678118654752;
        arc.aux.value[1] = 0.70710678118654752;
        arc.target.value[0] = 0.0;
        arc.target.value[1] = 1.0;
        arc.velocity = 1.0e-9; // hold inside the arc for the whole loop
        arc.path_choice = axis::CircPathChoice::counter_clockwise;
        if(!bench_group.submit_circular(arc)) {
            std::printf("BENCH_FAIL group circular submit\n");
            return 1;
        }
    }
    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        bench_group.cycle();
    }
    const double group_circular_cycle_ms = millis_since(start);
    position_sum += gx.snapshot().command_position + gy.snapshot().command_position;

    // Cycle-time efficiency trend (long-term-plan 6.5): total duration of the
    // time-optimal planner over the baseline planner on a fixed case set.
    long long optimal_cycles = 0;
    long long baseline_cycles = 0;
    {
        const otg::Limits1D otg_limits{3.0, 2.0, 2.0, 2.5};
        const otg::State1D froms[] = {
            {0.0, 0.0, 0.0}, {0.0, 1.5, 0.0}, {0.0, -2.0, 0.0}, {0.0, 2.0, -1.5}};
        const otg::Target1D tos[] = {
            {8.0, 0.0, 0.0}, {30.0, 1.0, 0.0}, {-6.0, 0.0, 0.0}, {2.5, 0.0, 0.0}};
        for(std::size_t i = 0; i < 4; ++i) {
            const rt::Result<otg::Profile1D> optimal =
                otg::plan_time_optimal(froms[i], tos[i], otg_limits);
            const rt::Result<otg::Profile1D> baseline = otg::plan(froms[i], tos[i], otg_limits);
            if(!optimal || !baseline) {
                std::printf("BENCH_FAIL efficiency case %zu\n", i);
                return 1;
            }
            optimal_cycles += optimal.value().duration_cycles();
            baseline_cycles += baseline.value().duration_cycles();
        }
    }
    const double cycle_time_efficiency =
        baseline_cycles > 0
            ? static_cast<double>(optimal_cycles) / static_cast<double>(baseline_cycles)
            : 0.0;

    const double speed_ripple =
        std::fabs(lookahead.value().exit_speed[0] - lookahead.value().entry_speed[1]);
    const double path_error = geom::norm(path_buffer.sample(path_buffer.total_length()) -
                                         geom::Vec3{5.0, 5.0, 0.0});
    const double cam_error = std::fabs(cam.sample(2.5).value() - 5.0);
    const geom::Vec3 overlaid = exec::apply_overlay({1.0, 1.0, 1.0}, {0.25, -0.25, 0.5});
    const double overlay_checksum = overlaid.x + overlaid.y + overlaid.z;

    // B9 stream budget (KB-035, robot-integration section 5): 28 joints at a
    // 1 ms cycle must stay under 30% of the cycle budget (300 us). The
    // staggered tier is the 100 Hz steady state (~2.8 solves per cycle); the
    // burst tier re-solves every joint every cycle (the 1 kHz-stream ceiling).
    double stream_stagger_us = 0.0;
    double stream_burst_us = 0.0;
    {
        stream::JointStreamGroup joints;
        stream::StreamFilterConfig stream_config{};
        stream_config.limits = {0.4, 0.02, 0.02, 0.005};
        stream_config.timeout_cycles = 50;
        stream_config.extrapolation_cycles = 40;
        if(joints.configure(28, stream_config) != rt::ErrorCode::ok) {
            std::printf("BENCH_FAIL stream configure\n");
            return 1;
        }
        for(std::size_t j = 0; j < joints.joint_count(); ++j) {
            joints.reset(j, {0.0, 0.0, 0.0});
        }

        constexpr int StaggerCycles = 20000;
        std::int64_t now = 0;
        start = std::clock();
        for(int i = 0; i < StaggerCycles; ++i) {
            for(std::size_t j = 0; j < joints.joint_count(); ++j) {
                if((now + static_cast<std::int64_t>(j)) % 10 == 0) {
                    stream::StreamTarget target{};
                    target.position = 0.2 * static_cast<double>(now + 1);
                    target.velocity = 0.2;
                    target.has_velocity = true;
                    target.timestamp_cycles = now + 1;
                    joints.push_target(j, target);
                }
            }
            joints.cycle();
            ++now;
        }
        stream_stagger_us = 1000.0 * millis_since(start) / StaggerCycles;

        constexpr int BurstCycles = 2000;
        start = std::clock();
        for(int i = 0; i < BurstCycles; ++i) {
            for(std::size_t j = 0; j < joints.joint_count(); ++j) {
                stream::StreamTarget target{};
                target.position = 0.2 * static_cast<double>(now + 1);
                target.velocity = 0.2;
                target.has_velocity = true;
                target.timestamp_cycles = now + 1;
                joints.push_target(j, target);
            }
            joints.cycle();
            ++now;
        }
        stream_burst_us = 1000.0 * millis_since(start) / BurstCycles;
        position_sum += joints.state(0).position;
    }

    std::printf("BENCH_BASELINE static_vector_ms=%.3f spsc_ms=%.3f sample_ms=%.3f "
                "path_sample_ms=%.3f axis_cycle_pair_ms=%.3f group_circular_cycle_ms=%.3f "
                "checksum=%.3f\n",
                vector_ms, queue_ms, sample_ms, path_sample_ms, axis_cycle_ms,
                group_circular_cycle_ms, position_sum + sink);
    std::printf("STREAM_METRICS joints=28 stagger_us_per_cycle=%.2f burst_us_per_cycle=%.2f "
                "budget_us=300\n",
                stream_stagger_us, stream_burst_us);
    std::printf("PATH_METRICS speed_ripple=%.6f path_error=%.12f cycle_efficiency=%.6f "
                "blend_deviation=%.12f cam_error=%.12f overlay_checksum=%.6f "
                "otg_duration_vs_baseline=%.4f\n",
                speed_ripple, path_error, committed.total_length() / path_buffer.total_length(),
                blend.curve.quintic.max_deviation, cam_error, overlay_checksum,
                cycle_time_efficiency);
    return 0;
}
