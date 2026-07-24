#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sampler.h"
#include "exec/sync.h"
#include "geom/geometry.h"
#include "kin/serial_chain.h"
#include "kin/wrist6r.h"
#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "plan/path.h"
#include "rt/spsc_queue.h"
#include "rt/static_vector.h"
#include "st/st.h"
#include "stream/joint_group.h"
#include "test_support/serial_chain_fixture.h"

namespace
{

double millis_since(std::clock_t start)
{
    return 1000.0 * static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
}

#if defined(_WIN32)
constexpr const char *BenchPlatform = "windows";
#elif defined(__linux__)
constexpr const char *BenchPlatform = "linux";
#elif defined(__APPLE__)
constexpr const char *BenchPlatform = "macos";
#else
constexpr const char *BenchPlatform = "unknown";
#endif

#if defined(__clang__)
constexpr const char *BenchCompiler = "clang";
constexpr int BenchCompilerMajor = __clang_major__;
constexpr int BenchCompilerMinor = __clang_minor__;
constexpr int BenchCompilerPatch = __clang_patchlevel__;
#elif defined(_MSC_VER)
constexpr const char *BenchCompiler = "msvc";
constexpr int BenchCompilerMajor = _MSC_VER / 100;
constexpr int BenchCompilerMinor = _MSC_VER % 100;
constexpr int BenchCompilerPatch = _MSC_FULL_VER % 100000;
#elif defined(__GNUC__)
constexpr const char *BenchCompiler = "gcc";
constexpr int BenchCompilerMajor = __GNUC__;
constexpr int BenchCompilerMinor = __GNUC_MINOR__;
constexpr int BenchCompilerPatch = __GNUC_PATCHLEVEL__;
#else
constexpr const char *BenchCompiler = "unknown";
constexpr int BenchCompilerMajor = 0;
constexpr int BenchCompilerMinor = 0;
constexpr int BenchCompilerPatch = 0;
#endif

#if defined(NDEBUG)
constexpr const char *BenchBuild = "release";
constexpr int BenchCalibrationEligible = 1;
#else
constexpr const char *BenchBuild = "debug";
constexpr int BenchCalibrationEligible = 0;
#endif

struct StCalibration
{
    bool ok = false;
    bool native_profile_required = false;
    std::uint64_t work_units = 0;
    std::uint32_t native_fb_instances = 0;
    double observed_ns_per_scan = 0.0;
};

struct StThroughput
{
    bool ok = false;
    std::int64_t instructions = 0;
    double observed_ns_per_instruction = 0.0;
};

struct H1StreamBenchmark
{
    double direct_steady_us = 0.0;
    double direct_adversarial_us = 0.0;
    double upsample_fast_us = 0.0;
    double upsample_slow_budget_us = 0.0;
    double checksum = 0.0;
};

plcopen::core::stream::JointStreamGroupConfig
make_h1_stream_config(plcopen::core::stream::JointFrameMode mode, bool fast_path)
{
    using namespace plcopen::core;

    stream::JointStreamGroupConfig config{};
    config.mode = mode;
    config.joint_count = stream::JointStreamGroup::MaxJoints;
    config.gain_ramp_cycles = 4;
    for(std::size_t joint = 0; joint < config.joint_count; ++joint) {
        stream::JointStreamConfig &member = config.joints[joint];
        member.filter.limits = {0.5, 0.05, 0.05, 0.01};
        member.filter.position_envelope_enabled = true;
        member.filter.min_position = -100.0;
        member.filter.max_position = 100.0;
        member.filter.timeout_cycles = 1000000;
        member.filter.extrapolation_cycles = 40;
        member.filter.quintic_fast_path = fast_path;
        member.max_abs_tau_ff = 10.0;
        member.min_kp = 0.0;
        member.max_kp = 100.0;
        member.min_kd = 0.0;
        member.max_kd = 20.0;
        member.safe_kp = 2.0;
        member.safe_kd = 1.0;
    }
    return config;
}

bool reset_h1_stream(plcopen::core::stream::JointStreamGroup &group)
{
    using namespace plcopen::core;

    for(std::size_t joint = 0; joint < group.joint_count(); ++joint) {
        if(group.reset(joint, {0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
            return false;
        }
    }
    return true;
}

int run_h1_stream_benchmark(H1StreamBenchmark &result)
{
    using namespace plcopen::core;

    constexpr int DirectSteadyCycles = 20000;
    constexpr int DirectAdversarialCycles = 20000;
    constexpr int UpsampleFastCycles = 20000;
    constexpr int UpsampleSlowCycles = 2000;

    stream::JointStreamGroup direct;
    if(direct.configure_frame(
           make_h1_stream_config(stream::JointFrameMode::direct, false)) !=
           rt::ErrorCode::ok ||
       !reset_h1_stream(direct)) {
        return 1;
    }
    stream::JointCommandFrame direct_frame{};
    direct_frame.joint_count = direct.joint_count();
    direct_frame.timestamp_cycles = 1;
    for(std::size_t joint = 0; joint < direct_frame.joint_count; ++joint) {
        direct_frame.joints[joint] = {0.0, 0.0, 0.0, 4.0, 2.0};
    }
    if(direct.push_frame(direct_frame) != rt::ErrorCode::ok) {
        return 1;
    }
    direct.cycle();

    std::clock_t start = std::clock();
    for(int cycle = 0; cycle < DirectSteadyCycles; ++cycle) {
        direct.cycle();
    }
    result.direct_steady_us =
        1000.0 * millis_since(start) / DirectSteadyCycles;

    start = std::clock();
    for(int cycle = 0; cycle < DirectAdversarialCycles; ++cycle) {
        const double direction = cycle % 2 == 0 ? 1.0 : -1.0;
        ++direct_frame.timestamp_cycles;
        for(std::size_t joint = 0; joint < direct_frame.joint_count; ++joint) {
            const double index = static_cast<double>(joint);
            direct_frame.joints[joint] = {
                direction * (0.5 + 0.001 * index),
                direction * 0.1,
                direction * (1.0 + 0.01 * index),
                4.0 + index,
                2.0 + 0.1 * index,
            };
        }
        if(direct.push_frame(direct_frame) != rt::ErrorCode::ok) {
            return 1;
        }
        direct.cycle();
    }
    result.direct_adversarial_us =
        1000.0 * millis_since(start) / DirectAdversarialCycles;
    result.checksum += direct.read_setpoint_frame().joints[0].position;

    stream::JointStreamGroup fast;
    if(fast.configure_frame(
           make_h1_stream_config(stream::JointFrameMode::upsample, true)) !=
           rt::ErrorCode::ok ||
       !reset_h1_stream(fast)) {
        return 1;
    }
    stream::JointCommandFrame fast_frame{};
    fast_frame.joint_count = fast.joint_count();
    start = std::clock();
    for(int cycle = 0; cycle < UpsampleFastCycles; ++cycle) {
        fast_frame.timestamp_cycles = cycle + 1;
        const double position = 0.001 * static_cast<double>(cycle);
        for(std::size_t joint = 0; joint < fast_frame.joint_count; ++joint) {
            fast_frame.joints[joint] = {position, 0.001, 0.0, 4.0, 2.0};
        }
        if(fast.push_frame(fast_frame) != rt::ErrorCode::ok) {
            return 1;
        }
        fast.cycle();
        if(fast.pending_replans() != 0 || fast.deferred_replans() != 0) {
            return 1;
        }
    }
    result.upsample_fast_us =
        1000.0 * millis_since(start) / UpsampleFastCycles;
    result.checksum += fast.read_setpoint_frame().joints[0].position;

    stream::JointStreamGroup slow;
    if(slow.configure_frame(
           make_h1_stream_config(stream::JointFrameMode::upsample, false)) !=
           rt::ErrorCode::ok ||
       !reset_h1_stream(slow)) {
        return 1;
    }
    stream::JointCommandFrame slow_frame{};
    slow_frame.joint_count = slow.joint_count();
    start = std::clock();
    for(int cycle = 0; cycle < UpsampleSlowCycles; ++cycle) {
        const double direction = cycle % 2 == 0 ? 1.0 : -1.0;
        slow_frame.timestamp_cycles = cycle + 1;
        for(std::size_t joint = 0; joint < slow_frame.joint_count; ++joint) {
            slow_frame.joints[joint] = {
                direction * (0.5 + 0.001 * static_cast<double>(joint)),
                0.0,
                direction,
                4.0,
                2.0,
            };
        }
        if(slow.push_frame(slow_frame) != rt::ErrorCode::ok) {
            return 1;
        }
        slow.cycle();
    }
    result.upsample_slow_budget_us =
        1000.0 * millis_since(start) / UpsampleSlowCycles;
    result.checksum += slow.read_setpoint_frame().joints[0].position;
    if(slow.pending_replans() !=
           stream::JointStreamGroup::MaxJoints -
               stream::JointStreamGroup::SlowReplansPerCycle ||
       slow.deferred_replans() !=
           static_cast<std::uint64_t>(UpsampleSlowCycles) *
               (stream::JointStreamGroup::MaxJoints -
                stream::JointStreamGroup::SlowReplansPerCycle)) {
        return 1;
    }
    return 0;
}

StCalibration calibrate_st_scan(const char *source)
{
    using namespace plcopen::core;

    const st::CompileResult compiled = st::compile(source);
    if(!compiled.ok) return {};
    const st::WcetReport report = st::make_wcet_report(compiled.program);
    if(!report.bounded || report.worst_case_work_units == 0 ||
       report.worst_case_work_units > static_cast<std::uint64_t>(
                                          std::numeric_limits<std::int64_t>::max()))
        return {};

    alignas(8) static unsigned char storage[65536] = {};
    st::Instance instance;
    if(instance.load(compiled.program, "main", storage, sizeof(storage),
                     1000000) != rt::ErrorCode::ok)
        return {};

    constexpr int WarmupScans = 100;
    constexpr int CalibrationScans = 200000;
    const std::int64_t budget =
        static_cast<std::int64_t>(report.worst_case_work_units);
    for(int i = 0; i < WarmupScans; ++i)
        if(instance.scan(budget) != st::ScanError::ok) return {};

    const auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < CalibrationScans; ++i)
        if(instance.scan(budget) != st::ScanError::ok) return {};
    const double observed_ns = std::chrono::duration<double, std::nano>(
                                   std::chrono::steady_clock::now() - start)
                                   .count() /
        CalibrationScans;
    if(observed_ns <= 0.0) return {};
    return {true, report.requires_native_fb_profile,
            report.worst_case_work_units, report.native_fb_instances,
            observed_ns};
}

StThroughput calibrate_st_throughput()
{
    using namespace plcopen::core;

    // Metric 5.9: the established mixed arithmetic/control-flow workload.
    const st::CompileResult compiled = st::compile(
        "PROGRAM p\n"
        "VAR i : DINT; s : DINT; END_VAR\n"
        "FOR i := 1 TO 100000 DO s := s + i * 2 - 1; END_FOR;\n"
        "END_PROGRAM\n");
    if(!compiled.ok) return {};

    alignas(8) static unsigned char storage[65536] = {};
    st::Instance instance;
    constexpr std::int64_t Budget = 10000000;
    if(instance.load(compiled.program, storage, sizeof(storage), 1000000) !=
           rt::ErrorCode::ok ||
       instance.scan(Budget) != st::ScanError::ok ||
       instance.begin(Budget) != st::ScanError::ok)
        return {};

    std::int64_t executed = 0;
    const auto start = std::chrono::steady_clock::now();
    const st::ScanError result = instance.resume(
        std::numeric_limits<std::int64_t>::max(), executed);
    const double observed_ns = std::chrono::duration<double, std::nano>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
    if(result != st::ScanError::ok || executed <= 0 || observed_ns <= 0.0)
        return {};
    return {true, executed, observed_ns / static_cast<double>(executed)};
}

struct SerialChainBenchmark
{
    double ik_us = 0.0;
    double checksum = 0.0;
};

int run_serial_chain_benchmark(bool enforce_budget, SerialChainBenchmark &result)
{
    using namespace plcopen::core;

    // H2 numerical IK budget: the shared seven-link fixture has alternating
    // twists, so its redundancy is distributed across the arm rather than
    // supplied by a zero-length axis coaxial with the wrist. Each solve starts
    // from a nearby 1e-5-rad seed, which is the cycle-path hot-start contract;
    // cold-start exhaustion remains a correctness/error-classification test.
    const kin::SerialChainSpec spec = test_support::seven_dof_arm_spec();
    const kin::SerialChain chain(spec);
    const double target_joints[2][7] = {
        {0.2, -0.6, 0.8, -1.0, 0.7, 0.5, -0.3},
        {0.21, -0.59, 0.79, -0.99, 0.71, 0.49, -0.29}};
    const double seeds[2][7] = {
        {0.20001, -0.60001, 0.80001, -0.99999, 0.69999, 0.50001, -0.29999},
        {0.20999, -0.58999, 0.78999, -0.99001, 0.71001, 0.48999, -0.29001}};
    const double preferred[7] = {0.5, -0.3, 0.4, -0.7, 0.4, 0.2, 0.1};
    kin::SerialChainSolveOptions options{};
    options.preferred_joints = preferred;
    options.preference_weight = 1e-4;
    kin::Pose6 targets[2];
    chain.forward(target_joints[0], targets[0]);
    chain.forward(target_joints[1], targets[1]);
    double solved[7] = {};
    for(int i = 0; i < 100; ++i) {
        if(chain.solve(targets[i & 1], seeds[i & 1], 0.1, options, solved) !=
           rt::ErrorCode::ok) {
            std::printf("BENCH_FAIL serial chain warmup\n");
            return 1;
        }
    }
    double seed_preference_cost = 0.0;
    double solved_preference_cost = 0.0;
    for(std::size_t joint = 0; joint < 7; ++joint) {
        const double seed_difference = preferred[joint] - seeds[1][joint];
        const double solved_difference = preferred[joint] - solved[joint];
        seed_preference_cost += seed_difference * seed_difference;
        solved_preference_cost += solved_difference * solved_difference;
    }
    if(!(solved_preference_cost < seed_preference_cost)) {
        std::printf("BENCH_FAIL serial chain preference path\n");
        return 1;
    }
    constexpr int SerialIterations = 20000;
    const std::clock_t start = std::clock();
    for(int i = 0; i < SerialIterations; ++i) {
        if(chain.solve(targets[i & 1], seeds[i & 1], 0.1, options, solved) !=
           rt::ErrorCode::ok) {
            std::printf("BENCH_FAIL serial chain solve\n");
            return 1;
        }
        result.checksum += solved[0] + solved[6];
    }
    result.ik_us = 1000.0 * millis_since(start) / SerialIterations;
    if(enforce_budget && result.ik_us > 30.0) {
        std::printf("BENCH_FAIL serial_chain_ik_us=%.2f exceeds 30us gate\n",
                    result.ik_us);
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    using namespace plcopen::core;

    if(argc == 2 && std::strcmp(argv[1], "--serial-chain-only") == 0) {
        SerialChainBenchmark serial_chain;
        if(run_serial_chain_benchmark(true, serial_chain) != 0) {
            return 1;
        }
        std::printf(
            "SERIAL_CHAIN_METRICS serial_chain_ik_us=%.3f serial_chain_budget_us=30\n",
            serial_chain.ik_us);
        return 0;
    }
    if(argc != 1) {
        std::printf("BENCH_FAIL unknown benchmark selector\n");
        return 2;
    }

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

    // A2 observations are environment-scoped calibration, never a certified
    // wall-clock bound.  Compilation/allocation happen before scan timing.
    const StCalibration st_scalar = calibrate_st_scan(
        "PROGRAM Main VAR A : DINT := 3; B : DINT; END_VAR "
        "B := A * 7 + 2; END_PROGRAM");
    const StCalibration st_native = calibrate_st_scan(
        "PROGRAM Main VAR Edge : R_TRIG; Q : BOOL; END_VAR "
        "Edge(CLK := TRUE); Q := Edge.Q; END_PROGRAM");
    const StThroughput st_mixed = calibrate_st_throughput();
    if(!st_scalar.ok || !st_native.ok ||
       !st_native.native_profile_required ||
       st_native.native_fb_instances == 0 || !st_mixed.ok) {
        std::printf("BENCH_FAIL st calibration\n");
        return 1;
    }
    const double st_scalar_ns_per_work_unit =
        st_scalar.observed_ns_per_scan /
        static_cast<double>(st_scalar.work_units);

    // E5 planning-domain trend: rebuild the full 64-segment look-ahead
    // window repeatedly.  The volatile window length prevents the optimizer
    // from hoisting the deterministic replan out of the measurement loop.
    constexpr std::size_t WindowSegments = 64;
    constexpr int WindowReplanIterations = 200000;
    plan::PathBuffer<WindowSegments> replan_path;
    geom::Vec3 replan_start{};
    for(std::size_t i = 0; i < WindowSegments; ++i) {
        const geom::Vec3 replan_finish{
            static_cast<double>(i + 1),
            (i % 2 == 0) ? 0.25 : -0.25,
            0.0};
        const rt::Result<geom::LineSegment> segment =
            geom::make_line(replan_start, replan_finish);
        if(!segment ||
           replan_path.push(geom::as_path_segment(segment.value())) !=
               rt::ErrorCode::ok) {
            std::printf("BENCH_FAIL window fixture\n");
            return 1;
        }
        replan_start = replan_finish;
    }
    volatile std::size_t replan_window = WindowSegments;
    double replan_checksum = 0.0;
    start = std::clock();
    for(int i = 0; i < WindowReplanIterations; ++i) {
        const rt::Result<plan::LookAheadPlan<WindowSegments>> replanned =
            plan::compute_lookahead(replan_path, 4.0, 2.0, replan_window);
        if(!replanned) {
            std::printf("BENCH_FAIL window replan\n");
            return 1;
        }
        for(std::size_t j = 0; j < WindowSegments; ++j) {
            replan_checksum += replanned.value().entry_speed[j] +
                replanned.value().exit_speed[j];
        }
    }
    const double window_replan_us =
        1000.0 * millis_since(start) / WindowReplanIterations;
    position_sum += replan_checksum * 1.0e-12;

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

    H1StreamBenchmark h1_stream;
    if(run_h1_stream_benchmark(h1_stream) != 0) {
        std::printf("BENCH_FAIL H1 stream setup or bounded-replan invariant\n");
        return 1;
    }
    position_sum += h1_stream.checksum;
    if(BenchCalibrationEligible != 0 &&
       (h1_stream.direct_steady_us > 300.0 ||
        h1_stream.direct_adversarial_us > 300.0 ||
        h1_stream.upsample_fast_us > 300.0 ||
        h1_stream.upsample_slow_budget_us > 300.0)) {
        std::printf(
            "BENCH_FAIL H1 stream cycle budget direct_steady=%.2f "
            "direct_adversarial=%.2f upsample_fast=%.2f "
            "upsample_slow_budget=%.2f budget_us=300\n",
            h1_stream.direct_steady_us, h1_stream.direct_adversarial_us,
            h1_stream.upsample_fast_us,
            h1_stream.upsample_slow_budget_us);
        return 1;
    }

    // Cartesian-interpolation budget gate (approved matrix decision #11):
    // a 6R pose group rides Cartesian segments through the per-cycle
    // analytic inverse; the measured per-cycle cost carries the hard 50 us
    // gate, and the 250 us @4kHz tier conclusion goes to the matrix
    // implementation record.
    double cartesian_ik_us = 0.0;
    {
        static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
        static axis::AxisModel pose_axes[6];
        static axis::AxisGroup pose_group;
        for(auto &axis_model : pose_axes) {
            axis_model.set_power(true);
            pose_group.add_axis(axis_model);
        }
        pose_group.enable();
        if(pose_group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
            std::printf("BENCH_FAIL pose configure\n");
            return 1;
        }
        axis::GroupCommand approach{};
        approach.target.size = 6;
        const double q0[6] = {0.3, 0.6, 1.0, -0.4, 0.9, 0.2};
        for(int i = 0; i < 6; ++i) {
            approach.target.value[i] = q0[i];
        }
        approach.velocity = 0.05;
        approach.acceleration = 0.004;
        approach.deceleration = 0.004;
        approach.jerk = 0.004;
        pose_group.submit_linear(approach);
        for(int i = 0; i < 20000 && pose_group.status() != axis::GroupStatus::standby;
            ++i) {
            pose_group.cycle();
        }

        const double poses[2][6] = {{0.32, 0.18, 0.5, 0.5, -0.3, 0.9},
                                    {0.4, -0.1, 0.55, 0.1, 0.2, -0.6}};
        long cartesian_cycles = 0;
        start = std::clock();
        for(int leg = 0; leg < 8; ++leg) {
            axis::GroupCommand segment{};
            segment.target.size = 6;
            for(int i = 0; i < 6; ++i) {
                segment.target.value[i] = poses[leg % 2][i];
            }
            segment.velocity = 0.002;
            segment.acceleration = 0.0005;
            segment.deceleration = 0.0005;
            segment.jerk = 0.0005;
            segment.coord_system = axis::CoordSystem::mcs;
            segment.interpolation_space = axis::InterpolationSpace::cartesian;
            if(!pose_group.submit_linear(segment)) {
                std::printf("BENCH_FAIL cartesian submit\n");
                return 1;
            }
            for(int i = 0; i < 60000; ++i) {
                pose_group.cycle();
                ++cartesian_cycles;
                if(pose_group.status() == axis::GroupStatus::standby) {
                    break;
                }
            }
        }
        cartesian_ik_us = 1000.0 * millis_since(start) /
                          static_cast<double>(cartesian_cycles);
        position_sum += pose_axes[0].snapshot().command_position;
        if(cartesian_ik_us > 50.0) {
            std::printf("BENCH_FAIL cartesian_ik_cycle_us=%.2f exceeds 50us gate\n",
                        cartesian_ik_us);
            return 1;
        }
    }

    SerialChainBenchmark serial_chain;
    if(run_serial_chain_benchmark(false, serial_chain) != 0) {
        return 1;
    }
    position_sum += serial_chain.checksum * 1e-12;

    std::printf("CARTESIAN_METRICS cartesian_ik_cycle_us=%.3f budget_us=50\n",
                cartesian_ik_us);
    std::printf(
        "SERIAL_CHAIN_METRICS serial_chain_ik_us=%.3f serial_chain_budget_us=30\n",
        serial_chain.ik_us);
    std::printf("WINDOW_METRICS segments=%zu window_replan_us=%.3f\n",
                WindowSegments, window_replan_us);
    std::printf(
        "ST_WCET_METRICS platform=%s compiler=%s compiler_version=%d.%d.%d "
        "build=%s calibration_eligible=%d certified_wcet=0 "
        "scalar_work_units=%llu scalar_observed_ns_per_scan=%.3f "
        "scalar_observed_ns_per_work_unit=%.3f native_work_units=%llu "
        "native_fb_instances=%u native_observed_ns_per_scan=%.3f "
        "mixed_instructions=%lld mixed_observed_ns_per_instruction=%.3f\n",
        BenchPlatform, BenchCompiler, BenchCompilerMajor, BenchCompilerMinor,
        BenchCompilerPatch, BenchBuild, BenchCalibrationEligible,
        static_cast<unsigned long long>(st_scalar.work_units),
        st_scalar.observed_ns_per_scan, st_scalar_ns_per_work_unit,
        static_cast<unsigned long long>(st_native.work_units),
        st_native.native_fb_instances, st_native.observed_ns_per_scan,
        static_cast<long long>(st_mixed.instructions),
        st_mixed.observed_ns_per_instruction);
    std::printf("BENCH_BASELINE static_vector_ms=%.3f spsc_ms=%.3f sample_ms=%.3f "
                "path_sample_ms=%.3f axis_cycle_pair_ms=%.3f group_circular_cycle_ms=%.3f "
                "checksum=%.3f\n",
                vector_ms, queue_ms, sample_ms, path_sample_ms, axis_cycle_ms,
                group_circular_cycle_ms, position_sum + sink);
    std::printf("STREAM_METRICS joints=28 stagger_us_per_cycle=%.2f burst_us_per_cycle=%.2f "
                "budget_us=300\n",
                stream_stagger_us, stream_burst_us);
    std::printf(
        "STREAM_METRICS h1_joints=48 direct_steady_us_per_cycle=%.2f "
        "direct_adversarial_us_per_cycle=%.2f "
        "upsample_fast_us_per_cycle=%.2f "
        "upsample_slow_budget_us_per_cycle=%.2f slow_replans_per_cycle=%zu "
        "budget_us=300\n",
        h1_stream.direct_steady_us, h1_stream.direct_adversarial_us,
        h1_stream.upsample_fast_us, h1_stream.upsample_slow_budget_us,
        stream::JointStreamGroup::SlowReplansPerCycle);
    std::printf("PATH_METRICS speed_ripple=%.6f path_error=%.12f cycle_efficiency=%.6f "
                "blend_deviation=%.12f cam_error=%.12f overlay_checksum=%.6f "
                "otg_duration_vs_baseline=%.4f\n",
                speed_ripple, path_error, committed.total_length() / path_buffer.total_length(),
                blend.curve.quintic.max_deviation, cam_error, overlay_checksum,
                cycle_time_efficiency);
    return 0;
}
