// L0 compiler acceptance (approved st-l0-semantics 1.x/2.x, metrics
// 5.6/5.8 front-end side): lexical forms, fault-tolerant recovery with
// accurate positions, strict same-type rules, dedicated unsupported codes,
// capacity diagnostics. Plain-main + fail() per house style.

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

st::CompileResult compile(const char *source)
{
    return st::compile(source);
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &d : result.diagnostics) {
        if(d.code == code) {
            return true;
        }
    }
    return false;
}

const st::Diagnostic *first_of(const st::CompileResult &result,
                               st::DiagCode code)
{
    for(const st::Diagnostic &d : result.diagnostics) {
        if(d.code == code) {
            return &d;
        }
    }
    return nullptr;
}

// Wraps a body (and optional VAR block) into a program.
std::string wrap(const char *vars, const char *body)
{
    std::string source = "PROGRAM p\nVAR\n";
    source += vars;
    source += "\nEND_VAR\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

// --- lexical forms ---------------------------------------------------------

void lexical_forms()
{
    // TIME literal composition and units (matrix 1.5, anchor L0-1.5-time).
    struct TimeCase
    {
        const char *literal;
        long long ns;
    };
    const TimeCase cases[] = {
        {"T#0s", 0LL},
        {"T#1s", 1000000000LL},
        {"T#1s500ms", 1500000000LL},
        {"T#1.5s", 1500000000LL},
        {"T#2h30m", 9000000000000LL},
        {"T#1d2h", 93600000000000LL},
        {"TIME#250us", 250000LL},
        {"T#42ns", 42LL},
        {"T#-10ms", -10000000LL},
        {"T#1m30s", 90000000000LL},
    };
    for(const TimeCase &c : cases) {
        std::string body = "t := ";
        body += c.literal;
        body += ";";
        const st::CompileResult r =
            compile(wrap("t : TIME;", body.c_str()).c_str());
        if(!r.ok) {
            fail(c.literal);
            continue;
        }
        // Fold lands in the constant pool; verify through execution below in
        // runtime tests; here the compile must succeed.
    }

    // Malformed TIME literals (anchor L0-1.5-time-bad).
    const char *bad[] = {"T#", "T#5x", "T#1s2h", "T#1.5s500ms", "T#1msx"};
    for(const char *literal : bad) {
        std::string body = "t := ";
        body += literal;
        body += ";";
        const st::CompileResult r =
            compile(wrap("t : TIME;", body.c_str()).c_str());
        check(!r.ok, "bad time literal must fail");
        check(has_code(r, st::DiagCode::lex_bad_time_literal),
              "bad time literal code");
    }

    // Based and separated integers (anchor L0-1.5-based).
    check(compile(wrap("x : DINT;", "x := 16#FF;").c_str()).ok, "16#FF");
    check(compile(wrap("x : DINT;", "x := 2#1010_1010;").c_str()).ok,
          "2#1010_1010");
    check(compile(wrap("x : DINT;", "x := 1_000_000;").c_str()).ok,
          "1_000_000");
    check(!compile(wrap("x : DINT;", "x := 8#9;").c_str()).ok, "8#9 invalid");
    check(!compile(wrap("x : DINT;", "x := 3#12;").c_str()).ok,
          "base 3 invalid");

    // Comments including nesting (matrix 1.13, anchor L0-1.13-comments).
    check(compile(wrap("x : INT;",
                       "(* outer (* inner *) still comment *) x := 1; // t\n")
                      .c_str())
              .ok,
          "nested comments");
    {
        const st::CompileResult r =
            compile(wrap("x : INT;", "(* never closed").c_str());
        check(!r.ok, "unterminated comment fails");
        check(has_code(r, st::DiagCode::lex_unterminated_comment),
              "unterminated comment code");
    }

    // Case-insensitive keywords and identifiers (anchor L0-1.13-case).
    check(compile("pRoGrAm P vAr X : iNt; eNd_VaR X := 1;UnTiL2 := 0;"
                  "eNd_PrOgRaM")
                  .ok == false,
          "case-insensitive keywords still type-check bodies");
    check(compile("pRoGrAm P vAr X : iNt; eNd_VaR X := 1; eNd_PrOgRaM").ok,
          "case-insensitive program compiles");

    // Identifier underscore rules (matrix 1.13, anchor L0-1.13-ident).
    check(!compile(wrap("a__b : INT;", "a__b := 1;").c_str()).ok,
          "consecutive underscores rejected");
    check(!compile(wrap("trail_ : INT;", "trail_ := 1;").c_str()).ok,
          "trailing underscore rejected");
}

// --- recovery and diagnostics ----------------------------------------------

void recovery_diagnostics()
{
    // Metric 5.8: three independent syntax errors -> >=3 diagnostics with
    // accurate positions (anchor L0-5.8-recovery).
    const char *source =
        "PROGRAM p\n"          // 1
        "VAR\n"                // 2
        "  x : INT;\n"         // 3
        "END_VAR\n"            // 4
        "x := ;\n"             // 5: missing expression
        "x := 1;\n"            // 6: valid
        "IF THEN END_IF;\n"    // 7: missing condition
        "x := 2;\n"            // 8: valid
        "x + 1;\n"             // 9: not a statement (missing ':=')
        "x := 3;\n"            // 10: valid
        "END_PROGRAM\n";
    const st::CompileResult r = compile(source);
    check(!r.ok, "errors must fail compile");
    check(r.diagnostics.size() >= 3, "at least three diagnostics");
    bool line5 = false;
    bool line7 = false;
    bool line9 = false;
    for(const st::Diagnostic &d : r.diagnostics) {
        line5 = line5 || d.line == 5;
        line7 = line7 || d.line == 7;
        line9 = line9 || d.line == 9;
    }
    check(line5, "diagnostic at line 5");
    check(line7, "diagnostic at line 7");
    check(line9, "diagnostic at line 9");

    // Nesting bound (matrix 2.7, anchor L0-2.7-nesting).
    std::string deep = "x := ";
    for(int i = 0; i < 200; ++i) {
        deep += "(";
    }
    deep += "1";
    for(int i = 0; i < 200; ++i) {
        deep += ")";
    }
    deep += ";";
    const st::CompileResult nested =
        compile(wrap("x : INT;", deep.c_str()).c_str());
    check(!nested.ok, "deep nesting fails");
    check(has_code(nested, st::DiagCode::parse_nesting_too_deep),
          "deep nesting code");
}

// --- unsupported constructs (matrix 6) ---------------------------------------

void unsupported_constructs()
{
    // anchor L0-6-l1: the former L1 rejection representatives graduated in
    // L1b and must no longer be routed through the L0 unsupported diagnostic.
    check(compile("PROGRAM p VAR s : STRING; END_VAR END_PROGRAM").ok,
          "L1b STRING graduated from unsupported boundary");
    check(compile("TYPE A : ARRAY [1..3] OF INT; END_TYPE "
                  "PROGRAM p VAR a : A; END_VAR END_PROGRAM")
              .ok,
          "L1b ARRAY graduated from unsupported boundary");

    struct UnsupportedCase
    {
        const char *source;
        st::DiagCode code;
    };
    const UnsupportedCase cases[] = {
        // anchor L0-6-l2
        {"FUNCTION f : INT END_FUNCTION", st::DiagCode::unsupported_l2},
        {"PROGRAM p VAR_INPUT x : INT; END_VAR END_PROGRAM",
         st::DiagCode::unsupported_l2},
        // anchor L0-6-l3: L3 constructs are accepted/rejected by their
        // current semantics and covered by core/test/st_l3_tests.cpp.
        // anchor L0-6-l5
        {"CONFIGURATION c END_CONFIGURATION", st::DiagCode::unsupported_l5},
        // anchor L0-6-l6: textual SFC graduated; syntax and semantics are covered by
        // core/test/st_l6_tests.cpp rather than the legacy unsupported gate.
        // anchor L0-6-non-goal
        {"PROGRAM p VAR r : RTC; END_VAR END_PROGRAM",
         st::DiagCode::unsupported_non_goal},
    };
    for(const UnsupportedCase &c : cases) {
        const st::CompileResult r = compile(c.source);
        if(r.ok || !has_code(r, c.code)) {
            fail(c.source);
        }
    }
}

void binding_type_prefix()
{
    const st::CompileResult plain = compile(
        "TYPE UserValue : (zero); END_TYPE "
        "PROGRAM p VAR value : UserValue; END_VAR END_PROGRAM");
    check(plain.ok, "plain source installs binding type prefix");
    if(plain.ok) {
        st::TypeId user = st::invalid_type_id;
        check(plain.program.types.find("MC_BUFFER_MODE", user) ==
                      st::TypeError::ok &&
                  user == st::binding_type::mc_buffer_mode,
              "binding enum TypeId is stable without binding syntax");
        check(plain.program.types.find("UserValue", user) ==
                      st::TypeError::ok &&
                  user == st::first_load_type_id + st::binding_type::count,
              "user TypeId follows the fixed binding prefix");
    }

    const st::CompileResult collision = compile(
        "TYPE MC_BUFFER_MODE : (zero); END_TYPE "
        "PROGRAM p VAR value : INT; END_VAR END_PROGRAM");
    check(!collision.ok,
          "user type cannot shadow canonical binding type");
    check(has_code(collision, st::DiagCode::sema_duplicate_identifier),
          "binding type collision has stable duplicate diagnostic");
}

// --- strict typing (matrix 1.6-1.11) -----------------------------------------

void strict_typing()
{
    // Lossy directions stay strict (anchor L0-1.7-strict); the lossless
    // widening whitelist is L1a 3.1 (tested in the L1a suite).
    {
        const st::CompileResult r = compile(
            wrap("a : INT; b : DINT;", "a := b;").c_str());
        check(!r.ok, "DINT to INT assignment rejected");
        check(has_code(r, st::DiagCode::sema_type_mismatch),
              "DINT/INT mismatch code");
    }
    check(!compile(wrap("a : REAL; b : LREAL;", "a := b + 1.0;").c_str()).ok,
          "LREAL to REAL mix rejected");
    check(!compile(wrap("a : INT; b : INT;", "a := b + T#1s;").c_str()).ok,
          "INT/TIME mix rejected");

    // Literal context typing with range checks (anchor L0-1.6-literal).
    check(compile(wrap("a : INT;", "a := 32767;").c_str()).ok, "INT max");
    check(compile(wrap("a : INT;", "a := -32768;").c_str()).ok, "INT min");
    {
        const st::CompileResult r =
            compile(wrap("a : INT;", "a := 32768;").c_str());
        check(!r.ok, "INT overflow literal rejected");
        check(has_code(r, st::DiagCode::sema_literal_out_of_range),
              "INT literal range code");
    }
    check(!compile(wrap("a : INT;", "a := -32769;").c_str()).ok,
          "INT min-1 rejected");
    check(compile(wrap("a : DINT;", "a := 2147483647;").c_str()).ok,
          "DINT max");
    check(compile(wrap("a : DINT;", "a := -2147483648;").c_str()).ok,
          "DINT min");
    check(!compile(wrap("a : DINT;", "a := 2147483648;").c_str()).ok,
          "DINT overflow rejected");
    check(compile(wrap("a : INT;", "a := 16#FFFF;").c_str()).ok,
          "based bit pattern fits INT");
    check(!compile(wrap("a : INT;", "a := 16#10000;").c_str()).ok,
          "based pattern too wide for INT");
    check(compile(wrap("a : LREAL;", "a := 5;").c_str()).ok,
          "int literal as LREAL");
    check(!compile(wrap("a : TIME;", "a := 5;").c_str()).ok,
          "int literal not TIME");
    check(!compile(wrap("a : BOOL;", "a := 1;").c_str()).ok,
          "int literal not BOOL");

    // Constant division by zero (matrix 1.9, anchor L0-1.9-const-div).
    {
        const st::CompileResult r =
            compile(wrap("a : INT;", "a := 1 / 0;").c_str());
        check(!r.ok, "const int div by zero rejected");
        check(has_code(r, st::DiagCode::sema_division_by_zero_const),
              "const div code");
    }
    check(!compile(wrap("a : INT; b : INT;", "a := b MOD 0;").c_str()).ok,
          "const mod zero rejected");
    check(compile(wrap("a : LREAL;", "a := 1.0 / 0.0;").c_str()).ok,
          "float div by zero is IEEE, compiles");

    // TIME operator surface (matrix 1.11, anchor L0-1.11-time-ops).
    check(compile(wrap("t : TIME;", "t := T#1s + T#500ms;").c_str()).ok,
          "TIME add");
    check(compile(wrap("t : TIME;", "t := T#1s * 2;").c_str()).ok,
          "TIME multiply by scale accepted (L1a 5.1)");
    check(!compile(wrap("t : TIME;", "t := T#1s * T#2s;").c_str()).ok,
          "TIME multiply by TIME rejected");
    check(!compile(wrap("t : TIME;", "t := -T#1s;").c_str()).ok,
          "unary minus on TIME rejected");

    // Ordering comparison on BOOL rejected (anchor L0-1.4-bool-order).
    check(!compile(
              wrap("a : BOOL; b : BOOL; c : BOOL;", "c := a < b;").c_str())
               .ok,
          "BOOL ordering rejected");
    check(compile(
              wrap("a : BOOL; b : BOOL; c : BOOL;", "c := a = b;").c_str())
              .ok,
          "BOOL equality allowed");

    // VAR initializer must be constant (anchor L0-1.14-init).
    check(compile(wrap("a : INT := 3; b : INT := -2;", "a := b;").c_str()).ok,
          "const initializers");
    {
        const st::CompileResult r =
            compile(wrap("a : INT := 1; b : INT := a;", ";").c_str());
        check(!r.ok, "non-const initializer rejected");
        check(has_code(r, st::DiagCode::sema_not_const_expr),
              "non-const init code");
    }
}

// --- statement-level semantics ------------------------------------------------

void statement_semantics()
{
    // FOR control protection (matrix 1.12, anchor L0-1.12-control).
    {
        const st::CompileResult r = compile(
            wrap("i : INT; s : INT;",
                 "FOR i := 1 TO 3 DO i := i + 1; END_FOR;")
                .c_str());
        check(!r.ok, "control assignment rejected");
        check(has_code(r, st::DiagCode::sema_for_control_assigned),
              "control assignment code");
    }
    check(!compile(wrap("i : INT;",
                        "FOR i := 1 TO 3 DO FOR i := 1 TO 2 DO ; END_FOR; "
                        "END_FOR;")
                        .c_str())
               .ok,
          "nested same-control rejected");
    {
        const st::CompileResult r = compile(
            wrap("i : INT;", "FOR i := 1 TO 3 BY 0 DO ; END_FOR;").c_str());
        check(!r.ok, "const BY 0 rejected");
        check(has_code(r, st::DiagCode::sema_for_step_zero_const),
              "const BY 0 code");
    }
    check(!compile(wrap("r : REAL;",
                        "FOR r := 1.0 TO 3.0 DO ; END_FOR;")
                        .c_str())
               .ok,
          "REAL control rejected");

    // EXIT outside a loop (anchor L0-1.3-exit).
    {
        const st::CompileResult r = compile(wrap("x : INT;", "EXIT;").c_str());
        check(!r.ok, "EXIT outside loop rejected");
        check(has_code(r, st::DiagCode::sema_exit_outside_loop),
              "EXIT outside loop code");
    }

    // CASE label rules (anchor L0-1.3-case-labels).
    check(!compile(wrap("x : INT;",
                        "CASE x OF 1: ; 1..3: ; END_CASE;")
                        .c_str())
               .ok,
          "overlapping labels rejected");
    {
        const st::CompileResult r = compile(
            wrap("x : INT;", "CASE x OF 5..2: ; END_CASE;").c_str());
        check(!r.ok, "inverted range rejected");
        check(has_code(r, st::DiagCode::sema_case_label_range_invalid),
              "inverted range code");
    }
    check(!compile(wrap("x : INT; y : INT;",
                        "CASE x OF y: ; END_CASE;")
                        .c_str())
               .ok,
          "non-const label rejected");
    check(!compile(wrap("x : REAL;",
                        "CASE x OF 1: ; END_CASE;")
                        .c_str())
               .ok,
          "REAL selector rejected");

    // Identifier rules (anchor L0-3.13-symbols).
    {
        const st::CompileResult r =
            compile(wrap("a : INT; A : DINT;", ";").c_str());
        check(!r.ok, "case-insensitive duplicate rejected");
        check(has_code(r, st::DiagCode::sema_duplicate_identifier),
              "duplicate code");
    }
    {
        const st::CompileResult r =
            compile(wrap("a : INT;", "b := 1;").c_str());
        check(!r.ok, "unknown identifier rejected");
        check(has_code(r, st::DiagCode::sema_unknown_identifier),
              "unknown identifier code");
    }
}

// --- FB surface --------------------------------------------------------------

void fb_surface()
{
    // Formal call + output read compile (matrix 3.9, anchor L0-3.9-call).
    check(compile(wrap("t : TON; q : BOOL;",
                       "t(IN := TRUE, PT := T#10ms); q := t.Q;")
                      .c_str())
              .ok,
          "TON call and output read");

    // Pin discipline (matrix 3.9/3.10, anchors L0-3.9-pins).
    {
        const st::CompileResult r = compile(
            wrap("t : TON;", "t(NOPE := TRUE);").c_str());
        check(!r.ok, "unknown pin rejected");
        check(has_code(r, st::DiagCode::sema_unknown_fb_pin),
              "unknown pin code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON;", "t(Q := TRUE);").c_str());
        check(!r.ok, "writing output pin rejected");
        check(has_code(r, st::DiagCode::sema_pin_not_input),
              "output pin write code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON; b : BOOL;", "b := t.IN;").c_str());
        check(!r.ok, "reading input pin rejected");
        check(has_code(r, st::DiagCode::sema_pin_not_output),
              "input pin read code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON;", "t.Q := TRUE;").c_str());
        check(!r.ok, "assigning pin rejected");
        check(has_code(r, st::DiagCode::sema_not_assignable),
              "pin assign code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON; x : INT;", "x := t;").c_str());
        check(!r.ok, "instance as value rejected");
        check(has_code(r, st::DiagCode::sema_operand_type_invalid),
              "instance as value code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON;", "t(IN := TRUE, IN := FALSE);").c_str());
        check(!r.ok, "duplicate parameter rejected");
        check(has_code(r, st::DiagCode::sema_duplicate_identifier),
              "duplicate parameter code");
    }
    {
        const st::CompileResult r = compile(
            wrap("t : TON;", "t(PT := 5);").c_str());
        check(!r.ok, "pin type mismatch rejected");
        check(has_code(r, st::DiagCode::sema_type_mismatch),
              "pin type mismatch code");
    }
    // Full-coverage positional calls graduated in L1a 5.4; mixing forms
    // stays rejected (anchor L0-3.9-formal-only).
    check(!compile(wrap("t : TON;", "t(IN := TRUE, T#1s);").c_str()).ok,
          "mixed call forms rejected");
    // FB instances take no initializer.
    check(!compile(wrap("t : TON := 5;", ";").c_str()).ok,
          "FB initializer rejected");
}

// --- capacities ----------------------------------------------------------------

void capacities()
{
    // Variable area cap (matrix 3.11, anchor L0-3.11-vars).
    {
        std::string vars;
        for(int i = 0; i < 3000; ++i) {
            vars += "v";
            vars += std::to_string(i);
            vars += " : INT;\n";
        }
        const st::CompileResult r = compile(wrap(vars.c_str(), ";").c_str());
        check(!r.ok, "variable capacity enforced");
        check(has_code(r, st::DiagCode::capacity_variables),
              "variable capacity code");
    }
    // FB instance cap (anchor L0-3.11-fb).
    {
        std::string vars;
        for(int i = 0; i < 1025; ++i) {
            vars += "t";
            vars += std::to_string(i);
            vars += " : R_TRIG;\n";
        }
        const st::CompileResult r = compile(wrap(vars.c_str(), ";").c_str());
        check(!r.ok, "FB capacity enforced");
        check(has_code(r, st::DiagCode::capacity_fb_instances),
              "FB capacity code");
    }
    // Stack depth cap (matrix 3.3, anchor L0-3.3-stack). Uses a variable so
    // constant folding cannot collapse the tree.
    {
        std::string body = "x := x";
        for(int i = 0; i < 70; ++i) {
            body += " + (x";
        }
        for(int i = 0; i < 70; ++i) {
            body += ")";
        }
        body += ";";
        st::CompileOptions options;
        options.max_nesting = 1000; // isolate the stack cap from nesting
        const st::CompileResult r =
            st::compile(wrap("x : INT;", body.c_str()), options);
        check(!r.ok, "stack capacity enforced");
        check(has_code(r, st::DiagCode::capacity_stack),
              "stack capacity code");
    }
}

} // namespace

int main()
{
    lexical_forms();
    recovery_diagnostics();
    unsupported_constructs();
    binding_type_prefix();
    strict_typing();
    statement_semantics();
    fb_surface();
    capacities();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l0 compiler tests passed\n");
    return 0;
}
