// L7 monitor/force/debugger acceptance (approved st-l7-semantics, 2026-07-17).
//
// This file is intentionally the RED contract.  It fixes the caller-owned,
// bounded core API needed by a host debugger without adding a socket, UI,
// authentication store or online bytecode replacement surface.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

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
constexpr std::int64_t kRunBudget = 1000000;

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

std::string debug_configuration(std::string body,
                                std::string declarations =
                                    "X : DINT; Y : DINT; Z : DINT;")
{
    return "PROGRAM Main\nVAR " + declarations +
           " END_VAR\n" + body +
           "\nEND_PROGRAM\n"
           "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
           "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
           "BUDGET := 100000);\n"
           "PROGRAM P0 WITH Main : Main;\n"
           "END_RESOURCE\nEND_CONFIGURATION\n";
}

struct Rig
{
    st::CompileResult compiled;
    st::ConfigurationRuntime runtime;
    st::DebugSession debug;
    alignas(8) unsigned char runtime_storage[1048576]{};
    alignas(8) unsigned char publish_storage[262144]{};
    alignas(8) unsigned char snapshot_storage[262144]{};
    st::DebugSnapshotEntry publish_entries[8192]{};
    st::DebugSnapshotEntry snapshot_entries[8192]{};
    st::DebugTraceRecord trace_storage[256]{};

    bool build(const std::string &source,
               st::DebugMode mode = st::DebugMode::enabled,
               const st::DebugSessionOptions &session_options =
                   st::DebugSessionOptions{})
    {
        st::CompileOptions options;
        options.source_name = "debug.st";
        options.debug_mode = mode;
        compiled = st::compile(source, options);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        if(runtime.load(compiled.program, "plant", runtime_storage,
                        sizeof(runtime_storage), kTickNs) != rt::ErrorCode::ok) {
            return false;
        }
        return debug.attach(runtime, {"r0", "main"}, publish_storage,
                            sizeof(publish_storage), publish_entries,
                            8192, trace_storage, 256, session_options) ==
               st::DebugError::ok;
    }

    bool tick(std::uint64_t tick)
    {
        return runtime.boundary(tick, nullptr, 0) == rt::ErrorCode::ok &&
               runtime.run(kRunBudget) == rt::ErrorCode::ok;
    }

    st::TaskStatus task(const char *name)
    {
        st::TaskStatus status{};
        check(runtime.task_status("r0", name, status) == rt::ErrorCode::ok,
              "L7 task status is readable");
        return status;
    }

    st::SymbolInfo symbol(const char *qualified_name) const
    {
        st::SymbolInfo info{};
        check(compiled.program.find_symbol(qualified_name, info) ==
                  rt::ErrorCode::ok,
              "L7 fully-qualified symbol exists");
        return info;
    }

    st::DebugError snapshot(st::DebugSnapshot &header,
                            std::size_t retries = 8)
    {
        return debug.read_snapshot(header, snapshot_entries, 8192,
                                   snapshot_storage, sizeof(snapshot_storage),
                                   retries);
    }
};

std::int64_t snapshot_i64(const Rig &rig, const st::DebugSnapshot &snapshot,
                          st::SymbolId id)
{
    for(std::size_t index = 0; index < snapshot.value_count; ++index) {
        const st::DebugSnapshotEntry &entry = rig.snapshot_entries[index];
        if(entry.symbol_id == id && entry.offset + entry.size <=
                                      snapshot.value_bytes) {
            std::uint64_t bits = 0;
            if(entry.size == 0 || entry.size > sizeof(bits)) return -424242;
            std::memcpy(&bits, rig.snapshot_storage + entry.offset, entry.size);
            if(entry.size < sizeof(bits) &&
               (rig.snapshot_storage[entry.offset + entry.size - 1] & 0x80U) !=
                   0) {
                bits |= ~std::uint64_t{0} << (entry.size * 8U);
            }
            return static_cast<std::int64_t>(bits);
        }
    }
    return -424242;
}

// L7-A01/D01-D03: successful and faulted task boundaries publish one complete
// symbol/POU/SFC snapshot.  The reader either gets a complete version or the
// explicit bounded-retry result; it never waits on the writer.
void seqlock_snapshot_is_consistent_and_bounded()
{
    Rig rig;
    check(rig.build(debug_configuration(
              "X := X + 1; Y := -X; Z := X + Y;")),
          "L7-A01 snapshot fixture builds");
    const st::SymbolInfo x = rig.symbol("plant.r0.p0.main.x");
    const st::SymbolInfo y = rig.symbol("plant.r0.p0.main.y");
    check(rig.debug.add_watch(x.id) == st::DebugError::ok &&
              rig.debug.add_watch(y.id) == st::DebugError::ok,
          "L7-D01 snapshot watches bind by stable ID");

    std::atomic<bool> writer_ready{false};
    std::atomic<bool> writer_done{false};
    std::atomic<bool> writer_failed{false};
    std::thread writer([&]() {
        writer_ready.store(true, std::memory_order_release);
        for(std::uint64_t tick = 0; tick < 5000; ++tick) {
            if(!rig.tick(tick)) {
                writer_failed.store(true, std::memory_order_relaxed);
                break;
            }
        }
        writer_done.store(true, std::memory_order_release);
    });
    while(!writer_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::uint64_t last_version = 0;
    std::size_t complete = 0;
    std::size_t busy = 0;
    while(!writer_done.load(std::memory_order_acquire)) {
        st::DebugSnapshot snapshot{};
        const st::DebugError error = rig.snapshot(snapshot, 8);
        if(error == st::DebugError::snapshot_busy) {
            ++busy;
            continue;
        }
        if(error != st::DebugError::ok || snapshot.version < last_version ||
           snapshot.active_pou_count == 0 ||
           snapshot_i64(rig, snapshot, x.id) !=
               -snapshot_i64(rig, snapshot, y.id)) {
            fail("L7-A01 concurrent snapshot is complete and monotonic");
            break;
        }
        last_version = snapshot.version;
        ++complete;
    }
    writer.join();
    check(!writer_failed.load(std::memory_order_relaxed) && complete != 0,
          "L7-A01 concurrent writer and reader both make progress");
    check(busy <= 5000,
          "L7-D02 snapshot_busy is a bounded result, not reader blocking");

    st::DebugSnapshot no_attempt{};
    check(rig.snapshot(no_attempt, 0) == st::DebugError::snapshot_busy,
          "L7-D02 zero retries deterministically returns snapshot_busy");
}

void stable_symbol_ids_use_qualified_names()
{
    const std::string source =
        "PROGRAM Main VAR Value : DINT; END_VAR Value := Value + 1; "
        "END_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK T(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 1000); "
        "PROGRAM Left WITH T : Main; PROGRAM Right WITH T : Main; "
        "END_RESOURCE END_CONFIGURATION";
    Rig first;
    Rig second;
    check(first.build(source) && second.build(source),
          "L7-D03 duplicate-POU symbol fixture builds");
    const st::SymbolInfo left =
        first.symbol("plant.r0.left.main.value");
    const st::SymbolInfo right =
        first.symbol("plant.r0.right.main.value");
    const st::SymbolInfo repeated =
        second.symbol("PLANT.R0.LEFT.MAIN.VALUE");
    check(left.id != right.id && left.id == repeated.id,
          "L7-D03 stable ID includes instance-qualified path and ignores case");
    check(left.id == st::stable_symbol_id("plant.r0.left.main.value"),
          "L7-D03 public stable-ID function matches manifest ID");
    st::SymbolInfo ambiguous{};
    check(first.compiled.program.find_symbol("value", ambiguous) ==
              rt::ErrorCode::invalid_argument,
          "L7-D03 unqualified ambiguous lookup is not approximated");
}

// L7-A02/D04: source positions point to the first instruction of an IF,
// loop, called POU and SFC action.  Duplicate breakpoints share one bitmap bit.
void breakpoints_map_source_and_deduplicate()
{
    const std::string source =
        "FUNCTION Twice : DINT\nVAR_INPUT V : DINT; END_VAR\n"
        "Twice := V * 2;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR X : DINT; I : DINT; END_VAR\n"
        "IF X = 0 THEN X := Twice(2); END_IF;\n"
        "FOR I := 1 TO 2 DO X := X + I; END_FOR;\n"
        "SFC Flow\nINITIAL_STEP A: Work(P); END_STEP\n"
        "ACTION Work: X := X + 10; END_ACTION\nEND_SFC\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000);\nPROGRAM P0 WITH Main : Main;\n"
        "END_RESOURCE\nEND_CONFIGURATION";
    Rig rig;
    check(rig.build(source), "L7-A02 source-map fixture builds");

    st::BreakpointId first = 0;
    st::BreakpointId duplicate = 0;
    check(rig.debug.add_breakpoint("debug.st", 7, 1, first) ==
                  st::DebugError::ok &&
              rig.debug.add_breakpoint("debug.st", 7, 18, duplicate) ==
                  st::DebugError::ok &&
              first == duplicate && rig.debug.breakpoint_count() == 1,
          "L7-D04 same source statement deduplicates to one instruction");
    st::BreakpointId loop = 0;
    st::BreakpointId action = 0;
    check(rig.debug.add_breakpoint("debug.st", 8, 1, loop) ==
                  st::DebugError::ok &&
              rig.debug.add_breakpoint("debug.st", 11, 1, action) ==
                  st::DebugError::ok,
          "L7-A02 loop and SFC action source locations map to instructions");
    check(rig.tick(0), "L7-A02 breakpoint tick yields to host");
    st::DebugStop stop{};
    check(rig.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.reason == st::DebugStopReason::breakpoint &&
              stop.task == "main" && stop.pou == "main" &&
              stop.source_name == "debug.st" && stop.line == 7 &&
              stop.instruction ==
                  rig.compiled.program.source_map.first_instruction_at(
                      "debug.st", 7),
          "L7-D04 breakpoint stops before the mapped IF statement");
    check(rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.line == 8 && stop.pou == "main",
          "L7-A02 loop breakpoint maps to its first instruction");
    check(rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.line == 11 && stop.pou == "main" && stop.sfc == "flow",
          "L7-A02 SFC action breakpoint carries network source context");

    check(rig.debug.add_breakpoint("missing.st", 1, 1, first) ==
              st::DebugError::invalid_source_location,
          "L7 invalid source is rejected without approximate matching");
    check(rig.debug.add_breakpoint("debug.st", 9999, 1, first) ==
              st::DebugError::invalid_source_location,
          "L7 invalid line is rejected with stable diagnostic");
    check(rig.debug.add_breakpoint_instruction(0xFFFFFFFFU, first) ==
              st::DebugError::invalid_instruction,
          "L7 invalid instruction is rejected without approximation");
}

// L7-A03/D05: stepping is defined in source statements and call depth, not
// bytecode count.  Step-over skips a called POU; step-in and step-out expose it.
void step_in_over_out_follow_source_and_call_depth()
{
    const std::string source =
        "FUNCTION Inc : DINT\nVAR_INPUT V : DINT; END_VAR\n"
        "Inc := V + 1;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR X : DINT; Y : DINT; END_VAR\n"
        "X := Inc(X);\n"
        "Y := X + 10;\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000); PROGRAM P0 WITH Main : Main; "
        "END_RESOURCE END_CONFIGURATION";

    Rig step_in;
    check(step_in.build(source), "L7-A03 step-in fixture builds");
    st::BreakpointId breakpoint = 0;
    check(step_in.debug.add_breakpoint("debug.st", 7, 1, breakpoint) ==
                  st::DebugError::ok &&
              step_in.tick(0),
          "L7-A03 step-in reaches call site");
    st::DebugStop stop{};
    check(step_in.debug.control(st::DebugCommand::step_in) ==
                  st::DebugError::ok &&
              step_in.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              step_in.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.pou == "inc" && stop.line == 3 && stop.call_depth == 2,
          "L7-D05 step-in stops at first called-POU statement");
    check(step_in.debug.control(st::DebugCommand::step_out) ==
                  st::DebugError::ok &&
              step_in.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              step_in.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.pou == "main" && stop.line == 8 && stop.call_depth == 1,
          "L7-D05 step-out stops at caller's next source statement");

    Rig step_over;
    check(step_over.build(source), "L7-A03 step-over fixture builds");
    check(step_over.debug.add_breakpoint("debug.st", 7, 1, breakpoint) ==
                  st::DebugError::ok &&
              step_over.tick(0) &&
              step_over.debug.control(st::DebugCommand::step_over) ==
                  st::DebugError::ok &&
              step_over.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              step_over.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.pou == "main" && stop.line == 8 && stop.call_depth == 1,
          "L7-D05 step-over skips the called POU and stops after the call");
}

// L7-A04/D06: pausing one ST task neither commits its partial L3 transaction
// nor stalls another ST task or the caller's motion cadence.  A paused task
// retains its instruction budget and is exempt from wallclock fault injection.
void pause_is_isolated_and_discards_half_scan_output()
{
    const std::string source =
        "PROGRAM Paused\nVAR Q AT %QB0 : BYTE; X : DINT; END_VAR\n"
        "Q := BYTE#1;\nX := X + 1;\nQ := BYTE#2;\nEND_PROGRAM\n"
        "PROGRAM Healthy\nVAR N : DINT; END_VAR\nN := N + 1;\nEND_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK A(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000);\n"
        "TASK B(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 100000);\n"
        "PROGRAM PA WITH A : Paused;\nPROGRAM PB WITH B : Healthy;\n"
        "END_RESOURCE\nEND_CONFIGURATION";
    Rig rig;
    check(rig.build(source), "L7-A04 pause-isolation fixture builds");
    st::BreakpointId id = 0;
    check(rig.debug.add_breakpoint("debug.st", 5, 1, id) ==
                  st::DebugError::ok &&
              rig.tick(0),
          "L7-D06 target task pauses before final output assignment");
    const st::TaskStatus before = rig.task("a");
    std::uint64_t motion_ticks = 0;
    for(std::uint64_t tick = 1; tick <= 100; ++tick) {
        check(rig.tick(tick), "L7-A04 other task continues while target paused");
        ++motion_ticks;
    }
    const st::TaskStatus paused = rig.task("a");
    const st::TaskStatus healthy = rig.task("b");
    unsigned char output[8]{};
    std::size_t written = 0;
    std::uint64_t version = 99;
    check(rig.runtime.output_snapshot("r0", output, sizeof(output), written,
                                      version) == rt::ErrorCode::ok &&
              version == 0 && output[0] == 0,
          "L7-D06 paused half scan does not commit Q shadow");
    check(paused.state == st::TaskState::paused &&
              paused.fault == st::TaskFault::none &&
              paused.remaining_budget == before.remaining_budget &&
              healthy.release_count == 101 && motion_ticks == 100,
          "L7-A04 pause preserves budget and isolates task/motion progress");
    check(rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.runtime.output_snapshot("r0", output, sizeof(output),
                                          written, version) ==
                  rt::ErrorCode::ok &&
              version == 1 && output[0] == 2,
          "L7-D06 continue resumes at unexecuted instruction and commits once");
}

// L7-A06/D01/D07: a fault publishes a read-only snapshot and call stack.  A
// direct continue is forbidden; L5 reset/restart must be applied at a boundary
// before the debugger can release the stopped task.
void fault_snapshot_is_read_only_and_recovery_is_ordered()
{
    Rig rig;
    check(rig.build(debug_configuration(
              "X := 7; Y := X / Z;", "X : DINT; Y : DINT; Z : DINT;")),
          "L7-A06 fault fixture builds");
    const st::SymbolInfo x = rig.symbol("plant.r0.p0.main.x");
    check(rig.debug.add_watch(x.id) == st::DebugError::ok && rig.tick(0),
          "L7-A06 faulting task returns to host");
    st::DebugSnapshot snapshot{};
    check(rig.snapshot(snapshot) == st::DebugError::ok &&
              snapshot.task_state == st::TaskState::faulted &&
              snapshot.fault != st::TaskFault::none &&
              snapshot.call_depth != 0 &&
              snapshot_i64(rig, snapshot, x.id) == 7,
          "L7-D01 fault publishes readable values and call stack");
    const std::int64_t value = 9;
    check(rig.debug.write_symbol(x.id, st::builtin::dint, &value,
                                 sizeof(value)) ==
              st::DebugError::permission_denied,
          "L7 fault/monitor surface never permits ordinary variable writes");
    check(rig.debug.control(st::DebugCommand::continue_) ==
              st::DebugError::task_faulted,
          "L7-D07 faulted task cannot continue directly");
    check(rig.runtime.reset_task("r0", "main") == rt::ErrorCode::ok &&
              rig.runtime.boundary(1, nullptr, 0) == rt::ErrorCode::ok &&
              rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok,
          "L7-D07 L5 reset boundary permits continue and preserves state");

    check(rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.runtime.restart_task("r0", "main") == rt::ErrorCode::ok &&
              rig.runtime.boundary(2, nullptr, 0) == rt::ErrorCode::ok &&
              rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok,
          "L7-D07 L5 restart boundary also permits continue after reinit");
}

// L7-A05: DebugSession returns the exact L3 queue version/target release and
// delegates validation.  There is no second debugger-owned force mask.
void force_reuses_l3_queue_version_and_validation()
{
    Rig rig;
    check(rig.build(debug_configuration(
              "Q := I;", "I AT %IB0 : BYTE; Q AT %QB0 : BYTE;")),
          "L7-A05 force fixture builds");
    const st::SymbolInfo input = rig.symbol("plant.r0.p0.main.i");
    const st::SymbolInfo output = rig.symbol("plant.r0.p0.main.q");
    const unsigned char forced = 23;
    st::ForceReceipt receipt{};
    check(rig.debug.queue_force(input.id, st::builtin::byte_, &forced, 1,
                                receipt) == st::DebugError::ok &&
              receipt.target_release == 1 && receipt.queue_version ==
                  rig.runtime.force_queue_version("r0"),
          "L7-A05 force receipt exposes the shared L3 queue version");
    check(rig.debug.queue_force(input.id, st::builtin::word, &forced, 1,
                                receipt) == st::DebugError::type_mismatch,
          "L7 force rejects type mismatch through L3 validation");
    check(rig.debug.queue_force(st::SymbolId{}, st::builtin::byte_, &forced,
                                1, receipt) ==
              st::DebugError::invalid_symbol,
          "L7 force rejects unknown stable symbol ID");
    check(rig.tick(0), "L7-A05 queued force applies at scan boundary");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 0;
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              bytes[0] == forced,
          "L7-A05 shared L3 force affects task output");
    check(rig.debug.release_force(input.id, receipt) == st::DebugError::ok &&
              receipt.target_release == 2 && receipt.queue_version ==
                  rig.runtime.force_queue_version("r0"),
          "L7-A05 release is queued through the same L3 version source");

    st::BreakpointId id = 0;
    check(rig.debug.add_breakpoint("debug.st", 3, 1, id) ==
                  st::DebugError::ok &&
              rig.tick(1),
          "L7 force-pause fixture reaches paused state");
    check(rig.debug.queue_force(output.id, st::builtin::byte_, &forced, 1,
                                receipt) == st::DebugError::ok &&
              receipt.target_release == 3,
          "L7 force may queue while paused for the next resumed boundary");
}

// L7-A08/D08: trace storage is caller-owned and fixed.  Overflow discards the
// oldest record, keeps newest records in sequence, and counts every discard.
void trace_ring_drops_oldest_and_counts_overflow()
{
    st::DebugSessionOptions options;
    options.trace_capacity = 4;
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1;"),
                    st::DebugMode::enabled, options),
          "L7-A08 trace-ring fixture builds");
    for(std::uint64_t tick = 0; tick < 20; ++tick) {
        check(rig.tick(tick), "L7-A08 traced tick succeeds");
    }
    st::DebugTraceRecord records[4]{};
    st::DebugTraceReport report{};
    check(rig.debug.read_trace(records, 4, report) == st::DebugError::ok &&
              report.written == 4 && report.dropped != 0,
          "L7-D08 fixed trace ring reports dropped oldest records");
    for(std::size_t index = 1; index < report.written; ++index) {
        check(records[index].sequence == records[index - 1].sequence + 1,
              "L7-D08 retained trace tail is contiguous and ordered");
    }
    bool has_task = false;
    bool has_scan = false;
    bool has_pou = false;
    for(std::size_t index = 0; index < report.written; ++index) {
        has_task = has_task ||
                   records[index].kind == st::DebugEventKind::task;
        has_scan = has_scan ||
                   records[index].kind == st::DebugEventKind::scan;
        has_pou = has_pou ||
                  records[index].kind == st::DebugEventKind::pou;
    }
    check(has_task || has_scan || has_pou,
          "L7-D08 trace records typed task/scan/POU activity");
}

void trace_covers_scan_task_pou_sfc_fb_and_fault_events()
{
    const std::string source =
        "PROGRAM Main\nVAR Edge : R_TRIG; X : BOOL; Z : DINT; D : DINT; "
        "END_VAR\n"
        "Edge(CLK := X);\n"
        "SFC Flow\nINITIAL_STEP A: Work(P); END_STEP\n"
        "ACTION Work: D := 1 / Z; END_ACTION\nEND_SFC\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000); PROGRAM P0 WITH Main : Main; "
        "END_RESOURCE END_CONFIGURATION";
    Rig rig;
    check(rig.build(source), "L7-D08 all-event trace fixture builds");
    check(rig.tick(0), "L7-D08 faulting traced scan returns to host");
    st::DebugTraceRecord records[256]{};
    st::DebugTraceReport report{};
    check(rig.debug.read_trace(records, 256, report) == st::DebugError::ok,
          "L7-D08 all-event trace is readable");
    bool seen[6]{};
    for(std::size_t index = 0; index < report.written; ++index) {
        if(records[index].kind == st::DebugEventKind::scan) seen[0] = true;
        if(records[index].kind == st::DebugEventKind::task) seen[1] = true;
        if(records[index].kind == st::DebugEventKind::pou) seen[2] = true;
        if(records[index].kind == st::DebugEventKind::sfc) seen[3] = true;
        if(records[index].kind == st::DebugEventKind::fb) seen[4] = true;
        if(records[index].kind == st::DebugEventKind::fault) seen[5] = true;
    }
    check(seen[0] && seen[1] && seen[2] && seen[3] && seen[4] && seen[5],
          "L7-D08 trace covers scan task POU SFC FB and fault event classes");
}

// L7-A07: zero release overhead is an artifact property, not a brittle
// platform-disassembly assertion.  The compiler reports actual emitted probe
// opcodes and source maps; release bytecode contains neither.
void debug_and_release_artifacts_have_explicit_zero_cost_contract()
{
    const std::string source = debug_configuration(
        "IF X = 0 THEN X := 1; ELSE X := X + 1; END_IF;");
    st::CompileOptions debug_options;
    debug_options.source_name = "debug.st";
    debug_options.debug_mode = st::DebugMode::enabled;
    st::CompileOptions release_options = debug_options;
    release_options.debug_mode = st::DebugMode::disabled;
    const st::CompileResult debug = st::compile(source, debug_options);
    const st::CompileResult release = st::compile(source, release_options);
    check(debug.ok && release.ok,
          "L7-A07 debug and release artifacts both compile");
    check(debug.program.debug_report.breakpoint_probe_count > 0 &&
              debug.program.debug_report.source_map_entries > 0 &&
              debug.program.debug_report.probe_opcode == st::Op::debug_probe,
          "L7-A07 debug artifact reports explicit O(1) bitmap probes");
    check(release.program.debug_report.breakpoint_probe_count == 0 &&
              release.program.debug_report.source_map_entries == 0 &&
              release.program.debug_report.runtime_debug_branches == 0 &&
              release.program.code.size() < debug.program.code.size(),
          "L7-A07 release artifact emits zero probe branches and no map");

    Rig disabled;
    check(!disabled.build(source, st::DebugMode::disabled) &&
              disabled.debug.last_error() == st::DebugError::debugging_disabled,
          "L7 release artifact rejects debugger attachment explicitly");
}

void exact_breakpoint_watch_and_snapshot_capacities()
{
    st::DebugSessionOptions defaults;
    check(defaults.max_breakpoints == 4096 &&
              defaults.max_watch_symbols == 8192,
          "L7 capacity defaults match approved limits");
    st::DebugSessionOptions options;
    options.max_breakpoints = 4;
    options.max_watch_symbols = 4;
    options.trace_capacity = 8;
    const std::string source = debug_configuration(
        "X := X + 1;\nY := Y + 1;\nZ := Z + 1;\nX := X + Y;\nY := Y + Z;",
        "X : DINT; Y : DINT; Z : DINT; A : DINT; B : DINT;");
    Rig rig;
    check(rig.build(source, st::DebugMode::enabled, options),
          "L7-A08 capacity fixture builds");
    st::BreakpointId id = 0;
    for(std::uint32_t line = 3; line < 7; ++line) {
        check(rig.debug.add_breakpoint("debug.st", line, 1, id) ==
                  st::DebugError::ok,
              "L7 breakpoint capacity N is accepted");
    }
    check(rig.debug.add_breakpoint("debug.st", 7, 1, id) ==
              st::DebugError::capacity_exceeded,
          "L7 breakpoint capacity N+1 is rejected");

    const char *names[] = {
        "plant.r0.p0.main.x", "plant.r0.p0.main.y",
        "plant.r0.p0.main.z", "plant.r0.p0.main.a",
        "plant.r0.p0.main.b",
    };
    for(std::size_t index = 0; index < 4; ++index) {
        check(rig.debug.add_watch(rig.symbol(names[index]).id) ==
                  st::DebugError::ok,
              "L7 watch capacity N is accepted");
    }
    check(rig.debug.add_watch(rig.symbol(names[4]).id) ==
              st::DebugError::capacity_exceeded,
          "L7 watch capacity N+1 is rejected");

    st::DebugSnapshot snapshot{};
    check(rig.debug.read_snapshot(snapshot, rig.snapshot_entries, 3,
                                  rig.snapshot_storage,
                                  sizeof(rig.snapshot_storage), 8) ==
                  st::DebugError::capacity_exceeded &&
              snapshot.value_count == 0 && snapshot.value_bytes == 0,
          "L7 undersized snapshot entry buffer fails without partial output");
    check(rig.debug.read_snapshot(snapshot, rig.snapshot_entries, 8192,
                                  rig.snapshot_storage, 15, 8) ==
                  st::DebugError::capacity_exceeded &&
              snapshot.value_count == 0 && snapshot.value_bytes == 0,
          "L7 undersized snapshot byte buffer fails without partial output");
}

std::uint32_t next_random(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

// L7-A08: deterministic random control traffic checks the smallest useful
// controller oracle: breakpoint set cardinality/dedup, invalid targets,
// snapshot boundedness and monitor-write rejection.  It does not fuzz a UI.
void random_control_sequence_matches_minimal_oracle()
{
    st::DebugSessionOptions options;
    options.max_breakpoints = 4;
    options.max_watch_symbols = 4;
    Rig rig;
    check(rig.build(debug_configuration(
              "X := X + 1;\nY := -X;\nZ := X + Y;\nX := X + 1;"),
                    st::DebugMode::enabled, options),
          "L7-A08 random-control fixture builds");
    const st::SymbolInfo x = rig.symbol("plant.r0.p0.main.x");
    const std::int64_t value = 1;
    bool present[4]{};
    std::size_t expected_count = 0;
    std::uint32_t random = 0x71A5C0DEU;
    for(std::size_t iteration = 0; iteration < 100000; ++iteration) {
        const std::uint32_t sample = next_random(random);
        const std::size_t slot = (sample >> 3U) & 3U;
        st::BreakpointId id = 0;
        switch(sample & 3U) {
        case 0: {
            const st::DebugError error = rig.debug.add_breakpoint(
                "debug.st", static_cast<std::uint32_t>(3 + slot), 1, id);
            check(error == st::DebugError::ok,
                  "L7-A08 random valid breakpoint add succeeds");
            if(!present[slot]) {
                present[slot] = true;
                ++expected_count;
            }
            break;
        }
        case 1:
            if(present[slot]) {
                check(rig.debug.remove_breakpoint_at(
                          "debug.st", static_cast<std::uint32_t>(3 + slot), 1) ==
                          st::DebugError::ok,
                      "L7-A08 random present breakpoint removes");
                present[slot] = false;
                --expected_count;
            } else {
                check(rig.debug.remove_breakpoint_at(
                          "debug.st", static_cast<std::uint32_t>(3 + slot), 1) ==
                          st::DebugError::invalid_source_location,
                      "L7-A08 random absent breakpoint rejects stably");
            }
            break;
        case 2: {
            st::DebugSnapshot snapshot{};
            const st::DebugError error = rig.snapshot(snapshot, sample & 7U);
            check(error == st::DebugError::ok ||
                      error == st::DebugError::snapshot_busy,
                  "L7-A08 random bounded snapshot has finite result");
            break;
        }
        default:
            check(rig.debug.write_symbol(x.id, st::builtin::dint, &value,
                                         sizeof(value)) ==
                      st::DebugError::permission_denied,
                  "L7-A08 random monitor write remains forbidden");
            break;
        }
        check(rig.debug.breakpoint_count() == expected_count,
              "L7-A08 random breakpoint cardinality matches oracle");
    }
}

void debug_cycle_path_is_zero_allocation()
{
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1; Y := -X;")),
          "L7 RT fixture builds");
    const st::SymbolInfo x = rig.symbol("plant.r0.p0.main.x");
    check(rig.debug.add_watch(x.id) == st::DebugError::ok && rig.tick(0),
          "L7 RT fixture warms up");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    bool healthy = true;
    for(std::uint64_t tick = 1; tick <= 1000; ++tick) {
        st::DebugSnapshot snapshot{};
        if(!rig.tick(tick) ||
           rig.snapshot(snapshot, 8) != st::DebugError::ok) {
            healthy = false;
            break;
        }
    }
    g_freeze_allocations = false;
    check(healthy, "L7 debug scan/snapshot remains healthy when allocator frozen");
    check(g_frozen_allocations == 0,
          "L7 debug scan publication and snapshot perform zero allocations");
}

void lower_layer_runtime_regression()
{
    const char *sources[] = {
        "PROGRAM Main VAR X : DINT; END_VAR X := 2 + 3 * 4; END_PROGRAM",
        "TYPE Pair : STRUCT A : DINT; B : DINT; END_STRUCT END_TYPE "
        "PROGRAM Main VAR P : Pair := (A := 2, B := 5); X : DINT; END_VAR "
        "X := P.A + P.B; END_PROGRAM",
        "PROGRAM Main VAR Q AT %QB0 : BYTE; END_VAR Q := BYTE#7; "
        "END_PROGRAM",
    };
    for(const char *source : sources) {
        const st::CompileResult compiled = st::compile(source);
        check(compiled.ok, "L7 lower-layer source still compiles");
        if(!compiled.ok) continue;
        alignas(8) unsigned char storage[65536]{};
        st::Instance instance;
        check(instance.load(compiled.program, "main", storage,
                            sizeof(storage), kTickNs) == rt::ErrorCode::ok &&
                  instance.scan(kRunBudget) == st::ScanError::ok,
              "L7 lower-layer source still runs without debugger");
    }
}

} // namespace

int main()
{
    seqlock_snapshot_is_consistent_and_bounded();
    stable_symbol_ids_use_qualified_names();
    breakpoints_map_source_and_deduplicate();
    step_in_over_out_follow_source_and_call_depth();
    pause_is_isolated_and_discards_half_scan_output();
    fault_snapshot_is_read_only_and_recovery_is_ordered();
    force_reuses_l3_queue_version_and_validation();
    trace_ring_drops_oldest_and_counts_overflow();
    trace_covers_scan_task_pou_sfc_fb_and_fault_events();
    debug_and_release_artifacts_have_explicit_zero_cost_contract();
    exact_breakpoint_watch_and_snapshot_capacities();
    random_control_sequence_matches_minimal_oracle();
    debug_cycle_path_is_zero_allocation();
    lower_layer_runtime_regression();
    return failures == 0 ? 0 : 1;
}
