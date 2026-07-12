#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "st/ast.h"
#include "st/diag.h"
#include "st/lexer.h"

// L0 fault-tolerant recursive-descent parser (approved st-l0-semantics
// 2.1/2.3/2.7): statement-level panic recovery at sync points (';', END_*,
// statement-start keywords), multiple diagnostics per run, partial AST.
// Expression nesting is depth-bounded so arbitrary fuzz input cannot
// exhaust the machine stack. Incremental granularity is POU-level by API
// shape: parse() consumes one full compilation unit deterministically.

namespace plcopen::core::st
{

struct ParseResult
{
    Ast ast;
    std::vector<Diagnostic> diagnostics;
    bool ok = false; // no error diagnostics produced by lexer/parser
};

class Parser
{
public:
    Parser(std::string_view source, std::size_t max_diagnostics,
           std::int32_t max_nesting)
        : lexer_(source)
        , max_diagnostics_(max_diagnostics)
        , max_nesting_(max_nesting)
    {
        next_ = fetch();
        bump();
    }

    ParseResult parse()
    {
        parse_program();
        result_.ok = !has_error_;
        return static_cast<ParseResult &&>(result_);
    }

private:
    // --- token plumbing -------------------------------------------------

    // Lexical errors surface as diagnostics immediately; the token stream
    // continues so parsing can recover.
    Token fetch()
    {
        Token token = lexer_.next();
        while(token.kind == TokenKind::error) {
            diag(static_cast<DiagCode>(token.diag_payload), token);
            token = lexer_.next();
        }
        return token;
    }

    void bump()
    {
        current_ = next_;
        next_ = fetch();
    }

    const Token &peek_next() const
    {
        return next_;
    }

    bool at(TokenKind kind) const
    {
        return current_.kind == kind;
    }

    bool eat(TokenKind kind)
    {
        if(!at(kind)) {
            return false;
        }
        bump();
        return true;
    }

    bool expect(TokenKind kind, const char *what)
    {
        if(eat(kind)) {
            return true;
        }
        if(at(TokenKind::unsupported_keyword)) {
            // Report the ownership code instead of a bare syntax error so
            // out-of-subset constructs are attributed to their batch.
            diag(static_cast<DiagCode>(current_.diag_payload), current_);
            bump();
            return false;
        }
        diag(DiagCode::parse_expected_token, current_, what);
        return false;
    }

    void diag(DiagCode code, const Token &token, const char *note = nullptr)
    {
        has_error_ = true;
        if(result_.diagnostics.size() >= max_diagnostics_) {
            if(!capacity_reported_) {
                capacity_reported_ = true;
                Diagnostic d;
                d.line = token.line;
                d.column = token.column;
                d.code = DiagCode::capacity_diagnostics;
                d.message = "diagnostic capacity exceeded";
                result_.diagnostics.push_back(static_cast<Diagnostic &&>(d));
            }
            return;
        }
        Diagnostic d;
        d.line = token.line;
        d.column = token.column;
        d.code = code;
        d.message = to_string(code);
        if(note) {
            d.message += ": ";
            d.message += note;
        }
        if(!token.text.empty() && token.text.size() <= 64) {
            d.message += " near '";
            d.message.append(token.text.data(), token.text.size());
            d.message += "'";
        }
        result_.diagnostics.push_back(static_cast<Diagnostic &&>(d));
    }

    // --- recovery --------------------------------------------------------

    static bool is_statement_start(TokenKind kind)
    {
        switch(kind) {
        case TokenKind::identifier:
        case TokenKind::kw_if:
        case TokenKind::kw_case:
        case TokenKind::kw_for:
        case TokenKind::kw_while:
        case TokenKind::kw_repeat:
        case TokenKind::kw_exit:
        case TokenKind::kw_continue:
        case TokenKind::kw_return:
        case TokenKind::semicolon:
            return true;
        default:
            return false;
        }
    }

    static bool is_block_end(TokenKind kind)
    {
        switch(kind) {
        case TokenKind::kw_end_if:
        case TokenKind::kw_end_case:
        case TokenKind::kw_end_for:
        case TokenKind::kw_end_while:
        case TokenKind::kw_end_repeat:
        case TokenKind::kw_end_program:
        case TokenKind::kw_end_var:
        case TokenKind::kw_elsif:
        case TokenKind::kw_else:
        case TokenKind::kw_until:
        case TokenKind::end_of_input:
            return true;
        default:
            return false;
        }
    }

    // Panic-mode: skip until a statement boundary.
    void recover_statement()
    {
        while(!at(TokenKind::end_of_input)) {
            if(at(TokenKind::semicolon)) {
                bump();
                return;
            }
            if(is_block_end(current_.kind) || is_statement_start(current_.kind)) {
                return;
            }
            bump();
        }
    }

    // --- program ----------------------------------------------------------

    void parse_program()
    {
        if(at(TokenKind::unsupported_keyword)) {
            diag(static_cast<DiagCode>(current_.diag_payload), current_);
            return;
        }
        if(!expect(TokenKind::kw_program, "PROGRAM")) {
            return;
        }
        if(at(TokenKind::identifier)) {
            result_.ast.program_name.assign(current_.text.data(),
                                            current_.text.size());
            bump();
        } else {
            diag(DiagCode::parse_expected_token, current_, "program name");
        }
        while(at(TokenKind::kw_var)) {
            parse_var_block();
        }
        parse_statement_list(result_.ast.body, {TokenKind::kw_end_program});
        expect(TokenKind::kw_end_program, "END_PROGRAM");
    }

    void parse_var_block()
    {
        bump(); // VAR
        // VAR CONSTANT block (approved st-l1a-semantics 2.5): every member
        // is a compile-time constant.
        const bool constant_block = eat(TokenKind::kw_constant);
        while(!at(TokenKind::kw_end_var) && !at(TokenKind::end_of_input)) {
            if(at(TokenKind::unsupported_keyword)) {
                diag(static_cast<DiagCode>(current_.diag_payload), current_);
                bump();
                recover_statement();
                continue;
            }
            if(!at(TokenKind::identifier)) {
                diag(DiagCode::parse_expected_token, current_,
                     "variable declaration");
                // Consume the offending token before recovery: statement
                // starters would otherwise satisfy the recovery sync set
                // without progress and loop forever (fuzz-found).
                bump();
                recover_statement();
                continue;
            }
            VarDecl decl;
            decl.is_constant = constant_block;
            decl.line = current_.line;
            decl.column = current_.column;
            decl.name.assign(current_.text.data(), current_.text.size());
            decl.lower = lower_copy(current_.text);
            bump();
            if(!expect(TokenKind::colon, "':'")) {
                recover_statement();
                continue;
            }
            if(!parse_type(decl)) {
                recover_statement();
                continue;
            }
            if(eat(TokenKind::assign)) {
                decl.init = parse_expression();
                if(decl.init == kNoExpr) {
                    recover_statement();
                    continue;
                }
            }
            expect(TokenKind::semicolon, "';'");
            result_.ast.vars.push_back(static_cast<VarDecl &&>(decl));
        }
        expect(TokenKind::kw_end_var, "END_VAR");
    }

    bool parse_type(VarDecl &decl)
    {
        switch(current_.kind) {
        case TokenKind::kw_bool: decl.type = Type::bool_; break;
        case TokenKind::kw_int: decl.type = Type::int_; break;
        case TokenKind::kw_dint: decl.type = Type::dint; break;
        case TokenKind::kw_real: decl.type = Type::real; break;
        case TokenKind::kw_lreal: decl.type = Type::lreal; break;
        case TokenKind::kw_time: decl.type = Type::time; break;
        case TokenKind::kw_sint: decl.type = Type::sint; break;
        case TokenKind::kw_lint: decl.type = Type::lint; break;
        case TokenKind::kw_usint: decl.type = Type::usint; break;
        case TokenKind::kw_uint: decl.type = Type::uint_; break;
        case TokenKind::kw_udint: decl.type = Type::udint; break;
        case TokenKind::kw_ulint: decl.type = Type::ulint; break;
        case TokenKind::kw_byte: decl.type = Type::byte_; break;
        case TokenKind::kw_word: decl.type = Type::word; break;
        case TokenKind::kw_dword: decl.type = Type::dword; break;
        case TokenKind::kw_lword: decl.type = Type::lword; break;
        case TokenKind::unsupported_keyword:
            diag(static_cast<DiagCode>(current_.diag_payload), current_);
            bump();
            return false;
        case TokenKind::identifier: {
            // FB type name (matrix 3.8 bound set).
            static constexpr struct
            {
                std::string_view lower;
                FbType type;
            } kFbNames[] = {
                {"r_trig", FbType::r_trig}, {"f_trig", FbType::f_trig},
                {"sr", FbType::sr},         {"rs", FbType::rs},
                {"ton", FbType::ton},       {"tof", FbType::tof},
                {"tp", FbType::tp},         {"ctu", FbType::ctu},
                {"ctd", FbType::ctd},       {"ctud", FbType::ctud},
                {"mc_power", FbType::mc_power},
                {"mc_home", FbType::mc_home},
                {"mc_stop", FbType::mc_stop},
                {"mc_halt", FbType::mc_halt},
                {"mc_moveabsolute", FbType::mc_move_absolute},
                {"mc_moverelative", FbType::mc_move_relative},
                {"mc_moveadditive", FbType::mc_move_additive},
                {"mc_movevelocity", FbType::mc_move_velocity},
                {"mc_setoverride", FbType::mc_set_override},
                {"mc_reset", FbType::mc_reset},
            };
            const std::string lower = lower_copy(current_.text);
            if(lower == "axis_ref") {
                decl.type = Type::axis_ref;
                bump();
                return true;
            }
            for(const auto &entry : kFbNames) {
                if(lower == entry.lower) {
                    decl.is_fb = true;
                    decl.fb_type = entry.type;
                    bump();
                    return true;
                }
            }
            if(lower == "rtc") {
                diag(DiagCode::unsupported_non_goal, current_,
                     "RTC needs a calendar clock");
                bump();
                return false;
            }
            diag(DiagCode::parse_expected_type, current_);
            bump();
            return false;
        }
        default:
            diag(DiagCode::parse_expected_type, current_);
            return false;
        }
        bump();
        return true;
    }

    // --- statements -------------------------------------------------------

    // Statement list bounded by an explicit stop set. A block-end token that
    // is NOT in the stop set is a stray closer: it is reported and skipped so
    // the list (and every later statement) keeps parsing -- recovery must
    // never silently terminate an outer list (metric 5.8).
    void parse_statement_list(std::vector<StmtIndex> &out,
                              std::initializer_list<TokenKind> stops)
    {
        const auto stopped = [&]() {
            for(const TokenKind stop : stops) {
                if(at(stop)) {
                    return true;
                }
            }
            return at(TokenKind::end_of_input);
        };
        while(!stopped()) {
            if(is_block_end(current_.kind)) {
                diag(DiagCode::parse_unexpected_token, current_,
                     "stray block end");
                bump();
                continue;
            }
            const StmtIndex index = parse_statement();
            if(index != -2) { // -2 => recovered, nothing to append
                out.push_back(index);
            }
        }
    }

    // Returns stmt index, or -2 when the statement failed and recovery ran.
    StmtIndex parse_statement()
    {
        switch(current_.kind) {
        case TokenKind::semicolon: {
            Stmt stmt;
            stmt.kind = StmtKind::empty;
            stmt.line = current_.line;
            stmt.column = current_.column;
            bump();
            return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
        }
        case TokenKind::kw_if: return parse_if();
        case TokenKind::kw_case: return parse_case();
        case TokenKind::kw_for: return parse_for();
        case TokenKind::kw_while: return parse_while();
        case TokenKind::kw_repeat: return parse_repeat();
        case TokenKind::kw_exit: {
            Stmt stmt;
            stmt.kind = StmtKind::exit_;
            stmt.line = current_.line;
            stmt.column = current_.column;
            bump();
            expect(TokenKind::semicolon, "';'");
            return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
        }
        case TokenKind::kw_continue: {
            Stmt stmt;
            stmt.kind = StmtKind::continue_;
            stmt.line = current_.line;
            stmt.column = current_.column;
            bump();
            expect(TokenKind::semicolon, "';'");
            return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
        }
        case TokenKind::kw_return: {
            Stmt stmt;
            stmt.kind = StmtKind::return_;
            stmt.line = current_.line;
            stmt.column = current_.column;
            bump();
            expect(TokenKind::semicolon, "';'");
            return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
        }
        case TokenKind::identifier: return parse_assign_or_call();
        case TokenKind::unsupported_keyword:
            diag(static_cast<DiagCode>(current_.diag_payload), current_);
            bump();
            recover_statement();
            return -2;
        default:
            diag(DiagCode::parse_expected_statement, current_);
            bump();
            recover_statement();
            return -2;
        }
    }

    StmtIndex parse_assign_or_call()
    {
        Stmt stmt;
        stmt.line = current_.line;
        stmt.column = current_.column;
        std::string first(current_.text.data(), current_.text.size());
        bump();

        if(at(TokenKind::lparen)) {
            // FB invocation: formal inst(PIN := expr, ...) or non-formal
            // inst(expr, expr, ...) covering every input pin in declaration
            // order (approved st-l1a-semantics 5.4). Mixing forms is an
            // error; the form is decided by the first argument.
            stmt.kind = StmtKind::fb_call;
            stmt.instance = static_cast<std::string &&>(first);
            bump();
            if(!at(TokenKind::rparen)) {
                const bool formal = at(TokenKind::identifier) &&
                                    peek_next().kind == TokenKind::assign;
                do {
                    CallParam param;
                    param.line = current_.line;
                    param.column = current_.column;
                    if(formal) {
                        if(!at(TokenKind::identifier) ||
                           peek_next().kind != TokenKind::assign) {
                            diag(DiagCode::parse_expected_token, current_,
                                 "formal parameter (no mixing of forms)");
                            recover_statement();
                            return -2;
                        }
                        param.pin.assign(current_.text.data(),
                                         current_.text.size());
                        bump();
                        bump(); // ':='
                    }
                    param.value = parse_expression();
                    if(param.value == kNoExpr) {
                        recover_statement();
                        return -2;
                    }
                    stmt.params.push_back(static_cast<CallParam &&>(param));
                } while(eat(TokenKind::comma));
            }
            if(!expect(TokenKind::rparen, "')'")) {
                recover_statement();
                return -2;
            }
            expect(TokenKind::semicolon, "';'");
            return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
        }

        if(at(TokenKind::dot)) {
            // Assignment to a pin is rejected later by sema; parse the
            // target as name.pin so the diagnostic lands precisely.
            bump();
            if(!at(TokenKind::identifier)) {
                diag(DiagCode::parse_expected_token, current_, "pin name");
                recover_statement();
                return -2;
            }
            first += '.';
            first.append(current_.text.data(), current_.text.size());
            bump();
        }

        stmt.kind = StmtKind::assign;
        stmt.target = static_cast<std::string &&>(first);
        if(!expect(TokenKind::assign, "':='")) {
            recover_statement();
            return -2;
        }
        stmt.value = parse_expression();
        if(stmt.value == kNoExpr) {
            recover_statement();
            return -2;
        }
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    StmtIndex parse_if()
    {
        Stmt stmt;
        stmt.kind = StmtKind::if_;
        stmt.line = current_.line;
        stmt.column = current_.column;
        bump(); // IF
        while(true) {
            const ExprIndex condition = parse_expression();
            if(condition == kNoExpr) {
                recover_statement();
                return -2;
            }
            stmt.conditions.push_back(condition);
            stmt.branches.emplace_back();
            if(!expect(TokenKind::kw_then, "THEN")) {
                recover_statement();
                return -2;
            }
            parse_statement_list(stmt.branches.back(),
                                 {TokenKind::kw_elsif, TokenKind::kw_else,
                                  TokenKind::kw_end_if});
            if(eat(TokenKind::kw_elsif)) {
                continue;
            }
            break;
        }
        if(eat(TokenKind::kw_else)) {
            parse_statement_list(stmt.else_body, {TokenKind::kw_end_if});
        }
        expect(TokenKind::kw_end_if, "END_IF");
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    StmtIndex parse_case()
    {
        Stmt stmt;
        stmt.kind = StmtKind::case_;
        stmt.line = current_.line;
        stmt.column = current_.column;
        bump(); // CASE
        stmt.selector = parse_expression();
        if(stmt.selector == kNoExpr || !expect(TokenKind::kw_of, "OF")) {
            recover_statement();
            return -2;
        }
        while(!at(TokenKind::kw_else) && !at(TokenKind::kw_end_case) &&
              !at(TokenKind::end_of_input)) {
            CaseArm arm;
            do {
                CaseLabel label;
                label.low = parse_expression();
                if(label.low == kNoExpr) {
                    recover_statement();
                    return -2;
                }
                if(eat(TokenKind::dotdot)) {
                    label.high = parse_expression();
                    if(label.high == kNoExpr) {
                        recover_statement();
                        return -2;
                    }
                }
                arm.labels.push_back(label);
            } while(eat(TokenKind::comma));
            if(!expect(TokenKind::colon, "':'")) {
                recover_statement();
                return -2;
            }
            parse_case_body(arm.body);
            stmt.arms.push_back(static_cast<CaseArm &&>(arm));
        }
        if(eat(TokenKind::kw_else)) {
            CaseArm else_arm;
            parse_statement_list(else_arm.body, {TokenKind::kw_end_case});
            // ELSE body is stored as an arm with zero labels.
            stmt.arms.push_back(static_cast<CaseArm &&>(else_arm));
        }
        expect(TokenKind::kw_end_case, "END_CASE");
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    // A CASE arm body additionally stops at the start of the next label:
    // labels are integer constants (optionally negated), and no statement
    // can start with a literal or '-'.
    void parse_case_body(std::vector<StmtIndex> &out)
    {
        while(!at(TokenKind::kw_else) && !at(TokenKind::kw_end_case) &&
              !at(TokenKind::end_of_input) && !at(TokenKind::int_literal) &&
              !at(TokenKind::minus)) {
            if(is_block_end(current_.kind)) {
                diag(DiagCode::parse_unexpected_token, current_,
                     "stray block end");
                bump();
                continue;
            }
            const StmtIndex index = parse_statement();
            if(index != -2) {
                out.push_back(index);
            }
        }
    }

    StmtIndex parse_for()
    {
        Stmt stmt;
        stmt.kind = StmtKind::for_;
        stmt.line = current_.line;
        stmt.column = current_.column;
        bump(); // FOR
        if(!at(TokenKind::identifier)) {
            diag(DiagCode::parse_expected_token, current_, "control variable");
            recover_statement();
            return -2;
        }
        stmt.control.assign(current_.text.data(), current_.text.size());
        bump();
        if(!expect(TokenKind::assign, "':='")) {
            recover_statement();
            return -2;
        }
        stmt.from = parse_expression();
        if(stmt.from == kNoExpr || !expect(TokenKind::kw_to, "TO")) {
            recover_statement();
            return -2;
        }
        stmt.to = parse_expression();
        if(stmt.to == kNoExpr) {
            recover_statement();
            return -2;
        }
        if(eat(TokenKind::kw_by)) {
            stmt.by = parse_expression();
            if(stmt.by == kNoExpr) {
                recover_statement();
                return -2;
            }
        }
        if(!expect(TokenKind::kw_do, "DO")) {
            recover_statement();
            return -2;
        }
        parse_statement_list(stmt.body, {TokenKind::kw_end_for});
        expect(TokenKind::kw_end_for, "END_FOR");
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    StmtIndex parse_while()
    {
        Stmt stmt;
        stmt.kind = StmtKind::while_;
        stmt.line = current_.line;
        stmt.column = current_.column;
        bump(); // WHILE
        stmt.condition = parse_expression();
        if(stmt.condition == kNoExpr || !expect(TokenKind::kw_do, "DO")) {
            recover_statement();
            return -2;
        }
        parse_statement_list(stmt.body, {TokenKind::kw_end_while});
        expect(TokenKind::kw_end_while, "END_WHILE");
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    StmtIndex parse_repeat()
    {
        Stmt stmt;
        stmt.kind = StmtKind::repeat;
        stmt.line = current_.line;
        stmt.column = current_.column;
        bump(); // REPEAT
        parse_statement_list(stmt.body, {TokenKind::kw_until});
        if(!expect(TokenKind::kw_until, "UNTIL")) {
            recover_statement();
            return -2;
        }
        stmt.condition = parse_expression();
        if(stmt.condition == kNoExpr) {
            recover_statement();
            return -2;
        }
        expect(TokenKind::kw_end_repeat, "END_REPEAT");
        expect(TokenKind::semicolon, "';'");
        return result_.ast.add_stmt(static_cast<Stmt &&>(stmt));
    }

    // --- expressions (precedence per appendix A) ---------------------------

    ExprIndex parse_expression()
    {
        if(nesting_ >= max_nesting_) {
            diag(DiagCode::parse_nesting_too_deep, current_);
            return kNoExpr;
        }
        ++nesting_;
        const ExprIndex result = parse_or();
        --nesting_;
        return result;
    }

    ExprIndex parse_or()
    {
        ExprIndex lhs = parse_xor();
        while(lhs != kNoExpr && at(TokenKind::kw_or)) {
            const Token op = current_;
            bump();
            const ExprIndex rhs = parse_xor();
            lhs = make_binary(BinaryOp::logical_or, lhs, rhs, op);
        }
        return lhs;
    }

    ExprIndex parse_xor()
    {
        ExprIndex lhs = parse_and();
        while(lhs != kNoExpr && at(TokenKind::kw_xor)) {
            const Token op = current_;
            bump();
            const ExprIndex rhs = parse_and();
            lhs = make_binary(BinaryOp::logical_xor, lhs, rhs, op);
        }
        return lhs;
    }

    ExprIndex parse_and()
    {
        ExprIndex lhs = parse_comparison();
        while(lhs != kNoExpr &&
              (at(TokenKind::kw_and) || at(TokenKind::ampersand))) {
            const Token op = current_;
            bump();
            const ExprIndex rhs = parse_comparison();
            lhs = make_binary(BinaryOp::logical_and, lhs, rhs, op);
        }
        return lhs;
    }

    ExprIndex parse_comparison()
    {
        ExprIndex lhs = parse_additive();
        if(lhs == kNoExpr) {
            return lhs;
        }
        BinaryOp op;
        switch(current_.kind) {
        case TokenKind::equal: op = BinaryOp::cmp_eq; break;
        case TokenKind::not_equal: op = BinaryOp::cmp_ne; break;
        case TokenKind::less: op = BinaryOp::cmp_lt; break;
        case TokenKind::greater: op = BinaryOp::cmp_gt; break;
        case TokenKind::less_equal: op = BinaryOp::cmp_le; break;
        case TokenKind::greater_equal: op = BinaryOp::cmp_ge; break;
        default: return lhs;
        }
        const Token token = current_;
        bump();
        const ExprIndex rhs = parse_additive();
        return make_binary(op, lhs, rhs, token);
    }

    ExprIndex parse_additive()
    {
        ExprIndex lhs = parse_multiplicative();
        while(lhs != kNoExpr &&
              (at(TokenKind::plus) || at(TokenKind::minus))) {
            const BinaryOp op =
                at(TokenKind::plus) ? BinaryOp::add : BinaryOp::subtract;
            const Token token = current_;
            bump();
            const ExprIndex rhs = parse_multiplicative();
            lhs = make_binary(op, lhs, rhs, token);
        }
        return lhs;
    }

    ExprIndex parse_multiplicative()
    {
        ExprIndex lhs = parse_unary();
        while(lhs != kNoExpr &&
              (at(TokenKind::star) || at(TokenKind::slash) ||
               at(TokenKind::kw_mod))) {
            BinaryOp op = BinaryOp::multiply;
            if(at(TokenKind::slash)) {
                op = BinaryOp::divide;
            } else if(at(TokenKind::kw_mod)) {
                op = BinaryOp::modulo;
            }
            const Token token = current_;
            bump();
            const ExprIndex rhs = parse_unary();
            lhs = make_binary(op, lhs, rhs, token);
        }
        return lhs;
    }

    ExprIndex parse_unary()
    {
        if(nesting_ >= max_nesting_) {
            diag(DiagCode::parse_nesting_too_deep, current_);
            return kNoExpr;
        }
        if(at(TokenKind::minus) || at(TokenKind::kw_not)) {
            Expr expr;
            expr.kind = ExprKind::unary;
            expr.unary_op = at(TokenKind::minus) ? UnaryOp::negate
                                                 : UnaryOp::logical_not;
            expr.line = current_.line;
            expr.column = current_.column;
            bump();
            ++nesting_;
            expr.lhs = parse_unary();
            --nesting_;
            if(expr.lhs == kNoExpr) {
                return kNoExpr;
            }
            // Fold a direct negative literal so INT/DINT minimum values type
            // correctly (-32768 must not range-check 32768 first).
            if(expr.unary_op == UnaryOp::negate) {
                Expr &operand =
                    result_.ast.exprs[static_cast<std::size_t>(expr.lhs)];
                if(operand.kind == ExprKind::literal_int && !operand.based &&
                   operand.signed_value == 0) {
                    operand.signed_value = -1; // negative-sign marker
                    return expr.lhs;
                }
                if(operand.kind == ExprKind::literal_real) {
                    operand.real_value = -operand.real_value;
                    return expr.lhs;
                }
            }
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        }
        return parse_power();
    }

    // ** binds tighter than unary minus and is right-associative; the
    // exponent may carry its own sign (IEC precedence, L1a 5.2).
    ExprIndex parse_power()
    {
        ExprIndex lhs = parse_primary();
        if(lhs == kNoExpr || !at(TokenKind::star_star)) {
            return lhs;
        }
        const Token token = current_;
        bump();
        ++nesting_;
        const ExprIndex rhs =
            nesting_ >= max_nesting_
                ? (diag(DiagCode::parse_nesting_too_deep, current_), kNoExpr)
                : parse_unary();
        --nesting_;
        return make_binary(BinaryOp::power, lhs, rhs, token);
    }

    ExprIndex parse_primary()
    {
        Expr expr;
        expr.line = current_.line;
        expr.column = current_.column;
        switch(current_.kind) {
        case TokenKind::int_literal:
            expr.kind = ExprKind::literal_int;
            expr.unsigned_value = current_.unsigned_value;
            expr.based = current_.based;
            bump();
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        case TokenKind::real_literal:
            expr.kind = ExprKind::literal_real;
            expr.real_value = current_.real_value;
            bump();
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        case TokenKind::bool_literal:
            expr.kind = ExprKind::literal_bool;
            expr.unsigned_value = current_.unsigned_value;
            bump();
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        case TokenKind::time_literal:
            expr.kind = ExprKind::literal_time;
            expr.signed_value = current_.signed_value;
            bump();
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        case TokenKind::typed_literal:
            expr.kind = ExprKind::literal_typed;
            expr.literal_type = current_.literal_type;
            expr.unsigned_value = current_.unsigned_value;
            expr.real_value = current_.real_value;
            expr.real_form = current_.real_form;
            expr.based = current_.based;
            expr.signed_value = current_.signed_value; // sign marker
            bump();
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        case TokenKind::lparen: {
            bump();
            ++nesting_;
            const ExprIndex inner =
                nesting_ >= max_nesting_
                    ? (diag(DiagCode::parse_nesting_too_deep, current_), kNoExpr)
                    : parse_expression();
            --nesting_;
            if(inner == kNoExpr) {
                return kNoExpr;
            }
            expect(TokenKind::rparen, "')'");
            return inner;
        }
        case TokenKind::identifier: {
            expr.name.assign(current_.text.data(), current_.text.size());
            bump();
            if(eat(TokenKind::dot)) {
                if(!at(TokenKind::identifier)) {
                    diag(DiagCode::parse_expected_token, current_, "pin name");
                    return kNoExpr;
                }
                expr.kind = ExprKind::pin_read;
                expr.pin.assign(current_.text.data(), current_.text.size());
                bump();
            } else if(at(TokenKind::lparen)) {
                // Conversion-function call (approved st-l1a-semantics 4.x):
                // single argument, resolved by sema against conv.h.
                bump();
                expr.kind = ExprKind::call;
                ++nesting_;
                expr.lhs = nesting_ >= max_nesting_
                               ? (diag(DiagCode::parse_nesting_too_deep,
                                       current_),
                                  kNoExpr)
                               : parse_expression();
                --nesting_;
                if(expr.lhs == kNoExpr) {
                    return kNoExpr;
                }
                if(!expect(TokenKind::rparen, "')'")) {
                    return kNoExpr;
                }
            } else {
                expr.kind = ExprKind::variable;
            }
            return result_.ast.add_expr(static_cast<Expr &&>(expr));
        }
        case TokenKind::unsupported_keyword:
            diag(static_cast<DiagCode>(current_.diag_payload), current_);
            bump();
            return kNoExpr;
        default:
            diag(DiagCode::parse_expected_expression, current_);
            return kNoExpr;
        }
    }

    ExprIndex make_binary(BinaryOp op, ExprIndex lhs, ExprIndex rhs,
                          const Token &token)
    {
        if(lhs == kNoExpr || rhs == kNoExpr) {
            return kNoExpr;
        }
        Expr expr;
        expr.kind = ExprKind::binary;
        expr.binary_op = op;
        expr.lhs = lhs;
        expr.rhs = rhs;
        expr.line = token.line;
        expr.column = token.column;
        return result_.ast.add_expr(static_cast<Expr &&>(expr));
    }

    static std::string lower_copy(std::string_view text)
    {
        std::string lower;
        lower.reserve(text.size());
        for(char c : text) {
            lower.push_back(detail::to_lower(c));
        }
        return lower;
    }

    Lexer lexer_;
    Token current_;
    Token next_;
    ParseResult result_;
    std::size_t max_diagnostics_ = 256;
    std::int32_t max_nesting_ = 64;
    std::int32_t nesting_ = 0;
    bool has_error_ = false;
    bool capacity_reported_ = false;
};

} // namespace plcopen::core::st
