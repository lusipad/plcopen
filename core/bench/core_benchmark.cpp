#include <cstdio>
#include <ctime>

#include "axis/state.h"
#include "exec/sampler.h"
#include "exec/sync.h"
#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "plan/path.h"
#include "rt/spsc_queue.h"
#include "rt/static_vector.h"

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

    std::printf("BENCH_BASELINE static_vector_ms=%.3f spsc_ms=%.3f sample_ms=%.3f "
                "path_sample_ms=%.3f axis_cycle_pair_ms=%.3f checksum=%.3f\n",
                vector_ms, queue_ms, sample_ms, path_sample_ms, axis_cycle_ms,
                position_sum + sink);
    std::printf("PATH_METRICS speed_ripple=%.6f path_error=%.12f cycle_efficiency=%.6f "
                "blend_deviation=%.12f cam_error=%.12f overlay_checksum=%.6f "
                "otg_duration_vs_baseline=%.4f\n",
                speed_ripple, path_error, committed.total_length() / path_buffer.total_length(),
                blend.curve.blend.max_deviation, cam_error, overlay_checksum,
                cycle_time_efficiency);
    return 0;
}
