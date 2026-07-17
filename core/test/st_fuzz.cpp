// L0 front-end fuzz driver (approved st-l0-semantics 2.7, metric 5.2):
// crash-free compilation of arbitrary input. Successfully compiled programs
// are loaded into fixed static storage and scanned with a finite instruction
// budget. Three deterministic modes per iteration -- structured program
// generation, byte mutation of a valid seed, and raw token soup -- driven by
// a fixed-seed xorshift PRNG so every run is reproducible. The smoke tier runs
// in CTest; the nightly tier runs the full budget under ASan/UBSan (workflow
// core-nightly).
//
// Usage: st_fuzz [--iterations N] [--seed HEX] [--l2b|--l5|--l6|--l7]

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "st/st.h"

namespace
{

struct Rng
{
    std::uint64_t state;

    explicit Rng(std::uint64_t seed)
        : state(seed ? seed : 0x9E3779B97F4A7C15ULL)
    {
    }

    std::uint64_t next()
    {
        std::uint64_t x = state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state = x;
        return x;
    }

    std::uint32_t below(std::uint32_t bound)
    {
        return bound ? static_cast<std::uint32_t>(next() % bound) : 0;
    }
};

const char *const kFragments[] = {
    "PROGRAM", "END_PROGRAM", "VAR", "END_VAR", "IF", "THEN", "ELSIF",
    "ELSE", "END_IF", "CASE", "OF", "END_CASE", "FOR", "TO", "BY", "DO",
    "END_FOR", "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT",
    "EXIT", "RETURN", "AND", "OR", "XOR", "NOT", "MOD", "BOOL", "INT",
    "DINT", "REAL", "LREAL", "TIME", "TON", "CTU", "R_TRIG", "STRUCT",
    "ARRAY", "STRING", "TASK", ":=", ";", ":", ",", "..", ".", "(", ")",
    "+", "-", "*", "/", "<", ">", "<=", ">=", "<>", "=", "&", "16#FF",
    "2#10", "8#7", "T#1s", "T#", "TIME#5ms", "T#1.5s2h", "1.5e3", "1..",
    "SINT", "LINT", "USINT", "ULINT", "BYTE", "WORD", "DWORD", "LWORD",
    "CONSTANT", "CONTINUE", "**", "INT#5", "BYTE#16#FF", "LREAL#1.5",
    "SINT#-129", "ULINT#", "INT_TO_REAL", "LREAL_TO_DINT", "TRUNC_INT",
    "BOOL_TO_BYTE", "WORD_TO_UINT", "TIME_TO_LINT", "XXX_TO_YYY",
    "AXIS_REF", "MC_Power", "MC_Home", "MC_Stop", "MC_Halt",
    "MC_MoveAbsolute", "MC_MoveRelative", "MC_MoveAdditive",
    "MC_MoveVelocity", "MC_SetOverride", "MC_Reset", "Axis", "Execute",
    "BufferMode", "Direction", "ErrorID",
    "1__2", "x", "y", "zz_9", "TRUE", "FALSE", "32768", "-32769",
    "9999999999999999999999", "0.0", "(*", "*)", "//", "\n", "\t", "\"",
    "'", "#", "%", "@", "\x01", "\xFF", "\x80",
};

constexpr std::size_t kRuntimeBufferBytes = 1024U * 1024U;
constexpr std::int64_t kTaskPeriodNs = 1000000;
constexpr std::int64_t kScanBudget = 10000;
alignas(8) unsigned char runtime_buffer[kRuntimeBufferBytes];
std::atomic<std::uint64_t> l7_publish_words[4096]{};
plcopen::core::st::DebugSnapshotEntry l7_publish_entries[16]{};
plcopen::core::st::DebugSnapshotEntry l7_snapshot_entries[16]{};
unsigned char l7_snapshot_bytes[128]{};
std::atomic<std::uint64_t> l7_trace_storage[
    4 * sizeof(plcopen::core::st::DebugTraceRecord) /
    sizeof(std::uint64_t)]{};

bool bounded_scan_error(plcopen::core::st::ScanError error)
{
    using plcopen::core::st::ScanError;
    switch(error) {
    case ScanError::ok:
    case ScanError::division_by_zero:
    case ScanError::for_step_zero:
    case ScanError::budget_exceeded:
    case ScanError::invalid_bytecode:
    case ScanError::conversion_invalid:
    case ScanError::range_violation:
    case ScanError::string_capacity_exceeded:
    case ScanError::date_time_range_violation:
    case ScanError::alias_violation:
    case ScanError::paused:
    case ScanError::invalid_argument: return true;
    case ScanError::not_loaded: return false;
    }
    return false;
}

std::string structured_program(Rng &rng)
{
    std::string source = "PROGRAM f\nVAR\n";
    const int vars = 1 + static_cast<int>(rng.below(4));
    static const char *const kTypes[] = {
        "BOOL", "INT",   "DINT",  "REAL", "LREAL", "TIME",  "TON",
        "CTU",  "SINT",  "LINT",  "USINT", "UINT", "UDINT", "ULINT",
        "BYTE", "WORD",  "DWORD", "LWORD", "AXIS_REF", "MC_Power",
        "MC_MoveAbsolute"};
    for(int i = 0; i < vars; ++i) {
        source += "v";
        source += std::to_string(i);
        source += " : ";
        source += kTypes[rng.below(sizeof(kTypes) / sizeof(kTypes[0]))];
        source += ";\n";
    }
    source += "END_VAR\n";
    const int statements = static_cast<int>(rng.below(12));
    for(int i = 0; i < statements; ++i) {
        switch(rng.below(8)) {
        case 0:
            source += "v0 := v";
            source += std::to_string(rng.below(4));
            source += " + ";
            source += std::to_string(rng.below(70000));
            source += ";\n";
            break;
        case 1:
            source += "IF v0 > 1 THEN v1 := 1; ELSE v1 := 2; END_IF;\n";
            break;
        case 2:
            source += "FOR v0 := 1 TO ";
            source += std::to_string(rng.below(100));
            source += " DO v1 := v1 + 1; END_FOR;\n";
            break;
        case 3:
            source += "CASE v0 OF 1: ; 2..5: ; ELSE ; END_CASE;\n";
            break;
        case 4:
            source += "v0(IN := TRUE, PT := T#1ms);\n";
            break;
        default:
            source += "WHILE v0 < 3 DO v0 := v0 + 1; END_WHILE;\n";
            break;
        }
    }
    source += "END_PROGRAM\n";
    return source;
}

std::string structured_pou_project(Rng &rng)
{
    std::string source;
    const int functions = 1 + static_cast<int>(rng.below(4));
    for(int i = 0; i < functions; ++i) {
        source += "FUNCTION F" + std::to_string(i) + " : DINT\n";
        source += "VAR_INPUT X : DINT; END_VAR\n";
        source += "F" + std::to_string(i) + " := ";
        if(i == 0) {
            source += "X";
        } else {
            source += "F" + std::to_string(i - 1) + "(X)";
        }
        source += " + " + std::to_string(rng.below(8)) + ";\n";
        source += "END_FUNCTION\n";
    }
    source +=
        "FUNCTION_BLOCK Accumulator\n"
        "VAR_INPUT Step : DINT; END_VAR\n"
        "VAR_OUTPUT Q : DINT; END_VAR\n"
        "VAR N : DINT; END_VAR\n"
        "N := N + Step; Q := N;\n"
        "END_FUNCTION_BLOCK\n"
        "PROGRAM Main\nVAR Out : DINT; ";
    const int instances = 1 + static_cast<int>(rng.below(3));
    for(int i = 0; i < instances; ++i) {
        source += "A" + std::to_string(i) + " : Accumulator; ";
    }
    source += "END_VAR\n";
    for(int i = 0; i < instances; ++i) {
        source += "A" + std::to_string(i) + "(Step := F" +
                  std::to_string(functions - 1) + "(" +
                  std::to_string(rng.below(32)) + ")); ";
    }
    source += "Out := A" + std::to_string(instances - 1) +
              ".Q;\nEND_PROGRAM\n";
    return source;
}

std::string structured_configuration(Rng &rng)
{
    const int programs = 1 + static_cast<int>(rng.below(4));
    const int tasks = 1 + static_cast<int>(rng.below(
        static_cast<std::uint32_t>(programs)));
    const bool fault_last = programs > 1 && rng.below(5) == 0;
    std::string source;
    for(int index = 0; index < programs; ++index) {
        source += "PROGRAM P" + std::to_string(index) + "\nVAR ";
        if(index == 0) source += "I AT %ID0 : DWORD; ";
        source += "Q AT %QD" + std::to_string(index * 4) +
                  " : DWORD; N : DINT; Z : DINT; END_VAR\n"
                  "N := N + 1; Q := DINT_TO_DWORD(N); ";
        if(fault_last && index + 1 == programs) source += "N := 1 / Z; ";
        source += "END_PROGRAM\n";
    }
    source += "CONFIGURATION Plant\n";
    const int resources = 1 + static_cast<int>(rng.below(3));
    for(int resource = 0; resource < resources; ++resource) {
        source += "RESOURCE R" + std::to_string(resource) + " ON PLC_" +
                  std::to_string(rng.below(4)) + "\n";
        for(int task = 0; task < tasks; ++task) {
            source += "TASK T" + std::to_string(task) +
                      "(INTERVAL := T#" +
                      std::to_string(1U + rng.below(8)) +
                      "ms, PHASE := T#0ms, PRIORITY := " +
                      std::to_string(rng.below(16)) + ", BUDGET := " +
                      std::to_string(8U + rng.below(256)) + ");\n";
        }
        for(int program = 0; program < programs; ++program) {
            source += "PROGRAM I" + std::to_string(program) + " WITH T" +
                      std::to_string(program % tasks) + " : P" +
                      std::to_string(program) + ";\n";
        }
        source += "END_RESOURCE\n";
    }
    source += "END_CONFIGURATION\n";
    return source;
}

enum class L6Run : std::uint8_t
{
    normal = 0,
    restart,
    fault,
    trace_overflow,
};

struct L6Case
{
    std::string source;
    plcopen::core::st::CompileOptions options;
    bool should_compile = true;
    plcopen::core::st::DiagCode expected_diagnostic =
        plcopen::core::st::DiagCode::sema_unsafe_sfc_network;
    L6Run run = L6Run::normal;
};

std::string sfc_program(std::string_view network,
                        std::string_view action_body =
                            "Count := Count + 1;")
{
    std::string source =
        "PROGRAM Main\nVAR Count : DINT; Flag : BOOL; Z : DINT; END_VAR\n"
        "SFC Flow\n";
    source += network;
    if(network.find("Work(") != std::string_view::npos) {
        source += "\nACTION Work: ";
        source += action_body;
        source += " END_ACTION";
    }
    source += "\nEND_SFC\nEND_PROGRAM\n";
    return source;
}

std::string qualifier_block(std::uint32_t qualifier, Rng &rng)
{
    static const char *const kQualifiers[] = {
        "N", "S", "R", "L", "D", "P", "SD", "DS", "SL"};
    std::string result = "Work(";
    result += kQualifiers[qualifier % 9U];
    if(qualifier == 3U || qualifier == 4U || qualifier >= 6U) {
        result += ", T#" + std::to_string(rng.below(5)) + "ms";
    }
    result += ")";
    return result;
}

std::string linear_sfc(std::uint32_t steps)
{
    std::string network = "INITIAL_STEP S0: END_STEP\n";
    for(std::uint32_t index = 1; index < steps; ++index) {
        network += "STEP S" + std::to_string(index) + ": END_STEP\n";
    }
    for(std::uint32_t index = 1; index < steps; ++index) {
        network += "TRANSITION FROM S" + std::to_string(index - 1) +
                   " TO S" + std::to_string(index) +
                   " := TRUE; END_TRANSITION\n";
    }
    network += "TRANSITION FROM S" + std::to_string(steps - 1) +
               " TO S0 := TRUE; END_TRANSITION";
    return sfc_program(network);
}

L6Case structured_sfc_case(Rng &rng, long long iteration)
{
    L6Case test;
    const std::uint32_t qualifier = iteration < 9
        ? static_cast<std::uint32_t>(iteration)
        : rng.below(9);
    const std::string block = qualifier_block(qualifier, rng);
    const std::uint32_t scenario = iteration < 9
        ? static_cast<std::uint32_t>(iteration % 4)
        : iteration < 16 ? static_cast<std::uint32_t>(iteration - 5)
                         : rng.below(11);
    switch(scenario) {
    case 0:
        test.source = sfc_program(
            "INITIAL_STEP A: " + block + "; END_STEP\n"
            "STEP B: TERMINAL; END_STEP\n"
            "TRANSITION FROM A TO B := TRUE; END_TRANSITION");
        break;
    case 1:
        test.source = sfc_program(
            "INITIAL_STEP Pick: END_STEP\n"
            "STEP First: " + block + "; TERMINAL; END_STEP\n"
            "STEP Second: TERMINAL; END_STEP\n"
            "TRANSITION FROM Pick TO First := Flag; END_TRANSITION\n"
            "TRANSITION FROM Pick TO Second := TRUE; END_TRANSITION");
        break;
    case 2:
        test.source = sfc_program(
            "INITIAL_STEP Start: END_STEP\n"
            "STEP Left: " + block + "; END_STEP\n"
            "STEP Right: END_STEP\nSTEP Done: TERMINAL; END_STEP\n"
            "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
            "END_TRANSITION\n"
            "TRANSITION FROM (Left, Right) TO Done SIMULTANEOUS := TRUE; "
            "END_TRANSITION");
        break;
    case 3:
        test.source = sfc_program(
            "INITIAL_STEP Start: END_STEP\n"
            "STEP Left: END_STEP\nSTEP Right: END_STEP\n"
            "STEP Done: " + block + "; TERMINAL; END_STEP\n"
            "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
            "END_TRANSITION\n"
            "TRANSITION FROM (Left, Right) TO Done SIMULTANEOUS := TRUE; "
            "END_TRANSITION");
        break;
    case 4:
        test.should_compile = false;
        test.expected_diagnostic =
            plcopen::core::st::DiagCode::sema_unsafe_sfc_network;
        switch(rng.below(4)) {
        case 0:
            test.source = sfc_program(
                "STEP A: END_STEP");
            break;
        case 1:
            test.source = sfc_program(
                "INITIAL_STEP A: END_STEP\nINITIAL_STEP B: END_STEP");
            break;
        case 2:
            test.source = sfc_program(
                "INITIAL_STEP A: END_STEP\nSTEP B: END_STEP\n"
                "STEP C: END_STEP\n"
                "TRANSITION FROM A TO (B, C) SIMULTANEOUS := TRUE; "
                "END_TRANSITION");
            break;
        default:
            test.source = sfc_program(
                "INITIAL_STEP Start: END_STEP\n"
                "STEP Left: END_STEP\nSTEP Right: END_STEP\n"
                "STEP Cross: END_STEP\nSTEP Done: TERMINAL; END_STEP\n"
                "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := "
                "TRUE; END_TRANSITION\n"
                "TRANSITION FROM (Left, Cross) TO Done SIMULTANEOUS := "
                "TRUE; END_TRANSITION");
            break;
        }
        break;
    case 5:
    case 6: {
        const std::uint32_t steps = 2U + rng.below(6);
        test.source = linear_sfc(steps);
        test.options.max_sfc_steps = static_cast<std::uint16_t>(
            scenario == 5 ? steps : steps - 1U);
        if(scenario == 6) {
            test.should_compile = false;
            test.expected_diagnostic =
                plcopen::core::st::DiagCode::capacity_exceeded;
        }
        break;
    }
    case 7:
        test.source = sfc_program(
            "INITIAL_STEP A: Work(N); TERMINAL; END_STEP",
            "Count := 1 / Z;");
        test.run = L6Run::fault;
        break;
    case 8:
        test.source = sfc_program(
            "INITIAL_STEP A: END_STEP\n"
            "STEP B: Work(P); END_STEP\n"
            "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
            "TRANSITION FROM B TO A := TRUE; END_TRANSITION");
        test.run = L6Run::restart;
        break;
    case 9:
        test.source = sfc_program(
            "INITIAL_STEP A: Work(N); TERMINAL; END_STEP");
        test.run = L6Run::trace_overflow;
        break;
    default:
        test.source = sfc_program(
            "INITIAL_STEP Start: END_STEP\n"
            "STEP Left: " + block + "; TERMINAL; END_STEP\n"
            "STEP Right: TERMINAL; END_STEP\n"
            "TRANSITION FROM Start TO (Left, Right) SIMULTANEOUS := TRUE; "
            "END_TRANSITION");
        break;
    }
    return test;
}

bool has_diagnostic(const plcopen::core::st::CompileResult &result,
                    plcopen::core::st::DiagCode code)
{
    for(const plcopen::core::st::Diagnostic &diagnostic :
        result.diagnostics) {
        if(diagnostic.code == code) return true;
    }
    return false;
}

template <typename Instance, typename = void>
struct HasL6RuntimeApi : std::false_type
{
};

template <typename Instance>
struct HasL6RuntimeApi<
    Instance,
    std::void_t<
        decltype(std::declval<Instance &>().configure_sfc_trace(
            static_cast<plcopen::core::st::SfcTraceRecord *>(nullptr),
            std::size_t{})),
        decltype(std::declval<Instance &>().restart_sfc(
            static_cast<const char *>(nullptr))),
        decltype(std::declval<const Instance &>().sfc_step_active(
            static_cast<const char *>(nullptr),
            static_cast<const char *>(nullptr), std::declval<bool &>())),
        decltype(std::declval<const Instance &>().sfc_trace_size()),
        decltype(std::declval<const Instance &>().sfc_trace_dropped())>>
    : std::true_type
{
};

template <typename Instance>
bool run_l6_runtime(Instance &instance, const L6Case &test, Rng &rng,
                    long long iteration)
{
    using namespace plcopen::core;
    if constexpr(!HasL6RuntimeApi<Instance>::value) {
        (void)instance;
        (void)test;
        (void)rng;
        std::printf(
            "FUZZ_FAIL L6 runtime API unavailable at iteration %lld: "
            "configure_sfc_trace/restart_sfc/sfc_step_active/trace counters\n",
            iteration);
        return false;
    } else {
        st::SfcTraceRecord trace[8]{};
        std::size_t trace_capacity = 0;
        if(test.run == L6Run::trace_overflow) {
            trace_capacity = 1U + rng.below(4);
            if(instance.configure_sfc_trace(trace, trace_capacity) !=
               rt::ErrorCode::ok) {
                std::printf("FUZZ_FAIL L6 trace setup at iteration %lld\n",
                            iteration);
                return false;
            }
        }

        const int scans = test.run == L6Run::trace_overflow ? 8 : 3;
        st::ScanError last = st::ScanError::ok;
        for(int scan = 0; scan < scans; ++scan) {
            last = instance.scan(kScanBudget);
            if(test.run == L6Run::fault) break;
            if(last != st::ScanError::ok) {
                std::printf("FUZZ_FAIL L6 scan at iteration %lld error=%d\n",
                            iteration, static_cast<int>(last));
                return false;
            }
        }

        if(test.run == L6Run::fault) {
            if(last != st::ScanError::division_by_zero ||
               instance.restart_sfc("flow") != rt::ErrorCode::ok) {
                std::printf(
                    "FUZZ_FAIL L6 fault/restart at iteration %lld error=%d\n",
                    iteration, static_cast<int>(last));
                return false;
            }
        } else if(test.run == L6Run::restart) {
            bool active = false;
            if(instance.restart_sfc("flow") != rt::ErrorCode::ok ||
               instance.sfc_step_active("flow", "a", active) !=
                   rt::ErrorCode::ok || !active) {
                std::printf("FUZZ_FAIL L6 restart at iteration %lld\n",
                            iteration);
                return false;
            }
        } else if(test.run == L6Run::trace_overflow) {
            if(instance.sfc_trace_size() > trace_capacity ||
               instance.sfc_trace_dropped() == 0) {
                std::printf(
                    "FUZZ_FAIL L6 trace overflow at iteration %lld size=%zu "
                    "capacity=%zu dropped=%llu\n",
                    iteration, instance.sfc_trace_size(), trace_capacity,
                    static_cast<unsigned long long>(
                        instance.sfc_trace_dropped()));
                return false;
            }
        }
        return true;
    }
}

bool run_l6_case(const L6Case &test,
                 const plcopen::core::st::CompileResult &result,
                 Rng &rng, long long iteration)
{
    using namespace plcopen::core;
    if(!test.should_compile) {
        if(result.ok || !has_diagnostic(result, test.expected_diagnostic)) {
            std::printf("FUZZ_FAIL L6 rejection at iteration %lld\n",
                        iteration);
            return false;
        }
        return true;
    }
    if(!result.ok) {
        const int diagnostic = result.diagnostics.empty()
            ? -1 : static_cast<int>(result.diagnostics.front().code);
        std::printf("FUZZ_FAIL L6 compile at iteration %lld diagnostic=%d\n",
                    iteration, diagnostic);
        return false;
    }

    st::Instance instance;
    const rt::ErrorCode loaded = instance.load(
        result.program, "main", runtime_buffer, sizeof(runtime_buffer),
        kTaskPeriodNs);
    if(loaded != rt::ErrorCode::ok) {
        std::printf("FUZZ_FAIL L6 load at iteration %lld required=%zu error=%d\n",
                    iteration, result.program.required_bytes(),
                    static_cast<int>(loaded));
        return false;
    }

    return run_l6_runtime(instance, test, rng, iteration);
}

std::string token_soup(Rng &rng)
{
    std::string source;
    const int tokens = static_cast<int>(rng.below(120));
    for(int i = 0; i < tokens; ++i) {
        source += kFragments[rng.below(sizeof(kFragments) /
                                       sizeof(kFragments[0]))];
        if(rng.below(3) == 0) {
            source += ' ';
        }
    }
    return source;
}

std::string mutated_seed(Rng &rng)
{
    std::string source =
        "PROGRAM m\nVAR x : INT; t : TON; END_VAR\n"
        "x := 16#FF + 2 * (x - 1);\n"
        "IF x >= 5 THEN x := x MOD 3; END_IF;\n"
        "FOR x := 1 TO 10 BY 2 DO ; END_FOR;\n"
        "t(IN := x > 2, PT := T#1s500ms);\n"
        "CASE x OF 1, 3..4: x := 0; ELSE ; END_CASE;\n"
        "END_PROGRAM\n";
    const int mutations = 1 + static_cast<int>(rng.below(8));
    for(int i = 0; i < mutations; ++i) {
        const std::size_t at = rng.below(
            static_cast<std::uint32_t>(source.size()));
        source[at] = static_cast<char>(rng.next() & 0xFF);
    }
    return source;
}

const char *l7_debug_configuration()
{
    return
        "FUNCTION Inc : DINT\n"
        "VAR_INPUT V : DINT; END_VAR\n"
        "Inc := V + 1;\n"
        "END_FUNCTION\n"
        "PROGRAM Main\n"
        "VAR I AT %IB0 : BYTE; Q AT %QB0 : BYTE; X : DINT; Y : DINT; "
        "Z : DINT; END_VAR\n"
        "X := Inc(X);\n"
        "Y := X + 10;\n"
        "Q := I;\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant\n"
        "RESOURCE R0 ON PLC\n"
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000);\n"
        "PROGRAM P0 WITH Main : Main;\n"
        "END_RESOURCE\n"
        "END_CONFIGURATION\n";
}

struct L7FuzzRig
{
    plcopen::core::st::CompileResult compiled;
    plcopen::core::st::ConfigurationRuntime runtime;
    plcopen::core::st::DebugSession debug;
    plcopen::core::st::SymbolInfo input;
    plcopen::core::st::SymbolInfo output;
    plcopen::core::st::SymbolInfo x;
    plcopen::core::st::SymbolInfo y;
    std::array<bool, 3> breakpoint_present{};
    std::size_t breakpoint_count = 0;
    std::uint64_t tick = 0;
    std::uint64_t snapshot_version = 0;
    bool published = false;

    static constexpr std::array<std::uint32_t, 3> breakpoint_lines{
        7U, 8U, 9U};

    bool fail(long long iteration, const char *operation,
              const char *detail) const
    {
        std::printf("FUZZ_FAIL L7 iteration=%lld operation=%s detail=%s\n",
                    iteration, operation, detail);
        return false;
    }

    bool build(long long iteration)
    {
        using namespace plcopen::core;
        st::CompileOptions compile_options;
        compile_options.source_name = "debug.st";
        compile_options.debug_mode = st::DebugMode::enabled;
        compiled = st::compile(l7_debug_configuration(), compile_options);
        if(!compiled.ok)
            return fail(iteration, "build", "compile");
        if(compiled.program.debug_call_sites.empty() ||
           compiled.program.source_map.first_instruction_at("debug.st", 7) ==
               UINT32_MAX)
            return fail(iteration, "build", "source-map");
        if(runtime.load(compiled.program, "plant", runtime_buffer,
                        sizeof(runtime_buffer), kTaskPeriodNs) !=
           rt::ErrorCode::ok)
            return fail(iteration, "build", "runtime-load");

        st::DebugSessionOptions options;
        options.max_breakpoints = breakpoint_lines.size();
        options.max_watch_symbols = 2;
        options.trace_capacity = 4;
        if(debug.attach(runtime, {"r0", "main"},
                        l7_publish_words, std::size(l7_publish_words),
                        l7_publish_entries,
                        std::size(l7_publish_entries), l7_trace_storage,
                        std::size(l7_trace_storage), options) !=
           st::DebugError::ok)
            return fail(iteration, "build", "debug-attach");
        if(compiled.program.find_symbol("plant.r0.p0.main.i", input) !=
               rt::ErrorCode::ok ||
           compiled.program.find_symbol("plant.r0.p0.main.q", output) !=
               rt::ErrorCode::ok ||
           compiled.program.find_symbol("plant.r0.p0.main.x", x) !=
               rt::ErrorCode::ok ||
           compiled.program.find_symbol("plant.r0.p0.main.y", y) !=
               rt::ErrorCode::ok)
            return fail(iteration, "build", "symbol-map");
        if(debug.add_watch(x.id) != st::DebugError::ok ||
           debug.add_watch(output.id) != st::DebugError::ok ||
           debug.add_watch(x.id) != st::DebugError::ok)
            return fail(iteration, "build", "watch-dedup");
        return true;
    }

    bool task_status(plcopen::core::st::TaskStatus &status,
                     long long iteration, const char *operation)
    {
        if(runtime.task_status("r0", "main", status) !=
           plcopen::core::rt::ErrorCode::ok)
            return fail(iteration, operation, "task-status");
        return true;
    }

    bool boundary_and_run(std::int64_t budget, long long iteration,
                          const char *operation)
    {
        ++tick;
        if(runtime.boundary(tick, nullptr, 0) !=
               plcopen::core::rt::ErrorCode::ok ||
           runtime.run(budget) != plcopen::core::rt::ErrorCode::ok)
            return fail(iteration, operation, "boundary-run");
        published = true;
        return true;
    }

    bool clear_breakpoints(long long iteration, const char *operation)
    {
        using namespace plcopen::core;
        for(std::size_t slot = 0; slot < breakpoint_present.size(); ++slot) {
            if(!breakpoint_present[slot]) continue;
            if(debug.remove_breakpoint_at("debug.st", breakpoint_lines[slot],
                                          1) != st::DebugError::ok)
                return fail(iteration, operation, "breakpoint-clear");
            breakpoint_present[slot] = false;
            --breakpoint_count;
        }
        if(debug.breakpoint_count() != breakpoint_count)
            return fail(iteration, operation, "breakpoint-oracle");
        return true;
    }

    bool recover(long long iteration, const char *operation)
    {
        using namespace plcopen::core;
        if(!clear_breakpoints(iteration, operation)) return false;
        st::TaskStatus status{};
        if(!task_status(status, iteration, operation)) return false;
        if(status.state == st::TaskState::paused) {
            if(debug.control(st::DebugCommand::continue_) !=
                   st::DebugError::ok ||
               runtime.run(kScanBudget) != rt::ErrorCode::ok)
                return fail(iteration, operation, "continue-paused");
            if(!task_status(status, iteration, operation)) return false;
        }
        if(status.state == st::TaskState::faulted) {
            if(runtime.restart_task("r0", "main") != rt::ErrorCode::ok ||
               !boundary_and_run(kScanBudget, iteration, operation) ||
               debug.control(st::DebugCommand::continue_) !=
                   st::DebugError::ok)
                return fail(iteration, operation, "restart-faulted");
        }
        return true;
    }

    bool breakpoint_operation(Rng &rng, long long iteration)
    {
        using namespace plcopen::core;
        const std::size_t slot = rng.below(
            static_cast<std::uint32_t>(breakpoint_present.size()));
        const bool add = !breakpoint_present[slot] || rng.below(2) == 0;
        if(add) {
            st::BreakpointId first = 0;
            st::BreakpointId duplicate = 0;
            if(debug.add_breakpoint("debug.st", breakpoint_lines[slot], 1,
                                    first) != st::DebugError::ok ||
               debug.add_breakpoint("debug.st", breakpoint_lines[slot], 1,
                                    duplicate) != st::DebugError::ok ||
               first != duplicate)
                return fail(iteration, "breakpoint", "add-dedup");
            if(!breakpoint_present[slot]) {
                breakpoint_present[slot] = true;
                ++breakpoint_count;
            }
        } else {
            if(debug.remove_breakpoint_at("debug.st", breakpoint_lines[slot],
                                          1) != st::DebugError::ok)
                return fail(iteration, "breakpoint", "remove");
            breakpoint_present[slot] = false;
            --breakpoint_count;
            if(debug.remove_breakpoint_at("debug.st", breakpoint_lines[slot],
                                          1) !=
               st::DebugError::invalid_source_location)
                return fail(iteration, "breakpoint", "remove-absent");
        }
        if(debug.breakpoint_count() != breakpoint_count)
            return fail(iteration, "breakpoint", "cardinality");
        if(iteration == 0 && breakpoint_present[slot]) {
            if(debug.remove_breakpoint_at("debug.st", breakpoint_lines[slot],
                                          1) != st::DebugError::ok)
                return fail(iteration, "breakpoint", "initial-remove");
            breakpoint_present[slot] = false;
            --breakpoint_count;
            if(debug.remove_breakpoint_at("debug.st", breakpoint_lines[slot],
                                          1) !=
                   st::DebugError::invalid_source_location ||
               debug.breakpoint_count() != breakpoint_count)
                return fail(iteration, "breakpoint", "initial-absent");
        }
        return true;
    }

    bool snapshot_operation(long long iteration)
    {
        using namespace plcopen::core;
        if(!published) {
            if(!recover(iteration, "snapshot") ||
               !boundary_and_run(kScanBudget, iteration, "snapshot"))
                return false;
        }
        st::DebugSnapshot snapshot{};
        if(debug.read_snapshot(snapshot, l7_snapshot_entries,
                               std::size(l7_snapshot_entries),
                               l7_snapshot_bytes, sizeof(l7_snapshot_bytes),
                               0) != st::DebugError::snapshot_busy)
            return fail(iteration, "snapshot", "zero-retry");
        if(debug.read_snapshot(snapshot, l7_snapshot_entries,
                               std::size(l7_snapshot_entries),
                               l7_snapshot_bytes, sizeof(l7_snapshot_bytes),
                               8) != st::DebugError::ok ||
           snapshot.value_count != 2 ||
           snapshot.version < snapshot_version)
            return fail(iteration, "snapshot", "read-oracle");
        snapshot_version = snapshot.version;
        if(debug.add_watch(y.id) != st::DebugError::watch_plan_frozen ||
           debug.add_watch(y.id) != st::DebugError::watch_plan_frozen)
            return fail(iteration, "snapshot", "watch-freeze");
        return true;
    }

    bool pause_continue_operation(long long iteration)
    {
        using namespace plcopen::core;
        if(!recover(iteration, "pause-continue")) return false;
        st::BreakpointId id = 0;
        if(debug.add_breakpoint("debug.st", 8, 1, id) !=
           st::DebugError::ok)
            return fail(iteration, "pause-continue", "add");
        breakpoint_present[1] = true;
        ++breakpoint_count;
        if(!boundary_and_run(kScanBudget, iteration, "pause-continue"))
            return false;
        st::DebugStop stop{};
        st::TaskStatus status{};
        if(debug.poll_stop(stop) != st::DebugError::ok || stop.line != 8 ||
           !task_status(status, iteration, "pause-continue") ||
           status.state != st::TaskState::paused)
            return fail(iteration, "pause-continue", "stop-oracle");
        if(!clear_breakpoints(iteration, "pause-continue") ||
           debug.control(st::DebugCommand::continue_) != st::DebugError::ok ||
           runtime.run(kScanBudget) != rt::ErrorCode::ok)
            return fail(iteration, "pause-continue", "resume");
        return true;
    }

    bool step_operation(bool step_in, long long iteration)
    {
        using namespace plcopen::core;
        const char *operation = step_in ? "step-in-out" : "step-over";
        if(!recover(iteration, operation)) return false;
        st::BreakpointId id = 0;
        if(debug.add_breakpoint("debug.st", 7, 1, id) !=
           st::DebugError::ok)
            return fail(iteration, operation, "add");
        breakpoint_present[0] = true;
        ++breakpoint_count;
        if(!boundary_and_run(kScanBudget, iteration, operation)) return false;
        st::DebugStop stop{};
        if(debug.poll_stop(stop) != st::DebugError::ok || stop.line != 7 ||
           !clear_breakpoints(iteration, operation))
            return fail(iteration, operation, "call-stop");
        if(debug.control(step_in ? st::DebugCommand::step_in
                                : st::DebugCommand::step_over) !=
               st::DebugError::ok ||
           runtime.run(kScanBudget) != rt::ErrorCode::ok ||
           debug.poll_stop(stop) != st::DebugError::ok ||
           stop.reason != st::DebugStopReason::step)
            return fail(iteration, operation, "first-step");
        if(step_in) {
            if(stop.pou != "inc" || stop.call_depth != 2 ||
               debug.control(st::DebugCommand::step_out) !=
                   st::DebugError::ok ||
               runtime.run(kScanBudget) != rt::ErrorCode::ok ||
               debug.poll_stop(stop) != st::DebugError::ok ||
               stop.pou != "main" || stop.line != 8 ||
               stop.call_depth != 1)
                return fail(iteration, operation, "step-out");
        } else if(stop.pou != "main" || stop.line != 8 ||
                  stop.call_depth != 1) {
            return fail(iteration, operation, "step-over-oracle");
        }
        if(debug.control(st::DebugCommand::continue_) != st::DebugError::ok ||
           runtime.run(kScanBudget) != rt::ErrorCode::ok)
            return fail(iteration, operation, "complete");
        return true;
    }

    bool force_operation(Rng &rng, long long iteration)
    {
        using namespace plcopen::core;
        if(!recover(iteration, "force")) return false;
        const unsigned char first = static_cast<unsigned char>(
            1U + rng.below(100));
        const unsigned char updated = static_cast<unsigned char>(
            101U + rng.below(100));
        st::ForceReceipt first_receipt{};
        st::ForceReceipt update_receipt{};
        if(debug.queue_force(input.id, st::builtin::byte_, &first, 1,
                             first_receipt) != st::DebugError::ok ||
           debug.queue_force(input.id, st::builtin::byte_, &updated, 1,
                             update_receipt) != st::DebugError::ok ||
           update_receipt.queue_version != first_receipt.queue_version + 1U ||
           update_receipt.target_release != first_receipt.target_release ||
           update_receipt.target_resource != "r0" ||
           update_receipt.target_task != "main" ||
           runtime.force_queue_version("r0") !=
               update_receipt.queue_version)
            return fail(iteration, "force", "queue-update");
        if(!boundary_and_run(kScanBudget, iteration, "force")) return false;
        unsigned char bytes[8]{};
        std::size_t written = 0;
        std::uint64_t version = 0;
        if(runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                   version) != rt::ErrorCode::ok ||
           written == 0 || bytes[0] != updated)
            return fail(iteration, "force", "updated-value");
        st::ForceReceipt release_receipt{};
        if(debug.release_force(input.id, release_receipt) !=
               st::DebugError::ok ||
           release_receipt.queue_version != update_receipt.queue_version + 1U ||
           release_receipt.target_release <= update_receipt.target_release ||
           !boundary_and_run(kScanBudget, iteration, "force") ||
           runtime.output_snapshot("r0", bytes, sizeof(bytes), written,
                                   version) != rt::ErrorCode::ok ||
           bytes[0] != 0)
            return fail(iteration, "force", "release");
        return true;
    }

    bool fault_recovery_operation(bool restart, long long iteration)
    {
        using namespace plcopen::core;
        const char *operation = restart ? "fault-restart" : "fault-reset";
        if(!recover(iteration, operation)) return false;
        ++tick;
        if(runtime.boundary(tick, nullptr, 0) != rt::ErrorCode::ok ||
           runtime.run(1) != rt::ErrorCode::ok ||
           runtime.report_wallclock_exceeded("r0", "main") !=
               rt::ErrorCode::ok) {
            return fail(iteration, operation, "inject");
        }
        ++tick;
        if(runtime.boundary(tick, nullptr, 0) != rt::ErrorCode::ok)
            return fail(iteration, operation, "fault-boundary");
        st::TaskStatus status{};
        st::DebugSnapshot snapshot{};
        if(!task_status(status, iteration, operation) ||
           status.state != st::TaskState::faulted ||
           debug.control(st::DebugCommand::continue_) !=
               st::DebugError::task_faulted ||
           debug.read_snapshot(snapshot, l7_snapshot_entries,
                               std::size(l7_snapshot_entries),
                               l7_snapshot_bytes, sizeof(l7_snapshot_bytes),
                               8) != st::DebugError::ok ||
           snapshot.task_state != st::TaskState::faulted)
            return fail(iteration, operation, "fault-oracle");
        const rt::ErrorCode queued = restart
            ? runtime.restart_task("r0", "main")
            : runtime.reset_task("r0", "main");
        if(queued != rt::ErrorCode::ok ||
           !boundary_and_run(kScanBudget, iteration, operation) ||
           debug.control(st::DebugCommand::continue_) != st::DebugError::ok ||
           !boundary_and_run(kScanBudget, iteration, operation) ||
           !task_status(status, iteration, operation) ||
           status.state == st::TaskState::faulted)
            return fail(iteration, operation, "recovery-oracle");
        return true;
    }

    bool trace_operation(long long iteration)
    {
        using namespace plcopen::core;
        if(!recover(iteration, "trace")) return false;
        for(int index = 0; index < 4; ++index)
            if(!boundary_and_run(kScanBudget, iteration, "trace"))
                return false;
        st::DebugTraceRecord records[4]{};
        st::DebugTraceReport report{};
        if(debug.read_trace(records, std::size(records), report) !=
               st::DebugError::ok ||
           report.written != std::size(records) || report.dropped == 0)
            return fail(iteration, "trace", "overflow-report");
        for(std::size_t index = 1; index < report.written; ++index)
            if(records[index].sequence != records[index - 1].sequence + 1U)
                return fail(iteration, "trace", "sequence");
        return true;
    }

    bool invalid_operation(long long iteration)
    {
        using namespace plcopen::core;
        st::BreakpointId id = 0;
        const std::int64_t value = 1;
        st::ForceReceipt receipt{};
        const unsigned char forced = 1;
        if(debug.add_breakpoint("missing.st", 1, 1, id) !=
               st::DebugError::invalid_source_location ||
           debug.add_breakpoint_instruction(UINT32_MAX, id) !=
               st::DebugError::invalid_instruction ||
           debug.write_symbol(x.id, st::builtin::dint, &value,
                              sizeof(value)) !=
               st::DebugError::permission_denied ||
           debug.queue_force(st::SymbolId{}, st::builtin::byte_, &forced, 1,
                             receipt) != st::DebugError::invalid_symbol ||
           debug.queue_force(input.id, st::builtin::word, &forced, 1,
                             receipt) != st::DebugError::type_mismatch)
            return fail(iteration, "invalid", "stable-error");
        return true;
    }
};

bool run_l7_state_machine(long long iterations, std::uint64_t seed)
{
    L7FuzzRig rig;
    if(!rig.build(0)) return false;
    Rng rng(seed);
    for(long long iteration = 0; iteration < iterations; ++iteration) {
        const std::uint32_t operation = iteration < 10
            ? static_cast<std::uint32_t>(iteration)
            : rng.below(16);
        bool ok = false;
        switch(operation) {
        case 0:
        case 10:
        case 11: ok = rig.breakpoint_operation(rng, iteration); break;
        case 1:
        case 12: ok = rig.snapshot_operation(iteration); break;
        case 2: ok = rig.pause_continue_operation(iteration); break;
        case 3: ok = rig.step_operation(false, iteration); break;
        case 4: ok = rig.step_operation(true, iteration); break;
        case 5:
        case 13: ok = rig.force_operation(rng, iteration); break;
        case 6: ok = rig.fault_recovery_operation(false, iteration); break;
        case 7: ok = rig.fault_recovery_operation(true, iteration); break;
        case 8:
        case 14: ok = rig.trace_operation(iteration); break;
        case 9:
        case 15: ok = rig.invalid_operation(iteration); break;
        default: ok = false; break;
        }
        if(!ok) return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    long long iterations = 3000;
    std::uint64_t seed = 0xC0DEF00DULL;
    bool l2b_only = false;
    bool l5_only = false;
    bool l6_only = false;
    bool l7_only = false;
    for(int i = 1; i < argc; ++i) {
        if(i + 1 < argc && std::strcmp(argv[i], "--iterations") == 0) {
            iterations = std::atoll(argv[i + 1]);
            ++i;
        } else if(i + 1 < argc && std::strcmp(argv[i], "--seed") == 0) {
            seed = std::strtoull(argv[i + 1], nullptr, 16);
            ++i;
        } else if(std::strcmp(argv[i], "--l2b") == 0) {
            l2b_only = true;
        } else if(std::strcmp(argv[i], "--l5") == 0) {
            l5_only = true;
        } else if(std::strcmp(argv[i], "--l6") == 0) {
            l6_only = true;
        } else if(std::strcmp(argv[i], "--l7") == 0) {
            l7_only = true;
        }
    }

    if(l7_only) {
        if(!run_l7_state_machine(iterations, seed)) return 1;
        std::printf("FUZZ_PASS seed=0x%llX iterations=%lld ok=%lld mode=L7\n",
                    static_cast<unsigned long long>(seed), iterations,
                    iterations);
        return 0;
    }

    Rng rng(seed);
    long long compiled_ok = 0;
    for(long long i = 0; i < iterations; ++i) {
        std::string source;
        L6Case l6_case;
        if(l6_only) {
            l6_case = structured_sfc_case(rng, i);
            source = l6_case.source;
        } else if(l5_only) {
            source = structured_configuration(rng);
        } else if(l2b_only) {
            source = structured_pou_project(rng);
        } else {
            switch(rng.below(3)) {
            case 0: source = structured_program(rng); break;
            case 1: source = token_soup(rng); break;
            default: source = mutated_seed(rng); break;
            }
        }
        const plcopen::core::st::CompileResult result = l6_only
            ? plcopen::core::st::compile(source, l6_case.options)
            : plcopen::core::st::compile(source);
        if(l6_only) {
            if(result.ok) ++compiled_ok;
            if(!run_l6_case(l6_case, result, rng, i)) return 1;
            continue;
        }
        if(result.ok) {
            ++compiled_ok;
            if(l5_only) {
                if(result.program.configurations.empty()) continue;
                const std::string &configuration =
                    result.program.configurations.front().name;
                plcopen::core::st::TaskingReport report{};
                if(result.program.tasking_report(configuration.c_str(),
                                                 report) !=
                   plcopen::core::rt::ErrorCode::ok) {
                    std::printf("FUZZ_FAIL L5 report at iteration %lld\n", i);
                    return 1;
                }
                plcopen::core::st::ConfigurationRuntime runtime;
                const plcopen::core::rt::ErrorCode loaded = runtime.load(
                    result.program, configuration.c_str(), runtime_buffer,
                    sizeof(runtime_buffer), kTaskPeriodNs);
                if(loaded != plcopen::core::rt::ErrorCode::ok) {
                    std::printf(
                        "FUZZ_FAIL L5 load at iteration %lld required=%llu error=%d\n",
                        i, static_cast<unsigned long long>(
                               report.required_runtime_bytes),
                        static_cast<int>(loaded));
                    return 1;
                }
                const unsigned char input[4] = {
                    static_cast<unsigned char>(rng.next()), 0, 0, 0};
                for(const plcopen::core::st::ResourceInfo &resource :
                    result.program.configurations.front().resources) {
                    if(runtime.submit_input(resource.name.c_str(), input,
                                            sizeof(input), 1) !=
                       plcopen::core::rt::ErrorCode::ok) {
                        std::printf("FUZZ_FAIL L5 input at iteration %lld\n",
                                    i);
                        return 1;
                    }
                }
                if(runtime.boundary(0) !=
                       plcopen::core::rt::ErrorCode::ok ||
                   runtime.run(kScanBudget) !=
                       plcopen::core::rt::ErrorCode::ok) {
                    std::printf("FUZZ_FAIL L5 run at iteration %lld\n", i);
                    return 1;
                }
                switch(rng.below(4)) {
                case 0: (void)runtime.reset_task("r0", "t0"); break;
                case 1: (void)runtime.restart_task("r0", "t0"); break;
                case 2:
                    (void)runtime.report_wallclock_exceeded("r0", "t0");
                    break;
                default: break;
                }
                if(runtime.boundary(1) !=
                       plcopen::core::rt::ErrorCode::ok ||
                   runtime.run(kScanBudget) !=
                       plcopen::core::rt::ErrorCode::ok) {
                    std::printf("FUZZ_FAIL L5 recovery at iteration %lld\n",
                                i);
                    return 1;
                }
                continue;
            }
            plcopen::core::st::Instance instance;
            const plcopen::core::rt::ErrorCode loaded = instance.load(
                result.program, runtime_buffer, sizeof(runtime_buffer),
                kTaskPeriodNs);
            if(loaded != plcopen::core::rt::ErrorCode::ok) {
                std::printf("FUZZ_FAIL load at iteration %lld required=%zu error=%d\n",
                            i, result.program.required_bytes(),
                            static_cast<int>(loaded));
                return 1;
            }
            const plcopen::core::st::ScanError scan =
                instance.scan(kScanBudget);
            if(!bounded_scan_error(scan)) {
                std::printf("FUZZ_FAIL scan at iteration %lld error=%d\n", i,
                            static_cast<int>(scan));
                return 1;
            }
        }
        // Crash-free is the contract; diagnostics content is not asserted
        // here. A defensive floor: a compile must always produce either a
        // program or at least one diagnostic.
        if(!result.ok && result.diagnostics.empty()) {
            std::printf("FUZZ_FAIL silent failure at iteration %lld\n", i);
            return 1;
        }
    }
    std::printf("FUZZ_PASS seed=0x%llX iterations=%lld ok=%lld\n",
                static_cast<unsigned long long>(seed), iterations,
                compiled_ok);
    return 0;
}
