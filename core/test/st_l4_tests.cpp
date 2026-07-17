// L4 standard-function acceptance (approved st-l4-semantics sections 0-6).
//
// This is deliberately a RED contract.  It names the production manifest,
// diagnostics, host UTC boundary and cost report required to close L4; the
// test does not carry a shadow implementation of any standard function.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "st/st.h"

namespace
{

bool g_freeze_allocations = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_freeze_allocations) {
        ++g_frozen_allocations;
    }
    return std::malloc(size ? size : 1);
}

void *operator new[](std::size_t size)
{
    if(g_freeze_allocations) {
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

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 10000000;
constexpr std::int64_t kSecondNs = 1000000000LL;
constexpr std::int64_t kDayNs = 86400LL * kSecondNs;

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

std::string unit(const std::string &vars, const std::string &body)
{
    return "PROGRAM p\nVAR\n" + vars + "\nEND_VAR\n" + body +
           "\nEND_PROGRAM\n";
}

std::uint32_t read_u32(const unsigned char *bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[131072] = {};

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

    st::ScanError scan(std::int64_t budget = kBudget)
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
        return index < 0
                   ? std::numeric_limits<double>::quiet_NaN()
                   : instance.value_f64(static_cast<std::size_t>(index));
    }

    const st::VarInfo *var(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? nullptr : instance.variable(
                                       static_cast<std::size_t>(index));
    }

    std::vector<unsigned char> object_bytes(const char *name) const
    {
        const st::VarInfo *info = var(name);
        if(info == nullptr) {
            return {};
        }
        const st::TypeDesc *desc = compiled.program.types.get(info->type_id);
        if(desc == nullptr || info->offset > sizeof(buffer) ||
           desc->size > sizeof(buffer) - info->offset) {
            return {};
        }
        return std::vector<unsigned char>(buffer + info->offset,
                                          buffer + info->offset + desc->size);
    }

    bool set_i64(const char *name, std::int64_t value)
    {
        const st::VarInfo *info = var(name);
        if(info == nullptr) return false;
        const st::TypeDesc *desc = compiled.program.types.get(info->type_id);
        if(desc == nullptr || desc->size == 0U || desc->size > 8U ||
           info->offset > sizeof(buffer) ||
           desc->size > sizeof(buffer) - info->offset) return false;
        const std::uint64_t bits = static_cast<std::uint64_t>(value);
        for(unsigned byte = 0; byte < desc->size; ++byte) {
            buffer[info->offset + byte] =
                static_cast<unsigned char>(bits >> (byte * 8U));
        }
        return true;
    }

    std::string string_value(const char *name) const
    {
        const std::vector<unsigned char> bytes = object_bytes(name);
        if(bytes.size() < 4) {
            return {};
        }
        const std::uint32_t length = read_u32(bytes.data());
        if(length > bytes.size() - 4U) {
            return {};
        }
        return std::string(reinterpret_cast<const char *>(bytes.data() + 4),
                           length);
    }

    std::vector<std::uint32_t> wstring_value(const char *name) const
    {
        const std::vector<unsigned char> bytes = object_bytes(name);
        if(bytes.size() < 4) {
            return {};
        }
        const std::uint32_t length = read_u32(bytes.data());
        if(static_cast<std::uint64_t>(length) * 4U > bytes.size() - 4U) {
            return {};
        }
        std::vector<std::uint32_t> result;
        for(std::uint32_t index = 0; index < length; ++index) {
            result.push_back(read_u32(
                bytes.data() + 4U +
                static_cast<std::size_t>(index) * 4U));
        }
        return result;
    }
};

std::string cat()
{
    return "\xE7\x8C\xAB";
}

std::string grin()
{
    return "\xF0\x9F\x98\x80";
}

// L4-A01: the production manifest is the sole registry surface.  Its dump
// must be deterministic and equal to the approved 51-name feature set.
void exact_function_manifest()
{
    const char *expected_names[] = {
        "ABS",          "SQRT",       "LN",          "LOG",
        "EXP",          "SIN",        "COS",         "TAN",
        "ASIN",         "ACOS",       "ATAN",        "ADD",
        "SUB",          "MUL",        "DIV",         "MOD",
        "EXPT",         "MIN",        "MAX",         "LIMIT",
        "SEL",          "MUX",        "GT",          "GE",
        "EQ",           "LE",         "LT",          "NE",
        "SHL",          "SHR",        "ROL",         "ROR",
        "LEN",          "LEFT",       "RIGHT",       "MID",
        "CONCAT",       "INSERT",     "DELETE",      "REPLACE",
        "FIND",         "ADD_TIME",   "ADD_TOD_TIME", "ADD_DT_TIME",
        "SUB_TIME",     "SUB_DATE_DATE", "SUB_TOD_TIME", "SUB_DT_DT",
        "MULTIME",      "DIVTIME",    "CONCAT_DATE_TOD",
    };
    std::vector<std::string> expected;
    for(const char *name : expected_names) {
        expected.emplace_back(name);
    }
    std::sort(expected.begin(), expected.end());

    const auto &manifest = st::standard_function_manifest();
    std::vector<std::string> actual;
    for(const auto &entry : manifest) {
        check(entry.registered, "L4-A01 every declared function registered");
        actual.emplace_back(entry.name);
    }
    std::sort(actual.begin(), actual.end());
    check(actual.size() == 51, "L4-A01 manifest count is exactly 51");
    check(std::adjacent_find(actual.begin(), actual.end()) == actual.end(),
          "L4-A01 manifest has no duplicate names");
    check(actual == expected, "L4-A01 manifest equals approved function set");
    check(st::standard_function_manifest_dump() ==
              st::standard_function_manifest_dump(),
          "L4-A01 canonical manifest dump deterministic");
}

// L4-A02/D01-D08: ANY join, return types, variadic bounds, selection and
// comparison chains.  Each value is independently observable.
void any_resolution_arithmetic_selection_and_comparison()
{
    Rig rig;
    const std::string vars =
        "s : SINT := -128; i : INT := 2; d : DINT := 3; "
        "abs_min : SINT; narrow_abs : LINT; narrow_add : LINT; "
        "joined : DINT; sub : DINT; mul : DINT; "
        "div : DINT; modv : DINT; power : LREAL; mn : DINT; mx : DINT; "
        "limited : DINT; selected : DINT; muxed : DINT; "
        "gt : BOOL; ge : BOOL; eq : BOOL; le : BOOL; lt : BOOL; ne : BOOL;";
    const std::string body =
        "abs_min := ABS(s); narrow_abs := ABS(SINT#-128); "
        "narrow_add := ADD(SINT#127, SINT#1); "
        "joined := ADD(i, d, SINT#4); "
        "sub := SUB(20, 3); mul := MUL(2, 3, 4); div := DIV(20, 3); "
        "modv := MOD(20, 3); power := EXPT(2.0, 3.0); "
        "mn := MIN(8, 3, 5); mx := MAX(8, 3, 5); "
        "limited := LIMIT(0, 12, 10); selected := SEL(FALSE, 7, 9); "
        "muxed := MUX(2, 10, 20, 30); "
        "gt := GT(9, 8, 7); ge := GE(9, 9, 7); eq := EQ(4, 4, 4); "
        "le := LE(1, 1, 2); lt := LT(1, 2, 3); ne := NE(1, 2, 1);";
    check(rig.build(unit(vars, body)), "L4-A02 ANY fixture builds");
    check(rig.scan() == st::ScanError::ok, "L4-A02 ANY fixture scans");
    check(rig.i64("abs_min") == -128, "L4-A02 ABS signed min wraps");
    check(rig.i64("narrow_abs") == -128 && rig.i64("narrow_add") == -128,
          "L4-A02 assignment target does not change ANY join");
    check(rig.i64("joined") == 9, "L4-A02 lossless ANY join chosen");
    check(rig.i64("sub") == 17 && rig.i64("mul") == 24 &&
              rig.i64("div") == 6 && rig.i64("modv") == 2,
          "L4-A02 arithmetic function results");
    check(std::fabs(rig.f64("power") - 8.0) < 1e-12,
          "L4-A02 EXPT result");
    check(rig.i64("mn") == 3 && rig.i64("mx") == 8 &&
              rig.i64("limited") == 10 && rig.i64("selected") == 7 &&
              rig.i64("muxed") == 30,
          "L4-A02 selection function results");
    check(rig.i64("gt") == 1 && rig.i64("ge") == 1 &&
              rig.i64("eq") == 1 && rig.i64("le") == 1 &&
              rig.i64("lt") == 1 && rig.i64("ne") == 1,
          "L4-A02 comparison-chain results");

    std::string args = "1";
    for(int index = 1; index < 32; ++index) {
        args += ",1";
    }
    Rig max_arity;
    check(max_arity.build(unit("out : DINT;", "out := ADD(" + args + ");")) &&
              max_arity.scan() == st::ScanError::ok &&
              max_arity.i64("out") == 32,
          "L4-A02 variadic arity 32 accepted");

    const st::CompileResult too_few =
        st::compile(unit("x : DINT;", "x := ADD(1);"));
    check(!too_few.ok &&
              has_code(too_few, st::DiagCode::sema_no_matching_overload),
          "L4-A02 variadic arity one rejected");
    const st::CompileResult too_many = st::compile(
        unit("x : DINT;", "x := ADD(" + args + ",1);"));
    check(!too_many.ok &&
              has_code(too_many, st::DiagCode::sema_no_matching_overload),
          "L4-A02 variadic arity 33 rejected");
    const st::CompileResult wrong_family =
        st::compile(unit("x : DINT;", "x := ABS('x');"));
    check(!wrong_family.ok &&
              has_code(wrong_family, st::DiagCode::sema_no_matching_overload),
          "L4-A02 wrong ANY family rejected");
    const st::CompileResult ambiguous =
        st::compile(unit("x : DINT;", "x := ABS(16#1);"));
    check(!ambiguous.ok &&
              has_code(ambiguous, st::DiagCode::sema_ambiguous_overload),
          "L4-A02 tied overload is explicit ambiguity");
}

void integer_division_and_checked_date_time_boundaries()
{
    Rig integer;
    check(integer.build(unit(
              "u : ULINT := ULINT#18_446_744_073_709_551_615; "
              "uq : ULINT; ur : ULINT; lo : LINT := LINT#-9_223_372_036_854_775_808; "
              "sq : LINT; sr : LINT;",
              "uq := DIV(u, ULINT#2); ur := MOD(u, ULINT#2); "
              "sq := DIV(lo, LINT#-1); sr := MOD(lo, LINT#-1);")),
          "L4-B01 integer division boundary fixture builds");
    check(integer.scan() == st::ScanError::ok,
          "L4-B01 integer division boundary fixture scans");
    check(static_cast<std::uint64_t>(integer.i64("uq")) ==
              0x7FFFFFFFFFFFFFFFULL && integer.i64("ur") == 1,
          "L4-B01 unsigned DIV MOD preserve high bit");
    check(integer.i64("sq") == std::numeric_limits<std::int64_t>::min() &&
              integer.i64("sr") == 0,
          "L4-B01 LINT minimum DIV MOD minus one wrap without UB");

    Rig subtract_min;
    check(subtract_min.build(unit(
              "a : TIME; b : TIME; out : TIME := T#7ns;",
              "out := SUB_TIME(a, b);")) &&
              subtract_min.set_i64("a", std::numeric_limits<std::int64_t>::min()) &&
              subtract_min.set_i64("b", std::numeric_limits<std::int64_t>::min()),
          "L4-B02 checked subtraction fixture builds");
    check(subtract_min.scan() == st::ScanError::ok &&
              subtract_min.i64("out") == 0,
          "L4-B02 minimum minus minimum is representable");

    struct Boundary
    {
        const char *name;
        std::string vars;
        std::string body;
        const char *left;
        std::int64_t left_value;
        const char *right;
        std::int64_t right_value;
    };
    const Boundary boundaries[] = {
        {"SUB_DATE_DATE", "a : DATE; b : DATE; out : TIME := T#7ns; marker : DINT;",
         "marker := 1; out := SUB_DATE_DATE(a, b); marker := 2;",
         "a", std::numeric_limits<std::int32_t>::max(), "b",
         std::numeric_limits<std::int32_t>::min()},
        {"MULTIME", "a : TIME; b : DINT := 2; out : TIME := T#7ns; marker : DINT;",
         "marker := 1; out := MULTIME(a, b); marker := 2;",
         "a", std::numeric_limits<std::int64_t>::max(), "b", 2},
        {"DIVTIME", "a : TIME; b : DINT := -1; out : TIME := T#7ns; marker : DINT;",
         "marker := 1; out := DIVTIME(a, b); marker := 2;",
         "a", std::numeric_limits<std::int64_t>::min(), "b", -1},
        {"CONCAT_DATE_TOD", "a : DATE; b : TOD; out : DT := DT#1970-01-01-00:00:00.000000007; marker : DINT;",
         "marker := 1; out := CONCAT_DATE_TOD(a, b); marker := 2;",
         "a", 106752, "b", 0},
    };
    for(const Boundary &boundary : boundaries) {
        Rig rig;
        check(rig.build(unit(boundary.vars, boundary.body)) &&
                  rig.set_i64(boundary.left, boundary.left_value) &&
                  rig.set_i64(boundary.right, boundary.right_value),
              boundary.name);
        check(rig.scan() == st::ScanError::date_time_range_violation &&
                  rig.i64("out") == 7 && rig.i64("marker") == 1,
              boundary.name);
    }
}

// L4-A03/D05-D08: integer wrap, IEEE domains, invalid LIMIT/MUX and
// left-to-right eager argument evaluation.
void numeric_domains_faults_and_evaluation_order()
{
    Rig floating;
    check(floating.build(unit(
              "neg : LREAL := -1.0; zero : LREAL; root : LREAL; "
              "logv : LREAL; log10_v : LREAL; exp_v : LREAL; "
              "divv : LREAL; tan_v : LREAL; asin_v : LREAL; "
              "acos_v : LREAL; atan_v : LREAL;",
              "root := SQRT(neg); logv := LN(neg); divv := DIV(1.0, zero); "
              "log10_v := LOG(100.0); exp_v := EXP(1.0); tan_v := TAN(0.0); "
              "asin_v := ASIN(2.0); acos_v := ACOS(2.0); atan_v := ATAN(1.0);")),
          "L4-A03 floating-domain fixture builds");
    check(floating.scan() == st::ScanError::ok,
          "L4-A03 floating-domain fixture scans");
    check(std::isnan(floating.f64("root")) &&
              std::isnan(floating.f64("logv")) &&
              std::isinf(floating.f64("divv")) &&
              std::isnan(floating.f64("asin_v")) &&
              std::isnan(floating.f64("acos_v")),
          "L4-A03 IEEE NaN and Inf rules");
    check(std::fabs(floating.f64("log10_v") - 2.0) < 1e-12 &&
              std::fabs(floating.f64("exp_v") - std::exp(1.0)) < 1e-12 &&
              floating.f64("tan_v") == 0.0 &&
              std::fabs(floating.f64("atan_v") - std::atan(1.0)) < 1e-12,
          "L4-A03 remaining transcendental functions");

    const st::CompileResult bad_limit =
        st::compile(unit("x : DINT;", "x := LIMIT(10, 5, 0);"));
    check(!bad_limit.ok &&
              has_code(bad_limit, st::DiagCode::sema_invalid_argument),
          "L4-A03 constant reversed LIMIT diagnosed");

    Rig dynamic_limit;
    check(dynamic_limit.build(unit(
              "lo : DINT := 10; hi : DINT := 0; out : DINT := 7; marker : DINT;",
              "marker := 3; out := LIMIT(lo, 5, hi); marker := 4;")),
          "L4-A03 dynamic LIMIT fixture builds");
    check(dynamic_limit.scan() == st::ScanError::invalid_argument &&
              dynamic_limit.i64("out") == 7 &&
              dynamic_limit.i64("marker") == 3,
          "L4-A03 invalid LIMIT faults before target write");

    Rig mux;
    check(mux.build(unit(
              "index : DINT := 3; out : DINT := 7; marker : DINT;",
              "marker := 2; out := MUX(index, 10, 20); marker := 3;")),
          "L4-A03 MUX range fixture builds");
    check(mux.scan() == st::ScanError::range_violation &&
              mux.i64("out") == 7 && mux.i64("marker") == 2,
          "L4-A03 MUX out of range faults atomically");

    Rig eager;
    check(eager.build(unit(
              "zero : DINT; out : DINT := 9; marker : DINT;",
              "marker := 1; out := SEL(TRUE, 7, DIV(1, zero)); marker := 2;")),
          "L4-A03 eager evaluation fixture builds");
    check(eager.scan() == st::ScanError::division_by_zero &&
              eager.i64("out") == 9 && eager.i64("marker") == 1,
          "L4-A03 SEL evaluates all arguments left to right");
}

// L4-A04/D09-D11: all four bit-string widths and the 0, width-1, width,
// over-width and negative-count boundaries.
void shifts_and_rotates()
{
    struct WidthCase
    {
        const char *type;
        int width;
        const char *one;
    };
    const WidthCase cases[] = {{"BYTE", 8, "BYTE#1"},
                               {"WORD", 16, "WORD#1"},
                               {"DWORD", 32, "DWORD#1"},
                               {"LWORD", 64, "LWORD#1"}};
    for(const WidthCase &test : cases) {
        const std::string prefix = test.type;
        std::string vars = "x : ";
        vars += prefix;
        vars += " := ";
        vars += test.one;
        vars += "; a : ";
        vars += prefix;
        vars += "; b : ";
        vars += prefix;
        vars += "; c : ";
        vars += prefix;
        vars += "; d : ";
        vars += prefix;
        vars += "; e : ";
        vars += prefix;
        vars += "; f : ";
        vars += prefix;
        vars += ";";
        const std::string width = std::to_string(test.width);
        std::string body = "a := SHL(x, 0); b := SHL(x, ";
        body += std::to_string(test.width - 1);
        body += "); c := SHL(x, ";
        body += width;
        body += "); d := SHR(x, ";
        body += std::to_string(test.width + 1);
        body += "); e := ROL(x, ";
        body += width;
        body += "); f := ROR(x, ";
        body += std::to_string(test.width + 1);
        body += ");";
        Rig rig;
        if(!rig.build(unit(vars, body)) || rig.scan() != st::ScanError::ok) {
            fail(test.type);
            continue;
        }
        check(rig.i64("a") == 1, test.type);
        check(rig.i64("b") ==
                  static_cast<std::int64_t>(
                      std::uint64_t{1} << (test.width - 1)),
              test.type);
        check(rig.i64("c") == 0 && rig.i64("d") == 0, test.type);
        check(rig.i64("e") == 1, test.type);
        const std::uint64_t high = std::uint64_t{1} << (test.width - 1);
        check(static_cast<std::uint64_t>(rig.i64("f")) == high, test.type);
    }

    const st::CompileResult negative =
        st::compile(unit("x : WORD;", "x := SHL(WORD#1, -1);"));
    check(!negative.ok &&
              has_code(negative, st::DiagCode::sema_range_violation),
          "L4-A04 negative shift constant rejected");
    const st::CompileResult integer_input =
        st::compile(unit("x : DINT;", "x := SHL(1, 1);"));
    check(!integer_input.ok &&
              has_code(integer_input,
                       st::DiagCode::sema_no_matching_overload),
          "L4-A04 integer input requires explicit bit-string conversion");
}

// L4-A05/D12-D15: all nine string functions operate in Unicode-scalar
// positions, keep STRING/WSTRING separate and never truncate on capacity.
void string_functions_unicode_and_faults()
{
    const std::string text = "A" + cat() + grin();
    Rig strings;
    const std::string vars =
        "s : STRING[16] := '" + text +
        "'; len : DINT; left : STRING[16]; right : STRING[16]; "
        "mid : STRING[16]; joined : STRING[16]; inserted : STRING[16]; "
        "deleted : STRING[16]; replaced : STRING[16]; found : DINT; empty : DINT;";
    const std::string body =
        "len := LEN(s); left := LEFT(s, 2); right := RIGHT(s, 1); "
        "mid := MID(s, 1, 2); joined := CONCAT('A', 'B', 'C'); "
        "inserted := INSERT('AC', 'B', 2); deleted := DELETE('ABCD', 2, 2); "
        "replaced := REPLACE('ABCD', 'xy', 2, 2); found := FIND(s, '" +
        cat() + "'); empty := FIND(s, '');";
    check(strings.build(unit(vars, body)), "L4-A05 STRING fixture builds");
    check(strings.scan() == st::ScanError::ok,
          "L4-A05 STRING fixture scans");
    check(strings.i64("len") == 3, "L4-A05 LEN counts Unicode scalars");
    check(strings.string_value("left") == "A" + cat(),
          "L4-A05 LEFT uses scalar count");
    check(strings.string_value("right") == grin(),
          "L4-A05 RIGHT uses scalar count");
    check(strings.string_value("mid") == cat(),
          "L4-A05 MID uses one-based scalar position");
    check(strings.string_value("joined") == "ABC" &&
              strings.string_value("inserted") == "ABC" &&
              strings.string_value("deleted") == "AD" &&
              strings.string_value("replaced") == "AxyD",
          "L4-A05 modifying STRING functions");
    check(strings.i64("found") == 2 && strings.i64("empty") == 1,
          "L4-A05 FIND scalar position and empty needle");

    Rig wide;
    const std::string wide_vars =
        "s : WSTRING[8] := \"A" + cat() + grin() +
        "\"; len : DINT; left : WSTRING[8]; mid : WSTRING[8]; "
        "joined : WSTRING[8]; found : DINT;";
    const std::string wide_body =
        "len := LEN(s); left := LEFT(s, 2); mid := MID(s, 1, 3); "
        "joined := CONCAT(\"A\", \"" + cat() + "\"); found := FIND(s, \"" +
        grin() + "\");";
    check(wide.build(unit(wide_vars, wide_body)),
          "L4-A05 WSTRING fixture builds");
    check(wide.scan() == st::ScanError::ok,
          "L4-A05 WSTRING fixture scans");
    check(wide.i64("len") == 3 && wide.i64("found") == 3,
          "L4-A05 WSTRING scalar positions");
    check(wide.wstring_value("left") ==
              std::vector<std::uint32_t>({65, 0x732B}) &&
              wide.wstring_value("mid") ==
                  std::vector<std::uint32_t>({0x1F600}) &&
              wide.wstring_value("joined") ==
                  std::vector<std::uint32_t>({65, 0x732B}),
          "L4-A05 WSTRING results preserve scalar values");

    const st::CompileResult mixed = st::compile(unit(
        "x : STRING[8];", "x := CONCAT('A', \"B\");"));
    check(!mixed.ok &&
              has_code(mixed, st::DiagCode::sema_no_matching_overload),
          "L4-A05 STRING and WSTRING do not implicitly mix");

    Rig capacity;
    check(capacity.build(unit(
              "source : STRING[8] := 'abc'; out : STRING[3] := 'old'; marker : DINT;",
              "marker := 7; out := CONCAT(source, 'x'); marker := 9;")),
          "L4-A05 capacity fixture builds");
    check(capacity.scan() == st::ScanError::string_capacity_exceeded &&
              capacity.string_value("out") == "old" &&
              capacity.i64("marker") == 7,
          "L4-A05 capacity fault preserves target and prior writes");

    Rig position;
    check(position.build(unit(
              "p : DINT; source : STRING[8] := 'abc'; out : STRING[8] := 'old'; marker : DINT;",
              "marker := 4; out := INSERT(source, 'x', p); marker := 5;")),
          "L4-A05 range fixture builds");
    check(position.scan() == st::ScanError::range_violation &&
              position.string_value("out") == "old" &&
              position.i64("marker") == 4,
          "L4-A05 invalid position faults atomically");
}

void string_capacity_fault_classification()
{
    struct Case
    {
        const char *name;
        const char *expression;
    };
    const Case cases[] = {
        {"INSERT", "INSERT(source, 'xyz', 2)"},
        {"REPLACE", "REPLACE(source, 'xyz', 1, 2)"},
    };
    for(const Case &test : cases) {
        Rig rig;
        check(rig.build(unit(
                  "source : STRING[8] := 'abc'; out : STRING[3] := 'old'; "
                  "marker : DINT;",
                  std::string("marker := 1; out := ") + test.expression +
                      "; marker := 2;")),
              test.name);
        check(rig.scan() == st::ScanError::string_capacity_exceeded &&
                  rig.string_value("out") == "old" &&
                  rig.i64("marker") == 1,
              test.name);
    }
}

// L4-A06/D16-D18: pure integer calendar/nanosecond functions, leap-day and
// midnight boundaries, overflow atomicity, and explicit host UTC injection.
void date_time_functions_and_utc_boundary()
{
    Rig rig;
    check(rig.build(unit(
              "tadd : TIME; tsub : TIME; tod : TOD; dt : DT; "
              "date_delta : TIME; tod_sub : TOD; dt_delta : TIME; "
              "scaled : TIME; divided : TIME; combined : DT;",
              "tadd := ADD_TIME(T#2s, T#500ms); tsub := SUB_TIME(T#2s, T#500ms); "
              "tod := ADD_TOD_TIME(TOD#23:59:59.500000000, T#1s); "
              "dt := ADD_DT_TIME(DT#2000-02-28-23:59:59, T#1s); "
              "date_delta := SUB_DATE_DATE(D#2000-03-01, D#2000-02-28); "
              "tod_sub := SUB_TOD_TIME(TOD#00:00:00.500000000, T#1s); "
              "dt_delta := SUB_DT_DT(DT#1970-01-01-00:00:02, DT#1970-01-01-00:00:00); "
              "scaled := MULTIME(T#2s, -3); divided := DIVTIME(T#5s, 2); "
              "combined := CONCAT_DATE_TOD(D#1970-01-02, TOD#00:00:01);")),
          "L4-A06 date/time fixture builds");
    check(rig.scan() == st::ScanError::ok,
          "L4-A06 date/time fixture scans");
    check(rig.i64("tadd") == 2500000000LL &&
              rig.i64("tsub") == 1500000000LL,
          "L4-A06 TIME addition and subtraction");
    check(rig.i64("tod") == 500000000LL,
          "L4-A06 TOD addition wraps midnight");
    check(rig.i64("dt") == 951782400LL * kSecondNs,
          "L4-A06 DT addition crosses leap day");
    check(rig.i64("date_delta") == 2LL * kDayNs,
          "L4-A06 DATE subtraction observes leap day");
    check(rig.i64("tod_sub") == kDayNs - 500000000LL &&
              rig.i64("dt_delta") == 2LL * kSecondNs,
          "L4-A06 TOD subtraction wraps and DT subtraction returns TIME");
    check(rig.i64("scaled") == -6LL * kSecondNs &&
              rig.i64("divided") == 2500000000LL,
          "L4-A06 TIME multiply and divide");
    check(rig.i64("combined") == kDayNs + kSecondNs,
          "L4-A06 CONCAT_DATE_TOD builds UTC DT");

    Rig overflow;
    check(overflow.build(unit(
              "source : DT := DT#2262-04-11-23:47:16.854775807; "
              "out : DT := DT#1970-01-01-00:00:07; marker : DINT;",
              "marker := 7; out := ADD_DT_TIME(source, T#1ns); marker := 9;")),
          "L4-A06 overflow fixture builds");
    check(overflow.scan() == st::ScanError::date_time_range_violation &&
              overflow.i64("out") == 7LL * kSecondNs &&
              overflow.i64("marker") == 7,
          "L4-A06 date overflow faults before target write");

    Rig divide_zero;
    check(divide_zero.build(unit(
              "zero : DINT; out : TIME := T#7ns; marker : DINT;",
              "marker := 1; out := DIVTIME(T#1s, zero); marker := 2;")),
          "L4-A06 DIVTIME zero fixture builds");
    check(divide_zero.scan() == st::ScanError::division_by_zero &&
              divide_zero.i64("out") == 7 && divide_zero.i64("marker") == 1,
          "L4-A06 DIVTIME zero faults atomically");

    Rig utc;
    check(utc.build(unit("x : DINT;", "x := 1;")),
          "L4-D17 UTC injection fixture builds");
    check(utc.instance.inject_utc_dt(1710000000123456789LL) ==
              rt::ErrorCode::ok &&
              utc.instance.utc_dt_ns() == 1710000000123456789LL,
          "L4-D17 executor injects UTC without VM wall-clock read");
    check(utc.scan() == st::ScanError::ok,
          "L4-D17 injected UTC is stable across scan boundary");
}

// L4-A08 and cost contract: the compile artifact reports the bounded worst
// function cost; exact budget N succeeds, N-1 faults; warmed scans allocate
// nothing and repeated compiles/runs are deterministic.
void cost_budget_determinism_and_zero_allocation()
{
    const std::string source = unit(
        "x : LREAL := 0.5; y : LREAL; bits : LWORD; s : STRING[32] := 'abc'; "
        "out : STRING[32]; dt : DT; n : DINT;",
        "y := SIN(x) + COS(x) + SQRT(x); bits := ROL(LWORD#1, 17); "
        "out := CONCAT(LEFT(s, 2), RIGHT(s, 1)); "
        "dt := CONCAT_DATE_TOD(D#2000-01-01, TOD#12:00:00); n := LEN(out);");
    const st::CompileResult first = st::compile(source);
    const st::CompileResult second = st::compile(source);
    check(first.ok && second.ok,
          "L4-A08 deterministic/cost fixture compiles twice");
    check(first.ok && first.program.max_standard_function_cost > 0,
          "L4-A08 artifact reports maximum standard-function cost");
    check(first.ok && second.ok && first.program.code == second.program.code &&
              first.program.constants == second.program.constants &&
              first.program.max_standard_function_cost ==
                  second.program.max_standard_function_cost,
          "L4-A08 compile artifact deterministic");

    std::int64_t needed = -1;
    for(std::int64_t budget = 1; budget < 10000; ++budget) {
        Rig probe;
        if(!probe.build(source)) {
            fail("L4-A08 budget probe build");
            break;
        }
        if(probe.scan(budget) == st::ScanError::ok) {
            needed = budget;
            break;
        }
    }
    check(needed > 1, "L4-A08 bounded program has finite budget");
    if(needed > 1) {
        Rig exact;
        Rig under;
        check(exact.build(source) && under.build(source),
              "L4-A08 exact-budget rigs build");
        check(exact.scan(needed) == st::ScanError::ok,
              "L4-A08 exact budget N completes");
        check(under.scan(needed - 1) == st::ScanError::budget_exceeded,
              "L4-A08 budget N-1 faults");
    }

    Rig frozen;
    check(frozen.build(source), "L4-A08 allocation fixture builds");
    check(frozen.scan() == st::ScanError::ok,
          "L4-A08 allocation fixture warms up");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    for(int scan = 0; scan < 1000; ++scan) {
        if(frozen.scan() != st::ScanError::ok) {
            g_freeze_allocations = false;
            fail("L4-A08 frozen scan completes");
            return;
        }
    }
    g_freeze_allocations = false;
    check(g_frozen_allocations == 0,
          "L4-A08 standard functions allocate zero in scan");
}

void standard_cost_consistency()
{
    std::string scalar_args = "1";
    for(int argument = 1; argument < 32; ++argument) scalar_args += ",1";
    const std::string scalar_source = unit(
        "out : DINT;", "out := ADD(" + scalar_args + ");");
    Rig scalar_exact;
    Rig scalar_under;
    check(scalar_exact.build(scalar_source) && scalar_under.build(scalar_source),
          "L4-B03 scalar cost fixtures build");
    const std::int64_t scalar_budget = static_cast<std::int64_t>(
        scalar_exact.compiled.program.worst_case_instructions);
    check(scalar_exact.compiled.program.max_standard_function_cost == 32 &&
              scalar_budget > 32,
          "L4-B03 scalar argc cost is reported in artifact WCET");
    check(scalar_exact.scan(scalar_budget) == st::ScanError::ok &&
              scalar_under.scan(scalar_budget - 1) ==
                  st::ScanError::budget_exceeded,
          "L4-B03 scalar dynamic charge matches WCET exactly");

    const std::string find_source = unit(
        "hay : STRING[64] := 'aaaaaaaa'; needle : STRING[32] := 'b'; "
        "found : DINT;", "found := FIND(hay, needle);");
    Rig find_exact;
    Rig find_under;
    check(find_exact.build(find_source) && find_under.build(find_source),
          "L4-B04 FIND cost fixtures build");
    const std::uint32_t find_cost = 64U * 32U + 64U + 32U;
    const std::int64_t find_budget = static_cast<std::int64_t>(
        find_exact.compiled.program.worst_case_instructions);
    check(find_exact.compiled.program.max_standard_function_cost == find_cost,
          "L4-B04 FIND worst-case cost includes search product");
    check(find_exact.scan(find_budget) == st::ScanError::ok &&
              find_under.scan(find_budget - 1) ==
                  st::ScanError::budget_exceeded,
          "L4-B04 FIND dynamic charge matches WCET exactly");
}

// L0-L3 source regression: current-source behavior survives the L4 registry
// and opcode extension.  The L3 case is compile-only because its process
// image transaction behavior belongs to st_l3_tests.cpp.
void prior_layer_regression()
{
    struct Case
    {
        const char *name;
        const char *source;
        const char *result;
        std::int64_t expected;
    };
    const Case cases[] = {
        {"L0 arithmetic",
         "PROGRAM p VAR x : DINT; END_VAR x := 2 + 3 * 4; END_PROGRAM",
         "x", 14},
        {"L1 enum/subrange",
         "TYPE E : (a, b); END_TYPE TYPE R : INT (1..3); END_TYPE "
         "PROGRAM p VAR e : E := E#b; r : R := 3; x : DINT; END_VAR "
         "x := E_TO_DINT(e) + r; END_PROGRAM",
         "x", 4},
        {"L1 aggregate/string",
         "TYPE A : ARRAY[1..2] OF DINT; END_TYPE PROGRAM p VAR a : A; "
         "s : STRING[4] := 'abc'; x : DINT; END_VAR a[2] := 5; "
         "x := a[2]; END_PROGRAM",
         "x", 5},
        {"L2 basic FB",
         "PROGRAM p VAR edge : R_TRIG; x : DINT; END_VAR "
         "edge(CLK := TRUE); IF edge.Q THEN x := 1; END_IF; END_PROGRAM",
         "x", 1},
    };
    for(const Case &test : cases) {
        Rig rig;
        check(rig.build(test.source), test.name);
        check(rig.compiled.ok && rig.scan() == st::ScanError::ok &&
                  rig.i64(test.result) == test.expected,
              test.name);
    }

    const st::CompileResult l3 = st::compile(
        "PROGRAM p VAR mapped AT %MD0 : DWORD; END_VAR mapped := 7; END_PROGRAM");
    check(l3.ok, "L3 located-variable source still compiles");
}

} // namespace

int main()
{
    exact_function_manifest();
    any_resolution_arithmetic_selection_and_comparison();
    numeric_domains_faults_and_evaluation_order();
    integer_division_and_checked_date_time_boundaries();
    shifts_and_rotates();
    string_functions_unicode_and_faults();
    string_capacity_fault_classification();
    date_time_functions_and_utc_boundary();
    cost_budget_determinism_and_zero_allocation();
    standard_cost_consistency();
    prior_layer_regression();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l4 tests passed\n");
    return 0;
}
