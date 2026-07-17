// L0 quality gates (approved st-l0-semantics metrics 5.3/5.4/5.9):
// deterministic code generation (repeat + cross-platform anchor hash),
// zero-allocation scan under a freeze window, and the conservative
// interpreter-throughput gate (1e6 mixed instructions within 100 ms in
// Release; the gate catches structural regressions, not micro-tuning).
// Test binaries are load-domain: <chrono> is fine here.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "st/st.h"
#include "axis/state.h"

namespace
{

bool g_frozen = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_frozen) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void *operator new[](std::size_t size)
{
    if(g_frozen) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
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

using namespace plcopen::core;

static_assert(sizeof(st::Instance) <= 256,
              "Instance must keep binding registries out of the stack object");

int failures = 0;

void fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    ++failures;
}

void check(bool condition, const char *name)
{
    if(!condition) {
        fail(name);
    }
}

// Reference program exercising every op family; also the cross-platform
// determinism anchor (metric 5.3).
const char *kReference =
    "PROGRAM anchor\n"
    "VAR\n"
    "  i : INT; d : DINT; r : REAL; l : LREAL; t : TIME; b : BOOL;\n"
    "  n : INT; m : DINT; timer : TON; edge : R_TRIG; count : CTU;\n"
    "END_VAR\n"
    "n := n + 1;\n"
    "m := m + 1;\n"
    "i := (n * 3 - 1) MOD 100;\n"
    "d := d + 2147483000 / m;\n"
    "r := r + 0.125;\n"
    "l := l * 1.0001 + 0.5;\n"
    "t := t + T#1.5ms;\n"
    "b := (i > 10) AND NOT (d < 0) XOR (r >= 1.0);\n"
    "IF b THEN i := i + 1; ELSIF n = 2 THEN i := i - 1; ELSE i := 0; END_IF;\n"
    "CASE i OF 0: d := d + 1; 1..9: d := d + 2; ELSE d := d + 3; END_CASE;\n"
    "FOR n := n TO n DO ; END_FOR;\n"
    "timer(IN := b, PT := T#5ms);\n"
    "edge(CLK := timer.Q);\n"
    "count(CU := edge.Q, PV := 3);\n"
    "WHILE FALSE DO ; END_WHILE;\n"
    "REPEAT ; UNTIL TRUE END_REPEAT;\n"
    "END_PROGRAM\n";

// FNV-1a over the serialized program payload.
unsigned long long fnv1a(const st::Program &program)
{
    unsigned long long hash = 1469598103934665603ULL;
    const auto mix = [&hash](const unsigned char *data, std::size_t size) {
        for(std::size_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    };
    mix(program.code.data(), program.code.size());
    for(const std::uint64_t constant : program.constants) {
        unsigned char bytes[8];
        for(int i = 0; i < 8; ++i) {
            bytes[i] = static_cast<unsigned char>(
                (constant >> (8 * i)) & 0xFFULL);
        }
        mix(bytes, 8);
    }
    return hash;
}

void determinism()
{
    const st::CompileResult first = st::compile(kReference);
    check(first.ok, "reference compiles");
    if(!first.ok) {
        for(const st::Diagnostic &d : first.diagnostics) {
            std::printf("  diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
        }
        return;
    }
    // Metric 5.3: 100 repeat compiles, byte-identical code and constants.
    for(int i = 0; i < 100; ++i) {
        const st::CompileResult again = st::compile(kReference);
        if(!again.ok || again.program.code != first.program.code ||
           again.program.constants != first.program.constants) {
            fail("repeat compile determinism");
            return;
        }
    }
    // Cross-platform anchor (anchor L0-5.3-hash): Windows and Linux CI must
    // both produce exactly this byte stream. On a deliberate codegen change,
    // refresh the constant in the same commit and say so. If the platforms
    // ever disagree, the declared fallback is a self-authored deterministic
    // literal parser (matrix 2.5).
    const unsigned long long hash = fnv1a(first.program);
    std::printf("determinism anchor hash: %llu\n", hash);
    check(hash == 10850470790425961545ULL, "cross-platform anchor hash");

    // FOR bound temporaries and constant pool must not leak between
    // compiles of different sources (pool is value-keyed).
    const st::CompileResult other = st::compile(
        "PROGRAM q VAR x : INT; END_VAR x := 1; END_PROGRAM");
    check(other.ok, "secondary program compiles");

    // L1a extension anchor (anchor L1a-7.7-hash): exercises the appended
    // opcode families (iarith/cmp_u/bit_*/time_scale/power/conversions).
    // ** never folds, so the pool carries no libm result (matrix 5.2).
    const char *reference_l1a =
        "PROGRAM anchor2\n"
        "VAR\n"
        "  s8 : SINT; u64 : ULINT; w : WORD; l : LREAL; t : TIME;\n"
        "  n : INT; q : BOOL; d : DINT;\n"
        "END_VAR\n"
        "VAR CONSTANT k : DINT := 7; END_VAR\n"
        "n := n + 1;\n"
        "s8 := s8 + 1;\n"
        "u64 := u64 * 3 + 1;\n"
        "w := (w OR WORD#16#0F0F) XOR NOT w;\n"
        "l := 1.5 ** n;\n"
        "t := T#1s * 2 / 3;\n"
        "d := LREAL_TO_DINT(l) + DINT_TO_INT(k) + WORD_TO_DINT(w);\n"
        "q := u64 > 100;\n"
        "FOR n := 1 TO 3 DO CONTINUE; END_FOR;\n"
        "END_PROGRAM\n";
    const st::CompileResult l1a = st::compile(reference_l1a);
    check(l1a.ok, "L1a reference compiles");
    if(l1a.ok) {
        const unsigned long long l1a_hash = fnv1a(l1a.program);
        std::printf("determinism anchor hash (l1a): %llu\n", l1a_hash);
        check(l1a_hash == 15855232410053270438ULL,
              "cross-platform L1a anchor hash");
        for(int i = 0; i < 20; ++i) {
            const st::CompileResult again = st::compile(reference_l1a);
            if(!again.ok || again.program.code != l1a.program.code ||
               again.program.constants != l1a.program.constants) {
                fail("L1a repeat compile determinism");
                break;
            }
        }
    }
}

void zero_allocation_scan()
{
    const st::CompileResult compiled = st::compile(kReference);
    check(compiled.ok, "alloc program compiles");
    alignas(8) static unsigned char buffer[65536];
    st::Instance instance;
    check(instance.load(compiled.program, buffer, sizeof(buffer), 1000000) ==
              rt::ErrorCode::ok,
          "alloc program loads");
    // Warm-up scan outside the freeze window.
    check(instance.scan(1000000) == st::ScanError::ok, "warm-up scan");

    g_frozen = true;
    g_frozen_allocations = 0;
    for(int i = 0; i < 1000; ++i) {
        if(instance.scan(1000000) != st::ScanError::ok) {
            g_frozen = false;
            fail("frozen scan errored");
            return;
        }
    }
    g_frozen = false;
    if(g_frozen_allocations != 0) {
        std::printf("  %llu allocation(s) inside the freeze window\n",
                    g_frozen_allocations);
        fail("scan allocated");
    }
}

void zero_allocation_mc_scan()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM mc\nVAR AxisX : AXIS_REF; Power : MC_Power; "
        "Move : MC_MoveAbsolute; END_VAR\n"
        "Power(Axis := AxisX, Enable := TRUE);\n"
        "Move(Axis := AxisX, Execute := TRUE, ContinuousUpdate := FALSE, "
        "Position := 1.0, Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#current, "
        "BufferMode := MC_BUFFER_MODE#aborting);\nEND_PROGRAM\n");
    check(compiled.ok, "MC alloc program compiles");
    if(!compiled.ok) return;
    alignas(8) unsigned char buffer[4096]{};
    st::Instance instance;
    axis::AxisModel axis;
    check(instance.load(compiled.program, buffer, sizeof(buffer), 1000000) ==
              rt::ErrorCode::ok,
          "MC alloc program loads");
    check(instance.bind_axis("AxisX", &axis) == st::BindingError::ok,
          "MC alloc axis binds");
    g_frozen_allocations = 0;
    g_frozen = true;
    for(int i = 0; i < 1000; ++i) {
        if(instance.scan(256) != st::ScanError::ok) {
            g_frozen = false;
            fail("frozen MC scan errored");
            return;
        }
        axis.cycle();
    }
    g_frozen = false;
    check(g_frozen_allocations == 0, "MC scan allocated");
}

void interpreter_throughput()
{
    // A tight arithmetic loop: roughly 10 instructions per iteration; run
    // 100k iterations => ~1e6 instructions in one scan.
    const st::CompileResult compiled = st::compile(
        "PROGRAM p\n"
        "VAR i : DINT; s : DINT; END_VAR\n"
        "FOR i := 1 TO 100000 DO s := s + i * 2 - 1; END_FOR;\n"
        "END_PROGRAM\n");
    check(compiled.ok, "throughput program compiles");
    alignas(8) static unsigned char buffer[65536];
    st::Instance instance;
    check(instance.load(compiled.program, buffer, sizeof(buffer), 1000000) ==
              rt::ErrorCode::ok,
          "throughput program loads");

    const auto start = std::chrono::steady_clock::now();
    const st::ScanError error = instance.scan(10000000);
    const auto stop = std::chrono::steady_clock::now();
    check(error == st::ScanError::ok, "throughput scan completes");
    const long long us =
        std::chrono::duration_cast<std::chrono::microseconds>(stop - start)
            .count();
    std::printf("throughput: ~1e6 instructions in %lld us\n", us);
#if defined(NDEBUG) && !defined(PLCOPEN_CROSS_COMPILED_TEST)
    // Metric 5.9 gates native Release runs; Debug and cross-emulated numbers
    // are reported only because emulator overhead is not target performance.
    check(us <= 100000, "1e6 instructions within 100 ms (Release)");
#endif
}

} // namespace

int main()
{
    determinism();
    zero_allocation_scan();
    zero_allocation_mc_scan();
    interpreter_throughput();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l0 quality tests passed\n");
    return 0;
}
