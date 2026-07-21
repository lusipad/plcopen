// A2 WCET software measurement contract.
//
// Work-unit bounds and observed wall-clock time are deliberately separate:
// this test locks the public opcode table and allocation-free report surface,
// not a host-specific timing threshold.

#include <cstdio>
#include <cstdlib>

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

} // namespace

int main()
{
    opcode_cost_table_is_complete();
    report_classifies_artifact_without_changing_budget();
    report_keeps_unbounded_control_flow_explicit();
    if(failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st wcet tests passed\n");
    return 0;
}
