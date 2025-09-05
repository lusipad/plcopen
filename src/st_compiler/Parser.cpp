/**
 * @file Parser.cpp
 * @brief ST语法分析器实现 - 递归下降解析器
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include "st_compiler/Lexer.h"
#include "st_compiler/AST.h"
#include <iostream>
#include <stdexcept>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief ST语法分析器实现类
 */
class ParserImpl {
private:
    std::vector<Token> tokens_;
    size_t current_;
    std::vector<std::string> errors_;
    
public:
    explicit ParserImpl(const std::vector<Token>& tokens) 
        : tokens_(tokens), current_(0) {}
    
    std::unique_ptr<AST> parse() {
        try {
            return parse_program();
        } catch (const std::exception& e) {
            errors_.push_back(std::string("Parse error: ") + e.what());
            return nullptr;
        }
    }
    
    const std::vector<std::string>& get_errors() const {
        return errors_;
    }
    
private:
    // Token操作
    const Token& current_token() const {
        if (current_ >= tokens_.size()) {
            static Token eof_token{TokenType::EOF_TOKEN};
            return eof_token;
        }
        return tokens_[current_];
    }
    
    const Token& peek_token(size_t offset = 1) const {
        size_t pos = current_ + offset;
        if (pos >= tokens_.size()) {
            static Token eof_token{TokenType::EOF_TOKEN};
            return eof_token;
        }
        return tokens_[pos];
    }
    
    void advance() {
        if (current_ < tokens_.size()) {
            current_++;
        }
    }
    
    bool match(TokenType type) {
        if (current_token().type == type) {
            advance();
            return true;
        }
        return false;
    }
    
    void consume(TokenType type, const std::string& error_msg) {
        if (current_token().type != type) {
            throw std::runtime_error(error_msg + " at line " + 
                std::to_string(current_token().line));
        }
        advance();
    }
    
    void skip_newlines() {
        while (match(TokenType::NEWLINE)) {
            // skip
        }
    }
    
    // 语法解析方法
    std::unique_ptr<AST> parse_program() {
        auto program = std::make_unique<AST>();
        program->node_type = ASTNodeType::PROGRAM;
        program->value = "program";
        
        skip_newlines();
        
        // 解析PROGRAM声明
        if (match(TokenType::PROGRAM)) {
            if (current_token().type == TokenType::IDENTIFIER) {
                program->value = current_token().value;
                advance();
            }
            skip_newlines();
        }
        
        // 解析变量声明区
        while (current_token().type == TokenType::VAR ||
               current_token().type == TokenType::VAR_INPUT ||
               current_token().type == TokenType::VAR_OUTPUT ||
               current_token().type == TokenType::VAR_IN_OUT ||
               current_token().type == TokenType::VAR_TEMP) {
            auto var_section = parse_variable_declaration();
            if (var_section) {
                program->children.push_back(std::move(var_section));
            }
            skip_newlines();
        }
        
        // 解析程序体
        while (current_token().type != TokenType::END_PROGRAM &&
               current_token().type != TokenType::EOF_TOKEN) {
            skip_newlines();
            if (current_token().type == TokenType::END_PROGRAM ||
                current_token().type == TokenType::EOF_TOKEN) {
                break;
            }
            
            auto stmt = parse_statement();
            if (stmt) {
                program->children.push_back(std::move(stmt));
            }
            skip_newlines();
        }
        
        // 可选的END_PROGRAM
        if (current_token().type == TokenType::END_PROGRAM) {
            advance();
        }
        
        return program;
    }
    
    std::unique_ptr<AST> parse_variable_declaration() {
        auto var_decl = std::make_unique<AST>();
        var_decl->node_type = ASTNodeType::VARIABLE_DECLARATION;
        
        // 记录变量区类型
        TokenType var_type = current_token().type;
        var_decl->value = current_token().value;
        advance();
        skip_newlines();
        
        // 解析变量列表
        while (current_token().type != TokenType::END_VAR &&
               current_token().type != TokenType::EOF_TOKEN) {
            skip_newlines();
            if (current_token().type == TokenType::END_VAR) {
                break;
            }
            
            auto var = parse_single_variable();
            if (var) {
                var_decl->children.push_back(std::move(var));
            }
            
            if (match(TokenType::SEMICOLON)) {
                skip_newlines();
            }
        }
        
        consume(TokenType::END_VAR, "Expected END_VAR");
        return var_decl;
    }
    
    std::unique_ptr<AST> parse_single_variable() {
        auto var = std::make_unique<AST>();
        var->node_type = ASTNodeType::VARIABLE;
        
        // 变量名
        if (current_token().type != TokenType::IDENTIFIER) {
            errors_.push_back("Expected variable name at line " + 
                std::to_string(current_token().line));
            return nullptr;
        }
        
        var->value = current_token().value;
        advance();
        
        // 数据类型
        if (match(TokenType::COLON)) {
            if (is_data_type(current_token().type)) {
                auto type_node = std::make_unique<AST>();
                type_node->node_type = ASTNodeType::DATA_TYPE;
                type_node->value = current_token().value;
                var->children.push_back(std::move(type_node));
                advance();
            } else {
                errors_.push_back("Expected data type at line " + 
                    std::to_string(current_token().line));
            }
        }
        
        // 可选的初始值
        if (match(TokenType::ASSIGN)) {
            auto init_expr = parse_expression();
            if (init_expr) {
                var->children.push_back(std::move(init_expr));
            }
        }
        
        return var;
    }
    
    std::unique_ptr<AST> parse_statement() {
        skip_newlines();
        
        switch (current_token().type) {
            case TokenType::IF:
                return parse_if_statement();
            case TokenType::WHILE:
                return parse_while_statement();
            case TokenType::FOR:
                return parse_for_statement();
            case TokenType::CASE:
                return parse_case_statement();
            case TokenType::IDENTIFIER:
                return parse_assignment_or_call();
            case TokenType::RETURN:
                return parse_return_statement();
            default:
                // 跳过无法识别的token
                if (current_token().type != TokenType::EOF_TOKEN) {
                    errors_.push_back("Unexpected token '" + current_token().value + 
                        "' at line " + std::to_string(current_token().line));
                    advance();
                }
                return nullptr;
        }
    }
    
    std::unique_ptr<AST> parse_if_statement() {
        auto if_stmt = std::make_unique<AST>();
        if_stmt->node_type = ASTNodeType::IF_STATEMENT;
        
        consume(TokenType::IF, "Expected IF");
        skip_newlines();
        
        // 条件表达式
        auto condition = parse_expression();
        if (condition) {
            if_stmt->children.push_back(std::move(condition));
        }
        
        consume(TokenType::THEN, "Expected THEN");
        skip_newlines();
        
        // THEN分支
        auto then_block = std::make_unique<AST>();
        then_block->node_type = ASTNodeType::BLOCK;
        then_block->value = "then";
        
        while (current_token().type != TokenType::ELSE &&
               current_token().type != TokenType::ELSIF &&
               current_token().type != TokenType::END_IF &&
               current_token().type != TokenType::EOF_TOKEN) {
            auto stmt = parse_statement();
            if (stmt) {
                then_block->children.push_back(std::move(stmt));
            }
            skip_newlines();
        }
        
        if_stmt->children.push_back(std::move(then_block));
        
        // 可选的ELSE分支
        if (match(TokenType::ELSE)) {
            skip_newlines();
            auto else_block = std::make_unique<AST>();
            else_block->node_type = ASTNodeType::BLOCK;
            else_block->value = "else";
            
            while (current_token().type != TokenType::END_IF &&
                   current_token().type != TokenType::EOF_TOKEN) {
                auto stmt = parse_statement();
                if (stmt) {
                    else_block->children.push_back(std::move(stmt));
                }
                skip_newlines();
            }
            
            if_stmt->children.push_back(std::move(else_block));
        }
        
        consume(TokenType::END_IF, "Expected END_IF");
        return if_stmt;
    }
    
    std::unique_ptr<AST> parse_while_statement() {
        auto while_stmt = std::make_unique<AST>();
        while_stmt->node_type = ASTNodeType::WHILE_STATEMENT;
        
        consume(TokenType::WHILE, "Expected WHILE");
        skip_newlines();
        
        // 条件表达式
        auto condition = parse_expression();
        if (condition) {
            while_stmt->children.push_back(std::move(condition));
        }
        
        consume(TokenType::DO, "Expected DO");
        skip_newlines();
        
        // 循环体
        auto body = std::make_unique<AST>();
        body->node_type = ASTNodeType::BLOCK;
        
        while (current_token().type != TokenType::END_WHILE &&
               current_token().type != TokenType::EOF_TOKEN) {
            auto stmt = parse_statement();
            if (stmt) {
                body->children.push_back(std::move(stmt));
            }
            skip_newlines();
        }
        
        while_stmt->children.push_back(std::move(body));
        consume(TokenType::END_WHILE, "Expected END_WHILE");
        
        return while_stmt;
    }
    
    std::unique_ptr<AST> parse_for_statement() {
        auto for_stmt = std::make_unique<AST>();
        for_stmt->node_type = ASTNodeType::FOR_STATEMENT;
        
        consume(TokenType::FOR, "Expected FOR");
        skip_newlines();
        
        // 循环变量
        if (current_token().type == TokenType::IDENTIFIER) {
            auto var = std::make_unique<AST>();
            var->node_type = ASTNodeType::IDENTIFIER;
            var->value = current_token().value;
            for_stmt->children.push_back(std::move(var));
            advance();
        }
        
        consume(TokenType::ASSIGN, "Expected := in FOR loop");
        
        // 起始值
        auto start_expr = parse_expression();
        if (start_expr) {
            for_stmt->children.push_back(std::move(start_expr));
        }
        
        consume(TokenType::TO, "Expected TO");
        
        // 结束值
        auto end_expr = parse_expression();
        if (end_expr) {
            for_stmt->children.push_back(std::move(end_expr));
        }
        
        // 可选的BY步长
        if (match(TokenType::BY)) {
            auto step_expr = parse_expression();
            if (step_expr) {
                for_stmt->children.push_back(std::move(step_expr));
            }
        }
        
        consume(TokenType::DO, "Expected DO");
        skip_newlines();
        
        // 循环体
        auto body = std::make_unique<AST>();
        body->node_type = ASTNodeType::BLOCK;
        
        while (current_token().type != TokenType::END_FOR &&
               current_token().type != TokenType::EOF_TOKEN) {
            auto stmt = parse_statement();
            if (stmt) {
                body->children.push_back(std::move(stmt));
            }
            skip_newlines();
        }
        
        for_stmt->children.push_back(std::move(body));
        consume(TokenType::END_FOR, "Expected END_FOR");
        
        return for_stmt;
    }
    
    std::unique_ptr<AST> parse_case_statement() {
        auto case_stmt = std::make_unique<AST>();
        case_stmt->node_type = ASTNodeType::CASE_STATEMENT;
        
        consume(TokenType::CASE, "Expected CASE");
        skip_newlines();
        
        // 选择表达式
        auto selector = parse_expression();
        if (selector) {
            case_stmt->children.push_back(std::move(selector));
        }
        
        consume(TokenType::OF, "Expected OF");
        skip_newlines();
        
        // CASE分支
        while (current_token().type != TokenType::END_CASE &&
               current_token().type != TokenType::EOF_TOKEN) {
            auto case_branch = parse_case_branch();
            if (case_branch) {
                case_stmt->children.push_back(std::move(case_branch));
            }
            skip_newlines();
        }
        
        consume(TokenType::END_CASE, "Expected END_CASE");
        return case_stmt;
    }
    
    std::unique_ptr<AST> parse_case_branch() {
        auto branch = std::make_unique<AST>();
        branch->node_type = ASTNodeType::CASE_BRANCH;
        
        // 标签值
        auto label = parse_expression();
        if (label) {
            branch->children.push_back(std::move(label));
        }
        
        consume(TokenType::COLON, "Expected : after case label");
        skip_newlines();
        
        // 分支语句
        while (current_token().type != TokenType::INTEGER_LITERAL &&
               current_token().type != TokenType::IDENTIFIER &&
               current_token().type != TokenType::END_CASE &&
               current_token().type != TokenType::EOF_TOKEN) {
            auto stmt = parse_statement();
            if (stmt) {
                branch->children.push_back(std::move(stmt));
            }
            skip_newlines();
        }
        
        return branch;
    }
    
    std::unique_ptr<AST> parse_assignment_or_call() {
        auto expr = std::make_unique<AST>();
        
        std::string name = current_token().value;
        advance();
        
        if (match(TokenType::ASSIGN)) {
            // 赋值语句
            expr->node_type = ASTNodeType::ASSIGNMENT;
            
            auto lhs = std::make_unique<AST>();
            lhs->node_type = ASTNodeType::IDENTIFIER;
            lhs->value = name;
            expr->children.push_back(std::move(lhs));
            
            auto rhs = parse_expression();
            if (rhs) {
                expr->children.push_back(std::move(rhs));
            }
        } else if (match(TokenType::LPAREN)) {
            // 函数调用
            expr->node_type = ASTNodeType::FUNCTION_CALL;
            expr->value = name;
            
            // 参数列表
            if (current_token().type != TokenType::RPAREN) {
                do {
                    auto arg = parse_expression();
                    if (arg) {
                        expr->children.push_back(std::move(arg));
                    }
                } while (match(TokenType::COMMA));
            }
            
            consume(TokenType::RPAREN, "Expected )");
        } else {
            // 单独的标识符作为表达式语句
            expr->node_type = ASTNodeType::IDENTIFIER;
            expr->value = name;
        }
        
        return expr;
    }
    
    std::unique_ptr<AST> parse_return_statement() {
        auto ret_stmt = std::make_unique<AST>();
        ret_stmt->node_type = ASTNodeType::RETURN_STATEMENT;
        
        consume(TokenType::RETURN, "Expected RETURN");
        
        // 可选的返回值
        if (current_token().type != TokenType::SEMICOLON &&
            current_token().type != TokenType::NEWLINE &&
            current_token().type != TokenType::EOF_TOKEN) {
            auto expr = parse_expression();
            if (expr) {
                ret_stmt->children.push_back(std::move(expr));
            }
        }
        
        return ret_stmt;
    }
    
    std::unique_ptr<AST> parse_expression() {
        return parse_logical_or();
    }
    
    std::unique_ptr<AST> parse_logical_or() {
        auto left = parse_logical_and();
        
        while (match(TokenType::OR)) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = "OR";
            op->children.push_back(std::move(left));
            
            auto right = parse_logical_and();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_logical_and() {
        auto left = parse_equality();
        
        while (match(TokenType::AND)) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = "AND";
            op->children.push_back(std::move(left));
            
            auto right = parse_equality();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_equality() {
        auto left = parse_relational();
        
        while (current_token().type == TokenType::EQUAL ||
               current_token().type == TokenType::NOT_EQUAL) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = current_token().value;
            advance();
            
            op->children.push_back(std::move(left));
            
            auto right = parse_relational();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_relational() {
        auto left = parse_additive();
        
        while (current_token().type == TokenType::LESS_THAN ||
               current_token().type == TokenType::LESS_EQUAL ||
               current_token().type == TokenType::GREATER_THAN ||
               current_token().type == TokenType::GREATER_EQUAL) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = current_token().value;
            advance();
            
            op->children.push_back(std::move(left));
            
            auto right = parse_additive();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_additive() {
        auto left = parse_multiplicative();
        
        while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = tokens_[current_ - 1].value; // 获取刚刚匹配的操作符
            
            op->children.push_back(std::move(left));
            
            auto right = parse_multiplicative();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_multiplicative() {
        auto left = parse_unary();
        
        while (match(TokenType::MULTIPLY) || match(TokenType::DIVIDE) || match(TokenType::MODULO)) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::BINARY_OPERATION;
            op->value = tokens_[current_ - 1].value;
            
            op->children.push_back(std::move(left));
            
            auto right = parse_unary();
            if (right) {
                op->children.push_back(std::move(right));
            }
            
            left = std::move(op);
        }
        
        return left;
    }
    
    std::unique_ptr<AST> parse_unary() {
        if (match(TokenType::NOT) || match(TokenType::MINUS)) {
            auto op = std::make_unique<AST>();
            op->node_type = ASTNodeType::UNARY_OPERATION;
            op->value = tokens_[current_ - 1].value;
            
            auto operand = parse_unary();
            if (operand) {
                op->children.push_back(std::move(operand));
            }
            
            return op;
        }
        
        return parse_primary();
    }
    
    std::unique_ptr<AST> parse_primary() {
        auto expr = std::make_unique<AST>();
        
        switch (current_token().type) {
            case TokenType::INTEGER_LITERAL:
                expr->node_type = ASTNodeType::LITERAL;
                expr->value = current_token().value;
                expr->data_type = "INT";
                advance();
                break;
                
            case TokenType::REAL_LITERAL:
                expr->node_type = ASTNodeType::LITERAL;
                expr->value = current_token().value;
                expr->data_type = "REAL";
                advance();
                break;
                
            case TokenType::STRING_LITERAL:
                expr->node_type = ASTNodeType::LITERAL;
                expr->value = current_token().value;
                expr->data_type = "STRING";
                advance();
                break;
                
            case TokenType::BOOL_LITERAL:
                expr->node_type = ASTNodeType::LITERAL;
                expr->value = current_token().value;
                expr->data_type = "BOOL";
                advance();
                break;
                
            case TokenType::IDENTIFIER:
                expr->node_type = ASTNodeType::IDENTIFIER;
                expr->value = current_token().value;
                advance();
                break;
                
            case TokenType::LPAREN:
                advance(); // skip '('
                expr = parse_expression();
                consume(TokenType::RPAREN, "Expected )");
                break;
                
            default:
                errors_.push_back("Unexpected token in expression: " + current_token().value +
                    " at line " + std::to_string(current_token().line));
                return nullptr;
        }
        
        return expr;
    }
    
    bool is_data_type(TokenType type) const {
        return type == TokenType::BOOL || type == TokenType::SINT || 
               type == TokenType::INT || type == TokenType::DINT ||
               type == TokenType::LINT || type == TokenType::USINT ||
               type == TokenType::UINT || type == TokenType::UDINT ||
               type == TokenType::ULINT || type == TokenType::REAL ||
               type == TokenType::LREAL || type == TokenType::STRING ||
               type == TokenType::TIME || type == TokenType::DATE ||
               type == TokenType::BYTE || type == TokenType::WORD ||
               type == TokenType::DWORD || type == TokenType::LWORD;
    }
};

// Parser类实现
std::unique_ptr<AST> Parser::parse(const std::string& source) {
    // 首先进行词法分析
    LexerImpl lexer(source);
    auto tokens = lexer.tokenize();
    
    // 然后进行语法分析
    ParserImpl parser(tokens);
    return parser.parse();
}

} // namespace st_compiler
} // namespace plc_runtime