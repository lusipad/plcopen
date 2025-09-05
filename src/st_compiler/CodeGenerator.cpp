/**
 * @file CodeGenerator.cpp
 * @brief ST代码生成器实现 - 生成中间代码指令
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include "st_compiler/Lexer.h"
#include "st_compiler/AST.h"
#include "st_compiler/SymbolTable.h"
#include <sstream>
#include <iomanip>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief 虚拟机指令类型
 */
enum class OpCode {
    // 数据操作
    LOAD_CONST,      // 加载常量
    LOAD_VAR,        // 加载变量
    STORE_VAR,       // 存储变量
    
    // 算术运算
    ADD, SUB, MUL, DIV, MOD,
    NEG,             // 取负
    
    // 比较运算
    EQ, NE, LT, LE, GT, GE,
    
    // 逻辑运算
    AND, OR, XOR, NOT,
    
    // 控制流
    JUMP,            // 无条件跳转
    JUMP_IF_FALSE,   // 条件跳转
    JUMP_IF_TRUE,    // 条件跳转
    CALL,            // 函数调用
    RET,             // 返回
    
    // 栈操作
    POP,             // 出栈
    DUP,             // 复制栈顶
    
    // 程序控制
    HALT             // 程序结束
};

/**
 * @brief 虚拟机指令
 */
struct Instruction {
    OpCode opcode;
    std::string operand;  // 操作数（变量名、常量值、标签等）
    int address = -1;     // 地址（用于跳转）
    std::string comment;  // 注释
    
    Instruction(OpCode op, const std::string& operand = "", const std::string& comment = "")
        : opcode(op), operand(operand), comment(comment) {}
};

/**
 * @brief 代码生成器实现类
 */
class CodeGeneratorImpl {
private:
    std::vector<Instruction> instructions_;
    SymbolTable* symbol_table_;
    CompileOptions options_;
    int label_counter_;
    
public:
    CodeGeneratorImpl(SymbolTable* symbol_table, const CompileOptions& options)
        : symbol_table_(symbol_table), options_(options), label_counter_(0) {}
    
    std::unique_ptr<CompiledProgram> generate(AST* ast) {
        if (!ast) {
            return nullptr;
        }
        
        instructions_.clear();
        label_counter_ = 0;
        
        // 生成代码
        generate_node(ast);
        
        // 添加程序结束指令
        emit(OpCode::HALT, "", "Program end");
        
        // 创建编译后的程序
        auto program = std::make_unique<CompiledProgram>();
        program->instructions = std::move(instructions_);
        program->variables = collect_variables();
        program->compile_time = std::chrono::milliseconds(100); // 模拟编译时间
        
        return program;
    }
    
private:
    void generate_node(AST* node) {
        if (!node) return;
        
        switch (node->node_type) {
            case ASTNodeType::PROGRAM:
                generate_program(node);
                break;
            case ASTNodeType::VARIABLE_DECLARATION:
                generate_variable_declaration(node);
                break;
            case ASTNodeType::ASSIGNMENT:
                generate_assignment(node);
                break;
            case ASTNodeType::IF_STATEMENT:
                generate_if_statement(node);
                break;
            case ASTNodeType::WHILE_STATEMENT:
                generate_while_statement(node);
                break;
            case ASTNodeType::FOR_STATEMENT:
                generate_for_statement(node);
                break;
            case ASTNodeType::FUNCTION_CALL:
                generate_function_call(node);
                break;
            case ASTNodeType::BINARY_OPERATION:
                generate_binary_operation(node);
                break;
            case ASTNodeType::UNARY_OPERATION:
                generate_unary_operation(node);
                break;
            case ASTNodeType::IDENTIFIER:
                generate_identifier(node);
                break;
            case ASTNodeType::LITERAL:
                generate_literal(node);
                break;
            case ASTNodeType::BLOCK:
                generate_block(node);
                break;
            case ASTNodeType::RETURN_STATEMENT:
                generate_return_statement(node);
                break;
            default:
                // 递归处理子节点
                for (auto& child : node->children) {
                    generate_node(child.get());
                }
                break;
        }
    }
    
    void generate_program(AST* node) {
        emit_comment("Program: " + node->value);
        
        // 生成所有子节点
        for (auto& child : node->children) {
            generate_node(child.get());
        }
    }
    
    void generate_variable_declaration(AST* node) {
        emit_comment("Variable declaration section");
        
        // 变量声明不需要生成运行时代码
        // 符号表已经包含了变量信息
        for (auto& child : node->children) {
            if (child->node_type == ASTNodeType::VARIABLE && 
                child->children.size() > 1) {
                // 如果有初始值，生成初始化代码
                emit_comment("Initialize variable: " + child->value);
                generate_node(child->children[1].get()); // 初始值表达式
                emit(OpCode::STORE_VAR, child->value, "Store initial value");
            }
        }
    }
    
    void generate_assignment(AST* node) {
        if (node->children.size() != 2) return;
        
        AST* lhs = node->children[0].get();
        AST* rhs = node->children[1].get();
        
        emit_comment("Assignment: " + lhs->value + " := ...");
        
        // 生成右表达式，结果在栈顶
        generate_node(rhs);
        
        // 存储到左值变量
        if (lhs->node_type == ASTNodeType::IDENTIFIER) {
            emit(OpCode::STORE_VAR, lhs->value);
        }
    }
    
    void generate_if_statement(AST* node) {
        if (node->children.empty()) return;
        
        std::string else_label = generate_label("else");
        std::string end_label = generate_label("endif");
        
        emit_comment("IF statement");
        
        // 生成条件表达式
        generate_node(node->children[0].get());
        
        // 条件跳转到ELSE
        emit(OpCode::JUMP_IF_FALSE, else_label, "Jump to else if false");
        
        // 生成THEN块
        if (node->children.size() > 1) {
            generate_node(node->children[1].get());
        }
        
        // 跳转到结束
        emit(OpCode::JUMP, end_label, "Jump to end");
        
        // ELSE标签
        emit_label(else_label);
        
        // 生成ELSE块（如果存在）
        if (node->children.size() > 2) {
            generate_node(node->children[2].get());
        }
        
        // 结束标签
        emit_label(end_label);
    }
    
    void generate_while_statement(AST* node) {
        if (node->children.size() < 2) return;
        
        std::string loop_label = generate_label("while_loop");
        std::string end_label = generate_label("while_end");
        
        emit_comment("WHILE statement");
        
        // 循环开始标签
        emit_label(loop_label);
        
        // 生成条件表达式
        generate_node(node->children[0].get());
        
        // 条件跳转到结束
        emit(OpCode::JUMP_IF_FALSE, end_label, "Exit loop if false");
        
        // 生成循环体
        generate_node(node->children[1].get());
        
        // 跳转回循环开始
        emit(OpCode::JUMP, loop_label, "Jump back to loop start");
        
        // 结束标签
        emit_label(end_label);
    }
    
    void generate_for_statement(AST* node) {
        if (node->children.size() < 4) return;
        
        std::string loop_label = generate_label("for_loop");
        std::string end_label = generate_label("for_end");
        
        emit_comment("FOR statement");
        
        AST* loop_var = node->children[0].get();
        AST* start_expr = node->children[1].get();
        AST* end_expr = node->children[2].get();
        
        // 初始化循环变量
        generate_node(start_expr);
        emit(OpCode::STORE_VAR, loop_var->value, "Initialize loop variable");
        
        // 循环开始标签
        emit_label(loop_label);
        
        // 检查循环条件：loop_var <= end_value
        emit(OpCode::LOAD_VAR, loop_var->value);
        generate_node(end_expr);
        emit(OpCode::LE, "", "Compare loop variable with end value");
        emit(OpCode::JUMP_IF_FALSE, end_label, "Exit loop if condition false");
        
        // 生成循环体
        size_t body_index = (node->children.size() > 4) ? 4 : 3;
        if (body_index < node->children.size()) {
            generate_node(node->children[body_index].get());
        }
        
        // 递增循环变量
        emit(OpCode::LOAD_VAR, loop_var->value);
        
        if (node->children.size() > 4) {
            // 使用指定的步长
            generate_node(node->children[3].get());
        } else {
            // 默认步长为1
            emit(OpCode::LOAD_CONST, "1");
        }
        
        emit(OpCode::ADD, "", "Increment loop variable");
        emit(OpCode::STORE_VAR, loop_var->value);
        
        // 跳转回循环开始
        emit(OpCode::JUMP, loop_label, "Jump back to loop start");
        
        // 结束标签
        emit_label(end_label);
    }
    
    void generate_function_call(AST* node) {
        emit_comment("Function call: " + node->value);
        
        // 生成参数（从右到左）
        for (int i = node->children.size() - 1; i >= 0; --i) {
            generate_node(node->children[i].get());
        }
        
        // 调用函数
        emit(OpCode::CALL, node->value, "Call function with " + 
             std::to_string(node->children.size()) + " parameters");
    }
    
    void generate_binary_operation(AST* node) {
        if (node->children.size() != 2) return;
        
        // 生成左操作数
        generate_node(node->children[0].get());
        
        // 生成右操作数
        generate_node(node->children[1].get());
        
        // 生成运算指令
        std::string op = node->value;
        if (op == "+") {
            emit(OpCode::ADD, "", "Addition");
        } else if (op == "-") {
            emit(OpCode::SUB, "", "Subtraction");
        } else if (op == "*") {
            emit(OpCode::MUL, "", "Multiplication");
        } else if (op == "/") {
            emit(OpCode::DIV, "", "Division");
        } else if (op == "=") {
            emit(OpCode::EQ, "", "Equal comparison");
        } else if (op == "<>") {
            emit(OpCode::NE, "", "Not equal comparison");
        } else if (op == "<") {
            emit(OpCode::LT, "", "Less than comparison");
        } else if (op == "<=") {
            emit(OpCode::LE, "", "Less or equal comparison");
        } else if (op == ">") {
            emit(OpCode::GT, "", "Greater than comparison");
        } else if (op == ">=") {
            emit(OpCode::GE, "", "Greater or equal comparison");
        } else if (op == "AND") {
            emit(OpCode::AND, "", "Logical AND");
        } else if (op == "OR") {
            emit(OpCode::OR, "", "Logical OR");
        } else if (op == "XOR") {
            emit(OpCode::XOR, "", "Logical XOR");
        }
    }
    
    void generate_unary_operation(AST* node) {
        if (node->children.size() != 1) return;
        
        // 生成操作数
        generate_node(node->children[0].get());
        
        // 生成运算指令
        std::string op = node->value;
        if (op == "-") {
            emit(OpCode::NEG, "", "Negation");
        } else if (op == "NOT") {
            emit(OpCode::NOT, "", "Logical NOT");
        }
    }
    
    void generate_identifier(AST* node) {
        emit(OpCode::LOAD_VAR, node->value, "Load variable");
    }
    
    void generate_literal(AST* node) {
        emit(OpCode::LOAD_CONST, node->value, "Load constant");
    }
    
    void generate_block(AST* node) {
        // 生成块中的所有语句
        for (auto& child : node->children) {
            generate_node(child.get());
        }
    }
    
    void generate_return_statement(AST* node) {
        if (!node->children.empty()) {
            // 生成返回值表达式
            generate_node(node->children[0].get());
        }
        
        emit(OpCode::RET, "", "Return from function/program");
    }
    
    void emit(OpCode opcode, const std::string& operand = "", const std::string& comment = "") {
        instructions_.emplace_back(opcode, operand, comment);
    }
    
    void emit_label(const std::string& label) {
        // 在指令中标记标签位置
        for (auto& instr : instructions_) {
            if (instr.operand == label) {
                instr.address = instructions_.size();
            }
        }
    }
    
    void emit_comment(const std::string& comment) {
        if (options_.debug_info) {
            // 如果启用调试信息，添加注释指令
            instructions_.emplace_back(OpCode::HALT, "", "// " + comment);
        }
    }
    
    std::string generate_label(const std::string& prefix) {
        return prefix + "_" + std::to_string(label_counter_++);
    }
    
    std::vector<VariableInfo> collect_variables() {
        std::vector<VariableInfo> variables;
        
        // 从符号表收集所有变量
        auto symbols = symbol_table_->get_all_symbols();
        for (const auto& symbol : symbols) {
            if (symbol.symbol_type == SymbolType::VARIABLE) {
                VariableInfo var_info;
                var_info.name = symbol.name;
                var_info.data_type = parse_data_type(symbol.data_type);
                var_info.storage_class = StorageClass::LOCAL;
                var_info.address = variables.size(); // 简单地址分配
                var_info.size = get_type_size(var_info.data_type);
                
                variables.push_back(var_info);
            }
        }
        
        return variables;
    }
    
    DataType parse_data_type(const std::string& type_str) {
        if (type_str == "BOOL") return DataType::BOOL;
        if (type_str == "SINT") return DataType::SINT;
        if (type_str == "INT") return DataType::INT;
        if (type_str == "DINT") return DataType::DINT;
        if (type_str == "REAL") return DataType::REAL;
        if (type_str == "STRING") return DataType::STRING;
        return DataType::INT; // 默认
    }
    
    size_t get_type_size(DataType type) {
        switch (type) {
            case DataType::BOOL: return 1;
            case DataType::SINT: return 1;
            case DataType::INT: return 2;
            case DataType::DINT: return 4;
            case DataType::REAL: return 4;
            case DataType::STRING: return 256; // 默认字符串长度
            default: return 4;
        }
    }
};

// CodeGenerator类实现
std::unique_ptr<CompiledProgram> CodeGenerator::generate(AST* ast, 
                                                        SymbolTable* symbol_table, 
                                                        const CompileOptions& options) {
    if (!ast || !symbol_table) {
        return nullptr;
    }
    
    CodeGeneratorImpl generator(symbol_table, options);
    return generator.generate(ast);
}

} // namespace st_compiler
} // namespace plc_runtime