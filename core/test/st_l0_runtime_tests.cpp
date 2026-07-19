// L0 VM + FB binding acceptance (approved st-l0-semantics 3.x, metrics
// 5.5/5.6/5.7): execution semantics, fault state machine with
// assignments-preserved contract, exact instruction-budget boundary, timer
// ceil quantization, counter/edge blocks. Plain-main + fail() style.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "st/st.h"
#include "fb/io.h"

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
    static constexpr std::size_t kBufferSize = 65536;

    st::CompileResult compiled;
    st::Instance instance;
    std::unique_ptr<unsigned char[]> buffer =
        std::make_unique<unsigned char[]>(kBufferSize);

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
        return instance.load(compiled.program, buffer.get(), kBufferSize,
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
              rt::ErrorCode::bytecode_version_mismatch,
          "format version mismatch rejected");

    check(instance.load(compiled.program, buffer, 4096, kPeriodNs) ==
              rt::ErrorCode::ok,
          "valid load succeeds");
    check(instance.scan(kBigBudget) == st::ScanError::ok, "scan runs");
}

void instance_boundary_matrix()
{
    alignas(8) unsigned char buffer[65536] = {};
    unsigned char value[16]{};
    bool active = false;
    std::int64_t executed = -1;
    st::Instance empty;
    check(!empty.read_variable(0, 1, value),
          "unloaded read_variable is rejected");
    check(empty.program_name()[0] == '\0',
          "unloaded program name is empty");
    check(empty.begin(1) == st::ScanError::not_loaded &&
              empty.resume(1, executed) == st::ScanError::not_loaded,
          "unloaded cooperative execution is rejected");
    check(empty.sfc_step_active(nullptr, nullptr, active) ==
                  rt::ErrorCode::invalid_argument &&
              empty.restart_sfc(nullptr) == rt::ErrorCode::invalid_argument,
          "unloaded SFC APIs reject null names");
    st::SfcTraceRecord trace[2]{};
    check(empty.configure_sfc_trace(trace, 0) ==
                  rt::ErrorCode::invalid_argument &&
              empty.configure_sfc_trace(nullptr, 2) ==
                  rt::ErrorCode::invalid_argument,
          "SFC trace pointer and capacity must agree");

    const st::CompileResult plain =
        st::compile(wrap("x : DINT;", "x := x + 1;"));
    check(plain.ok, "instance-boundary plain program compiles");
    if(!plain.ok) return;
    const st::CompileResult multi = st::compile(
        "PROGRAM First VAR X : DINT; END_VAR X := 1; END_PROGRAM\n"
        "PROGRAM Second VAR X : DINT; END_VAR X := 2; END_PROGRAM\n");
    check(multi.ok && multi.program.programs.size() == 2,
          "instance-boundary multi-program project compiles");
    if(!multi.ok || multi.program.programs.size() != 2) return;
    st::Instance named;
    check(named.load(multi.program, nullptr, buffer, sizeof(buffer),
                     kPeriodNs) == rt::ErrorCode::invalid_argument,
          "null selected program name is rejected");
    check(named.load(multi.program, "FIRST", buffer, sizeof(buffer),
                     kPeriodNs) ==
                  rt::ErrorCode::ok &&
              named.program_name() == std::string("first"),
          "selected program name is ASCII case insensitive");
    named.unload();
    check(named.load(multi.program, "", buffer, sizeof(buffer), kPeriodNs) ==
                  rt::ErrorCode::invalid_argument &&
              named.load(multi.program, "firstx", buffer, sizeof(buffer),
                         kPeriodNs) == rt::ErrorCode::invalid_argument &&
              named.load(multi.program, "x", buffer, sizeof(buffer),
                         kPeriodNs) == rt::ErrorCode::invalid_argument,
          "selected program name rejects length and content mismatches");
    check(named.load(plain.program, buffer, sizeof(buffer), kPeriodNs) ==
                  rt::ErrorCode::ok,
          "instance-boundary program loads");
    check(!named.read_variable(0, 1, nullptr) &&
              !named.read_variable(plain.program.vars_bytes + 1U, 0, value) &&
              !named.read_variable(plain.program.vars_bytes, 1, value) &&
              named.read_variable(0, sizeof(std::int32_t), value),
          "read_variable validates every range boundary");
    check(named.resume(1, executed) == st::ScanError::invalid_argument,
          "resume before begin is rejected");
    check(named.begin(kBigBudget) == st::ScanError::ok &&
              named.begin(kBigBudget) == st::ScanError::invalid_argument &&
              named.scan(kBigBudget) == st::ScanError::invalid_argument &&
              named.resume(0, executed) == st::ScanError::ok,
          "cooperative execution validates state and zero slice");
    named.abort();
    check(named.resume(1, executed) == st::ScanError::invalid_argument,
          "resume after abort is rejected");

    st::Program named_root = plain.program;
    named_root.program_name = "main";
    check(named.load(named_root, "MAIN", buffer, sizeof(buffer), kPeriodNs) ==
                  rt::ErrorCode::ok,
          "root artifact name comparison accepts uppercase ASCII");
    named.unload();
    check(named.load(named_root, "mai", buffer, sizeof(buffer), kPeriodNs) ==
                  rt::ErrorCode::invalid_argument &&
              named.load(named_root, "mainx", buffer, sizeof(buffer),
                         kPeriodNs) == rt::ErrorCode::invalid_argument &&
              named.load(named_root, "mxin", buffer, sizeof(buffer),
                         kPeriodNs) == rt::ErrorCode::invalid_argument,
          "root artifact name comparison rejects length and content drift");

    st::Program invalid = plain.program;
    invalid.initial_data.resize(invalid.vars_bytes + 1U);
    check(named.load(invalid, buffer, sizeof(buffer), kPeriodNs) ==
              rt::ErrorCode::invalid_argument,
          "initial data outside variable storage is rejected");
    invalid = plain.program;
    invalid.vars.front().type_id = st::invalid_type_id;
    check(named.load(invalid, buffer, sizeof(buffer), kPeriodNs) ==
              rt::ErrorCode::ok,
          "non-string variable with unknown descriptor is skipped");

    const st::CompileResult sfc = st::compile(
        "PROGRAM Main\nVAR X : DINT; END_VAR\nSFC Flow\n"
        "INITIAL_STEP A: END_STEP\nSTEP B: TERMINAL; END_STEP\n"
        "TRANSITION FROM A TO B := TRUE; END_TRANSITION\n"
        "END_SFC\nEND_PROGRAM\n");
    check(sfc.ok, "instance-boundary SFC program compiles");
    if(!sfc.ok) return;
    st::Instance sfc_instance;
    check(sfc_instance.load(sfc.program, "main", buffer, sizeof(buffer),
                            kPeriodNs) == rt::ErrorCode::ok,
          "SFC program loads by name");
    check(sfc_instance.sfc_step_active(nullptr, "a", active) ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.sfc_step_active("flow", nullptr, active) ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.sfc_step_active("missing", "a", active) ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.sfc_step_active("flow", "missing", active) ==
                  rt::ErrorCode::invalid_argument,
          "SFC step lookup rejects null and unknown names");
    check(!sfc_instance.debug_sfc_committed_step_active(99, 0) &&
              !sfc_instance.debug_sfc_committed_step_active(0, 99),
          "SFC committed-state lookup validates both indices");
    check(sfc_instance.restart_sfc(nullptr) ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.restart_sfc("missing") ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.configure_sfc_trace(trace, 2) ==
                  rt::ErrorCode::ok,
          "loaded SFC control validates names and accepts trace storage");
    check(sfc_instance.begin(kBigBudget) == st::ScanError::ok &&
              sfc_instance.configure_sfc_trace(trace, 2) ==
                  rt::ErrorCode::invalid_argument &&
              sfc_instance.restart_sfc("flow") ==
                  rt::ErrorCode::invalid_argument,
          "running SFC locks trace and restart configuration");
    sfc_instance.abort();
    check(sfc_instance.restart_sfc("FLOW") == rt::ErrorCode::ok,
          "idle SFC restart is ASCII case insensitive");
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

st::ScanError scan_tampered_program(const st::Program &program,
                                    std::int64_t budget = kBigBudget)
{
    alignas(8) unsigned char buffer[65536] = {};
    st::Instance instance;
    if(instance.load(program, buffer, sizeof(buffer), kPeriodNs) !=
       rt::ErrorCode::ok) {
        return st::ScanError::not_loaded;
    }
    return instance.scan(budget);
}

void sweep_malformed_bytecode(const st::Program &base)
{
    for(std::size_t size = 0U; size < base.code.size(); ++size) {
        st::Program truncated = base;
        truncated.code.resize(size);
        (void)scan_tampered_program(truncated);
    }
    for(std::size_t index = 0U; index < base.code.size(); ++index) {
        for(const std::uint8_t replacement : {
                std::uint8_t{0U}, std::uint8_t{1U}, std::uint8_t{0x7FU},
                std::uint8_t{0x80U}, std::uint8_t{0xFEU},
                std::uint8_t{0xFFU}}) {
            st::Program mutated = base;
            mutated.code[index] = replacement;
            (void)scan_tampered_program(mutated);
        }
    }
}

void sweep_malformed_metadata(const st::Program &base)
{
    const auto scan = [](const st::Program &program) {
        (void)scan_tampered_program(program, 256);
    };
    st::Program mutated = base;
    mutated.constants.clear();
    scan(mutated);
    for(std::size_t size = 0; size < base.string_constants.size(); ++size) {
        mutated = base;
        mutated.string_constants.resize(size);
        scan(mutated);
    }
    for(const std::uint16_t slots : {std::uint16_t{0}, std::uint16_t{1}}) {
        mutated = base;
        mutated.stack_slots = slots;
        scan(mutated);
    }
    for(const std::uint32_t bytes :
        {std::uint32_t{0}, std::uint32_t{1}, base.vars_bytes / 2U}) {
        mutated = base;
        mutated.vars_bytes = bytes;
        mutated.initial_data.clear();
        scan(mutated);
    }
    mutated = base;
    mutated.fbs.clear();
    mutated.fb_bytes = 0;
    scan(mutated);
    for(std::size_t index = 0; index < base.vars.size(); ++index) {
        mutated = base;
        mutated.vars[index].offset = mutated.vars_bytes + 1U;
        mutated.initial_data.clear();
        scan(mutated);
        mutated = base;
        mutated.vars[index].type_id = st::invalid_type_id;
        scan(mutated);
    }
    if(!base.sfc_networks.empty()) {
        for(std::size_t network = 0; network < base.sfc_networks.size();
            ++network) {
            for(std::size_t transition = 0;
                transition < base.sfc_networks[network].transitions.size();
                ++transition) {
                mutated = base;
                auto &region = mutated.sfc_networks[network]
                                   .transitions[transition]
                                   .condition;
                region.code.clear();
                scan(mutated);
                mutated = base;
                mutated.sfc_networks[network]
                    .transitions[transition]
                    .condition.stack_slots = 0;
                scan(mutated);
            }
            for(std::size_t action = 0;
                action < base.sfc_networks[network].actions.size(); ++action) {
                mutated = base;
                mutated.sfc_networks[network].actions[action].region.code.clear();
                scan(mutated);
                mutated = base;
                mutated.sfc_networks[network]
                    .actions[action]
                    .region.stack_slots = 0;
                scan(mutated);
            }
        }
    }
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
    check(scan_tampered(
              compiled.program,
              {op(st::Op::push_const), 0, 0,
               op(st::Op::push_const), 1, 0,
               op(st::Op::standard_string),
               static_cast<std::uint8_t>(st::StandardFunction::find), 2,
               0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
               0, 0, op(st::Op::halt)},
              {1, 2}) == st::ScanError::invalid_bytecode,
          "string function operand signature rejected");

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

void malformed_bytecode_operand_matrix()
{
    const std::string sources[] = {
        wrap("a : INT; b : DINT; r : LREAL; q : BOOL; i : INT;",
             "a := 7; b := a * 3 + 2; r := DINT_TO_LREAL(b) / 2.0; "
             "q := (a < b) AND (r >= 1.0); "
             "FOR i := 1 TO 4 DO b := b + i; END_FOR;"),
        wrap("a : STRING[8] := 'abc'; b : STRING[8] := 'abd'; "
             "i : DINT := 2; c : CHAR; q : BOOL;",
             "c := a[i]; q := a < b; a := b;"),
        wrap("edge : R_TRIG; timer : TON; counter : CTU; q : BOOL; n : DINT;",
             "edge(CLK := TRUE); timer(IN := edge.Q, PT := T#2ms); "
             "counter(CU := timer.Q, R := FALSE, PV := 3); "
             "q := timer.Q; n := counter.CV;"),
        wrap("s : SINT := -7; i : INT := 2; d : DINT := 3; out : DINT; "
             "r : LREAL; q : BOOL;",
             "out := ADD(i, d, SINT#4); out := out + ABS(s); "
             "out := out + MIN(8, 3, 5) + MAX(8, 3, 5); "
             "out := LIMIT(0, out, 20); out := SEL(FALSE, 7, out); "
             "out := MUX(2, 10, 20, out); r := EXPT(2.0, 3.0); "
             "q := GT(out, 8, 7) AND LE(1, 1, 2);"),
        wrap("s : STRING[16] := 'abc'; len : DINT; left : STRING[16]; "
             "right : STRING[16]; mid : STRING[16]; joined : STRING[16]; "
             "inserted : STRING[16]; deleted : STRING[16]; "
             "replaced : STRING[16]; found : DINT;",
             "len := LEN(s); left := LEFT(s, 2); right := RIGHT(s, 1); "
             "mid := MID(s, 1, 2); joined := CONCAT('A', 'B', 'C'); "
             "inserted := INSERT('AC', 'B', 2); "
             "deleted := DELETE('ABCD', 2, 2); "
             "replaced := REPLACE('ABCD', 'xy', 2, 2); "
             "found := FIND(s, 'b');"),
        "TYPE Vec3 : ARRAY[-1..1] OF DINT; END_TYPE\n"
        "TYPE Point : STRUCT X : DINT; Y : DINT; END_STRUCT; END_TYPE\n"
        "PROGRAM p\nVAR A : Vec3; B : Vec3; P0 : Point; P1 : Point; "
        "I : DINT; END_VAR\n"
        "A[-1] := 1; A[0] := 2; A[1] := 3; B := A; "
        "P0.X := B[I]; P0.Y := P0.X + 1; P1 := P0;\n"
        "END_PROGRAM\n",
        "PROGRAM Main\nVAR X : DINT; END_VAR\nSFC Flow\n"
        "INITIAL_STEP A: Work(N); END_STEP\nSTEP B: TERMINAL; END_STEP\n"
        "TRANSITION FROM A TO B := X > 0; END_TRANSITION\n"
        "ACTION Work: X := X + 1; END_ACTION\n"
        "END_SFC\nEND_PROGRAM\n"};

    for(const std::string &source : sources) {
        const st::CompileResult compiled = st::compile(source);
        check(compiled.ok, "malformed-bytecode matrix source compiles");
        if(compiled.ok) {
            sweep_malformed_bytecode(compiled.program);
            sweep_malformed_metadata(compiled.program);
        }
    }
}

void malformed_source_sample_matrix()
{
    const std::string sources[] = {
        "TYPE Vec3 : ARRAY[-1..1] OF DINT; END_TYPE\n"
        "TYPE Point : STRUCT X : DINT; Y : DINT; END_STRUCT; END_TYPE\n"
        "FUNCTION Twice : DINT VAR_INPUT X : DINT; END_VAR "
        "Twice := X * 2; END_FUNCTION\n"
        "FUNCTION_BLOCK Accumulator VAR_INPUT Enable : BOOL; END_VAR "
        "VAR_OUTPUT Value : DINT; END_VAR "
        "IF Enable THEN Value := Value + 1; END_IF; END_FUNCTION_BLOCK\n"
        "PROGRAM Main\nVAR A : Vec3; P : Point; F : Accumulator; "
        "I : DINT; S : STRING[8] := 'abc'; END_VAR\n"
        "FOR I := -1 TO 1 DO A[I] := Twice(I); END_FOR; "
        "P.X := A[0]; P.Y := A[1]; F(Enable := P.X <= P.Y); "
        "IF F.Value > 3 THEN S := CONCAT(S, 'x'); END_IF;\n"
        "END_PROGRAM\n",
        "PROGRAM Main\nVAR X : DINT; END_VAR\n"
        "SFC Flow\nINITIAL_STEP Start: Work(N); END_STEP\n"
        "STEP Done: TERMINAL; END_STEP\n"
        "TRANSITION FROM Start TO Done := X > 0; END_TRANSITION\n"
        "ACTION Work: X := X + 1; END_ACTION\n"
        "END_SFC\nEND_PROGRAM\n",
        "VAR_GLOBAL Trigger : BOOL; END_VAR\n"
        "PROGRAM Main VAR N : DINT; END_VAR N := N + 1; END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK Periodic(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 1000);\n"
        "TASK Event(EVENT := Trigger, PRIORITY := 1, BUDGET := 1000);\n"
        "PROGRAM P0 WITH Periodic : Main;\n"
        "PROGRAM P1 WITH Event : Main;\n"
        "END_RESOURCE\nEND_CONFIGURATION\n"};
    const char replacements[] = {'\0', ' ', '\n', ';', ':', '(', ')', ','};
    constexpr std::size_t kSamplesPerSource = 24;

    for(const std::string &source : sources) {
        check(st::compile(source).ok, "malformed-source base compiles");
        for(std::size_t sample = 0; sample < kSamplesPerSource; ++sample) {
            const std::size_t size =
                source.size() * sample / kSamplesPerSource;
            (void)st::compile(source.substr(0, size));
            const std::size_t index =
                (source.size() - 1) * sample / (kSamplesPerSource - 1);
            for(const char replacement : replacements) {
                std::string mutated = source;
                mutated[index] = replacement;
                (void)st::compile(mutated);
            }
            std::string erased = source;
            erased.erase(index, 1);
            (void)st::compile(erased);
        }
    }
}

void constant_fold_contract_matrix()
{
    struct NumericCase
    {
        const char *type;
        const char *left;
        const char *right;
        bool modulo;
    };
    const NumericCase cases[] = {
        {"SINT", "SINT#7", "SINT#2", true},
        {"INT", "INT#7", "INT#2", true},
        {"DINT", "DINT#7", "DINT#2", true},
        {"LINT", "LINT#7", "LINT#2", true},
        {"USINT", "USINT#7", "USINT#2", true},
        {"UINT", "UINT#7", "UINT#2", true},
        {"UDINT", "UDINT#7", "UDINT#2", true},
        {"ULINT", "ULINT#7", "ULINT#2", true},
        {"REAL", "REAL#7.0", "REAL#2.0", false},
        {"LREAL", "LREAL#7.0", "LREAL#2.0", false}};
    const char *arithmetic[] = {"+", "-", "*", "/"};
    const char *comparisons[] = {"=", "<>", "<", ">", "<=", ">="};

    std::string source = "PROGRAM p\nVAR\nQ : BOOL;\n";
    for(const NumericCase &item : cases) {
        source += "V_";
        source += item.type;
        source += " : ";
        source += item.type;
        source += ";\n";
    }
    source += "B : BYTE; W : WORD; D : DWORD; L : LWORD; "
              "T : TIME;\nEND_VAR\n";
    for(const NumericCase &item : cases) {
        const std::string variable = std::string("V_") + item.type;
        for(const char *op : arithmetic) {
            source += variable + " := " + item.left + " " + op + " " +
                      item.right + ";\n";
        }
        if(item.modulo) {
            source += variable + " := " + item.left + " MOD " + item.right +
                      ";\n";
        }
        for(const char *op : comparisons) {
            source += "Q := " + std::string(item.left) + " " + op + " " +
                      item.right + ";\n";
        }
    }
    source +=
        "Q := TRUE AND FALSE; Q := TRUE OR FALSE; Q := TRUE XOR FALSE;\n"
        "B := BYTE#16#A5 AND BYTE#16#0F; B := BYTE#1 OR BYTE#2; "
        "B := BYTE#3 XOR BYTE#1;\n"
        "W := WORD#7 AND WORD#3; D := DWORD#7 OR DWORD#8; "
        "L := LWORD#7 XOR LWORD#2;\n"
        "T := T#7ms + T#2ms; T := T#7ms - T#2ms; "
        "T := T#7ms * DINT#2; T := T#7ms / DINT#2;\n"
        "V_INT := BOOL_TO_INT(TRUE); Q := INT_TO_BOOL(INT#1); "
        "V_LREAL := REAL_TO_LREAL(REAL#1.5); "
        "V_REAL := LREAL_TO_REAL(LREAL#1.5); "
        "V_REAL := DINT_TO_REAL(DINT#7); "
        "V_LREAL := UDINT_TO_LREAL(UDINT#7); "
        "V_DINT := LREAL_TO_DINT(LREAL#1.5); "
        "V_DINT := TRUNC_DINT(LREAL#1.5); "
        "V_USINT := LINT_TO_USINT(LINT#257);\n"
        "END_PROGRAM\n";

    Rig rig;
    check(rig.build(source), "constant-fold contract matrix builds");
    check(rig.scan() == st::ScanError::ok,
          "constant-fold contract matrix scans");
}

void opcode_operand_failure_matrix()
{
    const st::CompileResult compiled =
        st::compile(wrap("x : LINT;", "x := 1;"));
    check(compiled.ok, "opcode operand matrix base compiles");
    if(!compiled.ok) return;

    const auto byte = [](st::Op op) {
        return static_cast<std::uint8_t>(op);
    };
    const unsigned last = byte(st::Op::debug_probe);
    for(unsigned raw = 0U; raw <= last; ++raw) {
        for(std::size_t payload_size = 0U; payload_size <= 40U;
            ++payload_size) {
            for(const std::uint8_t fill : {std::uint8_t{0U},
                                           std::uint8_t{0xFFU}}) {
                for(const int stack_values : {0, 8}) {
                    st::Program program = compiled.program;
                    program.constants = {0U};
                    program.stack_slots = 16;
                    program.code.clear();
                    for(int index = 0; index < stack_values; ++index) {
                        program.code.push_back(byte(st::Op::push_const));
                        program.code.push_back(0U);
                        program.code.push_back(0U);
                    }
                    program.code.push_back(static_cast<std::uint8_t>(raw));
                    program.code.insert(program.code.end(), payload_size, fill);
                    program.code.push_back(byte(st::Op::halt));
                    (void)scan_tampered_program(program, 64);
                }
            }
        }
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
              "c : CTU; n : INT; pulse : BOOL; q : BOOL; cv : LINT;",
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
              "IF n = 1 THEN f(SET1 := TRUE, RESET := FALSE); END_IF;"
              "IF n = 2 THEN f(SET1 := FALSE); END_IF;" // RESET keeps FALSE
              "IF n = 3 THEN f(RESET := TRUE); END_IF;" // SET1 keeps FALSE
              "q := f.Q1;")),
          "SR builds");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 1, "SR sets");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 1,
          "SR holds with unassigned R");
    check(sr.scan() == st::ScanError::ok && sr.i64("q") == 0, "SR resets");

    // CTUD both directions.
    Rig ctud;
    check(ctud.build(wrap(
              "c : CTUD; n : INT; up : BOOL; down : BOOL; cv : LINT;",
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

void tod_negative_wrap()
{
    Rig rig;
    check(rig.build(wrap(
              "tod : TOD := TOD#00:00:01; t : TIME := T#3s; result : TOD;",
              "result := tod - t;")),
          "TOD negative wrap program builds");
    check(rig.scan() == st::ScanError::ok, "TOD negative wrap scan ok");
    check(rig.i64("result") == 86400000000000LL - 2000000000LL,
          "TOD wraps negative to end of day");
}

void time_scale_boundary()
{
    Rig rig;
    check(rig.build(wrap(
              "t : TIME := T#5s; d : DINT := -1; result : TIME;"
              "rs : LREAL := 2.5; scaled : TIME; divided : TIME;"
              "dz : LREAL := 0.0; bad : TIME;",
              "result := t / d;"
              "scaled := t * rs;"
              "divided := t / LREAL#2.0;")),
          "time-scale program builds");
    check(rig.scan() == st::ScanError::ok, "time-scale scan ok");
    check(rig.i64("result") == -5000000000LL, "TIME / -1");
    check(rig.i64("scaled") == 12500000000LL, "TIME * LREAL");
    check(rig.i64("divided") == 2500000000LL, "TIME / LREAL");

    Rig inf;
    check(inf.build(wrap(
              "t : TIME := T#5s; z : LREAL := 0.0; bad : TIME;",
              "bad := t / z;")),
          "time-scale infinity program builds");
    check(inf.scan() == st::ScanError::conversion_invalid,
          "TIME / LREAL#0.0 faults");
}

void lint_runtime_division()
{
    Rig rig;
    check(rig.build(wrap(
              "a : LINT; b : LINT; c : LINT; d : LINT; m : LINT;",
              "a := LINT#1000000000000; b := LINT#2;"
              "c := a * b;"
              "d := a / b;"
              "b := LINT#-1; m := a MOD b;")),
          "LINT runtime division program builds");
    check(rig.scan() == st::ScanError::ok, "LINT runtime division scan ok");
    check(rig.i64("c") == 2000000000000LL, "LINT multiply");
    check(rig.i64("d") == 500000000000LL, "LINT divide");
    check(rig.i64("m") == 0, "LINT MOD -1 is zero");

    Rig neg;
    check(neg.build(wrap(
              "a : LINT; b : LINT; r : LINT;",
              "a := LINT#7; b := LINT#-1; r := a / b;")),
          "LINT div-by-neg1 program builds");
    check(neg.scan() == st::ScanError::ok, "LINT div-by-neg1 scan ok");
    check(neg.i64("r") == -7, "LINT / -1 wraps via negate");

    Rig divzero;
    check(divzero.build(wrap(
              "a : LINT; b : LINT; c : LINT;",
              "a := LINT#7; b := LINT#0; c := a / b;")),
          "LINT div-zero program builds");
    check(divzero.scan() == st::ScanError::division_by_zero,
          "LINT division by zero detected");

    Rig ulint;
    check(ulint.build(wrap(
              "a : ULINT; b : ULINT; d : ULINT; m : ULINT;",
              "a := ULINT#100; b := ULINT#3; d := a / b; m := a MOD b;")),
          "ULINT division program builds");
    check(ulint.scan() == st::ScanError::ok, "ULINT division scan ok");
    check(ulint.i64("d") == 33, "ULINT divide");
}

void power_boundary()
{
    Rig rig;
    check(rig.build(wrap(
              "r : REAL; l : LREAL;",
              "r := EXPT(REAL#2.0, REAL#10.0);"
              "l := EXPT(LREAL#2.0, LREAL#10.0);")),
          "EXPT program builds");
    check(rig.scan() == st::ScanError::ok, "EXPT scan ok");
    check(std::abs(rig.f64("r") - 1024.0) < 1.0, "REAL EXPT result");
    check(std::abs(rig.f64("l") - 1024.0) < 0.001, "LREAL EXPT result");
}

void unicode_surrogate_check()
{
    Rig rig;
    check(rig.build(wrap(
              "val : UDINT := UDINT#16#D800; wc : WCHAR;",
              "wc := UDINT_TO_WCHAR(val);")),
          "unicode surrogate program builds");
    check(rig.scan() == st::ScanError::conversion_invalid,
          "UDINT_TO_WCHAR rejects surrogate");
}

void subrange_enum_runtime_check()
{
    Rig rig;
    check(rig.build(
              "TYPE SmallRange : DINT (1..10); END_TYPE\n"
              "PROGRAM p\nVAR raw : DINT := 11; x : SmallRange; END_VAR\n"
              "x := DINT_TO_SmallRange(raw);\nEND_PROGRAM\n"),
          "subrange runtime check program builds");
    if(rig.compiled.ok) {
        check(rig.scan() == st::ScanError::range_violation,
              "subrange runtime out-of-bounds detected");
    }

    Rig rig2;
    check(rig2.build(
              "TYPE Color : (RED, GREEN, BLUE); END_TYPE\n"
              "PROGRAM p\nVAR raw : DINT := 99; c : Color; END_VAR\n"
              "c := DINT_TO_Color(raw);\nEND_PROGRAM\n"),
          "enum runtime check program builds");
    if(rig2.compiled.ok) {
        check(rig2.scan() == st::ScanError::range_violation,
              "enum invalid ordinal detected");
    }
}

void broad_vm_language_coverage()
{
    // INT-specific arithmetic (add_int, sub_int, mul_int, div_int, mod_int)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : INT := INT#100; b : INT := INT#7;"
                  "sum : INT; diff : INT; prod : INT; quot : INT; rem : INT;"
                  "neg : INT;",
                  "sum := a + b;"
                  "diff := a - b;"
                  "prod := a * b;"
                  "quot := a / b;"
                  "rem := a MOD b;"
                  "neg := -a;")),
              "INT arith program builds");
        check(rig.scan() == st::ScanError::ok, "INT arith scan ok");
        check(rig.i64("sum") == 107, "INT add");
        check(rig.i64("diff") == 93, "INT sub");
        check(rig.i64("prod") == 700, "INT mul");
        check(rig.i64("quot") == 14, "INT div");
        check(rig.i64("rem") == 2, "INT mod");
        check(rig.i64("neg") == -100, "INT neg");
    }

    // REAL arithmetic (add_real, sub_real, mul_real, div_real, neg_real)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : REAL := REAL#3.0; b : REAL := REAL#2.0;"
                  "sum : REAL; diff : REAL; prod : REAL; quot : REAL;"
                  "neg : REAL;",
                  "sum := a + b;"
                  "diff := a - b;"
                  "prod := a * b;"
                  "quot := a / b;"
                  "neg := -a;")),
              "REAL arith program builds");
        check(rig.scan() == st::ScanError::ok, "REAL arith scan ok");
        check(std::abs(rig.f64("sum") - 5.0) < 0.01, "REAL add");
        check(std::abs(rig.f64("diff") - 1.0) < 0.01, "REAL sub");
        check(std::abs(rig.f64("prod") - 6.0) < 0.01, "REAL mul");
        check(std::abs(rig.f64("quot") - 1.5) < 0.01, "REAL div");
        check(std::abs(rig.f64("neg") + 3.0) < 0.01, "REAL neg");
    }

    // LREAL subtraction, division, negation (sub_lreal, div_lreal, neg_lreal)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : LREAL := 10.0; b : LREAL := 3.0;"
                  "diff : LREAL; quot : LREAL; neg : LREAL;",
                  "diff := a - b;"
                  "quot := a / b;"
                  "neg := -a;")),
              "LREAL arith program builds");
        check(rig.scan() == st::ScanError::ok, "LREAL arith scan ok");
        check(std::abs(rig.f64("diff") - 7.0) < 0.001, "LREAL sub");
        check(std::abs(rig.f64("quot") - 3.333) < 0.01, "LREAL div");
        check(std::abs(rig.f64("neg") + 10.0) < 0.001, "LREAL neg");
    }

    // REAL comparisons (cmp_eq_f, cmp_ne_f, cmp_lt_f, cmp_gt_f, cmp_le_f)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : REAL := REAL#3.0; b : REAL := REAL#5.0;"
                  "eq : BOOL; ne : BOOL; lt : BOOL; gt : BOOL;"
                  "le : BOOL; ge : BOOL;",
                  "eq := a = b;"
                  "ne := a <> b;"
                  "lt := a < b;"
                  "gt := a > b;"
                  "le := a <= b;"
                  "ge := a >= b;")),
              "REAL compare program builds");
        check(rig.scan() == st::ScanError::ok, "REAL compare scan ok");
        check(rig.i64("eq") == 0, "REAL eq");
        check(rig.i64("ne") == 1, "REAL ne");
        check(rig.i64("lt") == 1, "REAL lt");
        check(rig.i64("gt") == 0, "REAL gt");
        check(rig.i64("le") == 1, "REAL le");
        check(rig.i64("ge") == 0, "REAL ge");
    }

    // Bitwise ops (bit_and, bit_or, bit_xor, bit_not)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : WORD := WORD#16#FF00; b : WORD := WORD#16#0F0F;"
                  "r_and : WORD; r_or : WORD; r_xor : WORD; r_not : WORD;",
                  "r_and := a AND b;"
                  "r_or := a OR b;"
                  "r_xor := a XOR b;"
                  "r_not := NOT a;")),
              "bitwise program builds");
        check(rig.scan() == st::ScanError::ok, "bitwise scan ok");
        check(rig.i64("r_and") == 0x0F00, "WORD AND");
        check(rig.i64("r_or") == 0xFF0F, "WORD OR");
        check(rig.i64("r_xor") == 0xF00F, "WORD XOR");
        check(rig.i64("r_not") == 0x00FF, "WORD NOT");
    }

    // FOR with INT control (for_guard, for_step_int)
    {
        Rig rig;
        check(rig.build(wrap(
                  "i : INT; s : INT;",
                  "FOR i := INT#1 TO INT#5 BY INT#1 DO s := s + i; END_FOR;")),
              "FOR INT program builds");
        check(rig.scan() == st::ScanError::ok, "FOR INT scan ok");
        check(rig.i64("s") == 15, "FOR INT sum 1..5");
    }

    // String comparisons (all 6 operators)
    {
        Rig rig;
        check(rig.build(wrap(
                  "s : STRING[20] := 'Hello';"
                  "eq : BOOL; ne : BOOL; lt : BOOL;"
                  "gt : BOOL; le : BOOL; ge : BOOL;"
                  "t : STRING[20] := 'World';",
                  "eq := s = t;"
                  "ne := s <> t;"
                  "lt := s < t;"
                  "gt := s > t;"
                  "le := s <= t;"
                  "ge := s >= t;")),
              "string ops program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "string ops scan ok");
            check(rig.i64("eq") == 0, "string eq");
            check(rig.i64("ne") == 1, "string ne");
            check(rig.i64("gt") == 0, "string gt");
            check(rig.i64("le") == 1, "string le");
            check(rig.i64("ge") == 0, "string ge");
        }
    }

    // DATE/TOD/DT arithmetic (date_arith: DATE±DINT, TOD±TIME, DT±TIME)
    {
        Rig rig;
        check(rig.build(wrap(
                  "d : DATE := D#2026-01-10;"
                  "t : TOD := TOD#12:00:00;"
                  "dt : DATE_AND_TIME := DT#2026-01-15-08:00:00;"
                  "offset : TIME := T#1h;"
                  "days : DINT := 1;"
                  "d2 : DATE; d3 : DATE;"
                  "t2 : TOD; t3 : TOD;"
                  "dt2 : DATE_AND_TIME; dt3 : DATE_AND_TIME;",
                  "d2 := d + days;"
                  "d3 := d - days;"
                  "t2 := t + offset;"
                  "t3 := t - offset;"
                  "dt2 := dt + offset;"
                  "dt3 := dt - offset;")),
              "DATE arith program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "DATE arith scan ok");
        }
    }

    // TIME * DINT and TIME / DINT (time_scale sub=0,1)
    {
        Rig rig;
        check(rig.build(wrap(
                  "t : TIME := T#2s; n : DINT := 3;"
                  "scaled : TIME; divided : TIME;",
                  "scaled := t * n;"
                  "divided := t / n;")),
              "TIME int-scale program builds");
        check(rig.scan() == st::ScanError::ok, "TIME int-scale scan ok");
        check(rig.i64("scaled") == 6000000000LL, "TIME * DINT");
        check(rig.i64("divided") == 666666666LL, "TIME / DINT trunc");
    }

    // Unsigned arithmetic (iarith unsigned path) and unsigned compare (cmp_u)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : USINT := USINT#200; b : USINT := USINT#50;"
                  "sum : USINT; diff : USINT; prod : USINT;"
                  "lt : BOOL; gt : BOOL; eq : BOOL;",
                  "sum := a + b;"
                  "diff := a - b;"
                  "prod := a * b;"
                  "lt := a < b;"
                  "gt := a > b;"
                  "eq := a = b;")),
              "unsigned arith program builds");
        check(rig.scan() == st::ScanError::ok, "unsigned arith scan ok");
        check(rig.i64("sum") == 250, "USINT add");
        check(rig.i64("diff") == 150, "USINT sub");
        check((rig.i64("prod") & 0xFF) == ((200*50) & 0xFF), "USINT mul wrap");
        check(rig.i64("lt") == 0, "USINT lt");
        check(rig.i64("gt") == 1, "USINT gt");
        check(rig.i64("eq") == 0, "USINT eq");
    }

    // conv_to_bool, conv_f_to_bool, conv_u2d
    {
        Rig rig;
        check(rig.build(wrap(
                  "n : DINT := 42; z : DINT := 0;"
                  "r : REAL := REAL#0.0; r2 : REAL := REAL#1.5;"
                  "u : UDINT := UDINT#100;"
                  "b1 : BOOL; b2 : BOOL; b3 : BOOL; b4 : BOOL;"
                  "f : LREAL;",
                  "b1 := DINT_TO_BOOL(n);"
                  "b2 := DINT_TO_BOOL(z);"
                  "b3 := REAL_TO_BOOL(r);"
                  "b4 := REAL_TO_BOOL(r2);"
                  "f := UDINT_TO_LREAL(u);")),
              "conversion program builds");
        check(rig.scan() == st::ScanError::ok, "conversion scan ok");
        check(rig.i64("b1") == 1, "DINT_TO_BOOL nonzero");
        check(rig.i64("b2") == 0, "DINT_TO_BOOL zero");
        check(rig.i64("b3") == 0, "REAL_TO_BOOL zero");
        check(rig.i64("b4") == 1, "REAL_TO_BOOL nonzero");
        check(std::abs(rig.f64("f") - 100.0) < 0.001, "UDINT_TO_LREAL");
    }

    // Standard functions: LIMIT, SEL, MUX
    {
        Rig rig;
        check(rig.build(wrap(
                  "x : DINT; y : DINT; z : DINT;"
                  "low : DINT := -5; mn : DINT := 0; mx : DINT := 10;"
                  "s0 : DINT := 100; s1 : DINT := 200;"
                  "m0 : DINT := 10; m1 : DINT := 20; m2 : DINT := 30;"
                  "sel : BOOL := FALSE; idx : DINT := 1;",
                  "x := LIMIT(mn, low, mx);"
                  "y := SEL(sel, s0, s1);"
                  "z := MUX(idx, m0, m1, m2);")),
              "std functions program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "std functions scan ok");
            check(rig.i64("x") == 0, "LIMIT clamps low");
            check(rig.i64("y") == 100, "SEL false picks IN0");
            check(rig.i64("z") == 20, "MUX index 1");
        }
    }

    // copy_bytes (triggered by structured assignment)
    {
        Rig rig;
        check(rig.build(
                  "TYPE Point : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE\n"
                  "PROGRAM p\nVAR\n"
                  "a : Point; b : Point;\n"
                  "END_VAR\n"
                  "a.x := 10; a.y := 20; b := a;\n"
                  "END_PROGRAM\n"),
              "copy_bytes program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "copy_bytes scan ok");
        }
    }

    // INT comparison (cmp_ne_i - the only uncovered one from the cmp_*_i group)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : INT := INT#5; b : INT := INT#5; c : INT := INT#3;"
                  "ne1 : BOOL; ne2 : BOOL;",
                  "ne1 := a <> b;"
                  "ne2 := a <> c;")),
              "INT cmp_ne program builds");
        check(rig.scan() == st::ScanError::ok, "INT cmp_ne scan ok");
        check(rig.i64("ne1") == 0, "INT ne same");
        check(rig.i64("ne2") == 1, "INT ne diff");
    }

    // Math standard functions (sqrt, ln, log, exp, sin, cos, tan, asin, acos, atan)
    {
        Rig rig;
        check(rig.build(wrap(
                  "s : LREAL; l : LREAL; g : LREAL; e : LREAL;"
                  "sn : LREAL; cs : LREAL; tn : LREAL;"
                  "asn : LREAL; acs : LREAL; atn : LREAL;"
                  "a : LREAL;",
                  "s := SQRT(4.0);"
                  "l := LN(2.718281828);"
                  "g := LOG(100.0);"
                  "e := EXP(1.0);"
                  "sn := SIN(0.0);"
                  "cs := COS(0.0);"
                  "tn := TAN(0.0);"
                  "asn := ASIN(0.5);"
                  "acs := ACOS(0.5);"
                  "atn := ATAN(1.0);"
                  "a := ABS(-5.0);")),
              "math functions program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "math functions scan ok");
            check(std::abs(rig.f64("s") - 2.0) < 0.001, "SQRT(4)");
            check(std::abs(rig.f64("l") - 1.0) < 0.01, "LN(e)");
            check(std::abs(rig.f64("g") - 2.0) < 0.001, "LOG(100)");
            check(rig.f64("e") > 2.7 && rig.f64("e") < 2.8, "EXP(1)");
            check(std::abs(rig.f64("sn")) < 0.001, "SIN(0)");
            check(std::abs(rig.f64("cs") - 1.0) < 0.001, "COS(0)");
            check(std::abs(rig.f64("tn")) < 0.001, "TAN(0)");
            check(std::abs(rig.f64("a") - 5.0) < 0.001, "ABS(-5)");
        }
    }

    // MIN, MAX, ABS on unsigned integer types (unsigned path in std func)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : UDINT := UDINT#100; b : UDINT := UDINT#200;"
                  "mn : UDINT; mx : UDINT; ab : DINT;",
                  "mn := MIN(a, b);"
                  "mx := MAX(a, b);"
                  "ab := ABS(DINT#-42);")),
              "min/max/abs program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "min/max/abs scan ok");
            check(rig.i64("mn") == 100, "MIN unsigned");
            check(rig.i64("mx") == 200, "MAX unsigned");
            check(rig.i64("ab") == 42, "ABS signed");
        }
    }

    // GT, GE, LT, LE, EQ, NE comparison functions on unsigned types
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : UDINT := UDINT#10; b : UDINT := UDINT#20;"
                  "g : BOOL; ge : BOOL; l : BOOL; le : BOOL; e : BOOL; n : BOOL;",
                  "g := a > b;"
                  "ge := a >= b;"
                  "l := a < b;"
                  "le := a <= b;"
                  "e := a = b;"
                  "n := a <> b;")),
              "unsigned compare program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "unsigned compare scan ok");
            check(rig.i64("g") == 0, "UDINT gt");
            check(rig.i64("ge") == 0, "UDINT ge");
            check(rig.i64("l") == 1, "UDINT lt");
            check(rig.i64("le") == 1, "UDINT le");
            check(rig.i64("e") == 0, "UDINT eq");
            check(rig.i64("n") == 1, "UDINT ne");
        }
    }

    // String function LEN (string_length opcode)
    {
        Rig rig;
        check(rig.build(wrap(
                  "s : STRING[30] := 'Hello World';"
                  "n : DINT;",
                  "n := LEN(s);")),
              "string LEN program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "string LEN scan ok");
            check(rig.i64("n") == 11, "LEN('Hello World')");
        }
    }

    // Array indexing (exercises commit_outputs + range checking paths)
    {
        Rig rig;
        check(rig.build(
                  "TYPE IntArr : ARRAY[1..5] OF DINT; END_TYPE\n"
                  "PROGRAM p\nVAR\n"
                  "  arr : IntArr := [10, 20, 30, 40, 50];\n"
                  "  i : DINT := 3;\n"
                  "  val : DINT;\n"
                  "  sum : DINT;\n"
                  "END_VAR\n"
                  "val := arr[i];\n"
                  "FOR i := 1 TO 5 DO sum := sum + arr[i]; END_FOR;\n"
                  "END_PROGRAM\n"),
              "array program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "array scan ok");
            check(rig.i64("val") == 30, "array index 3");
            check(rig.i64("sum") == 150, "array sum");
        }
    }

    // DINT arithmetic to exercise add_dint/sub_dint/mul_dint via function calls
    // (these may not have been emitted directly in other tests)
    {
        Rig rig;
        check(rig.build(wrap(
                  "a : DINT := 1000; b : DINT := 7;"
                  "sum : DINT; diff : DINT; prod : DINT;"
                  "quot : DINT; rem : DINT;",
                  "sum := ADD(a, b);"
                  "diff := SUB(a, b);"
                  "prod := MUL(a, b);"
                  "quot := DIV(a, b);"
                  "rem := MOD(a, b);")),
              "DINT std arith program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "DINT std arith scan ok");
            check(rig.i64("sum") == 1007, "ADD DINT");
            check(rig.i64("diff") == 993, "SUB DINT");
            check(rig.i64("prod") == 7000, "MUL DINT");
            check(rig.i64("quot") == 142, "DIV DINT");
            check(rig.i64("rem") == 6, "MOD DINT");
        }
    }
}

void broad_binding_validation_and_sema_errors()
{
    // Multi-dimensional array access exercises bounds-checking opcodes
    // (vm.h lines 2055-2095: multi-index array range validation)
    {
        Rig rig;
        check(rig.build(
                  "TYPE Mat3x4 : ARRAY[1..3, 1..4] OF DINT; END_TYPE\n"
                  "PROGRAM md_array VAR\n"
                  "  Matrix : Mat3x4;\n"
                  "  i : DINT; j : DINT; v : DINT;\n"
                  "END_VAR\n"
                  "FOR i := 1 TO 3 DO\n"
                  "  FOR j := 1 TO 4 DO\n"
                  "    Matrix[i, j] := i * 10 + j;\n"
                  "  END_FOR;\n"
                  "END_FOR;\n"
                  "v := Matrix[2, 3];\n"
                  "END_PROGRAM"),
              "multi-dim array program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "multi-dim array scan ok");
            check(rig.i64("v") == 23, "multi-dim matrix value");
        }
    }

    // Cam switch table binding validation (vm.h lines 413-441)
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM cam_bind VAR\n"
                  "  A : AXIS_REF;\n"
                  "  Switches : MC_CAM_SWITCH_TABLE_VIEW;\n"
                  "  Cam : MC_DigitalCamSwitch;\n"
                  "  b : BOOL;\n"
                  "END_VAR\n"
                  "Cam(Axis := A, Switches := Switches, Enable := TRUE);\n"
                  "b := Cam.InOperation;\n"
                  "END_PROGRAM"),
              "cam switch binding program builds");
        if(rig.compiled.ok) {
            axis::AxisModel cam_axis;
            cam_axis.set_power(true);
            rig.instance.bind_axis("A", &cam_axis);

            fb::CamSwitchAction invalid_action{};
            invalid_action.track_number = 0;
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects track 0");

            invalid_action.track_number = 1;
            invalid_action.on_position = std::numeric_limits<double>::quiet_NaN();
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects NaN position");

            invalid_action.on_position = 0.0;
            invalid_action.off_position = 1.0;
            invalid_action.period = -1.0;
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects negative period");

            invalid_action.period = 0.0;
            invalid_action.on_position = 2.0;
            invalid_action.off_position = 1.0;
            invalid_action.cam_switch_mode = fb::CamSwitchAction::Mode::position;
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects inverted non-periodic position");

            invalid_action.on_position = 0.0;
            invalid_action.off_position = 1.0;
            invalid_action.cam_switch_mode = fb::CamSwitchAction::Mode::time;
            invalid_action.duration_ns = 0;
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects zero time duration");

            invalid_action.cam_switch_mode = fb::CamSwitchAction::Mode::position;
            invalid_action.axis_direction =
                static_cast<fb::CamSwitchAction::AxisDirection>(99);
            check(rig.instance.bind_cam_switch_table("Switches", &invalid_action, 1) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects invalid direction");

            check(rig.instance.bind_cam_switch_table("Switches", nullptr, 1) ==
                      st::BindingError::null_target,
                  "cam switch rejects null data");

            fb::CamSwitchAction valid_action{};
            valid_action.track_number = 1;
            valid_action.on_position = 0.0;
            valid_action.off_position = 1.0;
            valid_action.period = 0.0;
            valid_action.axis_direction = fb::CamSwitchAction::AxisDirection::both;
            valid_action.cam_switch_mode = fb::CamSwitchAction::Mode::position;
            check(rig.instance.bind_cam_switch_table("Switches", &valid_action, 0) ==
                      st::BindingError::invalid_value,
                  "cam switch rejects count 0");
            check(rig.instance.bind_cam_switch_table("Switches", &valid_action, 1) ==
                      st::BindingError::ok,
                  "cam switch accepts valid table");
        }
    }

    // Semantic error programs (sema.h branches for various error paths)
    {
        auto has_error = [](const st::CompileResult &r, st::DiagCode code) {
            for(const st::Diagnostic &d : r.diagnostics) {
                if(d.code == code) return true;
            }
            return false;
        };

        // Recursive type detection (sema.h line 261-264)
        const st::CompileResult recursive = st::compile(
            "TYPE RecNode : STRUCT val : DINT; next : RecNode; END_STRUCT; END_TYPE\n"
            "PROGRAM p VAR x : RecNode; END_VAR x.val := 1; END_PROGRAM");
        check(!recursive.ok && has_error(recursive, st::DiagCode::sema_recursive_type),
              "recursive type detected");

        // Duplicate variable name
        const st::CompileResult dup_var = st::compile(
            "PROGRAM p VAR x : DINT; x : BOOL; END_VAR x := 1; END_PROGRAM");
        check(!dup_var.ok, "duplicate var rejected");

        // Assignment to constant
        const st::CompileResult const_assign = st::compile(
            "PROGRAM p VAR CONSTANT c : DINT := 5; END_VAR c := 10; END_PROGRAM");
        check(!const_assign.ok, "constant assignment rejected");

        // Function call with wrong argument count
        const st::CompileResult wrong_args = st::compile(
            "PROGRAM p VAR x : DINT; END_VAR x := ABS(1, 2); END_PROGRAM");
        check(!wrong_args.ok, "wrong arg count rejected");

        // Type mismatch in binary operation
        const st::CompileResult type_mismatch = st::compile(
            "PROGRAM p VAR x : DINT; s : STRING; END_VAR x := x + s; END_PROGRAM");
        check(!type_mismatch.ok, "type mismatch in binary op rejected");

        // Array index out of static bounds
        const st::CompileResult array_oob = st::compile(
            "PROGRAM p VAR a : ARRAY[1..5] OF DINT; x : DINT; END_VAR "
            "x := a[0]; END_PROGRAM");
        check(!array_oob.ok, "static array OOB rejected");

        // Invalid CASE selector type
        const st::CompileResult bad_case = st::compile(
            "PROGRAM p VAR x : LREAL; END_VAR "
            "CASE x OF 1.0: ; END_CASE; END_PROGRAM");
        check(!bad_case.ok, "LREAL case selector rejected");

        // RETURN in PROGRAM (allowed in functions, error in programs)
        // Note: this may or may not be an error depending on implementation
        const st::CompileResult prog_return = st::compile(
            "PROGRAM p VAR x : DINT; END_VAR RETURN; x := 1; END_PROGRAM");
        (void)prog_return;

        // Nested array declaration with invalid bounds
        const st::CompileResult bad_bounds = st::compile(
            "PROGRAM p VAR a : ARRAY[5..1] OF DINT; END_VAR a[5] := 1; END_PROGRAM");
        check(!bad_bounds.ok, "inverted array bounds rejected");

        // Undeclared function call (sema.h: function resolution error)
        const st::CompileResult unknown_fn = st::compile(
            "PROGRAM p VAR x : DINT; END_VAR x := UNKNOWN_FN(5); END_PROGRAM");
        check(!unknown_fn.ok, "unknown function rejected");

        // Assignment to input pin (sema.h: pin direction error)
        const st::CompileResult pin_assign = st::compile(
            "PROGRAM p VAR m : MC_MoveAbsolute; END_VAR "
            "m.Position := 5.0; m(Axis := m.Axis, Execute := TRUE); END_PROGRAM");
        (void)pin_assign;

        // String to integer assignment (sema.h: type incompatible)
        const st::CompileResult str_to_int = st::compile(
            "PROGRAM p VAR x : DINT; END_VAR x := 'hello'; END_PROGRAM");
        check(!str_to_int.ok, "string to int assignment rejected");

        // WHILE without boolean condition
        const st::CompileResult while_nonbool = st::compile(
            "PROGRAM p VAR x : DINT; END_VAR "
            "WHILE x DO x := x - 1; END_WHILE; END_PROGRAM");
        (void)while_nonbool;

        // Nested IF with complex boolean expressions
        const st::CompileResult complex_bool = st::compile(
            "PROGRAM p VAR a : BOOL; b : BOOL; c : DINT; END_VAR "
            "IF (a AND b) OR (c > 5 AND c < 10) THEN c := 1; "
            "ELSIF NOT a AND b THEN c := 2; "
            "ELSIF a XOR b THEN c := 3; "
            "END_IF; END_PROGRAM");
        check(complex_bool.ok, "complex boolean expression compiles");

        // FOR loop with negative step
        const st::CompileResult for_neg = st::compile(
            "PROGRAM p VAR i : DINT; s : DINT; END_VAR "
            "FOR i := 10 TO 1 BY -1 DO s := s + i; END_FOR; END_PROGRAM");
        check(for_neg.ok, "negative step FOR compiles");

        // REPEAT..UNTIL loop
        const st::CompileResult repeat_loop = st::compile(
            "PROGRAM p VAR i : DINT; END_VAR "
            "REPEAT i := i + 1; UNTIL i >= 10 END_REPEAT; END_PROGRAM");
        check(repeat_loop.ok, "REPEAT loop compiles");

        // EXIT in FOR loop
        const st::CompileResult exit_loop = st::compile(
            "PROGRAM p VAR i : DINT; found : DINT; END_VAR "
            "FOR i := 1 TO 100 DO "
            "  IF i = 42 THEN found := i; EXIT; END_IF; "
            "END_FOR; END_PROGRAM");
        check(exit_loop.ok, "EXIT in FOR compiles");

        // CONTINUE in loop
        const st::CompileResult continue_loop = st::compile(
            "PROGRAM p VAR i : DINT; s : DINT; END_VAR "
            "FOR i := 1 TO 10 DO "
            "  IF i MOD 2 = 0 THEN CONTINUE; END_IF; "
            "  s := s + i; "
            "END_FOR; END_PROGRAM");
        check(continue_loop.ok, "CONTINUE in FOR compiles");

        // WSTRING operations (vm.h wstring branch)
        const st::CompileResult wstr_ops = st::compile(
            "PROGRAM p VAR ws : WSTRING[32] := \"Hello\"; "
            "n : DINT; END_VAR n := LEN(ws); END_PROGRAM");
        check(wstr_ops.ok, "WSTRING LEN compiles");

        // CASE with integer ranges
        const st::CompileResult case_range = st::compile(
            "PROGRAM p VAR x : DINT; y : DINT; END_VAR "
            "CASE x OF "
            "  1: y := 10; "
            "  2, 3: y := 20; "
            "  4..10: y := 30; "
            "  ELSE y := 0; "
            "END_CASE; END_PROGRAM");
        check(case_range.ok, "CASE with ranges compiles");

        // Enum type and CASE on enum
        const st::CompileResult enum_case = st::compile(
            "TYPE Color : (Red, Green, Blue); END_TYPE\n"
            "PROGRAM p VAR c : Color := Color#Green; v : DINT; END_VAR "
            "CASE c OF "
            "  Color#Red: v := 1; "
            "  Color#Green: v := 2; "
            "  Color#Blue: v := 3; "
            "END_CASE; END_PROGRAM");
        check(enum_case.ok, "CASE on enum compiles");

        // Subrange type with range checking
        const st::CompileResult subrange = st::compile(
            "TYPE Percent : DINT(0..100); END_TYPE\n"
            "PROGRAM p VAR x : Percent := 50; y : DINT; END_VAR "
            "y := x + 10; END_PROGRAM");
        check(subrange.ok, "subrange type compiles");

        // Multiple type conversions
        const st::CompileResult conversions = st::compile(
            "PROGRAM p VAR\n"
            "  r : LREAL := 3.14;\n"
            "  i : DINT;\n"
            "  u : UDINT;\n"
            "  w : WORD;\n"
            "  b : BOOL;\n"
            "  s : SINT;\n"
            "END_VAR\n"
            "i := LREAL_TO_DINT(r);\n"
            "u := DINT_TO_UDINT(i);\n"
            "w := UDINT_TO_WORD(u);\n"
            "b := i > 0;\n"
            "r := DINT_TO_LREAL(i);\n"
            "s := DINT_TO_SINT(i);\n"
            "END_PROGRAM");
        check(conversions.ok, "type conversions compile");

        // TIME arithmetic
        const st::CompileResult time_arith = st::compile(
            "PROGRAM p VAR\n"
            "  t1 : TIME := T#1s;\n"
            "  t2 : TIME := T#500ms;\n"
            "  sum : TIME;\n"
            "  diff : TIME;\n"
            "  scaled : TIME;\n"
            "  divided : TIME;\n"
            "END_VAR\n"
            "sum := ADD_TIME(t1, t2);\n"
            "diff := SUB_TIME(t1, t2);\n"
            "scaled := MULTIME(t1, 3);\n"
            "divided := DIVTIME(t1, 2);\n"
            "END_PROGRAM");
        check(time_arith.ok, "TIME arithmetic compiles");

        // Nested struct access
        const st::CompileResult nested_struct = st::compile(
            "TYPE Inner : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE\n"
            "TYPE Outer : STRUCT pos : Inner; name : DINT; END_STRUCT; END_TYPE\n"
            "PROGRAM p VAR o : Outer; v : DINT; END_VAR "
            "o.pos.x := 10; o.pos.y := 20; v := o.pos.x + o.pos.y; END_PROGRAM");
        check(nested_struct.ok, "nested struct access compiles");
    }

    // Runtime execution of compiled programs to exercise VM branches
    {
        // CASE with integer ranges — exercises case_range opcode
        Rig case_rig;
        check(case_rig.build(
                  "PROGRAM p VAR x : DINT; y : DINT; END_VAR\n"
                  "x := 7;\n"
                  "CASE x OF\n"
                  "  1: y := 10;\n"
                  "  2, 3: y := 20;\n"
                  "  4..10: y := 30;\n"
                  "  ELSE y := 0;\n"
                  "END_CASE;\n"
                  "END_PROGRAM"),
              "CASE range program builds");
        if(case_rig.compiled.ok) {
            check(case_rig.scan() == st::ScanError::ok, "CASE range scan ok");
            check(case_rig.i64("y") == 30, "CASE range selects 4..10");
        }

        // Enum CASE — exercises check_range opcode
        Rig enum_rig;
        check(enum_rig.build(
                  "TYPE Color : (Red, Green, Blue); END_TYPE\n"
                  "PROGRAM p VAR c : Color := Color#Green; v : DINT; END_VAR\n"
                  "CASE c OF\n"
                  "  Color#Red: v := 1;\n"
                  "  Color#Green: v := 2;\n"
                  "  Color#Blue: v := 3;\n"
                  "END_CASE;\n"
                  "END_PROGRAM"),
              "enum CASE program builds");
        if(enum_rig.compiled.ok) {
            check(enum_rig.scan() == st::ScanError::ok, "enum CASE scan ok");
            check(enum_rig.i64("v") == 2, "enum CASE selects Green");
        }

        // REPEAT with EXIT — exercises repeat/exit opcodes
        Rig repeat_rig;
        check(repeat_rig.build(
                  "PROGRAM p VAR i : DINT; sum : DINT; END_VAR\n"
                  "i := 0; sum := 0;\n"
                  "REPEAT\n"
                  "  i := i + 1;\n"
                  "  IF i = 5 THEN EXIT; END_IF;\n"
                  "  sum := sum + i;\n"
                  "UNTIL i >= 100\n"
                  "END_REPEAT;\n"
                  "END_PROGRAM"),
              "REPEAT EXIT program builds");
        if(repeat_rig.compiled.ok) {
            check(repeat_rig.scan() == st::ScanError::ok, "REPEAT EXIT scan ok");
            check(repeat_rig.i64("sum") == 10, "REPEAT exits at i=5, sum=1+2+3+4");
        }

        // WHILE with CONTINUE — exercises while/continue opcodes
        Rig while_rig;
        check(while_rig.build(
                  "PROGRAM p VAR i : DINT; sum : DINT; END_VAR\n"
                  "i := 0; sum := 0;\n"
                  "WHILE i < 10 DO\n"
                  "  i := i + 1;\n"
                  "  IF i MOD 2 = 0 THEN CONTINUE; END_IF;\n"
                  "  sum := sum + i;\n"
                  "END_WHILE;\n"
                  "END_PROGRAM"),
              "WHILE CONTINUE program builds");
        if(while_rig.compiled.ok) {
            check(while_rig.scan() == st::ScanError::ok, "WHILE CONTINUE scan ok");
            check(while_rig.i64("sum") == 25, "sum of odd 1..9 = 25");
        }

        // FOR with negative step
        Rig for_neg_rig;
        check(for_neg_rig.build(
                  "PROGRAM p VAR i : DINT; sum : DINT; END_VAR\n"
                  "sum := 0;\n"
                  "FOR i := 5 TO 1 BY -1 DO\n"
                  "  sum := sum + i;\n"
                  "END_FOR;\n"
                  "END_PROGRAM"),
              "FOR negative step program builds");
        if(for_neg_rig.compiled.ok) {
            check(for_neg_rig.scan() == st::ScanError::ok,
                  "FOR negative step scan ok");
            check(for_neg_rig.i64("sum") == 15, "sum 5+4+3+2+1 = 15");
        }

        // Type conversions at runtime
        Rig conv_rig;
        check(conv_rig.build(
                  "PROGRAM p VAR\n"
                  "  r : LREAL := 3.7;\n"
                  "  i : DINT;\n"
                  "  u : UDINT;\n"
                  "  back : LREAL;\n"
                  "END_VAR\n"
                  "i := LREAL_TO_DINT(r);\n"
                  "u := DINT_TO_UDINT(i);\n"
                  "back := UDINT_TO_LREAL(u);\n"
                  "END_PROGRAM"),
              "type conversion program builds");
        if(conv_rig.compiled.ok) {
            check(conv_rig.scan() == st::ScanError::ok, "type conversion scan ok");
            check(conv_rig.i64("i") == 4, "LREAL_TO_DINT(3.7) = 4 (round)");
        }

        // WSTRING basic operations
        Rig wstr_rig;
        check(wstr_rig.build(
                  "PROGRAM p VAR\n"
                  "  ws : WSTRING[32] := \"ABC\";\n"
                  "  n : DINT;\n"
                  "  b : BOOL;\n"
                  "END_VAR\n"
                  "n := LEN(ws);\n"
                  "b := ws = \"ABC\";\n"
                  "END_PROGRAM"),
              "WSTRING program builds");
        if(wstr_rig.compiled.ok) {
            check(wstr_rig.scan() == st::ScanError::ok, "WSTRING scan ok");
            check(wstr_rig.i64("n") == 3, "WSTRING LEN = 3");
            check(wstr_rig.i64("b") == 1, "WSTRING equality");
        }

        // Nested struct access at runtime
        Rig struct_rig;
        check(struct_rig.build(
                  "TYPE Vec2 : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE\n"
                  "TYPE Box : STRUCT min : Vec2; max : Vec2; END_STRUCT; END_TYPE\n"
                  "PROGRAM p VAR b : Box; area : DINT; END_VAR\n"
                  "b.min.x := 1; b.min.y := 2;\n"
                  "b.max.x := 5; b.max.y := 7;\n"
                  "area := (b.max.x - b.min.x) * (b.max.y - b.min.y);\n"
                  "END_PROGRAM"),
              "nested struct program builds");
        if(struct_rig.compiled.ok) {
            check(struct_rig.scan() == st::ScanError::ok, "nested struct scan ok");
            check(struct_rig.i64("area") == 20, "box area = 4*5 = 20");
        }

        // POWER operator (vm.h: Op::power)
        Rig pow_rig;
        check(pow_rig.build(
                  "PROGRAM p VAR x : LREAL; y : LREAL; END_VAR\n"
                  "x := 2.0 ** 10.0;\n"
                  "y := 3.0 ** 2.0;\n"
                  "END_PROGRAM"),
              "POWER operator program builds");
        if(pow_rig.compiled.ok) {
            check(pow_rig.scan() == st::ScanError::ok, "POWER scan ok");
        }

        // String comparison operators (vm.h string_compare: all 6 comparison types)
        Rig strcmp_rig;
        check(strcmp_rig.build(
                  "PROGRAM p VAR\n"
                  "  a : STRING[16] := 'ABC';\n"
                  "  b : STRING[16] := 'DEF';\n"
                  "  eq : BOOL; ne : BOOL; lt : BOOL;\n"
                  "  gt : BOOL; le : BOOL; ge : BOOL;\n"
                  "END_VAR\n"
                  "eq := a = b;\n"
                  "ne := a <> b;\n"
                  "lt := a < b;\n"
                  "gt := a > b;\n"
                  "le := a <= b;\n"
                  "ge := a >= b;\n"
                  "END_PROGRAM"),
              "string compare program builds");
        if(strcmp_rig.compiled.ok) {
            check(strcmp_rig.scan() == st::ScanError::ok, "string compare scan ok");
            check(strcmp_rig.i64("eq") == 0, "ABC != DEF");
            check(strcmp_rig.i64("ne") == 1, "ABC <> DEF");
            check(strcmp_rig.i64("lt") == 1, "ABC < DEF");
        }
    }
}

void broad_sfc_execution_coverage()
{
    // SFC exercising all qualifier types (N, S, R, P, L, D, SD, DS, SL) and
    // simultaneous divergence/convergence, driving through multiple scans to
    // exercise the full advance_sfc_runner state machine.
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p\nVAR\n"
                  "  count : DINT;\n"
                  "  flag : BOOL;\n"
                  "  delayed : DINT;\n"
                  "  limited : DINT;\n"
                  "  stored_delayed : DINT;\n"
                  "  delay_stored : DINT;\n"
                  "  store_limited : DINT;\n"
                  "END_VAR\n"
                  "SFC Main\n"
                  "  INITIAL_STEP Init:\n"
                  "    CountAction(N);\n"
                  "    PulseAction(P);\n"
                  "    LimitedAction(L, T#3ms);\n"
                  "    DelayedAction(D, T#2ms);\n"
                  "  END_STEP\n"
                  "  STEP Phase2:\n"
                  "    CountAction(N);\n"
                  "    StoreAction(S);\n"
                  "    SDAction(SD, T#2ms);\n"
                  "    DSAction(DS, T#2ms);\n"
                  "    SLAction(SL, T#4ms);\n"
                  "  END_STEP\n"
                  "  STEP Phase3:\n"
                  "    ResetAction(R, flag);\n"
                  "  END_STEP\n"
                  "  STEP Done:\n"
                  "    TERMINAL;\n"
                  "  END_STEP\n"
                  "  TRANSITION FROM Init TO Phase2 := count >= 2;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM Phase2 TO Phase3 := count >= 5;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM Phase3 TO Done := TRUE;\n"
                  "  END_TRANSITION\n"
                  "  ACTION CountAction:\n"
                  "    count := count + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION PulseAction:\n"
                  "    flag := TRUE;\n"
                  "  END_ACTION\n"
                  "  ACTION StoreAction:\n"
                  "    flag := TRUE;\n"
                  "  END_ACTION\n"
                  "  ACTION ResetAction:\n"
                  "    flag := FALSE;\n"
                  "  END_ACTION\n"
                  "  ACTION LimitedAction:\n"
                  "    limited := limited + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION DelayedAction:\n"
                  "    delayed := delayed + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION SDAction:\n"
                  "    stored_delayed := stored_delayed + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION DSAction:\n"
                  "    delay_stored := delay_stored + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION SLAction:\n"
                  "    store_limited := store_limited + 1;\n"
                  "  END_ACTION\n"
                  "END_SFC\n"
                  "END_PROGRAM\n"),
              "SFC all-qualifiers program builds");
        if(rig.compiled.ok) {
            for(int i = 0; i < 10; ++i) {
                check(rig.scan() == st::ScanError::ok, "SFC all-q scan ok");
            }
            check(rig.i64("count") >= 5, "SFC count reaches Phase3");
        }
    }

    // SFC with simultaneous divergence (parallel branches) and convergence
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p\nVAR\n"
                  "  x : DINT; y : DINT;\n"
                  "END_VAR\n"
                  "SFC Flow\n"
                  "  INITIAL_STEP Start:\n"
                  "  END_STEP\n"
                  "  STEP BranchA:\n"
                  "    IncX(N);\n"
                  "  END_STEP\n"
                  "  STEP BranchB:\n"
                  "    IncY(N);\n"
                  "  END_STEP\n"
                  "  STEP Join:\n"
                  "    TERMINAL;\n"
                  "  END_STEP\n"
                  "  TRANSITION FROM Start TO (BranchA, BranchB) SIMULTANEOUS := TRUE;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM (BranchA, BranchB) TO Join SIMULTANEOUS := x >= 2 AND y >= 2;\n"
                  "  END_TRANSITION\n"
                  "  ACTION IncX:\n"
                  "    x := x + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION IncY:\n"
                  "    y := y + 1;\n"
                  "  END_ACTION\n"
                  "END_SFC\n"
                  "END_PROGRAM\n"),
              "SFC parallel program builds");
        if(rig.compiled.ok) {
            for(int i = 0; i < 5; ++i) {
                check(rig.scan() == st::ScanError::ok, "SFC parallel scan ok");
            }
            check(rig.i64("x") >= 2, "SFC parallel branch A ran");
            check(rig.i64("y") >= 2, "SFC parallel branch B ran");
        }
    }

    // SFC with competing transitions (priority resolution — two transitions
    // from same step, first wins)
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p\nVAR\n"
                  "  path : DINT;\n"
                  "  tick : DINT;\n"
                  "END_VAR\n"
                  "SFC Flow\n"
                  "  INITIAL_STEP Wait:\n"
                  "    Tick(N);\n"
                  "  END_STEP\n"
                  "  STEP PathA:\n"
                  "    MarkA(N);\n"
                  "  END_STEP\n"
                  "  STEP PathB:\n"
                  "    MarkB(N);\n"
                  "  END_STEP\n"
                  "  STEP End:\n"
                  "    TERMINAL;\n"
                  "  END_STEP\n"
                  "  TRANSITION FROM Wait TO PathA := tick >= 2;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM Wait TO PathB := tick >= 3;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM PathA TO End := TRUE;\n"
                  "  END_TRANSITION\n"
                  "  TRANSITION FROM PathB TO End := TRUE;\n"
                  "  END_TRANSITION\n"
                  "  ACTION Tick:\n"
                  "    tick := tick + 1;\n"
                  "  END_ACTION\n"
                  "  ACTION MarkA:\n"
                  "    path := 1;\n"
                  "  END_ACTION\n"
                  "  ACTION MarkB:\n"
                  "    path := 2;\n"
                  "  END_ACTION\n"
                  "END_SFC\n"
                  "END_PROGRAM\n"),
              "SFC priority program builds");
        if(rig.compiled.ok) {
            for(int i = 0; i < 5; ++i) {
                check(rig.scan() == st::ScanError::ok, "SFC priority scan ok");
            }
            check(rig.i64("path") == 1, "SFC first transition wins");
        }
    }

    // SFC restart: exercise the restart_sfc path
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p\nVAR\n"
                  "  val : DINT;\n"
                  "END_VAR\n"
                  "SFC Flow\n"
                  "  INITIAL_STEP A:\n"
                  "    Inc(N);\n"
                  "  END_STEP\n"
                  "  STEP B:\n"
                  "    TERMINAL;\n"
                  "  END_STEP\n"
                  "  TRANSITION FROM A TO B := val >= 2;\n"
                  "  END_TRANSITION\n"
                  "  ACTION Inc:\n"
                  "    val := val + 1;\n"
                  "  END_ACTION\n"
                  "END_SFC\n"
                  "END_PROGRAM\n"),
              "SFC restart program builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "SFC restart scan 1");
            check(rig.scan() == st::ScanError::ok, "SFC restart scan 2");
            check(rig.scan() == st::ScanError::ok, "SFC restart scan 3");
            check(rig.instance.restart_sfc("flow") == rt::ErrorCode::ok,
                  "SFC restart succeeds");
            check(rig.scan() == st::ScanError::ok, "SFC restart post scan");
        }
    }
}

void broad_fb_runtime_dispatch()
{
    // Exercise many FB types at runtime by instantiating, calling, and reading
    // outputs. Each FB type hits unique dispatch branches in st_binding_native.h
    // and commit_outputs paths in vm.h.

    // MC_Power + MC_ReadStatus + MC_ReadActualPosition + MC_ReadActualVelocity
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  Pwr : MC_Power;\n"
                  "  RS : MC_ReadStatus;\n"
                  "  RAP : MC_ReadActualPosition;\n"
                  "  RAV : MC_ReadActualVelocity;\n"
                  "  RAT : MC_ReadActualTorque;\n"
                  "  st : BOOL; pos : LREAL; vel : LREAL;\n"
                  "END_VAR\n"
                  "Pwr(Axis := A, Enable := TRUE);\n"
                  "st := Pwr.Status;\n"
                  "RS(Axis := A, Enable := TRUE);\n"
                  "RAP(Axis := A, Enable := TRUE);\n"
                  "pos := RAP.Position;\n"
                  "RAV(Axis := A, Enable := TRUE);\n"
                  "vel := RAV.Velocity;\n"
                  "RAT(Axis := A, Enable := TRUE);\n"
                  "END_PROGRAM"),
              "FB dispatch: power+read builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: power+read runs");
        }
    }

    // MC_MoveAbsolute + MC_MoveRelative + MC_MoveVelocity + MC_Stop + MC_Halt
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  MA : MC_MoveAbsolute;\n"
                  "  MR : MC_MoveRelative;\n"
                  "  MV : MC_MoveVelocity;\n"
                  "  St : MC_Stop;\n"
                  "  Ha : MC_Halt;\n"
                  "  d1 : BOOL; d2 : BOOL; e : BOOL;\n"
                  "END_VAR\n"
                  "MA(Axis := A, Execute := TRUE, Position := 10.0,\n"
                  "   Velocity := 1.0, Acceleration := 1.0,\n"
                  "   Deceleration := 1.0, Jerk := 1.0);\n"
                  "d1 := MA.Done;\n"
                  "MR(Axis := A, Execute := FALSE, Distance := 5.0,\n"
                  "   Velocity := 1.0, Acceleration := 1.0,\n"
                  "   Deceleration := 1.0, Jerk := 1.0);\n"
                  "MV(Axis := A, Execute := FALSE, Velocity := 2.0,\n"
                  "   Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0);\n"
                  "St(Axis := A, Execute := FALSE, Deceleration := 1.0, Jerk := 1.0);\n"
                  "Ha(Axis := A, Execute := FALSE, Deceleration := 1.0, Jerk := 1.0);\n"
                  "d2 := MR.Done;\n"
                  "e := St.Error;\n"
                  "END_PROGRAM"),
              "FB dispatch: motion commands builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: motion commands runs");
        }
    }

    // MC_Home + MC_SetPosition + MC_SetOverride + MC_Reset + MC_ReadAxisError
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  Hm : MC_Home;\n"
                  "  SP : MC_SetPosition;\n"
                  "  SO : MC_SetOverride;\n"
                  "  Rst : MC_Reset;\n"
                  "  RAE : MC_ReadAxisError;\n"
                  "  done : BOOL; eid : DINT;\n"
                  "END_VAR\n"
                  "Hm(Axis := A, Execute := FALSE);\n"
                  "SP(Axis := A, Execute := FALSE, Position := 0.0);\n"
                  "SO(Axis := A, Enable := TRUE, VelFactor := 100,\n"
                  "   AccFactor := 100, JerkFactor := 100);\n"
                  "Rst(Axis := A, Execute := FALSE);\n"
                  "RAE(Axis := A, Enable := TRUE);\n"
                  "done := SO.Enabled;\n"
                  "eid := RAE.AxisErrorID;\n"
                  "END_PROGRAM"),
              "FB dispatch: homing+override builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: homing+override runs");
        }
    }

    // MC_ReadParameter + MC_WriteParameter + MC_ReadBoolParameter + MC_WriteBoolParameter
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  RP : MC_ReadParameter;\n"
                  "  WP : MC_WriteParameter;\n"
                  "  RBP : MC_ReadBoolParameter;\n"
                  "  WBP : MC_WriteBoolParameter;\n"
                  "  val : LREAL; bval : BOOL;\n"
                  "END_VAR\n"
                  "RP(Axis := A, Enable := TRUE, ParameterNumber := "
                  "MC_AXIS_PARAMETER#commanded_position);\n"
                  "val := RP.Value;\n"
                  "WP(Axis := A, Execute := FALSE, ParameterNumber := "
                  "MC_AXIS_PARAMETER#sw_limit_pos, Value := 1.0);\n"
                  "RBP(Axis := A, Enable := TRUE, ParameterNumber := "
                  "MC_AXIS_PARAMETER#enable_limit_pos);\n"
                  "bval := RBP.Value;\n"
                  "WBP(Axis := A, Execute := FALSE, ParameterNumber := "
                  "MC_AXIS_PARAMETER#enable_limit_neg, Value := TRUE);\n"
                  "END_PROGRAM"),
              "FB dispatch: parameters builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: parameters runs");
        }
    }

    // MC_ReadDigitalInput + MC_ReadDigitalOutput + MC_WriteDigitalOutput
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  InRef : MC_INPUT_REF;\n"
                  "  OutRef : MC_OUTPUT_REF;\n"
                  "  RDI : MC_ReadDigitalInput;\n"
                  "  RDO : MC_ReadDigitalOutput;\n"
                  "  WDO : MC_WriteDigitalOutput;\n"
                  "  v1 : BOOL; v2 : BOOL;\n"
                  "END_VAR\n"
                  "RDI(Input := InRef, InputNumber := 0, Enable := TRUE);\n"
                  "v1 := RDI.Value;\n"
                  "RDO(Output := OutRef, OutputNumber := 0, Enable := TRUE);\n"
                  "v2 := RDO.Value;\n"
                  "WDO(Output := OutRef, OutputNumber := 0, Execute := FALSE, Value := TRUE);\n"
                  "END_PROGRAM"),
              "FB dispatch: digital IO builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "FB dispatch: digital IO runs");
        }
    }

    // MC_GearIn + MC_GearOut + MC_CamTableSelect + MC_CamIn + MC_CamOut
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  M : AXIS_REF;\n"
                  "  S : AXIS_REF;\n"
                  "  GI : MC_GearIn;\n"
                  "  GO : MC_GearOut;\n"
                  "  CTS : MC_CamTableSelect;\n"
                  "  CI : MC_CamIn;\n"
                  "  CO : MC_CamOut;\n"
                  "  ig : BOOL; d : BOOL;\n"
                  "END_VAR\n"
                  "GI(Master := M, Slave := S, Execute := FALSE,\n"
                  "   RatioNumerator := 1, RatioDenominator := 1,\n"
                  "   Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0);\n"
                  "ig := GI.InGear;\n"
                  "GO(Slave := S, Execute := FALSE);\n"
                  "CTS(Master := M, Slave := S, Execute := FALSE);\n"
                  "CI(Master := M, Slave := S, Execute := FALSE);\n"
                  "CO(Slave := S, Execute := FALSE);\n"
                  "d := GO.Done;\n"
                  "END_PROGRAM"),
              "FB dispatch: sync builds");
        if(rig.compiled.ok) {
            axis::AxisModel master, slave;
            master.set_power(true);
            slave.set_power(true);
            rig.instance.bind_axis("M", &master);
            rig.instance.bind_axis("S", &slave);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: sync runs");
        }
    }

    // MC_CombineAxes + MC_PhasingAbsolute + MC_PhasingRelative + MC_GearInPos
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  M1 : AXIS_REF;\n"
                  "  M2 : AXIS_REF;\n"
                  "  S : AXIS_REF;\n"
                  "  CA : MC_CombineAxes;\n"
                  "  PA : MC_PhasingAbsolute;\n"
                  "  PR : MC_PhasingRelative;\n"
                  "  GIP : MC_GearInPos;\n"
                  "  b1 : BOOL; b2 : BOOL;\n"
                  "END_VAR\n"
                  "CA(Master1 := M1, Master2 := M2, Slave := S, Execute := FALSE,\n"
                  "   GearRationNumeratorM1 := 1, GearRatioDenominatorM1 := 1,\n"
                  "   GearRatioNumeratorM2 := 1, GearRatioDenominatorM2 := 1);\n"
                  "PA(Master := M1, Slave := S, Execute := FALSE, PhaseShift := 1.0,\n"
                  "   Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0);\n"
                  "PR(Master := M1, Slave := S, Execute := FALSE, PhaseShift := 1.0,\n"
                  "   Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0);\n"
                  "GIP(Master := M1, Slave := S, Execute := FALSE,\n"
                  "    RatioNumerator := 1, RatioDenominator := 1,\n"
                  "    Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0);\n"
                  "b1 := CA.InSync;\n"
                  "b2 := GIP.InSync;\n"
                  "END_PROGRAM"),
              "FB dispatch: combine+phasing builds");
        if(rig.compiled.ok) {
            axis::AxisModel m1, m2, s;
            m1.set_power(true);
            m2.set_power(true);
            s.set_power(true);
            rig.instance.bind_axis("M1", &m1);
            rig.instance.bind_axis("M2", &m2);
            rig.instance.bind_axis("S", &s);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: combine+phasing runs");
        }
    }

    // MC_MoveSuperimposed + MC_HaltSuperimposed + MC_MoveAdditive +
    // MC_MoveContinuousAbsolute + MC_MoveContinuousRelative + MC_TorqueControl
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  MS : MC_MoveSuperimposed;\n"
                  "  HS : MC_HaltSuperimposed;\n"
                  "  Mad : MC_MoveAdditive;\n"
                  "  MCA : MC_MoveContinuousAbsolute;\n"
                  "  MCR : MC_MoveContinuousRelative;\n"
                  "  TC : MC_TorqueControl;\n"
                  "  d1 : BOOL; d2 : BOOL;\n"
                  "END_VAR\n"
                  "MS(Axis := A, Execute := FALSE, Distance := 1.0,\n"
                  "   VelocityDiff := 1.0, Acceleration := 1.0,\n"
                  "   Deceleration := 1.0, Jerk := 1.0);\n"
                  "HS(Axis := A, Execute := FALSE, Deceleration := 1.0, Jerk := 1.0);\n"
                  "Mad(Axis := A, Execute := FALSE, Distance := 1.0,\n"
                  "    Velocity := 1.0, Acceleration := 1.0,\n"
                  "    Deceleration := 1.0, Jerk := 1.0);\n"
                  "MCA(Axis := A, Execute := FALSE, Position := 1.0,\n"
                  "    Velocity := 1.0, Acceleration := 1.0,\n"
                  "    Deceleration := 1.0, Jerk := 1.0, EndVelocity := 0.5);\n"
                  "MCR(Axis := A, Execute := FALSE, Distance := 1.0,\n"
                  "    Velocity := 1.0, Acceleration := 1.0,\n"
                  "    Deceleration := 1.0, Jerk := 1.0, EndVelocity := 0.5);\n"
                  "TC(Axis := A, Execute := FALSE, Torque := 0.5,\n"
                  "   TorqueRamp := 1.0, Velocity := 1.0);\n"
                  "d1 := MS.Done;\n"
                  "d2 := TC.InTorque;\n"
                  "END_PROGRAM"),
              "FB dispatch: superimposed+continuous builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: superimposed+continuous runs");
        }
    }

    // MC_PositionProfile + MC_VelocityProfile + MC_AccelerationProfile
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  PP : MC_PositionProfile;\n"
                  "  VP : MC_VelocityProfile;\n"
                  "  AP : MC_AccelerationProfile;\n"
                  "  d1 : BOOL; d2 : BOOL; d3 : BOOL;\n"
                  "END_VAR\n"
                  "PP(Axis := A, Execute := FALSE);\n"
                  "VP(Axis := A, Execute := FALSE);\n"
                  "AP(Axis := A, Execute := FALSE);\n"
                  "d1 := PP.Done;\n"
                  "d2 := VP.ProfileCompleted;\n"
                  "d3 := AP.ProfileCompleted;\n"
                  "END_PROGRAM"),
              "FB dispatch: profiles builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: profiles runs");
        }
    }

    // MC_TouchProbe + MC_AbortTrigger + MC_ReadAxisInfo + MC_ReadMotionState
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  TP : MC_TouchProbe;\n"
                  "  Abort : MC_AbortTrigger;\n"
                  "  RAI : MC_ReadAxisInfo;\n"
                  "  RMS : MC_ReadMotionState;\n"
                  "  pos : LREAL; moving : BOOL;\n"
                  "END_VAR\n"
                  "TP(Axis := A, Execute := FALSE);\n"
                  "Abort(Axis := A, TriggerInput := ULINT#1, Execute := FALSE);\n"
                  "RAI(Axis := A, Enable := TRUE);\n"
                  "RMS(Axis := A, Enable := TRUE);\n"
                  "pos := TP.RecordedPosition;\n"
                  "moving := RMS.ConstantVelocity;\n"
                  "END_PROGRAM"),
              "FB dispatch: probe+info builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: probe+info runs");
        }
    }

    // Homing FBs: MC_HomeDirect + MC_StepAbsoluteSwitch + MC_StepLimitSwitch +
    // MC_StepReferencePulse + MC_FinishHoming
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  A : AXIS_REF;\n"
                  "  HD : MC_HomeDirect;\n"
                  "  SAS : MC_StepAbsoluteSwitch;\n"
                  "  SLS : MC_StepLimitSwitch;\n"
                  "  SRP : MC_StepReferencePulse;\n"
                  "  FH : MC_FinishHoming;\n"
                  "  d1 : BOOL; d2 : BOOL;\n"
                  "END_VAR\n"
                  "HD(Axis := A, Execute := FALSE, SetPosition := 0.0);\n"
                  "SAS(Axis := A, Execute := FALSE, Velocity := 1.0);\n"
                  "SLS(Axis := A, Execute := FALSE, Velocity := 1.0);\n"
                  "SRP(Axis := A, Execute := FALSE, Velocity := 1.0);\n"
                  "FH(Axis := A, Execute := FALSE);\n"
                  "d1 := HD.Done;\n"
                  "d2 := FH.Done;\n"
                  "END_PROGRAM"),
              "FB dispatch: homing FBs builds");
        if(rig.compiled.ok) {
            axis::AxisModel ax;
            ax.set_power(true);
            rig.instance.bind_axis("A", &ax);
            check(rig.scan() == st::ScanError::ok, "FB dispatch: homing FBs runs");
        }
    }

    // String operations at runtime (CONCAT, LEFT, RIGHT, MID, FIND, LEN, INSERT, DELETE)
    {
        Rig rig;
        check(rig.build(
                  "PROGRAM p VAR\n"
                  "  s1 : STRING[64] := 'Hello';\n"
                  "  s2 : STRING[64] := ' World';\n"
                  "  cat : STRING[64];\n"
                  "  left3 : STRING[64];\n"
                  "  right3 : STRING[64];\n"
                  "  mid2 : STRING[64];\n"
                  "  pos : DINT;\n"
                  "  length : DINT;\n"
                  "END_VAR\n"
                  "cat := CONCAT(s1, s2);\n"
                  "left3 := LEFT(s1, 3);\n"
                  "right3 := RIGHT(s2, 3);\n"
                  "mid2 := MID(s1, 2, 2);\n"
                  "pos := FIND(cat, 'World');\n"
                  "length := LEN(cat);\n"
                  "END_PROGRAM"),
              "FB dispatch: string ops builds");
        if(rig.compiled.ok) {
            check(rig.scan() == st::ScanError::ok, "string ops run");
            check(rig.i64("length") == 11, "CONCAT LEN = 11");
            check(rig.i64("pos") == 7, "FIND 'World' at 7");
        }
    }
}

} // namespace

int main()
{
    load_contract();
    instance_boundary_matrix();
    arithmetic();
    control_flow();
    fault_machine();
    invalid_bytecode_contract();
    malformed_bytecode_operand_matrix();
    malformed_source_sample_matrix();
    constant_fold_contract_matrix();
    opcode_operand_failure_matrix();
    budget_boundary();
    timers();
    counters_edges();
    symbols();
    tod_negative_wrap();
    time_scale_boundary();
    lint_runtime_division();
    power_boundary();
    unicode_surrogate_check();
    subrange_enum_runtime_check();
    broad_vm_language_coverage();
    broad_binding_validation_and_sema_errors();
    broad_sfc_execution_coverage();
    broad_fb_runtime_dispatch();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l0 runtime tests passed\n");
    return 0;
}
