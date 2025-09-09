/**
 * @file STCompiler.cpp
 * @brief ST编译器实现
 * @version MVP-1.0
 */

#include "st_compiler/STCompiler.h"
#include "st_compiler/Lexer.h"
#include "st_compiler/Parser.h"
#include "st_compiler/SemanticAnalyzer.h"
#include "st_compiler/CodeGenerator.h"
#include "st_compiler/SymbolTable.h"
#include "st_compiler/AST.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <chrono>

namespace plc_runtime {
namespace st_compiler {

namespace plc_runtime {
namespace st_compiler {

// =============================================================================
// STCompiler Implementation
// =============================================================================

STCompiler::STCompiler(const CompileOptions& options)
    : options_(options) {
    
    // 创建内部组件
    lexer_ = std::make_unique<Lexer>();
    parser_ = std::make_unique<Parser>();
    semantic_analyzer_ = std::make_unique<SemanticAnalyzer>();
    code_generator_ = std::make_unique<CodeGenerator>();
    symbol_table_ = std::make_unique<SymbolTable>();
    
    // 清空统计信息
    statistics_ = CompileStatistics{};
}

STCompiler::~STCompiler() = default;

std::unique_ptr<CompiledProgram> STCompiler::compile(const std::string& source_code,
                                                    const std::string& program_name) {
    clear_errors();
    
    auto compile_start = std::chrono::high_resolution_clock::now();
    
    // 预处理源代码
    std::string processed_source = utils::remove_comments(source_code);
    processed_source = utils::normalize_whitespace(processed_source);
    
    // 统计源代码信息
    statistics_.source_lines = utils::split_lines(source_code).size();
    
    try {
        // 1. 词法分析
        auto lexer_start = std::chrono::high_resolution_clock::now();
        auto ast = lexical_analysis(processed_source);
        if (!ast || !errors_.empty()) {
            return nullptr;
        }
        statistics_.lexer_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - lexer_start);
        
        // 2. 语法分析
        auto parser_start = std::chrono::high_resolution_clock::now();
        ast = syntax_analysis(processed_source);
        if (!ast || !errors_.empty()) {
            return nullptr;
        }
        statistics_.parser_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - parser_start);
        
        // 3. 语义分析
        auto semantic_start = std::chrono::high_resolution_clock::now();
        if (!semantic_analysis(ast.get()) || !errors_.empty()) {
            return nullptr;
        }
        statistics_.semantic_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - semantic_start);
        
        // 4. 代码生成
        auto codegen_start = std::chrono::high_resolution_clock::now();
        auto program = code_generation(ast.get());
        if (!program || !errors_.empty()) {
            return nullptr;
        }
        statistics_.codegen_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - codegen_start);
        
        // 完成统计
        auto compile_end = std::chrono::high_resolution_clock::now();
        statistics_.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            compile_end - compile_start);
        
        program->compile_time = statistics_.total_time;
        statistics_.instruction_count = program->instructions.size();
        statistics_.variable_count = program->variables.size();
        statistics_.memory_usage = program->data_size + program->stack_size;
        
        return program;
        
    } catch (const std::exception& e) {
        add_error(CompileError::Type::CODEGEN, 0, 0, 
                 std::string("Internal compiler error: ") + e.what());
        return nullptr;
    }
}

std::unique_ptr<CompiledProgram> STCompiler::compile_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        add_error(CompileError::Type::LEXICAL, 0, 0, 
                 "Cannot open file: " + file_path);
        return nullptr;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source_code = buffer.str();
    
    // 提取程序名（从文件名）
    std::string program_name = file_path;
    size_t last_slash = program_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        program_name = program_name.substr(last_slash + 1);
    }
    size_t dot = program_name.find_last_of('.');
    if (dot != std::string::npos) {
        program_name = program_name.substr(0, dot);
    }
    
    return compile(source_code, program_name);
}

const std::vector<CompileError>& STCompiler::get_errors() const {
    return errors_;
}

const std::vector<CompileError>& STCompiler::get_warnings() const {
    return warnings_;
}

void STCompiler::clear_errors() {
    errors_.clear();
    warnings_.clear();
}

void STCompiler::set_options(const CompileOptions& options) {
    options_ = options;
}

const CompileOptions& STCompiler::get_options() const {
    return options_;
}

bool STCompiler::add_global_variable(const VariableInfo& var) {
    // 检查变量名冲突
    for (const auto& existing : global_variables_) {
        if (existing.name == var.name) {
            return false;
        }
    }
    
    global_variables_.push_back(var);
    return true;
}

const SymbolTable* STCompiler::get_symbol_table() const {
    return symbol_table_.get();
}

STCompiler::CompileStatistics STCompiler::get_statistics() const {
    return statistics_;
}

std::string STCompiler::get_version() {
    return "PLC Runtime ST Compiler v1.0.0";
}

std::vector<std::string> STCompiler::get_supported_features() {
    return {
        "Basic Data Types (BOOL, INT, DINT, REAL, STRING)",
        "Variable Declarations (VAR, VAR_INPUT, VAR_OUTPUT)",
        "Assignment Statements",
        "Arithmetic Operations (+, -, *, /, MOD)",
        "Comparison Operations (=, <>, <, <=, >, >=)", 
        "Logical Operations (AND, OR, XOR, NOT)",
        "Control Flow (IF-THEN-ELSE, CASE, FOR, WHILE)",
        "Function Calls",
        "Basic Function Blocks (TON, TOF, CTU, CTD)",
        "Direct Variables (%IX, %QX, %MW)",
        "Comments (// and (* *))",
        "Constants and Literals"
    };
}

// Private implementation methods
std::unique_ptr<AST> STCompiler::lexical_analysis(const std::string& source_code) {
    try {
        if (!lexer_) {
            add_error(CompileError::Type::LEXICAL, 0, 0, "Lexer not initialized");
            return nullptr;
        }
        
        return lexer_->analyze(source_code);
        
    } catch (const std::exception& e) {
        add_error(CompileError::Type::LEXICAL, 0, 0, 
                 std::string("Lexical analysis failed: ") + e.what());
        return nullptr;
    }
}

std::unique_ptr<AST> STCompiler::syntax_analysis(const std::string& source_code) {
    try {
        if (!parser_) {
            add_error(CompileError::Type::SYNTAX, 0, 0, "Parser not initialized");
            return nullptr;
        }
        
        return parser_->parse(source_code);
        
    } catch (const std::exception& e) {
        add_error(CompileError::Type::SYNTAX, 0, 0,
                 std::string("Syntax analysis failed: ") + e.what());
        return nullptr;
    }
}

bool STCompiler::semantic_analysis(AST* ast) {
    try {
        if (!semantic_analyzer_ || !ast) {
            add_error(CompileError::Type::SEMANTIC, 0, 0, "Semantic analyzer not initialized");
            return false;
        }
        
        // 添加全局变量到符号表
        for (const auto& var : global_variables_) {
            symbol_table_->add_variable(var);
        }
        
        return semantic_analyzer_->analyze(ast, symbol_table_.get());
        
    } catch (const std::exception& e) {
        add_error(CompileError::Type::SEMANTIC, 0, 0,
                 std::string("Semantic analysis failed: ") + e.what());
        return false;
    }
}

std::unique_ptr<CompiledProgram> STCompiler::code_generation(AST* ast) {
    try {
        if (!code_generator_ || !ast) {
            add_error(CompileError::Type::CODEGEN, 0, 0, "Code generator not initialized");
            return nullptr;
        }
        
        return code_generator_->generate(ast, symbol_table_.get(), options_);
        
    } catch (const std::exception& e) {
        add_error(CompileError::Type::CODEGEN, 0, 0,
                 std::string("Code generation failed: ") + e.what());
        return nullptr;
    }
}

void STCompiler::add_error(CompileError::Type type, size_t line, size_t column,
                          const std::string& message, const std::string& context) {
    if (errors_.size() >= options_.max_errors) {
        return;
    }
    
    errors_.emplace_back(type, line, column, message, context);
}

void STCompiler::add_warning(CompileError::Type type, size_t line, size_t column,
                           const std::string& message, const std::string& context) {
    warnings_.emplace_back(type, line, column, message, context);
}

// =============================================================================
// STCompilerFactory Implementation
// =============================================================================

std::unique_ptr<STCompiler> STCompilerFactory::create_compiler(const CompileOptions& options) {
    return std::make_unique<STCompiler>(options);
}

std::unique_ptr<STCompiler> STCompilerFactory::create_optimized_compiler() {
    CompileOptions options;
    options.optimize = true;
    options.debug_info = false;
    options.strict_mode = true;
    options.target = "runtime";
    
    return std::make_unique<STCompiler>(options);
}

std::unique_ptr<STCompiler> STCompilerFactory::create_debug_compiler() {
    CompileOptions options;
    options.optimize = false;
    options.debug_info = true;
    options.strict_mode = false;
    options.target = "debug";
    
    return std::make_unique<STCompiler>(options);
}

bool STCompilerFactory::check_feature_support(const std::string& feature) {
    auto supported = STCompiler::get_supported_features();
    return std::find_if(supported.begin(), supported.end(),
        [&feature](const std::string& supported_feature) {
            return supported_feature.find(feature) != std::string::npos;
        }) != supported.end();
}

CompileOptions STCompilerFactory::get_default_options() {
    return CompileOptions{};
}

// =============================================================================
// Utils Implementation
// =============================================================================

namespace utils {

std::string data_type_to_string(STDataType type) {
    switch (type) {
        case STDataType::BOOL: return "BOOL";
        case STDataType::SINT: return "SINT";
        case STDataType::INT: return "INT";
        case STDataType::DINT: return "DINT";
        case STDataType::LINT: return "LINT";
        case STDataType::USINT: return "USINT";
        case STDataType::UINT: return "UINT";
        case STDataType::UDINT: return "UDINT";
        case STDataType::ULINT: return "ULINT";
        case STDataType::REAL: return "REAL";
        case STDataType::LREAL: return "LREAL";
        case STDataType::STRING: return "STRING";
        case STDataType::TIME: return "TIME";
        case STDataType::DATE: return "DATE";
        case STDataType::TOD: return "TOD";
        case STDataType::DT: return "DT";
        case STDataType::ARRAY: return "ARRAY";
        case STDataType::STRUCT: return "STRUCT";
        default: return "UNKNOWN";
    }
}

STDataType string_to_data_type(const std::string& type_str) {
    std::string upper = type_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "BOOL") return STDataType::BOOL;
    if (upper == "SINT") return STDataType::SINT;
    if (upper == "INT") return STDataType::INT;
    if (upper == "DINT") return STDataType::DINT;
    if (upper == "LINT") return STDataType::LINT;
    if (upper == "USINT") return STDataType::USINT;
    if (upper == "UINT") return STDataType::UINT;
    if (upper == "UDINT") return STDataType::UDINT;
    if (upper == "ULINT") return STDataType::ULINT;
    if (upper == "REAL") return STDataType::REAL;
    if (upper == "LREAL") return STDataType::LREAL;
    if (upper == "STRING") return STDataType::STRING;
    if (upper == "TIME") return STDataType::TIME;
    if (upper == "DATE") return STDataType::DATE;
    if (upper == "TOD") return STDataType::TOD;
    if (upper == "DT") return STDataType::DT;
    if (upper == "ARRAY") return STDataType::ARRAY;
    if (upper == "STRUCT") return STDataType::STRUCT;
    
    return STDataType::UNKNOWN;
}

size_t get_data_type_size(STDataType type) {
    switch (type) {
        case STDataType::BOOL: return 1;
        case STDataType::SINT: 
        case STDataType::USINT: return 1;
        case STDataType::INT:
        case STDataType::UINT: return 2;
        case STDataType::DINT:
        case STDataType::UDINT:
        case STDataType::REAL: return 4;
        case STDataType::LINT:
        case STDataType::ULINT:
        case STDataType::LREAL: return 8;
        case STDataType::STRING: return 256; // 默认字符串长度
        case STDataType::TIME:
        case STDataType::DATE:
        case STDataType::TOD:
        case STDataType::DT: return 8;
        default: return 0;
    }
}

bool is_numeric_type(STDataType type) {
    return type >= STDataType::SINT && type <= STDataType::LREAL;
}

bool is_compatible_type(STDataType from, STDataType to) {
    if (from == to) {
        return true;
    }
    
    // 数值类型之间可以转换（可能有精度损失）
    if (is_numeric_type(from) && is_numeric_type(to)) {
        return true;
    }
    
    // BOOL可以转换为数值类型
    if (from == STDataType::BOOL && is_numeric_type(to)) {
        return true;
    }
    
    return false;
}

std::string storage_class_to_string(StorageClass storage_class) {
    switch (storage_class) {
        case StorageClass::VAR: return "VAR";
        case StorageClass::VAR_INPUT: return "VAR_INPUT";
        case StorageClass::VAR_OUTPUT: return "VAR_OUTPUT";
        case StorageClass::VAR_IN_OUT: return "VAR_IN_OUT";
        case StorageClass::VAR_GLOBAL: return "VAR_GLOBAL";
        case StorageClass::VAR_EXTERNAL: return "VAR_EXTERNAL";
        case StorageClass::CONSTANT: return "CONSTANT";
        default: return "UNKNOWN";
    }
}

StorageClass string_to_storage_class(const std::string& class_str) {
    std::string upper = class_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "VAR") return StorageClass::VAR;
    if (upper == "VAR_INPUT") return StorageClass::VAR_INPUT;
    if (upper == "VAR_OUTPUT") return StorageClass::VAR_OUTPUT;
    if (upper == "VAR_IN_OUT") return StorageClass::VAR_IN_OUT;
    if (upper == "VAR_GLOBAL") return StorageClass::VAR_GLOBAL;
    if (upper == "VAR_EXTERNAL") return StorageClass::VAR_EXTERNAL;
    if (upper == "CONSTANT") return StorageClass::CONSTANT;
    
    return StorageClass::VAR;
}

std::string format_error(const CompileError& error) {
    std::ostringstream oss;
    
    std::string type_str;
    switch (error.type) {
        case CompileError::Type::LEXICAL: type_str = "Lexical"; break;
        case CompileError::Type::SYNTAX: type_str = "Syntax"; break;
        case CompileError::Type::SEMANTIC: type_str = "Semantic"; break;
        case CompileError::Type::CODEGEN: type_str = "CodeGen"; break;
    }
    
    oss << type_str << " Error";
    if (error.line > 0) {
        oss << " [" << error.line << ":" << error.column << "]";
    }
    oss << ": " << error.message;
    
    if (!error.context.empty()) {
        oss << "\n  Context: " << error.context;
    }
    
    return oss.str();
}

std::string format_error_list(const std::vector<CompileError>& errors) {
    if (errors.empty()) {
        return "No errors";
    }
    
    std::ostringstream oss;
    oss << errors.size() << " error(s):\n";
    
    for (size_t i = 0; i < errors.size(); ++i) {
        oss << (i + 1) << ". " << format_error(errors[i]) << "\n";
    }
    
    return oss.str();
}

std::string remove_comments(const std::string& source) {
    std::string result;
    result.reserve(source.size());
    
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        
        if (in_string) {
            result += c;
            if (c == '\'' && (i == 0 || source[i-1] != '\\')) {
                in_string = false;
            }
        } else if (in_line_comment) {
            if (c == '\n' || c == '\r') {
                result += c;
                in_line_comment = false;
            }
        } else if (in_block_comment) {
            if (c == '*' && i + 1 < source.size() && source[i + 1] == ')') {
                ++i; // 跳过 ')'
                in_block_comment = false;
            }
        } else {
            if (c == '\'' && (i == 0 || source[i-1] != '\\')) {
                in_string = true;
                result += c;
            } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
                in_line_comment = true;
                ++i; // 跳过第二个 '/'
            } else if (c == '(' && i + 1 < source.size() && source[i + 1] == '*') {
                in_block_comment = true;
                ++i; // 跳过 '*'
            } else {
                result += c;
            }
        }
    }
    
    return result;
}

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    std::istringstream stream(source);
    std::string line;
    
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    return lines;
}

std::string normalize_whitespace(const std::string& source) {
    // 简单的空白符规范化
    std::string result = source;
    
    // 将制表符转换为空格
    std::replace(result.begin(), result.end(), '\t', ' ');
    
    // 移除多余的空格
    std::regex multiple_spaces("[ ]{2,}");
    result = std::regex_replace(result, multiple_spaces, " ");
    
    return result;
}

} // namespace utils

} // namespace st_compiler
} // namespace plc_runtime