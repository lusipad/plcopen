/**
 * @file Lexer.h  
 * @brief ST词法分析器
 */

#pragma once

#include "AST.h"
#include "SymbolTable.h"
#include <memory>
#include <string>
#include <vector>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief ST语言token类型
 */
enum class TokenType {
    // 字面量
    INTEGER_LITERAL,      // 123, 16#FF, 8#177, 2#1010
    REAL_LITERAL,         // 3.14, 1.23E-4
    STRING_LITERAL,       // 'Hello', "World"
    BOOL_LITERAL,         // TRUE, FALSE
    TIME_LITERAL,         // T#1s, TIME#100ms
    
    // 标识符和关键字
    IDENTIFIER,           // variable_name
    
    // 数据类型关键字
    BOOL, SINT, INT, DINT, LINT,
    USINT, UINT, UDINT, ULINT,
    REAL, LREAL,
    STRING, WSTRING,
    TIME, DATE, DT, TOD,
    BYTE, WORD, DWORD, LWORD,
    
    // 程序结构关键字
    PROGRAM, END_PROGRAM,
    FUNCTION, END_FUNCTION,
    FUNCTION_BLOCK, END_FUNCTION_BLOCK,
    VAR, VAR_INPUT, VAR_OUTPUT, VAR_IN_OUT,
    VAR_TEMP, VAR_GLOBAL, VAR_EXTERNAL,
    END_VAR, CONSTANT, RETAIN, AT,
    
    // 控制结构关键字
    IF, THEN, ELSE, ELSIF, END_IF,
    CASE, OF, END_CASE,
    FOR, TO, BY, DO, END_FOR,
    WHILE, END_WHILE,
    REPEAT, UNTIL, END_REPEAT,
    EXIT, CONTINUE, RETURN,
    
    // 运算符
    ASSIGN,               // :=
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO,
    POWER,                // **
    
    // 比较运算符
    EQUAL, NOT_EQUAL,     // =, <>
    LESS_THAN, LESS_EQUAL,
    GREATER_THAN, GREATER_EQUAL,
    
    // 逻辑运算符
    AND, OR, XOR, NOT,
    
    // 位运算符
    AND_THEN, OR_ELSE,
    
    // 分隔符
    SEMICOLON, COMMA, COLON,
    DOT, DOUBLE_DOT,      // ., ..
    
    // 括号
    LPAREN, RPAREN,       // ()
    LBRACKET, RBRACKET,   // []
    
    // 特殊
    NEWLINE, COMMENT,
    EOF_TOKEN, INVALID,
};

/**
 * @brief Token结构体
 */
struct Token {
    TokenType type;
    std::string value;
    size_t line;
    size_t column;
    size_t position;
    
    Token(TokenType t = TokenType::INVALID, const std::string& v = "", 
          size_t l = 1, size_t c = 1, size_t p = 0)
        : type(t), value(v), line(l), column(c), position(p) {}
};

class Lexer {
public:
    std::unique_ptr<AST> analyze(const std::string& source);
};

class Parser {
public:
    std::unique_ptr<AST> parse(const std::string& source);
};

class SemanticAnalyzer {
public:
    bool analyze(AST* ast, SymbolTable* symbol_table);
};

class CodeGenerator {
public:
    std::unique_ptr<CompiledProgram> generate(AST* ast, SymbolTable* symbol_table, const CompileOptions& options);
};

} // namespace st_compiler
} // namespace plc_runtime