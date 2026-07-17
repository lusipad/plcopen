// L5 task-runtime acceptance (approved st-l5-semantics sections 0-5).
//
// This is intentionally a RED contract.  It requires a caller-driven,
// allocation-free ConfigurationRuntime whose boundary sampling is separate
// from cooperative execution.  The tests do not emulate scheduling in the
// fixture and do not give the VM access to a clock.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "axis/state.h"
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

constexpr std::int64_t kTickNs = 1000000;
constexpr std::int64_t kRunSlice = 1000000;

int failures = 0;

void fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    ++failures;
}

void check(bool condition, const char *name)
{
    if(!condition) fail(name);
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) return true;
    }
    return false;
}

std::string configuration(std::string_view pous, std::string_view body,
                          std::string_view globals = {})
{
    std::string source(globals);
    source += pous;
    source += "\nCONFIGURATION Plant\n";
    source += body;
    source += "\nEND_CONFIGURATION\n";
    return source;
}

std::string one_periodic(std::string_view program,
                         std::string_view declarations,
                         std::string_view body,
                         std::string_view task =
                             "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, "
                             "PRIORITY := 0, BUDGET := 10000);")
{
    std::string pous = "PROGRAM ";
    pous += program;
    pous += "\n";
    pous += declarations;
    pous += "\n";
    pous += body;
    pous += "\nEND_PROGRAM\n";
    std::string config = "RESOURCE R0 ON PLC\n";
    config += task;
    config += "\nPROGRAM P0 WITH Main : ";
    config += program;
    config += ";\nEND_RESOURCE";
    return configuration(pous, config);
}

struct Rig
{
    st::CompileResult compiled;
    st::ConfigurationRuntime runtime;
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
        return runtime.load(compiled.program, "plant", storage,
                            sizeof(storage), kTickNs) == rt::ErrorCode::ok;
    }

    bool boundary(std::uint64_t tick, const st::EventSample *events = nullptr,
                  std::size_t event_count = 0)
    {
        return runtime.boundary(tick, events, event_count) ==
               rt::ErrorCode::ok;
    }

    bool drain(std::int64_t slice = kRunSlice)
    {
        return runtime.run(slice) == rt::ErrorCode::ok;
    }

    st::TaskStatus task(const char *resource, const char *name)
    {
        st::TaskStatus status{};
        check(runtime.task_status(resource, name, status) ==
                  rt::ErrorCode::ok,
              "L5 task status lookup succeeds");
        return status;
    }

    std::int64_t value(const char *resource, const char *instance,
                       const char *name)
    {
        std::int64_t result = -424242;
        check(runtime.value_i64(resource, instance, name, result) ==
                  rt::ErrorCode::ok,
              "L5 PROGRAM value lookup succeeds");
        return result;
    }

    std::uint64_t memory(const char *resource, std::size_t offset = 0)
    {
        unsigned char bytes[64]{};
        std::size_t written = 0;
        std::uint64_t version = 0;
        check(runtime.memory_snapshot(resource, bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok,
              "L5 resource memory snapshot succeeds");
        std::uint64_t value = 0;
        const std::size_t available = offset < written ? written - offset : 0;
        const std::size_t width = available < 8 ? available : 8;
        for(std::size_t index = 0; index < width; ++index) {
            value |= static_cast<std::uint64_t>(bytes[offset + index]) <<
                     (index * 8U);
        }
        return value;
    }
};

// L5-A01/D01-D02: periodic and event releases match one integer oracle for
// 10,000 base ticks.  The event level is sampled only by boundary().
void ten_thousand_tick_release_oracle()
{
    const char *pous =
        "PROGRAM Every2 VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "PROGRAM Every3 VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "PROGRAM OnEdge VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK T2(INTERVAL := T#2ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "TASK T3(INTERVAL := T#3ms, PHASE := T#1ms, PRIORITY := 1, "
        "BUDGET := 100);\n"
        "TASK TE(EVENT := Trigger, PRIORITY := 2, BUDGET := 100);\n"
        "PROGRAM P2 WITH T2 : Every2;\n"
        "PROGRAM P3 WITH T3 : Every3;\n"
        "PROGRAM PE WITH TE : OnEdge;\n"
        "END_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config,
                                  "VAR_GLOBAL Trigger : BOOL; END_VAR\n")),
          "L5-A01 release-oracle configuration builds");
    std::int64_t expected2 = 0;
    std::int64_t expected3 = 0;
    std::int64_t expected_event = 0;
    for(std::uint64_t tick = 0; tick < 10000; ++tick) {
        const bool level = tick % 17U == 3U;
        const st::EventSample event{"r0", "trigger", level};
        if(!rig.boundary(tick, &event, 1) || !rig.drain()) {
            fail("L5-A01 every oracle tick executes");
            return;
        }
        if(tick % 2U == 0U) ++expected2;
        if(tick % 3U == 1U) ++expected3;
        if(level) ++expected_event;
        if(rig.value("r0", "p2", "n") != expected2 ||
           rig.value("r0", "p3", "n") != expected3 ||
           rig.value("r0", "pe", "n") != expected_event) {
            fail("L5-A01 10000-tick integer oracle matches every release");
            return;
        }
    }
}

void event_requires_a_new_rising_edge()
{
    Rig rig;
    check(rig.build(configuration(
              "PROGRAM Edge VAR N : DINT; END_VAR N := N + 1; END_PROGRAM",
              "RESOURCE R0 ON PLC\n"
              "TASK E(EVENT := Trigger, PRIORITY := 0, BUDGET := 100);\n"
              "PROGRAM PE WITH E : Edge;\nEND_RESOURCE",
              "VAR_GLOBAL Trigger : BOOL; END_VAR\n")),
          "L5-D02 edge configuration builds");
    const bool levels[] = {false, true, true, true, false, true};
    for(std::uint64_t tick = 0; tick < 6; ++tick) {
        const st::EventSample event{"r0", "trigger", levels[tick]};
        check(rig.boundary(tick, &event, 1) && rig.drain(),
              "L5-D02 event boundary executes");
    }
    check(rig.value("r0", "pe", "n") == 2,
          "L5-D02 continuous high level releases once per rising edge");
}

// L5-A02/D03: lower numeric priority executes first; the final transactional
// write is therefore from the lower-priority task that ran last.
void priority_orders_same_tick_commits()
{
    const char *globals =
        "VAR_GLOBAL Winner AT %MD0 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM High VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 1; END_PROGRAM\n"
        "PROGRAM Low VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 2; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK LowTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 7, "
        "BUDGET := 100);\n"
        "TASK HighTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "PROGRAM PL WITH LowTask : Low;\n"
        "PROGRAM PH WITH HighTask : High;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)),
          "L5-A02 priority configuration builds");
    check(rig.boundary(0) && rig.drain(),
          "L5-A02 same-tick priority release executes");
    check(rig.memory("r0") == 2,
          "L5-D03 priority zero runs before priority seven");
    st::ResourceStatus status{};
    check(rig.runtime.resource_status("r0", status) == rt::ErrorCode::ok &&
              status.write_conflict_count == 1,
          "L5-A03 same-release write conflict is counted once");
}

void declaration_order_breaks_priority_ties()
{
    const char *globals =
        "VAR_GLOBAL Winner AT %MD0 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM First VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 11; END_PROGRAM\n"
        "PROGRAM Second VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 22; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK A(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 3, "
        "BUDGET := 100);\n"
        "TASK B(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 3, "
        "BUDGET := 100);\n"
        "PROGRAM PA WITH A : First;\n"
        "PROGRAM PB WITH B : Second;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)),
          "L5-D03 declaration-order configuration builds");
    check(rig.boundary(0) && rig.drain() && rig.memory("r0") == 22,
          "L5-D03 later-declared equal-priority task commits last");
}

void program_mapping_order_is_preserved()
{
    const char *globals =
        "VAR_GLOBAL Winner AT %MD0 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM First VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 31; END_PROGRAM\n"
        "PROGRAM Second VAR_EXTERNAL Winner : DWORD; END_VAR "
        "Winner := 32; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "PROGRAM PFirst WITH T : First;\n"
        "PROGRAM PSecond WITH T : Second;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)),
          "L5-D03 PROGRAM-order configuration builds");
    check(rig.boundary(0) && rig.drain() && rig.memory("r0") == 32,
          "L5-D03 same-task PROGRAM mappings execute in declaration order");
}

// L5-A03/D05-D06: each mapping has independent persistent state, while a
// reader observes either complete committed pairs, never a half-write.
void program_instances_are_independent()
{
    Rig rig;
    check(rig.build(configuration(
              "PROGRAM Counter VAR N : DINT; END_VAR N := N + 1; END_PROGRAM",
              "RESOURCE R0 ON PLC\n"
              "TASK Fast(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
              "BUDGET := 100);\n"
              "TASK Slow(INTERVAL := T#2ms, PHASE := T#0ms, PRIORITY := 1, "
              "BUDGET := 100);\n"
              "PROGRAM A WITH Fast : Counter;\n"
              "PROGRAM B WITH Slow : Counter;\nEND_RESOURCE")),
          "L5-D05 independent-instance configuration builds");
    for(std::uint64_t tick = 0; tick < 10; ++tick) {
        check(rig.boundary(tick) && rig.drain(),
              "L5-D05 independent-instance tick executes");
    }
    check(rig.value("r0", "a", "n") == 10 &&
              rig.value("r0", "b", "n") == 5,
          "L5-D05 PROGRAM mappings retain independent instance state");
}

void task_snapshots_never_expose_half_writes()
{
    const char *globals =
        "VAR_GLOBAL Left AT %MD0 : DWORD; Right AT %MD4 : DWORD; "
        "Torn AT %MD8 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM Writer VAR_EXTERNAL Left : DWORD; Right : DWORD; END_VAR "
        "Left := Left + 1; Right := Left; END_PROGRAM\n"
        "PROGRAM Reader VAR_EXTERNAL Left : DWORD; Right : DWORD; "
        "Torn : DWORD; END_VAR "
        "IF Left <> Right THEN Torn := Torn + 1; END_IF; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK Write(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "TASK Read(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 100);\n"
        "PROGRAM PW WITH Write : Writer;\n"
        "PROGRAM PR WITH Read : Reader;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)),
          "L5-A03 snapshot configuration builds");
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        if(!rig.boundary(tick) || !rig.drain()) {
            fail("L5-A03 snapshot tick executes");
            return;
        }
    }
    check(rig.memory("r0", 8) == 0,
          "L5-D06 task snapshot never exposes a half-written pair");
}

// L5-A04/D04: release sampling continues while cooperative execution is
// yielded.  New releases are counted, not queued or replayed.
void missed_releases_do_not_catch_up()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Long", "VAR I : DINT; N : DINT; END_VAR",
              "FOR I := 1 TO 100 DO N := N + 1; END_FOR;",
              "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
              "BUDGET := 10000);")),
          "L5-A04 long-running task builds");
    check(rig.boundary(0) && rig.runtime.run(1) == rt::ErrorCode::ok,
          "L5-A04 first release yields after one instruction");
    for(std::uint64_t tick = 1; tick < 5; ++tick) {
        check(rig.boundary(tick), "L5-A04 busy task samples later release");
    }
    st::TaskStatus status = rig.task("r0", "main");
    check(status.state == st::TaskState::running &&
              status.release_count == 1 && status.missed_release_count == 4,
          "L5-D04 busy task counts four missed releases without queuing");
    check(rig.drain(), "L5-A04 original release eventually completes");
    status = rig.task("r0", "main");
    check(status.completed_count == 1 && status.release_count == 1,
          "L5-D04 completion does not catch up missed releases");
    check(rig.boundary(5) && rig.drain(),
          "L5-D04 next boundary releases exactly one new scan");
    status = rig.task("r0", "main");
    check(status.completed_count == 2 && status.release_count == 2,
          "L5-D04 scheduler resumes from the current release only");
}

void instruction_budget_fault_discards_output()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Budget", "VAR Q AT %QD0 : DWORD; N : DINT; END_VAR",
              "N := N + 1; Q := DINT_TO_DWORD(N);",
              "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
              "BUDGET := 2);")),
          "L5-A05 budget-fault configuration builds");
    check(rig.boundary(0) && rig.drain(),
          "L5-A05 budget-fault release executes");
    const st::TaskStatus status = rig.task("r0", "main");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 99;
    check(status.state == st::TaskState::faulted &&
              status.fault == st::TaskFault::task_budget_exceeded &&
              status.fault_count == 1 && !status.fault_pou.empty() &&
              status.fault_instruction != 0,
          "L5-D08 budget fault preserves reason POU position and count");
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              version == 0,
          "L5-D08 faulted task discards its L3 output transaction");
}

void wallclock_fault_is_injected_at_a_boundary()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Slow", "VAR I : DINT; Q AT %QD0 : DWORD; END_VAR",
              "FOR I := 1 TO 100 DO Q := Q + 1; END_FOR;")),
          "L5-D09 wallclock-fault configuration builds");
    check(rig.boundary(0) && rig.runtime.run(1) == rt::ErrorCode::ok,
          "L5-D09 task is active before host report");
    check(rig.runtime.report_wallclock_exceeded("r0", "main") ==
              rt::ErrorCode::ok,
          "L5-D09 host queues wallclock report without VM clock access");
    check(rig.boundary(1), "L5-D09 report latches at next boundary");
    const st::TaskStatus status = rig.task("r0", "main");
    check(status.state == st::TaskState::faulted &&
              status.fault == st::TaskFault::task_wallclock_exceeded,
          "L5-D09 wallclock report becomes stable task fault");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 99;
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              version == 0,
          "L5-D09 wallclock-faulted release does not commit output");
}

void faulted_task_is_isolated_from_other_tasks()
{
    const char *pous =
        "PROGRAM Bad VAR Z : DINT; X : DINT; END_VAR X := 1 / Z; "
        "END_PROGRAM\n"
        "PROGRAM Good VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK BadTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "TASK GoodTask(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 100);\n"
        "PROGRAM PB WITH BadTask : Bad;\n"
        "PROGRAM PG WITH GoodTask : Good;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config)),
          "L5-D10 task-fault isolation configuration builds");
    for(std::uint64_t tick = 0; tick < 10; ++tick) {
        check(rig.boundary(tick) && rig.drain(),
              "L5-D10 healthy task continues beside faulted task");
    }
    const st::TaskStatus bad = rig.task("r0", "badtask");
    const st::TaskStatus good = rig.task("r0", "goodtask");
    check(bad.state == st::TaskState::faulted && bad.release_count == 1 &&
              good.completed_count == 10 && rig.value("r0", "pg", "n") == 10,
          "L5-D10 task fault stops only its own future releases");
}

void reset_preserves_state_restart_reinitializes_state()
{
    Rig rig;
    const std::string source =
        "FUNCTION_BLOCK Accum VAR_OUTPUT Q : DINT; END_VAR "
        "Q := Q + 1; END_FUNCTION_BLOCK\n" +
        one_periodic(
            "Stateful",
            "VAR C : Accum; N : DINT := 10; FbState : DINT; END_VAR",
            "C(); N := N + 1; FbState := C.Q;");
    check(rig.build(source),
          "L5-A06 recovery configuration builds");
    check(rig.boundary(0) && rig.drain() &&
              rig.value("r0", "p0", "n") == 11 &&
              rig.value("r0", "p0", "fbstate") == 1,
          "L5-A06 initial stateful release completes");
    check(rig.boundary(1) &&
              rig.runtime.run(1) == rt::ErrorCode::ok &&
              rig.runtime.report_wallclock_exceeded("r0", "main") ==
                  rt::ErrorCode::ok &&
              rig.boundary(2),
          "L5-A06 host faults an active task at a boundary");
    check(rig.runtime.reset_task("r0", "main") == rt::ErrorCode::ok &&
              rig.boundary(3) && rig.boundary(4) && rig.drain(),
          "L5-D11 reset applies at boundary and realigns next release");
    st::TaskStatus status = rig.task("r0", "main");
    check(status.fault == st::TaskFault::none &&
              status.missed_release_count == 0 &&
              rig.value("r0", "p0", "n") == 12 &&
              rig.value("r0", "p0", "fbstate") == 2,
          "L5-D11 reset clears diagnostics and preserves variable/FB state");

    check(rig.runtime.restart_task("r0", "main") == rt::ErrorCode::ok &&
              rig.boundary(5) && rig.boundary(6) && rig.drain(),
          "L5-D12 restart applies before next release");
    check(rig.value("r0", "p0", "n") == 11 &&
              rig.value("r0", "p0", "fbstate") == 1,
          "L5-D12 restart reinitializes PROGRAM and nested FB state");
}

void resource_fault_stops_one_resource_only()
{
    const char *pous =
        "PROGRAM Counter VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK T0(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\nPROGRAM P0 WITH T0 : Counter;\nEND_RESOURCE\n"
        "RESOURCE R1 ON PLC\n"
        "TASK T1(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\nPROGRAM P1 WITH T1 : Counter;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config)),
          "L5-D13 multi-resource configuration builds");
    check(rig.boundary(0) && rig.drain(),
          "L5-D13 both resources execute before fault");
    check(rig.runtime.report_resource_fault(
              "r0", st::ResourceFault::image_commit_failed) ==
                  rt::ErrorCode::ok,
          "L5-D13 host queues resource image fault");
    for(std::uint64_t tick = 1; tick < 10; ++tick) {
        check(rig.boundary(tick) && rig.drain(),
              "L5-D13 unaffected resource continues");
    }
    st::ResourceStatus left{};
    st::ResourceStatus right{};
    check(rig.runtime.resource_status("r0", left) == rt::ErrorCode::ok &&
              rig.runtime.resource_status("r1", right) == rt::ErrorCode::ok &&
              left.fault == st::ResourceFault::image_commit_failed &&
              right.fault == st::ResourceFault::none &&
              rig.value("r0", "p0", "n") == 1 &&
              rig.value("r1", "p1", "n") == 10,
          "L5-D13 resource fault neither runs locally nor propagates remotely");
}

// L5-A07/D07: the ST runtime owns no motion executor and returns after a
// fault; a host can continue consuming one motion tick per base boundary.
void motion_tick_consumption_survives_st_fault()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Bad", "VAR Z : DINT; X : DINT; END_VAR", "X := 1 / Z;")),
          "L5-A07 faulting ST configuration builds");
    axis::AxisModel motion;
    motion.set_power(true);
    std::uint64_t motion_ticks = 0;
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        check(rig.boundary(tick) && rig.drain(),
              "L5-A07 faulted ST boundary returns to host");
        motion.cycle();
        ++motion_ticks;
    }
    check(motion_ticks == 1000 &&
              rig.task("r0", "main").state == st::TaskState::faulted,
          "L5-D07 motion-domain cadence survives latched ST task fault");
}

void invalid_task_configuration_has_stable_diagnostics()
{
    st::CompileOptions options;
    options.base_tick_ns = kTickNs;
    const char *tasks[] = {
        "TASK Main(INTERVAL := T#0ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 10);",
        "TASK Main(INTERVAL := T#1500us, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 10);",
        "TASK Main(INTERVAL := T#2ms, PHASE := T#2ms, PRIORITY := 0, "
        "BUDGET := 10);",
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := -1, "
        "BUDGET := 10);",
    };
    const st::DiagCode codes[] = {
        st::DiagCode::sema_task_interval_invalid,
        st::DiagCode::sema_task_interval_invalid,
        st::DiagCode::sema_task_phase_invalid,
        st::DiagCode::sema_task_priority_invalid,
    };
    for(std::size_t index = 0; index < 4; ++index) {
        const std::string source = one_periodic("P", "", ";", tasks[index]);
        const st::CompileResult result = st::compile(source, options);
        check(!result.ok && has_code(result, codes[index]),
              "L5-D01 invalid task configuration has stable diagnostic");
    }
}

void duplicate_and_unmapped_programs_are_diagnosed()
{
    const char *pous =
        "PROGRAM P VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n";
    const char *duplicate =
        "RESOURCE R0 ON PLC\n"
        "TASK A(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "TASK B(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 100);\n"
        "PROGRAM Same WITH A : P;\nPROGRAM Same WITH B : P;\nEND_RESOURCE";
    const st::CompileResult bad =
        st::compile(configuration(pous, duplicate));
    check(!bad.ok &&
              has_code(bad, st::DiagCode::sema_program_duplicate_mapping),
          "L5-D05 one instance cannot map to two tasks");

    const st::CompileResult warning = st::compile(configuration(
        pous, "RESOURCE R0 ON PLC\nEND_RESOURCE"));
    check(warning.ok &&
              has_code(warning, st::DiagCode::warning_program_unmapped),
          "L5 unmapped PROGRAM compiles but emits stable warning");
}

void task_self_control_and_dynamic_tasks_are_rejected()
{
    const char *bodies[] = {"RESET_TASK(Main);", "CREATE_TASK(Main);",
                            "DELETE_TASK(Main);"};
    for(const char *body : bodies) {
        const st::CompileResult result =
            st::compile(one_periodic("P", "", body));
        check(!result.ok &&
                  has_code(result, st::DiagCode::unsupported_l5_task_control),
              "L5 task self-control and dynamic tasks are unsupported");
    }
}

std::string capacity_source(std::size_t resources, std::size_t tasks,
                            std::size_t mappings)
{
    std::string source =
        "PROGRAM P VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\n";
    for(std::size_t resource = 0; resource < resources; ++resource) {
        source += "RESOURCE R" + std::to_string(resource) + " ON PLC\n";
        for(std::size_t task = 0; task < tasks; ++task) {
            source += "TASK T" + std::to_string(task) +
                      "(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
                      "BUDGET := 100);\n";
        }
        for(std::size_t mapping = 0; mapping < mappings; ++mapping) {
            source += "PROGRAM P" + std::to_string(mapping) + " WITH T0 : P;\n";
        }
        source += "END_RESOURCE\n";
    }
    source += "END_CONFIGURATION\n";
    return source;
}

void resource_capacity_is_exact()
{
    check(st::compile(capacity_source(16, 1, 1)).ok,
          "L5-A08 sixteen resources fit");
    const st::CompileResult overflow =
        st::compile(capacity_source(17, 1, 1));
    check(!overflow.ok && has_code(overflow, st::DiagCode::capacity_resources),
          "L5-A08 seventeenth resource is rejected");
}

void task_capacity_is_exact()
{
    check(st::compile(capacity_source(1, 64, 1)).ok,
          "L5-A08 sixty-four tasks fit one resource");
    const st::CompileResult overflow =
        st::compile(capacity_source(1, 65, 1));
    check(!overflow.ok && has_code(overflow, st::DiagCode::capacity_tasks),
          "L5-A08 sixty-fifth task is rejected");
}

void program_mapping_capacity_is_exact()
{
    check(st::compile(capacity_source(1, 1, 64)).ok,
          "L5-A08 sixty-four PROGRAM mappings fit one task");
    const st::CompileResult overflow =
        st::compile(capacity_source(1, 1, 65));
    check(!overflow.ok &&
              has_code(overflow, st::DiagCode::capacity_program_mappings),
          "L5-A08 sixty-fifth PROGRAM mapping is rejected");
}

void artifact_reports_release_costs_and_image_bytes()
{
    const st::CompileResult compiled = st::compile(one_periodic(
        "Report", "VAR I AT %ID0 : DWORD; Q AT %QD0 : DWORD; END_VAR",
        "Q := I + 1;"));
    check(compiled.ok, "L5 report configuration compiles");
    if(!compiled.ok) return;
    st::TaskingReport report{};
    check(compiled.program.tasking_report("plant", report) ==
                  rt::ErrorCode::ok &&
              report.resources == 1 && report.tasks == 1 &&
              report.program_mappings == 1 &&
              report.release_worst_case_instructions > 0 &&
              report.image_copy_bytes >= 8 &&
              report.required_runtime_bytes > 0,
          "L5 artifact reports release cost image copy and runtime storage");
}

void compilation_and_schedule_are_deterministic()
{
    const std::string source = configuration(
        "PROGRAM Periodic VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "PROGRAM Event VAR N : DINT; END_VAR N := N + 1; END_PROGRAM",
        "RESOURCE R0 ON PLC\n"
        "TASK A(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 2, "
        "BUDGET := 100);\n"
        "TASK E(EVENT := Trigger, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM PA WITH A : Periodic;\n"
        "PROGRAM PE WITH E : Event;\nEND_RESOURCE",
        "VAR_GLOBAL Trigger : BOOL; END_VAR\n");
    const st::CompileResult baseline = st::compile(source);
    check(baseline.ok, "L5 deterministic source compiles");
    if(!baseline.ok) return;
    const std::string manifest = baseline.program.canonical_manifest();
    for(int repeat = 0; repeat < 25; ++repeat) {
        const st::CompileResult again = st::compile(source);
        check(again.ok && again.program.code == baseline.program.code &&
                  again.program.constants == baseline.program.constants &&
                  again.program.canonical_manifest() == manifest,
              "L5 compiler and task manifest are deterministic");
    }

    Rig left;
    Rig right;
    check(left.build(source) && right.build(source),
          "L5 duplicate runtimes load");
    std::uint32_t random = 0xC001D00DU;
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        random = random * 1664525U + 1013904223U;
        const st::EventSample event{"r0", "trigger", (random & 3U) == 0U};
        if(!left.boundary(tick, &event, 1) ||
           !right.boundary(tick, &event, 1) || !left.drain() ||
           !right.drain()) {
            fail("L5 duplicate runtime ticks execute");
            return;
        }
        const st::TaskStatus a = left.task("r0", "a");
        const st::TaskStatus b = right.task("r0", "a");
        if(a.release_count != b.release_count ||
           a.completed_count != b.completed_count ||
           left.value("r0", "pa", "n") != right.value("r0", "pa", "n") ||
           left.value("r0", "pe", "n") != right.value("r0", "pe", "n")) {
            fail("L5 identical schedules produce identical state each tick");
            return;
        }
    }
}

void cycle_path_is_zero_allocation()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Rt", "VAR I AT %ID0 : DWORD; Q AT %QD0 : DWORD; END_VAR",
              "Q := I + 1;")),
          "L5 RT configuration builds");
    check(rig.boundary(0) && rig.drain(), "L5 RT warm-up completes");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    for(std::uint64_t tick = 1; tick <= 1000; ++tick) {
        if(!rig.boundary(tick) || !rig.drain()) {
            g_freeze_allocations = false;
            fail("L5 RT frozen tick completes");
            return;
        }
    }
    g_freeze_allocations = false;
    check(g_frozen_allocations == 0,
          "L5 boundary scheduling VM execution and L3 commit allocate zero");
}

// L5 may change the compiled artifact ABI, but current-revision L0-L3 source
// behavior remains available when a PROGRAM is explicitly task-mapped.
void lower_layer_source_regression()
{
    struct SourceCase
    {
        const char *name;
        const char *declarations;
        const char *body;
        const char *result;
        std::int64_t expected;
    };
    const SourceCase cases[] = {
        {"L0 control", "VAR X : DINT; Y : DINT; END_VAR",
         "X := 2 + 3 * 4; IF X = 14 THEN Y := 1; END_IF;", "y", 1},
        {"L1a scalar", "VAR X : SINT; Y : DINT; END_VAR",
         "X := 127; X := X + 1; Y := SINT_TO_DINT(X);", "y", -128},
        {"L1b aggregate",
         "VAR P : Pair := (A := 3, B := 4); Out : DINT; END_VAR",
         "Out := P.A + P.B;", "out", 7},
        {"L2b function", "VAR Out : DINT; END_VAR", "Out := UserAdd(5, 6);",
         "out", 11},
        {"L3 process image",
         "VAR I AT %ID0 : DWORD; Out : DINT; END_VAR",
         "Out := DWORD_TO_DINT(I) + 2;", "out", 2},
    };
    for(const SourceCase &test : cases) {
        std::string prefix;
        if(std::string_view(test.name) == "L1b aggregate") {
            prefix = "TYPE Pair : STRUCT A : DINT; B : DINT; END_STRUCT; "
                     "END_TYPE\n";
        } else if(std::string_view(test.name) == "L2b function") {
            prefix = "FUNCTION UserAdd : DINT VAR_INPUT A : DINT; B : DINT; "
                     "END_VAR UserAdd := A + B; END_FUNCTION\n";
        }
        const std::string source =
            prefix + one_periodic("Regression", test.declarations, test.body);
        Rig rig;
        if(!rig.build(source)) {
            fail(test.name);
            continue;
        }
        check(rig.boundary(0) && rig.drain() &&
                  rig.value("r0", "p0", test.result) == test.expected,
              test.name);
    }
}

} // namespace

int main()
{
    ten_thousand_tick_release_oracle();
    event_requires_a_new_rising_edge();
    priority_orders_same_tick_commits();
    declaration_order_breaks_priority_ties();
    program_mapping_order_is_preserved();
    program_instances_are_independent();
    task_snapshots_never_expose_half_writes();
    missed_releases_do_not_catch_up();
    instruction_budget_fault_discards_output();
    wallclock_fault_is_injected_at_a_boundary();
    faulted_task_is_isolated_from_other_tasks();
    reset_preserves_state_restart_reinitializes_state();
    resource_fault_stops_one_resource_only();
    motion_tick_consumption_survives_st_fault();
    invalid_task_configuration_has_stable_diagnostics();
    duplicate_and_unmapped_programs_are_diagnosed();
    task_self_control_and_dynamic_tasks_are_rejected();
    resource_capacity_is_exact();
    task_capacity_is_exact();
    program_mapping_capacity_is_exact();
    artifact_reports_release_costs_and_image_bytes();
    compilation_and_schedule_are_deterministic();
    cycle_path_is_zero_allocation();
    lower_layer_source_regression();
    if(failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l5 tests passed\n");
    return 0;
}
