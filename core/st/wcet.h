#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "st/bytecode.h"

namespace plcopen::core::st
{

// Work-unit charging is a VM watchdog contract.  TimingClass is deliberately
// separate: it identifies the target-specific calibration needed before an
// artifact can be discussed in wall-clock units.
enum class WcetBudgetRule : std::uint8_t
{
    unreviewed = 0,
    fixed_one,
    string_capacity,
    object_size,
    standard_function,
    output_count,
};

enum class WcetTimingClass : std::uint8_t
{
    fixed = 0,
    linear_bytes,
    standard_function,
    native_fb,
    count,
};

struct WcetOpcodeCost
{
    WcetBudgetRule budget_rule = WcetBudgetRule::unreviewed;
    WcetTimingClass timing_class = WcetTimingClass::fixed;
};

inline constexpr std::uint32_t kWcetReviewedBytecodeFormatVersion = 6;
inline constexpr std::size_t kWcetReviewedOpcodeCount = 87;
inline constexpr std::size_t kWcetTimingClassCount =
    static_cast<std::size_t>(WcetTimingClass::count);

static_assert(kBytecodeFormatVersion == kWcetReviewedBytecodeFormatVersion,
              "review the A2 opcode cost table for the new bytecode format");
static_assert(static_cast<std::size_t>(Op::debug_probe) + 1U ==
                  kWcetReviewedOpcodeCount,
              "review the A2 cost of every added or removed opcode");

constexpr WcetOpcodeCost wcet_opcode_cost(Op op) noexcept
{
    switch(op) {
    case Op::string_copy:
    case Op::string_copy_const:
    case Op::string_compare:
    case Op::string_index:
    case Op::string_length:
        return {WcetBudgetRule::string_capacity,
                WcetTimingClass::linear_bytes};
    case Op::fb_store_object:
    case Op::fb_load_object:
        return {WcetBudgetRule::object_size,
                WcetTimingClass::linear_bytes};
    case Op::standard_scalar:
    case Op::standard_string:
        return {WcetBudgetRule::standard_function,
                WcetTimingClass::standard_function};
    case Op::commit_outputs:
        return {WcetBudgetRule::output_count,
                WcetTimingClass::linear_bytes};
    case Op::copy_bytes:
        return {WcetBudgetRule::fixed_one,
                WcetTimingClass::linear_bytes};
    case Op::fb_call:
        return {WcetBudgetRule::fixed_one, WcetTimingClass::native_fb};
    default:
        return static_cast<std::size_t>(op) < kWcetReviewedOpcodeCount
                   ? WcetOpcodeCost{WcetBudgetRule::fixed_one,
                                    WcetTimingClass::fixed}
                   : WcetOpcodeCost{};
    }
}

constexpr const char *wcet_budget_rule_name(WcetBudgetRule rule) noexcept
{
    switch(rule) {
    case WcetBudgetRule::fixed_one: return "fixed_one";
    case WcetBudgetRule::string_capacity: return "string_capacity";
    case WcetBudgetRule::object_size: return "object_size";
    case WcetBudgetRule::standard_function: return "standard_function";
    case WcetBudgetRule::output_count: return "output_count";
    default: return "unreviewed";
    }
}

constexpr const char *wcet_timing_class_name(WcetTimingClass timing) noexcept
{
    switch(timing) {
    case WcetTimingClass::fixed: return "fixed";
    case WcetTimingClass::linear_bytes: return "linear_bytes";
    case WcetTimingClass::standard_function: return "standard_function";
    case WcetTimingClass::native_fb: return "native_fb";
    default: return "unreviewed";
    }
}

struct WcetReport
{
    std::uint32_t bytecode_format_version = 0;
    bool opcode_table_reviewed = false;
    bool bounded = false;
    bool wall_clock_bound_available = false;
    bool requires_platform_calibration = false;
    bool requires_native_fb_profile = false;
    std::uint64_t worst_case_work_units = 0;
    std::uint16_t max_call_depth = 0;
    std::uint16_t max_instance_depth = 0;
    std::uint32_t program_count = 0;
    std::uint32_t native_fb_instances = 0;
    std::array<std::uint32_t, kWcetTimingClassCount>
        artifact_opcode_counts{};
    std::uint32_t unreviewed_opcodes = 0;
};

namespace wcet_detail
{

inline void increment(std::uint32_t &value) noexcept
{
    if(value != std::numeric_limits<std::uint32_t>::max()) ++value;
}

inline void add_size(std::uint32_t &value, std::size_t added) noexcept
{
    const std::size_t room =
        std::numeric_limits<std::uint32_t>::max() - value;
    value += static_cast<std::uint32_t>(added > room ? room : added);
}

inline void add_opcode(WcetReport &report, Op op) noexcept
{
    const WcetOpcodeCost cost = wcet_opcode_cost(op);
    const std::size_t timing =
        static_cast<std::size_t>(cost.timing_class);
    if(cost.budget_rule == WcetBudgetRule::unreviewed ||
       timing >= report.artifact_opcode_counts.size()) {
        increment(report.unreviewed_opcodes);
        return;
    }
    increment(report.artifact_opcode_counts[timing]);
}

inline void add_region(WcetReport &report, const SfcRegionInfo &region) noexcept
{
    for(const std::uint32_t offset : region.instruction_offsets) {
        if(offset >= region.code.size()) {
            increment(report.unreviewed_opcodes);
        } else {
            add_opcode(report, static_cast<Op>(region.code[offset]));
        }
    }
}

inline void add_program_body(WcetReport &report,
                             const Program &program) noexcept
{
    report.opcode_table_reviewed = report.opcode_table_reviewed &&
        program.format_version == kWcetReviewedBytecodeFormatVersion;
    increment(report.program_count);
    add_size(report.native_fb_instances, program.fbs.size());
    for(const std::uint32_t offset : program.instruction_offsets) {
        if(offset >= program.code.size()) {
            increment(report.unreviewed_opcodes);
        } else {
            add_opcode(report, static_cast<Op>(program.code[offset]));
        }
    }
    for(const SfcNetworkInfo &network : program.sfc_networks) {
        for(const SfcTransitionInfo &transition : network.transitions)
            add_region(report, transition.condition);
        for(const SfcActionInfo &action : network.actions)
            add_region(report, action.region);
    }
}

inline void add_artifact_programs(WcetReport &report,
                                  const Program &program) noexcept
{
    if(program.programs.empty()) {
        add_program_body(report, program);
        return;
    }
    for(const Program &child : program.programs)
        add_artifact_programs(report, child);
}

} // namespace wcet_detail

// Load/diagnostic-domain report.  It walks existing vectors but performs no
// allocation and never changes the executable artifact.
inline WcetReport make_wcet_report(const Program &program) noexcept
{
    WcetReport report;
    report.bytecode_format_version = program.format_version;
    report.opcode_table_reviewed =
        program.format_version == kWcetReviewedBytecodeFormatVersion;
    report.bounded = program.worst_case_bounded;
    report.worst_case_work_units = report.bounded
        ? program.worst_case_instructions : 0U;
    report.max_call_depth = program.max_call_depth;
    report.max_instance_depth = program.max_instance_depth;
    wcet_detail::add_artifact_programs(report, program);

    const auto count = [&report](WcetTimingClass timing) noexcept {
        return report.artifact_opcode_counts[
            static_cast<std::size_t>(timing)];
    };
    report.requires_native_fb_profile =
        report.native_fb_instances != 0 ||
        count(WcetTimingClass::native_fb) != 0;
    report.requires_platform_calibration =
        count(WcetTimingClass::linear_bytes) != 0 ||
        count(WcetTimingClass::standard_function) != 0 ||
        report.requires_native_fb_profile;
    return report;
}

} // namespace plcopen::core::st
