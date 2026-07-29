// A2 WCET software measurement contract.
//
// Work-unit bounds and observed wall-clock time are deliberately separate:
// this test locks the public opcode table and allocation-free report surface,
// not a host-specific timing threshold.

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "st/st.h"
#include "st/wcet.h"

namespace
{

using namespace plcopen::core;

int failures = 0;
bool g_freeze_allocations = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{

void check(bool condition, const char *name)
{
    if(!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

void opcode_cost_table_is_complete()
{
    for(std::size_t value = 0; value < st::kWcetReviewedOpcodeCount; ++value) {
        const st::WcetOpcodeCost cost =
            st::wcet_opcode_cost(static_cast<st::Op>(value));
        check(cost.budget_rule != st::WcetBudgetRule::unreviewed,
              "A2 every reviewed opcode has a budget rule");
    }

    const st::WcetOpcodeCost scalar =
        st::wcet_opcode_cost(st::Op::push_const);
    check(scalar.budget_rule == st::WcetBudgetRule::fixed_one &&
              scalar.timing_class == st::WcetTimingClass::fixed,
          "A2 scalar opcode is fixed cost");

    const st::WcetOpcodeCost copy =
        st::wcet_opcode_cost(st::Op::copy_bytes);
    check(copy.budget_rule == st::WcetBudgetRule::fixed_one &&
              copy.timing_class == st::WcetTimingClass::linear_bytes,
          "A2 byte copy keeps budget semantics but requires calibration");

    const st::WcetOpcodeCost object =
        st::wcet_opcode_cost(st::Op::fb_store_object);
    check(object.budget_rule == st::WcetBudgetRule::object_size &&
              object.timing_class == st::WcetTimingClass::linear_bytes,
          "A2 FB object copy is sized in the budget table");

    const st::WcetOpcodeCost standard =
        st::wcet_opcode_cost(st::Op::standard_scalar);
    check(standard.budget_rule == st::WcetBudgetRule::standard_function &&
              standard.timing_class ==
                  st::WcetTimingClass::standard_function,
          "A2 standard function keeps its approved dynamic cost");

    const st::WcetOpcodeCost native =
        st::wcet_opcode_cost(st::Op::fb_call);
    check(native.budget_rule == st::WcetBudgetRule::fixed_one &&
              native.timing_class == st::WcetTimingClass::native_fb,
          "A2 native FB timing is an explicit profile class");
}

void report_classifies_artifact_without_changing_budget()
{
    const st::CompileResult compiled = st::compile(
        "TYPE Pair : STRUCT A : DINT; B : DINT; END_STRUCT END_TYPE\n"
        "PROGRAM Main\nVAR Left : Pair; Right : Pair; Edge : R_TRIG; "
        "Q : BOOL; END_VAR\n"
        "Right := Left; Edge(CLK := TRUE); Q := Edge.Q;\nEND_PROGRAM\n");
    check(compiled.ok, "A2 classified artifact fixture compiles");
    if(!compiled.ok) return;

    st::WcetReport first;
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    first = st::make_wcet_report(compiled.program);
    g_freeze_allocations = false;
    check(g_frozen_allocations == 0,
          "A2 report generation performs no allocation");
    const st::WcetReport second = st::make_wcet_report(compiled.program);
    const std::size_t linear =
        static_cast<std::size_t>(st::WcetTimingClass::linear_bytes);
    const std::size_t native =
        static_cast<std::size_t>(st::WcetTimingClass::native_fb);

    check(first.opcode_table_reviewed && first.bounded &&
              first.worst_case_work_units ==
                  compiled.program.worst_case_instructions &&
              first.max_call_depth == compiled.program.max_call_depth &&
              first.max_instance_depth ==
                  compiled.program.max_instance_depth,
          "A2 report mirrors compile artifact bounds");
    check(first.program_count == 1 && first.native_fb_instances == 1 &&
              first.artifact_opcode_counts[linear] > 0 &&
              first.artifact_opcode_counts[native] == 1,
          "A2 report counts byte-copy and native-FB classes");
    check(first.requires_platform_calibration &&
              first.requires_native_fb_profile &&
              !first.wall_clock_bound_available &&
              first.unreviewed_opcodes == 0,
          "A2 report refuses an uncalibrated wall-clock claim");
    check(first.artifact_opcode_counts == second.artifact_opcode_counts &&
              first.worst_case_work_units == second.worst_case_work_units &&
              first.native_fb_instances == second.native_fb_instances,
          "A2 report is deterministic");
}

void report_keeps_unbounded_control_flow_explicit()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM Main\nVAR Run : BOOL := FALSE; END_VAR\n"
        "WHILE Run DO Run := FALSE; END_WHILE;\nEND_PROGRAM\n");
    check(compiled.ok, "A2 unbounded fixture compiles");
    if(!compiled.ok) return;

    const st::WcetReport report = st::make_wcet_report(compiled.program);
    check(!report.bounded && report.worst_case_work_units == 0 &&
              !report.wall_clock_bound_available,
          "A2 back edge has no static work-unit or wall-clock bound");
}

void report_rejects_unreviewed_and_malformed_artifacts()
{
    const st::WcetBudgetRule unknown_budget_rule =
        static_cast<st::WcetBudgetRule>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    const st::WcetTimingClass unknown_timing_class =
        static_cast<st::WcetTimingClass>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    const st::Op unknown_opcode =
        static_cast<st::Op>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    check(std::string_view(st::wcet_budget_rule_name(
              st::WcetBudgetRule::fixed_one)) == "fixed_one" &&
              std::string_view(st::wcet_budget_rule_name(
                  st::WcetBudgetRule::string_capacity)) ==
                  "string_capacity" &&
              std::string_view(st::wcet_budget_rule_name(
                  st::WcetBudgetRule::object_size)) == "object_size" &&
              std::string_view(st::wcet_budget_rule_name(
                  st::WcetBudgetRule::standard_function)) ==
                  "standard_function" &&
              std::string_view(st::wcet_budget_rule_name(
                  st::WcetBudgetRule::output_count)) == "output_count" &&
              std::string_view(st::wcet_budget_rule_name(
                  unknown_budget_rule)) == "unreviewed",
          "A2 budget rule names are total");
    check(std::string_view(st::wcet_timing_class_name(
              st::WcetTimingClass::fixed)) == "fixed" &&
              std::string_view(st::wcet_timing_class_name(
                  st::WcetTimingClass::linear_bytes)) == "linear_bytes" &&
              std::string_view(st::wcet_timing_class_name(
                  st::WcetTimingClass::standard_function)) ==
                  "standard_function" &&
              std::string_view(st::wcet_timing_class_name(
                  st::WcetTimingClass::native_fb)) == "native_fb" &&
              std::string_view(st::wcet_timing_class_name(
                  unknown_timing_class)) == "unreviewed",
          "A2 timing class names are total");
    check(st::wcet_opcode_cost(unknown_opcode).budget_rule ==
              st::WcetBudgetRule::unreviewed,
          "A2 unknown opcodes remain unreviewed");

    std::uint32_t count = std::numeric_limits<std::uint32_t>::max();
    st::wcet_detail::increment(count);
    check(count == std::numeric_limits<std::uint32_t>::max(),
          "A2 counters saturate at uint32 max");
    count = std::numeric_limits<std::uint32_t>::max() - 1U;
    st::wcet_detail::add_size(count, 10U);
    check(count == std::numeric_limits<std::uint32_t>::max(),
          "A2 size accumulation saturates");

    st::Program child;
    child.format_version = 0;
    child.code = {static_cast<std::uint8_t>(st::Op::push_const)};
    child.instruction_offsets = {0, 1};
    child.fbs.resize(1);

    st::SfcNetworkInfo network;
    st::SfcTransitionInfo transition;
    transition.condition.code = {
        static_cast<std::uint8_t>(st::Op::copy_bytes)};
    transition.condition.instruction_offsets = {0, 1};
    network.transitions.push_back(transition);
    st::SfcActionInfo action;
    action.region.code = {static_cast<std::uint8_t>(st::Op::fb_call)};
    action.region.instruction_offsets = {0};
    network.actions.push_back(action);
    child.sfc_networks.push_back(network);

    st::Program artifact;
    artifact.programs.push_back(child);
    const st::WcetReport report = st::make_wcet_report(artifact);
    check(!report.opcode_table_reviewed && report.program_count == 1 &&
              report.native_fb_instances == 1 &&
              report.unreviewed_opcodes == 2 &&
              report.requires_platform_calibration &&
              report.requires_native_fb_profile,
          "A2 malformed nested artifact is conservatively classified");

    const st::WcetReport empty = st::make_wcet_report(st::Program{});
    check(empty.opcode_table_reviewed && empty.program_count == 1 &&
              !empty.requires_platform_calibration &&
              !empty.requires_native_fb_profile &&
              empty.unreviewed_opcodes == 0,
          "A2 empty reviewed artifact needs no calibration");

    st::Program reviewed;
    st::Program unreviewed_child;
    unreviewed_child.format_version = 0;
    reviewed.programs.push_back(unreviewed_child);
    check(!st::make_wcet_report(reviewed).opcode_table_reviewed,
          "A2 an unreviewed child invalidates a reviewed artifact");

    st::Program timed;
    timed.code = {
        static_cast<std::uint8_t>(st::Op::copy_bytes),
        static_cast<std::uint8_t>(st::Op::fb_call)};
    timed.instruction_offsets = {0, 1};
    const st::WcetReport timed_report = st::make_wcet_report(timed);
    check(timed_report.requires_native_fb_profile &&
              timed_report.requires_platform_calibration,
          "A2 opcode timing classes request their required calibration");
}

void artifact_lookup_and_opcode_scan_are_exact()
{
    st::Program artifact;
    st::SymbolInfo symbol;
    symbol.qualified_name = "main.value";
    artifact.symbols.push_back(symbol);
    st::SymbolInfo found;
    check(artifact.find_symbol(nullptr, found) ==
                  rt::ErrorCode::invalid_argument &&
              artifact.find_symbol("MAIN.VALUE", found) == rt::ErrorCode::ok &&
              artifact.find_symbol("missing", found) ==
                  rt::ErrorCode::invalid_argument,
          "A2 artifact symbol lookup is null-safe and case-insensitive");

    artifact.code = {static_cast<std::uint8_t>(st::Op::push_const)};
    artifact.instruction_offsets = {1, 0};
    st::SfcNetworkInfo network;
    st::SfcTransitionInfo transition;
    transition.condition.code = {
        static_cast<std::uint8_t>(st::Op::copy_bytes)};
    transition.condition.instruction_offsets = {1, 0};
    network.transitions.push_back(transition);
    st::SfcActionInfo action;
    action.name = "Work";
    action.region.code = {static_cast<std::uint8_t>(st::Op::fb_call)};
    action.region.instruction_offsets = {0};
    network.actions.push_back(action);
    network.name = "Flow";
    artifact.sfc_networks.push_back(network);
    st::Program child;
    child.program_name = "child";
    child.code = {static_cast<std::uint8_t>(st::Op::debug_probe)};
    child.instruction_offsets = {0};
    artifact.programs.push_back(child);

    check(artifact.contains_opcode(st::Op::push_const) &&
              artifact.contains_opcode(st::Op::copy_bytes) &&
              artifact.contains_opcode(st::Op::fb_call) &&
              artifact.contains_opcode(st::Op::debug_probe) &&
              !artifact.contains_opcode(st::Op::string_index),
          "A2 opcode scan covers base, SFC and child artifacts");

    st::Program single;
    single.program_name = "main";
    single.pous.push_back(st::PouInfo{"Main", "main"});
    check(single.artifact_pou_name(0) == "Main" &&
              single.artifact_pou_name(1).empty() &&
              single.artifact_program(0) == &single,
          "A2 single-program artifact lookup is exact");
    single.program_name = "other";
    check(single.artifact_program(0) == nullptr,
          "A2 single-program lookup rejects a mismatched POU");

    artifact.pous.push_back(st::PouInfo{"Child", "child"});
    check(artifact.artifact_program(1) == nullptr &&
              artifact.artifact_program(0) == &artifact.programs[0] &&
              artifact.artifact_sfc_name(0, 0).empty() &&
              artifact.artifact_sfc_name(1, 0).empty(),
          "A2 nested artifact lookup validates POU and SFC bounds");
    artifact.programs[0].sfc_networks.push_back(network);
    check(artifact.artifact_sfc_name(0, 0) == "Flow" &&
              artifact.artifact_sfc_action_name(0, 0, 0) == "Work" &&
              artifact.artifact_sfc_action_name(0, 1, 0).empty() &&
              artifact.artifact_sfc_action_name(0, 0, 1).empty() &&
              artifact.artifact_sfc_action_name(1, 0, 0).empty(),
          "A2 nested SFC lookup validates every index");

    st::Program multi;
    multi.pous.push_back(st::PouInfo{"Target", "target"});
    st::Program mismatch;
    mismatch.program_name = "other";
    st::Program match;
    match.program_name = "target";
    multi.programs.push_back(mismatch);
    multi.programs.push_back(match);
    check(multi.artifact_program(0) == &multi.programs[1],
          "A2 nested POU lookup skips non-matching programs");

    st::SfcRegionInfo unbounded;
    unbounded.worst_case_bounded = false;
    std::string serialized;
    st::bytecode_detail::append_sfc_region(serialized, unbounded);
    check(!serialized.empty(),
          "A2 SFC serialization records an unbounded region");
}

void tasking_report_rejects_malformed_artifacts()
{
    st::Program artifact;
    st::TaskingReport report;
    check(artifact.tasking_report(nullptr, report) ==
                  rt::ErrorCode::invalid_argument &&
              artifact.tasking_report("missing", report) ==
                  rt::ErrorCode::invalid_argument,
          "A2 tasking report requires a known configuration");

    st::ConfigurationInfo configuration;
    configuration.lower = "cfg";
    st::ResourceInfo resource;
    st::TaskInfo task;
    task.lower = "task";
    task.instruction_budget = 0;
    resource.tasks.push_back(task);
    configuration.resources.push_back(resource);
    artifact.configurations.push_back(configuration);
    check(artifact.tasking_report("CFG", report) ==
              rt::ErrorCode::invalid_argument,
          "A2 tasking report rejects a non-positive budget");

    artifact.configurations[0].resources[0].tasks[0].instruction_budget = 1;
    artifact.configurations[0].resources[0].tasks[0].mappings = {1};
    check(artifact.tasking_report("cfg", report) ==
              rt::ErrorCode::invalid_argument,
          "A2 tasking report rejects an invalid mapping index");

    st::ProgramMappingInfo mapping;
    mapping.task = "other";
    mapping.program = "main";
    artifact.configurations[0].resources[0].mappings.push_back(mapping);
    artifact.configurations[0].resources[0].tasks[0].mappings = {0};
    check(artifact.tasking_report("cfg", report) ==
              rt::ErrorCode::invalid_argument,
          "A2 tasking report rejects a cross-task mapping");

    artifact.configurations[0].resources[0].mappings[0].task = "task";
    check(artifact.tasking_report("cfg", report) ==
              rt::ErrorCode::invalid_argument,
          "A2 tasking report rejects a missing mapped program");

    artifact.configurations[0].resources[0].tasks.clear();
    artifact.configurations[0].resources[0].mappings[0].program = "missing";
    check(artifact.tasking_report("cfg", report) ==
              rt::ErrorCode::invalid_argument,
          "A2 resource accounting rejects a missing program");
}

void diagnostic_names_cover_the_complete_code_space()
{
    std::size_t known = 0;
    for(int value = 0; value <= 600; ++value) {
        const st::DiagCode code =
            static_cast<st::DiagCode>(value); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        if(std::string_view(st::to_string(code)) != "unknown") {
            ++known;
        }
    }
    const st::DiagCode unknown_code =
        static_cast<st::DiagCode>(600); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    check(known == 81 &&
              std::string_view(st::to_string(unknown_code)) == "unknown",
          "A2 diagnostic names cover all declared codes and the fallback");
}

} // namespace

int main()
{
    opcode_cost_table_is_complete();
    report_classifies_artifact_without_changing_budget();
    report_keeps_unbounded_control_flow_explicit();
    report_rejects_unreviewed_and_malformed_artifacts();
    artifact_lookup_and_opcode_scan_are_exact();
    tasking_report_rejects_malformed_artifacts();
    diagnostic_names_cover_the_complete_code_space();
    if(failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st wcet tests passed\n");
    return 0;
}
