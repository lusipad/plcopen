// L5 task-runtime acceptance (approved st-l5-semantics sections 0-5).
//
// This is intentionally a RED contract.  It requires a caller-driven,
// allocation-free ConfigurationRuntime whose boundary sampling is separate
// from cooperative execution.  The tests do not emulate scheduling in the
// fixture and do not give the VM access to a clock.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "adapters/servo.h"
#include "rt/spsc_queue.h"
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
    std::vector<std::uint64_t> storage =
        std::vector<std::uint64_t>(1048576U / sizeof(std::uint64_t));

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
        return runtime.load(
                   compiled.program, "plant",
                   reinterpret_cast<unsigned char *>(storage.data()),
                   storage.size() * sizeof(std::uint64_t), kTickNs) ==
               rt::ErrorCode::ok;
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

void priority_and_declaration_order_have_full_oracle()
{
    const char *globals = "VAR_GLOBAL Trace AT %MD0 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM One VAR_EXTERNAL Trace : DWORD; END_VAR "
        "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + 1); "
        "END_PROGRAM\n"
        "PROGRAM Two VAR_EXTERNAL Trace : DWORD; END_VAR "
        "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + 2); "
        "END_PROGRAM\n"
        "PROGRAM Three VAR_EXTERNAL Trace : DWORD; END_VAR "
        "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + 3); "
        "END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK B(INTERVAL := T#1ms, PRIORITY := 2, BUDGET := 100);\n"
        "TASK A(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "TASK C(INTERVAL := T#1ms, PRIORITY := 2, BUDGET := 100);\n"
        "PROGRAM P2 WITH B : Two;\nPROGRAM P1 WITH A : One;\n"
        "PROGRAM P3 WITH C : Three;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)) &&
              rig.boundary(0) && rig.drain() && rig.memory("r0") == 123,
          "L5-A02 full oracle is priority then declaration order");

    std::uint32_t random = 0x5a17c3e1U;
    auto next_random = [&] {
        random = random * 1664525U + 1013904223U;
        return random;
    };
    for(int round = 0; round < 24; ++round) {
        int declaration[5] = {0, 1, 2, 3, 4};
        int priority[5]{};
        for(int index = 4; index > 0; --index) {
            const int other = static_cast<int>(next_random() %
                                               static_cast<unsigned>(index + 1));
            std::swap(declaration[index], declaration[other]);
        }
        for(int index = 0; index < 5; ++index)
            priority[index] = static_cast<int>(next_random() % 3U);

        std::string random_pous;
        for(int index = 0; index < 5; ++index) {
            random_pous += "PROGRAM P" + std::to_string(index) +
                " VAR_EXTERNAL Trace : DWORD; END_VAR "
                "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + " +
                std::to_string(index + 1) + "); END_PROGRAM\n";
        }
        std::string random_config = "RESOURCE R0 ON PLC\n";
        for(int at = 0; at < 5; ++at) {
            const int index = declaration[at];
            random_config += "TASK T" + std::to_string(index) +
                "(INTERVAL := T#1ms, PRIORITY := " +
                std::to_string(priority[index]) +
                ", BUDGET := 100);\n";
        }
        for(int index = 4; index >= 0; --index) {
            random_config += "PROGRAM I" + std::to_string(index) +
                " WITH T" + std::to_string(index) + " : P" +
                std::to_string(index) + ";\n";
        }
        random_config += "END_RESOURCE";

        int execution[5] = {0, 1, 2, 3, 4};
        int declaration_rank[5]{};
        for(int at = 0; at < 5; ++at)
            declaration_rank[declaration[at]] = at;
        std::sort(execution, execution + 5, [&](int left, int right) {
            return priority[left] != priority[right]
                ? priority[left] < priority[right]
                : declaration_rank[left] < declaration_rank[right];
        });
        std::int64_t expected = 0;
        for(const int index : execution) expected = expected * 10 + index + 1;

        Rig random_rig;
        check(random_rig.build(configuration(random_pous, random_config,
                                             globals)) &&
                  random_rig.boundary(0) && random_rig.drain() &&
                  random_rig.memory("r0") == expected,
              "L5-A02 randomized priority/declaration full oracle");
    }
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

void program_mapping_release_is_one_image_transaction()
{
    const std::string programs =
        "PROGRAM First\nVAR Q AT %QD0 : DWORD; END_VAR\n"
        "Q := DWORD#11;\nEND_PROGRAM\n"
        "PROGRAM Second\nVAR Q AT %QD4 : DWORD; END_VAR\n"
        "Q := DWORD#22;\nEND_PROGRAM\n";
    const std::string config =
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM A WITH T : First;\nPROGRAM B WITH T : Second;\n"
        "END_RESOURCE";
    Rig rig;
    check(rig.build(configuration(programs, config)),
          "L5 multi-mapping transaction builds");
    check(rig.boundary(0), "L5 multi-mapping transaction releases");
    for(int step = 0; step < 100; ++step) {
        check(rig.runtime.run(1) == rt::ErrorCode::ok,
              "L5 multi-mapping transaction advances");
        const st::TaskStatus status = rig.task("r0", "t");
        unsigned char bytes[8]{};
        std::size_t written = 0;
        std::uint64_t version = 99;
        check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                          version) == rt::ErrorCode::ok,
              "L5 multi-mapping snapshot succeeds");
        if(status.state == st::TaskState::running) {
            check(version == 0 && bytes[0] == 0 && bytes[4] == 0,
                  "L5 multi-mapping partial release stays unpublished");
            continue;
        }
        check(status.state == st::TaskState::idle && version == 1 &&
                  bytes[0] == 11 && bytes[4] == 22,
              "L5 multi-mapping release publishes once when complete");
        return;
    }
    fail("L5 multi-mapping release completes within budget");
}

void later_mapping_reads_same_task_staged_image()
{
    const std::string programs =
        "PROGRAM First\nVAR_EXTERNAL Q : DWORD; END_VAR\n"
        "Q := DWORD#17;\nEND_PROGRAM\n"
        "PROGRAM Second\nVAR_EXTERNAL Q : DWORD; END_VAR\n"
        "VAR Seen AT %QD4 : DWORD; END_VAR\nSeen := Q;\nEND_PROGRAM\n";
    const std::string config =
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM A WITH T : First;\nPROGRAM B WITH T : Second;\n"
        "END_RESOURCE";
    Rig rig;
    check(rig.build(configuration(
              programs, config,
              "VAR_GLOBAL Q AT %QD0 : DWORD; END_VAR\n")) &&
              rig.boundary(0) && rig.drain(),
          "L5 same-task staged-image fixture executes");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 0;
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              bytes[0] == 17 && bytes[4] == 17 && version == 1,
          "L5 later mapping reads the shared staged task image");
}

void later_mapping_fault_discards_earlier_mapping_writes()
{
    const std::string programs =
        "PROGRAM First\nVAR Q AT %QD0 : DWORD; END_VAR\n"
        "Q := DWORD#11;\nEND_PROGRAM\n"
        "PROGRAM Bad\nVAR Z : DINT; X : DINT; END_VAR\n"
        "X := 1 / Z;\nEND_PROGRAM\n";
    const std::string config =
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM A WITH T : First;\nPROGRAM B WITH T : Bad;\n"
        "END_RESOURCE";
    Rig rig;
    check(rig.build(configuration(programs, config)),
          "L5 later-mapping fault transaction builds");
    check(rig.boundary(0) && rig.drain(),
          "L5 later-mapping fault transaction executes");
    const st::TaskStatus status = rig.task("r0", "t");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 99;
    check(status.state == st::TaskState::faulted &&
              rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                          version) == rt::ErrorCode::ok &&
              version == 0 && bytes[0] == 0,
          "L5 later mapping fault discards whole task image transaction");
}

void resource_image_remaps_initial_values_and_rejects_bad_overlap()
{
    const std::string initialized = configuration(
        "PROGRAM A\nVAR Q AT %QD0 : DWORD := DWORD#11; END_VAR\n"
        "END_PROGRAM\n"
        "PROGRAM B\nVAR Q AT %QD4 : DWORD := DWORD#22; END_VAR\n"
        "END_PROGRAM\n",
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM A0 WITH T : A;\nPROGRAM B0 WITH T : B;\n"
        "END_RESOURCE");
    Rig rig;
    check(rig.build(initialized),
          "L5 resource image remapped initial values build");
    unsigned char bytes[8]{};
    std::size_t written = 0;
    std::uint64_t version = 99;
    check(rig.runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                      version) == rt::ErrorCode::ok &&
              bytes[0] == 11 && bytes[4] == 22,
          "L5 resource image preserves per-PROGRAM located initial values");

    const st::CompileResult incompatible = st::compile(configuration(
        "PROGRAM Wide\nVAR Q AT %QD0 : DWORD; END_VAR\nEND_PROGRAM\n"
        "PROGRAM Narrow\nVAR Q AT %QB0 : BYTE; END_VAR\nEND_PROGRAM\n",
        "RESOURCE R0 ON PLC\n"
        "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
        "PROGRAM W WITH T : Wide;\nPROGRAM N WITH T : Narrow;\n"
        "END_RESOURCE"));
    std::vector<std::uint64_t> storage(1024);
    st::ConfigurationRuntime runtime;
    check(incompatible.ok &&
              runtime.load(
                  incompatible.program, "plant",
                  reinterpret_cast<unsigned char *>(storage.data()),
                  storage.size() * sizeof(std::uint64_t), kTickNs) ==
                  rt::ErrorCode::invalid_argument,
          "L5 resource image rejects cross-PROGRAM incompatible overlap");

    for(const char area : {'Q', 'M'}) {
        const std::string location = std::string("%") + area + "D0";
        const st::CompileResult local_exact = st::compile(configuration(
            "PROGRAM A\nVAR V AT " + location +
                " : DWORD; END_VAR\nEND_PROGRAM\n"
            "PROGRAM B\nVAR V AT " + location +
                " : DWORD; END_VAR\nEND_PROGRAM\n",
            "RESOURCE R0 ON PLC\n"
            "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
            "PROGRAM A0 WITH T : A;\nPROGRAM B0 WITH T : B;\n"
            "END_RESOURCE"));
        st::ConfigurationRuntime local_runtime;
        check(local_exact.ok &&
                  local_runtime.load(
                      local_exact.program, "plant",
                      reinterpret_cast<unsigned char *>(storage.data()),
                      storage.size() * sizeof(std::uint64_t), kTickNs) ==
                      rt::ErrorCode::invalid_argument,
              area == 'Q'
                  ? "L5 resource rejects distinct local exact Q aliases"
                  : "L5 resource rejects distinct local exact M aliases");
    }

    const char *input_widths[] = {"B", "W", "D", "L"};
    const char *input_types[] = {"BYTE", "WORD", "DWORD", "LWORD"};
    for(std::size_t index = 0; index < 4; ++index) {
        const std::string source = configuration(
            std::string(
                "PROGRAM Bit\nVAR I AT %IX0.0 : BOOL; END_VAR\nEND_PROGRAM\n"
                "PROGRAM Whole\nVAR I AT %I") +
                input_widths[index] + "0 : " + input_types[index] +
                "; END_VAR\nEND_PROGRAM\n",
            "RESOURCE R0 ON PLC\n"
            "TASK T(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
            "PROGRAM X WITH T : Bit;\nPROGRAM N WITH T : Whole;\n"
            "END_RESOURCE");
        Rig input_alias;
        check(input_alias.build(source),
              "L5 resource accepts I X alias contained by B/W/D/L");
    }
}

void conflicts_use_actual_dirty_bits()
{
    Rig readonly;
    check(readonly.build(configuration(
              "PROGRAM A\nVAR_EXTERNAL Q : DWORD; END_VAR\n"
              "VAR N : DINT; END_VAR\n"
              "N := N + 1;\nEND_PROGRAM\n"
              "PROGRAM B\nVAR_EXTERNAL Q : DWORD; END_VAR\n"
              "VAR N : DINT; END_VAR\n"
              "N := N + 1;\nEND_PROGRAM\n",
              "RESOURCE R0 ON PLC\n"
              "TASK A(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
              "TASK B(INTERVAL := T#1ms, PRIORITY := 1, BUDGET := 100);\n"
              "PROGRAM A0 WITH A : A;\nPROGRAM B0 WITH B : B;\n"
              "END_RESOURCE",
              "VAR_GLOBAL Q AT %QD0 : DWORD; END_VAR\n")) &&
              readonly.boundary(0) && readonly.drain(),
          "L5 read-only alias tasks execute");
    st::ResourceStatus readonly_status{};
    check(readonly.runtime.resource_status("r0", readonly_status) ==
                  rt::ErrorCode::ok &&
              readonly_status.write_conflict_count == 0,
          "L5 declared but unwritten located variables do not conflict");

    Rig bits;
    check(bits.build(configuration(
              "PROGRAM A\nVAR Q AT %QX0.0 : BOOL; END_VAR\n"
              "Q := TRUE;\nEND_PROGRAM\n"
              "PROGRAM B\nVAR Q AT %QX0.1 : BOOL; END_VAR\n"
              "Q := TRUE;\nEND_PROGRAM\n",
              "RESOURCE R0 ON PLC\n"
              "TASK A(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 100);\n"
              "TASK B(INTERVAL := T#1ms, PRIORITY := 1, BUDGET := 100);\n"
              "PROGRAM A0 WITH A : A;\nPROGRAM B0 WITH B : B;\n"
              "END_RESOURCE")) &&
              bits.boundary(0) && bits.drain(),
          "L5 disjoint-bit tasks execute");
    st::ResourceStatus bit_status{};
    check(bits.runtime.resource_status("r0", bit_status) ==
                  rt::ErrorCode::ok &&
              bit_status.write_conflict_count == 0,
          "L5 disjoint dirty bits in one byte do not conflict");
}

void resource_input_submission_is_sampled_once_per_task()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Input", "VAR I AT %ID0 : DWORD; Seen : DINT; END_VAR",
              "Seen := DWORD_TO_DINT(I);")),
          "L5 resource input configuration builds");
    const unsigned char input[4] = {42, 0, 0, 0};
    check(rig.runtime.submit_input("r0", input, sizeof(input), 7) ==
                  rt::ErrorCode::ok &&
              rig.boundary(0) && rig.drain() &&
              rig.value("r0", "p0", "seen") == 42,
          "L5 resource submit_input reaches mapped PROGRAM snapshot");
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

void resource_transaction_owner_is_cooperative_and_non_preemptive()
{
    const char *globals = "VAR_GLOBAL Trace AT %MD0 : DWORD; END_VAR\n";
    const char *pous =
        "PROGRAM Low VAR_EXTERNAL Trace : DWORD; END_VAR "
        "VAR I : DINT; N : DINT; END_VAR "
        "FOR I := 1 TO 100 DO N := N + 1; END_FOR; "
        "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + 1); "
        "END_PROGRAM\n"
        "PROGRAM High VAR_EXTERNAL Trace : DWORD; END_VAR "
        "Trace := UDINT_TO_DWORD(DWORD_TO_UDINT(Trace) * 10 + 2); "
        "END_PROGRAM\n";
    const char *config =
        "RESOURCE R0 ON PLC\n"
        "TASK LowTask(INTERVAL := T#10ms, PHASE := T#0ms, PRIORITY := 7, "
        "BUDGET := 10000);\n"
        "TASK HighTask(INTERVAL := T#10ms, PHASE := T#1ms, PRIORITY := 0, "
        "BUDGET := 100);\n"
        "PROGRAM L WITH LowTask : Low;\n"
        "PROGRAM H WITH HighTask : High;\nEND_RESOURCE";
    Rig rig;
    check(rig.build(configuration(pous, config, globals)) &&
              rig.boundary(0) &&
              rig.runtime.run(1) == rt::ErrorCode::ok &&
              rig.boundary(1) && rig.drain(),
          "L5 resource owner survives a higher-priority boundary release");
    check(rig.memory("r0") == 12 &&
              rig.task("r0", "lowtask").completed_count == 1 &&
              rig.task("r0", "hightask").completed_count == 1,
          "L5 cooperative owner completes before newly ready high task");
}

void reset_and_restart_discard_an_active_resource_owner()
{
    for(const bool restart : {false, true}) {
        const char *globals =
            "VAR_GLOBAL Trace AT %MD0 : DWORD; END_VAR\n";
        const char *pous =
            "PROGRAM Long VAR_EXTERNAL Trace : DWORD; END_VAR "
            "VAR I : DINT; N : DINT := 10; END_VAR "
            "N := N + 1; FOR I := 1 TO 100 DO N := N + 1; END_FOR; "
            "Trace := 1; END_PROGRAM\n"
            "PROGRAM Ready VAR_EXTERNAL Trace : DWORD; END_VAR "
            "Trace := 2; END_PROGRAM\n";
        const char *config =
            "RESOURCE R0 ON PLC\n"
            "TASK LongTask(INTERVAL := T#10ms, PHASE := T#0ms, "
            "PRIORITY := 7, BUDGET := 10000);\n"
            "TASK ReadyTask(INTERVAL := T#10ms, PHASE := T#1ms, "
            "PRIORITY := 0, BUDGET := 100);\n"
            "PROGRAM L WITH LongTask : Long;\n"
            "PROGRAM H WITH ReadyTask : Ready;\nEND_RESOURCE";
        Rig rig;
        check(rig.build(configuration(pous, config, globals)) &&
                  rig.boundary(0) &&
                  rig.runtime.run(8) == rt::ErrorCode::ok,
              "L5 active owner recovery fixture starts");
        const std::int64_t before = rig.value("r0", "l", "n");
        const rt::ErrorCode queued = restart
            ? rig.runtime.restart_task("r0", "longtask")
            : rig.runtime.reset_task("r0", "longtask");
        check(queued == rt::ErrorCode::ok && rig.boundary(1) && rig.drain() &&
                  rig.memory("r0") == 2 &&
                  rig.task("r0", "readytask").completed_count == 1,
              restart
                  ? "L5 restart discards owner and unblocks ready task"
                  : "L5 reset discards owner and unblocks ready task");
        check(rig.value("r0", "l", "n") == (restart ? 10 : before),
              restart
                  ? "L5 active-owner restart reinitializes PROGRAM state"
                  : "L5 active-owner reset preserves PROGRAM state");
    }
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
              status.fault_count == 1 &&
              status.fault_pou_index != st::invalid_artifact_index &&
              rig.runtime.artifact_pou_name(status.fault_pou_index) ==
                  "Budget" &&
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
              "FOR I := 1 TO 100 DO "
              "Q := UDINT_TO_DWORD(DWORD_TO_UDINT(Q) + 1); END_FOR;")),
          "L5-D09 wallclock-fault configuration builds");
    check(rig.boundary(0) && rig.runtime.run(8) == rt::ErrorCode::ok,
          "L5-D09 task is active before host report");
    check(rig.runtime.report_wallclock_exceeded("r0", "main") ==
              rt::ErrorCode::ok,
          "L5-D09 host queues wallclock report without VM clock access");
    check(rig.boundary(1), "L5-D09 report latches at next boundary");
    const st::TaskStatus status = rig.task("r0", "main");
    check(status.state == st::TaskState::faulted &&
              status.fault == st::TaskFault::task_wallclock_exceeded &&
              status.fault_pou_index != st::invalid_artifact_index &&
              rig.runtime.artifact_pou_name(status.fault_pou_index) ==
                  "Slow" &&
              status.fault_instruction != 0,
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
    check(rig.boundary(1) &&
              rig.runtime.run(1) == rt::ErrorCode::ok &&
              rig.runtime.report_resource_fault(
              "r0", st::ResourceFault::image_commit_failed) ==
                  rt::ErrorCode::ok,
          "L5-D13 host queues resource fault against active owner");
    for(std::uint64_t tick = 2; tick < 10; ++tick) {
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
              rig.value("r1", "p1", "n") == 9 &&
              rig.task("r1", "t1").missed_release_count == 1,
          "L5-D13 resource fault neither runs locally nor propagates remotely");
}

struct ExecutorFrame
{
    std::uint64_t tick = 0;
    adapters::ServoSetpoints setpoints{};
};

struct ExecutorTrace
{
    std::uint64_t tick = 0;
    double commanded = 0.0;
    double feedback = 0.0;
};

// L5-A07/D07: exercise the reference executor handoff itself: committed
// frames are consumed once, written through Servo, and emitted to a trace
// ring while ST remains an independent host-driven domain.
void motion_tick_consumption_survives_st_fault()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Bad", "VAR Z : DINT; X : DINT; END_VAR", "X := 1 / Z;")),
          "L5-A07 faulting ST configuration builds");
    rt::SpscQueue<ExecutorFrame, 1024> committed;
    rt::SpscQueue<ExecutorTrace, 1024> trace;
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        ExecutorFrame frame{};
        frame.tick = tick;
        frame.setpoints.position = static_cast<double>(tick) + 0.25;
        check(committed.push(frame), "L5-A07 committed frame is queued");
    }
    adapters::ServoSim servo;
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        check(rig.boundary(tick) && rig.drain(),
              "L5-A07 faulted ST boundary returns to host");
        ExecutorFrame frame{};
        adapters::ServoFeedback feedback{};
        check(committed.pop(frame), "L5-A07 RT executor consumes one frame");
        servo.write_setpoints(frame.setpoints);
        servo.read_feedback(feedback);
        check(trace.push({frame.tick, frame.setpoints.position,
                          feedback.position}),
              "L5-A07 RT executor publishes trace record");
    }
    std::uint64_t traces = 0;
    ExecutorTrace record{};
    bool ordered = true;
    while(trace.pop(record)) {
        ordered = ordered && record.tick == traces &&
                  record.commanded == record.feedback;
        ++traces;
    }
    check(traces == 1000 && ordered && committed.empty() &&
              rig.task("r0", "main").state == st::TaskState::faulted,
          "L5-D07 executor and trace cadence survive latched ST task fault");
}

void motion_and_l5_run_concurrently_without_shared_state()
{
    Rig rig;
    check(rig.build(one_periodic(
              "Bad", "VAR Z : DINT; X : DINT; END_VAR", "X := 1 / Z;")),
          "L5-A07 concurrent faulting configuration builds");
    rt::SpscQueue<ExecutorFrame, 1024> committed;
    rt::SpscQueue<ExecutorTrace, 1024> trace;
    for(std::uint64_t tick = 0; tick < 1000; ++tick) {
        ExecutorFrame frame{};
        frame.tick = tick;
        frame.setpoints.position = static_cast<double>(tick) + 0.5;
        check(committed.push(frame),
              "L5-A07 concurrent committed frame is queued");
    }
    std::atomic<bool> start{false};
    std::thread executor([&] {
        adapters::ServoSim servo;
        while(!start.load(std::memory_order_acquire)) {}
        for(std::uint64_t tick = 0; tick < 1000; ++tick) {
            ExecutorFrame frame{};
            while(!committed.pop(frame)) {}
            adapters::ServoFeedback feedback{};
            servo.write_setpoints(frame.setpoints);
            servo.read_feedback(feedback);
            while(!trace.push({frame.tick, frame.setpoints.position,
                               feedback.position})) {}
        }
    });
    start.store(true, std::memory_order_release);
    for(std::uint64_t tick = 0; tick < 1000; ++tick)
        check(rig.boundary(tick) && rig.drain(),
              "L5-A07 concurrent ST boundary returns");
    executor.join();
    std::uint64_t traces = 0;
    ExecutorTrace record{};
    bool ordered = true;
    while(trace.pop(record)) {
        ordered = ordered && record.tick == traces &&
                  record.commanded == record.feedback;
        ++traces;
    }
    check(traces == 1000 && ordered && committed.empty() &&
              rig.task("r0", "main").state == st::TaskState::faulted,
          "L5-A07 RT executor trace and ST complete concurrently");
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
        "Q := UDINT_TO_DWORD(DWORD_TO_UDINT(I) + 1);"));
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

    const std::size_t required =
        static_cast<std::size_t>(report.required_runtime_bytes);
    std::vector<std::uint64_t> exact((required + 7U) / 8U);
    std::vector<std::uint64_t> short_buffer((required + 7U) / 8U);
    st::ConfigurationRuntime exact_runtime;
    st::ConfigurationRuntime short_runtime;
    check(exact_runtime.load(
              compiled.program, "plant",
              reinterpret_cast<unsigned char *>(exact.data()), required,
              kTickNs) == rt::ErrorCode::ok,
          "L5 required_runtime_bytes is sufficient exactly");
    check(short_runtime.load(
              compiled.program, "plant",
              reinterpret_cast<unsigned char *>(short_buffer.data()),
              required - 1U, kTickNs) == rt::ErrorCode::capacity_exceeded,
          "L5 required_runtime_bytes minus one is rejected");
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
              "Q := UDINT_TO_DWORD(DWORD_TO_UDINT(I) + 1);")),
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
    priority_and_declaration_order_have_full_oracle();
    program_mapping_order_is_preserved();
    program_mapping_release_is_one_image_transaction();
    later_mapping_reads_same_task_staged_image();
    later_mapping_fault_discards_earlier_mapping_writes();
    resource_image_remaps_initial_values_and_rejects_bad_overlap();
    conflicts_use_actual_dirty_bits();
    resource_input_submission_is_sampled_once_per_task();
    program_instances_are_independent();
    task_snapshots_never_expose_half_writes();
    missed_releases_do_not_catch_up();
    resource_transaction_owner_is_cooperative_and_non_preemptive();
    reset_and_restart_discard_an_active_resource_owner();
    instruction_budget_fault_discards_output();
    wallclock_fault_is_injected_at_a_boundary();
    faulted_task_is_isolated_from_other_tasks();
    reset_preserves_state_restart_reinitializes_state();
    resource_fault_stops_one_resource_only();
    motion_tick_consumption_survives_st_fault();
    motion_and_l5_run_concurrently_without_shared_state();
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
