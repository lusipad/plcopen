// L2b user-POU acceptance (approved st-l2b-semantics sections 1-4).
//
// This is intentionally a RED contract.  It names the public load-selection,
// POU metadata, incremental-front-end, diagnostic and runtime-fault surfaces
// required by the approved matrix; it must not be weakened to fit the former
// single-PROGRAM implementation.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "st/st.h"

namespace
{

using namespace plcopen::core;

constexpr std::int64_t kPeriodNs = 1000000;
constexpr std::int64_t kBudget = 1000000;

int failures = 0;
bool g_freeze_allocations = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
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

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[262144] = {};

    bool build(const std::string &source, const char *program = "main")
    {
        instance.unload();
        compiled = st::compile(source);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        return instance.load(compiled.program, program, buffer, sizeof(buffer),
                             kPeriodNs) == rt::ErrorCode::ok;
    }

    st::ScanError scan(std::int64_t budget = kBudget)
    {
        return instance.scan(budget);
    }

    std::int64_t i64(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0
                   ? -424242
                   : instance.value_i64(static_cast<std::size_t>(index));
    }
};

struct Golden
{
    const char *name;
    const char *source;
    const char *program;
    const char *result;
    int scans;
    long long expected;
};

const st::PouInfo *pou_info(const st::CompileResult &result,
                            const char *lower);

const st::Program *program_info(const st::CompileResult &result,
                                const char *lower)
{
    for(const st::Program &program : result.program.programs) {
        if(program.program_name == lower) return &program;
    }
    return nullptr;
}

std::int64_t exact_budget(const st::CompileResult &result,
                          const char *program_name)
{
    for(std::int64_t budget = 1; budget < 1024; ++budget) {
        st::Instance instance;
        alignas(8) unsigned char buffer[262144] = {};
        if(instance.load(result.program, program_name, buffer, sizeof(buffer),
                         kPeriodNs) == rt::ErrorCode::ok &&
           instance.scan(budget) == st::ScanError::ok) {
            return budget;
        }
    }
    return 0;
}

bool same_vars(const std::vector<st::VarInfo> &left,
               const std::vector<st::VarInfo> &right)
{
    if(left.size() != right.size()) return false;
    for(std::size_t index = 0; index < left.size(); ++index) {
        const st::VarInfo &a = left[index];
        const st::VarInfo &b = right[index];
        if(a.name != b.name || a.lower != b.lower || a.type != b.type ||
           a.type_id != b.type_id || a.constant != b.constant ||
           a.offset != b.offset || a.init_bits != b.init_bits ||
           a.initial_length != b.initial_length) {
            return false;
        }
    }
    return true;
}

// L2b-A01: at least thirty hand-derived programs lock FUNCTION/FB/PROGRAM
// scope, automatic/persistent lifetime, calls, returns, interfaces and EN/ENO.
const Golden kGolden[] = {
    {"function-add",
     "FUNCTION UserAdd : DINT\nVAR_INPUT A : DINT; B : DINT; END_VAR\n"
     "UserAdd := A + B;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := UserAdd(2, 3);\nEND_PROGRAM\n",
     "main", "out", 1, 5},
    {"function-forward-call",
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := Twice(6);\nEND_PROGRAM\n"
     "FUNCTION Twice : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "Twice := X * 2;\nEND_FUNCTION\n",
     "main", "out", 1, 12},
    {"function-nested-call",
     "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "Inc := X + 1;\nEND_FUNCTION\n"
     "FUNCTION Twice : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "Twice := Inc(Inc(X));\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := Twice(8);\nEND_PROGRAM\n",
     "main", "out", 1, 10},
    {"function-local-is-automatic",
     "FUNCTION One : DINT\nVAR N : DINT; END_VAR\n"
     "N := N + 1; One := N;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := One() + One();\nEND_PROGRAM\n",
     "main", "out", 1, 2},
    {"function-input-copy-in",
     "FUNCTION Rewrite : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "X := 99; Rewrite := X;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR A : DINT := 7; Out : DINT; END_VAR\n"
     "Out := Rewrite(A) + A;\nEND_PROGRAM\n",
     "main", "out", 1, 106},
    {"function-return",
     "FUNCTION Sign : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "IF X < 0 THEN Sign := -1; RETURN; END_IF; Sign := 1;\n"
     "END_FUNCTION\nPROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := Sign(-8);\nEND_PROGRAM\n",
     "main", "out", 1, -1},
    {"function-named-arguments",
     "FUNCTION UserSub : DINT\nVAR_INPUT A : DINT; B : DINT; END_VAR\n"
     "UserSub := A - B;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := UserSub(B := 3, A := 10);\nEND_PROGRAM\n",
     "main", "out", 1, 7},
    {"function-case-insensitive-call",
     "FUNCTION MiXeD : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "mixed := X + 4;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := MIXED(6);\nEND_PROGRAM\n",
     "MAIN", "OUT", 1, 10},
    {"fb-output",
     "FUNCTION_BLOCK Echo\nVAR_INPUT X : DINT; END_VAR\n"
     "VAR_OUTPUT Q : DINT; END_VAR\nQ := X;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR E : Echo; Out : DINT; END_VAR\n"
     "E(X := 11); Out := E.Q;\nEND_PROGRAM\n",
     "main", "out", 1, 11},
    {"fb-persistent-state",
     "FUNCTION_BLOCK Counter\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "VAR N : DINT; END_VAR\nN := N + 1; Q := N;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR C : Counter; Out : DINT; END_VAR\n"
     "C(); Out := C.Q;\nEND_PROGRAM\n",
     "main", "out", 3, 3},
    {"fb-independent-instance-left",
     "FUNCTION_BLOCK Counter\nVAR_INPUT Step : DINT; END_VAR\n"
     "VAR_OUTPUT Q : DINT; END_VAR\nVAR N : DINT; END_VAR\n"
     "N := N + Step; Q := N;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR A : Counter; B : Counter; Out : DINT; END_VAR\n"
     "A(Step := 1); B(Step := 10); Out := A.Q;\nEND_PROGRAM\n",
     "main", "out", 2, 2},
    {"fb-independent-instance-right",
     "FUNCTION_BLOCK Counter\nVAR_INPUT Step : DINT; END_VAR\n"
     "VAR_OUTPUT Q : DINT; END_VAR\nVAR N : DINT; END_VAR\n"
     "N := N + Step; Q := N;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR A : Counter; B : Counter; Out : DINT; END_VAR\n"
     "A(Step := 1); B(Step := 10); Out := B.Q;\nEND_PROGRAM\n",
     "main", "out", 2, 20},
    {"fb-temp-zero-each-call",
     "FUNCTION_BLOCK TempUse\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "VAR N : DINT; END_VAR\nVAR_TEMP T : DINT; END_VAR\n"
     "T := T + 1; N := N + T; Q := N;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR F : TempUse; Out : DINT; END_VAR\n"
     "F(); Out := F.Q;\nEND_PROGRAM\n",
     "main", "out", 3, 3},
    {"fb-return-copy-out",
     "FUNCTION_BLOCK Early\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "Q := 8; RETURN; Q := 9;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR F : Early; Out : DINT; END_VAR\n"
     "F(Q => Out);\nEND_PROGRAM\n",
     "main", "out", 1, 8},
    {"fb-nested-instance",
     "FUNCTION_BLOCK Child\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "VAR N : DINT; END_VAR\nN := N + 1; Q := N;\nEND_FUNCTION_BLOCK\n"
     "FUNCTION_BLOCK Parent\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "VAR C : Child; END_VAR\nC(); Q := C.Q;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR P : Parent; Out : DINT; END_VAR\n"
     "P(); Out := P.Q;\nEND_PROGRAM\n",
     "main", "out", 2, 2},
    {"program-select-first",
     "PROGRAM First\nVAR Out : DINT; END_VAR\nOut := 1;\nEND_PROGRAM\n"
     "PROGRAM Second\nVAR Out : DINT; END_VAR\nOut := 2;\nEND_PROGRAM\n",
     "first", "out", 1, 1},
    {"program-select-second",
     "PROGRAM First\nVAR Out : DINT; END_VAR\nOut := 1;\nEND_PROGRAM\n"
     "PROGRAM Second\nVAR Out : DINT; END_VAR\nOut := 2;\nEND_PROGRAM\n",
     "second", "out", 1, 2},
    {"inout-direct-reference",
     "FUNCTION Bump : DINT\nVAR_IN_OUT X : DINT; END_VAR\n"
     "X := X + 1; Bump := X;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR A : DINT := 4; Out : DINT; END_VAR\n"
     "Out := Bump(A) + A;\nEND_PROGRAM\n",
     "main", "out", 1, 10},
    {"output-may-be-omitted",
     "FUNCTION_BLOCK Split\nVAR_INPUT X : DINT; END_VAR\n"
     "VAR_OUTPUT A : DINT; B : DINT; END_VAR\nA := X; B := X + 1;\n"
     "END_FUNCTION_BLOCK\nPROGRAM Main\n"
     "VAR F : Split; Out : DINT; END_VAR\n"
     "F(X := 5, B => Out);\nEND_PROGRAM\n",
     "main", "out", 1, 6},
    {"output-copy-back-declaration-order",
     "FUNCTION_BLOCK Pair\nVAR_OUTPUT A : DINT; B : DINT; END_VAR\n"
     "A := 1; B := 2;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR F : Pair; X : DINT; Out : DINT; END_VAR\n"
     "F(A => X, B => Out); Out := Out * 10 + X;\nEND_PROGRAM\n",
     "main", "out", 1, 21},
    {"en-false-function-default",
     "FUNCTION Value : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "Value := X + 1;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT := 9; Eno : BOOL := TRUE; END_VAR\n"
     "Out := Value(EN := FALSE, X := 4, ENO => Eno); "
     "Out := Out + BOOL_TO_DINT(Eno) * 100;\nEND_PROGRAM\n",
     "main", "out", 1, 0},
    {"en-true-function",
     "FUNCTION Value : DINT\nVAR_INPUT X : DINT; END_VAR\n"
     "Value := X + 1;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; Eno : BOOL; END_VAR\n"
     "Out := Value(EN := TRUE, X := 4, ENO => Eno);\n"
     "Out := Out + BOOL_TO_DINT(Eno);\nEND_PROGRAM\n",
     "main", "out", 1, 6},
    {"en-false-fb-holds-state",
     "FUNCTION_BLOCK Counter\nVAR_OUTPUT Q : DINT; END_VAR\n"
     "VAR N : DINT; END_VAR\nN := N + 1; Q := N;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR F : Counter; N : DINT; Out : DINT; E : BOOL; "
     "END_VAR\nN := N + 1; F(EN := N = 1, ENO => E); Out := F.Q;\n"
     "END_PROGRAM\n",
     "main", "out", 2, 1},
    {"en-true-fb-sets-eno",
     "FUNCTION_BLOCK Echo\nVAR_INPUT X : DINT; END_VAR\n"
     "VAR_OUTPUT Q : DINT; END_VAR\nQ := X;\nEND_FUNCTION_BLOCK\n"
     "PROGRAM Main\nVAR F : Echo; E : BOOL; Out : DINT; END_VAR\n"
     "F(EN := TRUE, X := 5, ENO => E); "
     "Out := F.Q + BOOL_TO_DINT(E);\nEND_PROGRAM\n",
     "main", "out", 1, 6},
    {"external-read",
     "VAR_GLOBAL Shared : DINT := 13; END_VAR\n"
     "FUNCTION ReadShared : DINT\nVAR_EXTERNAL Shared : DINT; END_VAR\n"
     "ReadShared := Shared;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := ReadShared();\nEND_PROGRAM\n",
     "main", "out", 1, 13},
    {"external-write",
     "VAR_GLOBAL Shared : DINT; END_VAR\n"
     "FUNCTION BumpShared : DINT\nVAR_EXTERNAL Shared : DINT; END_VAR\n"
     "Shared := Shared + 1; BumpShared := Shared;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := BumpShared() + BumpShared();\nEND_PROGRAM\n",
     "main", "out", 1, 3},
    {"left-to-right-positional",
     "FUNCTION Bump : DINT\nVAR_IN_OUT X : DINT; END_VAR\n"
     "X := X + 1; Bump := X;\nEND_FUNCTION\n"
     "FUNCTION Pack : DINT\nVAR_INPUT A : DINT; B : DINT; END_VAR\n"
     "Pack := A * 10 + B;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
     "Out := Pack(Bump(X), Bump(X));\nEND_PROGRAM\n",
     "main", "out", 1, 12},
    {"left-to-right-named",
     "FUNCTION Bump : DINT\nVAR_IN_OUT X : DINT; END_VAR\n"
     "X := X + 1; Bump := X;\nEND_FUNCTION\n"
     "FUNCTION Pack : DINT\nVAR_INPUT A : DINT; B : DINT; END_VAR\n"
     "Pack := A * 10 + B;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
     "Out := Pack(B := Bump(X), A := Bump(X));\nEND_PROGRAM\n",
     "main", "out", 1, 21},
    {"function-bool-return",
     "FUNCTION IsPositive : BOOL\nVAR_INPUT X : DINT; END_VAR\n"
     "IsPositive := X > 0;\nEND_FUNCTION\n"
     "PROGRAM Main\nVAR Out : BOOL; END_VAR\n"
     "Out := IsPositive(3);\nEND_PROGRAM\n",
     "main", "out", 1, 1},
    {"function-enum-return",
     "TYPE Mode : (idle, run); END_TYPE\n"
     "FUNCTION Choose : Mode\nVAR_INPUT Go : BOOL; END_VAR\n"
     "IF Go THEN Choose := Mode#run; ELSE Choose := Mode#idle; END_IF;\n"
     "END_FUNCTION\nPROGRAM Main\nVAR Out : DINT; END_VAR\n"
     "Out := MODE_TO_DINT(Choose(TRUE));\nEND_PROGRAM\n",
     "main", "out", 1, 1},
    {"function-subrange-input",
     "TYPE Percent : INT (0..100); END_TYPE\n"
     "FUNCTION AsInt : INT\nVAR_INPUT X : Percent; END_VAR\nAsInt := X;\n"
     "END_FUNCTION\nPROGRAM Main\nVAR P : Percent := 77; Out : INT; END_VAR\n"
     "Out := AsInt(P);\nEND_PROGRAM\n",
     "main", "out", 1, 77},
    {"program-state-persists",
     "PROGRAM Main\nVAR N : DINT; Out : DINT; END_VAR\n"
     "N := N + 1; Out := N;\nEND_PROGRAM\n",
     "main", "out", 4, 4},
    {"unselected-program-does-not-run",
     "VAR_GLOBAL Shared : DINT; END_VAR\n"
     "PROGRAM Writer\nShared := 99;\nEND_PROGRAM\n"
     "PROGRAM Main\nVAR_EXTERNAL Shared : DINT; END_VAR\n"
     "VAR Out : DINT; END_VAR\nOut := Shared;\nEND_PROGRAM\n",
     "main", "out", 1, 0},
};

void golden_programs()
{
    int programs = 0;
    for(const Golden &test : kGolden) {
        ++programs;
        Rig rig;
        if(!rig.build(test.source, test.program)) {
            fail(test.name);
            continue;
        }
        bool scanned = true;
        for(int n = 0; n < test.scans; ++n) {
            if(rig.scan() != st::ScanError::ok) {
                scanned = false;
                break;
            }
        }
        if(!scanned || rig.i64(test.result) != test.expected) {
            std::printf("  %s got %lld want %lld\n", test.name,
                        static_cast<long long>(rig.i64(test.result)),
                        test.expected);
            fail(test.name);
        }
    }
    check(programs >= 30, "L2b-A01 golden program floor");
}

void structured_call_positions_and_return()
{
    const char *bump =
        "FUNCTION Bump : DINT\nVAR_IN_OUT X : DINT; END_VAR\n"
        "X := X + 1; Bump := X;\nEND_FUNCTION\n";

    Rig rig;
    check(rig.build(
              std::string(bump) +
              "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
              "IF FALSE THEN Out := Bump(X); END_IF; Out := Out + X;\n"
              "END_PROGRAM\n") &&
              rig.scan() == st::ScanError::ok &&
              rig.i64("out") == 0 && rig.i64("x") == 0,
          "L2b structured expansion keeps calls inside false branch");

    check(rig.build(
              std::string(bump) +
              "PROGRAM Main\nVAR X : DINT; I : DINT; Out : DINT; END_VAR\n"
              "FOR I := 1 TO 3 DO Out := Out + Bump(X); END_FOR;\n"
              "END_PROGRAM\n") &&
              rig.scan() == st::ScanError::ok &&
              rig.i64("x") == 3 && rig.i64("out") == 6,
          "L2b structured expansion executes loop-body call each iteration");

    check(rig.build(
              std::string(bump) +
              "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
              "WHILE Bump(X) < 3 DO Out := Out + 10; END_WHILE; "
              "Out := Out + X;\nEND_PROGRAM\n") &&
              rig.scan() == st::ScanError::ok &&
              rig.i64("x") == 3 && rig.i64("out") == 23,
          "L2b structured expansion reevaluates loop-condition call");

    check(rig.build(
              "FUNCTION FirstTwo : DINT\nVAR I : DINT; END_VAR\n"
              "FOR I := 1 TO 3 DO FirstTwo := I; "
              "IF I = 2 THEN RETURN; END_IF; END_FOR; FirstTwo := 99;\n"
              "END_FUNCTION\nPROGRAM Main\nVAR Out : DINT; END_VAR\n"
              "Out := FirstTwo();\nEND_PROGRAM\n") &&
              rig.scan() == st::ScanError::ok &&
              rig.i64("out") == 2,
          "L2b RETURN exits nested structured control flow");
}

void entry_bound_inout()
{
    Rig inout;
    check(inout.build(
              "TYPE Values : ARRAY[0..1] OF DINT; END_TYPE\n"
              "FUNCTION Move : DINT\nVAR_IN_OUT X : DINT; I : DINT; "
              "END_VAR\nI := 1; X := 7; Move := X;\nEND_FUNCTION\n"
              "PROGRAM Main\nVAR V : Values; I : DINT; Out : DINT; END_VAR\n"
              "V[0] := 3; V[1] := 4; Out := Move(X := V[I], I := I); "
              "Out := Out * 100 + V[0] * 10 + V[1];\nEND_PROGRAM\n") &&
              inout.scan() == st::ScanError::ok &&
              inout.i64("out") == 774,
          "L2b IN_OUT dynamic lvalue is bound once at call entry");

}

void atomic_output_commit()
{
    const std::string source =
        "FUNCTION_BLOCK Pair\nVAR_OUTPUT A : DINT; B : DINT; END_VAR\n"
        "A := 1; B := 2;\nEND_FUNCTION_BLOCK\n"
        "PROGRAM Main\nVAR F : Pair; X : DINT := 7; Y : DINT := 8; "
        "END_VAR\nF(A => X, B => Y);\nEND_PROGRAM\n";
    std::int64_t exact_budget = 0;
    for(std::int64_t budget = 1; budget <= 256; ++budget) {
        Rig probe;
        if(probe.build(source) && probe.scan(budget) == st::ScanError::ok) {
            exact_budget = budget;
            check(probe.i64("x") == 1 && probe.i64("y") == 2,
                  "L2b successful OUTPUT commit publishes all outputs");
            break;
        }
    }
    check(exact_budget > 1, "L2b OUTPUT exact budget discovered");
    if(exact_budget > 1) {
        Rig under;
        check(under.build(source) &&
                  under.scan(exact_budget - 1) ==
                      st::ScanError::budget_exceeded &&
                  under.i64("x") == 7 && under.i64("y") == 8,
              "L2b budget fault commits no partial OUTPUT values");
    }
}

void nested_instance_paths_do_not_collide()
{
    Rig rig;
    check(rig.build(
              "FUNCTION_BLOCK Leaf\nVAR_INPUT Step : DINT; END_VAR\n"
              "VAR_OUTPUT Q : DINT; END_VAR\nVAR N : DINT; END_VAR\n"
              "N := N + Step; Q := N;\nEND_FUNCTION_BLOCK\n"
              "FUNCTION_BLOCK Left\nVAR_OUTPUT Q : DINT; END_VAR\n"
              "VAR BC : Leaf; END_VAR\nBC(Step := 1); Q := BC.Q;\n"
              "END_FUNCTION_BLOCK\n"
              "FUNCTION_BLOCK Right\nVAR_OUTPUT Q : DINT; END_VAR\n"
              "VAR C : Leaf; END_VAR\nC(Step := 10); Q := C.Q;\n"
              "END_FUNCTION_BLOCK\n"
              "PROGRAM Main\nVAR A : Left; AB : Right; Out : DINT; "
              "END_VAR\nA(); AB(); Out := A.Q * 100 + AB.Q;\n"
              "END_PROGRAM\n") &&
              rig.scan() == st::ScanError::ok &&
              rig.scan() == st::ScanError::ok &&
              rig.i64("out") == 220,
          "L2b A.BC and AB.C nested instance paths remain independent");
}

// L2b-A02: successful and early returns copy OUTPUT back.  A scan fault does
// not copy OUTPUT back, while direct IN_OUT writes and the FB's own state are
// observable at the fault point.
void copy_back_and_faults()
{
    Rig output_fault;
    check(output_fault.build(
              "FUNCTION_BLOCK Faulty\nVAR_INPUT D : DINT; END_VAR\n"
              "VAR_OUTPUT Q : DINT; END_VAR\nQ := 7; Q := 10 / D;\n"
              "END_FUNCTION_BLOCK\nPROGRAM Main\n"
              "VAR F : Faulty; Out : DINT := 3; Eno : BOOL := TRUE; "
              "END_VAR\nF(D := 0, Q => Out, ENO => Eno);\nEND_PROGRAM\n"),
          "L2b-A02 faulting OUTPUT project builds");
    check(output_fault.scan() == st::ScanError::division_by_zero,
          "L2b-A02 callee fault propagates");
    check(output_fault.i64("out") == 3,
          "L2b-A02 fault suppresses OUTPUT copy-back");
    check(output_fault.i64("f.q") == 7,
          "L2b-A02 FB output state before fault persists");
    check(output_fault.i64("eno") == 0,
          "L2b-A04 fault publishes ENO false");

    Rig inout_fault;
    check(inout_fault.build(
              "FUNCTION TouchThenFault : DINT\nVAR_INPUT D : DINT; END_VAR\n"
              "VAR_IN_OUT X : DINT; END_VAR\n"
              "X := X + 1; TouchThenFault := 10 / D;\nEND_FUNCTION\n"
              "PROGRAM Main\nVAR A : DINT := 4; Out : DINT; END_VAR\n"
              "Out := TouchThenFault(D := 0, X := A);\nEND_PROGRAM\n"),
          "L2b-A02 faulting IN_OUT project builds");
    check(inout_fault.scan() == st::ScanError::division_by_zero,
          "L2b-A02 IN_OUT callee faults");
    check(inout_fault.i64("a") == 5,
          "L2b-A02 IN_OUT writes are not rolled back");

    Rig budget_fault;
    check(budget_fault.build(
              "FUNCTION_BLOCK Slow\nVAR_OUTPUT Q : DINT; END_VAR\n"
              "VAR I : DINT; END_VAR\nQ := 9; WHILE TRUE DO I := I + 1; "
              "END_WHILE;\nEND_FUNCTION_BLOCK\n"
              "PROGRAM Main\nVAR F : Slow; Out : DINT := 2; END_VAR\n"
              "F(Q => Out);\nEND_PROGRAM\n"),
          "L2b-A02 budget-fault project builds");
    check(budget_fault.scan(64) == st::ScanError::budget_exceeded,
          "L2b-A02 budget fault propagates");
    check(budget_fault.i64("out") == 2,
          "L2b-A02 budget fault suppresses OUTPUT copy-back");
}

// L2b-A03: statically known overlap is a semantic error; overlap that depends
// on runtime indexes faults before entering the callee and latches.
void alias_contract()
{
    const st::CompileResult static_inout = st::compile(
        "FUNCTION SwapLike : DINT\nVAR_IN_OUT A : DINT; B : DINT; END_VAR\n"
        "A := A + 1; B := B + 1; SwapLike := A + B;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
        "Out := SwapLike(A := X, B := X);\nEND_PROGRAM\n");
    check(!static_inout.ok &&
              has_code(static_inout, st::DiagCode::sema_alias_violation),
          "L2b-A03 static IN_OUT alias rejected");

    const st::CompileResult static_output = st::compile(
        "FUNCTION_BLOCK Pair\nVAR_OUTPUT A : DINT; B : DINT; END_VAR\n"
        "A := 1; B := 2;\nEND_FUNCTION_BLOCK\n"
        "PROGRAM Main\nVAR F : Pair; X : DINT; END_VAR\n"
        "F(A => X, B => X);\nEND_PROGRAM\n");
    check(!static_output.ok &&
              has_code(static_output, st::DiagCode::sema_alias_violation),
          "L2b-A03 static OUTPUT alias rejected");

    Rig dynamic;
    check(dynamic.build(
              "TYPE Values : ARRAY[0..1] OF DINT; END_TYPE\n"
              "FUNCTION AddBoth : DINT\nVAR_IN_OUT A : DINT; B : DINT; "
              "END_VAR\nA := A + 1; B := B + 1; AddBoth := A + B;\n"
              "END_FUNCTION\nPROGRAM Main\n"
              "VAR V : Values; I : DINT := 1; Out : DINT; END_VAR\n"
              "Out := AddBoth(A := V[1], B := V[I]);\nEND_PROGRAM\n"),
          "L2b-A03 dynamic-alias project builds");
    check(dynamic.scan() == st::ScanError::alias_violation,
          "L2b-A03 dynamic alias faults");
    check(dynamic.instance.fault() == st::ScanError::alias_violation,
          "L2b-A03 dynamic alias fault latches");
}

// L2b-D11/D13/D16: IN_OUT requires an exact-type writable lvalue; calls have
// no defaults, cannot mix positional/named syntax, and reject duplicate pins.
void call_rejections()
{
    const char *function =
        "FUNCTION Use : DINT\nVAR_INPUT A : DINT; END_VAR\n"
        "VAR_IN_OUT X : DINT; END_VAR\nUse := A + X;\nEND_FUNCTION\n";
    const st::CompileResult literal = st::compile(
        (std::string(function) +
         "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
         "Out := Use(A := 1, X := 2);\nEND_PROGRAM\n")
            .c_str());
    check(!literal.ok && has_code(literal,
                                  st::DiagCode::sema_inout_requires_lvalue),
          "L2b-D11 IN_OUT literal rejected");

    const st::CompileResult wrong_type = st::compile(
        (std::string(function) +
         "PROGRAM Main\nVAR X : INT; Out : DINT; END_VAR\n"
         "Out := Use(A := 1, X := X);\nEND_PROGRAM\n")
            .c_str());
    check(!wrong_type.ok &&
              has_code(wrong_type, st::DiagCode::sema_type_mismatch),
          "L2b-D11 IN_OUT exact type enforced");

    const st::CompileResult missing = st::compile(
        (std::string(function) +
         "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
         "Out := Use(X := X);\nEND_PROGRAM\n")
            .c_str());
    check(!missing.ok &&
              has_code(missing, st::DiagCode::sema_missing_argument),
          "L2b-D16 required INPUT diagnosed");

    const st::CompileResult duplicate = st::compile(
        (std::string(function) +
         "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
         "Out := Use(A := 1, A := 2, X := X);\nEND_PROGRAM\n")
            .c_str());
    check(!duplicate.ok &&
              has_code(duplicate, st::DiagCode::sema_duplicate_argument),
          "L2b-D16 duplicate actual diagnosed");

    const st::CompileResult mixed = st::compile(
        (std::string(function) +
         "PROGRAM Main\nVAR X : DINT; Out : DINT; END_VAR\n"
         "Out := Use(1, X := X);\nEND_PROGRAM\n")
            .c_str());
    check(!mixed.ok && has_code(mixed, st::DiagCode::sema_call_form_mixed),
          "L2b-D13 positional and named actuals cannot mix");
}

// L2b-D02/D05-D08: names and calls are project-level, case-insensitive and
// statically resolved.  No implicit global capture or object extensions.
void scope_and_rejection_contract()
{
    Rig local_priority;
    check(local_priority.build(
              "VAR_GLOBAL Value : DINT := 1; END_VAR\n"
              "FUNCTION ReadLocal : DINT\nVAR Value : DINT := 2; END_VAR\n"
              "ReadLocal := Value;\nEND_FUNCTION\n"
              "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
              "Out := ReadLocal();\nEND_PROGRAM\n") &&
              local_priority.scan() == st::ScanError::ok &&
              local_priority.i64("out") == 2,
          "L2b-D05 local symbol wins over project global");

    const st::CompileResult leaked_local = st::compile(
        "FUNCTION Owner : DINT\nVAR Secret : DINT; END_VAR\n"
        "Owner := Secret;\nEND_FUNCTION\n"
        "FUNCTION Intruder : DINT\nIntruder := Secret;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!leaked_local.ok &&
              has_code(leaked_local, st::DiagCode::sema_unknown_identifier),
          "L2b-D06 local symbol does not cross POU boundary");

    const st::CompileResult duplicate_pou = st::compile(
        "FUNCTION Calc : DINT\nCalc := 1;\nEND_FUNCTION\n"
        "FUNCTION cAlC : DINT\ncalc := 2;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!duplicate_pou.ok &&
              has_code(duplicate_pou, st::DiagCode::sema_duplicate_pou),
          "L2b-D06 POU names are case-insensitively unique");

    const st::CompileResult capture = st::compile(
        "VAR_GLOBAL Shared : DINT; END_VAR\n"
        "FUNCTION ReadIt : DINT\nReadIt := Shared;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!capture.ok &&
              has_code(capture, st::DiagCode::sema_external_required),
          "L2b-D05 implicit global capture rejected");

    const st::CompileResult missing_external = st::compile(
        "FUNCTION ReadIt : DINT\nVAR_EXTERNAL Missing : DINT; END_VAR\n"
        "ReadIt := Missing;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n");
    check(!missing_external.ok &&
              has_code(missing_external,
                       st::DiagCode::sema_external_not_unique),
          "L2b-D05 unmatched external rejected");

    const st::CompileResult direct_fb = st::compile(
        "FUNCTION_BLOCK Counter\nEND_FUNCTION_BLOCK\n"
        "PROGRAM Main\nCounter();\nEND_PROGRAM\n");
    check(!direct_fb.ok &&
              has_code(direct_fb, st::DiagCode::sema_fb_instance_required),
          "L2b-D02 FB type cannot be called directly");

    const st::CompileResult standard_shadow = st::compile(
        "FUNCTION INT_TO_DINT : DINT\nVAR_INPUT X : INT; END_VAR\n"
        "INT_TO_DINT := X;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n");
    check(!standard_shadow.ok &&
              has_code(standard_shadow,
                       st::DiagCode::sema_standard_name_conflict),
          "L2b-D08 standard function name cannot be shadowed");

    const char *extensions[] = {
        "FUNCTION_BLOCK B\nMETHOD M : DINT\nEND_METHOD\nEND_FUNCTION_BLOCK\n",
        "INTERFACE I\nEND_INTERFACE\nPROGRAM Main\nEND_PROGRAM\n",
        "FUNCTION_BLOCK Child EXTENDS Parent\nEND_FUNCTION_BLOCK\n",
        "FUNCTION Generic<T> : T\nEND_FUNCTION\n",
        "TYPE P : REF_TO DINT; END_TYPE\nPROGRAM Main\nEND_PROGRAM\n",
    };
    for(const char *source : extensions) {
        const st::CompileResult result = st::compile(source);
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::unsupported_l2b_object_extension),
              "L2b object extension rejected with stable diagnostic");
    }
}

void external_type_and_standard_name_contract()
{
    const st::CompileResult duplicate_global = st::compile(
        "VAR_GLOBAL Shared : DINT; sHaReD : DINT; END_VAR\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!duplicate_global.ok &&
              has_code(duplicate_global,
                       st::DiagCode::sema_external_not_unique),
          "L2b-D05 duplicate global name rejected");

    const char *exact =
        "TYPE Mode : (Off, On); END_TYPE\n"
        "TYPE Percent : INT (0..100); END_TYPE\n"
        "TYPE Values : ARRAY[0..2] OF DINT; END_TYPE\n"
        "TYPE Packet : STRUCT Flag : BOOL; Values : Values; END_STRUCT "
        "END_TYPE\n"
        "VAR_GLOBAL E : Mode; R : Percent; A : Values; P : Packet; "
        "S : STRING[7]; W : WSTRING[5]; END_VAR\n"
        "FUNCTION Read : DINT\nVAR_EXTERNAL E : Mode; R : Percent; "
        "A : Values; P : Packet; S : STRING[7]; W : WSTRING[5]; "
        "END_VAR\nRead := 1;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n";
    check(st::compile(exact).ok,
          "L2b-D05 exact external nominal and descriptor types accepted");

    const st::CompileResult aliases = st::compile(
        "VAR_GLOBAL T : TOD; D : DT; S : STRING; W : WSTRING; END_VAR\n"
        "FUNCTION ReadAliases : DINT\nVAR_EXTERNAL T : TIME_OF_DAY; "
        "D : DATE_AND_TIME; S : STRING[80]; W : WSTRING[80]; END_VAR\n"
        "ReadAliases := 1;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n");
    check(aliases.ok,
          "L2b-D05 external exactness follows TypeId and string descriptor");

    const char *mismatches[] = {
        "TYPE Mode : (Off, On); END_TYPE\nVAR_GLOBAL X : Mode; END_VAR\n"
        "FUNCTION F : DINT\nVAR_EXTERNAL X : DINT; END_VAR\nF := 0;\n"
        "END_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n",
        "TYPE Percent : INT (0..100); END_TYPE\nVAR_GLOBAL X : Percent; "
        "END_VAR\nFUNCTION F : DINT\nVAR_EXTERNAL X : INT; END_VAR\n"
        "F := 0;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n",
        "TYPE A : ARRAY[0..2] OF DINT; END_TYPE\n"
        "TYPE B : ARRAY[0..2] OF DINT; END_TYPE\nVAR_GLOBAL X : A; "
        "END_VAR\nFUNCTION F : DINT\nVAR_EXTERNAL X : B; END_VAR\n"
        "F := 0;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n",
        "TYPE A : STRUCT X : DINT; END_STRUCT END_TYPE\n"
        "TYPE B : STRUCT X : DINT; END_STRUCT END_TYPE\n"
        "VAR_GLOBAL X : A; END_VAR\nFUNCTION F : DINT\n"
        "VAR_EXTERNAL X : B; END_VAR\nF := 0;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n",
        "VAR_GLOBAL X : STRING[7]; END_VAR\nFUNCTION F : DINT\n"
        "VAR_EXTERNAL X : STRING[8]; END_VAR\nF := 0;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n",
        "VAR_GLOBAL X : WSTRING[5]; END_VAR\nFUNCTION F : DINT\n"
        "VAR_EXTERNAL X : STRING[5]; END_VAR\nF := 0;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n",
    };
    for(const char *source : mismatches) {
        const st::CompileResult result = st::compile(source);
        check(!result.ok &&
                  has_code(result, st::DiagCode::sema_type_mismatch),
              "L2b-D05 external declaration type is exact");
    }

    const char *reserved[] = {"INT_TO_DINT", "LEN", "ABS",
                              "CONCAT_DATE_TOD"};
    for(const char *name : reserved) {
        const std::string source =
            "FUNCTION " + std::string(name) +
            " : DINT\n" + name +
            " := 0;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n";
        const st::CompileResult result = st::compile(source);
        check(!result.ok &&
                  has_code(result,
                           st::DiagCode::sema_standard_name_conflict),
              "L2b-D08 all standard function names are reserved");
    }
}

std::string call_chain(int depth)
{
    std::string source;
    for(int i = 0; i < depth; ++i) {
        source += "FUNCTION F" + std::to_string(i) + " : DINT\n";
        if(i + 1 < depth) {
            source += "F" + std::to_string(i) + " := F" +
                      std::to_string(i + 1) + "();\n";
        } else {
            source += "F" + std::to_string(i) + " := 1;\n";
        }
        source += "END_FUNCTION\n";
    }
    source += "PROGRAM Main\nVAR Out : DINT; END_VAR\nOut := F0();\n"
              "END_PROGRAM\n";
    return source;
}

std::string instance_chain(int depth)
{
    std::string source =
        "FUNCTION_BLOCK B0\nVAR_OUTPUT Q : DINT; END_VAR\nQ := 1;\n"
        "END_FUNCTION_BLOCK\n";
    for(int i = 1; i < depth; ++i) {
        source += "FUNCTION_BLOCK B" + std::to_string(i) + "\nVAR C : B" +
                  std::to_string(i - 1) +
                  "; END_VAR\nVAR_OUTPUT Q : DINT; END_VAR\n"
                  "C(); Q := C.Q;\nEND_FUNCTION_BLOCK\n";
    }
    source += "PROGRAM Main\nVAR Root : B" + std::to_string(depth - 1) +
              "; Out : DINT; END_VAR\nRoot(); Out := Root.Q;\nEND_PROGRAM\n";
    return source;
}

// L2b-A05: recursion and each loading-domain resource have stable bounds.
void graph_and_capacity_contract()
{
    const st::CompileResult direct = st::compile(
        "FUNCTION Again : DINT\nAgain := Again();\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!direct.ok &&
              has_code(direct, st::DiagCode::sema_recursive_pou),
          "L2b-A05 direct recursion rejected");

    const st::CompileResult indirect = st::compile(
        "FUNCTION A : DINT\nA := B();\nEND_FUNCTION\n"
        "FUNCTION B : DINT\nB := C();\nEND_FUNCTION\n"
        "FUNCTION C : DINT\nC := A();\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!indirect.ok &&
              has_code(indirect, st::DiagCode::sema_recursive_pou),
          "L2b-A05 indirect recursion rejected");

    const st::CompileResult self_instance = st::compile(
        "FUNCTION_BLOCK Node\nVAR Next : Node; END_VAR\n"
        "END_FUNCTION_BLOCK\nPROGRAM Main\nEND_PROGRAM\n");
    check(!self_instance.ok &&
              has_code(self_instance, st::DiagCode::sema_recursive_pou),
          "L2b-A05 FB self-containment rejected");

    st::CompileOptions options;
    options.max_pous = 1;
    const st::CompileResult pous = st::compile(
        "FUNCTION F : DINT\nF := 1;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n",
        options);
    check(!pous.ok && has_code(pous, st::DiagCode::capacity_exceeded),
          "L2b-A05 POU count bounded");

    options = st::CompileOptions{};
    options.max_parameters_per_pou = 2;
    const st::CompileResult parameters = st::compile(
        "FUNCTION F : DINT\nVAR_INPUT A : DINT; B : DINT; C : DINT; "
        "END_VAR\nF := A + B + C;\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n",
        options);
    check(!parameters.ok &&
              has_code(parameters, st::DiagCode::capacity_exceeded),
          "L2b-A05 parameter count bounded");

    options = st::CompileOptions{};
    options.max_call_depth = 4;
    check(st::compile(call_chain(3), options).ok,
          "L2b-A05 call depth N accepted");
    const st::CompileResult too_deep_call =
        st::compile(call_chain(4), options);
    check(!too_deep_call.ok &&
              has_code(too_deep_call, st::DiagCode::capacity_exceeded),
          "L2b-A05 call depth N+1 rejected");

    options = st::CompileOptions{};
    options.max_instance_depth = 4;
    check(st::compile(instance_chain(3), options).ok,
          "L2b-A05 instance depth N accepted");
    const st::CompileResult too_deep_instance =
        st::compile(instance_chain(4), options);
    check(!too_deep_instance.ok &&
              has_code(too_deep_instance, st::DiagCode::capacity_exceeded),
          "L2b-A05 instance depth N+1 rejected");

    options = st::CompileOptions{};
    options.max_vars_bytes = 8;
    const st::CompileResult frame_bytes = st::compile(
        "FUNCTION F : DINT\nVAR A : DINT; B : DINT; END_VAR\n"
        "F := A + B;\nEND_FUNCTION\nPROGRAM Main\nEND_PROGRAM\n",
        options);
    check(!frame_bytes.ok &&
              has_code(frame_bytes, st::DiagCode::capacity_exceeded),
          "L2b-A05 aggregate frame layout overflow rejected");
}

// L2b-A06 plus the reporting clause: reordering independent compilation units
// must not change canonical bytecode/layout, and the artifact reports bounded
// frames, instance trees, call depth and worst-case instruction count.
void deterministic_manifest_and_reports()
{
    const char *first =
        "FUNCTION A : DINT\nVAR_INPUT X : DINT; END_VAR\nA := X + 1;\n"
        "END_FUNCTION\n"
        "FUNCTION B : DINT\nVAR_INPUT X : DINT; END_VAR\nB := X * 2;\n"
        "END_FUNCTION\n"
        "PROGRAM Main\nVAR Out : DINT; END_VAR\nOut := B(A(4));\n"
        "END_PROGRAM\n";
    const char *permuted =
        "PROGRAM Main\nVAR Out : DINT; END_VAR\nOut := B(A(4));\n"
        "END_PROGRAM\n"
        "FUNCTION B : DINT\nVAR_INPUT X : DINT; END_VAR\nB := X * 2;\n"
        "END_FUNCTION\n"
        "FUNCTION A : DINT\nVAR_INPUT X : DINT; END_VAR\nA := X + 1;\n"
        "END_FUNCTION\n";
    const st::CompileResult left = st::compile(first);
    const st::CompileResult right = st::compile(permuted);
    check(left.ok && right.ok, "L2b-A06 permuted projects compile");
    if(left.ok && right.ok) {
        check(left.program.canonical_manifest() ==
                  right.program.canonical_manifest(),
              "L2b-A06 canonical manifest ignores unit order");
        check(left.program.max_call_depth == 2,
              "L2b report records maximum call depth");
        check(left.program.worst_case_instructions > 0,
              "L2b report records worst-case call instructions");
        check(left.program.pous.size() == 3,
              "L2b report enumerates every POU");
        for(const st::PouInfo &pou : left.program.pous) {
            check(pou.frame_bytes <= left.program.required_bytes(),
                  "L2b report bounds each POU frame");
            check(pou.worst_case_instructions > 0,
                  "L2b report bounds each POU instruction path");
        }
    }

    const st::CompileResult instances = st::compile(instance_chain(3));
    check(instances.ok, "L2b instance report project compiles");
    if(instances.ok) {
        check(instances.program.max_instance_depth == 4,
              "L2b report records PROGRAM-rooted instance depth");
        check(instances.program.instances.size() == 4,
              "L2b report enumerates PROGRAM and nested FB instances");
        std::uint64_t previous_end = 0;
        for(const st::InstanceInfo &instance : instances.program.instances) {
            check(instance.offset >= previous_end,
                  "L2b instance report is non-overlapping and ordered");
            previous_end = instance.offset + instance.bytes;
        }
        check(previous_end <= instances.program.required_bytes(),
              "L2b instance tree fits the load footprint");
    }
}

void complete_canonical_artifact()
{
    const char *ordered =
        "TYPE Small : DINT(0..10); END_TYPE\n"
        "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
        "Inc := X + 1;\nEND_FUNCTION\n"
        "PROGRAM Zeta\nVAR Edge : R_TRIG; Text : STRING[4]; "
        "Out : DINT; END_VAR\n"
        "Edge(CLK := TRUE); Text := 'x'; Out := Inc(2);\nEND_PROGRAM\n"
        "PROGRAM Alpha\nVAR Out : DINT := 4; END_VAR\n"
        "Out := Out + 1; Out := Out + 2;\nEND_PROGRAM\n";
    const char *permuted =
        "PROGRAM Alpha\nVAR Out : DINT := 4; END_VAR\n"
        "Out := Out + 1; Out := Out + 2;\nEND_PROGRAM\n"
        "PROGRAM Zeta\nVAR Edge : R_TRIG; Text : STRING[4]; "
        "Out : DINT; END_VAR\n"
        "Edge(CLK := TRUE); Text := 'x'; Out := Inc(2);\nEND_PROGRAM\n"
        "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
        "Inc := X + 1;\nEND_FUNCTION\n"
        "TYPE Small : DINT(0..10); END_TYPE\n";
    const st::CompileResult base = st::compile(ordered);
    const st::CompileResult reordered = st::compile(permuted);
    check(base.ok && reordered.ok, "L2b-A06 complete artifact fixtures compile");
    if(!base.ok || !reordered.ok) return;
    const std::string artifact = base.program.canonical_manifest();
    check(artifact == reordered.program.canonical_manifest(),
          "L2b-A06 complete artifact canonicalizes POU and PROGRAM order");
    check(artifact.size() > 20U && artifact[0] == 'L' &&
              artifact[1] == '2' && artifact[2] == 'B' &&
              artifact[3] == 'A' &&
              static_cast<unsigned char>(artifact[4]) ==
                  st::kCanonicalManifestVersion &&
              static_cast<unsigned char>(artifact[5]) == 0U &&
              static_cast<unsigned char>(artifact[6]) == 0U &&
              static_cast<unsigned char>(artifact[7]) == 0U,
          "L2b-A06 artifact uses versioned little-endian framing");

    const auto changed = [&artifact](const st::Program &candidate) {
        return candidate.canonical_manifest() != artifact;
    };
    st::Program mutation = base.program;
    mutation.pous[0].frame_bytes += 8;
    check(changed(mutation), "L2b-A06 artifact covers root POU records");
    mutation = base.program;
    mutation.pous[0].worst_case_bounded =
        !mutation.pous[0].worst_case_bounded;
    check(changed(mutation), "L2b-A06 artifact covers POU boundedness");
    mutation = base.program;
    ++mutation.pous[0].worst_case_instructions;
    check(changed(mutation), "L2b-A06 artifact covers POU WCI");
    mutation = base.program;
    mutation.instances[0].offset += 8;
    check(changed(mutation), "L2b-A06 artifact covers complete instance layout");
    mutation = base.program;
    mutation.layout_bytes += 8;
    check(changed(mutation), "L2b-A06 artifact covers layout bytes");
    mutation = base.program;
    ++mutation.max_call_depth;
    check(changed(mutation), "L2b-A06 artifact covers call depth");
    mutation = base.program;
    ++mutation.max_instance_depth;
    check(changed(mutation), "L2b-A06 artifact covers instance depth");
    mutation = base.program;
    ++mutation.worst_case_instructions;
    check(changed(mutation), "L2b-A06 artifact covers root WCI");
    mutation = base.program;
    mutation.worst_case_bounded = !mutation.worst_case_bounded;
    check(changed(mutation), "L2b-A06 artifact covers root WCI boundedness");

    const auto mutate_child = [&base, &changed](const char *name,
                                                void (*edit)(st::Program &)) {
        st::Program copy = base.program;
        edit(copy.programs[0]);
        check(changed(copy), name);
    };
    mutate_child("L2b-A06 artifact covers code", [](st::Program &p) {
        p.code[0] ^= 1U;
    });
    mutate_child("L2b-A06 artifact covers bytecode version", [](st::Program &p) {
        ++p.format_version;
    });
    mutate_child("L2b-A06 artifact covers constants", [](st::Program &p) {
        if(p.constants.empty()) p.constants.push_back(7);
        else ++p.constants[0];
    });
    mutate_child("L2b-A06 artifact covers string constants", [](st::Program &p) {
        if(p.string_constants.empty()) p.string_constants.push_back(1);
        else p.string_constants[0] ^= 1U;
    });
    mutate_child("L2b-A06 artifact covers TypeTable", [](st::Program &p) {
        st::TypeId ignored = st::invalid_type_id;
        (void)p.types.add_string("artifact_extra", 3, ignored);
    });
    mutate_child("L2b-A06 artifact covers vars", [](st::Program &p) {
        p.vars[0].offset += 8;
    });
    mutate_child("L2b-A06 artifact covers FBs", [](st::Program &p) {
        if(p.fbs.empty()) {
            st::FbInfo info;
            info.name = "probe";
            info.lower = "probe";
            p.fbs.push_back(info);
        } else {
            ++p.fbs[0].offset;
        }
    });
    mutate_child("L2b-A06 artifact covers initial data", [](st::Program &p) {
        p.initial_data[0] ^= 1U;
    });
    mutate_child("L2b-A06 artifact covers stack cost", [](st::Program &p) {
        ++p.stack_slots;
    });
    mutate_child("L2b-A06 artifact covers variable cost", [](st::Program &p) {
        ++p.vars_bytes;
    });
    mutate_child("L2b-A06 artifact covers FB cost", [](st::Program &p) {
        ++p.fb_bytes;
    });
    mutate_child("L2b-A06 artifact covers string cost", [](st::Program &p) {
        ++p.max_string_operation_cost;
    });
    mutate_child("L2b-A06 artifact covers PROGRAM name", [](st::Program &p) {
        p.program_name += "x";
    });
    mutate_child("L2b-A06 artifact covers PROGRAM WCI", [](st::Program &p) {
        ++p.worst_case_instructions;
    });
    mutate_child("L2b-A06 artifact covers PROGRAM WCI boundedness",
                 [](st::Program &p) {
                     p.worst_case_bounded = !p.worst_case_bounded;
                 });
    mutate_child("L2b-A06 artifact covers PROGRAM instance layout",
                 [](st::Program &p) {
                     st::InstanceInfo info;
                     info.name = "probe";
                     info.lower = "probe";
                     info.offset = 8;
                     info.bytes = 8;
                     p.instances.push_back(info);
                 });
    mutate_child("L2b-A06 artifact covers PROGRAM layout bytes",
                 [](st::Program &p) { p.layout_bytes += 8; });
    mutate_child("L2b-A06 artifact covers PROGRAM call depth",
                 [](st::Program &p) { ++p.max_call_depth; });
    mutate_child("L2b-A06 artifact covers PROGRAM instance depth",
                 [](st::Program &p) { ++p.max_instance_depth; });

    mutation = base.program;
    check(mutation.programs[0].constants.size() >= 2U,
          "L2b-A06 constant-order fixture has indexed entries");
    if(mutation.programs[0].constants.size() >= 2U) {
        std::swap(mutation.programs[0].constants[0],
                  mutation.programs[0].constants[1]);
        check(changed(mutation),
              "L2b-A06 indexed constant order is never canonical-sorted");
    }
    mutation = base.program;
    check(mutation.programs.size() >= 2U &&
              mutation.programs[1].vars.size() >= 2U,
          "L2b-A06 variable-order fixture has indexed entries");
    if(mutation.programs.size() >= 2U &&
       mutation.programs[1].vars.size() >= 2U) {
        std::swap(mutation.programs[1].vars[0], mutation.programs[1].vars[1]);
        check(changed(mutation),
              "L2b-A06 indexed variable order is never canonical-sorted");
    }
}

void source_order_determinism()
{
    const char *types_a =
        "TYPE Scalar : DINT(0..100); END_TYPE\n"
        "TYPE Values : ARRAY[0..1] OF Scalar; END_TYPE\n"
        "TYPE Packet : STRUCT Values : Values; Flag : BOOL; END_STRUCT "
        "END_TYPE\n"
        "TYPE Mode : (Idle, Run); END_TYPE\n";
    const char *types_b =
        "TYPE Packet : STRUCT Values : Values; Flag : BOOL; END_STRUCT "
        "END_TYPE\n"
        "TYPE Mode : (Idle, Run); END_TYPE\n"
        "TYPE Values : ARRAY[0..1] OF Scalar; END_TYPE\n"
        "TYPE Scalar : DINT(0..100); END_TYPE\n";
    const char *types_c =
        "TYPE Values : ARRAY[0..1] OF Scalar; END_TYPE\n"
        "TYPE Scalar : DINT(0..100); END_TYPE\n"
        "TYPE Mode : (Idle, Run); END_TYPE\n"
        "TYPE Packet : STRUCT Values : Values; Flag : BOOL; END_STRUCT "
        "END_TYPE\n";
    const char *globals_a =
        "VAR_GLOBAL ZShared : DINT := 9; END_VAR\n"
        "VAR_GLOBAL AShared : DINT := 4; END_VAR\n";
    const char *globals_b =
        "VAR_GLOBAL AShared : DINT := 4; END_VAR\n"
        "VAR_GLOBAL ZShared : DINT := 9; END_VAR\n";
    const char *function =
        "FUNCTION ReadShared : DINT\n"
        "VAR_EXTERNAL AShared : DINT; ZShared : DINT; END_VAR\n"
        "ReadShared := AShared * 10 + ZShared;\nEND_FUNCTION\n";
    const char *alpha =
        "PROGRAM Alpha\nVAR P : Packet; Out : DINT; END_VAR\n"
        "Out := ReadShared();\n"
        "END_PROGRAM\n";
    const char *zeta =
        "PROGRAM Zeta\nVAR M : Mode; Out : DINT; END_VAR\n"
        "Out := ReadShared();\nEND_PROGRAM\n";

    const std::string sources[] = {
        std::string(types_a) + globals_a + function + alpha + zeta,
        std::string(types_b) + globals_b + zeta + alpha + function,
        std::string(types_c) + globals_a + alpha + function + zeta,
    };
    const st::CompileResult compiled[] = {
        st::compile(sources[0]), st::compile(sources[1]),
        st::compile(sources[2]),
    };
    check(compiled[0].ok && compiled[1].ok && compiled[2].ok,
          "L2b-A06 type/global/POU source permutations compile");
    if(!compiled[0].ok || !compiled[1].ok || !compiled[2].ok) return;

    const std::string manifest = compiled[0].program.canonical_manifest();
    for(std::size_t index = 1; index < 3; ++index) {
        check(compiled[index].program.canonical_manifest() == manifest,
              "L2b-A06 source permutations produce identical artifact");
        for(const char *name : {"alpha", "zeta"}) {
            const st::Program *base = program_info(compiled[0], name);
            const st::Program *other = program_info(compiled[index], name);
            std::string base_types;
            std::string other_types;
            const bool same_types =
                base != nullptr && other != nullptr &&
                base->types.canonical_dump(base_types) == st::TypeError::ok &&
                other->types.canonical_dump(other_types) == st::TypeError::ok &&
                base_types == other_types;
            check(base != nullptr && other != nullptr &&
                      base->code == other->code &&
                      same_vars(base->vars, other->vars) &&
                      base->initial_data == other->initial_data && same_types,
                  "L2b-A06 TypeId/global layout/code/initial data are stable");
        }
    }

    for(const st::CompileResult &result : compiled) {
        for(const char *name : {"alpha", "zeta"}) {
            st::Instance instance;
            alignas(8) unsigned char buffer[262144] = {};
            const bool loaded =
                instance.load(result.program, name, buffer, sizeof(buffer),
                              kPeriodNs) == rt::ErrorCode::ok;
            const int out = loaded ? instance.find("out") : -1;
            check(loaded && instance.scan(kBudget) == st::ScanError::ok &&
                      out >= 0 &&
                      instance.value_i64(static_cast<std::size_t>(out)) == 49,
                  "L2b-A06 every source permutation executes identically");
        }
    }

    const st::CompileResult unknown = st::compile(
        "TYPE Broken : ARRAY[0..1] OF Missing; END_TYPE\n"
        "VAR_GLOBAL G : DINT; END_VAR\n"
        "PROGRAM Main\nVAR X : Broken; END_VAR\nEND_PROGRAM\n");
    check(!unknown.ok && has_code(unknown, st::DiagCode::sema_type_mismatch),
          "L2b-A06 unknown type keeps existing diagnostic");
    const st::CompileResult cycle = st::compile(
        "TYPE First : STRUCT Next : Second; END_STRUCT END_TYPE\n"
        "TYPE Second : STRUCT Next : First; END_STRUCT END_TYPE\n"
        "VAR_GLOBAL G : DINT; END_VAR\n"
        "PROGRAM Main\nVAR X : First; END_VAR\nEND_PROGRAM\n");
    check(!cycle.ok && has_code(cycle, st::DiagCode::sema_recursive_type),
          "L2b-A06 type cycle keeps existing diagnostic");
    const st::CompileResult duplicate = st::compile(
        "TYPE Same : DINT(0..1); END_TYPE\n"
        "TYPE Same : DINT(0..2); END_TYPE\n"
        "VAR_GLOBAL G : DINT; END_VAR\n"
        "PROGRAM Main\nEND_PROGRAM\n");
    check(!duplicate.ok &&
              has_code(duplicate, st::DiagCode::sema_duplicate_identifier),
          "L2b-A06 duplicate type keeps existing diagnostic");
}

void real_instruction_reports()
{
    const char *compact =
        "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
        "Inc := X + 1;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR Out : DINT; END_VAR\nOut := Inc(4);\n"
        "END_PROGRAM\n";
    const char *spaced =
        "(* formatting and comments are not executable *)\n"
        "FUNCTION Inc : DINT\n\nVAR_INPUT X : DINT; END_VAR\n"
        "Inc := X + 1; (* same body *)\nEND_FUNCTION\n\n"
        "PROGRAM Main\nVAR Out : DINT; END_VAR\n\nOut := Inc(4);\n"
        "END_PROGRAM\n";
    const st::CompileResult left = st::compile(compact);
    const st::CompileResult right = st::compile(spaced);
    check(left.ok && right.ok, "L2b WCI whitespace fixtures compile");
    if(left.ok && right.ok) {
        check(left.program.worst_case_bounded &&
                  right.program.worst_case_bounded &&
                  left.program.worst_case_instructions ==
                      right.program.worst_case_instructions,
              "L2b WCI is instruction-derived, not source-size-derived");
        for(const st::PouInfo &pou : left.program.pous) {
            const st::PouInfo *other = pou_info(right, pou.lower.c_str());
            check(other != nullptr &&
                      pou.worst_case_bounded == other->worst_case_bounded &&
                      pou.worst_case_instructions ==
                          other->worst_case_instructions,
                  "L2b per-POU WCI ignores whitespace and comments");
        }
    }

    const st::CompileResult linear = st::compile(
        "FUNCTION Id : DINT\nVAR_INPUT X : DINT; END_VAR\n"
        "Id := X;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR A : DINT; B : DINT; END_VAR\n"
        "A := Id(2 + 3); B := A * 4;\nEND_PROGRAM\n");
    check(linear.ok && linear.program.worst_case_bounded,
          "L2b straight-line WCI is bounded");
    if(linear.ok) {
        std::int64_t exact = 0;
        for(std::int64_t budget = 1; budget < 128; ++budget) {
            st::Instance instance;
            alignas(8) unsigned char buffer[4096] = {};
            if(instance.load(linear.program, "main", buffer, sizeof(buffer),
                             kPeriodNs) == rt::ErrorCode::ok &&
               instance.scan(budget) == st::ScanError::ok) {
                exact = budget;
                break;
            }
        }
        check(exact > 0 &&
                  linear.program.worst_case_instructions ==
                      static_cast<std::uint64_t>(exact),
              "L2b straight-line WCI equals VM exact minimum budget");
        const st::PouInfo *main = pou_info(linear, "main");
        check(main != nullptr && main->worst_case_bounded &&
                  main->worst_case_instructions ==
                      static_cast<std::uint64_t>(exact),
              "L2b PROGRAM report equals its exact VM instruction bound");
    }

    const st::CompileResult loop = st::compile(
        "FUNCTION Id : DINT\nVAR_INPUT X : DINT; END_VAR\n"
        "Id := X;\nEND_FUNCTION\n"
        "PROGRAM Main\nVAR Run : BOOL := FALSE; Out : DINT; END_VAR\n"
        "Out := Id(1); WHILE Run DO Run := FALSE; END_WHILE;\n"
        "END_PROGRAM\n");
    const st::PouInfo *main = loop.ok ? pou_info(loop, "main") : nullptr;
    check(loop.ok && !loop.program.worst_case_bounded && main != nullptr &&
              !main->worst_case_bounded,
          "L2b back-edge WCI is explicitly unbounded");

    const st::CompileResult branches = st::compile(
        "FUNCTION Choose : DINT\nVAR_INPUT Fast : BOOL; END_VAR\n"
        "VAR X : DINT; END_VAR\n"
        "IF Fast THEN Choose := 1; RETURN; "
        "ELSE X := 2; X := X + 3; X := X * 4; Choose := X; END_IF;\n"
        "END_FUNCTION\n"
        "PROGRAM Short\nVAR Out : DINT; END_VAR\n"
        "Out := Choose(TRUE);\nEND_PROGRAM\n"
        "PROGRAM Long\nVAR Out : DINT; END_VAR\n"
        "Out := Choose(FALSE);\nEND_PROGRAM\n");
    check(branches.ok && branches.program.worst_case_bounded,
          "L2b unequal IF/early RETURN WCI fixture compiles bounded");
    if(branches.ok) {
        const std::int64_t short_exact = exact_budget(branches, "short");
        const std::int64_t long_exact = exact_budget(branches, "long");
        const st::Program *short_program = program_info(branches, "short");
        const st::Program *long_program = program_info(branches, "long");
        const auto output = [&branches](const char *name) {
            st::Instance instance;
            alignas(8) unsigned char buffer[262144] = {};
            if(instance.load(branches.program, name, buffer, sizeof(buffer),
                             kPeriodNs) != rt::ErrorCode::ok ||
               instance.scan(kBudget) != st::ScanError::ok) {
                return std::int64_t{-1};
            }
            const int out = instance.find("out");
            return out < 0
                       ? std::int64_t{-1}
                       : instance.value_i64(static_cast<std::size_t>(out));
        };
        check(short_exact > 0 && long_exact > short_exact,
              "L2b WCI drives early-return and longer executable paths");
        check(output("short") == 1 && output("long") == 20,
              "L2b WCI fixture executes both unequal IF paths");
        check(long_exact > 0 &&
                  branches.program.worst_case_instructions ==
                      static_cast<std::uint64_t>(long_exact) &&
                  short_program != nullptr && long_program != nullptr &&
                  short_program->worst_case_instructions ==
                      static_cast<std::uint64_t>(long_exact) &&
                  long_program->worst_case_instructions ==
                      static_cast<std::uint64_t>(long_exact),
              "L2b WCI report equals longer path exact VM budget");
    }

    const st::CompileResult object_input = st::compile(
        "PROGRAM Main\nVAR G : GROUP_REF; Positive : MC_JOG_BOOLEAN_ARRAY; "
        "Negative : MC_JOG_BOOLEAN_ARRAY; Jog : MC_GroupJog; END_VAR\n"
        "Jog(AxesGroup := G, Enable := FALSE, JogPositive := Positive, "
        "JogNegative := Negative, VelOverride := 1.0, AccOverride := 1.0, "
        "CoordSystem := MC_COORD_SYSTEM#acs, MaxLinearDistance := 0.0, "
        "MaxAngularDistance := 0.0);\nEND_PROGRAM\n");
    check(object_input.ok && object_input.program.worst_case_bounded,
          "A2 object-input WCI fixture compiles bounded");
    if(object_input.ok) {
        const std::int64_t object_exact = exact_budget(object_input, "main");
        check(object_exact > 0 &&
                  object_input.program.worst_case_instructions ==
                      static_cast<std::uint64_t>(object_exact),
              "A2 FB object-input WCI equals VM exact minimum budget");
    }
}

void multi_pou_scan_is_zero_allocation()
{
    const char *source =
        "FUNCTION Inner : DINT\n"
        "VAR_INPUT X : DINT; END_VAR\nVAR_IN_OUT Ref : DINT; END_VAR\n"
        "Ref := Ref + X; Inner := Ref * 2;\nEND_FUNCTION\n"
        "FUNCTION Outer : DINT\n"
        "VAR_INPUT X : DINT; END_VAR\nVAR_IN_OUT Ref : DINT; END_VAR\n"
        "Outer := Inner(X, Ref);\n"
        "END_FUNCTION\n"
        "FUNCTION_BLOCK Counter\nVAR_INPUT Step : DINT; END_VAR\n"
        "VAR_OUTPUT Q : DINT; END_VAR\nVAR Total : DINT; END_VAR\n"
        "Total := Total + Step; Q := Total;\nEND_FUNCTION_BLOCK\n"
        "PROGRAM Main\nVAR C : Counter; State : DINT; "
        "FbOut : DINT; Result : DINT; END_VAR\n"
        "C(Step := 1, Q => FbOut); "
        "Result := Outer(2, State) + FbOut;\n"
        "END_PROGRAM\n";
    std::int64_t exact = 0;
    for(std::int64_t budget = 1; budget < 1024; ++budget) {
        Rig probe;
        if(probe.build(source) && probe.scan(budget) == st::ScanError::ok) {
            exact = budget;
            break;
        }
    }
    check(exact > 1, "L2b RT multi-POU exact budget discovered");
    Rig under;
    check(under.build(source) &&
              under.scan(exact - 1) == st::ScanError::budget_exceeded,
          "L2b RT multi-POU budget N-1 faults");
    Rig exact_rig;
    check(exact_rig.build(source) &&
              exact_rig.scan(exact) == st::ScanError::ok,
          "L2b RT multi-POU budget N succeeds");
    check(exact_rig.scan(kBudget) == st::ScanError::ok,
          "L2b RT multi-POU warm-up scan succeeds");
    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    bool healthy = true;
    for(int scan = 0; scan < 1000; ++scan) {
        if(exact_rig.scan(kBudget) != st::ScanError::ok) {
            healthy = false;
            break;
        }
    }
    g_freeze_allocations = false;
    check(healthy, "L2b RT multi-POU remains healthy with allocator frozen");
    check(g_frozen_allocations == 0,
          "L2b RT nested FUNCTION/FB/IN_OUT/OUTPUT scan allocates zero");
}

const st::PouInfo *pou_info(const st::CompileResult &result,
                            const char *lower)
{
    for(const st::PouInfo &pou : result.program.pous) {
        if(pou.lower == lower) return &pou;
    }
    return nullptr;
}

void typed_frame_and_instance_layout()
{
    const char *types =
        "TYPE Values : ARRAY[0..2] OF DINT; END_TYPE\n"
        "TYPE Packet : STRUCT Flag : BOOL; Values : Values; END_STRUCT "
        "END_TYPE\n";
    const std::string source =
        std::string(types) +
        "FUNCTION Work : Packet\nVAR_INPUT A : Values; END_VAR\n"
        "VAR S : STRING[5]; W : WSTRING[3]; END_VAR\nEND_FUNCTION\n"
        "FUNCTION_BLOCK Leaf\nVAR S : STRING[5]; W : WSTRING[3]; "
        "A : Values; END_VAR\nEND_FUNCTION_BLOCK\n"
        "FUNCTION_BLOCK Parent\nVAR Child : Leaf; P : Packet; END_VAR\n"
        "END_FUNCTION_BLOCK\n"
        "PROGRAM Alpha\nVAR Root : Parent; Local : Packet; "
        "Text : STRING[9]; END_VAR\nEND_PROGRAM\n"
        "PROGRAM Beta\nVAR Leaf2 : Leaf; Wide : WSTRING[5]; END_VAR\n"
        "END_PROGRAM\n";
    const st::CompileResult result = st::compile(source);
    check(result.ok, "L2b typed layout project compiles");
    if(!result.ok) return;

    const st::PouInfo *work = pou_info(result, "work");
    const st::PouInfo *leaf = pou_info(result, "leaf");
    const st::PouInfo *parent = pou_info(result, "parent");
    const st::PouInfo *alpha = pou_info(result, "alpha");
    const st::PouInfo *beta = pou_info(result, "beta");
    check(work != nullptr && work->frame_bytes == 56,
          "L2b frame uses ARRAY/STRUCT/STRING/WSTRING TypeDesc sizes");
    check(leaf != nullptr && leaf->frame_bytes == 40,
          "L2b FB frame uses exact descriptor sizes");
    check(parent != nullptr && parent->frame_bytes == 56,
          "L2b nested FB frame includes its static child tree");
    check(alpha != nullptr && alpha->frame_bytes == 88 && beta != nullptr &&
              beta->frame_bytes == 64,
          "L2b every PROGRAM reports its complete typed tree bytes");

    check(result.program.instances.size() == 5,
          "L2b reports every PROGRAM and nested FB instance");
    std::uint64_t previous_end = 0;
    for(const st::InstanceInfo &instance : result.program.instances) {
        check(instance.offset % 8U == 0 && instance.bytes % 8U == 0,
              "L2b instance offsets and sizes are 8-aligned");
        check(instance.offset >= previous_end,
              "L2b all PROGRAM instance ranges are non-overlapping");
        previous_end = instance.offset + instance.bytes;
    }
    check(previous_end == 152,
          "L2b typed multi-PROGRAM instance layout has exact bytes");
    check(previous_end <= result.program.required_bytes(),
          "L2b complete instance report fits artifact footprint");

    st::CompileOptions options;
    options.max_vars_bytes = 56;
    const std::string frame_source =
        std::string(types) +
        "FUNCTION Work : Packet\nVAR_INPUT A : Values; END_VAR\n"
        "VAR S : STRING[5]; W : WSTRING[3]; END_VAR\nEND_FUNCTION\n"
        "PROGRAM Main\nEND_PROGRAM\n";
    check(st::compile(frame_source, options).ok,
          "L2b typed frame capacity N accepted");
    options.max_vars_bytes = 55;
    const st::CompileResult frame_over = st::compile(frame_source, options);
    check(!frame_over.ok &&
              has_code(frame_over, st::DiagCode::capacity_exceeded),
          "L2b typed frame capacity N+1 rejected");

    options = st::CompileOptions{};
    options.max_vars_bytes = 56;
    const std::string nested_source =
        std::string(types) +
        "FUNCTION_BLOCK Leaf\nVAR S : STRING[5]; W : WSTRING[3]; "
        "A : Values; END_VAR\nEND_FUNCTION_BLOCK\n"
        "FUNCTION_BLOCK Parent\nVAR Child : Leaf; P : Packet; END_VAR\n"
        "END_FUNCTION_BLOCK\nPROGRAM Main\nVAR Root : Parent; END_VAR\n"
        "END_PROGRAM\n";
    check(st::compile(nested_source, options).ok,
          "L2b nested instance capacity N accepted");
    options.max_vars_bytes = 55;
    const st::CompileResult instance_over =
        st::compile(nested_source, options);
    check(!instance_over.ok &&
              has_code(instance_over, st::DiagCode::capacity_exceeded),
          "L2b nested instance capacity N+1 rejected");
}

// T40/L0 semantics 2.3: the API owns POU-grained fragments and produces the
// same semantics as a clean build.  Full reparse is deliberately permitted;
// true incremental caching belongs to the excluded IDE/LSP tooling scope.
void pou_fragment_frontend()
{
    st::IncrementalCompiler session;
    check(session.update_pou(
              "inc",
              "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
              "Inc := X + 1;\nEND_FUNCTION\n") == st::IncrementalStatus::ok,
          "T40 initial FUNCTION fragment accepted");
    check(session.update_pou(
              "main",
              "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
              "Out := Inc(4);\nEND_PROGRAM\n") == st::IncrementalStatus::ok,
          "T40 initial PROGRAM fragment accepted");
    st::CompileResult initial = session.compile();
    check(initial.ok, "T40 initial project compiles");

    check(session.update_pou(
              "inc",
              "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
              "Inc := X + 2;\nEND_FUNCTION\n") == st::IncrementalStatus::ok,
          "T40 changed FUNCTION fragment accepted");
    st::CompileResult changed = session.compile();
    check(changed.ok, "T40 changed project compiles");
    if(changed.ok) {
        st::Instance instance;
        alignas(8) unsigned char buffer[262144] = {};
        check(instance.load(changed.program, "main", buffer, sizeof(buffer),
                            kPeriodNs) == rt::ErrorCode::ok &&
                  instance.scan(kBudget) == st::ScanError::ok &&
                  instance.value_i64(
                      static_cast<std::size_t>(instance.find("out"))) == 6,
              "T40 changed fragment matches clean-build semantics");
    }

    check(session.update_pou("inc", "FUNCTION Inc : DINT\nInc := ;\n") ==
              st::IncrementalStatus::ok,
          "T40 incomplete fragment is retained");
    st::CompileResult broken = session.compile();
    check(!broken.ok && !broken.diagnostics.empty(),
          "T40 incomplete fragment returns diagnostics");
    check(session.update_pou(
              "inc",
              "FUNCTION Inc : DINT\nVAR_INPUT X : DINT; END_VAR\n"
              "Inc := X + 3;\nEND_FUNCTION\n") == st::IncrementalStatus::ok &&
              session.compile().ok,
          "T40 repaired fragment recovers without recreating session");
}

// L2b-A08: the bytecode format may move, but current L0-L1b source programs
// are recompiled and keep their source-level behavior under multi-POU ABI v2.
void current_source_regression()
{
    const Golden cases[] = {
        {"L0 arithmetic",
         "PROGRAM Main\nVAR Out : DINT; END_VAR\nOut := 2 + 3 * 4;\n"
         "END_PROGRAM\n",
         "main", "out", 1, 14},
        {"L0 control flow",
         "PROGRAM Main\nVAR I : INT; Out : INT; END_VAR\n"
         "FOR I := 1 TO 3 DO Out := Out + I; END_FOR;\nEND_PROGRAM\n",
         "main", "out", 1, 6},
        {"L1a conversion",
         "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
         "Out := INT_TO_DINT(DINT_TO_INT(65537));\nEND_PROGRAM\n",
         "main", "out", 1, 1},
        {"L1b1 enum",
         "TYPE E : (a, b := 4); END_TYPE\n"
         "PROGRAM Main\nVAR Out : DINT; END_VAR\n"
         "Out := E_TO_DINT(E#b);\nEND_PROGRAM\n",
         "main", "out", 1, 4},
        {"L1b2 aggregate",
         "TYPE Pair : STRUCT A : DINT; B : DINT; END_STRUCT END_TYPE\n"
         "PROGRAM Main\nVAR P : Pair := (A := 2, B := 5); Out : DINT; "
         "END_VAR\nOut := P.A + P.B;\nEND_PROGRAM\n",
         "main", "out", 1, 7},
        {"L1b3 string",
         "PROGRAM Main\nVAR S : STRING[8] := 'ab'; Out : DINT; END_VAR\n"
         "Out := LEN(S);\nEND_PROGRAM\n",
         "main", "out", 1, 2},
    };
    for(const Golden &test : cases) {
        Rig rig;
        if(!rig.build(test.source, test.program) ||
           rig.scan() != st::ScanError::ok ||
           rig.i64(test.result) != test.expected) {
            fail(test.name);
        }
    }
}

} // namespace

int main()
{
    golden_programs();
    structured_call_positions_and_return();
    entry_bound_inout();
    atomic_output_commit();
    nested_instance_paths_do_not_collide();
    copy_back_and_faults();
    alias_contract();
    call_rejections();
    scope_and_rejection_contract();
    external_type_and_standard_name_contract();
    graph_and_capacity_contract();
    deterministic_manifest_and_reports();
    complete_canonical_artifact();
    source_order_determinism();
    real_instruction_reports();
    multi_pou_scan_is_zero_allocation();
    typed_frame_and_instance_layout();
    pou_fragment_frontend();
    current_source_regression();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l2b tests passed\n");
    return 0;
}
