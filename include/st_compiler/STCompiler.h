/**
 * @file STCompiler.h
 * @brief ST编译器主接口 - IEC 61131-3 ST语言支持
 * @version MVP-1.0
 * @date 2025-09-04
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace plc_runtime {
namespace st_compiler {

// 前向声明
class Lexer;
class Parser;
class SemanticAnalyzer;
class CodeGenerator;
class SymbolTable;
class AST;

/**
 * @brief ST数据类型
 */
enum class STDataType : uint8_t {
    BOOL = 1,       // 布尔类型
    SINT = 2,       // 短整型 (-128..127)
    INT = 3,        // 整型 (-32768..32767)  
    DINT = 4,       // 双整型 (-2^31..2^31-1)
    LINT = 5,       // 长整型 (-2^63..2^63-1)
    USINT = 6,      // 无符号短整型 (0..255)
    UINT = 7,       // 无符号整型 (0..65535)
    UDINT = 8,      // 无符号双整型 (0..2^32-1)
    ULINT = 9,      // 无符号长整型 (0..2^64-1)
    REAL = 10,      // 实数类型 (32位浮点)
    LREAL = 11,     // 长实数类型 (64位浮点)
    STRING = 12,    // 字符串类型
    TIME = 13,      // 时间类型
    DATE = 14,      // 日期类型
    TOD = 15,       // 时刻类型
    DT = 16,        // 日期时间类型
    ARRAY = 17,     // 数组类型
    STRUCT = 18,    // 结构体类型
    UNKNOWN = 255   // 未知类型
};

/**
 * @brief 变量存储类别
 */
enum class StorageClass : uint8_t {
    VAR = 0,        // 普通变量
    VAR_INPUT = 1,  // 输入变量
    VAR_OUTPUT = 2, // 输出变量
    VAR_IN_OUT = 3, // 输入输出变量
    VAR_GLOBAL = 4, // 全局变量
    VAR_EXTERNAL = 5, // 外部变量
    CONSTANT = 6    // 常量
};

/**
 * @brief 编译错误信息
 */
struct CompileError {
    enum class Type {
        LEXICAL,    // 词法错误
        SYNTAX,     // 语法错误
        SEMANTIC,   // 语义错误
        CODEGEN     // 代码生成错误
    };
    
    Type type;              // 错误类型
    size_t line;            // 行号
    size_t column;          // 列号
    std::string message;    // 错误消息
    std::string context;    // 错误上下文
    
    CompileError(Type t, size_t l, size_t c, const std::string& msg, const std::string& ctx = "")
        : type(t), line(l), column(c), message(msg), context(ctx) {}
};

/**
 * @brief 编译选项
 */
struct CompileOptions {
    bool optimize = true;           // 启用优化
    bool debug_info = false;        // 生成调试信息
    bool strict_mode = true;        // 严格模式
    bool case_sensitive = false;    // 大小写敏感
    size_t max_errors = 10;         // 最大错误数
    std::string target = "runtime"; // 目标平台
    
    CompileOptions() = default;
};

/**
 * @brief 变量信息
 */
struct VariableInfo {
    std::string name;           // 变量名
    STDataType data_type;       // 数据类型
    StorageClass storage_class; // 存储类别
    size_t offset;              // 内存偏移
    size_t size;                // 大小(字节)
    std::string initial_value;  // 初始值
    bool is_array;              // 是否数组
    std::vector<size_t> array_dimensions; // 数组维度
    
    VariableInfo() : data_type(STDataType::UNKNOWN), storage_class(StorageClass::VAR),
                    offset(0), size(0), is_array(false) {}
};

/**
 * @brief 中间代码指令
 */
struct IRInstruction {
    enum class OpCode : uint16_t {
        // 数据操作
        LOAD_CONST = 0x0001,   // 加载常量
        LOAD_VAR = 0x0002,     // 加载变量
        STORE_VAR = 0x0003,    // 存储变量
        COPY = 0x0004,         // 复制数据
        
        // 算术运算
        ADD = 0x0010,          // 加法
        SUB = 0x0011,          // 减法
        MUL = 0x0012,          // 乘法
        DIV = 0x0013,          // 除法
        MOD = 0x0014,          // 取模
        NEG = 0x0015,          // 取负
        
        // 比较运算
        EQ = 0x0020,           // 等于
        NE = 0x0021,           // 不等于
        LT = 0x0022,           // 小于
        LE = 0x0023,           // 小于等于
        GT = 0x0024,           // 大于
        GE = 0x0025,           // 大于等于
        
        // 逻辑运算
        AND = 0x0030,          // 逻辑与
        OR = 0x0031,           // 逻辑或
        XOR = 0x0032,          // 逻辑异或
        NOT = 0x0033,          // 逻辑非
        
        // 控制流
        JUMP = 0x0040,         // 无条件跳转
        JUMP_IF = 0x0041,      // 条件跳转(真)
        JUMP_IF_NOT = 0x0042,  // 条件跳转(假)
        CALL = 0x0043,         // 函数调用
        RETURN = 0x0044,       // 返回
        
        // 功能块操作
        FB_CALL = 0x0050,      // 功能块调用
        FB_LOAD_INPUT = 0x0051, // 加载FB输入
        FB_STORE_OUTPUT = 0x0052, // 存储FB输出
        
        // 特殊操作
        NOP = 0x0000,          // 空操作
        HALT = 0xFFFF          // 停机
    };
    
    OpCode opcode;             // 操作码
    uint32_t operand1;         // 操作数1
    uint32_t operand2;         // 操作数2  
    uint32_t operand3;         // 操作数3
    std::string label;         // 标签(可选)
    
    IRInstruction(OpCode op, uint32_t op1 = 0, uint32_t op2 = 0, uint32_t op3 = 0)
        : opcode(op), operand1(op1), operand2(op2), operand3(op3) {}
};

/**
 * @brief 编译后的程序
 */
struct CompiledProgram {
    std::vector<IRInstruction> instructions;        // 指令序列
    std::vector<VariableInfo> variables;           // 变量表
    std::unordered_map<std::string, size_t> labels; // 标签表
    size_t stack_size;                             // 栈大小
    size_t data_size;                              // 数据区大小
    std::chrono::milliseconds compile_time;        // 编译耗时
    
    CompiledProgram() : stack_size(0), data_size(0), compile_time(0) {}
};

/**
 * @brief ST编译器主类
 */
class STCompiler {
public:
    /**
     * @brief 构造函数
     */
    explicit STCompiler(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 析构函数
     */
    ~STCompiler();
    
    // 禁用拷贝和移动
    STCompiler(const STCompiler&) = delete;
    STCompiler& operator=(const STCompiler&) = delete;
    STCompiler(STCompiler&&) = delete;
    STCompiler& operator=(STCompiler&&) = delete;
    
    /**
     * @brief 编译ST源代码
     * @param source_code ST源代码
     * @param program_name 程序名称
     * @return 编译后的程序，失败返回nullptr
     */
    std::unique_ptr<CompiledProgram> compile(const std::string& source_code,
                                           const std::string& program_name = "main");
    
    /**
     * @brief 编译文件
     * @param file_path ST源文件路径
     * @return 编译后的程序，失败返回nullptr
     */
    std::unique_ptr<CompiledProgram> compile_file(const std::string& file_path);
    
    /**
     * @brief 获取编译错误
     */
    const std::vector<CompileError>& get_errors() const;
    
    /**
     * @brief 获取编译警告
     */
    const std::vector<CompileError>& get_warnings() const;
    
    /**
     * @brief 清除错误和警告
     */
    void clear_errors();
    
    /**
     * @brief 设置编译选项
     */
    void set_options(const CompileOptions& options);
    
    /**
     * @brief 获取编译选项
     */
    const CompileOptions& get_options() const;
    
    /**
     * @brief 添加全局变量
     */
    bool add_global_variable(const VariableInfo& var);
    
    /**
     * @brief 获取符号表
     */
    const SymbolTable* get_symbol_table() const;
    
    /**
     * @brief 获取编译统计信息
     */
    struct CompileStatistics {
        size_t source_lines;        // 源代码行数
        size_t instruction_count;   // 指令数量
        size_t variable_count;      // 变量数量
        size_t memory_usage;        // 内存使用量
        std::chrono::milliseconds lexer_time;     // 词法分析时间
        std::chrono::milliseconds parser_time;    // 语法分析时间
        std::chrono::milliseconds semantic_time;  // 语义分析时间
        std::chrono::milliseconds codegen_time;   // 代码生成时间
        std::chrono::milliseconds total_time;     // 总时间
    };
    
    CompileStatistics get_statistics() const;
    
    /**
     * @brief 获取版本信息
     */
    static std::string get_version();
    
    /**
     * @brief 获取支持的ST语言特性
     */
    static std::vector<std::string> get_supported_features();
    
private:
    // 编译阶段实现
    std::unique_ptr<AST> lexical_analysis(const std::string& source_code);
    std::unique_ptr<AST> syntax_analysis(const std::string& source_code);
    bool semantic_analysis(AST* ast);
    std::unique_ptr<CompiledProgram> code_generation(AST* ast);
    
    // 错误处理
    void add_error(CompileError::Type type, size_t line, size_t column,
                   const std::string& message, const std::string& context = "");
    void add_warning(CompileError::Type type, size_t line, size_t column,
                     const std::string& message, const std::string& context = "");
    
    // 内部组件
    std::unique_ptr<Lexer> lexer_;
    std::unique_ptr<Parser> parser_;
    std::unique_ptr<SemanticAnalyzer> semantic_analyzer_;
    std::unique_ptr<CodeGenerator> code_generator_;
    std::unique_ptr<SymbolTable> symbol_table_;
    
    // 配置和状态
    CompileOptions options_;
    std::vector<CompileError> errors_;
    std::vector<CompileError> warnings_;
    CompileStatistics statistics_;
    
    // 全局变量表
    std::vector<VariableInfo> global_variables_;
};

/**
 * @brief ST编译器工厂类
 */
class STCompilerFactory {
public:
    /**
     * @brief 创建编译器实例
     */
    static std::unique_ptr<STCompiler> create_compiler(const CompileOptions& options = CompileOptions{});
    
    /**
     * @brief 创建优化编译器
     */
    static std::unique_ptr<STCompiler> create_optimized_compiler();
    
    /**
     * @brief 创建调试编译器
     */
    static std::unique_ptr<STCompiler> create_debug_compiler();
    
    /**
     * @brief 检测ST语言特性支持
     */
    static bool check_feature_support(const std::string& feature);
    
    /**
     * @brief 获取默认编译选项
     */
    static CompileOptions get_default_options();
};

/**
 * @brief 工具函数
 */
namespace utils {

    /**
     * @brief 数据类型转换
     */
    std::string data_type_to_string(STDataType type);
    STDataType string_to_data_type(const std::string& type_str);
    size_t get_data_type_size(STDataType type);
    bool is_numeric_type(STDataType type);
    bool is_compatible_type(STDataType from, STDataType to);
    
    /**
     * @brief 存储类别转换
     */
    std::string storage_class_to_string(StorageClass storage_class);
    StorageClass string_to_storage_class(const std::string& class_str);
    
    /**
     * @brief 错误格式化
     */
    std::string format_error(const CompileError& error);
    std::string format_error_list(const std::vector<CompileError>& errors);
    
    /**
     * @brief 源代码预处理
     */
    std::string remove_comments(const std::string& source);
    std::vector<std::string> split_lines(const std::string& source);
    std::string normalize_whitespace(const std::string& source);

} // namespace utils

} // namespace st_compiler
} // namespace plc_runtime