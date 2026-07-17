// L1b3 fixed string and calendar acceptance (approved
// st-l1b3-semantics sections 0-4). This is the RED contract for bounded
// UTF-8/Unicode-scalar storage and host-independent integer calendar rules.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include "st/st.h"

namespace
{
std::atomic<std::uint64_t> allocation_count{0};
}

void *operator new(std::size_t size)
{
    allocation_count.fetch_add(1, std::memory_order_relaxed);
    if(void *pointer = std::malloc(size)) {
        return pointer;
    }
    std::abort();
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
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

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 1000000;

std::uint32_t read_u32(const unsigned char *bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void write_u32(unsigned char *bytes, std::uint32_t value)
{
    bytes[0] = static_cast<unsigned char>(value);
    bytes[1] = static_cast<unsigned char>(value >> 8U);
    bytes[2] = static_cast<unsigned char>(value >> 16U);
    bytes[3] = static_cast<unsigned char>(value >> 24U);
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

    const st::VarInfo *var(const char *name) const
    {
        for(const st::VarInfo &item : compiled.program.vars) {
            if(item.name == name) {
                return &item;
            }
        }
        return nullptr;
    }

    const st::TypeDesc *type(const char *name) const
    {
        const st::VarInfo *item = var(name);
        return item == nullptr ? nullptr
                               : compiled.program.types.get(item->type_id);
    }

    std::vector<unsigned char> object_bytes(const char *name) const
    {
        const st::VarInfo *item = var(name);
        const st::TypeDesc *desc = type(name);
        if(item == nullptr || desc == nullptr || item->offset > sizeof(buffer) ||
           desc->size > sizeof(buffer) - item->offset) {
            return {};
        }
        return std::vector<unsigned char>(buffer + item->offset,
                                          buffer + item->offset + desc->size);
    }

    std::string string_value(const char *name) const
    {
        const std::vector<unsigned char> bytes = object_bytes(name);
        if(bytes.size() < 4) {
            return {};
        }
        const std::uint32_t length = read_u32(bytes.data());
        if(length > bytes.size() - 4) {
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
        result.reserve(length);
        for(std::uint32_t i = 0; i < length; ++i) {
            result.push_back(read_u32(bytes.data() + 4U + i * 4U));
        }
        return result;
    }
};

std::string utf8_cat()
{
    return "\xE7\x8C\xAB";
}

std::string utf8_grinning()
{
    return "\xF0\x9F\x98\x80";
}

// L1b3-A01/D01-D03: defaults, fixed capacities, UTF-8 byte counts,
// Unicode-scalar counts and deterministic IEC '$' escapes.
void declarations_literals_defaults_and_layout()
{
    const std::string cat = utf8_cat();
    const std::string grin = utf8_grinning();
    Rig rig;
    const std::string source = unit(
        "c : CHAR := 'A'; wc : WCHAR := \"" + grin +
            "\"; empty : STRING[4]; default_s : STRING; "
            "s : STRING[5] := 'A" + cat +
            "B'; ws : WSTRING[2] := \"" + cat + grin +
            "\"; escaped : STRING[3] := 'A$N$$'; "
            "wescaped : WSTRING[3] := \"A$N$$\";",
        ";");
    check(rig.build(source), "L1b3-A01 declarations and literals build");
    check(rig.i64("c") == 65 && rig.i64("wc") == 0x1F600,
          "L1b3-A01 CHAR/WCHAR initialization");
    check(rig.string_value("empty").empty(),
          "L1b3-A01 STRING default is empty");
    check(rig.string_value("s") == std::string("A") + cat + "B",
          "L1b3-A01 STRING stores valid UTF-8 bytes");
    check(rig.wstring_value("ws") ==
              std::vector<std::uint32_t>({0x732B, 0x1F600}),
          "L1b3-A01 WSTRING stores Unicode scalars");
    check(rig.string_value("escaped") == std::string("A\n$", 3),
          "L1b3-A01 STRING escape decoding");
    check(rig.wstring_value("wescaped") ==
              std::vector<std::uint32_t>({'A', '\n', '$'}),
          "L1b3-A01 WSTRING escape decoding");

    const st::TypeDesc *character = rig.type("c");
    const st::TypeDesc *wide_character = rig.type("wc");
    const st::TypeDesc *string = rig.type("s");
    const st::TypeDesc *wide_string = rig.type("ws");
    const st::TypeDesc *default_string = rig.type("default_s");
    check(character != nullptr && character->size == 1,
          "L1b3 CHAR is one byte");
    check(wide_character != nullptr && wide_character->size == 4,
          "L1b3 WCHAR is one 32-bit scalar");
    check(string != nullptr && string->kind == st::TypeKind::string &&
              string->string.capacity == 5 && string->size == 9,
          "L1b3 STRING fixed byte-capacity layout");
    check(wide_string != nullptr &&
              wide_string->kind == st::TypeKind::wstring &&
              wide_string->string.capacity == 2 && wide_string->size == 12,
          "L1b3 WSTRING fixed scalar-capacity layout");
    check(default_string != nullptr &&
              default_string->string.capacity == 80,
          "L1b3 omitted STRING capacity defaults to 80");
}

// L1b3-A02/D01/D02/D04: capacity is measured in UTF-8 bytes for STRING and
// Unicode scalars for WSTRING. Invalid encodings and invalid capacity forms
// are load-domain failures; no literal is silently truncated.
void capacity_and_encoding_rejections()
{
    const std::string cat = utf8_cat();
    const std::string grin = utf8_grinning();
    check(st::compile(unit("s : STRING[3] := '" + cat + "';", ";")).ok,
          "L1b3 STRING exact multibyte endpoint accepted");
    const st::CompileResult string_small =
        st::compile(unit("s : STRING[2] := '" + cat + "';", ";"));
    check(!string_small.ok &&
              has_code(string_small,
                       st::DiagCode::sema_string_capacity_exceeded),
          "L1b3 STRING rejects a split UTF-8 code point");
    check(st::compile(unit("s : WSTRING[1] := \"" + grin + "\";", ";"))
              .ok,
          "L1b3 WSTRING counts one supplementary scalar");
    const st::CompileResult wstring_small = st::compile(
        unit("s : WSTRING[1] := \"A" + grin + "\";", ";"));
    check(!wstring_small.ok &&
              has_code(wstring_small,
                       st::DiagCode::sema_string_capacity_exceeded),
          "L1b3 WSTRING scalar capacity is bounded");

    for(const char *declaration : {"s : STRING[0];",
                                   "s : STRING[4097];",
                                   "s : WSTRING[0];",
                                   "s : WSTRING[4097];"}) {
        const st::CompileResult result = st::compile(unit(declaration, ";"));
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::sema_string_capacity_exceeded),
              "L1b3 declaration capacity outside 1..4096 rejected");
    }

    std::string invalid_utf8 = "PROGRAM p VAR s : STRING[4] := '";
    invalid_utf8.push_back(static_cast<char>(0xC3));
    invalid_utf8.push_back('(');
    invalid_utf8 += "'; END_VAR END_PROGRAM";
    const st::CompileResult bad_utf8 = st::compile(invalid_utf8);
    check(!bad_utf8.ok &&
              has_code(bad_utf8,
                       st::DiagCode::sema_invalid_string_literal),
          "L1b3 malformed UTF-8 literal rejected");

    std::string surrogate = "PROGRAM p VAR s : WSTRING[1] := \"";
    surrogate.append("\xED\xA0\x80", 3);
    surrogate += "\"; END_VAR END_PROGRAM";
    const st::CompileResult bad_surrogate = st::compile(surrogate);
    check(!bad_surrogate.ok &&
              has_code(bad_surrogate,
                       st::DiagCode::sema_invalid_string_literal),
          "L1b3 surrogate WSTRING literal rejected");

    std::string above_unicode = "PROGRAM p VAR s : WSTRING[1] := \"";
    above_unicode.append("\xF4\x90\x80\x80", 4);
    above_unicode += "\"; END_VAR END_PROGRAM";
    const st::CompileResult bad_scalar = st::compile(above_unicode);
    check(!bad_scalar.ok &&
              has_code(bad_scalar,
                       st::DiagCode::sema_invalid_string_literal),
          "L1b3 scalar above U+10FFFF rejected");
}

// L1b3-D04: a runtime-sized assignment faults before modifying the target,
// leaves preceding writes visible and locks the fault until reset.
void runtime_string_capacity_fault_is_atomic()
{
    Rig rig;
    check(rig.build(unit(
              "source : STRING[5] := 'hello'; target : STRING[4] := 'keep'; "
              "marker : DINT;",
              "marker := 7; target := source; marker := 9;")),
          "L1b3 runtime capacity fixture builds");
    const std::vector<unsigned char> before = rig.object_bytes("target");
    check(!before.empty(), "L1b3 runtime capacity target snapshot");
    check(rig.scan() == st::ScanError::string_capacity_exceeded,
          "L1b3 runtime capacity faults");
    check(rig.i64("marker") == 7,
          "L1b3 runtime capacity stops at faulting instruction");
    check(rig.object_bytes("target") == before &&
              rig.string_value("target") == "keep",
          "L1b3 runtime capacity fault does not write target");
    check(rig.scan() == st::ScanError::string_capacity_exceeded &&
              rig.instance.fault() ==
                  st::ScanError::string_capacity_exceeded,
          "L1b3 runtime capacity fault locks");
    rig.instance.reset();
    check(rig.instance.fault() == st::ScanError::ok,
          "L1b3 runtime capacity fault resets explicitly");
}

// L1b3-D05 plus the requested indexing contract: comparisons use Unicode
// scalar order. STRING/WSTRING indexing is one-based and bounded by the
// current value length rather than the declared capacity.
void indexing_and_comparison()
{
    Rig rig;
    const std::string grin = utf8_grinning();
    check(rig.build(unit(
              "a : STRING[8] := 'abc'; b : STRING[8] := 'abd'; "
              "wa : WSTRING[2] := \"A" + grin +
                  "\"; wb : WSTRING[2] := \"B" + grin +
                  "\"; first : CHAR; last : CHAR; smile : WCHAR; "
                  "eq : BOOL; lt : BOOL; wlt : BOOL;",
              "first := a[1]; last := a[3]; smile := wa[2]; "
              "eq := a = 'abc'; lt := a < b; wlt := wa < wb;")),
          "L1b3 indexing and comparison fixture builds");
    check(rig.scan() == st::ScanError::ok,
          "L1b3 indexing and comparison scans");
    check(rig.i64("first") == 'a' && rig.i64("last") == 'c' &&
              rig.i64("smile") == 0x1F600,
          "L1b3 one-based STRING/WSTRING indexing");
    check(rig.i64("eq") == 1 && rig.i64("lt") == 1 &&
              rig.i64("wlt") == 1,
          "L1b3 scalar-sequence comparison");

    Rig assigned;
    check(assigned.build(unit("s : STRING[8]; c : CHAR;",
                              "s := 'abc'; c := s[1];")) &&
              assigned.scan() == st::ScanError::ok &&
              assigned.i64("c") == 'a',
          "L1b3 mutable string index uses assigned current length");

    const st::CompileResult impossible_index = st::compile(
        unit("s : STRING[8] := 'abc'; c : CHAR;", "c := s[9];"));
    check(!impossible_index.ok &&
              has_code(impossible_index,
                       st::DiagCode::sema_index_out_of_range),
          "L1b3 constant string index beyond capacity is rejected");

    Rig dynamic_oob;
    check(dynamic_oob.build(unit(
              "s : STRING[8] := 'abc'; i : DINT := 4; c : CHAR := 'Z'; "
              "marker : DINT;",
              "marker := 7; c := s[i]; marker := 9;")),
          "L1b3 dynamic string index fixture builds");
    check(dynamic_oob.scan() == st::ScanError::range_violation,
          "L1b3 dynamic string index faults");
    check(dynamic_oob.i64("c") == 'Z' && dynamic_oob.i64("marker") == 7,
          "L1b3 dynamic string index fault is atomic");
}

// L1b3-D06: character/integer conversions are explicit bit/scalar
// conversions. String-width conversion is absent from L1b3.
void explicit_character_conversions()
{
    Rig rig;
    check(rig.build(unit(
              "c : CHAR; wc : WCHAR; u8 : USINT; u32 : UDINT;",
              "c := USINT_TO_CHAR(16#FF); u8 := CHAR_TO_USINT(c); "
              "wc := UDINT_TO_WCHAR(16#1F600); "
              "u32 := WCHAR_TO_UDINT(wc);")),
          "L1b3 explicit character conversions build");
    check(rig.scan() == st::ScanError::ok && rig.i64("u8") == 255 &&
              rig.i64("u32") == 0x1F600,
          "L1b3 explicit character conversions preserve values");

    for(const char *body : {"u8 := c;", "c := u8;", "u32 := wc;",
                            "wc := u32;"}) {
        const st::CompileResult result = st::compile(unit(
            "c : CHAR; wc : WCHAR; u8 : USINT; u32 : UDINT;", body));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_type_mismatch),
              "L1b3 implicit character/integer conversion rejected");
    }
    for(const char *body : {"s := ws;", "ws := s;"}) {
        const st::CompileResult result = st::compile(
            unit("s : STRING[8]; ws : WSTRING[8];", body));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_type_mismatch),
              "L1b3 STRING/WSTRING implicit conversion rejected");
    }

    const st::CompileResult invalid_scalar = st::compile(unit(
        "wc : WCHAR;", "wc := UDINT_TO_WCHAR(16#D800);"));
    check(!invalid_scalar.ok &&
              has_code(invalid_scalar,
                       st::DiagCode::sema_literal_out_of_range),
          "L1b3 constant invalid WCHAR conversion rejected");

    Rig dynamic_invalid;
    check(dynamic_invalid.build(unit(
              "raw : UDINT := 16#110000; wc : WCHAR := \"A\"; marker : "
              "DINT;",
              "marker := 7; wc := UDINT_TO_WCHAR(raw); marker := 9;")),
          "L1b3 dynamic invalid WCHAR fixture builds");
    check(dynamic_invalid.scan() == st::ScanError::conversion_invalid &&
              dynamic_invalid.i64("wc") == 'A' &&
              dynamic_invalid.i64("marker") == 7,
          "L1b3 dynamic invalid WCHAR conversion faults atomically");
}

// Host-library-free proleptic Gregorian oracle. The result is the signed
// day count relative to 1970-01-01.
constexpr std::int64_t days_from_civil(std::int64_t year, unsigned month,
                                       unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned shifted_month =
        static_cast<unsigned>(static_cast<int>(month) +
                              (month > 2 ? -3 : 9));
    const unsigned doy = (153U * shifted_month + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

constexpr std::int64_t kSecondNs = 1000000000LL;
constexpr std::int64_t kDayNs = 86400LL * kSecondNs;

// L1b3-A03/D07-D09: Gregorian leap rules, TOD nanoseconds and UTC DT are
// derived exclusively with integer arithmetic.
void date_time_literals_and_oracle()
{
    Rig rig;
    check(rig.build(unit(
              "epoch : DATE := D#1970-01-01; leap : DATE := "
              "DATE#2000-02-29; before_epoch : DATE := D#1969-12-31; "
              "noon : TOD := TOD#12:34:56.123456789; last : TOD := "
              "TOD#23:59:59.999999999; dt_epoch : DT := "
              "DT#1970-01-01-00:00:00; dt_leap : DT := "
              "DT#2000-02-29-12:34:56.123456789;",
              ";")),
          "L1b3-A03 date/time literals build");
    check(rig.i64("epoch") == 0 && rig.i64("before_epoch") == -1 &&
              rig.i64("leap") == days_from_civil(2000, 2, 29),
          "L1b3-A03 DATE Gregorian day oracle");
    const std::int64_t noon =
        (12LL * 3600 + 34LL * 60 + 56LL) * kSecondNs + 123456789LL;
    check(rig.i64("noon") == noon && rig.i64("last") == kDayNs - 1,
          "L1b3-A03 TOD nanosecond oracle");
    const std::int64_t leap_dt =
        days_from_civil(2000, 2, 29) * kDayNs + noon;
    check(rig.i64("dt_epoch") == 0 && rig.i64("dt_leap") == leap_dt,
          "L1b3-A03 DT UTC nanosecond oracle");

    struct InvalidCalendar
    {
        const char *type;
        const char *literal;
    };
    const InvalidCalendar invalid[] = {
        {"DATE", "D#1900-02-29"},
        {"DATE", "DATE#2001-02-29"},
        {"DATE", "DATE#2024-04-31"},
        {"TOD", "TOD#24:00:00"},
        {"TOD", "TOD#12:60:00"},
        {"TOD", "TOD#12:00:60"},
        {"DT", "DT#2023-02-29-00:00:00"},
        {"DT", "DT#2262-04-11-23:47:16.854775808"},
        {"DT", "DT#1677-09-21-00:12:43.145224191"},
        {"DT", "DT#9999-12-31-23:59:59.999999999"},
    };
    for(const InvalidCalendar &test : invalid) {
        const st::CompileResult result = st::compile(
            unit(std::string("x : ") + test.type + " := " + test.literal +
                     ";",
                 ";"));
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::date_time_range_violation),
              "L1b3 invalid calendar literal rejected");
    }

    Rig dt_limits;
    check(dt_limits.build(unit(
              "lo : DT := DT#1677-09-21-00:12:43.145224192; "
              "hi : DT := DT#2262-04-11-23:47:16.854775807;",
              ";")) &&
              dt_limits.i64("lo") ==
                  std::numeric_limits<std::int64_t>::min() &&
              dt_limits.i64("hi") ==
                  std::numeric_limits<std::int64_t>::max(),
          "L1b3 DT int64 endpoints are exact");
}

// L1b3-D10/A04: DATE days and DT/TOD nanoseconds have exact integer
// arithmetic. TOD wraps modulo one day; DATE/DT overflow faults atomically.
void date_time_arithmetic_and_faults()
{
    Rig rig;
    check(rig.build(unit(
              "d0 : DATE := D#2000-02-28; d1 : DATE; d2 : DATE; "
              "tod : TOD := TOD#23:59:59.500000000; dt : DT := "
              "DT#1970-01-01-00:00:00; q : BOOL;",
              "d1 := d0 + 1; d2 := d1 + 1; "
              "tod := tod + T#1s; dt := dt - T#1ns; "
              "q := d1 < d2;")),
          "L1b3-A04 date/time arithmetic builds");
    check(rig.scan() == st::ScanError::ok,
          "L1b3-A04 date/time arithmetic scans");
    check(rig.i64("d1") == days_from_civil(2000, 2, 29) &&
              rig.i64("d2") == days_from_civil(2000, 3, 1),
          "L1b3-A04 DATE plus integer day");
    check(rig.i64("tod") == 500000000,
          "L1b3-A04 TOD plus TIME wraps modulo one day");
    check(rig.i64("dt") == -1 && rig.i64("q") == 1,
          "L1b3-A04 DT arithmetic and DATE comparison");

    Rig dt_overflow;
    check(dt_overflow.build(unit(
              "source : DT := DT#2262-04-11-23:47:16.854775807; "
              "target : DT := DT#1970-01-01-00:00:07; marker : DINT;",
              "marker := 7; target := source + T#1ns; marker := 9;")),
          "L1b3 DT overflow fixture builds");
    check(dt_overflow.scan() == st::ScanError::date_time_range_violation,
          "L1b3 DT overflow faults");
    check(dt_overflow.i64("target") == 7LL * kSecondNs &&
              dt_overflow.i64("marker") == 7,
          "L1b3 DT overflow does not write target");
    check(dt_overflow.scan() ==
                  st::ScanError::date_time_range_violation &&
              dt_overflow.instance.fault() ==
                  st::ScanError::date_time_range_violation,
          "L1b3 DT overflow fault locks");

    Rig date_overflow;
    check(date_overflow.build(unit(
              "target : DATE := D#1970-01-01; delta : DINT := 2147483647; "
              "one : DINT := 1; marker : DINT;",
              "target := target + delta; marker := 7; "
              "target := target + one; marker := 9;")),
          "L1b3 DATE overflow fixture builds");
    check(date_overflow.scan() ==
                  st::ScanError::date_time_range_violation &&
              date_overflow.i64("target") == 2147483647 &&
              date_overflow.i64("marker") == 7,
          "L1b3 DATE overflow faults before target write");
}

// L1b3-D09 rejection surface: the VM never guesses locale, timezone or DST.
void timezone_and_locale_are_explicitly_unsupported()
{
    for(const char *literal : {"DT#2024-01-01-00:00:00Z",
                               "DT#2024-01-01-00:00:00+08:00",
                               "DT#2024-01-01-00:00:00CST"}) {
        const st::CompileResult result = st::compile(
            unit(std::string("x : DT := ") + literal + ";", ";"));
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::unsupported_l1b3_timezone),
              "L1b3 timezone inference rejected explicitly");
    }
}

// Capacity contract: the program reports a bounded maximum string-operation
// cost, and the combined string constant pool cannot exceed 64 KiB.
void bounded_cost_and_constant_pool()
{
    const st::CompileResult bounded = st::compile(unit(
        "a : STRING[8] := 'abc'; b : STRING[8] := 'abd'; q : BOOL;",
        "q := a < b;"));
    check(bounded.ok && bounded.program.max_string_operation_cost >= 8,
          "L1b3 compile artifact reports maximum string operation cost");

    Rig narrow;
    Rig wide;
    check(narrow.build(unit(
              "a : STRING[8] := 'abc'; b : STRING[8] := 'abc'; q : BOOL;",
              "q := a = b;")) &&
              wide.build(unit(
                  "a : STRING[4096] := 'abc'; "
                  "b : STRING[4096] := 'abc'; q : BOOL;",
                  "q := a = b;")),
          "L1b3 string budget fixtures build");
    check(narrow.scan(10) == st::ScanError::ok,
          "L1b3 narrow comparison fits capacity-based budget");
    check(wide.scan(4097) == st::ScanError::budget_exceeded,
          "L1b3 wide comparison charges capacity to budget");
    Rig exact;
    check(exact.build(unit(
              "a : STRING[4096] := 'abc'; "
              "b : STRING[4096] := 'abc'; q : BOOL;",
              "q := a = b;")) &&
              exact.scan(4098) == st::ScanError::ok,
          "L1b3 string comparison budget exact boundary");

    std::string vars;
    for(int i = 0; i < 17; ++i) {
        const std::string payload =
            std::string(4095, 'a') + static_cast<char>('A' + i);
        vars += "s" + std::to_string(i) + " : STRING[4096] := '" +
                payload + "';";
    }
    const st::CompileResult too_many_constants =
        st::compile(unit(vars, ";"));
    check(!too_many_constants.ok &&
              has_code(too_many_constants, st::DiagCode::capacity_code),
          "L1b3 string constant pool is bounded to 64 KiB");
}

void corrupted_runtime_string_objects_are_rejected()
{
    Rig overlong;
    check(overlong.build(unit(
              "a : STRING[8] := 'abc'; b : STRING[8] := 'abc'; q : BOOL;",
              "q := a = b;")),
          "L1b3 malformed UTF-8 fixture builds");
    const st::VarInfo *a = overlong.var("a");
    if(a != nullptr) {
        write_u32(overlong.buffer + a->offset, 3);
        overlong.buffer[a->offset + 4] = 0xE0U;
        overlong.buffer[a->offset + 5] = 0x80U;
        overlong.buffer[a->offset + 6] = 0x80U;
    }
    check(a != nullptr &&
              overlong.scan() == st::ScanError::invalid_bytecode,
          "L1b3 runtime overlong UTF-8 is rejected");

    Rig corrupt_length;
    check(corrupt_length.build(unit(
              "s : STRING[8] := 'abc'; i : DINT := 100; c : CHAR;",
              "c := s[i];")),
          "L1b3 corrupt length fixture builds");
    const st::VarInfo *s = corrupt_length.var("s");
    if(s != nullptr) {
        write_u32(corrupt_length.buffer + s->offset, 0xFFFFFFFFU);
    }
    check(s != nullptr &&
              corrupt_length.scan() == st::ScanError::invalid_bytecode,
          "L1b3 corrupt STRING length faults before indexing");

    Rig surrogate;
    check(surrogate.build(unit(
              "s : WSTRING[1] := \"A\"; i : DINT := 1; c : WCHAR;",
              "c := s[i];")),
          "L1b3 invalid WSTRING scalar fixture builds");
    const st::VarInfo *w = surrogate.var("s");
    if(w != nullptr) {
        write_u32(surrogate.buffer + w->offset + 4U, 0xD800U);
    }
    check(w != nullptr &&
              surrogate.scan() == st::ScanError::invalid_bytecode,
          "L1b3 runtime WSTRING surrogate is rejected");

    for(const char *source : {
            "PROGRAM p VAR s : STRING[8]; END_VAR s := 'abc'; END_PROGRAM",
            "PROGRAM p VAR s : WSTRING[8]; END_VAR s := \"abc\"; END_PROGRAM"}) {
        Rig truncated;
        check(truncated.build(source),
              "L1b3 truncated constant fixture builds");
        truncated.compiled.program.string_constants.resize(4);
        truncated.compiled.program.string_constants.shrink_to_fit();
        check(truncated.scan() == st::ScanError::invalid_bytecode,
              "L1b3 truncated string constant faults before payload read");
    }
}

void maximum_capacity_scan_is_allocation_free()
{
    Rig rig;
    check(rig.build(unit(
              "a : STRING[4096] := 'abc'; b : STRING[4096] := 'abc'; "
              "copy : STRING[4096]; q : BOOL; i : DINT := 1; c : CHAR;",
              "copy := a; q := a = b; c := copy[i];")),
          "L1b3 maximum-capacity RT fixture builds");
    check(rig.scan() == st::ScanError::ok,
          "L1b3 maximum-capacity RT warmup succeeds");
    const std::uint64_t before =
        allocation_count.load(std::memory_order_relaxed);
    for(int i = 0; i < 1000; ++i) {
        if(rig.scan() != st::ScanError::ok) {
            fail("L1b3 maximum-capacity frozen scan succeeds");
            break;
        }
    }
    check(allocation_count.load(std::memory_order_relaxed) == before,
          "L1b3 maximum-capacity copy/compare/index allocate zero");
}

// L1b3-A05/A07: recompilation preserves bytecode, constants, type metadata,
// initial object image and runtime results. Existing L0/L1a source behavior
// remains stable after the value-universe expansion.
void deterministic_artifact_and_existing_source_regression()
{
    const std::string source = unit(
        "s : STRING[8] := 'abc'; ws : WSTRING[2] := \"A" +
            utf8_grinning() +
            "\"; d : DATE := D#2000-02-29; t : TOD := "
            "TOD#12:34:56.123456789; dt : DT := "
            "DT#2000-02-29-12:34:56.123456789; q : BOOL;",
        "q := s = 'abc';");
    Rig first;
    Rig second;
    check(first.build(source) && second.build(source),
          "L1b3 deterministic fixture compiles twice");
    std::string first_types;
    std::string second_types;
    check(first.compiled.ok && second.compiled.ok &&
              first.compiled.program.types.canonical_dump(first_types) ==
                  st::TypeError::ok &&
              second.compiled.program.types.canonical_dump(second_types) ==
                  st::TypeError::ok &&
              first.compiled.program.code == second.compiled.program.code &&
              first.compiled.program.constants ==
                  second.compiled.program.constants &&
              first_types == second_types &&
              first.compiled.program.vars_bytes ==
                  second.compiled.program.vars_bytes &&
              first.object_bytes("s") == second.object_bytes("s") &&
              first.object_bytes("ws") == second.object_bytes("ws"),
          "L1b3 bytecode/constants/types/image are deterministic");
    check(first.scan() == st::ScanError::ok &&
              second.scan() == st::ScanError::ok &&
              first.i64("q") == 1 && second.i64("q") == 1,
          "L1b3 deterministic runtime result");

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
         "x := 2; CASE x OF 1: out := 10; 2: out := 20; END_CASE;",
         "out", 20},
        {"L1a wrap", "x : SINT;", "x := 127; x := x + 1;", "x", -128},
        {"L1a conversion", "x : DINT;",
         "x := LINT_TO_DINT(4294967297);", "x", 1},
    };
    for(const ExistingCase &test : cases) {
        Rig rig;
        if(!rig.build(unit(test.vars, test.body))) {
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
    declarations_literals_defaults_and_layout();
    capacity_and_encoding_rejections();
    runtime_string_capacity_fault_is_atomic();
    indexing_and_comparison();
    explicit_character_conversions();
    date_time_literals_and_oracle();
    date_time_arithmetic_and_faults();
    timezone_and_locale_are_explicitly_unsupported();
    bounded_cost_and_constant_pool();
    corrupted_runtime_string_objects_are_rejected();
    maximum_capacity_scan_is_allocation_free();
    deterministic_artifact_and_existing_source_regression();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l1b3 tests passed\n");
    return 0;
}
