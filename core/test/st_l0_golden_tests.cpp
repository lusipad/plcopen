// L0 golden program set (approved st-l0-semantics metric 5.1): >=30
// hand-derived programs, one per language construct family, each run for a
// fixed number of scans and compared against manually computed expectations
// (provenance discipline: every case is self-authored; the oracle is the
// hand derivation in the comment).

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

constexpr std::int64_t kPeriodNs = 1000000; // 1 ms

struct Golden
{
    const char *name;
    const char *vars;
    const char *body;
    int scans;
    const char *var;    // checked variable
    long long expect;   // expected canonical integer value
};

// Integer-checked golden programs. Hand derivations in trailing comments.
const Golden kGolden[] = {
    {"assign-chain", "a : INT; b : INT;", "a := 5; b := a; a := b + 1;", 1,
     "a", 6},
    {"precedence", "x : DINT;", "x := 2 + 3 * 4;", 1, "x", 14},
    {"parens", "x : DINT;", "x := (2 + 3) * 4;", 1, "x", 20},
    {"unary-minus", "x : INT;", "x := -5 + 2;", 1, "x", -3},
    {"double-negate", "x : INT;", "x := - - 7;", 1, "x", 7},
    {"mod-positive", "x : INT;", "x := 17 MOD 5;", 1, "x", 2},
    {"div-trunc", "x : INT;", "x := -9 / 2;", 1, "x", -4},
    {"int-wrap-mul", "a : INT;", "a := 300; a := a * a * a;", 1, "a",
     -832}, // per-op wrap: 300*300=90000->24464; 24464*300=7339200
            // -> 7339200 mod 65536 = 64704 -> signed -832
    {"dint-wrap-add", "a : DINT;", "a := 2147483647; a := a + 2;", 1, "a",
     -2147483647LL},
    {"based-bits", "a : INT;", "a := 16#8000;", 1, "a", -32768},
    {"underscored", "a : DINT;", "a := 1_000_00 * 10;", 1, "a", 1000000},
    {"bool-and-or", "a : BOOL; b : BOOL; c : BOOL;",
     "a := TRUE; b := FALSE; c := a AND NOT b OR FALSE;", 1, "c", 1},
    {"bool-xor", "c : BOOL;", "c := TRUE XOR TRUE;", 1, "c", 0},
    {"ampersand-and", "c : BOOL;", "c := TRUE & TRUE;", 1, "c", 1},
    {"cmp-chain-int", "c : BOOL;", "c := (3 < 4) AND (4 <= 4) AND (5 > 4) "
                                   "AND (4 >= 4) AND (3 <> 4) AND (4 = 4);",
     1, "c", 1},
    {"cmp-time", "c : BOOL;", "c := T#1s > T#999ms;", 1, "c", 1},
    {"if-else", "x : INT; y : INT;",
     "x := 3; IF x = 1 THEN y := 10; ELSIF x = 2 THEN y := 20; "
     "ELSE y := 30; END_IF;",
     1, "y", 30},
    {"if-no-else", "x : INT; y : INT;",
     "IF x = 0 THEN y := 7; END_IF;", 1, "y", 7},
    {"case-value", "x : INT; y : INT;",
     "x := 2; CASE x OF 1: y := 1; 2: y := 2; END_CASE;", 1, "y", 2},
    {"case-multi-label", "x : INT; y : INT;",
     "x := 9; CASE x OF 1, 9, 12: y := 5; ELSE y := 6; END_CASE;", 1, "y",
     5},
    {"case-range", "x : INT; y : INT;",
     "x := 7; CASE x OF 1..3: y := 1; 4..9: y := 2; END_CASE;", 1, "y", 2},
    {"case-else", "x : INT; y : INT;",
     "x := 99; CASE x OF 1: y := 1; ELSE y := 42; END_CASE;", 1, "y", 42},
    {"case-negative", "x : INT; y : INT;",
     "x := -2; CASE x OF -3..-1: y := 8; END_CASE;", 1, "y", 8},
    {"case-no-match", "x : INT; y : INT;",
     "x := 5; y := 1; CASE x OF 1: y := 2; END_CASE;", 1, "y", 1},
    {"for-sum", "i : INT; s : INT;",
     "FOR i := 1 TO 10 DO s := s + i; END_FOR;", 1, "s", 55},
    {"for-by", "i : INT; s : INT;",
     "FOR i := 0 TO 20 BY 5 DO s := s + 1; END_FOR;", 1, "s", 5},
    {"for-desc", "i : DINT; s : DINT;",
     "FOR i := 3 TO 1 BY -1 DO s := s * 10 + i; END_FOR;", 1, "s", 321},
    {"for-empty", "i : INT; s : INT;",
     "s := 9; FOR i := 5 TO 1 DO s := 0; END_FOR;", 1, "s", 9},
    {"for-nested", "i : INT; j : INT; s : INT;",
     "FOR i := 1 TO 3 DO FOR j := 1 TO 4 DO s := s + 1; END_FOR; END_FOR;",
     1, "s", 12},
    {"for-exit", "i : INT; s : INT;",
     "FOR i := 1 TO 100 DO IF i = 4 THEN EXIT; END_IF; s := s + i; "
     "END_FOR;",
     1, "s", 6},
    {"while-countdown", "n : INT; steps : INT;",
     "n := 16; WHILE n > 1 DO n := n / 2; steps := steps + 1; END_WHILE;",
     1, "steps", 4},
    {"repeat-once", "n : INT;",
     "REPEAT n := n + 1; UNTIL TRUE END_REPEAT;", 1, "n", 1},
    {"repeat-until", "n : INT;",
     "REPEAT n := n + 3; UNTIL n >= 10 END_REPEAT;", 1, "n", 12},
    {"return-early", "x : INT;",
     "x := 1; IF TRUE THEN RETURN; END_IF; x := 2;", 1, "x", 1},
    {"empty-statements", "x : INT;", ";; x := 4; ;", 1, "x", 4},
    {"init-defaults", "a : INT; b : BOOL; t : TIME; c : DINT := 12;",
     ";", 1, "c", 12},
    {"multi-scan-accumulate", "n : DINT;", "n := n + 2;", 5, "n", 10},
    {"time-accumulate", "t : TIME;", "t := t + T#1.5ms;", 4, "t",
     6000000}, // 4 * 1.5 ms in ns
    {"time-negative", "t : TIME;", "t := T#1s + T#-1500ms;", 1, "t",
     -500000000LL},
    {"ton-immediate", "t : TON; q : BOOL;",
     "t(IN := TRUE, PT := T#0s); q := t.Q;", 1, "q", 1},
    {"tof-hold", "t : TOF; n : INT; q : BOOL;",
     "n := n + 1; t(IN := n = 1, PT := T#10ms); q := t.Q;", 3, "q",
     1}, // TOF keeps Q true while ET < PT after IN drops
    {"tp-pulse-end", "t : TP; n : INT; q : BOOL;",
     "n := n + 1; t(IN := TRUE, PT := T#2ms); q := t.Q;", 3, "q",
     0}, // 2 ms pulse at 1 ms scan ends by scan 3
    {"ctd-load-count", "c : CTD; n : INT; cv : DINT;",
     "n := n + 1; c(CD := (n MOD 2) = 0, LD := n = 1, PV := 3); cv := c.CV;",
     4, "cv", 1}, // scan1 loads 3; rising edges at n=2 and n=4 => 3-2 = 1
    {"f-trig-falling", "e : F_TRIG; n : INT; hits : INT;",
     "n := n + 1; e(CLK := n = 1); IF e.Q THEN hits := hits + 1; END_IF;", 3,
     "hits", 1},
    {"rs-reset-dominant", "f : RS; q : BOOL;",
     "f(S := TRUE, R1 := TRUE); q := f.Q1;", 1, "q", 0},
};

struct GoldenReal
{
    const char *name;
    const char *vars;
    const char *body;
    int scans;
    const char *var;
    double expect;
};

const GoldenReal kGoldenReal[] = {
    {"real-grid", "r : REAL;", "r := 0.1; r := r + 0.2;", 1, "r",
     static_cast<double>(0.1F + 0.2F)},
    {"lreal-sum", "l : LREAL;", "l := 0.1 + 0.2;", 1, "l", 0.1 + 0.2},
    {"real-div", "r : REAL;", "r := 1.0 / 3.0;", 1, "r",
     static_cast<double>(1.0F / 3.0F)},
    {"lreal-neg", "l : LREAL;", "l := -(1.5 + 0.25);", 1, "l", -1.75},
    {"lreal-int-literal", "l : LREAL;", "l := 3 * 4 + 1;", 1, "l", 13.0},
};

template <typename Case>
bool run_case(const Case &c, st::Instance &instance,
              unsigned char (&buffer)[65536])
{
    std::string source = "PROGRAM g\nVAR\n";
    source += c.vars;
    source += "\nEND_VAR\n";
    source += c.body;
    source += "\nEND_PROGRAM\n";
    // The program must outlive the instance (load contract), so it lives in
    // a static slot that persists past this call while the caller reads
    // symbols back.
    static st::CompileResult compiled;
    compiled = st::compile(source);
    if(!compiled.ok) {
        for(const st::Diagnostic &d : compiled.diagnostics) {
            std::printf("  %s diag %d:%d %s\n", c.name, d.line, d.column,
                        d.message.c_str());
        }
        fail(c.name);
        return false;
    }
    if(instance.load(compiled.program, buffer, sizeof(buffer), kPeriodNs) !=
       rt::ErrorCode::ok) {
        fail(c.name);
        return false;
    }
    for(int i = 0; i < c.scans; ++i) {
        if(instance.scan(1000000) != st::ScanError::ok) {
            fail(c.name);
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    static st::Instance instance;
    alignas(8) static unsigned char buffer[65536];

    int programs = 0;
    for(const Golden &c : kGolden) {
        ++programs;
        if(!run_case(c, instance, buffer)) {
            continue;
        }
        const int index = instance.find(c.var);
        if(index < 0 ||
           instance.value_i64(static_cast<std::size_t>(index)) != c.expect) {
            std::printf("  %s got %lld want %lld\n", c.name,
                        index < 0 ? -1LL
                                  : static_cast<long long>(instance.value_i64(
                                        static_cast<std::size_t>(index))),
                        c.expect);
            fail(c.name);
        }
    }
    for(const GoldenReal &c : kGoldenReal) {
        ++programs;
        if(!run_case(c, instance, buffer)) {
            continue;
        }
        const int index = instance.find(c.var);
        const double got =
            index < 0 ? -1.0
                      : instance.value_f64(static_cast<std::size_t>(index));
        if(index < 0 || got != c.expect) {
            std::printf("  %s got %.17g want %.17g\n", c.name, got, c.expect);
            fail(c.name);
        }
    }

    if(programs < 30) {
        fail("golden set below the 30-program floor");
    }
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l0 golden tests passed (%d programs)\n", programs);
    return 0;
}
