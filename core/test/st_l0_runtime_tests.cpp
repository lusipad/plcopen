// L0 VM + FB binding acceptance (approved st-l0-semantics 3.x, metrics
// 5.5/5.6/5.7): execution semantics, fault state machine with
// assignments-preserved contract, exact instruction-budget boundary, timer
// ceil quantization, counter/edge blocks. Plain-main + fail() style.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "st/st.h"

namespace
{

using namespace plcopen::core;

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

constexpr std::int64_t kPeriodNs = 1000000; // 1 ms task period
constexpr std::int64_t kBigBudget = 1000000;

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[65536] = {};

    bool build(const std::string &source,
               std::int64_t period_ns = kPeriodNs)
    {
        compiled = st::compile(source);
        if(!compiled.ok) {
            for(const st::Diagnostic &d : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", d.line, d.column,
                            d.message.c_str());
            }
            return false;
        }
        return instance.load(compiled.program, buffer, sizeof(buffer),
                             period_ns) == rt::ErrorCode::ok;
    }

    st::ScanError scan(std::int64_t budget = kBigBudget)
    {
        return instance.scan(budget);
    }

    std::int64_t i64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? -999999 : instance.value_i64(
                                         static_cast<std::size_t>(index));
    }

    double f64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? -999999.0
                         : instance.value_f64(static_cast<std::size_t>(index));
    }
};

std::string wrap(const char *vars, const char *body)
{
    std::string source = "PROGRAM p\nVAR\n";
    source += vars;
    source += "\nEND_VAR\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

// --- load contract (matrix 3.2/2.6, anchors L0-3.2-load / L0-2.6-version) ---

void load_contract()
{
    st::CompileResult compiled =
        st::compile(wrap("x : INT;", "x := 1;"));
    check(compiled.ok, "load contract program compiles");

    alignas(8) static unsigned char buffer[4096];
    st::Instance instance;
    check(instance.scan(10) == st::ScanError::not_loaded,
          "scan before load reports not_loaded");
    check(instance.load(compiled.program, nullptr, 4096, kPeriodNs) ==
              rt::ErrorCode::invalid_argument,
          "null buffer rejected");
    check(instance.load(compiled.program, buffer, 4096, 0) ==
              rt::ErrorCode::invalid_argument,
          "zero period rejected");
    check(instance.load(compiled.program, buffer + 1, 4095, kPeriodNs) ==
              rt::ErrorCode::invalid_argument,
          "misaligned buffer rejected");
    check(instance.load(compiled.program, buffer, 8, kPeriodNs) ==
              rt::ErrorCode::capacity_exceeded,
          "undersized buffer rejected");

    st::Program tampered = compiled.program;
    tampered.format_version = 999;
    check(instance.load(tampered, buffer, 4096, kPeriodNs) ==
              rt::ErrorCode::unsupported,
          "format version mismatch rejected");

    check(instance.load(compiled.program, buffer, 4096, kPeriodNs) ==
              rt::ErrorCode::ok,
          "valid load succeeds");
    check(instance.scan(kBigBudget) == st::ScanError::ok, "scan runs");
}

// --- arithmetic semantics (matrix 1.8-1.10, anchor L0-1.8-wrap) -------------

void arithmetic()
{
    Rig rig;
    check(rig.build(wrap(
              "a : INT; b : INT; c : DINT; d : DINT; t : TIME;"
              "r : REAL; l : LREAL; q : BOOL;",
              "a := 32767; a := a + 1;"           // INT wrap
              "b := -32768; b := b - 1;"          // INT wrap down
              "c := 2147483647; c := c + 1;"      // DINT wrap
              "d := 7 / 2;"                        // trunc toward zero
              "d := d + (-7) / 2;"                 // -3 => 3 + (-3) = 0
              "d := d + (-7 MOD 2);"               // MOD sign follows dividend
              "t := T#1s + T#500ms;"
              "r := 16777216.0 + 1.0;"             // f32 grid: stays 2^24
              "l := 16777216.0 + 1.0;"             // f64 keeps the +1
              "q := (0.0 / 0.0) = (0.0 / 0.0);")), // NaN never equal
          "arithmetic program builds");
    check(rig.scan() == st::ScanError::ok, "arithmetic scan ok");
    check(rig.i64("a") == -32768, "INT wrap up");
    check(rig.i64("b") == 32767, "INT wrap down");
    check(rig.i64("c") == -2147483647LL - 1, "DINT wrap");
    check(rig.i64("d") == -1, "trunc division and MOD sign");
    check(rig.i64("t") == 1500000000LL, "TIME add ns");
    check(rig.f64("r") == 16777216.0, "REAL binary32 grid");
    check(rig.f64("l") == 16777217.0, "LREAL binary64 keeps increment");
    check(rig.i64("q") == 0, "NaN equality false");

    Rig inf;
    check(inf.build(wrap("l : LREAL; q : BOOL;",
                         "l := 1.0 / 0.0; q := l > 0.0;")),
          "IEEE division program builds");
    check(inf.scan() == st::ScanError::ok, "float div by zero no fault");
    check(inf.i64("q") == 1, "positive infinity ordering");
}

// --- control flow (matrix 1.3, anchor L0-1.3-flow) ---------------------------

void control_flow()
{
    Rig rig;
    check(rig.build(wrap(
              "x : INT; y : INT; s : INT; i : INT; k : INT; w : INT;",
              "x := 7;"
              "IF x > 10 THEN y := 1;"
              "ELSIF x > 5 THEN y := 2;"
              "ELSE y := 3; END_IF;"
              "CASE x OF"
              " 1: k := 10;"
              " 5..8: k := 20;"
              " ELSE k := 30;"
              "END_CASE;"
              "FOR i := 1 TO 10 BY 2 DO s := s + i; END_FOR;" // 1+3+5+7+9
              "WHILE w < 5 DO w := w + 1; END_WHILE;"
              "REPEAT w := w + 10; UNTIL w >= 15 END_REPEAT;")),
          "control flow builds");
    check(rig.scan() == st::ScanError::ok, "control flow scan ok");
    check(rig.i64("y") == 2, "ELSIF branch");
    check(rig.i64("k") == 20, "CASE range arm");
    check(rig.i64("s") == 25, "FOR BY 2 sum");
    check(rig.i64("i") == 11, "FOR control after loop");
    check(rig.i64("w") == 15, "WHILE then REPEAT");

    Rig exits;
    check(exits.build(wrap(
              "i : INT; s : INT; z : INT;",
              "FOR i := 1 TO 100 DO"
              "  IF i > 3 THEN EXIT; END_IF;"
              "  s := s + i;"
              "END_FOR;"
              "z := 1;"
              "RETURN;"
              "z := 2;")),
          "exit/return builds");
    check(exits.scan() == st::ScanError::ok, "exit/return scan ok");
    check(exits.i64("s") == 6, "EXIT stops the loop");
    check(exits.i64("z") == 1, "RETURN ends the scan");

    Rig descending;
    check(descending.build(wrap(
              "i : DINT; s : DINT;",
              "FOR i := 5 TO 1 BY -1 DO s := s + i; END_FOR;"
              "FOR i := 5 TO 1 DO s := s + 1000; END_FOR;")), // zero pass
          "descending FOR builds");
    check(descending.scan() == st::ScanError::ok, "descending scan ok");
    check(descending.i64("s") == 15, "descending sum, empty ascending");
}

// --- fault machine (matrix 3.7, metric 5.7, anchor L0-3.7-fault) -------------

void fault_machine()
{
    // Runtime integer division by zero: assignments before the fault stay,
    // the fault latches, reset() recovers.
    Rig rig;
    check(rig.build(wrap("a : INT; d : INT; z : INT;",
                         "a := 11; z := a / d; a := 99;")),
          "div fault program builds");
    check(rig.scan() == st::ScanError::division_by_zero, "div zero faults");
    check(rig.i64("a") == 11, "assignment before fault preserved");
    check(rig.scan() == st::ScanError::division_by_zero,
          "fault latches on next scan");
    rig.instance.reset();
    check(rig.instance.fault() == st::ScanError::ok, "reset clears fault");
    const int slot = rig.instance.find("d");
    check(slot >= 0, "find d");
    // d is still zero, so the fault recurs -- set through a fresh program
    // path instead: rebuild with nonzero divisor to prove recovery scans.
    Rig healthy;
    check(healthy.build(wrap("a : INT; d : INT := 2; z : INT;",
                             "a := 11; z := a / d; a := 99;")),
          "healthy divisor builds");
    check(healthy.scan() == st::ScanError::ok, "healthy scan ok");
    check(healthy.i64("z") == 5, "division result");
    check(healthy.i64("a") == 99, "post-division assignment");

    // Runtime FOR step of zero through a variable (anchor L0-1.12-step0).
    Rig step;
    check(step.build(wrap("i : INT; b : INT; s : INT;",
                          "FOR i := 1 TO 3 BY b DO s := s + 1; END_FOR;")),
          "variable step builds");
    check(step.scan() == st::ScanError::for_step_zero,
          "runtime BY 0 faults");
    step.instance.reset();
    check(step.instance.fault() == st::ScanError::ok, "step fault resets");
}

st::ScanError scan_tampered(const st::Program &base,
                            const std::vector<std::uint8_t> &code,
                            const std::vector<std::uint64_t> &constants = {})
{
    st::Program program = base;
    program.code = code;
    program.constants = constants;
    program.stack_slots = 4;
    alignas(8) unsigned char buffer[4096] = {};
    st::Instance instance;
    if(instance.load(program, buffer, sizeof(buffer), kPeriodNs) !=
       rt::ErrorCode::ok) {
        return st::ScanError::not_loaded;
    }
    return instance.scan(32);
}

// Malformed bytecode is a load-domain input, but every malformed operand
// still has to fail closed in scan() without reading outside the declared
// variable image or evaluation stack.
void invalid_bytecode_contract()
{
    const st::CompileResult compiled =
        st::compile(wrap("x : INT;", "x := 1;"));
    check(compiled.ok, "invalid-bytecode base program compiles");
    if(!compiled.ok) {
        return;
    }
    const auto op = [](st::Op value) {
        return static_cast<std::uint8_t>(value);
    };

    check(scan_tampered(compiled.program, {op(st::Op::load_var)}) ==
              st::ScanError::invalid_bytecode,
          "truncated u16 operand rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::push_const), 1, 0, op(st::Op::halt)},
                        {7}) == st::ScanError::invalid_bytecode,
          "constant index outside pool rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::load_var), 1, 0, op(st::Op::halt)}) ==
              st::ScanError::invalid_bytecode,
          "load outside declared variable image rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::push_const), 0, 0,
                         op(st::Op::store_var), 1, 0, op(st::Op::halt)},
                        {7}) == st::ScanError::invalid_bytecode,
          "store outside declared variable image rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::for_guard), 1, 0, op(st::Op::halt)}) ==
              st::ScanError::invalid_bytecode,
          "FOR guard outside variable image rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::for_test), 0, 0, 0, 0, 1, 0,
                         op(st::Op::halt)}) ==
              st::ScanError::invalid_bytecode,
          "FOR test outside variable image rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::for_step_int), 1, 0, 0, 0,
                         op(st::Op::halt)}) ==
              st::ScanError::invalid_bytecode,
          "FOR step outside variable image rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::add_int), op(st::Op::halt)}) ==
              st::ScanError::invalid_bytecode,
          "stack underflow rejected");
    check(scan_tampered(compiled.program, {0xFF}) ==
              st::ScanError::invalid_bytecode,
          "unknown opcode rejected");
    check(scan_tampered(compiled.program,
                        {op(st::Op::jmp), 0xFF, 0xFF, 0xFF, 0x7F}) ==
              st::ScanError::invalid_bytecode,
          "jump outside bytecode rejected");

    const st::CompileResult fb_compiled =
        st::compile(wrap("edge : R_TRIG;", "edge(CLK := TRUE);"));
    check(fb_compiled.ok, "invalid-bytecode FB base program compiles");
    if(fb_compiled.ok) {
        check(scan_tampered(
                  fb_compiled.program,
                  {op(st::Op::push_const), 0, 0, op(st::Op::fb_store_in),
                   0, 0, 0xFF, op(st::Op::halt)},
                  {1}) == st::ScanError::invalid_bytecode,
              "FB input pin outside table rejected");
        check(scan_tampered(fb_compiled.program,
                            {op(st::Op::fb_load_out), 0, 0, 0xFF,
                             op(st::Op::halt)}) ==
                  st::ScanError::invalid_bytecode,
              "FB output pin outside table rejected");
        check(scan_tampered(fb_compiled.program,
                            {op(st::Op::fb_call), 1, 0,
                             op(st::Op::halt)}) ==
                  st::ScanError::invalid_bytecode,
              "FB instance outside table rejected");
    }
}

// --- instruction budget (matrix 3.6, metric 5.5, anchor L0-3.6-budget) -------

void budget_boundary()
{
    const std::string halt_source = wrap("x : INT;", "");
    Rig halt_zero;
    Rig halt_one;
    check(halt_zero.build(halt_source) && halt_one.build(halt_source),
          "halt budget rigs build");
    check(halt_zero.scan(0) == st::ScanError::budget_exceeded,
          "halt needs more than zero budget");
    check(halt_one.scan(1) == st::ScanError::ok,
          "halt completes with one budget");

    const std::string source = wrap(
        "a : INT; b : INT;",
        "a := 1; b := a + 2; a := b * 3; IF a > 0 THEN b := b + 1; END_IF;");

    // Find the minimal budget that completes on a fresh instance.
    std::int64_t needed = -1;
    for(std::int64_t budget = 1; budget <= 200; ++budget) {
        Rig probe;
        if(!probe.build(source)) {
            fail("budget probe build");
            return;
        }
        if(probe.scan(budget) == st::ScanError::ok) {
            needed = budget;
            break;
        }
    }
    check(needed > 0, "program completes under some budget");

    Rig exact;
    Rig under;
    check(exact.build(source) && under.build(source), "budget rigs build");
    check(exact.scan(needed) == st::ScanError::ok,
          "budget N completes");
    check(under.scan(needed - 1) == st::ScanError::budget_exceeded,
          "budget N-1 faults");
    check(under.instance.fault() == st::ScanError::budget_exceeded,
          "budget fault latches");

    // A runaway loop is stopped by the budget alone (watchdog contract).
    Rig runaway;
    check(runaway.build(wrap("w : INT;",
                             "WHILE TRUE DO w := w + 0; END_WHILE;")),
          "runaway builds");
    check(runaway.scan(10000) == st::ScanError::budget_exceeded,
          "runaway loop hits the budget");
}

// --- timers (matrix 3.4, metric 5.6, anchor L0-3.4-timer) ---------------------

void timers()
{
    // TON boundary cases at 1 ms period: PT=0 (immediate on IN),
    // PT < period (one scan), PT = 2.5 periods (ceil => 3 scans),
    // IN drop resets.
    Rig ton;
    check(ton.build(wrap("t : TON; q : BOOL; et : TIME;",
                         "t(IN := TRUE, PT := T#2.5ms);"
                         "q := t.Q; et := t.ET;")),
          "TON program builds");
    check(ton.scan() == st::ScanError::ok && ton.i64("q") == 0, "TON scan1 low");
    check(ton.scan() == st::ScanError::ok && ton.i64("q") == 0, "TON scan2 low");
    check(ton.scan() == st::ScanError::ok && ton.i64("q") == 1,
          "TON scan3 high (ceil of 2.5 periods)");

    Rig ton_zero;
    check(ton_zero.build(wrap("t : TON; q : BOOL;",
                              "t(IN := TRUE, PT := T#0s); q := t.Q;")),
          "TON PT=0 builds");
    check(ton_zero.scan() == st::ScanError::ok && ton_zero.i64("q") == 1,
          "TON PT=0 immediate");

    Rig ton_sub;
    check(ton_sub.build(wrap("t : TON; q : BOOL;",
                             "t(IN := TRUE, PT := T#100us); q := t.Q;")),
          "TON PT<period builds");
    check(ton_sub.scan() == st::ScanError::ok && ton_sub.i64("q") == 1,
          "TON sub-period fires on first scan");

    // IN drop resets Q and ET (uses a flag variable flipped by the test
    // program itself across scans).
    Rig drop;
    check(drop.build(wrap(
              "t : TON; en : BOOL := TRUE; n : INT; q : BOOL; et : TIME;",
              "n := n + 1;"
              "IF n = 3 THEN en := FALSE; END_IF;"
              "t(IN := en, PT := T#10ms);"
              "q := t.Q; et := t.ET;")),
          "TON drop builds");
    check(drop.scan() == st::ScanError::ok, "drop scan1");
    check(drop.scan() == st::ScanError::ok, "drop scan2");
    check(drop.i64("et") > 0, "ET accumulating");
    check(drop.scan() == st::ScanError::ok, "drop scan3");
    check(drop.i64("q") == 0 && drop.i64("et") == 0,
          "IN drop resets Q and ET");
}

// --- counters and edges (metric 5.6, anchor L0-3.8-blocks) --------------------

void counters_edges()
{
    // CTU counts rising edges only; R resets; Q at PV. The program toggles
    // CU every scan (odd scans high).
    Rig ctu;
    check(ctu.build(wrap(
              "c : CTU; n : INT; pulse : BOOL; q : BOOL; cv : DINT;",
              "n := n + 1;"
              "pulse := (n MOD 2) = 1;"
              "c(CU := pulse, PV := 2);"
              "q := c.Q; cv := c.CV;")),
          "CTU builds");
    // basic.h contract: the first evaluation initializes edge memory and
    // does not count (documented as-is).
    check(ctu.scan() == st::ScanError::ok && ctu.i64("cv") == 0,
          "CTU first scan initializes only");
    check(ctu.scan() == st::ScanError::ok && ctu.i64("cv") == 0,
          "CTU low scan holds");
    check(ctu.scan() == st::ScanError::ok && ctu.i64("cv") == 1,
          "CTU counts second rising edge");
    check(ctu.scan() == st::ScanError::ok && ctu.i64("cv") == 1,
          "CTU holds between edges");
    check(ctu.scan() == st::ScanError::ok && ctu.i64("cv") == 2,
          "CTU reaches PV");
    check(ctu.i64("q") == 1, "CTU Q at PV");

    // R_TRIG single-cycle pulse (documenting the basic.h first-scan rule:
    // CLK true on the very first evaluation reports a rising edge).
    Rig trig;
    check(trig.build(wrap(
              "e : R_TRIG; n : INT; q : BOOL; hits : INT;",
              "n := n + 1;"
              "e(CLK := n >= 2);"
              "q := e.Q;"
              "IF q THEN hits := hits + 1; END_IF;")),
          "R_TRIG builds");
    check(trig.scan() == st::ScanError::ok && trig.i64("q") == 0,
          "R_TRIG low before edge");
    check(trig.scan() == st::ScanError::ok && trig.i64("q") == 1,
          "R_TRIG fires on edge");
    check(trig.scan() == st::ScanError::ok && trig.i64("q") == 0,
          "R_TRIG one-cycle pulse");
    check(trig.i64("hits") == 1, "R_TRIG single hit");

    // SR dominance and unassigned inputs holding last values (matrix 3.9).
    Rig sr;
    check(sr.build(wrap(
              "f : SR; n : INT; q : BOOL;",
              "n := n + 1;"
              "IF n = 1 THEN f(S1 := TRUE, R := FALSE); END_IF;"
              "IF n = 2 THEN f(S1 := FALSE); END_IF;" // R keeps FALSE
              "IF n = 3 THEN f(R := TRUE); END_IF;"   // S1 keeps FALSE
              "q := f.Q1;")),
          "SR builds");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 1, "SR sets");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 1,
          "SR holds with unassigned R");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 0, "SR resets");

    // CTUD both directions.
    Rig ctud;
    check(ctud.build(wrap(
              "c : CTUD; n : INT; up : BOOL; down : BOOL; cv : DINT;",
              "n := n + 1;"
              "up := n = 2;"
              "down := n = 4;"
              "c(CU := up, CD := down, PV := 10);"
              "cv := c.CV;")),
          "CTUD builds");
    check(ctud.scan() == st::ScanError::ok, "CTUD scan1");
    check(ctud.scan() == st::ScanError::ok && ctud.i64("cv") == 1,
          "CTUD counts up");
    check(ctud.scan() == st::ScanError::ok && ctud.i64("cv") == 1,
          "CTUD holds");
    check(ctud.scan() == st::ScanError::ok && ctud.i64("cv") == 0,
          "CTUD counts down");
}

// --- symbol access (matrix 3.13, anchor L0-3.13-symbols) ----------------------

void symbols()
{
    Rig rig;
    check(rig.build(wrap("Counter : DINT := 41; Flag : BOOL := TRUE;",
                         "Counter := Counter + 1;")),
          "symbol program builds");
    check(rig.scan() == st::ScanError::ok, "symbol scan");
    check(rig.instance.variable_count() == 2, "variable count");
    check(rig.instance.find("counter") >= 0, "lower-case lookup");
    check(rig.instance.find("COUNTER") >= 0, "upper-case lookup");
    check(rig.instance.find("missing") < 0, "missing lookup");
    check(rig.i64("Counter") == 42, "value readback");
    const int flag = rig.instance.find("flag");
    check(flag >= 0 &&
              rig.instance.value_bool(static_cast<std::size_t>(flag)),
          "bool readback");
    const st::VarInfo *info =
        rig.instance.variable(static_cast<std::size_t>(flag));
    check(info != nullptr && info->type == st::Type::bool_, "type readback");
}

} // namespace

int main()
{
    load_contract();
    arithmetic();
    control_flow();
    fault_machine();
    invalid_bytecode_contract();
    budget_boundary();
    timers();
    counters_edges();
    symbols();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l0 runtime tests passed\n");
    return 0;
}
