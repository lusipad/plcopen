/**
 * @file Lexer.cpp
 * @brief ST词法分析器实现 - IEC 61131-3标准支持
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include "st_compiler/Lexer.h"
#include "st_compiler/AST.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief ST词法分析器实现类
 */
class LexerImpl {
private:
    std::string source_;
    size_t pos_;
    size_t line_;
    size_t column_;
    std::vector<Token> tokens_;
    
    // 关键字映射表
    static const std::unordered_map<std::string, TokenType> keywords_;
    
public:
    explicit LexerImpl(const std::string& source)
        : source_(source), pos_(0), line_(1), column_(1) {}
    
    std::vector<Token> tokenize() {
        tokens_.clear();
        pos_ = 0;
        line_ = 1;
        column_ = 1;
        
        while (pos_ < source_.length()) {
            skip_whitespace();
            if (pos_ >= source_.length()) break;
            
            char ch = source_[pos_];
            
            // 处理注释
            if (ch == '(' && peek() == '*') {
                tokenize_block_comment();
                continue;
            }
            if (ch == '/' && peek() == '/') {
                tokenize_line_comment();
                continue;
            }
            
            // 处理数字字面量
            if (std::isdigit(ch)) {
                tokenize_number();
                continue;
            }
            
            // 处理字符串字面量
            if (ch == '\'' || ch == '"') {
                tokenize_string();
                continue;
            }
            
            // 处理标识符和关键字
            if (std::isalpha(ch) || ch == '_') {
                tokenize_identifier();
                continue;
            }
            
            // 处理运算符和分隔符
            if (tokenize_operator()) {
                continue;
            }
            
            // 处理换行
            if (ch == '\n') {
                add_token(TokenType::NEWLINE, "\\n");
                advance();
                line_++;
                column_ = 1;
                continue;
            }
            
            // 无法识别的字符
            add_token(TokenType::INVALID, std::string(1, ch));
            advance();
        }
        
        add_token(TokenType::EOF_TOKEN, "");
        return tokens_;
    }
    
private:
    char current() const {
        return pos_ < source_.length() ? source_[pos_] : '\0';
    }
    
    char peek(size_t offset = 1) const {
        size_t peek_pos = pos_ + offset;
        return peek_pos < source_.length() ? source_[peek_pos] : '\0';
    }
    
    void advance() {
        if (pos_ < source_.length()) {
            pos_++;
            column_++;
        }
    }
    
    void skip_whitespace() {
        while (pos_ < source_.length() && std::isspace(current()) && current() != '\n') {
            advance();
        }
    }
    
    void add_token(TokenType type, const std::string& value) {
        tokens_.emplace_back(type, value, line_, column_ - value.length(), pos_);
    }
    
    void tokenize_block_comment() {
        std::string comment = "(*";
        advance(); // skip '('
        advance(); // skip '*'
        
        while (pos_ < source_.length()) {
            char ch = current();
            comment += ch;
            advance();
            
            if (ch == '*' && current() == ')') {
                comment += current();
                advance();
                break;
            }
            
            if (ch == '\n') {
                line_++;
                column_ = 1;
            }
        }
        
        add_token(TokenType::COMMENT, comment);
    }
    
    void tokenize_line_comment() {
        std::string comment = "//";
        advance(); // skip first '/'
        advance(); // skip second '/'
        
        while (pos_ < source_.length() && current() != '\n') {
            comment += current();
            advance();
        }
        
        add_token(TokenType::COMMENT, comment);
    }
    
    void tokenize_number() {
        std::string number;
        TokenType type = TokenType::INTEGER_LITERAL;
        
        // 检查进制前缀 (16#, 8#, 2#)
        if (std::isdigit(current())) {
            // 收集数字
            while (pos_ < source_.length() && std::isdigit(current())) {
                number += current();
                advance();
            }
            
            // 检查是否是进制前缀
            if (current() == '#') {
                number += current();
                advance();
                
                // 收集进制数字
                while (pos_ < source_.length() && 
                       (std::isalnum(current()) || current() == '_')) {
                    number += current();
                    advance();
                }
                
                add_token(TokenType::INTEGER_LITERAL, number);
                return;
            }
        }
        
        // 检查小数点
        if (current() == '.') {
            type = TokenType::REAL_LITERAL;
            number += current();
            advance();
            
            while (pos_ < source_.length() && std::isdigit(current())) {
                number += current();
                advance();
            }
        }
        
        // 检查科学记数法
        if (current() == 'E' || current() == 'e') {
            type = TokenType::REAL_LITERAL;
            number += current();
            advance();
            
            if (current() == '+' || current() == '-') {
                number += current();
                advance();
            }
            
            while (pos_ < source_.length() && std::isdigit(current())) {
                number += current();
                advance();
            }
        }
        
        add_token(type, number);
    }
    
    void tokenize_string() {
        char quote = current();
        std::string str;
        str += quote;
        advance();
        
        while (pos_ < source_.length() && current() != quote) {
            if (current() == '\\') {
                str += current();
                advance();
                if (pos_ < source_.length()) {
                    str += current();
                    advance();
                }
            } else {
                str += current();
                advance();
            }
        }
        
        if (pos_ < source_.length()) {
            str += current(); // closing quote
            advance();
        }
        
        add_token(TokenType::STRING_LITERAL, str);
    }
    
    void tokenize_identifier() {
        std::string identifier;
        
        while (pos_ < source_.length() && 
               (std::isalnum(current()) || current() == '_')) {
            identifier += current();
            advance();
        }
        
        // 转换为大写进行关键字匹配
        std::string upper_id = identifier;
        std::transform(upper_id.begin(), upper_id.end(), upper_id.begin(), ::toupper);
        
        // 检查是否是关键字
        auto it = keywords_.find(upper_id);
        if (it != keywords_.end()) {
            add_token(it->second, identifier);
        } else {
            add_token(TokenType::IDENTIFIER, identifier);
        }
    }
    
    bool tokenize_operator() {
        char ch = current();
        
        switch (ch) {
            case ':':
                if (peek() == '=') {
                    add_token(TokenType::ASSIGN, ":=");
                    advance();
                    advance();
                } else {
                    add_token(TokenType::COLON, ":");
                    advance();
                }
                return true;
                
            case '<':
                if (peek() == '=') {
                    add_token(TokenType::LESS_EQUAL, "<=");
                    advance();
                    advance();
                } else if (peek() == '>') {
                    add_token(TokenType::NOT_EQUAL, "<>");
                    advance();
                    advance();
                } else {
                    add_token(TokenType::LESS_THAN, "<");
                    advance();
                }
                return true;
                
            case '>':
                if (peek() == '=') {
                    add_token(TokenType::GREATER_EQUAL, ">=");
                    advance();
                    advance();
                } else {
                    add_token(TokenType::GREATER_THAN, ">");
                    advance();
                }
                return true;
                
            case '*':
                if (peek() == '*') {
                    add_token(TokenType::POWER, "**");
                    advance();
                    advance();
                } else {
                    add_token(TokenType::MULTIPLY, "*");
                    advance();
                }
                return true;
                
            case '.':
                if (peek() == '.') {
                    add_token(TokenType::DOUBLE_DOT, "..");
                    advance();
                    advance();
                } else {
                    add_token(TokenType::DOT, ".");
                    advance();
                }
                return true;
                
            case '+': add_token(TokenType::PLUS, "+"); advance(); return true;
            case '-': add_token(TokenType::MINUS, "-"); advance(); return true;
            case '/': add_token(TokenType::DIVIDE, "/"); advance(); return true;
            case '=': add_token(TokenType::EQUAL, "="); advance(); return true;
            case ';': add_token(TokenType::SEMICOLON, ";"); advance(); return true;
            case ',': add_token(TokenType::COMMA, ","); advance(); return true;
            case '(': add_token(TokenType::LPAREN, "("); advance(); return true;
            case ')': add_token(TokenType::RPAREN, ")"); advance(); return true;
            case '[': add_token(TokenType::LBRACKET, "["); advance(); return true;
            case ']': add_token(TokenType::RBRACKET, "]"); advance(); return true;
        }
        
        return false;
    }
};

// 关键字映射表定义
const std::unordered_map<std::string, TokenType> LexerImpl::keywords_ = {
    // 数据类型
    {"BOOL", TokenType::BOOL}, {"SINT", TokenType::SINT}, {"INT", TokenType::INT},
    {"DINT", TokenType::DINT}, {"LINT", TokenType::LINT}, {"USINT", TokenType::USINT},
    {"UINT", TokenType::UINT}, {"UDINT", TokenType::UDINT}, {"ULINT", TokenType::ULINT},
    {"REAL", TokenType::REAL}, {"LREAL", TokenType::LREAL}, {"STRING", TokenType::STRING},
    {"WSTRING", TokenType::WSTRING}, {"TIME", TokenType::TIME}, {"DATE", TokenType::DATE},
    {"DT", TokenType::DT}, {"TOD", TokenType::TOD}, {"BYTE", TokenType::BYTE},
    {"WORD", TokenType::WORD}, {"DWORD", TokenType::DWORD}, {"LWORD", TokenType::LWORD},
    
    // 程序结构
    {"PROGRAM", TokenType::PROGRAM}, {"END_PROGRAM", TokenType::END_PROGRAM},
    {"FUNCTION", TokenType::FUNCTION}, {"END_FUNCTION", TokenType::END_FUNCTION},
    {"FUNCTION_BLOCK", TokenType::FUNCTION_BLOCK}, {"END_FUNCTION_BLOCK", TokenType::END_FUNCTION_BLOCK},
    {"VAR", TokenType::VAR}, {"VAR_INPUT", TokenType::VAR_INPUT}, {"VAR_OUTPUT", TokenType::VAR_OUTPUT},
    {"VAR_IN_OUT", TokenType::VAR_IN_OUT}, {"VAR_TEMP", TokenType::VAR_TEMP},
    {"VAR_GLOBAL", TokenType::VAR_GLOBAL}, {"VAR_EXTERNAL", TokenType::VAR_EXTERNAL},
    {"END_VAR", TokenType::END_VAR}, {"CONSTANT", TokenType::CONSTANT},
    {"RETAIN", TokenType::RETAIN}, {"AT", TokenType::AT},
    
    // 控制结构
    {"IF", TokenType::IF}, {"THEN", TokenType::THEN}, {"ELSE", TokenType::ELSE},
    {"ELSIF", TokenType::ELSIF}, {"END_IF", TokenType::END_IF}, {"CASE", TokenType::CASE},
    {"OF", TokenType::OF}, {"END_CASE", TokenType::END_CASE}, {"FOR", TokenType::FOR},
    {"TO", TokenType::TO}, {"BY", TokenType::BY}, {"DO", TokenType::DO},
    {"END_FOR", TokenType::END_FOR}, {"WHILE", TokenType::WHILE}, {"END_WHILE", TokenType::END_WHILE},
    {"REPEAT", TokenType::REPEAT}, {"UNTIL", TokenType::UNTIL}, {"END_REPEAT", TokenType::END_REPEAT},
    {"EXIT", TokenType::EXIT}, {"CONTINUE", TokenType::CONTINUE}, {"RETURN", TokenType::RETURN},
    
    // 逻辑运算符
    {"AND", TokenType::AND}, {"OR", TokenType::OR}, {"XOR", TokenType::XOR},
    {"NOT", TokenType::NOT}, {"AND_THEN", TokenType::AND_THEN}, {"OR_ELSE", TokenType::OR_ELSE},
    
    // 布尔字面量
    {"TRUE", TokenType::BOOL_LITERAL}, {"FALSE", TokenType::BOOL_LITERAL},
};

// Lexer类实现
std::unique_ptr<AST> Lexer::analyze(const std::string& source) {
    LexerImpl lexer(source);
    auto tokens = lexer.tokenize();
    
    // 创建AST根节点
    auto ast = std::make_unique<AST>();
    auto program_node = std::make_unique<ProgramNode>("lexer_output", 1, 1);
    
    // 将tokens信息添加到AST中（简化实现）
    for (const auto& token : tokens) {
        if (token.type == TokenType::EOF_TOKEN || token.type == TokenType::NEWLINE) {
            continue;
        }
        
        auto token_node = std::make_unique<LiteralNode>(ASTNodeType::IDENTIFIER, token.value, 
                                                       token.line, token.column);
        program_node->add_child(std::move(token_node));
    }
    
    ast->set_root(std::move(program_node));
    return ast;
}

} // namespace st_compiler
} // namespace plc_runtime