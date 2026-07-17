#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "st/token.h"
#include "st/type_desc.h"
#include "st/types.h"

// L0 AST (load domain, allocation allowed): index-based pools for
// deterministic iteration; no polymorphism (core builds with -fno-rtti).

namespace plcopen::core::st
{

using ExprIndex = std::int32_t;
using StmtIndex = std::int32_t;
inline constexpr ExprIndex kNoExpr = -1;

enum class ExprKind : std::uint8_t
{
    literal_int,   // unsigned_value + based flag
    literal_real,  // real_value
    literal_bool,  // unsigned_value 0/1
    literal_time,  // signed_value ns
    literal_typed, // TYPE# literal: literal_type + int/real payload (L1a)
    literal_enum,  // UserType#Member (L1b1; resolved by sema)
    literal_string,
    literal_wstring,
    literal_date,
    literal_tod,
    literal_dt,
    variable,      // name
    pin_read,      // name '.' pin_name (FB output read)
    unary,         // op: minus / not
    binary,        // op token kind
    call,          // name '(' arguments ')': conversion/standard function
    aggregate_init,
};

struct AccessStep
{
    bool field = false;
    std::string name;
    std::vector<ExprIndex> indices;
};

struct InitItem
{
    std::string name; // empty for positional entries
    ExprIndex value = kNoExpr;
};

enum class UnaryOp : std::uint8_t
{
    negate,
    logical_not,
};

enum class BinaryOp : std::uint8_t
{
    add,
    subtract,
    multiply,
    divide,
    modulo,
    logical_and,
    logical_or,
    logical_xor,
    cmp_eq,
    cmp_ne,
    cmp_lt,
    cmp_gt,
    cmp_le,
    cmp_ge,
    power, // ** (L1a 5.2)
};

struct Expr
{
    ExprKind kind = ExprKind::literal_int;
    std::int32_t line = 0;
    std::int32_t column = 0;

    std::uint64_t unsigned_value = 0;
    std::int64_t signed_value = 0;
    double real_value = 0.0;
    bool based = false;
    bool real_form = false;           // literal_typed real payload
    Type literal_type = Type::bool_;  // literal_typed target type

    std::string name;      // variable / instance / conversion name
    std::string pin;       // pin name for pin_read
    std::string text;      // raw string payload (without quotes)

    UnaryOp unary_op = UnaryOp::negate;
    BinaryOp binary_op = BinaryOp::add;
    ExprIndex lhs = kNoExpr;
    ExprIndex rhs = kNoExpr;
    std::vector<ExprIndex> arguments;
    std::vector<AccessStep> access;
    std::vector<InitItem> items;
};

enum class StmtKind : std::uint8_t
{
    assign,
    if_,
    case_,
    for_,
    while_,
    repeat,
    fb_call,
    output_commit,
    exit_,
    continue_,
    return_,
    empty,
};

struct CaseLabel
{
    // Constant labels resolved by sema; parser stores expressions.
    ExprIndex low = kNoExpr;
    ExprIndex high = kNoExpr; // kNoExpr for single-value labels
};

struct CaseArm
{
    std::vector<CaseLabel> labels;
    std::vector<StmtIndex> body;
};

struct CallParam
{
    std::string pin;
    std::int32_t line = 0;
    std::int32_t column = 0;
    ExprIndex value = kNoExpr;
};

struct Stmt
{
    StmtKind kind = StmtKind::empty;
    std::int32_t line = 0;
    std::int32_t column = 0;
    bool debug_provenance = false;
    std::string debug_pou;
    std::string debug_call_path;
    std::int32_t debug_line = 0;
    std::int32_t debug_column = 0;
    std::uint16_t debug_call_depth = 0;

    // assign
    std::string target;            // variable name (original spelling)
    std::vector<AccessStep> target_access;
    ExprIndex value = kNoExpr;

    // if
    std::vector<ExprIndex> conditions;         // IF + ELSIF conditions
    std::vector<std::vector<StmtIndex>> branches; // matching bodies
    std::vector<StmtIndex> else_body;

    // case
    ExprIndex selector = kNoExpr;
    std::vector<CaseArm> arms;

    // for
    std::string control;           // control variable name
    ExprIndex from = kNoExpr;
    ExprIndex to = kNoExpr;
    ExprIndex by = kNoExpr;        // kNoExpr => implicit 1
    std::vector<StmtIndex> body;   // for/while/repeat body

    // while / repeat
    ExprIndex condition = kNoExpr; // WHILE pre / REPEAT UNTIL post

    // fb_call
    std::string instance;
    std::vector<CallParam> params;
};

struct VarDecl
{
    std::string name;          // original spelling
    std::string lower;         // lookup key
    bool is_fb = false;
    bool is_constant = false;  // VAR CONSTANT block member (L1a 2.5)
    bool is_retain = false;
    bool is_persistent = false;
    bool is_located = false;
    char location_area = 0;    // I/Q/M
    char location_width = 0;   // X/B/W/D/L
    std::uint32_t location_byte = 0;
    std::uint8_t location_bit = 0;
    Type type = Type::bool_;
    FbType fb_type = FbType::r_trig;
    std::string type_name;    // non-empty for a user-defined type
    std::uint32_t string_capacity = 0;
    bool wide_string = false;
    ExprIndex init = kNoExpr;  // constant expression or kNoExpr
    std::int32_t line = 0;
    std::int32_t column = 0;
};

enum class UserTypeKind : std::uint8_t
{
    enum_,
    subrange,
    array,
    struct_,
};

struct StructFieldDecl
{
    std::string name;
    std::string lower;
    Type type = Type::bool_;
    std::string type_name;
    std::int32_t line = 0;
    std::int32_t column = 0;
};

struct EnumMemberDecl
{
    std::string name;
    std::string lower;
    bool explicit_value = false;
    std::int64_t value = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
};

struct UserTypeDecl
{
    std::string name;
    std::string lower;
    UserTypeKind kind = UserTypeKind::enum_;
    Type base = Type::dint;
    std::vector<EnumMemberDecl> enum_members;
    IntegerValue range_lower;
    IntegerValue range_upper;
    std::vector<ArrayBound> array_bounds;
    Type element_type = Type::bool_;
    std::string element_type_name;
    std::vector<StructFieldDecl> struct_fields;
    std::int32_t line = 0;
    std::int32_t column = 0;
};

enum class SfcQualifier : std::uint8_t
{
    n = 0,
    s,
    r,
    l,
    d,
    p,
    sd,
    ds,
    sl,
};

struct SfcActionBlockDecl
{
    std::string action;
    std::string lower;
    SfcQualifier qualifier = SfcQualifier::n;
    std::int64_t duration_ns = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcStepDecl
{
    std::string name;
    std::string lower;
    bool initial = false;
    bool terminal = false;
    std::vector<SfcActionBlockDecl> actions;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcTransitionDecl
{
    std::vector<std::string> sources;
    std::vector<std::string> targets;
    std::string condition;
    bool simultaneous = false;
    std::int32_t condition_line = 0;
    std::int32_t condition_column = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcActionDecl
{
    std::string name;
    std::string lower;
    std::string body;
    std::int32_t body_line = 0;
    std::int32_t body_column = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcNetworkDecl
{
    std::string name;
    std::string lower;
    std::string program_lower;
    std::vector<SfcStepDecl> steps;
    std::vector<SfcTransitionDecl> transitions;
    std::vector<SfcActionDecl> actions;
    std::int32_t line = 0;
    std::int32_t column = 0;
};

struct Ast
{
    std::string program_name;
    std::vector<UserTypeDecl> user_types;
    std::vector<VarDecl> vars;
    std::vector<StmtIndex> body;
    std::vector<SfcNetworkDecl> sfc_networks;
    std::vector<Expr> exprs;
    std::vector<Stmt> stmts;

    ExprIndex add_expr(Expr expr)
    {
        exprs.push_back(static_cast<Expr &&>(expr));
        return static_cast<ExprIndex>(exprs.size() - 1);
    }

    StmtIndex add_stmt(Stmt stmt)
    {
        stmts.push_back(static_cast<Stmt &&>(stmt));
        return static_cast<StmtIndex>(stmts.size() - 1);
    }
};

} // namespace plcopen::core::st
