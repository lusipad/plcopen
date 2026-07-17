// L6 textual-SFC acceptance (approved st-l6-semantics, 2026-07-17).
//
// This is intentionally a RED contract.  It fixes the source grammar, the
// old-active-set scan rule, all nine action qualifiers, static graph reports,
// caller-owned trace storage and the minimum L5 task-fault integration.  It
// does not introduce graphical SFC, IL, macro steps or online change.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "st/st.h"

namespace
{

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

void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{

using namespace plcopen::core;

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 1000000;

int failures = 0;

void check(bool condition, const char *name)
{
    if(!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) return true;
    }
    return false;
}

std::string program(std::string_view variables, std::string_view sfc)
{
    std::string source = "PROGRAM Main\nVAR\n";
    source += variables;
    source += "\nEND_VAR\nSFC Flow\n";
    source += sfc;
    source += "\nEND_SFC\nEND_PROGRAM\n";
    return source;
}

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char storage[1048576] = {};

    bool build(const std::string &source,
               const st::CompileOptions &options = st::CompileOptions{})
    {
        compiled = st::compile(source, options);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        return instance.load(compiled.program, "main", storage,
                             sizeof(storage), kPeriodNs) == rt::ErrorCode::ok;
    }

    bool scan(std::int64_t budget = kBudget)
    {
        return instance.scan(budget) == st::ScanError::ok;
    }

    std::int64_t value(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0
                   ? -424242
                   : instance.value_i64(static_cast<std::size_t>(index));
    }

    bool active(const char *step) const
    {
        bool result = false;
        return instance.sfc_step_active("flow", step, result) ==
                   rt::ErrorCode::ok &&
               result;
    }
};

void sequential_network_uses_old_active_snapshot()
{
    Rig rig;
    check(rig.build(program(
              "Count : DINT;",
              "INITIAL_STEP A:\nEND_STEP\n"
              "STEP B:\nEND_STEP\n"
              "STEP C:\nTERMINAL;\nEND_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
              "TRANSITION FROM B TO C := TRUE; END_TRANSITION")),
          "L6-A01 sequential network builds");
    check(rig.active("a"), "L6-D01 initial step is active after load");
    check(rig.scan() && rig.active("b") && !rig.active("c"),
          "L6-D03 first scan fires only A-to-B");
    check(rig.scan() && rig.active("c"),
          "L6-D03 newly active B fires on the following scan");
}

void selection_uses_source_declaration_priority()
{
    Rig rig;
    check(rig.build(program(
              "Chosen : DINT;",
              "INITIAL_STEP Pick:\nEND_STEP\n"
              "STEP First: FirstAction(N); TERMINAL; END_STEP\n"
              "STEP Second: SecondAction(N); TERMINAL; END_STEP\n"
              "TRANSITION FROM Pick TO First := TRUE; END_TRANSITION\n"
              "TRANSITION FROM Pick TO Second := TRUE; END_TRANSITION\n"
              "ACTION FirstAction: Chosen := 1; END_ACTION\n"
              "ACTION SecondAction: Chosen := 2; END_ACTION")),
          "L6-A01 selection network builds");
    check(rig.scan() && rig.active("first") && !rig.active("second") &&
              rig.value("Chosen") == 1,
          "L6-D04 first true transition wins by declaration order");
}

void simultaneous_divergence_activates_every_successor()
{
    Rig rig;
    check(rig.build(program(
              "Count : DINT;",
              "INITIAL_STEP Start:\nEND_STEP\n"
              "STEP Left: CountLeft(N); TERMINAL; END_STEP\n"
              "STEP Right: CountRight(N); TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
              "END_TRANSITION\n"
              "ACTION CountLeft: Count := Count + 1; END_ACTION\n"
              "ACTION CountRight: Count := Count + 10; END_ACTION")),
          "L6-A01 simultaneous divergence builds");
    check(rig.scan() && rig.active("left") && rig.active("right") &&
              rig.value("Count") == 11,
          "L6-D05 divergence activates all successors atomically");
}

void simultaneous_convergence_waits_for_every_predecessor()
{
    Rig rig;
    check(rig.build(program(
              "Ticks : DINT;",
              "INITIAL_STEP Start:\nEND_STEP\n"
              "STEP Left: END_STEP\n"
              "STEP Right: Tick(N); END_STEP\n"
              "STEP LeftDone: END_STEP\n"
              "STEP RightDone: END_STEP\n"
              "STEP Done: TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
              "END_TRANSITION\n"
              "TRANSITION FROM Left TO LeftDone := TRUE; END_TRANSITION\n"
              "TRANSITION FROM Right TO RightDone := Ticks >= 2; "
              "END_TRANSITION\n"
              "TRANSITION FROM (LeftDone, RightDone) TO Done SIMULTANEOUS "
              ":= TRUE; END_TRANSITION\n"
              "ACTION Tick: Ticks := Ticks + 1; END_ACTION")),
          "L6-A01 simultaneous convergence builds");
    check(rig.scan() && rig.scan() && rig.active("leftdone") &&
              rig.active("right"),
          "L6-D05 convergence remains disabled with one predecessor missing");
    check(rig.scan() && rig.active("rightdone") && !rig.active("done"),
          "L6-D03 convergence does not consume a newly active predecessor");
    check(rig.scan() && rig.active("done"),
          "L6-D05 convergence fires after all predecessors were active");
}

void conflicting_step_update_is_rejected()
{
    const st::CompileResult result = st::compile(program(
        "X : BOOL;",
        "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
        "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
        "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
        "TRANSITION FROM A TO A := TRUE; END_TRANSITION"));
    check(!result.ok &&
              has_code(result, st::DiagCode::sema_unsafe_sfc_network),
          "L6-D06 conflicting activation and exit has stable diagnostic");
}

void n_qualifier_runs_only_while_step_is_active()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(N); END_STEP\n"
              "STEP Done: TERMINAL; END_STEP\n"
              "TRANSITION FROM A TO Done := Runs >= 2; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 N qualifier builds");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 N executes in the first active cycle");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 N executes for N active cycles");
    check(rig.scan() && rig.scan() && rig.value("Runs") == 2,
          "L6-A02 N stops on the exit scan and stays stopped");
}

void n_qualifier_resumes_after_step_reactivation()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(N); END_STEP\n"
              "STEP B: END_STEP\n"
              "TRANSITION FROM A TO B := Runs = 1; END_TRANSITION\n"
              "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 N reactivation program builds");
    check(rig.scan() && rig.scan() && rig.scan() && rig.value("Runs") == 2,
          "L6-A02 N resumes when its step becomes active again");
}

void s_qualifier_latches_until_r_and_can_reactivate()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Set: Work(S); END_STEP\n"
              "STEP Hold: END_STEP\n"
              "STEP Reset: Work(R); END_STEP\n"
              "STEP Again: Work(S); TERMINAL; END_STEP\n"
              "TRANSITION FROM Set TO Hold := TRUE; END_TRANSITION\n"
              "TRANSITION FROM Hold TO Reset := Runs >= 2; END_TRANSITION\n"
              "TRANSITION FROM Reset TO Again := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 S/R qualifier network builds");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 S remains active after its source step exits");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 S executes across later scans");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 R clears a stored action before execution");
    check(rig.scan() && rig.value("Runs") == 3,
          "L6-A02 S can be stored again after R");
}

void r_wins_same_scan_qualifier_conflicts()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Start: END_STEP\n"
              "STEP Setter: Work(S); TERMINAL; END_STEP\n"
              "STEP Resetter: Work(R); TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO (Setter, Resetter) SIMULTANEOUS "
              ":= TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 R-conflict network builds");
    check(rig.scan() && rig.value("Runs") == 0,
          "L6-A02 R wins over stored set in the same scan");
}

void l_qualifier_has_zero_one_and_n_cycle_boundaries()
{
    const char *durations[] = {"T#0ms", "T#1ms", "T#3ms"};
    const std::int64_t expected[] = {0, 1, 3};
    for(std::size_t index = 0; index < 3; ++index) {
        Rig rig;
        const std::string body =
            "INITIAL_STEP A: Work(L, " + std::string(durations[index]) +
            "); TERMINAL; END_STEP\nACTION Work: Runs := Runs + 1; END_ACTION";
        check(rig.build(program("Runs : DINT;", body)),
              "L6-A02 L boundary program builds");
        for(int scan = 0; scan < 5; ++scan) check(rig.scan(), "L6 L scan");
        check(rig.value("Runs") == expected[index],
              "L6-A02 L executes exactly 0/1/N task periods");
    }
}

void l_qualifier_stops_early_and_restarts_on_reactivation()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(L, T#5ms); END_STEP\n"
              "STEP B: END_STEP\n"
              "TRANSITION FROM A TO B := Runs >= 1; END_TRANSITION\n"
              "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 L early-exit program builds");
    check(rig.scan() && rig.scan() && rig.value("Runs") == 1,
          "L6-A02 L stops immediately when its step exits");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 L timer restarts after step reactivation");
}

void d_qualifier_has_zero_one_and_n_cycle_boundaries()
{
    const char *durations[] = {"T#0ms", "T#1ms", "T#3ms"};
    const std::int64_t expected[] = {5, 5, 3};
    for(std::size_t index = 0; index < 3; ++index) {
        Rig rig;
        const std::string body =
            "INITIAL_STEP A: Work(D, " + std::string(durations[index]) +
            "); TERMINAL; END_STEP\nACTION Work: Runs := Runs + 1; END_ACTION";
        check(rig.build(program("Runs : DINT;", body)),
              "L6-A02 D boundary program builds");
        for(int scan = 0; scan < 5; ++scan) check(rig.scan(), "L6 D scan");
        check(rig.value("Runs") == expected[index],
              "L6-A02 D begins after exactly 0/1/N task periods");
    }
}

void d_qualifier_cancels_delay_on_exit_and_reactivation()
{
    Rig rig;
    check(rig.build(program(
              "Clock : DINT; Runs : DINT;",
              "INITIAL_STEP A: ClockAction(N); Work(D, T#3ms); END_STEP\n"
              "STEP B: END_STEP\n"
              "TRANSITION FROM A TO B := Clock >= 1; END_TRANSITION\n"
              "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
              "ACTION ClockAction: Clock := Clock + 1; END_ACTION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 D cancellation program builds");
    for(int scan = 0; scan < 6; ++scan) check(rig.scan(), "L6 D cancel scan");
    check(rig.value("Runs") == 0,
          "L6-A02 D early exit clears delay before every reactivation");
}

void p_qualifier_pulses_on_initial_and_later_activation()
{
    Rig rig;
    check(rig.build(program(
              "Pulses : DINT;",
              "INITIAL_STEP A: Pulse(P); END_STEP\n"
              "STEP B: END_STEP\n"
              "TRANSITION FROM A TO B := Pulses >= 1; END_TRANSITION\n"
              "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
              "ACTION Pulse: Pulses := Pulses + 1; END_ACTION")),
          "L6-A02 P qualifier builds");
    check(rig.scan() && rig.value("Pulses") == 1,
          "L6-A02 P executes once for initial activation");
    check(rig.scan() && rig.value("Pulses") == 1,
          "L6-A02 P does not repeat while inactive");
    check(rig.scan() && rig.value("Pulses") == 2,
          "L6-A02 P executes once after reactivation");
    check(rig.scan() && rig.value("Pulses") == 2,
          "L6-A02 P does not repeat while continuously active");
}

void sd_qualifier_survives_early_exit_and_latches_at_deadline()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(SD, T#2ms); END_STEP\n"
              "STEP B: TERMINAL; END_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 SD early-exit program builds");
    check(rig.scan() && rig.value("Runs") == 0,
          "L6-A02 SD remains pending after source step exits");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 SD latches after its N-period deadline");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 SD stays stored after deadline");
}

void sd_zero_one_boundaries_and_r_cancellation()
{
    Rig zero;
    check(zero.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(SD, T#0ms); TERMINAL; END_STEP\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")) &&
              zero.scan() && zero.value("Runs") == 1,
          "L6-A02 SD TIME zero latches in the current qualifier phase");

    Rig cancel;
    check(cancel.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Start: Work(SD, T#1ms); END_STEP\n"
              "STEP Reset: Work(R); TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO Reset := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")) &&
              cancel.scan() && cancel.scan() &&
              cancel.value("Runs") == 0,
          "L6-A02 R cancels an SD request at its deadline");
}

void sd_can_be_requested_again_after_r()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Request: Work(SD, T#1ms); END_STEP\n"
              "STEP Reset: Work(R); END_STEP\n"
              "STEP Again: Work(SD, T#1ms); TERMINAL; END_STEP\n"
              "TRANSITION FROM Request TO Reset := TRUE; END_TRANSITION\n"
              "TRANSITION FROM Reset TO Again := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 SD reactivation program builds");
    check(rig.scan() && rig.scan() && rig.scan() && rig.value("Runs") == 1,
          "L6-A02 SD accepts a fresh delayed request after R");
}

void ds_requires_continuous_activation_then_stays_stored()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(DS, T#2ms); TERMINAL; END_STEP\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 DS program builds");
    check(rig.scan() && rig.value("Runs") == 0,
          "L6-A02 DS is inactive before its deadline");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 DS latches at its N-period deadline");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 DS stays stored after deadline");
}

void ds_early_exit_cancels_and_zero_reactivates()
{
    Rig cancel;
    check(cancel.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(DS, T#3ms); END_STEP\n"
              "STEP B: TERMINAL; END_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 DS early-exit program builds");
    for(int scan = 0; scan < 4; ++scan) check(cancel.scan(), "L6 DS cancel scan");
    check(cancel.value("Runs") == 0,
          "L6-A02 DS cancels when the step exits before deadline");

    Rig zero;
    check(zero.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(DS, T#0ms); TERMINAL; END_STEP\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")) &&
              zero.scan() && zero.value("Runs") == 1,
          "L6-A02 DS TIME zero stores on activation");
}

void ds_r_clears_latch_and_allows_reactivation()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Latch: Work(DS, T#1ms); END_STEP\n"
              "STEP Reset: Work(R); END_STEP\n"
              "STEP Again: Work(DS, T#1ms); TERMINAL; END_STEP\n"
              "TRANSITION FROM Latch TO Reset := Runs >= 1; END_TRANSITION\n"
              "TRANSITION FROM Reset TO Again := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 DS R/reactivation program builds");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 DS one-period delay latches");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-A02 R clears a DS stored latch");
    check(rig.scan() && rig.value("Runs") == 2,
          "L6-A02 DS latches again after reactivation");
}

void sl_has_zero_one_n_expiry_and_r_priority()
{
    const char *durations[] = {"T#0ms", "T#1ms", "T#3ms"};
    const std::int64_t expected[] = {0, 0, 2};
    for(std::size_t index = 0; index < 3; ++index) {
        Rig rig;
        const std::string body =
            "INITIAL_STEP A: Work(SL, " + std::string(durations[index]) +
            "); TERMINAL; END_STEP\nACTION Work: Runs := Runs + 1; END_ACTION";
        check(rig.build(program("Runs : DINT;", body)),
              "L6-A02 SL boundary program builds");
        for(int scan = 0; scan < 4; ++scan) check(rig.scan(), "L6 SL scan");
        check(rig.value("Runs") == expected[index],
              "L6-A02 SL expires after exactly 0/1/N periods");
    }

    Rig reset;
    check(reset.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Start: END_STEP\n"
              "STEP Latch: Work(SL, T#5ms); TERMINAL; END_STEP\n"
              "STEP Reset: Work(R); TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO (Latch, Reset) SIMULTANEOUS := TRUE; "
              "END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")) &&
              reset.scan() && reset.value("Runs") == 0,
          "L6-A02 R wins over SL activation in the same scan");
}

void sl_reactivation_starts_a_fresh_lifetime()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(SL, T#2ms); END_STEP\n"
              "STEP B: END_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
              "TRANSITION FROM B TO A := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A02 SL reactivation program builds");
    check(rig.scan() && rig.scan() && rig.scan() && rig.value("Runs") == 2,
          "L6-A02 SL starts a fresh bounded latch on reactivation");
}

void action_bodies_follow_declaration_order()
{
    Rig rig;
    check(rig.build(program(
              "Order : DINT;",
              "INITIAL_STEP A: Second(N); First(N); TERMINAL; END_STEP\n"
              "ACTION First: Order := Order * 10 + 1; END_ACTION\n"
              "ACTION Second: Order := Order * 10 + 2; END_ACTION")),
          "L6-D07 action-order program builds");
    check(rig.scan() && rig.value("Order") == 12,
          "L6-D07 action bodies execute by declaration order");
}

void shared_action_executes_once_per_scan()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP Start: END_STEP\n"
              "STEP Left: Work(N); TERMINAL; END_STEP\n"
              "STEP Right: Work(N); TERMINAL; END_STEP\n"
              "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
              "END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-D07 shared-action program builds");
    check(rig.scan() && rig.value("Runs") == 1,
          "L6-D07 one action referenced by two steps executes once");
}

void unsafe_networks_have_stable_recovering_diagnostics()
{
    struct BadNetwork
    {
        const char *name;
        const char *body;
        st::DiagCode code;
    };
    const BadNetwork cases[] = {
        {"missing initial",
         "STEP A: END_STEP",
         st::DiagCode::sema_unsafe_sfc_network},
        {"multiple initial",
         "INITIAL_STEP A: END_STEP\nINITIAL_STEP B: END_STEP",
         st::DiagCode::sema_unsafe_sfc_network},
        {"unreachable",
         "INITIAL_STEP A: END_STEP\nSTEP Lost: END_STEP",
         st::DiagCode::sema_unsafe_sfc_network},
        {"unterminated branch",
         "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
         "TRANSITION FROM A TO B := TRUE; END_TRANSITION",
         st::DiagCode::sema_unsafe_sfc_network},
        {"unpaired simultaneous",
         "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\nSTEP C: END_STEP\n"
         "TRANSITION FROM A TO (B, C) SIMULTANEOUS := TRUE; END_TRANSITION",
         st::DiagCode::sema_unsafe_sfc_network},
        {"assignment transition",
         "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
         "TRANSITION FROM A TO B := (X := TRUE); END_TRANSITION",
         st::DiagCode::sema_sfc_transition_side_effect},
        {"fb transition",
         "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
         "TRANSITION FROM A TO B := Edge(CLK := X).Q; END_TRANSITION",
         st::DiagCode::sema_sfc_transition_side_effect},
    };
    for(const BadNetwork &test : cases) {
        std::string variables = "X : BOOL; Edge : R_TRIG;";
        const st::CompileResult result =
            st::compile(program(variables, test.body) + " @");
        check(!result.ok && has_code(result, test.code), test.name);
        check(has_code(result, st::DiagCode::lex_invalid_character),
              "L6-A04 parser recovers and reports a later lexical error");
    }
}

void negative_time_has_stable_range_diagnostic()
{
    const st::CompileResult result = st::compile(program(
        "Runs : DINT;",
        "INITIAL_STEP A: Work(D, T#-1ms); TERMINAL; END_STEP\n"
        "ACTION Work: Runs := Runs + 1; END_ACTION"));
    check(!result.ok && has_code(result, st::DiagCode::sema_range_violation),
          "L6-A04 negative qualifier TIME has sema_range_violation");
}

void task_fault_names_the_sfc_action_and_isolates_other_task()
{
    const std::string source =
        "PROGRAM Bad\nVAR Z : DINT; X : DINT; END_VAR\n"
        "SFC Flow\nINITIAL_STEP A: Fail(N); TERMINAL; END_STEP\n"
        "ACTION Fail: X := 1 / Z; END_ACTION\nEND_SFC\nEND_PROGRAM\n"
        "PROGRAM Good VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK BadTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 1000);\n"
        "TASK GoodTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 1000);\n"
        "PROGRAM PB WITH BadTask : Bad;\n"
        "PROGRAM PG WITH GoodTask : Good;\n"
        "END_RESOURCE\nEND_CONFIGURATION\n";
    const st::CompileResult compiled = st::compile(source);
    check(compiled.ok, "L6-A05 SFC task-fault configuration compiles");
    if(!compiled.ok) return;
    alignas(8) unsigned char storage[1048576]{};
    st::ConfigurationRuntime runtime;
    check(runtime.load(compiled.program, "plant", storage, sizeof(storage),
                       kPeriodNs) == rt::ErrorCode::ok &&
              runtime.boundary(0, nullptr, 0) == rt::ErrorCode::ok &&
              runtime.run(kBudget) == rt::ErrorCode::ok,
          "L6-A05 SFC fault returns control to scheduler");
    st::TaskStatus bad{};
    st::TaskStatus good{};
    check(runtime.task_status("r0", "badtask", bad) == rt::ErrorCode::ok &&
              bad.state == st::TaskState::faulted && bad.fault_pou == "bad" &&
              bad.fault_sfc == "flow" && bad.fault_action == "fail",
          "L6-A05 task fault identifies POU network and action");
    check(runtime.task_status("r0", "goodtask", good) == rt::ErrorCode::ok &&
              good.state != st::TaskState::faulted,
          "L6-A05 SFC action fault is isolated from another task");
}

void trace_is_ordered_and_carries_source_positions()
{
    Rig rig;
    check(rig.build(program(
              "Runs : DINT;",
              "INITIAL_STEP A: Work(P); END_STEP\n"
              "STEP B: TERMINAL; END_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
              "ACTION Work: Runs := Runs + 1; END_ACTION")),
          "L6-A06 trace program builds");
    st::SfcTraceRecord records[32]{};
    check(rig.instance.configure_sfc_trace(records, 32) == rt::ErrorCode::ok,
          "L6-A06 caller-owned SFC trace buffer binds before scan");
    check(rig.scan(), "L6-A06 traced scan succeeds");
    const std::size_t count = rig.instance.sfc_trace_size();
    check(count >= 5, "L6-A06 scan emits step transition qualifier action events");
    bool saw_exit = false;
    bool saw_fire = false;
    bool saw_enter = false;
    bool saw_qualifier = false;
    bool saw_action = false;
    for(std::size_t index = 0; index < count; ++index) {
        check(records[index].line > 0 && records[index].column > 0,
              "L6-A06 every SFC trace event has a source position");
        if(records[index].kind == st::SfcEventKind::step_exit) saw_exit = true;
        if(records[index].kind == st::SfcEventKind::transition_fire) {
            saw_fire = saw_exit;
        }
        if(records[index].kind == st::SfcEventKind::step_enter) {
            saw_enter = saw_fire;
        }
        if(records[index].kind == st::SfcEventKind::qualifier_update) {
            saw_qualifier = saw_enter;
        }
        if(records[index].kind == st::SfcEventKind::action_execute) {
            saw_action = saw_qualifier;
        }
    }
    check(saw_exit && saw_fire && saw_enter && saw_qualifier && saw_action,
          "L6-A06 trace order is exit fire enter qualifier action");
}

std::string linear_network(std::size_t steps)
{
    std::string body = "INITIAL_STEP S0: END_STEP\n";
    for(std::size_t index = 1; index < steps; ++index) {
        body += "STEP S" + std::to_string(index) + ": END_STEP\n";
    }
    for(std::size_t index = 1; index < steps; ++index) {
        body += "TRANSITION FROM S" + std::to_string(index - 1) + " TO S" +
                std::to_string(index) + " := TRUE; END_TRANSITION\n";
    }
    body += "TRANSITION FROM S" + std::to_string(steps - 1) +
            " TO S0 := TRUE; END_TRANSITION\n";
    return program("X : BOOL;", body);
}

std::string action_network(std::size_t actions)
{
    std::string blocks;
    std::string bodies;
    for(std::size_t index = 0; index < actions; ++index) {
        blocks += " A" + std::to_string(index) + "(N);";
        bodies += "ACTION A" + std::to_string(index) +
                  ": X := X + 1; END_ACTION\n";
    }
    return program("X : DINT;", "INITIAL_STEP S:" + blocks +
                                      " TERMINAL; END_STEP\n" + bodies);
}

std::string branch_network(std::size_t width)
{
    std::string steps;
    std::string targets;
    std::string predecessors;
    for(std::size_t index = 0; index < width; ++index) {
        if(index != 0) {
            targets += ", ";
            predecessors += ", ";
        }
        targets += "B" + std::to_string(index);
        predecessors += "B" + std::to_string(index);
        steps += "STEP B" + std::to_string(index) + ": END_STEP\n";
    }
    return program(
        "X : BOOL;",
        "INITIAL_STEP Start: END_STEP\n" + steps + "STEP Done: END_STEP\n" +
            "TRANSITION FROM Start TO (" + targets +
            ") SIMULTANEOUS := TRUE; END_TRANSITION\n"
            "TRANSITION FROM (" + predecessors +
            ") TO Done SIMULTANEOUS := TRUE; END_TRANSITION\n"
            "TRANSITION FROM Done TO Start := TRUE; END_TRANSITION");
}

void exact_capacity_boundaries_are_enforced()
{
    const st::CompileOptions defaults;
    check(defaults.max_sfc_steps == 1024,
          "L6-A07 default SFC step capacity is 1024");
    check(defaults.max_sfc_transitions == 4096,
          "L6-A07 default SFC transition capacity is 4096");
    check(defaults.max_sfc_actions == 1024,
          "L6-A07 default SFC action capacity is 1024");
    check(defaults.max_sfc_action_blocks_per_step == 64,
          "L6-A07 default per-step action-block capacity is 64");
    check(defaults.max_sfc_branch_width == 64,
          "L6-A07 default SFC branch width is 64");

    st::CompileOptions options;
    options.max_sfc_steps = 4;
    check(st::compile(linear_network(4), options).ok,
          "L6-A07 step capacity N is accepted");
    const st::CompileResult steps = st::compile(linear_network(5), options);
    check(!steps.ok && has_code(steps, st::DiagCode::capacity_exceeded),
          "L6-A07 step capacity N+1 is rejected");

    options = st::CompileOptions{};
    options.max_sfc_transitions = 4;
    check(st::compile(linear_network(4), options).ok,
          "L6-A07 transition capacity N is accepted");
    const st::CompileResult transitions =
        st::compile(linear_network(5), options);
    check(!transitions.ok &&
              has_code(transitions, st::DiagCode::capacity_exceeded),
          "L6-A07 transition capacity N+1 is rejected");

    options = st::CompileOptions{};
    options.max_sfc_actions = 4;
    options.max_sfc_action_blocks_per_step = 4;
    check(st::compile(action_network(4), options).ok,
          "L6-A07 action and block capacity N is accepted");
    const st::CompileResult actions = st::compile(action_network(5), options);
    check(!actions.ok && has_code(actions, st::DiagCode::capacity_exceeded),
          "L6-A07 action and block capacity N+1 is rejected");

    options = st::CompileOptions{};
    options.max_sfc_branch_width = 4;
    check(st::compile(branch_network(4), options).ok,
          "L6-A07 branch width N is accepted");
    const st::CompileResult branch = st::compile(branch_network(5), options);
    check(!branch.ok && has_code(branch, st::DiagCode::capacity_exceeded),
          "L6-A07 branch width N+1 is rejected");
}

void graph_report_has_static_cost_and_reachability()
{
    const st::CompileResult result = st::compile(branch_network(3));
    check(result.ok, "L6 report network compiles");
    if(!result.ok) return;
    check(result.program.sfc_networks.size() == 1,
          "L6 report enumerates the SFC network");
    const st::SfcNetworkInfo &info = result.program.sfc_networks[0];
    check(info.step_count == 5 && info.reachable_step_count == 5,
          "L6 report records the complete reachable graph");
    check(info.max_parallel_active_steps == 3,
          "L6 report records maximum parallel active steps");
    check(info.worst_case_transition_evaluations > 0 &&
              info.worst_case_action_executions == 0 && info.static_bytes > 0,
          "L6 report records bounded scan cost and static storage");
}

void compile_and_trace_are_deterministic()
{
    const std::string source = branch_network(3);
    const st::CompileResult baseline = st::compile(source);
    check(baseline.ok, "L6-A07 deterministic source compiles");
    if(!baseline.ok) return;
    const std::string manifest = baseline.program.canonical_sfc_report();
    for(int iteration = 0; iteration < 25; ++iteration) {
        const st::CompileResult again = st::compile(source);
        check(again.ok && again.program.code == baseline.program.code &&
                  again.program.initial_data == baseline.program.initial_data &&
                  again.program.canonical_sfc_report() == manifest,
              "L6-A07 SFC compile artifact is deterministic");
    }

    Rig left;
    Rig right;
    check(left.build(source) && right.build(source),
          "L6-A06 deterministic trace instances load");
    st::SfcTraceRecord left_trace[128]{};
    st::SfcTraceRecord right_trace[128]{};
    check(left.instance.configure_sfc_trace(left_trace, 128) ==
                  rt::ErrorCode::ok &&
              right.instance.configure_sfc_trace(right_trace, 128) ==
                  rt::ErrorCode::ok,
          "L6-A06 deterministic trace buffers bind");
    for(int scan = 0; scan < 8; ++scan) {
        check(left.scan() && right.scan(), "L6 deterministic runtime scan");
    }
    check(left.instance.sfc_trace_size() == right.instance.sfc_trace_size() &&
              std::memcmp(left_trace, right_trace,
                          left.instance.sfc_trace_size() *
                              sizeof(st::SfcTraceRecord)) == 0,
          "L6-A06 identical scans emit byte-identical trace records");
}

void sfc_scan_is_zero_allocation()
{
    Rig rig;
    check(rig.build(branch_network(16)),
          "L6-A07 zero-allocation SFC network builds");
    st::SfcTraceRecord records[4096]{};
    check(rig.instance.configure_sfc_trace(records, 4096) == rt::ErrorCode::ok &&
              rig.scan(),
          "L6-A07 zero-allocation SFC warm-up succeeds");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    bool healthy = true;
    for(int scan = 0; scan < 1000; ++scan) {
        if(!rig.scan()) {
            healthy = false;
            break;
        }
    }
    g_freeze_allocations = false;
    check(healthy, "L6-A07 SFC remains healthy with allocator frozen");
    check(g_frozen_allocations == 0,
          "L6-A07 SFC scan performs zero allocations");
}

void explicit_restart_reinitializes_the_network()
{
    Rig rig;
    check(rig.build(program(
              "X : DINT;",
              "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
              "TRANSITION FROM A TO B := TRUE; END_TRANSITION")) &&
              rig.scan() && rig.active("b"),
          "L6-D01 restart fixture reaches non-initial step");
    check(rig.instance.restart_sfc("flow") == rt::ErrorCode::ok &&
              rig.active("a") && !rig.active("b"),
          "L6-D01 explicit restart activates only the initial step");
}

void lower_language_source_regression()
{
    const char *sources[] = {
        "PROGRAM Main VAR X : DINT; END_VAR X := 2 + 3 * 4; END_PROGRAM",
        "TYPE Pair : STRUCT A : DINT; B : DINT; END_STRUCT END_TYPE "
        "PROGRAM Main VAR P : Pair := (A := 2, B := 5); X : DINT; END_VAR "
        "X := P.A + P.B; END_PROGRAM",
        "FUNCTION Twice : DINT VAR_INPUT X : DINT; END_VAR "
        "Twice := X * 2; END_FUNCTION "
        "PROGRAM Main VAR X : DINT; END_VAR X := Twice(5); END_PROGRAM",
    };
    for(const char *source : sources) {
        const st::CompileResult compiled = st::compile(source);
        check(compiled.ok, "L6-A08 lower-layer source still compiles");
        if(!compiled.ok) continue;
        alignas(8) unsigned char storage[65536]{};
        st::Instance instance;
        check(instance.load(compiled.program, "main", storage,
                            sizeof(storage), kPeriodNs) == rt::ErrorCode::ok &&
                  instance.scan(kBudget) == st::ScanError::ok,
              "L6-A08 lower-layer source still executes");
    }
}

} // namespace

int main()
{
    sequential_network_uses_old_active_snapshot();
    selection_uses_source_declaration_priority();
    simultaneous_divergence_activates_every_successor();
    simultaneous_convergence_waits_for_every_predecessor();
    conflicting_step_update_is_rejected();
    n_qualifier_runs_only_while_step_is_active();
    n_qualifier_resumes_after_step_reactivation();
    s_qualifier_latches_until_r_and_can_reactivate();
    r_wins_same_scan_qualifier_conflicts();
    l_qualifier_has_zero_one_and_n_cycle_boundaries();
    l_qualifier_stops_early_and_restarts_on_reactivation();
    d_qualifier_has_zero_one_and_n_cycle_boundaries();
    d_qualifier_cancels_delay_on_exit_and_reactivation();
    p_qualifier_pulses_on_initial_and_later_activation();
    sd_qualifier_survives_early_exit_and_latches_at_deadline();
    sd_zero_one_boundaries_and_r_cancellation();
    sd_can_be_requested_again_after_r();
    ds_requires_continuous_activation_then_stays_stored();
    ds_early_exit_cancels_and_zero_reactivates();
    ds_r_clears_latch_and_allows_reactivation();
    sl_has_zero_one_n_expiry_and_r_priority();
    sl_reactivation_starts_a_fresh_lifetime();
    action_bodies_follow_declaration_order();
    shared_action_executes_once_per_scan();
    unsafe_networks_have_stable_recovering_diagnostics();
    negative_time_has_stable_range_diagnostic();
    task_fault_names_the_sfc_action_and_isolates_other_task();
    trace_is_ordered_and_carries_source_positions();
    exact_capacity_boundaries_are_enforced();
    graph_report_has_static_cost_and_reachability();
    compile_and_trace_are_deterministic();
    sfc_scan_is_zero_allocation();
    explicit_restart_reinitializes_the_network();
    lower_language_source_regression();
    if(failures == 0) std::printf("PASS st_l6_tests\n");
    return failures == 0 ? 0 : 1;
}
