// Reference executor, software shape (X3, architecture.md figures 3/4):
// a cycle thread drives the group + ServoSim bridge at a fixed period and
// publishes state through a seqlock snapshot; a low-priority planner
// thread submits path commands and reads the snapshot. On Linux the cycle
// thread attempts SCHED_FIFO and absolute-deadline sleeping (degrading
// gracefully without privileges); on Windows the loop free-runs for the
// smoke tier — no real-time claim is made off a PREEMPT_RT target.
//
// Every cycle also lands in a fixed-size trace ring (X4) drained to a
// versioned binary file consumable by tools/plcopen_trace.py. This demo is
// executor-domain code: threads, atomics, and file IO are deliberately
// outside the core library and outside the RT-safety scan.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "adapters/servo.h"
#include "axis/group.h"
#include "axis/state.h"
#include "rt/spsc_queue.h"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace
{

using namespace plcopen::core;

// --- seqlock snapshot (figure 3): single writer, torn reads retried. ---
struct GroupSnapshot
{
    std::int64_t tick = 0;
    double position[2] = {0.0, 0.0};
    double velocity[2] = {0.0, 0.0};
    int status = 0;
};

class SeqlockSnapshot
{
public:
    void publish(const GroupSnapshot &snapshot)
    {
        const std::uint32_t begin = sequence_.load(std::memory_order_relaxed);
        sequence_.store(begin + 1, std::memory_order_release);
        payload_ = snapshot;
        sequence_.store(begin + 2, std::memory_order_release);
    }

    bool read(GroupSnapshot &out) const
    {
        for(int attempt = 0; attempt < 4; ++attempt) {
            const std::uint32_t before = sequence_.load(std::memory_order_acquire);
            if(before % 2 != 0) {
                continue;
            }
            out = payload_;
            const std::uint32_t after = sequence_.load(std::memory_order_acquire);
            if(before == after) {
                return true;
            }
        }
        return false;
    }

private:
    std::atomic<std::uint32_t> sequence_{0};
    GroupSnapshot payload_{};
};

// --- trace record (X4): versioned POD, drained to file post-run. ---
struct TraceRecord
{
    std::int64_t tick;
    std::int32_t axis;
    std::int32_t reserved;
    double position;
    double velocity;
    double acceleration;
};

constexpr std::uint32_t TraceVersion = 1;

bool write_trace(const char *path, const TraceRecord *records, std::size_t count)
{
    std::FILE *file = std::fopen(path, "wb");
    if(file == nullptr) {
        return false;
    }
    const char magic[4] = {'P', 'L', 'C', 'T'};
    const std::uint32_t version = TraceVersion;
    const std::uint32_t record_size = sizeof(TraceRecord);
    std::fwrite(magic, 1, 4, file);
    std::fwrite(&version, sizeof(version), 1, file);
    std::fwrite(&record_size, sizeof(record_size), 1, file);
    std::fwrite(records, sizeof(TraceRecord), count, file);
    std::fclose(file);
    return true;
}

void try_elevate_cycle_thread()
{
#if defined(__linux__)
    sched_param param{};
    param.sched_priority = 80;
    if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        std::printf("executor: SCHED_FIFO unavailable, running best-effort\n");
    }
#endif
}

} // namespace

int main(int argc, char **argv)
{
    long cycles = 4000;
    long period_ns = 1000000; // 1 kHz nominal
    const char *trace_path = "rt_executor_trace.bin";
    for(int i = 1; i + 1 < argc; i += 2) {
        if(std::strcmp(argv[i], "--cycles") == 0) {
            cycles = std::strtol(argv[i + 1], nullptr, 10);
        } else if(std::strcmp(argv[i], "--period-ns") == 0) {
            period_ns = std::strtol(argv[i + 1], nullptr, 10);
        } else if(std::strcmp(argv[i], "--trace") == 0) {
            trace_path = argv[i + 1];
        }
    }

    static axis::AxisModel x;
    static axis::AxisModel y;
    static axis::AxisGroup group;
    static adapters::ServoSim servo_x;
    static adapters::ServoSim servo_y;
    x.set_power(true);
    y.set_power(true);
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    static SeqlockSnapshot snapshot_store;
    std::atomic<bool> running{true};
    std::atomic<long> planner_reads{0};
    std::atomic<long> torn_reads{0};

    // Planner domain (figure 4): submits a zigzag of blended segments and
    // polls the snapshot at a leisurely rate.
    std::thread planner([&]() {
        axis::GroupCommand first{};
        first.target.size = 2;
        first.target.value[0] = 1.0;
        first.target.value[1] = 0.0;
        first.velocity = 0.01;
        first.acceleration = 0.002;
        first.deceleration = 0.002;
        first.jerk = 0.002;
        group.submit_linear(first);
        double px = 1.0;
        double py = 0.0;
        double heading = 0.0;
        for(int i = 0; i < 6 && running.load(); ++i) {
            heading += (i % 2 == 0 ? 1.0 : -1.0) * 0.35;
            px += 0.5 * std::cos(heading);
            py += 0.5 * std::sin(heading);
            axis::GroupCommand blend = first;
            blend.target.value[0] = px;
            blend.target.value[1] = py;
            blend.buffer_mode = axis::BufferMode::blending_low;
            blend.transition_mode = axis::TransitionMode::max_corner_deviation;
            blend.transition_parameter = 0.02;
            group.submit_linear(blend);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        GroupSnapshot seen{};
        while(running.load()) {
            if(snapshot_store.read(seen)) {
                planner_reads.fetch_add(1);
            } else {
                torn_reads.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Cycle domain (figure 3): fixed-period loop, servo bridge, seqlock
    // publish, trace ring push.
    static rt::SpscQueue<TraceRecord, 65536> trace_ring;
    static TraceRecord drained[65536];
    std::size_t drained_count = 0;
    long overruns = 0;

    try_elevate_cycle_thread();
#if defined(__linux__)
    timespec deadline{};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#endif
    const auto start = std::chrono::steady_clock::now();
    for(long tick = 0; tick < cycles; ++tick) {
        group.cycle();

        axis::AxisModel *axes[2] = {&x, &y};
        adapters::ServoSim *servos[2] = {&servo_x, &servo_y};
        for(int a = 0; a < 2; ++a) {
            const adapters::ServoSetpoints setpoints =
                adapters::make_setpoints(axes[a]->snapshot());
            servos[a]->write_setpoints(setpoints);
            adapters::ServoFeedback feedback{};
            servos[a]->read_feedback(feedback);
            adapters::bridge_feedback(*axes[a], feedback);

            TraceRecord record{};
            record.tick = tick;
            record.axis = a;
            record.position = axes[a]->snapshot().command_position;
            record.velocity = axes[a]->snapshot().command_velocity;
            record.acceleration = axes[a]->snapshot().command_acceleration;
            if(!trace_ring.push(record) && drained_count == 0) {
                // Ring full: drain in-place (demo-domain shortcut).
                TraceRecord sink{};
                while(trace_ring.pop(sink) && drained_count < 65536) {
                    drained[drained_count++] = sink;
                }
                trace_ring.push(record);
            }
        }

        GroupSnapshot snapshot{};
        snapshot.tick = tick;
        snapshot.position[0] = x.snapshot().command_position;
        snapshot.position[1] = y.snapshot().command_position;
        snapshot.velocity[0] = x.snapshot().command_velocity;
        snapshot.velocity[1] = y.snapshot().command_velocity;
        snapshot.status = static_cast<int>(group.status());
        snapshot_store.publish(snapshot);

#if defined(__linux__)
        deadline.tv_nsec += period_ns;
        while(deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec += 1;
        }
        if(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) != 0) {
            ++overruns;
        }
#else
        // Windows smoke tier: coarse pacing only (no RT claim) so the
        // planner thread genuinely interleaves with the cycle domain.
        (void)period_ns;
        if(tick % 8 == 7) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    running.store(false);
    planner.join();

    TraceRecord sink{};
    while(trace_ring.pop(sink) && drained_count < 65536) {
        drained[drained_count++] = sink;
    }
    if(!write_trace(trace_path, drained, drained_count)) {
        std::printf("executor: cannot write trace %s\n", trace_path);
        return 1;
    }

    const double ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(elapsed)
            .count();
    std::printf("EXECUTOR cycles=%ld elapsed_ms=%.1f snapshot_reads=%ld "
                "torn_reads=%ld overruns=%ld trace_records=%zu final=(%.6f, %.6f) "
                "status=%d\n",
                cycles, ms, planner_reads.load(), torn_reads.load(), overruns,
                drained_count, x.snapshot().command_position,
                y.snapshot().command_position, static_cast<int>(group.status()));
    const bool healthy = drained_count > 0 && planner_reads.load() > 0;
    std::printf("EXECUTOR %s\n", healthy ? "PASS" : "FAIL");
    return healthy ? 0 : 1;
}
