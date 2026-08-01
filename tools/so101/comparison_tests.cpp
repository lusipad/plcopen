#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "comparison.h"

namespace
{

using plcopen::tools::so101::ComparisonMode;
using plcopen::tools::so101::ComparisonRun;
using plcopen::tools::so101::ComparisonScenario;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double left, double right, double tolerance = 1e-12)
{
    return std::fabs(left - right) <= tolerance;
}

int check_fair_wave_and_envelope()
{
    ComparisonRun naive{};
    ComparisonRun filtered{};
    if(!plcopen::tools::so101::run_comparison(
           ComparisonMode::naive, ComparisonScenario::wave, naive) ||
       !plcopen::tools::so101::run_comparison(
           ComparisonMode::plcopen, ComparisonScenario::wave, filtered) ||
       naive.samples.size() != filtered.samples.size() ||
       naive.accepted_frames != filtered.accepted_frames) {
        return fail("wave setup");
    }
    for(std::size_t tick = 0; tick < naive.samples.size(); ++tick) {
        if(naive.samples[tick].target_valid !=
           filtered.samples[tick].target_valid) {
            return fail("shared target validity");
        }
        for(std::size_t joint = 0;
            joint < plcopen::tools::so101::ComparisonJointCount; ++joint) {
            if(!near(naive.samples[tick].target[joint],
                     filtered.samples[tick].target[joint])) {
                return fail("shared target values");
            }
        }
    }
    if(filtered.metrics.max_abs_velocity >
           plcopen::tools::so101::ComparisonMaxVelocityPerCycle * 1.000001 ||
       filtered.metrics.max_abs_acceleration >
           plcopen::tools::so101::ComparisonMaxAccelerationPerCycle * 1.000001 ||
       filtered.metrics.max_abs_jerk >
           plcopen::tools::so101::ComparisonMaxJerkPerCycle * 1.000001) {
        return fail("plcopen envelope");
    }
    if(filtered.filter_faults != 0) {
        std::printf("  wave filter_faults=%u\n", filtered.filter_faults);
        return fail("plcopen filter faults");
    }
    if(!(naive.metrics.max_abs_jerk >
         filtered.metrics.max_abs_jerk * 10.0)) {
        return fail("wave exposes naive discontinuity");
    }
    return 0;
}

int check_dropout_stop_and_identity()
{
    ComparisonRun filtered{};
    if(!plcopen::tools::so101::run_comparison(
           ComparisonMode::plcopen, ComparisonScenario::dropout, filtered) ||
       filtered.dropout_count != 1 || filtered.accepted_frames == 0 ||
       filtered.filter_faults != 0) {
        std::printf("  dropout filter_faults=%u\n", filtered.filter_faults);
        return fail("dropout setup");
    }
    const auto &before_recovery =
        filtered.samples[plcopen::tools::so101::ComparisonDropoutEnd - 1];
    for(std::size_t joint = 0;
        joint < plcopen::tools::so101::ComparisonJointCount; ++joint) {
        if(std::fabs(before_recovery.command_velocity[joint]) > 1e-9 ||
           std::fabs(before_recovery.command_acceleration[joint]) > 1e-9) {
            return fail("dropout controlled stop");
        }
    }
    std::uint64_t previous_sequence = 0;
    for(const auto &sample : filtered.samples) {
        if(sample.frame_sequence < previous_sequence) {
            return fail("frame sequence monotonic");
        }
        previous_sequence = sample.frame_sequence;
    }
    return 0;
}

int check_csv_contract()
{
    ComparisonRun run{};
    if(!plcopen::tools::so101::run_comparison(
           ComparisonMode::plcopen, ComparisonScenario::dropout, run)) {
        return fail("csv setup");
    }
    std::ostringstream output;
    if(!plcopen::tools::so101::write_comparison_csv(output, run)) {
        return fail("csv write");
    }
    const std::string csv = output.str();
    if(csv.rfind("tick,time_s,mode,scenario,target_valid,all_members_stopped,"
                 "frame_sequence,dropout_count,filter_faults", 0) != 0 ||
       csv.find("actual") != std::string::npos ||
       csv.find(",plcopen,dropout,") == std::string::npos) {
        return fail("csv schema");
    }
    const std::size_t lines =
        static_cast<std::size_t>(std::count(csv.begin(), csv.end(), '\n'));
    if(lines != run.samples.size() + 1) {
        return fail("csv row count");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_fair_wave_and_envelope() != 0 ||
       check_dropout_stop_and_identity() != 0 ||
       check_csv_contract() != 0) {
        return 1;
    }
    std::printf("PASS SO-ARM101 comparison tests\n");
    return 0;
}
