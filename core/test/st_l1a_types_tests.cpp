// L1a scalar-universe acceptance (approved st-l1a-semantics 2.x/3.x/5.x,
// metrics 7.2/7.3/7.5): widening whitelist positive/negative, full-width
// wrap boundaries, typed literals, VAR CONSTANT, bit strings, unsigned
// division/comparison, CONTINUE, ** and TIME scaling with fault paths.

#include <cmath>
#include <cstdio>
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

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBigBudget = 1000000;

std::string wrap(const char *vars, const char *body)
{
    std::string source = "PROGRAM p\nVAR\n";
    source += vars;
    source += "\nEND_VAR\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[65536] = {};

    bool build(const std::string &source)
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
                             kPeriodNs) == rt::ErrorCode::ok;
    }

    st::ScanError scan(std::int64_t budget = kBigBudget)
    {
        return instance.scan(budget);
    }

    std::int64_t i64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? -424242
                         : instance.value_i64(
                               static_cast<std::size_t>(index));
    }

    double f64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? -424242.0
                         : instance.value_f64(static_cast<std::size_t>(index));
    }
};

bool compiles(const char *vars, const char *body)
{
    return st::compile(wrap(vars, body)).ok;
}

// --- widening whitelist (matrix 3.1/3.2, anchor L1a-3.1-widen) --------------

void widening()
{
    // one positive case per whitelist lane
    check(compiles("a : SINT; b : INT;", "b := a;"), "SINT->INT");
    check(compiles("a : INT; b : DINT;", "b := a;"), "INT->DINT");
    check(compiles("a : DINT; b : LINT;", "b := a;"), "DINT->LINT");
    check(compiles("a : USINT; b : UINT;", "b := a;"), "USINT->UINT");
    check(compiles("a : UDINT; b : ULINT;", "b := a;"), "UDINT->ULINT");
    check(compiles("a : USINT; b : INT;", "b := a;"), "USINT->INT");
    check(compiles("a : UINT; b : DINT;", "b := a;"), "UINT->DINT");
    check(compiles("a : UDINT; b : LINT;", "b := a;"), "UDINT->LINT");
    check(compiles("a : REAL; b : LREAL;", "b := a;"), "REAL->LREAL");
    check(compiles("a : BYTE; b : WORD;", "b := a;"), "BYTE->WORD");
    check(compiles("a : DWORD; b : LWORD;", "b := a;"), "DWORD->LWORD");
    check(compiles("a : SINT; b : DINT; c : DINT;", "c := b + a;"),
          "mixed arithmetic widens to the wider side");

    // lossy / cross-domain negatives (anchor L1a-3.1-widen-neg)
    check(!compiles("a : INT; b : SINT;", "b := a;"), "INT->SINT rejected");
    check(!compiles("a : LINT; b : DINT;", "b := a;"), "LINT->DINT rejected");
    check(!compiles("a : UINT; b : INT;", "b := a;"),
          "same-width cross-sign rejected");
    check(!compiles("a : INT; b : UINT;", "b := a;"),
          "signed->unsigned rejected");
    check(!compiles("a : ULINT; b : LINT;", "b := a;"),
          "ULINT->LINT rejected");
    check(!compiles("a : LREAL; b : REAL;", "b := a;"),
          "LREAL->REAL rejected");
    check(!compiles("a : DINT; b : REAL;", "b := a;"),
          "integer->float never implicit");
    check(!compiles("a : REAL; b : DINT;", "b := a;"),
          "float->integer never implicit");
    check(!compiles("a : BYTE; b : USINT;", "b := a;"),
          "bit-string->integer rejected");
    check(!compiles("a : USINT; b : BYTE;", "b := a;"),
          "integer->bit-string rejected");
    check(!compiles("a : WORD; b : BYTE;", "b := a;"),
          "bit-string narrowing rejected");
    check(!compiles("a : LWORD; b : LINT;", "b := a;"),
          "LWORD->LINT rejected");
}

// --- full-width wrap boundaries (matrix 2.1/2.2, anchor L1a-2.1-wrap) -------

void wrap_boundaries()
{
    Rig rig;
    check(rig.build(wrap(
              "s8 : SINT; s64 : LINT; u8 : USINT; u16 : UINT; u32 : UDINT;"
              "u64 : ULINT; q : BOOL;",
              "s8 := 127; s8 := s8 + 1;"
              "s64 := 9223372036854775807; s64 := s64 + 1;"
              "u8 := 255; u8 := u8 + 1;"
              "u16 := 0; u16 := u16 - 1;"
              "u32 := 4294967295; u32 := u32 + 1;"
              "u64 := 18446744073709551615; u64 := u64 + 1;"
              "q := u64 = 0;")),
          "wrap program builds");
    check(rig.scan() == st::ScanError::ok, "wrap scan ok");
    check(rig.i64("s8") == -128, "SINT wrap");
    check(rig.i64("s64") == -9223372036854775807LL - 1, "LINT wrap");
    check(rig.i64("u8") == 0, "USINT wrap");
    check(rig.i64("u16") == 65535, "UINT wrap down");
    check(rig.i64("u32") == 0, "UDINT wrap");
    check(rig.i64("q") == 1, "ULINT wrap to zero");

    // unsigned division and ordering are genuinely unsigned
    Rig unsigned_ops;
    check(unsigned_ops.build(wrap(
              "a : UINT; b : UINT; q : BOOL; d : UINT; m : UINT;",
              "a := 0; a := a - 1;" // 65535
              "b := 2;"
              "d := a / b;"          // 32767
              "m := a MOD 10;"       // 5
              "q := a > 32767;")),   // unsigned compare: true
          "unsigned ops build");
    check(unsigned_ops.scan() == st::ScanError::ok, "unsigned scan ok");
    check(unsigned_ops.i64("d") == 32767, "unsigned division");
    check(unsigned_ops.i64("m") == 5, "unsigned modulo");
    check(unsigned_ops.i64("q") == 1, "unsigned ordering");

    // signed full-width division guard: LINT min / -1 wraps, no trap
    Rig lint_div;
    check(lint_div.build(wrap(
              "a : LINT; b : LINT; c : LINT;",
              "a := -9223372036854775808; b := -1; c := a / b;")),
          "LINT min div builds");
    check(lint_div.scan() == st::ScanError::ok, "LINT min/-1 scan ok");
    check(lint_div.i64("c") == -9223372036854775807LL - 1,
          "LINT min / -1 wraps");
}

// --- typed literals and VAR CONSTANT (matrix 3.4/2.5) ------------------------

void typed_literals_constants()
{
    Rig rig;
    check(rig.build(wrap(
              "a : SINT; b : ULINT; c : WORD; d : LREAL; e : DINT;",
              "a := SINT#-128;"
              "b := ULINT#18_446_744_073_709_551_615;"
              "c := WORD#16#BEEF;"
              "d := LREAL#2.5;"
              "e := INT#100;")), // typed literal widens INT->DINT
          "typed literal program builds");
    check(rig.scan() == st::ScanError::ok, "typed literal scan");
    check(rig.i64("a") == -128, "SINT# literal");
    check(rig.i64("b") == -1, "ULINT# max canonical bits");
    check(rig.i64("c") == 0xBEEF, "WORD# based literal");
    check(rig.f64("d") == 2.5, "LREAL# literal");
    check(rig.i64("e") == 100, "typed literal widening");

    check(!compiles("a : SINT;", "a := SINT#200;"),
          "typed literal out of range");
    check(!compiles("a : DINT;", "a := INT#1.5;"),
          "real payload on integer typed literal");
    check(!compiles("a : INT;", "a := DINT#5;"),
          "typed literal cannot narrow");

    // anchor L1a-2.5-constant
    Rig constant;
    check(constant.build("PROGRAM p\nVAR CONSTANT limit : INT := 41; "
                         "END_VAR\nVAR x : INT; END_VAR\n"
                         "x := limit + 1;\nEND_PROGRAM\n"),
          "VAR CONSTANT builds");
    check(constant.scan() == st::ScanError::ok, "constant scan");
    check(constant.i64("x") == 42, "constant folded into expression");
    check(!st::compile("PROGRAM p\nVAR CONSTANT k : INT := 1; END_VAR\n"
                       "k := 2;\nEND_PROGRAM\n")
               .ok,
          "assignment to constant rejected");
    check(!st::compile("PROGRAM p\nVAR CONSTANT k : INT; END_VAR\n"
                       ";\nEND_PROGRAM\n")
               .ok,
          "constant without initializer rejected");
}

// --- bit strings (matrix 2.3, anchor L1a-2.3-bits) ---------------------------

void bitstrings()
{
    Rig rig;
    check(rig.build(wrap(
              "a : BYTE; b : BYTE; c : BYTE; d : WORD; q : BOOL;",
              "a := 16#F0; b := 16#3C;"
              "c := a AND b;"        // 16#30
              "c := c OR 16#03;"     // 16#33
              "c := c XOR 16#FF;"    // 16#CC
              "d := NOT WORD#16#00FF;" // 16#FF00
              "q := c = BYTE#16#CC;")),
          "bit string program builds");
    check(rig.scan() == st::ScanError::ok, "bit string scan");
    check(rig.i64("c") == 0xCC, "byte AND/OR/XOR chain");
    check(rig.i64("d") == 0xFF00, "WORD complement is width-masked");
    check(rig.i64("q") == 1, "bit string equality");

    check(!compiles("a : BYTE; b : BYTE; c : BYTE;", "c := a + b;"),
          "bit string arithmetic rejected");
    check(!compiles("a : BYTE; b : BYTE; q : BOOL;", "q := a < b;"),
          "bit string ordering rejected");
}

// --- CONTINUE / ** / non-formal calls / TIME scale (matrix 5.x) --------------

void operator_surface()
{
    // anchor L1a-5.3-continue
    Rig cont;
    check(cont.build(wrap(
              "i : INT; s : INT; w : INT;",
              "FOR i := 1 TO 10 DO"
              "  IF (i MOD 2) = 0 THEN CONTINUE; END_IF;"
              "  s := s + i;"
              "END_FOR;"                      // 1+3+5+7+9 = 25
              "WHILE w < 6 DO"
              "  w := w + 1;"
              "  IF w = 3 THEN CONTINUE; END_IF;"
              "  s := s + 100;"
              "END_WHILE;")),                 // 5 passes add 100
          "continue builds");
    check(cont.scan() == st::ScanError::ok, "continue scan");
    check(cont.i64("s") == 525, "CONTINUE skips iterations");
    check(!compiles("x : INT;", "CONTINUE;"), "CONTINUE outside loop");

    // anchor L1a-5.2-power
    Rig power;
    check(power.build(wrap(
              "l : LREAL; r : REAL; n : INT;",
              "l := 2.0 ** 10;"
              "n := 3;"
              "r := 2.0 ** n;"
              "l := l + 3.0 ** -2.0 * 9.0;")), // + ~1.0
          "power builds");
    check(power.scan() == st::ScanError::ok, "power scan");
    check(power.f64("l") == 1025.0, "LREAL power with integer exponent");
    check(power.f64("r") == 8.0, "REAL power with INT variable exponent");
    check(!compiles("n : INT;", "n := 2 ** 3;"),
          "integer base for ** rejected");

    // anchor L1a-5.1-time-scale
    Rig scale;
    check(scale.build(wrap(
              "t : TIME; u : TIME; v : TIME;",
              "t := T#1s500ms * 2;"
              "u := T#3s / 2;"
              "v := T#1s * 2.5;")),
          "time scale builds");
    check(scale.scan() == st::ScanError::ok, "time scale scan");
    check(scale.i64("t") == 3000000000LL, "TIME * int");
    check(scale.i64("u") == 1500000000LL, "TIME / int");
    check(scale.i64("v") == 2500000000LL, "TIME * real truncates at ns");

    Rig div_fault;
    check(div_fault.build(wrap("t : TIME; z : INT;", "t := T#1s / z;")),
          "time runtime div builds");
    check(div_fault.scan() == st::ScanError::division_by_zero,
          "TIME / 0 faults");

    Rig inf_fault;
    check(inf_fault.build(wrap("t : TIME; z : LREAL;", "t := T#1s / z;")),
          "time float div builds");
    check(inf_fault.scan() == st::ScanError::conversion_invalid,
          "TIME / 0.0 faults as conversion_invalid");

    // non-formal FB call (anchor L1a-5.4-nonformal)
    Rig call;
    check(call.build(wrap(
              "t : TON; q : BOOL;",
              "t(TRUE, T#0s); q := t.Q;")),
          "non-formal call builds");
    check(call.scan() == st::ScanError::ok && call.i64("q") == 1,
          "non-formal call drives pins in declaration order");
    check(!compiles("t : TON;", "t(TRUE);"),
          "non-formal call must cover every input");
    check(!compiles("t : TON;", "t(TRUE, PT := T#1s);"),
          "mixed call forms rejected");
}

// Keep operands in variables so sema, codegen and every VM opcode lane are
// exercised instead of disappearing into constant folding.
void dynamic_operator_matrix()
{
    Rig rig;
    check(rig.build(wrap(
              "sa : SINT := 7; sb : SINT := 3; sr : SINT; "
              "ua : USINT := 7; ub : USINT := 3; ur : USINT; "
              "ra : REAL := 7.5; rb : REAL := 2.0; rr : REAL; "
              "la : LREAL := 7.5; lb : LREAL := 2.0; lr : LREAL; "
              "ba : BYTE := BYTE#16#A5; bb : BYTE := BYTE#16#3C; br : BYTE; "
              "p : BOOL := TRUE; n : BOOL; q0 : BOOL; q1 : BOOL; q2 : BOOL; "
              "q3 : BOOL; q4 : BOOL; q5 : BOOL; q6 : BOOL; q7 : BOOL; "
              "t : TIME := T#3s; ti : TIME; tf : TIME; scale : INT := 2; "
              "factor : LREAL := 2.5;",
              "sr := sa + sb; sr := sa - sb; sr := sa * sb; "
              "sr := sa / sb; sr := sa MOD sb; "
              "ur := ua + ub; ur := ua - ub; ur := ua * ub; "
              "ur := ua / ub; ur := ua MOD ub; "
              "rr := ra + rb; rr := ra - rb; rr := ra * rb; rr := ra / rb; "
              "lr := la + lb; lr := la - lb; lr := la * lb; lr := la / lb; "
              "q0 := sa = sb; q1 := sa <> sb; q2 := sa < sb; "
              "q3 := sa > sb; q4 := sa <= sb; q5 := sa >= sb; "
              "q6 := ua < ub; q7 := ua >= ub; "
              "n := NOT p; q0 := p AND n; q1 := p OR n; q2 := p XOR n; "
              "br := ba AND bb; br := ba OR bb; br := ba XOR bb; "
              "br := NOT ba; "
              "ti := t * scale; ti := t / scale; "
              "tf := t * factor; tf := t / factor;")),
          "dynamic operator matrix builds");
    check(rig.scan() == st::ScanError::ok,
          "dynamic operator matrix scans");
    check(rig.i64("sr") == 1 && rig.i64("ur") == 1,
          "dynamic integer operators");
    check(rig.f64("rr") == 3.75 && rig.f64("lr") == 3.75,
          "dynamic floating operators");
    check(rig.i64("q0") == 0 && rig.i64("q1") == 1 &&
              rig.i64("q2") == 1 && rig.i64("q3") == 1 &&
              rig.i64("q4") == 0 && rig.i64("q5") == 1 &&
              rig.i64("q6") == 0 && rig.i64("q7") == 1,
          "dynamic comparison and BOOL operators");
    check(rig.i64("br") == 0x5A, "dynamic bit-string operators");
    check(rig.i64("ti") == 1500000000LL &&
              rig.i64("tf") == 1200000000LL,
          "dynamic TIME scale operators");
}

} // namespace

int main()
{
    widening();
    wrap_boundaries();
    typed_literals_constants();
    bitstrings();
    operator_surface();
    dynamic_operator_matrix();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l1a types tests passed\n");
    return 0;
}
