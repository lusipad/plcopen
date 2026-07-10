// Reference single-writer executor handoff (X3): one executor context owns
// AxisGroup, drives ServoSim, and publishes state through an SPSC snapshot
// queue; a producer thread only enqueues path commands and reads snapshots.
// Because submit_linear still performs planning in the executor loop, this
// demo is not the canonical planning -> committed trajectory -> RT split and
// makes no end-to-end hard-real-time claim. Linux scheduling only demonstrates
// the intended fixed-period shape; Windows uses a coarse smoke-tier cadence.
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
#include <type_traits>

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

struct GroupSnapshot
{
    std::int64_t tick = 0;
    double position[2] = {0.0, 0.0};
    double velocity[2] = {0.0, 0.0};
    int status = 0;
};

// Seven demo commands fit without producer back-pressure. The snapshot
// capacity covers the 4000-cycle default smoke run while the producer drains
// batches at its lower polling rate.
constexpr std::size_t CommandQueueCapacity = 8;
constexpr std::size_t SnapshotQueueCapacity = 4096;
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "executor SPSC indices must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free,
              "executor run flag must be lock-free");
static_assert(std::is_trivially_copyable<axis::GroupCommand>::value,
              "executor commands must remain allocation-free queue payloads");
static_assert(std::is_trivially_copyable<GroupSnapshot>::value,
              "executor snapshots must remain allocation-free queue payloads");

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

void try_elevate_executor_thread()
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

    static rt::SpscQueue<axis::GroupCommand, CommandQueueCapacity> command_queue;
    static rt::SpscQueue<GroupSnapshot, SnapshotQueueCapacity> snapshot_queue;
    std::atomic<bool> running{true};
    long commands_enqueued = 0;
    long command_queue_full = 0;
    long snapshot_reads = 0;

    // Producer thread: enqueues a zigzag of blended segments and drains
    // snapshots at a leisurely rate. It never accesses AxisGroup.
    std::thread producer([&]() {
        const auto enqueue = [&](const axis::GroupCommand &command) {
            if(command_queue.push(command)) {
                ++commands_enqueued;
            } else {
                ++command_queue_full;
            }
        };

        axis::GroupCommand first{};
        first.target.size = 2;
        first.target.value[0] = 1.0;
        first.target.value[1] = 0.0;
        first.velocity = 0.01;
        first.acceleration = 0.002;
        first.deceleration = 0.002;
        first.jerk = 0.002;
        enqueue(first);
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
            enqueue(blend);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        GroupSnapshot seen{};
        while(running.load()) {
            while(snapshot_queue.pop(seen)) {
                ++snapshot_reads;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while(snapshot_queue.pop(seen)) {
            ++snapshot_reads;
        }
    });

    // Single-writer executor context: consumes commands at loop boundaries,
    // advances the cycle, then publishes snapshots and trace records.
    static rt::SpscQueue<TraceRecord, 65536> trace_ring;
    static TraceRecord drained[65536];
    std::size_t drained_count = 0;
    long overruns = 0;
    long commands_consumed = 0;
    long command_rejections = 0;
    long snapshots_published = 0;
    long snapshot_queue_full = 0;

    const auto consume_pending_commands = [&]() {
        axis::GroupCommand command{};
        while(command_queue.pop(command)) {
            ++commands_consumed;
            if(!group.submit_linear(command)) {
                ++command_rejections;
            }
        }
    };
    const auto publish_snapshot = [&](std::int64_t tick) {
        GroupSnapshot snapshot{};
        snapshot.tick = tick;
        snapshot.position[0] = x.snapshot().command_position;
        snapshot.position[1] = y.snapshot().command_position;
        snapshot.velocity[0] = x.snapshot().command_velocity;
        snapshot.velocity[1] = y.snapshot().command_velocity;
        snapshot.status = static_cast<int>(group.status());
        if(snapshot_queue.push(snapshot)) {
            ++snapshots_published;
        } else {
            ++snapshot_queue_full;
        }
    };

    try_elevate_executor_thread();
#if defined(__linux__)
    timespec deadline{};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#endif
    const auto start = std::chrono::steady_clock::now();
    for(long tick = 0; tick < cycles; ++tick) {
        consume_pending_commands();
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

        publish_snapshot(tick);

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
        // producer thread genuinely interleaves with the executor context.
        (void)period_ns;
        if(tick % 8 == 7) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    running.store(false);
    producer.join();
    consume_pending_commands();
    publish_snapshot(cycles);
    GroupSnapshot final_snapshot{};
    const bool final_snapshot_received =
        snapshot_queue.pop(final_snapshot) && final_snapshot.tick == cycles;
    if(final_snapshot_received) {
        ++snapshot_reads;
    }

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
    std::printf("EXECUTOR cycles=%ld elapsed_ms=%.1f commands_enqueued=%ld "
                "commands_consumed=%ld command_queue_full=%ld command_rejections=%ld "
                "snapshot_published=%ld snapshot_reads=%ld snapshot_queue_full=%ld "
                "overruns=%ld trace_records=%zu final=(%.6f, %.6f) status=%d\n",
                cycles, ms, commands_enqueued, commands_consumed, command_queue_full,
                command_rejections, snapshots_published, snapshot_reads,
                snapshot_queue_full, overruns, drained_count, x.snapshot().command_position,
                y.snapshot().command_position, static_cast<int>(group.status()));
    const bool command_handoff_healthy =
        commands_enqueued > 0 && commands_consumed == commands_enqueued &&
        command_queue_full == 0;
    const bool snapshot_handoff_healthy =
        snapshots_published > 0 && snapshot_reads > 0 && snapshot_queue_full == 0 &&
        final_snapshot_received;
    const bool healthy =
        drained_count > 0 && command_handoff_healthy && snapshot_handoff_healthy;
    std::printf("EXECUTOR %s\n", healthy ? "PASS" : "FAIL");
    return healthy ? 0 : 1;
}
