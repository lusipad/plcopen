// L1b2 ARRAY/STRUCT acceptance (approved st-l1b2-semantics sections 0-4).
// This is the RED contract for byte-offset aggregate storage: tests inspect
// Program type metadata to prove layout determinism and fault atomicity.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

std::string unit(const std::string &types, const std::string &vars,
                 const std::string &body)
{
    return types + "\nPROGRAM p\nVAR\n" + vars + "\nEND_VAR\n" + body +
           "\nEND_PROGRAM\n";
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

    const st::VarInfo *var(const char *name) const
    {
        for(const st::VarInfo &item : compiled.program.vars) {
            if(item.name == name) {
                return &item;
            }
        }
        return nullptr;
    }

    std::vector<unsigned char> object_bytes(const char *name) const
    {
        const st::VarInfo *item = var(name);
        if(item == nullptr) {
            return {};
        }
        const st::TypeDesc *type = compiled.program.types.get(item->type_id);
        if(type == nullptr || item->offset > sizeof(buffer) ||
           type->size > sizeof(buffer) - item->offset) {
            return {};
        }
        return std::vector<unsigned char>(buffer + item->offset,
                                          buffer + item->offset + type->size);
    }
};

constexpr const char *kAggregateTypes =
    "TYPE Vec3 : ARRAY[-1..1] OF DINT; END_TYPE "
    "TYPE Grid : ARRAY[1..2, -1..1] OF INT; END_TYPE "
    "TYPE Cube : ARRAY[2..3, 0..1, -2..0] OF SINT; END_TYPE "
    "TYPE Point : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE "
    "TYPE Payload : STRUCT ready : BOOL; count : DINT; point : Point; "
    "samples : Vec3; END_STRUCT; END_TYPE";

// L1b2-A01: one to three dimensions use declared bounds and rightmost-
// contiguous row-major layout. Constant accesses cover both endpoints.
void array_dimensions_and_row_major_layout()
{
    Rig rig;
    check(rig.build(unit(
              kAggregateTypes,
              "v : Vec3 := [11, 12, 13]; "
              "g : Grid := [[10, 11, 12], [20, 21, 22]]; "
              "c : Cube; a : DINT; b : DINT; d : DINT; e : DINT;",
              "c[2, 0, -2] := -7; c[3, 1, 0] := 9; "
              "a := v[-1] * 100 + v[1]; "
              "b := g[1, -1] * 100 + g[2, 1]; "
              "d := c[2, 0, -2]; e := c[3, 1, 0];")),
          "L1b2-A01 1D/2D/3D declarations build");
    check(rig.scan() == st::ScanError::ok,
          "L1b2-A01 dimensional accesses scan");
    check(rig.i64("a") == 1113, "L1b2-A01 nonzero 1D bounds");
    check(rig.i64("b") == 1022, "L1b2-A01 2D endpoint oracle");
    check(rig.i64("d") == -7 && rig.i64("e") == 9,
          "L1b2-A01 3D endpoint oracle");

    std::string dump;
    check(rig.compiled.program.types.canonical_dump(dump) ==
              st::TypeError::ok,
          "L1b2-A01 layout dump available");
    check(dump.find("array Vec3 size=12 align=4") != std::string::npos &&
              dump.find("dim -1:1 extent=3 stride=4") !=
                  std::string::npos,
          "L1b2-A01 1D row-major stride");
    check(dump.find("array Grid size=12 align=2") != std::string::npos &&
              dump.find("dim 1:2 extent=2 stride=6") !=
                  std::string::npos &&
              dump.find("dim -1:1 extent=3 stride=2") !=
                  std::string::npos,
          "L1b2-A01 2D rightmost-contiguous strides");
    check(dump.find("array Cube size=12 align=1") != std::string::npos &&
              dump.find("dim 2:3 extent=2 stride=6") !=
                  std::string::npos &&
              dump.find("dim 0:1 extent=2 stride=3") !=
                  std::string::npos &&
              dump.find("dim -2:0 extent=3 stride=1") !=
                  std::string::npos,
          "L1b2-A01 3D rightmost-contiguous strides");
}

// L1b2-A02: all eight integer domains and a subrange may index arrays.
// An out-of-range dynamic store faults before touching the aggregate object.
struct IndexCase
{
    const char *name;
    const char *type;
    const char *lower;
    const char *upper;
    const char *bad;
    const char *initializer;
    long long expected_upper;
};

const IndexCase kIndices[] = {
    {"SINT", "SINT", "-2", "2", "-3", "[41, 42, 43, 44, 45]", 45},
    {"INT", "INT", "-2", "2", "3", "[41, 42, 43, 44, 45]", 45},
    {"DINT", "DINT", "-2", "2", "-3", "[41, 42, 43, 44, 45]", 45},
    {"LINT", "LINT", "-2", "2", "3", "[41, 42, 43, 44, 45]", 45},
    {"USINT", "USINT", "1", "2", "3", "[41, 42]", 42},
    {"UINT", "UINT", "1", "2", "3", "[41, 42]", 42},
    {"UDINT", "UDINT", "1", "2", "3", "[41, 42]", 42},
    {"ULINT", "ULINT", "1", "2", "3", "[41, 42]", 42},
};

void dynamic_index_and_fault_atomicity()
{
    for(const IndexCase &test : kIndices) {
        const std::string types =
            std::string("TYPE A : ARRAY[") + test.lower + ".." +
            test.upper + "] OF DINT; END_TYPE";
        const std::string vars = std::string("a : A := ") +
                                 test.initializer + "; idx : " + test.type +
                                 "; first : DINT; last : DINT; marker : "
                                 "DINT;";
        Rig valid;
        if(!valid.build(unit(types, vars,
                             std::string("idx := ") + test.lower +
                                 "; first := a[idx]; idx := " + test.upper +
                                 "; last := a[idx];"))) {
            fail(test.name);
            continue;
        }
        check(valid.scan() == st::ScanError::ok, test.name);
        check(valid.i64("first") == 41 &&
                  valid.i64("last") == test.expected_upper,
              test.name);

        Rig invalid;
        if(!invalid.build(unit(types, vars,
                               std::string("idx := ") + test.bad +
                                   "; marker := 7; a[idx] := 99; marker := "
                                   "9;"))) {
            fail(test.name);
            continue;
        }
        const std::vector<unsigned char> before = invalid.object_bytes("a");
        check(!before.empty(), "L1b2-A02 aggregate snapshot available");
        check(invalid.scan() == st::ScanError::range_violation, test.name);
        check(invalid.i64("marker") == 7, test.name);
        check(invalid.object_bytes("a") == before, test.name);
        check(invalid.scan() == st::ScanError::range_violation &&
                  invalid.instance.fault() == st::ScanError::range_violation,
              test.name);
    }

    Rig subrange;
    check(subrange.build(unit(
              "TYPE Index : INT (4..6); END_TYPE "
              "TYPE A : ARRAY[4..6] OF DINT; END_TYPE",
              "a : A := [4, 5, 6]; i : Index := 5; out : DINT;",
              "out := a[i];")),
          "L1b2-A02 subrange index builds");
    check(subrange.scan() == st::ScanError::ok &&
              subrange.i64("out") == 5,
          "L1b2-A02 subrange index scans");
}

// L1b2 reject surface: constant indices are folded and diagnosed; unsupported
// index domains and dynamic/open dimensions never enter bytecode generation.
void invalid_array_forms()
{
    const st::CompileResult constant_oob = st::compile(unit(
        "TYPE A : ARRAY[1..3] OF DINT; END_TYPE", "a : A; x : DINT;",
        "x := a[4];"));
    check(!constant_oob.ok &&
              has_code(constant_oob, st::DiagCode::sema_index_out_of_range),
          "L1b2 constant index out of range diagnosed");

    for(const char *type : {"REAL", "LREAL", "BOOL"}) {
        const st::CompileResult result = st::compile(unit(
            "TYPE A : ARRAY[1..3] OF DINT; END_TYPE",
            std::string("a : A; i : ") + type + "; x : DINT;",
            "x := a[i];"));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_type_mismatch),
              "L1b2 noninteger index rejected");
    }

    const st::CompileResult enum_index = st::compile(unit(
        "TYPE E : (one := 1, two := 2); END_TYPE "
        "TYPE A : ARRAY[1..2] OF DINT; END_TYPE",
        "a : A; i : E; x : DINT;", "x := a[i];"));
    check(!enum_index.ok &&
              has_code(enum_index, st::DiagCode::sema_type_mismatch),
          "L1b2 enum index rejected");

    for(const char *declaration : {
            "TYPE A : ARRAY[2..1] OF DINT; END_TYPE",
            "TYPE A : ARRAY[0..0, 0..0, 0..0, 0..0] OF DINT; END_TYPE",
            "TYPE A : ARRAY[-9223372036854775808..9223372036854775807] "
            "OF LREAL; END_TYPE",
        }) {
        const st::CompileResult result =
            st::compile(unit(declaration, "x : DINT;", ";"));
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::sema_invalid_array_bounds),
              "L1b2 invalid array shape diagnosed");
    }

    const st::CompileResult dynamic = st::compile(unit(
        "TYPE A : ARRAY[*] OF DINT; END_TYPE", "x : DINT;", ";"));
    check(!dynamic.ok &&
              has_code(dynamic,
                       st::DiagCode::unsupported_l1b2_dynamic_array),
          "L1b2 dynamic/open array rejected explicitly");
}

// L1b2-A03: STRUCT follows declaration order and deterministic natural
// alignment, independent of the C++ host ABI. Nested aggregate offsets are
// part of the canonical artifact metadata.
void struct_layout_and_nested_access()
{
    const char *types =
        "TYPE Record : STRUCT flag : BOOL; count : DINT; small : SINT; "
        "END_STRUCT; END_TYPE "
        "TYPE Nested : STRUCT record : Record; value : LREAL; END_STRUCT; "
        "END_TYPE "
        "TYPE Row : ARRAY[1..2] OF Record; END_TYPE "
        "TYPE Container : STRUCT rows : Row; selected : INT; END_STRUCT; "
        "END_TYPE";
    Rig rig;
    check(rig.build(unit(
              types,
              "n : Nested; c : Container; out_count : DINT; out_small : "
              "DINT;",
              "n.record.count := 1234; n.record.small := -7; "
              "c.rows[2].count := n.record.count; c.selected := 2; "
              "out_count := c.rows[c.selected].count; "
              "out_small := n.record.small;")),
          "L1b2-A03 nested structs and arrays build");
    check(rig.scan() == st::ScanError::ok,
          "L1b2-A03 nested structs and arrays scan");
    check(rig.i64("out_count") == 1234 && rig.i64("out_small") == -7,
          "L1b2-A03 nested member/subscript access");

    std::string dump;
    check(rig.compiled.program.types.canonical_dump(dump) ==
              st::TypeError::ok,
          "L1b2-A03 canonical struct dump");
    check(dump.find("struct Record size=12 align=4") != std::string::npos &&
              dump.find("field flag type=1 offset=0") != std::string::npos &&
              dump.find("field count type=4 offset=4") != std::string::npos &&
              dump.find("field small type=2 offset=8") != std::string::npos,
          "L1b2-A03 declaration-order natural layout");
    const std::string record_field =
        "field record type=" +
        std::to_string(st::first_load_type_id + st::binding_type::count) +
        " offset=0";
    check(dump.find("struct Nested size=24 align=8") != std::string::npos &&
              dump.find(record_field) !=
                  std::string::npos &&
              dump.find("field value type=11 offset=16") !=
                  std::string::npos,
          "L1b2-A03 nested aggregate alignment");
}

// L1b2-A04: more than twenty hand-derived programs cover default, positional,
// named and partial initialization plus aggregate copy and nested access.
struct Golden
{
    const char *name;
    const char *types;
    const char *vars;
    const char *body;
    const char *result;
    long long expected;
};

const Golden kGolden[] = {
    {"array-default", "TYPE A : ARRAY[1..2] OF DINT; END_TYPE",
     "a : A; out : DINT;", "out := a[1] + a[2];", "out", 0},
    {"array-positional", "TYPE A : ARRAY[1..3] OF DINT; END_TYPE",
     "a : A := [2, 3, 4]; out : DINT;", "out := a[1]+a[2]+a[3];", "out", 9},
    {"array-partial", "TYPE A : ARRAY[1..3] OF DINT; END_TYPE",
     "a : A := [7]; out : DINT;", "out := a[1]+a[2]+a[3];", "out", 7},
    {"array-negative-lower", "TYPE A : ARRAY[-2..0] OF INT; END_TYPE",
     "a : A := [3, 4, 5]; out : INT;", "out := a[-2]*10+a[0];", "out", 35},
    {"array-2d-init", "TYPE A : ARRAY[1..2, 1..2] OF DINT; END_TYPE",
     "a : A := [[1, 2], [3, 4]]; out : DINT;", "out := a[2,1]*10+a[1,2];", "out", 32},
    {"array-3d-init", "TYPE A : ARRAY[0..1, 0..1, 0..1] OF DINT; END_TYPE",
     "a : A := [[[1,2],[3,4]],[[5,6],[7,8]]]; out : DINT;",
     "out := a[1,1,0]*10+a[0,0,1];", "out", 72},
    {"array-low-write", "TYPE A : ARRAY[4..5] OF DINT; END_TYPE",
     "a : A; out : DINT;", "a[4] := 11; out := a[4];", "out", 11},
    {"array-high-write", "TYPE A : ARRAY[4..5] OF DINT; END_TYPE",
     "a : A; out : DINT;", "a[5] := 12; out := a[5];", "out", 12},
    {"array-copy", "TYPE A : ARRAY[1..2] OF DINT; END_TYPE",
     "a : A := [8,9]; b : A; out : DINT;", "b := a; out := b[1]*10+b[2];", "out", 89},
    {"array-enum-element", "TYPE E : (off, on); END_TYPE TYPE A : ARRAY[1..2] OF E; END_TYPE",
     "a : A := [E#on, E#off]; out : DINT;", "out := E_TO_DINT(a[1]);", "out", 1},
    {"struct-default", "TYPE S : STRUCT x : DINT; y : INT; END_STRUCT; END_TYPE",
     "s : S; out : DINT;", "out := s.x + s.y;", "out", 0},
    {"struct-positional", "TYPE S : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE",
     "s : S := (3, 4); out : DINT;", "out := s.x*10+s.y;", "out", 34},
    {"struct-named", "TYPE S : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE",
     "s : S := (y := 6, x := 5); out : DINT;", "out := s.x*10+s.y;", "out", 56},
    {"struct-partial", "TYPE S : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE",
     "s : S := (y := 7); out : DINT;", "out := s.x*10+s.y;", "out", 7},
    {"struct-field-write", "TYPE S : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE",
     "s : S; out : DINT;", "s.x := 17; out := s.x;", "out", 17},
    {"struct-copy", "TYPE S : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE",
     "a : S := (1,2); b : S; out : DINT;", "b := a; out := b.x*10+b.y;", "out", 12},
    {"nested-struct", "TYPE P : STRUCT x : DINT; END_STRUCT; END_TYPE TYPE S : STRUCT p : P; END_STRUCT; END_TYPE",
     "s : S; out : DINT;", "s.p.x := 21; out := s.p.x;", "out", 21},
    {"array-of-struct", "TYPE P : STRUCT x : DINT; END_STRUCT; END_TYPE TYPE A : ARRAY[1..2] OF P; END_TYPE",
     "a : A; out : DINT;", "a[2].x := 22; out := a[2].x;", "out", 22},
    {"struct-array-field", "TYPE A : ARRAY[0..1] OF DINT; END_TYPE TYPE S : STRUCT a : A; END_STRUCT; END_TYPE",
     "s : S; out : DINT;", "s.a[1] := 23; out := s.a[1];", "out", 23},
    {"nested-copy", "TYPE P : STRUCT x : DINT; END_STRUCT; END_TYPE TYPE A : ARRAY[1..2] OF P; END_TYPE TYPE S : STRUCT a : A; END_STRUCT; END_TYPE",
     "a : S; b : S; out : DINT;", "a.a[2].x := 24; b := a; out := b.a[2].x;", "out", 24},
    {"subrange-element", "TYPE R : INT (4..9); END_TYPE TYPE A : ARRAY[1..2] OF R; END_TYPE",
     "a : A := [4,9]; out : INT;", "out := a[1]+a[2];", "out", 13},
    {"subrange-index", "TYPE I : INT (2..3); END_TYPE TYPE A : ARRAY[2..3] OF DINT; END_TYPE",
     "a : A := [30,40]; i : I := 3; out : DINT;", "out := a[i];", "out", 40},
};

void aggregate_initializers_and_golden_programs()
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
            fail(test.name);
        }
    }
    check(programs >= 20, "L1b2-A04 golden program floor");

    const char *point =
        "TYPE Point : STRUCT x : DINT; y : DINT; END_STRUCT; END_TYPE";
    for(const char *vars : {
            "p : Point := (1, y := 2);",
            "p : Point := (x := 1, x := 2);",
            "p : Point := (1, 2, 3);",
        }) {
        check(!st::compile(unit(point, vars, ";")).ok,
              "L1b2 invalid struct initializer rejected");
    }
    check(!st::compile(unit("TYPE A : ARRAY[1..2] OF DINT; END_TYPE",
                            "a : A := [1,2,3];", ";"))
               .ok,
          "L1b2 excess array initializer rejected");
}

// L1b2-D04/D05/D09: aggregate assignment is nominal/exact and aggregate
// comparison is absent. Identical field shapes do not create compatibility.
void aggregate_assignment_and_comparison_rules()
{
    const char *types =
        "TYPE A12 : ARRAY[1..2] OF DINT; END_TYPE "
        "TYPE A02 : ARRAY[0..1] OF DINT; END_TYPE "
        "TYPE First : STRUCT x : DINT; END_STRUCT; END_TYPE "
        "TYPE Second : STRUCT x : DINT; END_STRUCT; END_TYPE";
    for(const char *body : {"a02 := a12;", "second := first;"}) {
        const st::CompileResult result = st::compile(unit(
            types,
            "a12 : A12; a02 : A02; first : First; second : Second; q : "
            "BOOL;",
            body));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_type_mismatch),
              "L1b2 incompatible aggregate operation rejected");
    }
    for(const char *body : {"q := a12 = a12;", "q := first <> first;",
                            "q := first < first;"}) {
        check(!st::compile(unit(
                   types,
                   "a12 : A12; a02 : A02; first : First; second : Second; "
                   "q : BOOL;",
                   body))
                   .ok,
              "L1b2 aggregate comparison rejected");
    }
}

// L1b2-A05: recursive types, independent capacity limits and arithmetic
// overflow fail during loading with stable diagnostics and no partial result.
std::string struct_with_fields(int count)
{
    std::string source = "TYPE Wide : STRUCT ";
    for(int i = 0; i < count; ++i) {
        source += "f" + std::to_string(i) + " : BOOL; ";
    }
    return source + "END_STRUCT; END_TYPE";
}

std::string nested_structs(int depth)
{
    std::string source =
        "TYPE Level0 : STRUCT value : DINT; END_STRUCT; END_TYPE ";
    for(int i = 1; i <= depth; ++i) {
        source += "TYPE Level" + std::to_string(i) + " : STRUCT value : " +
                  "Level" + std::to_string(i - 1) +
                  "; END_STRUCT; END_TYPE ";
    }
    return source;
}

void recursion_and_capacity_contracts()
{
    for(const char *types : {
            "TYPE Node : STRUCT next : Node; END_STRUCT; END_TYPE",
            "TYPE First : STRUCT second : Second; END_STRUCT; END_TYPE "
            "TYPE Second : STRUCT first : First; END_STRUCT; END_TYPE",
        }) {
        const st::CompileResult result =
            st::compile(unit(types, "x : DINT;", ";"));
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_recursive_type),
              "L1b2 recursive aggregate rejected");
    }

    st::CompileOptions options;
    options.max_array_elements = 65536;
    check(st::compile(unit(
                          "TYPE Limit : ARRAY[0..65535] OF BOOL; END_TYPE",
                          "x : DINT;", ";"),
                      options)
              .ok,
          "L1b2 array element capacity endpoint accepted");
    const st::CompileResult too_many_elements = st::compile(
        unit("TYPE TooMany : ARRAY[0..65536] OF BOOL; END_TYPE",
             "x : DINT;", ";"),
        options);
    check(!too_many_elements.ok &&
              has_code(too_many_elements, st::DiagCode::capacity_exceeded),
          "L1b2 array element capacity bounded");

    options = st::CompileOptions{};
    options.max_struct_fields = 256;
    check(st::compile(unit(struct_with_fields(256), "x : DINT;", ";"),
                      options)
              .ok,
          "L1b2 struct field capacity endpoint accepted");
    const st::CompileResult too_many_fields = st::compile(
        unit(struct_with_fields(257), "x : DINT;", ";"), options);
    check(!too_many_fields.ok &&
              has_code(too_many_fields, st::DiagCode::capacity_exceeded),
          "L1b2 struct field capacity bounded");

    options = st::CompileOptions{};
    options.max_aggregate_depth = 16;
    check(st::compile(unit(nested_structs(15), "x : Level15;", ";"),
                      options)
              .ok,
          "L1b2 aggregate depth endpoint accepted");
    const st::CompileResult too_deep = st::compile(
        unit(nested_structs(16), "x : Level16;", ";"), options);
    check(!too_deep.ok &&
              has_code(too_deep, st::DiagCode::capacity_exceeded),
          "L1b2 aggregate depth bounded");

    options = st::CompileOptions{};
    options.max_vars_bytes = 8;
    const st::CompileResult object_too_large = st::compile(
        unit("TYPE A : ARRAY[1..3] OF DINT; END_TYPE", "a : A;", ";"),
        options);
    check(!object_too_large.ok &&
              has_code(object_too_large, st::DiagCode::capacity_variables),
          "L1b2 static object storage bounded");
}

// L1b2 determinism: source recompilation preserves code and canonical type
// metadata. Existing L0/L1a source behavior remains stable after ABI-v2.
void deterministic_artifact_and_existing_source_regression()
{
    const std::string source = unit(
        kAggregateTypes, "p : Payload; g : Grid; out : DINT;",
        "p.point.x := 7; g[2,1] := 8; out := p.point.x + g[2,1];");
    const st::CompileResult first = st::compile(source);
    const st::CompileResult second = st::compile(source);
    check(first.ok && second.ok, "L1b2 deterministic fixture compiles");
    std::string first_dump;
    std::string second_dump;
    check(first.ok && second.ok &&
              first.program.types.canonical_dump(first_dump) ==
                  st::TypeError::ok &&
              second.program.types.canonical_dump(second_dump) ==
                  st::TypeError::ok &&
              first_dump == second_dump &&
              first.program.code == second.program.code &&
              first.program.vars_bytes == second.program.vars_bytes,
          "L1b2 bytecode and layout are deterministic");

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
    array_dimensions_and_row_major_layout();
    dynamic_index_and_fault_atomicity();
    invalid_array_forms();
    struct_layout_and_nested_access();
    aggregate_initializers_and_golden_programs();
    aggregate_assignment_and_comparison_rules();
    recursion_and_capacity_contracts();
    deterministic_artifact_and_existing_source_regression();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l1b2 tests passed (%zu golden programs)\n",
                sizeof(kGolden) / sizeof(kGolden[0]));
    return 0;
}
