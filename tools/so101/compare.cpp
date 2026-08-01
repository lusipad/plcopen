#include <cstdio>
#include <cstring>
#include <fstream>

#include "comparison.h"

namespace
{

using plcopen::tools::so101::ComparisonMode;
using plcopen::tools::so101::ComparisonScenario;

int usage(const char *program)
{
    std::fprintf(
        stderr,
        "usage: %s --mode naive|plcopen --scenario wave|dropout "
        "--output evidence.csv\n",
        program);
    return 64;
}

bool parse_mode(const char *value, ComparisonMode &mode)
{
    if(std::strcmp(value, "naive") == 0) {
        mode = ComparisonMode::naive;
        return true;
    }
    if(std::strcmp(value, "plcopen") == 0) {
        mode = ComparisonMode::plcopen;
        return true;
    }
    return false;
}

bool parse_scenario(const char *value, ComparisonScenario &scenario)
{
    if(std::strcmp(value, "wave") == 0) {
        scenario = ComparisonScenario::wave;
        return true;
    }
    if(std::strcmp(value, "dropout") == 0) {
        scenario = ComparisonScenario::dropout;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if(argc != 7 || std::strcmp(argv[1], "--mode") != 0 ||
       std::strcmp(argv[3], "--scenario") != 0 ||
       std::strcmp(argv[5], "--output") != 0) {
        return usage(argv[0]);
    }

    ComparisonMode mode{};
    ComparisonScenario scenario{};
    if(!parse_mode(argv[2], mode) ||
       !parse_scenario(argv[4], scenario) ||
       argv[6][0] == '\0') {
        return usage(argv[0]);
    }

    plcopen::tools::so101::ComparisonRun run{};
    if(!plcopen::tools::so101::run_comparison(mode, scenario, run)) {
        std::fprintf(stderr, "SO101_COMPARE ERROR scenario execution failed\n");
        return 1;
    }
    std::ofstream output(argv[6], std::ios::out | std::ios::trunc);
    if(!output ||
       !plcopen::tools::so101::write_comparison_csv(output, run)) {
        std::fprintf(stderr, "SO101_COMPARE ERROR output=%s\n", argv[6]);
        return 1;
    }

    const double cycle_hz =
        static_cast<double>(plcopen::tools::so101::ComparisonCycleHz);
    std::printf(
        "SO101_COMPARE PASS mode=%s scenario=%s evidence=command-only "
        "transport=none hardware=not-used samples=%zu frames=%zu dropouts=%u "
        "filter_faults=%u "
        "max_velocity_rad_s=%.9f max_acceleration_rad_s2=%.9f "
        "max_jerk_rad_s3=%.9f output=%s\n",
        plcopen::tools::so101::comparison_mode_name(mode),
        plcopen::tools::so101::comparison_scenario_name(scenario),
        run.samples.size(), run.accepted_frames, run.dropout_count,
        run.filter_faults,
        run.metrics.max_abs_velocity * cycle_hz,
        run.metrics.max_abs_acceleration * cycle_hz * cycle_hz,
        run.metrics.max_abs_jerk * cycle_hz * cycle_hz * cycle_hz,
        argv[6]);
    return 0;
}
