/**
 * @file Lexer.h  
 * @brief ST词法分析器接口 - 重构后的现代设计
 * @version MVP-1.1
 * @date 2025-09-06
 */

#pragma once

#include "STCompiler.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace plc_runtime {
namespace st_compiler {

// 前向声明
class AST;
class SymbolTable;

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
        
    // 便利方法
    bool is_literal() const;
    bool is_keyword() const;
    bool is_operator() const;
    bool is_identifier() const;
    std::string to_string() const;
};

/**
 * @brief 词法分析结果
 */
struct LexicalAnalysisResult {
    std::vector<Token> tokens;
    std::vector<CompileError> errors;
    size_t total_lines;
    size_t total_characters;
    std::chrono::milliseconds analysis_time;
    
    bool has_errors() const { return !errors.empty(); }
};

/**
 * @brief ST词法分析器类
 */
class Lexer {
public:
    /**
     * @brief 构造函数
     */
    explicit Lexer(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 析构函数
     */
    ~Lexer();
    
    /**
     * @brief 词法分析主接口 - 返回Token流
     * @param source ST源代码
     * @return 词法分析结果
     */
    LexicalAnalysisResult tokenize(const std::string& source);
    
    /**
     * @brief 兼容性接口 - 直接返回Token向量
     * @param source ST源代码
     * @return Token向量
     */
    std::vector<Token> tokenize_simple(const std::string& source);
    
    /**
     * @brief 获取关键字映射表
     */
    static const std::unordered_map<std::string, TokenType>& get_keywords();
    
    /**
     * @brief 检查是否为关键字
     */
    static bool is_keyword(const std::string& word);
    
    /**
     * @brief 获取Token类型名称
     */
    static std::string get_token_type_name(TokenType type);
    
    /**
     * @brief 设置选项
     */
    void set_options(const CompileOptions& options);
    
private:
    class LexerImpl;
    std::unique_ptr<LexerImpl> impl_;
};

/**
 * @brief ST语法分析器类
 */
class Parser {
public:
    /**
     * @brief 构造函数
     */
    explicit Parser(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 析构函数
     */
    ~Parser();
    
    /**
     * @brief 语法分析主接口
     * @param tokens Token流
     * @return AST树，失败返回nullptr
     */
    std::unique_ptr<AST> parse(const std::vector<Token>& tokens);
    
    /**
     * @brief 便利接口 - 直接从源代码解析
     * @param source ST源代码
     * @return AST树，失败返回nullptr
     */
    std::unique_ptr<AST> parse_source(const std::string& source);
    
    /**
     * @brief 获取解析错误
     */
    const std::vector<CompileError>& get_errors() const;
    
    /**
     * @brief 清除错误
     */
    void clear_errors();
    
private:
    class ParserImpl;
    std::unique_ptr<ParserImpl> impl_;
};

/**
 * @brief ST语义分析器类
 */
class SemanticAnalyzer {
public:
    /**
     * @brief 构造函数
     */
    explicit SemanticAnalyzer(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 析构函数
     */
    ~SemanticAnalyzer();
    
    /**
     * @brief 语义分析主接口
     * @param ast AST树
     * @param symbol_table 符号表
     * @return 分析成功返回true
     */
    bool analyze(AST* ast, SymbolTable* symbol_table);
    
    /**
     * @brief 获取分析错误
     */
    const std::vector<CompileError>& get_errors() const;
    
    /**
     * @brief 获取分析警告
     */
    const std::vector<CompileError>& get_warnings() const;
    
private:
    class SemanticAnalyzerImpl;
    std::unique_ptr<SemanticAnalyzerImpl> impl_;
};

/**
 * @brief ST代码生成器类
 */
class CodeGenerator {
public:
    /**
     * @brief 构造函数
     */
    explicit CodeGenerator(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 析构函数
     */
    ~CodeGenerator();
    
    /**
     * @brief 代码生成主接口
     * @param ast AST树
     * @param symbol_table 符号表
     * @param options 编译选项
     * @return 编译后程序，失败返回nullptr
     */
    std::unique_ptr<CompiledProgram> generate(AST* ast, SymbolTable* symbol_table, const CompileOptions& options);
    
    /**
     * @brief 获取生成错误
     */
    const std::vector<CompileError>& get_errors() const;
    
private:
    class CodeGeneratorImpl;
    std::unique_ptr<CodeGeneratorImpl> impl_;
};

} // namespace st_compiler
} // namespace plc_runtime