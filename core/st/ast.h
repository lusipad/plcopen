#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "st/token.h"
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
    variable,      // name
    pin_read,      // name '.' pin_name (FB output read)
    unary,         // op: minus / not
    binary,        // op token kind
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

    std::string name;      // variable / instance name (original spelling)
    std::string pin;       // pin name for pin_read

    UnaryOp unary_op = UnaryOp::negate;
    BinaryOp binary_op = BinaryOp::add;
    ExprIndex lhs = kNoExpr;
    ExprIndex rhs = kNoExpr;
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
    exit_,
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

    // assign
    std::string target;            // variable name (original spelling)
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
    Type type = Type::bool_;
    FbType fb_type = FbType::r_trig;
    ExprIndex init = kNoExpr;  // constant expression or kNoExpr
    std::int32_t line = 0;
    std::int32_t column = 0;
};

struct Ast
{
    std::string program_name;
    std::vector<VarDecl> vars;
    std::vector<StmtIndex> body;
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
