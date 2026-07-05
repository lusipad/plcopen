// Deterministic replay regression for the rewrite core (A7/R3.9 gate).
//
// Default mode re-runs the fixed scenarios and compares every emitted setpoint
// sample against the committed golden JSONL fixtures; any undeclared semantic
// change in the axis/group cycle path shows up as a diff. --record rewrites
// the golden files (only for reviewed, declared changes).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "axis/group.h"
#include "axis/state.h"

namespace
{

using namespace plcopen::core;

struct Sample
{
    std::int64_t tick = 0;
    int axis = 0;
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

constexpr std::size_t kMaxSamples = 4096;

struct Recording
{
    const char *id = nullptr;
    Sample samples[kMaxSamples];
    std::size_t count = 0;

    void emit(std::int64_t tick, int axis, const axis::AxisSnapshot &snapshot)
    {
        if(count >= kMaxSamples) {
            return;
        }
        samples[count++] = Sample{tick,
                                  axis,
                                  snapshot.command_position,
                                  snapshot.command_velocity,
                                  snapshot.command_acceleration};
    }
};

axis::AxisCommand make_move(double target, double velocity)
{
    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = target;
    move.velocity = velocity;
    move.acceleration = 0.01;
    move.deceleration = 0.01;
    move.jerk = 0.01;
    return move;
}

void run_single_axis_move(Recording &recording)
{
    // v2: declared change — discrete moves plan through otg::plan_time_optimal
    // (KB-026); v1 recorded the baseline single-quintic profiles.
    recording.id = "core-single-axis-move-v2";
    axis::AxisModel axis;
    axis.set_power(true);
    axis.submit(make_move(3.0, 0.05));
    for(std::int64_t tick = 0; tick < 2000; ++tick) {
        axis.cycle();
        recording.emit(tick, 0, axis.snapshot());
        if(axis.status() == axis::AxisStatus::standstill) {
            break;
        }
    }
}

void run_velocity_stop(Recording &recording)
{
    // v2: declared change — MC_Stop decelerates from the takeover velocity
    // with a controlled halt profile (KB-028); v1 recorded the immediate stop.
    recording.id = "core-velocity-stop-v2";
    axis::AxisModel axis;
    axis.set_power(true);

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.02;
    axis.submit(velocity);
    std::int64_t tick = 0;
    for(; tick < 20; ++tick) {
        axis.cycle();
        recording.emit(tick, 0, axis.snapshot());
    }

    axis::AxisCommand stop{};
    stop.kind = axis::CommandKind::stop;
    axis.submit(stop);
    for(; tick < 80; ++tick) {
        axis.cycle();
        recording.emit(tick, 0, axis.snapshot());
        if(axis.status() == axis::AxisStatus::standstill) {
            break;
        }
    }
}

void run_group_linear(Recording &recording)
{
    // v2: declared change — the shared path parameter is planned as one
    // jerk-limited profile honoring the command dynamics (KB-027); v1
    // recorded the constant-velocity linear interpolation.
    recording.id = "core-group-linear-v2";
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 3.0;
    linear.target.value[1] = 4.0;
    linear.velocity = 0.25;
    group.submit_linear(linear);
    for(std::int64_t tick = 0; tick < 200; ++tick) {
        group.cycle();
        recording.emit(tick, 0, x.snapshot());
        recording.emit(tick, 1, y.snapshot());
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
}

void run_group_circular(Recording &recording)
{
    // A3: BORDER quarter arc (radius 1) after a linear approach onto the
    // circle; the arc-length path parameter drives both plane axes.
    recording.id = "core-group-circular";
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    axis::GroupCommand approach{};
    approach.target.size = 2;
    approach.target.value[0] = 1.0;
    approach.target.value[1] = 0.0;
    approach.velocity = 0.25;
    group.submit_linear(approach);
    std::int64_t tick = 0;
    for(; tick < 200; ++tick) {
        group.cycle();
        recording.emit(tick, 0, x.snapshot());
        recording.emit(tick, 1, y.snapshot());
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    axis::GroupCommand arc{};
    arc.target.size = 2;
    arc.aux.size = 2;
    arc.aux.value[0] = 0.70710678118654752;
    arc.aux.value[1] = 0.70710678118654752;
    arc.target.value[0] = 0.0;
    arc.target.value[1] = 1.0;
    arc.velocity = 0.1;
    arc.path_choice = axis::CircPathChoice::counter_clockwise;
    group.submit_circular(arc);
    for(; tick < 600; ++tick) {
        group.cycle();
        recording.emit(tick, 0, x.snapshot());
        recording.emit(tick, 1, y.snapshot());
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
}

void run_group_blend(Recording &recording)
{
    // A4: shallow-corner quintic blend chain (KB-031) — the successor with
    // MaxCornerDeviation fuses the two linear legs into one Euclidean chain
    // profile that passes the corner without stopping.
    recording.id = "core-group-blend";
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    axis::GroupCommand first{};
    first.target.size = 2;
    first.target.value[0] = 2.0;
    first.target.value[1] = 0.0;
    first.velocity = 0.05;
    first.acceleration = 0.002;
    first.deceleration = 0.002;
    first.jerk = 0.002;
    group.submit_linear(first);
    std::int64_t tick = 0;
    for(; tick < 5; ++tick) {
        group.cycle();
        recording.emit(tick, 0, x.snapshot());
        recording.emit(tick, 1, y.snapshot());
    }

    axis::GroupCommand blend = first;
    blend.target.value[0] = 3.73205080756888;
    blend.target.value[1] = 1.0;
    blend.buffer_mode = axis::BufferMode::blending_high;
    blend.transition_mode = axis::TransitionMode::max_corner_deviation;
    blend.transition_parameter = 0.05;
    group.submit_linear(blend);
    for(; tick < 1500; ++tick) {
        group.cycle();
        recording.emit(tick, 0, x.snapshot());
        recording.emit(tick, 1, y.snapshot());
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
}

using ScenarioRunner = void (*)(Recording &);
constexpr ScenarioRunner kScenarios[] = {run_single_axis_move, run_velocity_stop,
                                         run_group_linear, run_group_circular,
                                         run_group_blend};

int write_recording(const Recording &recording, const std::string &directory)
{
    const std::string path = directory + "/" + recording.id + ".jsonl";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if(file == nullptr) {
        std::printf("FAIL cannot write %s\n", path.c_str());
        return 1;
    }
    for(std::size_t i = 0; i < recording.count; ++i) {
        const Sample &sample = recording.samples[i];
        std::fprintf(file,
                     "{\"tick\":%lld,\"axis\":%d,\"position\":%.17g,\"velocity\":%.17g,"
                     "\"acceleration\":%.17g,\"source\":\"%s\"}\n",
                     static_cast<long long>(sample.tick),
                     sample.axis,
                     sample.position,
                     sample.velocity,
                     sample.acceleration,
                     recording.id);
    }
    std::fclose(file);
    std::printf("recorded %s (%zu samples)\n", path.c_str(), recording.count);
    return 0;
}

bool near(double lhs, double rhs)
{
    const double magnitude = std::fabs(lhs) > std::fabs(rhs) ? std::fabs(lhs) : std::fabs(rhs);
    return std::fabs(lhs - rhs) <= 1e-12 + 1e-9 * magnitude;
}

int compare_recording(const Recording &recording, const std::string &directory)
{
    const std::string path = directory + "/" + recording.id + ".jsonl";
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if(file == nullptr) {
        std::printf("FAIL missing golden fixture %s\n", path.c_str());
        return 1;
    }

    char line[512];
    std::size_t index = 0;
    while(std::fgets(line, sizeof(line), file) != nullptr) {
        if(line[0] == '\0' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        long long tick = 0;
        int axis = 0;
        double position = 0.0;
        double velocity = 0.0;
        double acceleration = 0.0;
        const int parsed = std::sscanf(line,
                                       "{\"tick\":%lld,\"axis\":%d,\"position\":%lg,"
                                       "\"velocity\":%lg,\"acceleration\":%lg",
                                       &tick,
                                       &axis,
                                       &position,
                                       &velocity,
                                       &acceleration);
        if(parsed != 5) {
            std::printf("FAIL %s line %zu is not parseable\n", path.c_str(), index + 1);
            std::fclose(file);
            return 1;
        }
        if(index >= recording.count) {
            std::printf("FAIL %s has more samples than the replay (%zu)\n", path.c_str(),
                        recording.count);
            std::fclose(file);
            return 1;
        }
        const Sample &sample = recording.samples[index];
        if(sample.tick != tick || sample.axis != axis || !near(sample.position, position) ||
           !near(sample.velocity, velocity) || !near(sample.acceleration, acceleration)) {
            std::printf("FAIL %s diverges at sample %zu (tick %lld axis %d)\n", path.c_str(),
                        index, tick, axis);
            std::printf("  golden  p=%.17g v=%.17g a=%.17g\n", position, velocity, acceleration);
            std::printf("  replay  p=%.17g v=%.17g a=%.17g\n", sample.position, sample.velocity,
                        sample.acceleration);
            std::fclose(file);
            return 1;
        }
        ++index;
    }
    std::fclose(file);

    if(index != recording.count) {
        std::printf("FAIL %s sample count %zu != replay %zu\n", path.c_str(), index,
                    recording.count);
        return 1;
    }
    std::printf("replay match %s (%zu samples)\n", recording.id, recording.count);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    bool record = false;
    std::string directory;
    if(argc == 3 && std::strcmp(argv[1], "--record") == 0) {
        record = true;
        directory = argv[2];
    } else if(argc == 2) {
        directory = argv[1];
    } else {
        std::printf("usage: %s [--record] <fixture-dir>\n", argv[0]);
        return 2;
    }

    int failures = 0;
    for(ScenarioRunner scenario : kScenarios) {
        Recording recording{};
        scenario(recording);
        if(recording.count == 0) {
            std::printf("FAIL scenario produced no samples\n");
            ++failures;
            continue;
        }
        failures += record ? write_recording(recording, directory)
                           : compare_recording(recording, directory);
    }

    if(failures != 0) {
        return 1;
    }
    std::printf("PASS core replay regression\n");
    return 0;
}
