/**
 * @file Lexer.cpp
 * @brief ST词法分析器实现 - 重构后的现代设计
 * @version MVP-1.1  
 * @date 2025-09-06
 */

#include "st_compiler/Lexer.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <chrono>

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
const std::unordered_map<std::string, TokenType> Lexer::LexerImpl::keywords_ = {
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

// Lexer静态方法实现
const std::unordered_map<std::string, TokenType>& Lexer::get_keywords() {
    return LexerImpl::keywords_;
}

bool Lexer::is_keyword(const std::string& word) {
    std::string upper_word = word;
    std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);
    return LexerImpl::keywords_.find(upper_word) != LexerImpl::keywords_.end();
}

std::string Lexer::get_token_type_name(TokenType type) {
    static const std::unordered_map<TokenType, std::string> type_names = {
        {TokenType::INTEGER_LITERAL, "INTEGER_LITERAL"},
        {TokenType::REAL_LITERAL, "REAL_LITERAL"},
        {TokenType::STRING_LITERAL, "STRING_LITERAL"},
        {TokenType::BOOL_LITERAL, "BOOL_LITERAL"},
        {TokenType::TIME_LITERAL, "TIME_LITERAL"},
        {TokenType::IDENTIFIER, "IDENTIFIER"},
        {TokenType::BOOL, "BOOL"},
        {TokenType::SINT, "SINT"},
        {TokenType::INT, "INT"},
        {TokenType::DINT, "DINT"},
        {TokenType::LINT, "LINT"},
        {TokenType::USINT, "USINT"},
        {TokenType::UINT, "UINT"},
        {TokenType::UDINT, "UDINT"},
        {TokenType::ULINT, "ULINT"},
        {TokenType::REAL, "REAL"},
        {TokenType::LREAL, "LREAL"},
        {TokenType::STRING, "STRING"},
        {TokenType::WSTRING, "WSTRING"},
        {TokenType::TIME, "TIME"},
        {TokenType::DATE, "DATE"},
        {TokenType::DT, "DT"},
        {TokenType::TOD, "TOD"},
        {TokenType::BYTE, "BYTE"},
        {TokenType::WORD, "WORD"},
        {TokenType::DWORD, "DWORD"},
        {TokenType::LWORD, "LWORD"},
        {TokenType::PROGRAM, "PROGRAM"},
        {TokenType::END_PROGRAM, "END_PROGRAM"},
        {TokenType::FUNCTION, "FUNCTION"},
        {TokenType::END_FUNCTION, "END_FUNCTION"},
        {TokenType::FUNCTION_BLOCK, "FUNCTION_BLOCK"},
        {TokenType::END_FUNCTION_BLOCK, "END_FUNCTION_BLOCK"},
        {TokenType::VAR, "VAR"},
        {TokenType::VAR_INPUT, "VAR_INPUT"},
        {TokenType::VAR_OUTPUT, "VAR_OUTPUT"},
        {TokenType::VAR_IN_OUT, "VAR_IN_OUT"},
        {TokenType::VAR_TEMP, "VAR_TEMP"},
        {TokenType::VAR_GLOBAL, "VAR_GLOBAL"},
        {TokenType::VAR_EXTERNAL, "VAR_EXTERNAL"},
        {TokenType::END_VAR, "END_VAR"},
        {TokenType::CONSTANT, "CONSTANT"},
        {TokenType::RETAIN, "RETAIN"},
        {TokenType::AT, "AT"},
        {TokenType::IF, "IF"},
        {TokenType::THEN, "THEN"},
        {TokenType::ELSE, "ELSE"},
        {TokenType::ELSIF, "ELSIF"},
        {TokenType::END_IF, "END_IF"},
        {TokenType::CASE, "CASE"},
        {TokenType::OF, "OF"},
        {TokenType::END_CASE, "END_CASE"},
        {TokenType::FOR, "FOR"},
        {TokenType::TO, "TO"},
        {TokenType::BY, "BY"},
        {TokenType::DO, "DO"},
        {TokenType::END_FOR, "END_FOR"},
        {TokenType::WHILE, "WHILE"},
        {TokenType::END_WHILE, "END_WHILE"},
        {TokenType::REPEAT, "REPEAT"},
        {TokenType::UNTIL, "UNTIL"},
        {TokenType::END_REPEAT, "END_REPEAT"},
        {TokenType::EXIT, "EXIT"},
        {TokenType::CONTINUE, "CONTINUE"},
        {TokenType::RETURN, "RETURN"},
        {TokenType::ASSIGN, "ASSIGN"},
        {TokenType::PLUS, "PLUS"},
        {TokenType::MINUS, "MINUS"},
        {TokenType::MULTIPLY, "MULTIPLY"},
        {TokenType::DIVIDE, "DIVIDE"},
        {TokenType::MODULO, "MODULO"},
        {TokenType::POWER, "POWER"},
        {TokenType::EQUAL, "EQUAL"},
        {TokenType::NOT_EQUAL, "NOT_EQUAL"},
        {TokenType::LESS_THAN, "LESS_THAN"},
        {TokenType::LESS_EQUAL, "LESS_EQUAL"},
        {TokenType::GREATER_THAN, "GREATER_THAN"},
        {TokenType::GREATER_EQUAL, "GREATER_EQUAL"},
        {TokenType::AND, "AND"},
        {TokenType::OR, "OR"},
        {TokenType::XOR, "XOR"},
        {TokenType::NOT, "NOT"},
        {TokenType::AND_THEN, "AND_THEN"},
        {TokenType::OR_ELSE, "OR_ELSE"},
        {TokenType::SEMICOLON, "SEMICOLON"},
        {TokenType::COMMA, "COMMA"},
        {TokenType::COLON, "COLON"},
        {TokenType::DOT, "DOT"},
        {TokenType::DOUBLE_DOT, "DOUBLE_DOT"},
        {TokenType::LPAREN, "LPAREN"},
        {TokenType::RPAREN, "RPAREN"},
        {TokenType::LBRACKET, "LBRACKET"},
        {TokenType::RBRACKET, "RBRACKET"},
        {TokenType::NEWLINE, "NEWLINE"},
        {TokenType::COMMENT, "COMMENT"},
        {TokenType::EOF_TOKEN, "EOF_TOKEN"},
        {TokenType::INVALID, "INVALID"}
    };
    
    auto it = type_names.find(type);
    return it != type_names.end() ? it->second : "UNKNOWN";
}

// Token工具方法实现
bool Token::is_literal() const {
    return type == TokenType::INTEGER_LITERAL || 
           type == TokenType::REAL_LITERAL ||
           type == TokenType::STRING_LITERAL ||
           type == TokenType::BOOL_LITERAL ||
           type == TokenType::TIME_LITERAL;
}

bool Token::is_keyword() const {
    return static_cast<int>(type) >= static_cast<int>(TokenType::BOOL) &&
           static_cast<int>(type) <= static_cast<int>(TokenType::RETURN);
}

bool Token::is_operator() const {
    return type >= TokenType::ASSIGN && type <= TokenType::OR_ELSE;
}

bool Token::is_identifier() const {
    return type == TokenType::IDENTIFIER;
}

std::string Token::to_string() const {
    return "Token(" + Lexer::get_token_type_name(type) + ", '" + value + "', " +
           std::to_string(line) + ":" + std::to_string(column) + ")";
}

// Lexer类实现
class Lexer::LexerImpl {
private:
    std::string source_;
    size_t pos_;
    size_t line_;
    size_t column_;
    std::vector<Token> tokens_;
    std::vector<CompileError> errors_;
    CompileOptions options_;
    
    // 关键字映射表
    static const std::unordered_map<std::string, TokenType> keywords_;
    
public:
    explicit LexerImpl(const CompileOptions& options = CompileOptions{})
        : pos_(0), line_(1), column_(1), options_(options) {}
        
    LexicalAnalysisResult tokenize(const std::string& source) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        source_ = source;
        tokens_.clear();
        errors_.clear();
        pos_ = 0;
        line_ = 1;
        column_ = 1;
        
        while (pos_ < source_.length()) {
            skip_whitespace();
            if (pos_ >= source_.length()) break;
            
            char ch = source_[pos_];
            
            try {
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
                    if (!options_.case_sensitive) {  // 根据选项决定是否保留换行符
                        add_token(TokenType::NEWLINE, "\\n");
                    }
                    advance();
                    line_++;
                    column_ = 1;
                    continue;
                }
                
                // 无法识别的字符
                add_error("Unexpected character: '" + std::string(1, ch) + "'");
                advance();
            } catch (const std::exception& e) {
                add_error("Lexical analysis error: " + std::string(e.what()));
                advance(); // 尝试恢复
            }
        }
        
        add_token(TokenType::EOF_TOKEN, "");
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        LexicalAnalysisResult result;
        result.tokens = std::move(tokens_);
        result.errors = std::move(errors_);
        result.total_lines = line_;
        result.total_characters = source_.length();
        result.analysis_time = duration;
        
        return result;
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
    
    void add_error(const std::string& message) {
        errors_.emplace_back(CompileError::Type::LEXICAL, line_, column_, message);
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
        } else {
            add_error("Unterminated string literal");
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

// Lexer公开接口实现
Lexer::Lexer(const CompileOptions& options) 
    : impl_(std::make_unique<LexerImpl>(options)) {
}

Lexer::~Lexer() = default;

LexicalAnalysisResult Lexer::tokenize(const std::string& source) {
    return impl_->tokenize(source);
}

std::vector<Token> Lexer::tokenize_simple(const std::string& source) {
    auto result = impl_->tokenize(source);
    return std::move(result.tokens);
}

void Lexer::set_options(const CompileOptions& options) {
    impl_ = std::make_unique<LexerImpl>(options);
}

} // namespace st_compiler
} // namespace plc_runtime