#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>

#include "st/st.h"

namespace
{

using namespace plcopen::core;

int failures = 0;

void check(bool condition, const char *name)
{
    if(condition) return;
    std::printf("FAIL %s\n", name);
    ++failures;
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics)
        if(diagnostic.code == code) return true;
    return false;
}

const st::Diagnostic *find_code(const st::CompileResult &result,
                                st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics)
        if(diagnostic.code == code) return &diagnostic;
    return nullptr;
}

std::size_t count_code(const st::CompileResult &result, st::DiagCode code)
{
    std::size_t count = 0;
    for(const st::Diagnostic &diagnostic : result.diagnostics)
        if(diagnostic.code == code) ++count;
    return count;
}

void dump(const st::CompileResult &result, const char *name)
{
    if(result.ok) return;
    std::printf("  %s diagnostics:\n", name);
    for(const st::Diagnostic &diagnostic : result.diagnostics)
        std::printf("    %d:%d %s\n", diagnostic.line, diagnostic.column,
                    diagnostic.message.c_str());
}

std::string source(const std::string &task,
                   const std::string &mapping = "PROGRAM P0 WITH Main : P;")
{
    return "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
           "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n" +
           task + "\n" + mapping +
           "\nEND_RESOURCE\nEND_CONFIGURATION\n";
}

const char *valid_task =
    "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
    "BUDGET := 100);";

void lexical_masking_is_authoritative()
{
    const std::string text =
        "PROGRAM P\nVAR S : STRING[64]; N : DINT; END_VAR\n"
        "(* CONFIGURATION Ghost RESOURCE Bad ON PLC *)\n"
        "S := 'RESET_TASK(Main) CONFIGURATION Hidden'; N := N + 1;\n"
        "END_PROGRAM\nCONFIGURATION Plant\nRESOURCE R0 ON PLC\n" +
        std::string(valid_task) +
        "\nPROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION";
    const st::CompileResult result = st::compile(text);
    dump(result, "lexical masking");
    check(result.ok && result.program.configurations.size() == 1 &&
              !has_code(result, st::DiagCode::unsupported_l5_task_control),
          "L5 frontend ignores comments and strings while discovering syntax");
}

void task_control_detection_requires_call_syntax_and_keeps_offset()
{
    const std::string harmless =
        "PROGRAM P\nVAR RESET_TASK : DINT; END_VAR\n"
        "RESET_TASK := RESET_TASK + 1;\nEND_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n" +
        std::string(valid_task) +
        "\nPROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION";
    const st::CompileResult harmless_result = st::compile(harmless);
    check(harmless_result.ok &&
              !has_code(harmless_result,
                        st::DiagCode::unsupported_l5_task_control),
          "L5 task-control names are allowed outside call syntax");

    const std::string called =
        "PROGRAM P\nVAR N : DINT; END_VAR\n"
        "  RESET_TASK(Main);\nEND_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n" +
        std::string(valid_task) +
        "\nPROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION";
    const st::CompileResult called_result = st::compile(called);
    const st::Diagnostic *diagnostic = find_code(
        called_result, st::DiagCode::unsupported_l5_task_control);
    check(!called_result.ok && diagnostic != nullptr &&
              diagnostic->line == 3 && diagnostic->column == 3,
          "L5 task-control call diagnostic preserves lexical source offset");
}

void strict_productions_reject_malformed_inputs()
{
    const std::string bad_tasks[] = {
        "TASK Main(INTERVAL := T#1ms, INTERVAL := T#2ms, PRIORITY := 0, BUDGET := 10);",
        "TASK Main(INTERVAL := T#1ms, UNKNOWN := 1, PRIORITY := 0, BUDGET := 10);",
        "TASK Main(INTERVAL := T#1ms junk, PRIORITY := 0, BUDGET := 10);",
        "TASK Main(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 10)",
    };
    for(const std::string &task : bad_tasks)
        check(!st::compile(source(task)).ok,
              "L5 frontend rejects duplicate unknown trailing or unterminated task syntax");

    const std::string bad_resource =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 PLC\nEND_RESOURCE\n"
        "END_CONFIGURATION";
    check(!st::compile(bad_resource).ok,
          "L5 frontend requires RESOURCE ON target");
}

void namespaces_are_case_insensitively_unique()
{
    const std::string duplicate_task =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n" +
        std::string(valid_task) +
        "\nTASK mAiN(INTERVAL := T#1ms, PRIORITY := 1, BUDGET := 10);\n"
        "PROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION";
    check(!st::compile(duplicate_task).ok,
          "L5 frontend rejects case-folded duplicate TASK names");

    const std::string duplicate_resource =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\nEND_RESOURCE\n"
        "RESOURCE r0 ON PLC\nEND_RESOURCE\nEND_CONFIGURATION";
    check(!st::compile(duplicate_resource).ok,
          "L5 frontend rejects case-folded duplicate RESOURCE names");

    const std::string duplicate_configuration =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nEND_CONFIGURATION\n"
        "CONFIGURATION pLaNt\nEND_CONFIGURATION";
    check(!st::compile(duplicate_configuration).ok,
          "L5 frontend rejects case-folded duplicate CONFIGURATION names");

    const st::CompileResult duplicate_mapping = st::compile(source(
        valid_task,
        "PROGRAM P0 WITH Main : P; PROGRAM p0 WITH Main : P;"));
    check(!duplicate_mapping.ok &&
              has_code(duplicate_mapping,
                       st::DiagCode::sema_program_duplicate_mapping),
          "L5 frontend rejects case-folded duplicate mapping instances");
}

void full_time_literals_quantize_exactly()
{
    st::CompileOptions options;
    options.base_tick_ns = 1000000;
    const st::CompileResult result = st::compile(source(
        "TASK Main(INTERVAL := TIME#1m0s, PHASE := T#1_500ms, "
        "PRIORITY := 0, BUDGET := 10);"), options);
    dump(result, "full time");
    check(result.ok && result.program.configurations.size() == 1 &&
              result.program.configurations[0].resources[0].tasks[0]
                      .interval_ticks == 60000 &&
              result.program.configurations[0].resources[0].tasks[0]
                      .phase_ticks == 1500,
          "L5 frontend reuses compound fractional underscored TIME literals");

    const st::CompileResult non_integral = st::compile(source(
        "TASK Main(INTERVAL := TIME#1.000_5ms, PHASE := T#0ms, "
        "PRIORITY := 0, BUDGET := 10);"), options);
    check(!non_integral.ok &&
              has_code(non_integral,
                       st::DiagCode::sema_task_interval_invalid),
          "L5 frontend rejects TIME values not divisible by base tick");
}

void capacities_and_diagnostics_are_bounded_before_push()
{
    st::CompileOptions options;
    options.max_resources = 0;
    options.max_diagnostics = 2;
    std::string text =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\n";
    for(int index = 0; index < 5; ++index)
        text += "RESOURCE R" + std::to_string(index) +
                " ON PLC\nEND_RESOURCE\n";
    text += "END_CONFIGURATION";
    const st::CompileResult result = st::compile(text, options);
    check(!result.ok && result.diagnostics.size() <= 2 &&
              has_code(result, st::DiagCode::capacity_diagnostics) &&
              result.program.configurations.size() == 1 &&
              result.program.configurations[0].resources.empty(),
          "L5 frontend gates pushes and bounds diagnostic storage");
}

void warnings_are_per_configuration()
{
    const std::string text =
        "PROGRAM P\nVAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION A\nRESOURCE R0 ON PLC\n" +
        std::string(valid_task) +
        "\nPROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION\n"
        "CONFIGURATION B\nRESOURCE R1 ON PLC\nEND_RESOURCE\n"
        "END_CONFIGURATION";
    const st::CompileResult result = st::compile(text);
    dump(result, "warnings");
    check(result.ok &&
              count_code(result, st::DiagCode::warning_program_unmapped) == 1,
          "L5 frontend emits unmapped warning independently per configuration");
}

void manifest_includes_resource_target_and_is_stable()
{
    const auto build = [](const char *target) {
        return st::compile(
            "PROGRAM P\nVAR N : DINT; END_VAR\n"
            "N := N + 1;\nEND_PROGRAM\n"
            "CONFIGURATION Plant\nRESOURCE R0 ON " +
            std::string(target) + "\n" + valid_task +
            "\nPROGRAM P0 WITH Main : P;\nEND_RESOURCE\n"
            "END_CONFIGURATION\n");
    };
    const st::CompileResult a = build("PLC_A");
    const st::CompileResult a_again = build("PLC_A");
    const st::CompileResult b = build("PLC_B");
    dump(a, "manifest PLC_A");
    dump(b, "manifest PLC_B");
    check(a.ok && a_again.ok && b.ok &&
              a.program.canonical_manifest() ==
                  a_again.program.canonical_manifest() &&
              a.program.canonical_manifest() != b.program.canonical_manifest(),
          "L5 manifest is stable and includes RESOURCE target identity");
}

std::string capacity_source(std::size_t configurations,
                            std::size_t tasks, std::size_t mappings)
{
    std::string text =
        "PROGRAM P\nVAR N : DINT; END_VAR\n"
        "N := N + 1;\nEND_PROGRAM\n";
    for(std::size_t configuration = 0; configuration < configurations;
        ++configuration) {
        text += "CONFIGURATION C" + std::to_string(configuration) +
                "\nRESOURCE R0 ON PLC\n";
        for(std::size_t task = 0; task < tasks; ++task) {
            text += "TASK T" + std::to_string(task) +
                    "(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n";
        }
        for(std::size_t mapping = 0; mapping < mappings; ++mapping) {
            text += "PROGRAM P" + std::to_string(mapping) +
                    " WITH T0 : P;\n";
        }
        text += "END_RESOURCE\nEND_CONFIGURATION\n";
    }
    return text;
}

void configuration_task_and_mapping_capacities_are_exact()
{
    st::CompileOptions options;
    options.max_configurations = 2;
    options.max_tasks_per_resource = 2;
    options.max_program_mappings_per_task = 2;
    const st::CompileResult configurations_at =
        st::compile(capacity_source(2, 1, 1), options);
    const st::CompileResult configurations_over =
        st::compile(capacity_source(3, 1, 1), options);
    dump(configurations_at, "configuration capacity N");
    check(configurations_at.ok && !configurations_over.ok,
          "L5 CONFIGURATION capacity accepts N and rejects N+1");
    const st::CompileResult tasks_at =
        st::compile(capacity_source(1, 2, 1), options);
    const st::CompileResult tasks_over =
        st::compile(capacity_source(1, 3, 1), options);
    dump(tasks_at, "task capacity N");
    check(tasks_at.ok && !tasks_over.ok,
          "L5 TASK capacity accepts N and rejects N+1");
    const st::CompileResult mappings_at =
        st::compile(capacity_source(1, 1, 2), options);
    const st::CompileResult mappings_over =
        st::compile(capacity_source(1, 1, 3), options);
    dump(mappings_at, "mapping capacity N");
    check(mappings_at.ok && !mappings_over.ok,
          "L5 PROGRAM mapping capacity accepts N and rejects N+1");
}

void real_unbounded_loop_is_capped_by_task_budget()
{
    const std::string text =
        "PROGRAM P\nVAR N : DINT; END_VAR\n"
        "WHILE N >= 0 DO N := N + 1; END_WHILE;\nEND_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK Main(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 23);\n"
        "PROGRAM P0 WITH Main : P;\nEND_RESOURCE\nEND_CONFIGURATION";
    const st::CompileResult result = st::compile(text);
    dump(result, "real WHILE");
    st::TaskingReport report{};
    check(result.ok && !result.program.programs[0].worst_case_bounded &&
              result.program.tasking_report("plant", report) ==
                  rt::ErrorCode::ok &&
              report.release_worst_case_instructions == 23,
          "L5 real WHILE unbounded WCET is bounded by task budget");
}

void report_uses_budget_and_exact_shared_layout()
{
    const st::CompileResult compiled = st::compile(source(
        "TASK Main(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 17);"));
    dump(compiled, "report");
    if(!compiled.ok) {
        const std::string original = source(
            "TASK Main(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 17);");
        const std::string masked =
            st::tasking_detail::mask_configuration_blocks(original);
        std::printf("  has_program=%d has_configuration=%d\nMASKED:\n%s\n",
                    st::tasking_detail::has_program_declaration(original),
                    st::tasking_detail::source_contains_token(
                        original, "configuration"),
                    masked.c_str());
    }
    check(compiled.ok, "L5 frontend report fixture compiles");
    if(!compiled.ok) return;
    st::TaskingReport report{};
    const rt::ErrorCode code = compiled.program.tasking_report("plant", report);
    const st::Program &program = compiled.program.programs[0];
    const std::uint64_t image_bytes =
        static_cast<std::uint64_t>(program.process_image.input_bytes) +
        program.process_image.output_bytes + program.process_image.memory_bytes;
    const std::uint64_t expected =
        st::tasking_detail::align_runtime(sizeof(st::ResourceRuntimeStorage)) +
        st::tasking_detail::align_runtime(sizeof(st::TaskRuntimeStorage)) +
        st::tasking_detail::align_runtime(program.required_bytes()) +
        st::tasking_detail::align_runtime(image_bytes);
    check(code == rt::ErrorCode::ok &&
              report.release_worst_case_instructions <= 17 &&
              report.release_worst_case_instructions > 0 &&
              report.required_runtime_bytes == expected,
          "L5 report caps WCET by budget and uses shared exact layout");
    check(std::is_trivially_copyable_v<st::TaskStatus>,
          "L5 TaskStatus has no dynamically-owned diagnostic strings");

    st::Program broken = compiled.program;
    broken.configurations[0].resources[0].tasks[0].mappings[0] = 99;
    check(broken.tasking_report("plant", report) ==
              rt::ErrorCode::invalid_argument,
          "L5 report rejects corrupt mapping indices instead of under-reporting");

    st::Program saturated = compiled.program;
    saturated.programs[0].worst_case_instructions =
        std::numeric_limits<std::uint64_t>::max();
    saturated.configurations[0].resources[0].tasks[0].instruction_budget = 7;
    check(saturated.tasking_report("plant", report) == rt::ErrorCode::ok &&
              report.release_worst_case_instructions == 7,
          "L5 report uses checked saturated aggregation before budget cap");
}

} // namespace

int main()
{
    lexical_masking_is_authoritative();
    task_control_detection_requires_call_syntax_and_keeps_offset();
    strict_productions_reject_malformed_inputs();
    namespaces_are_case_insensitively_unique();
    full_time_literals_quantize_exactly();
    capacities_and_diagnostics_are_bounded_before_push();
    warnings_are_per_configuration();
    manifest_includes_resource_target_and_is_stable();
    configuration_task_and_mapping_capacities_are_exact();
    real_unbounded_loop_is_capped_by_task_budget();
    report_uses_budget_and_exact_shared_layout();
    if(failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l5 frontend tests passed\n");
    return 0;
}
