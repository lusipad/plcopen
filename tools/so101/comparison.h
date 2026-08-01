#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace plcopen::tools::so101
{

inline constexpr std::size_t ComparisonJointCount = 6;
inline constexpr int ComparisonCycleHz = 50;
inline constexpr int ComparisonTotalCycles = 450;
inline constexpr int ComparisonTargetInterval = 5;
inline constexpr int ComparisonDropoutBegin = 150;
inline constexpr int ComparisonDropoutEnd = 300;
inline constexpr double ComparisonMaxVelocityPerCycle = 1.0 / ComparisonCycleHz;
inline constexpr double ComparisonMaxAccelerationPerCycle =
    5.0 / (ComparisonCycleHz * ComparisonCycleHz);
inline constexpr double ComparisonMaxJerkPerCycle =
    50.0 / (ComparisonCycleHz * ComparisonCycleHz * ComparisonCycleHz);

enum class ComparisonMode
{
    naive,
    plcopen,
};

enum class ComparisonScenario
{
    wave,
    dropout,
};

struct ComparisonSample
{
    std::int64_t tick = 0;
    bool target_valid = false;
    bool all_members_stopped = false;
    std::uint64_t frame_sequence = 0;
    std::uint32_t dropout_count = 0;
    std::uint32_t filter_faults = 0;
    std::array<double, ComparisonJointCount> target{};
    std::array<double, ComparisonJointCount> command_position{};
    std::array<double, ComparisonJointCount> command_velocity{};
    std::array<double, ComparisonJointCount> command_acceleration{};
};

struct ComparisonMetrics
{
    double max_abs_position_delta = 0.0;
    double max_abs_velocity = 0.0;
    double max_abs_acceleration = 0.0;
    double max_abs_jerk = 0.0;
};

struct ComparisonRun
{
    ComparisonMode mode = ComparisonMode::naive;
    ComparisonScenario scenario = ComparisonScenario::wave;
    std::vector<ComparisonSample> samples;
    ComparisonMetrics metrics{};
    std::size_t accepted_frames = 0;
    std::uint32_t dropout_count = 0;
    std::uint32_t filter_faults = 0;
};

const char *comparison_mode_name(ComparisonMode mode);
const char *comparison_scenario_name(ComparisonScenario scenario);

bool run_comparison(ComparisonMode mode, ComparisonScenario scenario,
                    ComparisonRun &run);
bool write_comparison_csv(std::ostream &output, const ComparisonRun &run);

} // namespace plcopen::tools::so101
