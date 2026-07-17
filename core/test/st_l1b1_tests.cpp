// L1b1 enum/subrange acceptance (approved st-l1b1-semantics sections 1-4).
// Plain-main tests deliberately name the new diagnostics and runtime fault:
// this file is the RED contract for the implementation, not a compatibility
// shim around the previously unsupported TYPE surface.

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

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

std::string unit(const char *types, const char *vars, const char *body)
{
    std::string source = types;
    source += "\nPROGRAM p\nVAR\n";
    source += vars;
    source += "\nEND_VAR\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 1000000;

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[65536] = {};

    bool build(const std::string &source)
    {
        compiled = st::compile(source);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        return instance.load(compiled.program, buffer, sizeof(buffer),
                             kPeriodNs) == rt::ErrorCode::ok;
    }

    st::ScanError scan()
    {
        return instance.scan(kBudget);
    }

    std::int64_t i64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? -424242
                         : instance.value_i64(
                               static_cast<std::size_t>(index));
    }
};

constexpr const char *kDirection =
    "TYPE Direction : (current := 0, positive := 1, negative := -1); "
    "END_TYPE";

// L1b1-1: declaration, implicit member numbering, default initialization,
// explicit values, repeated values, and declaration placement.
void enum_declarations()
{
    Rig rig;
    check(rig.build(unit(
              "TYPE Mode : (idle, running := 4, stopped, alias := 4); "
              "END_TYPE",
              "a : Mode; b : Mode := Mode#stopped; same : BOOL;",
              "same := Mode#running = Mode#alias;")),
          "L1b1-1 enum declaration builds");
    check(rig.scan() == st::ScanError::ok, "L1b1-1 enum declaration scans");
    check(rig.i64("a") == 0, "L1b1-1 enum defaults to first member");
    check(rig.i64("b") == 5, "L1b1-1 omitted value increments predecessor");
    check(rig.i64("same") == 1, "L1b1-1 repeated explicit values compare equal");

    const st::CompileResult duplicate = st::compile(unit(
        "TYPE Mode : (idle, idle); END_TYPE", "x : INT;", ";"));
    check(!duplicate.ok &&
              has_code(duplicate, st::DiagCode::sema_duplicate_identifier),
          "L1b1-1 duplicate member name rejected");

    check(!st::compile(unit("TYPE Mode : (too_big := 2147483648); END_TYPE",
                            "x : INT;", ";"))
               .ok,
          "L1b1-1 enum explicit value is DINT bounded");
    check(!st::compile(unit("", "x : INT; TYPE Mode : (idle); END_TYPE",
                            ";"))
               .ok,
          "L1b1-1 TYPE cannot appear in VAR");
    check(!st::compile(unit("", "x : INT;",
                            "TYPE Mode : (idle); END_TYPE;"))
               .ok,
          "L1b1-1 TYPE cannot appear in statement body");
}

// L1b1-2: equality is nominally typed; CASE labels are same-enum and unique
// by value. Arithmetic, bitwise and ordering operators stay forbidden.
void enum_comparison_and_case()
{
    Rig legal;
    check(legal.build(unit(
              kDirection,
              "a : Direction := Direction#negative; b : Direction := "
              "Direction#negative; equal : BOOL; different : BOOL; hit : "
              "DINT;",
              "equal := a = b; different := a <> Direction#positive; "
              "CASE a OF Direction#current: hit := 10; "
              "Direction#positive: hit := 20; Direction#negative: hit := 30; "
              "ELSE hit := 40; END_CASE;")),
          "L1b1-2 same enum comparison and CASE build");
    check(legal.scan() == st::ScanError::ok,
          "L1b1-2 same enum comparison and CASE scan");
    check(legal.i64("equal") == 1 && legal.i64("different") == 1 &&
              legal.i64("hit") == 30,
          "L1b1-2 same enum comparison and CASE results");

    const char *two_enums =
        "TYPE First : (zero, one); END_TYPE "
        "TYPE Second : (zero, one); END_TYPE";
    const st::CompileResult cross = st::compile(unit(
        two_enums, "a : First; b : Second; q : BOOL;", "q := a = b;"));
    check(!cross.ok && has_code(cross, st::DiagCode::sema_type_mismatch),
          "L1b1-2 cross-enum equality rejected");

    const st::CompileResult integer = st::compile(unit(
        kDirection, "a : Direction; q : BOOL;", "q := a = 0;"));
    check(!integer.ok && has_code(integer, st::DiagCode::sema_type_mismatch),
          "L1b1-2 enum integer comparison rejected");
    check(!st::compile(unit(kDirection, "a : Direction;",
                            "a := Direction#positive + Direction#negative;"))
               .ok,
          "L1b1-2 enum arithmetic rejected");
    check(!st::compile(unit(kDirection, "a : Direction; q : BOOL;",
                            "q := a < Direction#positive;"))
               .ok,
          "L1b1-2 enum ordering rejected");
    check(!st::compile(unit(kDirection, "a : Direction;",
                            "a := a AND Direction#positive;"))
               .ok,
          "L1b1-2 enum bitwise operation rejected");

    const st::CompileResult wrong_case = st::compile(unit(
        two_enums, "a : First; hit : INT;",
        "CASE a OF Second#zero: hit := 1; END_CASE;"));
    check(!wrong_case.ok &&
              has_code(wrong_case, st::DiagCode::sema_type_mismatch),
          "L1b1-2 cross-enum CASE label rejected");

    const st::CompileResult duplicate_value = st::compile(unit(
        "TYPE Mode : (first := 1, alias := 1); END_TYPE",
        "m : Mode; hit : INT;",
        "CASE m OF Mode#first: hit := 1; Mode#alias: hit := 2; END_CASE;"));
    check(!duplicate_value.ok &&
              has_code(duplicate_value,
                       st::DiagCode::sema_case_label_duplicate),
          "L1b1-2 duplicate enum CASE value rejected");
}

// L1b1-3: generated nominal conversions preserve DINT values; bad constants
// are compile diagnostics and bad dynamic values latch range_violation.
void enum_conversions_and_fault()
{
    Rig legal;
    check(legal.build(unit(
              kDirection,
              "d : Direction; raw : DINT; roundtrip : DINT;",
              "d := DINT_TO_DIRECTION(-1); raw := DIRECTION_TO_DINT(d); "
              "roundtrip := DIRECTION_TO_DINT(DINT_TO_DIRECTION(raw));")),
          "L1b1-3 typed enum conversions build");
    check(legal.scan() == st::ScanError::ok,
          "L1b1-3 typed enum conversions scan");
    check(legal.i64("raw") == -1 && legal.i64("roundtrip") == -1,
          "L1b1-3 enum member bits round trip");

    const st::CompileResult constant_bad = st::compile(unit(
        kDirection, "d : Direction;", "d := DINT_TO_DIRECTION(99);"));
    check(!constant_bad.ok &&
              has_code(constant_bad, st::DiagCode::sema_range_violation),
          "L1b1-3 invalid constant enum conversion diagnosed");

    Rig dynamic_bad;
    check(dynamic_bad.build(unit(
              kDirection,
              "raw : DINT; d : Direction := Direction#positive; marker : "
              "DINT;",
              "raw := 99; marker := 7; d := DINT_TO_DIRECTION(raw); "
              "marker := 9;")),
          "L1b1-3 dynamic invalid enum conversion builds");
    check(dynamic_bad.scan() == st::ScanError::range_violation,
          "L1b1-3 dynamic invalid enum conversion faults");
    check(dynamic_bad.i64("d") == 1,
          "L1b1-3 faulting enum assignment preserves target");
    check(dynamic_bad.i64("marker") == 7,
          "L1b1-3 assignments before fault are preserved");
    check(dynamic_bad.scan() == st::ScanError::range_violation &&
              dynamic_bad.instance.fault() == st::ScanError::range_violation,
          "L1b1-3 enum range fault latches");
    dynamic_bad.instance.reset();
    check(dynamic_bad.instance.fault() == st::ScanError::ok,
          "L1b1-3 reset clears enum range fault");
}

struct SubrangeCase
{
    const char *name;
    const char *base;
    const char *lower;
    const char *upper;
    long long expected_lower;
    long long expected_upper;
};

const SubrangeCase kSubranges[] = {
    {"SINT", "SINT", "-128", "127", -128, 127},
    {"INT", "INT", "-32768", "32767", -32768, 32767},
    {"DINT", "DINT", "-2147483648", "2147483647", -2147483648LL,
     2147483647LL},
    {"LINT", "LINT", "-9223372036854775808", "9223372036854775807",
     -9223372036854775807LL - 1, 9223372036854775807LL},
    {"USINT", "USINT", "0", "255", 0, 255},
    {"UINT", "UINT", "0", "65535", 0, 65535},
    {"UDINT", "UDINT", "0", "4294967295", 0, 4294967295LL},
    {"ULINT", "ULINT", "0", "18446744073709551615", 0, -1},
};

// L1b1-4: all eight integer bases accept their exact domain endpoints. A
// narrower subrange rejects both constant sides and faults on dynamic input.
void subrange_boundaries()
{
    for(const SubrangeCase &test : kSubranges) {
        std::string type = "TYPE R_";
        type += test.name;
        type += " : ";
        type += test.base;
        type += " (";
        type += test.lower;
        type += "..";
        type += test.upper;
        type += "); END_TYPE";
        std::string vars = "lo : R_";
        vars += test.name;
        vars += " := ";
        vars += test.lower;
        vars += "; hi : R_";
        vars += test.name;
        vars += " := ";
        vars += test.upper;
        vars += ";";

        Rig rig;
        if(!rig.build(unit(type.c_str(), vars.c_str(), ";"))) {
            fail(test.name);
            continue;
        }
        check(rig.scan() == st::ScanError::ok, test.name);
        check(rig.i64("lo") == test.expected_lower, test.name);
        check(rig.i64("hi") == test.expected_upper, test.name);
    }

    const char *percent = "TYPE Percent : INT (0..100); END_TYPE";
    for(const char *value : {"-1", "101"}) {
        std::string vars = "p : Percent := ";
        vars += value;
        vars += ";";
        const st::CompileResult result =
            st::compile(unit(percent, vars.c_str(), ";"));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_range_violation),
              "L1b1-4 constant subrange overflow diagnosed");
    }
    check(!st::compile(unit("TYPE Bad : INT (10..0); END_TYPE", "x : INT;",
                            ";"))
               .ok,
          "L1b1-4 reversed subrange bounds rejected");

    Rig dynamic_bad;
    check(dynamic_bad.build(unit(
              percent, "raw : DINT; p : Percent := 50; marker : DINT;",
              "raw := 101; marker := 3; p := DINT_TO_PERCENT(raw); marker := "
              "4;")),
          "L1b1-4 dynamic subrange overflow builds");
    check(dynamic_bad.scan() == st::ScanError::range_violation,
          "L1b1-4 dynamic subrange overflow faults");
    check(dynamic_bad.i64("p") == 50 && dynamic_bad.i64("marker") == 3,
          "L1b1-4 subrange fault preserves target and prior writes");
    check(dynamic_bad.scan() == st::ScanError::range_violation,
          "L1b1-4 subrange fault latches");
    dynamic_bad.instance.reset();
    check(dynamic_bad.instance.fault() == st::ScanError::ok,
          "L1b1-4 reset clears subrange fault");
}

// L1b1-5: a subrange remains nominal on assignment. It may flow losslessly
// to its base/wider types, while arithmetic produces the base type and needs
// an explicit checked conversion to flow back into the subrange.
void subrange_conversion_rules()
{
    const char *types = "TYPE Percent : INT (0..100); END_TYPE";
    Rig legal;
    check(legal.build(unit(
              types,
              "a : Percent := 40; b : Percent := 2; base : INT; wide : DINT; "
              "sum : INT;",
              "base := a; wide := a; sum := a + b;")),
          "L1b1-5 lossless subrange flows build");
    check(legal.scan() == st::ScanError::ok,
          "L1b1-5 lossless subrange flows scan");
    check(legal.i64("base") == 40 && legal.i64("wide") == 40 &&
              legal.i64("sum") == 42,
          "L1b1-5 subrange arithmetic returns base type");

    check(!st::compile(unit(types, "raw : DINT; p : Percent;", "p := raw;"))
               .ok,
          "L1b1-5 base-to-subrange implicit assignment rejected");
    check(!st::compile(unit(types, "a : Percent; b : Percent;",
                            "a := a + b;"))
               .ok,
          "L1b1-5 arithmetic result does not silently clamp into subrange");

    Rig explicit_wrap_then_check;
    check(explicit_wrap_then_check.build(unit(
              "TYPE Tiny : SINT (-10..10); END_TYPE",
              "raw : INT; value : Tiny;",
              "raw := 257; value := SINT_TO_TINY(INT_TO_SINT(raw));")),
          "L1b1-5 explicit base wrap then subrange check builds");
    check(explicit_wrap_then_check.scan() == st::ScanError::ok &&
              explicit_wrap_then_check.i64("value") == 1,
          "L1b1-5 wrap occurs only in explicit base conversion");
}

struct Golden
{
    const char *name;
    const char *types;
    const char *vars;
    const char *body;
    const char *result;
    long long expected;
};

// L1b1-6: self-authored, hand-derived enum/subrange programs. These cases
// intentionally mix initialization, CASE, loops, conversions and comparisons
// so bytecode anchors can later hash the same corpus cross-platform.
const Golden kGolden[] = {
    {"enum-default", "TYPE E : (a, b); END_TYPE", "e : E; out : DINT;",
     "out := E_TO_DINT(e);", "out", 0},
    {"enum-explicit", "TYPE E : (a := -4, b := 9); END_TYPE",
     "e : E := E#b; out : DINT;", "out := E_TO_DINT(e);", "out", 9},
    {"enum-auto-after-explicit", "TYPE E : (a := 7, b); END_TYPE",
     "e : E := E#b; out : DINT;", "out := E_TO_DINT(e);", "out", 8},
    {"enum-alias-equal", "TYPE E : (a := 3, b := 3); END_TYPE",
     "q : BOOL;", "q := E#a = E#b;", "q", 1},
    {"enum-not-equal", "TYPE E : (a, b); END_TYPE", "q : BOOL;",
     "q := E#a <> E#b;", "q", 1},
    {"enum-case-first", "TYPE E : (a, b); END_TYPE", "e : E; out : INT;",
     "CASE e OF E#a: out := 4; E#b: out := 5; END_CASE;", "out", 4},
    {"enum-case-else", "TYPE E : (a, b, c); END_TYPE",
     "e : E := E#c; out : INT;",
     "CASE e OF E#a: out := 4; ELSE out := 6; END_CASE;", "out", 6},
    {"enum-roundtrip", "TYPE E : (a := -1, b := 2); END_TYPE",
     "e : E; out : DINT;", "e := DINT_TO_E(2); out := E_TO_DINT(e);", "out",
     2},
    {"range-low", "TYPE R : INT (-2..2); END_TYPE", "r : R := -2; out : INT;",
     "out := r;", "out", -2},
    {"range-high", "TYPE R : INT (-2..2); END_TYPE", "r : R := 2; out : INT;",
     "out := r;", "out", 2},
    {"range-default", "TYPE R : UINT (4..9); END_TYPE", "r : R; out : UINT;",
     "out := r;", "out", 4},
    {"range-equality", "TYPE R : INT (0..9); END_TYPE",
     "a : R := 4; b : R := 4; q : BOOL;", "q := a = b;", "q", 1},
    {"range-order", "TYPE R : INT (0..9); END_TYPE",
     "a : R := 4; b : R := 5; q : BOOL;", "q := a < b;", "q", 1},
    {"range-add-base", "TYPE R : INT (0..9); END_TYPE",
     "a : R := 4; b : R := 5; out : INT;", "out := a + b;", "out", 9},
    {"range-sub-base", "TYPE R : DINT (-9..9); END_TYPE",
     "a : R := -4; b : R := 5; out : DINT;", "out := a - b;", "out", -9},
    {"range-case-low", "TYPE R : INT (3..8); END_TYPE",
     "r : R := 3; out : INT;",
     "CASE r OF 3: out := 30; 4..8: out := 40; END_CASE;", "out", 30},
    {"range-case-band", "TYPE R : INT (3..8); END_TYPE",
     "r : R := 7; out : INT;",
     "CASE r OF 3: out := 30; 4..8: out := 40; END_CASE;", "out", 40},
    {"range-explicit", "TYPE R : INT (0..100); END_TYPE",
     "raw : DINT := 77; r : R; out : INT;",
     "r := DINT_TO_R(raw); out := r;", "out", 77},
    {"range-loop-read", "TYPE R : INT (1..3); END_TYPE",
     "limit : R := 3; i : INT; sum : INT;",
     "FOR i := 1 TO limit DO sum := sum + i; END_FOR;", "sum", 6},
    {"enum-loop-case", "TYPE E : (off, on); END_TYPE",
     "e : E := E#on; i : INT; hits : INT;",
     "FOR i := 1 TO 3 DO CASE e OF E#on: hits := hits + 1; ELSE ; "
     "END_CASE; END_FOR;",
     "hits", 3},
    {"range-multi-scan-safe", "TYPE R : DINT (0..100); END_TYPE",
     "r : R := 10; out : DINT;", "out := r + 5;", "out", 15},
    {"enum-negative-case", "TYPE E : (neg := -1, zero := 0); END_TYPE",
     "e : E := E#neg; out : INT;",
     "CASE e OF E#neg: out := -7; E#zero: out := 0; END_CASE;", "out", -7},
};

void golden_programs()
{
    static Rig rig;
    int programs = 0;
    for(const Golden &test : kGolden) {
        ++programs;
        if(!rig.build(unit(test.types, test.vars, test.body))) {
            fail(test.name);
            continue;
        }
        if(rig.scan() != st::ScanError::ok ||
           rig.i64(test.result) != test.expected) {
            std::printf("  %s got %lld want %lld\n", test.name,
                        static_cast<long long>(rig.i64(test.result)),
                        test.expected);
            fail(test.name);
        }
    }
    check(programs >= 20, "L1b1-6 golden program floor");
}

// L1b1-7: each loading-domain resource is independently bounded. All three
// limits share one stable capacity_types diagnostic because callers need a
// bounded failure class, not knowledge of compiler storage layout.
void type_capacity()
{
    st::CompileOptions options;
    options.max_user_types = 1;
    const st::CompileResult types = st::compile(
        unit("TYPE A : (x); END_TYPE TYPE B : (x); END_TYPE", "v : A;", ";"),
        options);
    check(!types.ok && has_code(types, st::DiagCode::capacity_types),
          "L1b1-7 user type count bounded");

    options = st::CompileOptions{};
    options.max_enum_members = 2;
    const st::CompileResult members = st::compile(
        unit("TYPE A : (x, y, z); END_TYPE", "v : A;", ";"), options);
    check(!members.ok && has_code(members, st::DiagCode::capacity_types),
          "L1b1-7 enum member count bounded");

    options = st::CompileOptions{};
    options.max_type_name_bytes = 4;
    const st::CompileResult name = st::compile(
        unit("TYPE LongName : (x); END_TYPE", "v : INT;", ";"), options);
    check(!name.ok && has_code(name, st::DiagCode::capacity_types),
          "L1b1-7 type name length bounded");
}

// L1b1-8: the current bytecode format is allowed to change, but recompiling
// representative L0/L1a source must preserve source-level behavior.
void current_version_regression()
{
    struct ExistingCase
    {
        const char *name;
        const char *vars;
        const char *body;
        const char *result;
        long long expected;
    };
    const ExistingCase cases[] = {
        {"L0 arithmetic", "x : DINT;", "x := 2 + 3 * 4;", "x", 14},
        {"L0 CASE", "x : INT; out : INT;",
         "x := 2; CASE x OF 1: out := 10; 2: out := 20; END_CASE;", "out",
         20},
        {"L1a wrap", "x : SINT;", "x := 127; x := x + 1;", "x", -128},
        {"L1a explicit conversion", "x : DINT;",
         "x := LINT_TO_DINT(4294967297);", "x", 1},
    };
    for(const ExistingCase &test : cases) {
        Rig rig;
        if(!rig.build(unit("", test.vars, test.body))) {
            fail(test.name);
            continue;
        }
        check(rig.scan() == st::ScanError::ok, test.name);
        check(rig.i64(test.result) == test.expected, test.name);
    }
}

} // namespace

int main()
{
    enum_declarations();
    enum_comparison_and_case();
    enum_conversions_and_fault();
    subrange_boundaries();
    subrange_conversion_rules();
    golden_programs();
    type_capacity();
    current_version_regression();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l1b1 tests passed (%zu golden programs)\n",
                sizeof(kGolden) / sizeof(kGolden[0]));
    return 0;
}
