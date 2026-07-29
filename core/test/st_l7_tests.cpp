// L7 monitor/force/debugger acceptance (approved st-l7-semantics, 2026-07-17).
//
// This file is intentionally the RED contract.  It fixes the caller-owned,
// bounded core API needed by a host debugger without adding a socket, UI,
// authentication store or online bytecode replacement surface.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
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

std::string debug_configuration(const std::string &body,
                                const std::string &declarations =
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
    std::vector<std::uint64_t> runtime_storage =
        std::vector<std::uint64_t>(1048576 / 8);
    static constexpr std::size_t publish_word_capacity = 262144 / 8;
    static constexpr std::size_t trace_record_capacity = 256;
    static constexpr std::size_t trace_word_capacity =
        trace_record_capacity * sizeof(st::DebugTraceRecord) /
        sizeof(std::uint64_t);
    std::unique_ptr<std::atomic<std::uint64_t>[]> publish_storage{
        new std::atomic<std::uint64_t>[publish_word_capacity]};
    std::vector<unsigned char> snapshot_storage =
        std::vector<unsigned char>(262144);
    std::vector<st::DebugSnapshotEntry> publish_entries =
        std::vector<st::DebugSnapshotEntry>(8192);
    std::vector<st::DebugSnapshotEntry> snapshot_entries =
        std::vector<st::DebugSnapshotEntry>(8192);
    std::unique_ptr<std::atomic<std::uint64_t>[]> trace_storage{
        new std::atomic<std::uint64_t>[trace_word_capacity]};

    st::DebugError attach(
        st::DebugTarget target = {"r0", "main"},
        const st::DebugSessionOptions &session_options =
            st::DebugSessionOptions{})
    {
        return debug.attach(
            runtime, target, publish_storage.get(), publish_word_capacity,
            publish_entries.data(), publish_entries.size(),
            trace_storage.get(), trace_word_capacity, session_options);
    }

    bool build(const std::string &source,
               st::DebugMode mode = st::DebugMode::enabled,
               const st::DebugSessionOptions &session_options =
                   st::DebugSessionOptions{},
               st::DebugTarget target = {"r0", "main"})
    {
        st::CompileOptions options;
        options.source_name = "debug.st";
        options.debug_mode = mode;
        runtime.unload();
        compiled = st::compile(source, options);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        if(runtime.load(compiled.program, "plant",
                        reinterpret_cast<unsigned char *>(runtime_storage.data()),
                        runtime_storage.size() * sizeof(std::uint64_t),
                        kTickNs) != rt::ErrorCode::ok) {
            return false;
        }
        return attach(target, session_options) == st::DebugError::ok;
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
        return debug.read_snapshot(header, snapshot_entries.data(),
                                   snapshot_entries.size(),
                                   snapshot_storage.data(), snapshot_storage.size(),
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
            std::memcpy(&bits, rig.snapshot_storage.data() + entry.offset,
                        entry.size);
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

bool same_executable_code(const st::Program &left, const st::Program &right)
{
    if(left.code != right.code || left.programs.size() != right.programs.size() ||
       left.sfc_networks.size() != right.sfc_networks.size())
        return false;
    for(std::size_t network = 0; network < left.sfc_networks.size(); ++network) {
        const st::SfcNetworkInfo &a = left.sfc_networks[network];
        const st::SfcNetworkInfo &b = right.sfc_networks[network];
        if(a.transitions.size() != b.transitions.size() ||
           a.actions.size() != b.actions.size())
            return false;
        for(std::size_t index = 0; index < a.transitions.size(); ++index)
            if(a.transitions[index].condition.code !=
               b.transitions[index].condition.code)
                return false;
        for(std::size_t index = 0; index < a.actions.size(); ++index)
            if(a.actions[index].region.code != b.actions[index].region.code)
                return false;
    }
    for(std::size_t index = 0; index < left.programs.size(); ++index)
        if(!same_executable_code(left.programs[index], right.programs[index]))
            return false;
    return true;
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
           snapshot.active_pou_count != 0 ||
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
    check(busy + complete != 0 &&
              writer_done.load(std::memory_order_acquire),
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
    check(first.build(source, st::DebugMode::enabled,
                      st::DebugSessionOptions{}, {"r0", "t"}) &&
              second.build(source, st::DebugMode::enabled,
                           st::DebugSessionOptions{}, {"r0", "t"}),
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

void debug_value_types_and_source_lookup_are_exact()
{
    check(st::SymbolId{7} == st::SymbolId{7} &&
              st::SymbolId{7} != st::SymbolId{8} &&
              st::stable_symbol_id("PLANT.Main") ==
                  st::stable_symbol_id("plant.main"),
          "L7 debug symbol value semantics are stable");

    const st::InstructionId base{1, 2, 3, 4,
                                 st::DebugRegionKind::base};
    st::InstructionId changed = base;
    check(base == changed, "L7 instruction IDs compare equal field by field");
    changed.artifact = 9;
    check(base != changed, "L7 instruction ID includes artifact");
    changed = base;
    changed.mapping = 9;
    check(base != changed, "L7 instruction ID includes mapping");
    changed = base;
    changed.region = 9;
    check(base != changed, "L7 instruction ID includes region");
    changed = base;
    changed.offset = 9;
    check(base != changed, "L7 instruction ID includes offset");
    changed = base;
    changed.region_kind = st::DebugRegionKind::sfc_action;
    check(base != changed, "L7 instruction ID includes region kind");

    st::SourceMap map;
    st::SourceMapEntry entry;
    entry.source_name = "Main.ST";
    entry.line = 7;
    entry.instruction = 42;
    map.entries.push_back(entry);
    check(map.first_instruction_at(nullptr, 7) == UINT32_MAX &&
              map.first_instruction_at("Main.ST", 8) == UINT32_MAX &&
              map.first_instruction_at("Main", 7) == UINT32_MAX &&
              map.first_instruction_at("Main.SX", 7) == UINT32_MAX &&
              map.first_instruction_at("MAIN.ST", 7) == 42,
          "L7 source lookup is null-safe, exact and case-insensitive");
}

void attach_rejects_unknown_task_and_reattaches_cleanly()
{
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1;")),
          "L7 lifecycle fixture builds");
    check(rig.attach({"r0", "missing"}) == st::DebugError::invalid_argument &&
              rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::invalid_argument,
          "L7 unknown task is rejected without task-zero fallback");
    st::DebugSnapshot snapshot{};
    check(rig.snapshot(snapshot) == st::DebugError::invalid_argument,
          "L7 detached session rejects stale snapshot reads");
    st::BreakpointId breakpoint = 0;
    st::DebugStop stop{};
    check(rig.attach() == st::DebugError::ok &&
              rig.debug.add_breakpoint("debug.st", 3, 1, breakpoint) ==
                  st::DebugError::ok &&
              rig.tick(0) &&
              rig.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.task == "main",
          "L7 session reattaches with reset lifecycle state");
}

void detached_debug_api_boundary_matrix()
{
    st::DebugSession debug;
    st::BreakpointId breakpoint = 0;
    st::DebugStop stop{};
    st::DebugSnapshot snapshot{};
    st::DebugSnapshotEntry entry{};
    unsigned char bytes[8]{};
    st::ForceReceipt receipt{};
    st::DebugTraceRecord trace{};
    st::DebugTraceReport report{};
    check(debug.add_watch(st::SymbolId{1}) ==
                  st::DebugError::invalid_argument &&
              debug.add_breakpoint("debug.st", 1, 1, breakpoint) ==
                  st::DebugError::invalid_argument &&
              debug.add_breakpoint_instruction(0, breakpoint) ==
                  st::DebugError::invalid_argument &&
              debug.remove_breakpoint_at("debug.st", 1, 1) ==
                  st::DebugError::invalid_argument &&
              debug.breakpoint_count() == 0,
          "L7 detached breakpoint and watch APIs reject requests");
    check(debug.poll_stop(stop) == st::DebugError::invalid_argument &&
              debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::invalid_argument,
          "L7 detached stop-control APIs reject requests");
    check(debug.read_snapshot(snapshot, &entry, 1, bytes, sizeof(bytes), 1) ==
                  st::DebugError::invalid_argument &&
              debug.read_snapshot(snapshot, nullptr, 0, bytes,
                                  sizeof(bytes), 1) ==
                  st::DebugError::invalid_argument &&
              debug.read_snapshot(snapshot, &entry, 1, nullptr, 0, 1) ==
                  st::DebugError::invalid_argument,
          "L7 detached snapshot API validates session and storage");
    check(debug.write_symbol(st::SymbolId{1}, st::builtin::dint, bytes,
                             sizeof(bytes)) ==
                  st::DebugError::permission_denied &&
              debug.queue_force(st::SymbolId{1}, st::builtin::dint, bytes,
                                sizeof(bytes), receipt) ==
                  st::DebugError::invalid_argument &&
              debug.release_force(st::SymbolId{1}, receipt) ==
                  st::DebugError::invalid_argument,
          "L7 detached write and force APIs preserve their contracts");
    check(debug.read_trace(nullptr, 0, report) ==
                  st::DebugError::invalid_argument &&
              debug.read_trace(&trace, 1, report) ==
                  st::DebugError::invalid_argument,
          "L7 detached trace API rejects requests");
}

void snapshot_supports_symbols_larger_than_sixty_four_bytes()
{
    Rig rig;
    check(rig.build(debug_configuration("S := 'wide';", "S : STRING[100];")),
          "L7 large-symbol snapshot fixture builds");
    const st::SymbolInfo symbol = rig.symbol("plant.r0.p0.main.s");
    st::DebugSnapshot snapshot{};
    check(symbol.size > 64 &&
              rig.debug.add_watch(symbol.id) == st::DebugError::ok &&
              rig.tick(0) && rig.snapshot(snapshot) == st::DebugError::ok &&
              snapshot.value_count == 1 &&
              snapshot.value_bytes == symbol.size &&
              rig.snapshot_entries[0].size == symbol.size &&
              rig.snapshot_storage[0] == 4,
          "L7 snapshot publishes a complete symbol larger than 64 bytes");
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
        "SFC Flow\nINITIAL_STEP A: Work(P); END_STEP STEP B: TERMINAL; "
        "END_STEP TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
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
              rig.debug.add_breakpoint("debug.st", 7, 8, duplicate) ==
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

    Rig same_line;
    check(same_line.build(debug_configuration("X := 1; Y := 2;")),
          "L7 same-line column fixture builds");
    st::BreakpointId left = 0;
    st::BreakpointId right = 0;
    check(same_line.debug.add_breakpoint("debug.st", 3, 1, left) ==
                  st::DebugError::ok &&
              same_line.debug.add_breakpoint("debug.st", 3, 9, right) ==
                  st::DebugError::ok &&
              left != right,
          "L7 same-line sibling statements bind by source column");
    check(same_line.tick(0) &&
              same_line.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.column == 1,
          "L7 first same-line sibling stops at its own column");
    check(same_line.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              same_line.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              same_line.debug.poll_stop(stop) == st::DebugError::ok &&
              stop.column == 9,
          "L7 second same-line sibling stops at its own column");
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

void step_in_tracks_call_chains_deeper_than_two_frames()
{
    const std::string source =
        "FUNCTION Leaf : DINT\nVAR_INPUT V : DINT; END_VAR\n"
        "Leaf := V + 1;\nEND_FUNCTION\n"
        "FUNCTION Middle : DINT\nVAR_INPUT V : DINT; END_VAR\n"
        "Middle := Leaf(V);\nEND_FUNCTION\n"
        "FUNCTION Outer : DINT\nVAR_INPUT V : DINT; END_VAR\n"
        "Outer := Middle(V);\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR X : DINT; Y : DINT; END_VAR\n"
        "X := Outer(X);\nY := X;\nEND_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000); PROGRAM P0 WITH Main : Main; "
        "END_RESOURCE END_CONFIGURATION";
    Rig rig;
    check(rig.build(source), "L7 deep call-chain fixture builds");
    st::BreakpointId breakpoint = 0;
    st::DebugStop stop{};
    check(rig.debug.add_breakpoint("debug.st", 15, 1, breakpoint) ==
                  st::DebugError::ok &&
              rig.tick(0),
          "L7 deep call-chain reaches outer call site");
    const char *pous[] = {"outer", "middle", "leaf"};
    const std::uint32_t lines[] = {11, 7, 3};
    for(std::size_t depth = 0; depth < 3; ++depth) {
        check(rig.debug.control(st::DebugCommand::step_in) ==
                      st::DebugError::ok &&
                  rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
                  rig.debug.poll_stop(stop) == st::DebugError::ok &&
                  stop.pou == pous[depth] && stop.line == lines[depth] &&
                  stop.call_depth == depth + 2,
              "L7 step-in preserves a call chain deeper than two frames");
    }
    st::DebugSnapshot snapshot{};
    check(rig.snapshot(snapshot) == st::DebugError::ok &&
              snapshot.task_state == st::TaskState::paused &&
              snapshot.active_pou_count == 4 && snapshot.call_depth == 4 &&
              snapshot.call_stack[0].pou == "main" &&
              snapshot.call_stack[1].pou == "outer" &&
              snapshot.call_stack[2].pou == "middle" &&
              snapshot.call_stack[3].pou == "leaf" &&
              snapshot.call_stack[3].line == 3,
          "L7 paused snapshot publishes the real four-frame call path");
}

void typed_instruction_ids_distinguish_mappings_and_sfc_regions()
{
    const std::string mapped =
        "PROGRAM Main\nVAR X : DINT; END_VAR\nX := X + 1;\nEND_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK A(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100000); "
        "TASK B(INTERVAL := T#1ms, PRIORITY := 1, BUDGET := 100000); "
        "PROGRAM Left WITH A : Main; PROGRAM Right WITH B : Main; "
        "END_RESOURCE END_CONFIGURATION";
    Rig left;
    Rig right;
    check(left.build(mapped, st::DebugMode::enabled,
                     st::DebugSessionOptions{}, {"r0", "a"}) &&
              right.build(mapped, st::DebugMode::enabled,
                          st::DebugSessionOptions{}, {"r0", "b"}),
          "L7 typed-ID multi-mapping fixture builds");
    st::BreakpointId breakpoint = 0;
    st::DebugStop left_stop{};
    st::DebugStop right_stop{};
    check(left.debug.add_breakpoint("debug.st", 3, 1, breakpoint) ==
                  st::DebugError::ok &&
              right.debug.add_breakpoint("debug.st", 3, 1, breakpoint) ==
                  st::DebugError::ok &&
              left.tick(0) && right.tick(0) &&
              left.debug.poll_stop(left_stop) == st::DebugError::ok &&
              right.debug.poll_stop(right_stop) == st::DebugError::ok &&
              left_stop.instruction_id.offset ==
                  right_stop.instruction_id.offset &&
              left_stop.instruction_id.artifact ==
                  right_stop.instruction_id.artifact &&
              left_stop.instruction_id.mapping !=
                  right_stop.instruction_id.mapping,
          "L7 typed ID distinguishes mappings with the same bytecode offset");

    const std::string sfc =
        "PROGRAM Main\nVAR X : DINT; END_VAR\n"
        "SFC Flow\n"
        "INITIAL_STEP A: First(P); Second(P); END_STEP "
        "STEP B: TERMINAL; END_STEP "
        "TRANSITION FROM A TO B := X > 0; END_TRANSITION\n"
        "ACTION First: X := X + 1; END_ACTION\n"
        "ACTION Second: X := X + 2; END_ACTION\n"
        "END_SFC\nEND_PROGRAM";
    st::CompileOptions options;
    options.source_name = "debug.st";
    options.debug_mode = st::DebugMode::enabled;
    const st::CompileResult compiled = st::compile(sfc, options);
    check(compiled.ok && !compiled.program.sfc_networks.empty(),
          "L7 typed-ID SFC-region fixture builds");
    if(compiled.ok && !compiled.program.sfc_networks.empty()) {
        const st::SfcNetworkInfo &network =
            compiled.program.sfc_networks.front();
        check(network.actions.size() == 2 &&
                  !network.actions[0].region.source_map.entries.empty() &&
                  !network.actions[1].region.source_map.entries.empty() &&
                  network.actions[0].region.source_map.entries[0]
                          .instruction_id.offset ==
                      network.actions[1].region.source_map.entries[0]
                          .instruction_id.offset &&
                  network.actions[0].region.source_map.entries[0]
                          .instruction_id !=
                      network.actions[1].region.source_map.entries[0]
                          .instruction_id,
              "L7 typed ID distinguishes SFC regions with offset zero");
    }
}

void one_task_debugs_all_of_its_mappings()
{
    const std::string source =
        "PROGRAM Main\nVAR X : DINT; END_VAR\nX := X + 1;\nEND_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100000); "
        "PROGRAM Left WITH T : Main; PROGRAM Right WITH T : Main; "
        "END_RESOURCE END_CONFIGURATION";
    Rig rig;
    check(rig.build(source, st::DebugMode::enabled,
                    st::DebugSessionOptions{}, {"r0", "t"}),
          "L7 multi-mapping task fixture builds");
    const st::SymbolInfo left =
        rig.symbol("plant.r0.left.main.x");
    const st::SymbolInfo right =
        rig.symbol("plant.r0.right.main.x");
    st::BreakpointId breakpoint = 0;
    st::BreakpointId raw = 0;
    check(rig.debug.add_watch(left.id) == st::DebugError::ok &&
              rig.debug.add_watch(right.id) == st::DebugError::ok &&
              rig.debug.add_breakpoint("debug.st", 3, 1, breakpoint) ==
                  st::DebugError::ok &&
              rig.debug.breakpoint_count() == 2 &&
              rig.debug.add_breakpoint_instruction(
                  rig.compiled.program.programs.front()
                      .source_map.entries.front().instruction,
                  raw) == st::DebugError::invalid_instruction,
          "L7 source breakpoint and watches cover every task mapping");
    st::DebugStop first{};
    st::DebugStop second{};
    check(rig.tick(0) &&
              rig.debug.poll_stop(first) == st::DebugError::ok &&
              rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.debug.poll_stop(second) == st::DebugError::ok &&
              first.instruction_id.mapping != second.instruction_id.mapping,
          "L7 one source breakpoint stops in both task mappings");
    st::DebugSnapshot snapshot{};
    check(rig.debug.remove_breakpoint_at("debug.st", 3, 1) ==
                  st::DebugError::ok &&
              rig.debug.breakpoint_count() == 0 &&
              rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.snapshot(snapshot) == st::DebugError::ok &&
              snapshot_i64(rig, snapshot, left.id) == 1 &&
              snapshot_i64(rig, snapshot, right.id) == 1,
          "L7 snapshot reads each watch from its owning mapping");
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
    check(rig.build(source, st::DebugMode::enabled,
                    st::DebugSessionOptions{}, {"r0", "a"}),
          "L7-A04 pause-isolation fixture builds");
    st::BreakpointId id = 0;
    check(rig.debug.add_breakpoint("debug.st", 5, 1, id) ==
                  st::DebugError::ok &&
              rig.tick(0),
          "L7-D06 target task pauses before final output assignment");
    const st::TaskStatus before = rig.task("a");
    std::uint64_t motion_ticks = 0;
    for(std::uint64_t tick = 1; tick <= 100; ++tick) {
        if(tick == 50)
            check(rig.runtime.report_wallclock_exceeded("r0", "a") ==
                      rt::ErrorCode::ok,
                  "L7 paused task accepts host wallclock report");
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
              output[0] == 0,
          "L7-D06 paused half scan does not commit Q shadow");
    check(paused.state == st::TaskState::paused &&
              paused.fault == st::TaskFault::none &&
              paused.remaining_budget == before.remaining_budget &&
              paused.missed_release_count == 0 &&
              healthy.release_count == 101 && motion_ticks == 100,
          "L7-A04 pause preserves budget and ignores releases/wallclock faults");
    check(rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.runtime.output_snapshot("r0", output, sizeof(output),
                                          written, version) ==
                  rt::ErrorCode::ok &&
              output[0] == 2,
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
              receipt.target_release == 2,
          "L7 paused force targets the parked release being resumed");
}

void force_uses_compiled_physical_location_not_local_name()
{
    const std::string source =
        "PROGRAM First VAR Q AT %QB0 : BYTE; END_VAR "
        "Q := BYTE#1; END_PROGRAM\n"
        "PROGRAM Second VAR Q AT %QB1 : BYTE; END_VAR "
        "Q := BYTE#2; END_PROGRAM\n"
        "CONFIGURATION Plant RESOURCE R0 ON PLC "
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100000); "
        "PROGRAM Left WITH T : First; PROGRAM Right WITH T : Second; "
        "END_RESOURCE END_CONFIGURATION";
    Rig rig;
    check(rig.build(source, st::DebugMode::enabled,
                    st::DebugSessionOptions{}, {"r0", "t"}),
          "L7 physical-force duplicate-name fixture builds");
    const st::SymbolInfo right =
        rig.symbol("plant.r0.right.second.q");
    const unsigned char forced = 19;
    st::ForceReceipt receipt{};
    check(right.located && right.physical_byte_offset == 1 &&
              rig.debug.queue_force(right.id, st::builtin::byte_, &forced, 1,
                                    receipt) == st::DebugError::ok &&
              receipt.target_release == 1 && rig.tick(0),
          "L7 force resolves the symbol's compiled physical handle");
    unsigned char output[8]{};
    std::size_t written = 0;
    std::uint64_t version = 0;
    check(rig.runtime.output_snapshot("r0", output, sizeof(output), written,
                                      version) == rt::ErrorCode::ok &&
              output[0] == 1 && output[1] == forced,
          "L7 duplicate local names do not redirect force to first match");
}

// L7-A05: a command addressed to a paused task stays pending while another
// task crosses scan boundaries over the same physical variable.  The command
// is consumed only when the addressed task resumes its parked release.
void targeted_force_waits_for_paused_task()
{
    const std::string source =
        "VAR_GLOBAL M AT %MB0 : BYTE; END_VAR\n"
        "PROGRAM Paused\nVAR_EXTERNAL M : BYTE; END_VAR\n"
        "VAR X : DINT; END_VAR\n"
        "X := X + 1;\nM := BYTE#5;\nEND_PROGRAM\n"
        "PROGRAM Healthy\nVAR_EXTERNAL M : BYTE; END_VAR\n"
        "VAR Q AT %QB0 : BYTE; END_VAR\n"
        "Q := M;\nEND_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK A(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000);\n"
        "TASK B(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 1, "
        "BUDGET := 100000);\n"
        "PROGRAM PA WITH A : Paused;\nPROGRAM PB WITH B : Healthy;\n"
        "END_RESOURCE\nEND_CONFIGURATION";
    Rig rig;
    check(rig.build(source, st::DebugMode::enabled,
                    st::DebugSessionOptions{}, {"r0", "a"}),
          "L7 targeted-force fixture builds");
    st::BreakpointId breakpoint = 0;
    check(rig.debug.add_breakpoint("debug.st", 6, 1, breakpoint) ==
                  st::DebugError::ok &&
              rig.tick(0) &&
              rig.task("a").state == st::TaskState::paused,
          "L7 targeted-force owner pauses before physical store");

    const st::SymbolInfo memory = rig.symbol("plant.r0.pa.paused.m");
    const unsigned char forced = 23;
    st::ForceReceipt receipt{};
    check(rig.debug.queue_force(memory.id, st::builtin::byte_, &forced, 1,
                                receipt) == st::DebugError::ok &&
              receipt.target_release == 1,
          "L7 targeted force records the paused task resume release");

    for(std::uint64_t tick = 1; tick <= 3; ++tick)
        check(rig.tick(tick),
              "L7 non-owner task crosses targeted-force boundary");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 0;
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              bytes[0] == 0,
          "L7 non-owner task cannot consume paused task force command");

    check(rig.debug.control(st::DebugCommand::continue_) ==
                  st::DebugError::ok &&
              rig.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
              rig.runtime.memory_snapshot("r0", bytes, sizeof(bytes), written,
                                          version) == rt::ErrorCode::ok &&
              bytes[0] == forced,
          "L7 paused owner consumes targeted force on resume boundary");
}

void concurrent_force_receipts_keep_each_operation_version()
{
    Rig rig;
    check(rig.build(debug_configuration(
              "Q := I;", "I AT %IB0 : BYTE; Q AT %QB0 : BYTE;")),
          "L7 concurrent-force receipt fixture builds");
    const st::SymbolInfo input = rig.symbol("plant.r0.p0.main.i");
    const st::SymbolInfo output = rig.symbol("plant.r0.p0.main.q");
    const unsigned char first = 11;
    const unsigned char second = 29;
    st::ForceReceipt receipts[2]{};
    st::DebugError errors[2]{};
    std::atomic<bool> start{false};
    std::thread left([&]() {
        while(!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        errors[0] = rig.debug.queue_force(input.id, st::builtin::byte_,
                                          &first, 1, receipts[0]);
    });
    std::thread right([&]() {
        while(!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        errors[1] = rig.debug.queue_force(output.id, st::builtin::byte_,
                                           &second, 1, receipts[1]);
    });
    start.store(true, std::memory_order_release);
    left.join();
    right.join();
    const std::uint64_t low =
        std::min(receipts[0].queue_version, receipts[1].queue_version);
    const std::uint64_t high =
        std::max(receipts[0].queue_version, receipts[1].queue_version);
    check(errors[0] == st::DebugError::ok &&
              errors[1] == st::DebugError::ok && low == 1 && high == 2 &&
              rig.runtime.force_queue_version("r0") == high,
          "L7 concurrent force receipts retain distinct exact queue versions");
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
              report.version != 0 && report.written == 4 &&
              report.dropped != 0,
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
        "SFC Flow\nINITIAL_STEP A: Work(P); END_STEP STEP B: TERMINAL; "
        "END_STEP TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
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
    bool identities = true;
    for(std::size_t index = 0; index < report.written; ++index) {
        identities = identities && records[index].resource_id != 0 &&
                     records[index].task_id != 0 &&
                     records[index].release == 1;
        if(records[index].kind == st::DebugEventKind::scan) seen[0] = true;
        if(records[index].kind == st::DebugEventKind::task) seen[1] = true;
        if(records[index].kind == st::DebugEventKind::pou) seen[2] = true;
        if(records[index].kind == st::DebugEventKind::sfc) seen[3] = true;
        if(records[index].kind == st::DebugEventKind::fb) seen[4] = true;
        if(records[index].kind == st::DebugEventKind::fault) seen[5] = true;
    }
    check(seen[0] && seen[1] && seen[2] && seen[3] && seen[4] && seen[5],
          "L7-D08 trace covers scan task POU SFC FB and fault event classes");
    check(identities,
          "L7-D08 every trace record carries resource task and release identity");
    bool pou_identity = false;
    bool sfc_identity = false;
    for(std::size_t index = 0; index < report.written; ++index) {
        if(records[index].kind == st::DebugEventKind::pou)
            pou_identity = pou_identity || records[index].pou_id != 0;
        if(records[index].kind == st::DebugEventKind::sfc)
            sfc_identity = sfc_identity ||
                           (records[index].pou_id != 0 &&
                            records[index].sfc_id != 0);
    }
    check(pou_identity && sfc_identity,
          "L7-D08 POU and SFC trace payloads carry artifact identities");
}

void trace_reader_is_consistent_during_concurrent_publication()
{
    st::DebugSessionOptions options;
    options.trace_capacity = 64;
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1; Y := X + 1;"),
                    st::DebugMode::enabled, options),
          "L7 concurrent trace fixture builds");
    std::atomic<bool> done{false};
    std::atomic<bool> healthy{true};
    std::uint64_t prior_version = 0;
    std::uint64_t prior_dropped = 0;
    std::thread writer([&]() {
        for(std::uint64_t tick = 0; tick < 10000; ++tick) {
            if(!rig.tick(tick)) {
                healthy.store(false, std::memory_order_relaxed);
                break;
            }
        }
        done.store(true, std::memory_order_release);
    });
    while(!done.load(std::memory_order_acquire)) {
        st::DebugTraceRecord records[64]{};
        st::DebugTraceReport report{};
        const st::DebugError error =
            rig.debug.read_trace(records, 64, report);
        if(error != st::DebugError::ok &&
           error != st::DebugError::snapshot_busy) {
            healthy.store(false, std::memory_order_relaxed);
            break;
        }
        if(error != st::DebugError::ok) continue;
        if(report.version < prior_version || report.dropped < prior_dropped) {
            healthy.store(false, std::memory_order_relaxed);
            break;
        }
        prior_version = report.version;
        prior_dropped = report.dropped;
        for(std::size_t index = 0; index < report.written; ++index) {
            if(records[index].sequence == 0 ||
               static_cast<unsigned>(records[index].kind) >
                   static_cast<unsigned>(st::DebugEventKind::fault) ||
               (index != 0 && records[index].sequence !=
                                  records[index - 1].sequence + 1U)) {
                healthy.store(false, std::memory_order_relaxed);
                break;
            }
        }
    }
    writer.join();
    check(healthy.load(std::memory_order_relaxed),
          "L7 concurrent trace reads are complete or bounded-busy, never torn");
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

    const std::string sfc_source =
        "PROGRAM Main\nVAR X : DINT; END_VAR\nX := X + 1;\n"
        "SFC Flow\nINITIAL_STEP A: Work(P); END_STEP "
        "STEP B: TERMINAL; END_STEP "
        "TRANSITION FROM A TO B := X > 100; END_TRANSITION\n"
        "ACTION Work: X := X + 2; END_ACTION\nEND_SFC\n"
        "END_PROGRAM";
    const st::CompileResult sfc_debug = st::compile(sfc_source, debug_options);
    const st::CompileResult sfc_release =
        st::compile(sfc_source, release_options);
    const st::CompileResult sfc_default = st::compile(sfc_source);
    check(sfc_debug.ok && sfc_release.ok && sfc_default.ok,
          "L7-A07 SFC debug/release/default artifacts compile");
    check(!sfc_release.program.contains_opcode(st::Op::debug_probe) &&
              !sfc_default.program.contains_opcode(st::Op::debug_probe),
          "L7-A07 decoded release base and every SFC region contain no probe opcode");
    check(same_executable_code(sfc_release.program, sfc_default.program),
          "L7-A07 default compile is byte-identical to explicit disabled mode");
    check(sfc_release.program.canonical_manifest() ==
              sfc_default.program.canonical_manifest(),
          "L7-A07 serialized release artifact is identical to default mode");

    if(sfc_debug.ok && sfc_release.ok) {
        const st::Program &debug_program = sfc_debug.program.programs.empty()
            ? sfc_debug.program
            : sfc_debug.program.programs.front();
        const st::Program &release_program = sfc_release.program.programs.empty()
            ? sfc_release.program
            : sfc_release.program.programs.front();
        std::vector<std::uint64_t> debug_storage(
            (debug_program.required_bytes() + 7U) / 8U);
        std::vector<std::uint64_t> release_storage(
            (release_program.required_bytes() + 7U) / 8U);
        st::Instance debug_instance;
        st::Instance release_instance;
        const bool loaded =
            debug_instance.load(
                debug_program,
                reinterpret_cast<unsigned char *>(debug_storage.data()),
                debug_storage.size() * sizeof(std::uint64_t), kTickNs) ==
                rt::ErrorCode::ok &&
            release_instance.load(
                release_program,
                reinterpret_cast<unsigned char *>(release_storage.data()),
                release_storage.size() * sizeof(std::uint64_t), kTickNs) ==
                rt::ErrorCode::ok;
        check(loaded, "L7-A07 parity instances load");
        std::int32_t debug_x = 0;
        std::int32_t release_x = 0;
        std::uint32_t debug_offset = 0;
        std::uint32_t release_offset = 0;
        for(const st::VarInfo &var : debug_program.vars)
            if(var.lower == "x") debug_offset = var.offset;
        for(const st::VarInfo &var : release_program.vars)
            if(var.lower == "x") release_offset = var.offset;
        check(loaded && debug_instance.scan(100000) == st::ScanError::ok &&
                  release_instance.scan(100000) == st::ScanError::ok &&
                  debug_instance.read_variable(
                      debug_offset, sizeof(debug_x),
                      reinterpret_cast<unsigned char *>(&debug_x)) &&
                  release_instance.read_variable(
                      release_offset, sizeof(release_x),
                      reinterpret_cast<unsigned char *>(&release_x)) &&
                  debug_x == release_x &&
                  debug_instance.remaining_budget() ==
                      release_instance.remaining_budget(),
              "L7-A07 debug/release SFC output and budget are identical");
    }

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
    check(rig.debug.read_snapshot(snapshot, rig.snapshot_entries.data(), 3,
                                  rig.snapshot_storage.data(),
                                  rig.snapshot_storage.size(), 8) ==
                  st::DebugError::capacity_exceeded &&
              snapshot.value_count == 0 && snapshot.value_bytes == 0,
          "L7 undersized snapshot entry buffer fails without partial output");
    check(rig.debug.read_snapshot(snapshot, rig.snapshot_entries.data(), 8192,
                                  rig.snapshot_storage.data(), 15, 8) ==
                  st::DebugError::capacity_exceeded &&
              snapshot.value_count == 0 && snapshot.value_bytes == 0,
          "L7 undersized snapshot byte buffer fails without partial output");
}

void typed_atomic_storage_has_exact_attach_contract()
{
    static_assert(alignof(std::atomic<std::uint64_t>) >=
                  alignof(std::uint64_t));
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1;")),
          "L7 typed-storage fixture builds");
    st::DebugSnapshotEntry entries[4]{};
    const std::size_t probes =
        rig.compiled.program.programs.empty()
            ? rig.compiled.program.source_map.entries.size()
            : rig.compiled.program.programs.front().source_map.entries.size();
    const std::size_t exact_words = 6U + (probes + 63U) / 64U;
    std::unique_ptr<std::atomic<std::uint64_t>[]> exact{
        new std::atomic<std::uint64_t>[exact_words]};
    std::unique_ptr<std::atomic<std::uint64_t>[]> short_publish{
        new std::atomic<std::uint64_t>[exact_words - 1U]};
    constexpr std::size_t trace_words =
        sizeof(st::DebugTraceRecord) / sizeof(std::uint64_t);
    std::unique_ptr<std::atomic<std::uint64_t>[]> trace{
        new std::atomic<std::uint64_t>[trace_words]};
    check(rig.debug.attach(rig.runtime, {"r0", "main"}, nullptr, 0,
                           entries, std::size(entries), nullptr, 0) ==
                  st::DebugError::invalid_argument,
          "L7 null typed publish storage is rejected");
    check(rig.debug.attach(rig.runtime, {"r0", "main"}, exact.get(),
                           exact_words, entries, std::size(entries),
                           trace.get(), trace_words - 1U) ==
                  st::DebugError::capacity_exceeded,
          "L7 partial trace-record storage is rejected");
    check(rig.debug.attach(rig.runtime, {"r0", "main"},
                           short_publish.get(), exact_words - 1U, entries,
                           std::size(entries), nullptr, 0) ==
                  st::DebugError::capacity_exceeded,
          "L7 publish storage N-1 is rejected");
    check(rig.debug.attach(rig.runtime, {"r0", "main"}, exact.get(),
                           exact_words, entries, std::size(entries),
                           trace.get(), trace_words) == st::DebugError::ok,
          "L7 publish and trace storage exact N is accepted");
}

void watch_plan_freezes_at_first_task_publication()
{
    Rig rig;
    check(rig.build(debug_configuration("X := X + 1; Y := Y + 1;")),
          "L7 watch-freeze fixture builds");
    const st::SymbolInfo x = rig.symbol("plant.r0.p0.main.x");
    const st::SymbolInfo y = rig.symbol("plant.r0.p0.main.y");
    check(rig.debug.add_watch(x.id) == st::DebugError::ok && rig.tick(0),
          "L7 watch plan accepts symbols before first task publication");
    check(rig.debug.add_watch(y.id) == st::DebugError::watch_plan_frozen &&
              rig.debug.add_watch(y.id) ==
                  st::DebugError::watch_plan_frozen,
          "L7 watch plan rejects later additions with a stable error");
    st::DebugSnapshot snapshot{};
    check(rig.snapshot(snapshot) == st::DebugError::ok &&
              snapshot.value_count == 1,
          "L7 frozen snapshot layout remains unchanged");
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

void debugger_control_and_fault_paths_are_zero_allocation()
{
    st::DebugSessionOptions options;
    options.trace_capacity = 4;
    Rig controlled;
    check(controlled.build(debug_configuration(
              "Q := BYTE#1;\nX := X + 1;",
              "Q AT %QB0 : BYTE; X : DINT;"),
              st::DebugMode::enabled, options),
          "L7 zero-allocation control fixture builds");
    const st::SymbolInfo output =
        controlled.symbol("plant.r0.p0.main.q");
    st::BreakpointId breakpoint = 0;
    check(controlled.debug.add_breakpoint("debug.st", 3, 1, breakpoint) ==
              st::DebugError::ok,
          "L7 zero-allocation control breakpoint binds");

    Rig faulted;
    check(faulted.build(debug_configuration(
              "X := 1; Y := X / Z;", "X : DINT; Y : DINT; Z : DINT;")),
          "L7 zero-allocation fault fixture builds");

    const unsigned char forced = 17;
    st::ForceReceipt receipt{};
    st::DebugTraceRecord trace[4]{};
    st::DebugTraceReport trace_report{};
    st::DebugSnapshot snapshot{};
    bool healthy = true;
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    healthy = healthy && controlled.tick(0);
    healthy = healthy &&
        controlled.debug.control(st::DebugCommand::step_over) ==
            st::DebugError::ok &&
        controlled.runtime.run(kRunBudget) == rt::ErrorCode::ok &&
        controlled.debug.control(st::DebugCommand::continue_) ==
            st::DebugError::ok &&
        controlled.runtime.run(kRunBudget) == rt::ErrorCode::ok;
    healthy = healthy &&
        controlled.debug.queue_force(output.id, st::builtin::byte_, &forced,
                                      1, receipt) == st::DebugError::ok &&
        controlled.tick(1) &&
        controlled.debug.read_trace(trace, 4, trace_report) ==
            st::DebugError::ok &&
        trace_report.dropped != 0;
    healthy = healthy && faulted.tick(0) &&
        faulted.snapshot(snapshot) == st::DebugError::ok &&
        faulted.debug.read_trace(trace, 4, trace_report) == st::DebugError::ok;
    g_freeze_allocations = false;
    check(healthy,
          "L7 hit/pause/resume/step/fault/trace/force paths stay healthy");
    check(g_frozen_allocations == 0,
          "L7 hit/pause/resume/step/fault/trace/force paths allocate zero");
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
    debug_value_types_and_source_lookup_are_exact();
    attach_rejects_unknown_task_and_reattaches_cleanly();
    detached_debug_api_boundary_matrix();
    snapshot_supports_symbols_larger_than_sixty_four_bytes();
    breakpoints_map_source_and_deduplicate();
    step_in_over_out_follow_source_and_call_depth();
    step_in_tracks_call_chains_deeper_than_two_frames();
    typed_instruction_ids_distinguish_mappings_and_sfc_regions();
    one_task_debugs_all_of_its_mappings();
    pause_is_isolated_and_discards_half_scan_output();
    fault_snapshot_is_read_only_and_recovery_is_ordered();
    force_reuses_l3_queue_version_and_validation();
    force_uses_compiled_physical_location_not_local_name();
    targeted_force_waits_for_paused_task();
    concurrent_force_receipts_keep_each_operation_version();
    trace_ring_drops_oldest_and_counts_overflow();
    trace_covers_scan_task_pou_sfc_fb_and_fault_events();
    trace_reader_is_consistent_during_concurrent_publication();
    debug_and_release_artifacts_have_explicit_zero_cost_contract();
    exact_breakpoint_watch_and_snapshot_capacities();
    typed_atomic_storage_has_exact_attach_contract();
    watch_plan_freezes_at_first_task_publication();
    random_control_sequence_matches_minimal_oracle();
    debug_cycle_path_is_zero_allocation();
    debugger_control_and_fault_paths_are_zero_allocation();
    lower_layer_runtime_regression();
    return failures == 0 ? 0 : 1;
}
