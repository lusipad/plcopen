/**
 * @file SemanticAnalyzer.cpp
 * @brief ST语义分析器实现 - 类型检查和符号表管理
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include "st_compiler/Lexer.h"
#include "st_compiler/AST.h"
#include "st_compiler/SymbolTable.h"
#include <iostream>
#include <unordered_set>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief 语义分析器实现类
 */
class SemanticAnalyzerImpl {
private:
    SymbolTable* symbol_table_;
    std::vector<std::string> errors_;
    std::unordered_set<std::string> built_in_types_;
    
public:
    SemanticAnalyzerImpl(SymbolTable* symbol_table) 
        : symbol_table_(symbol_table) {
        initialize_built_in_types();
    }
    
    bool analyze(AST* ast) {
        if (!ast) {
            errors_.push_back("Null AST node");
            return false;
        }
        
        try {
            analyze_node(ast);
            return errors_.empty();
        } catch (const std::exception& e) {
            errors_.push_back(std::string("Semantic error: ") + e.what());
            return false;
        }
    }
    
    const std::vector<std::string>& get_errors() const {
        return errors_;
    }
    
private:
    void initialize_built_in_types() {
        built_in_types_.insert("BOOL");
        built_in_types_.insert("SINT");
        built_in_types_.insert("INT");
        built_in_types_.insert("DINT");
        built_in_types_.insert("LINT");
        built_in_types_.insert("USINT");
        built_in_types_.insert("UINT");
        built_in_types_.insert("UDINT");
        built_in_types_.insert("ULINT");
        built_in_types_.insert("REAL");
        built_in_types_.insert("LREAL");
        built_in_types_.insert("STRING");
        built_in_types_.insert("TIME");
        built_in_types_.insert("DATE");
        built_in_types_.insert("BYTE");
        built_in_types_.insert("WORD");
        built_in_types_.insert("DWORD");
        built_in_types_.insert("LWORD");
    }
    
    void analyze_node(AST* node) {
        if (!node) return;
        
        switch (node->node_type) {
            case ASTNodeType::PROGRAM:
                analyze_program(node);
                break;
            case ASTNodeType::VARIABLE_DECLARATION:
                analyze_variable_declaration(node);
                break;
            case ASTNodeType::VARIABLE:
                analyze_variable(node);
                break;
            case ASTNodeType::ASSIGNMENT:
                analyze_assignment(node);
                break;
            case ASTNodeType::IF_STATEMENT:
                analyze_if_statement(node);
                break;
            case ASTNodeType::WHILE_STATEMENT:
                analyze_while_statement(node);
                break;
            case ASTNodeType::FOR_STATEMENT:
                analyze_for_statement(node);
                break;
            case ASTNodeType::FUNCTION_CALL:
                analyze_function_call(node);
                break;
            case ASTNodeType::BINARY_OPERATION:
                analyze_binary_operation(node);
                break;
            case ASTNodeType::UNARY_OPERATION:
                analyze_unary_operation(node);
                break;
            case ASTNodeType::IDENTIFIER:
                analyze_identifier(node);
                break;
            case ASTNodeType::LITERAL:
                analyze_literal(node);
                break;
            case ASTNodeType::BLOCK:
                analyze_block(node);
                break;
            default:
                // 递归分析子节点
                for (auto& child : node->children) {
                    analyze_node(child.get());
                }
                break;
        }
    }
    
    void analyze_program(AST* node) {
        // 进入程序作用域
        symbol_table_->enter_scope("program:" + node->value);
        
        // 分析子节点
        for (auto& child : node->children) {
            analyze_node(child.get());
        }
        
        // 离开程序作用域
        symbol_table_->exit_scope();
    }
    
    void analyze_variable_declaration(AST* node) {
        // 分析变量声明区中的所有变量
        for (auto& child : node->children) {
            analyze_node(child.get());
        }
    }
    
    void analyze_variable(AST* node) {
        std::string var_name = node->value;
        std::string data_type = "INT"; // 默认类型
        
        // 获取数据类型
        if (!node->children.empty() && 
            node->children[0]->node_type == ASTNodeType::DATA_TYPE) {
            data_type = node->children[0]->value;
            
            // 验证数据类型
            if (built_in_types_.find(data_type) == built_in_types_.end()) {
                errors_.push_back("Unknown data type: " + data_type + 
                    " for variable " + var_name);
                return;
            }
        }
        
        // 检查变量是否已经定义
        if (symbol_table_->is_defined(var_name)) {
            errors_.push_back("Variable " + var_name + " is already defined");
            return;
        }
        
        // 创建符号
        Symbol symbol;
        symbol.name = var_name;
        symbol.data_type = data_type;
        symbol.symbol_type = SymbolType::VARIABLE;
        symbol.scope = symbol_table_->get_current_scope();
        symbol.line = node->line;
        symbol.column = node->column;
        
        // 添加到符号表
        symbol_table_->define(symbol);
        
        // 分析初始值表达式
        if (node->children.size() > 1) {
            analyze_node(node->children[1].get());
            
            // 类型检查
            std::string init_type = infer_expression_type(node->children[1].get());
            if (!is_compatible_type(data_type, init_type)) {
                errors_.push_back("Type mismatch: cannot assign " + init_type + 
                    " to " + data_type + " variable " + var_name);
            }
        }
    }
    
    void analyze_assignment(AST* node) {
        if (node->children.size() != 2) {
            errors_.push_back("Invalid assignment statement");
            return;
        }
        
        AST* lhs = node->children[0].get();
        AST* rhs = node->children[1].get();
        
        // 分析左右表达式
        analyze_node(lhs);
        analyze_node(rhs);
        
        // 类型检查
        std::string lhs_type = infer_expression_type(lhs);
        std::string rhs_type = infer_expression_type(rhs);
        
        if (!is_compatible_type(lhs_type, rhs_type)) {
            errors_.push_back("Type mismatch in assignment: cannot assign " + 
                rhs_type + " to " + lhs_type);
        }
        
        // 检查左值是否可以赋值
        if (lhs->node_type == ASTNodeType::IDENTIFIER) {
            auto symbol = symbol_table_->lookup(lhs->value);
            if (!symbol) {
                errors_.push_back("Undefined variable: " + lhs->value);
            }
        }
    }
    
    void analyze_if_statement(AST* node) {
        if (node->children.empty()) {
            errors_.push_back("Empty IF statement");
            return;
        }
        
        // 分析条件表达式
        AST* condition = node->children[0].get();
        analyze_node(condition);
        
        std::string cond_type = infer_expression_type(condition);
        if (cond_type != "BOOL") {
            errors_.push_back("IF condition must be BOOL, got " + cond_type);
        }
        
        // 分析分支
        for (size_t i = 1; i < node->children.size(); ++i) {
            analyze_node(node->children[i].get());
        }
    }
    
    void analyze_while_statement(AST* node) {
        if (node->children.size() < 2) {
            errors_.push_back("Invalid WHILE statement");
            return;
        }
        
        // 分析条件表达式
        AST* condition = node->children[0].get();
        analyze_node(condition);
        
        std::string cond_type = infer_expression_type(condition);
        if (cond_type != "BOOL") {
            errors_.push_back("WHILE condition must be BOOL, got " + cond_type);
        }
        
        // 分析循环体
        analyze_node(node->children[1].get());
    }
    
    void analyze_for_statement(AST* node) {
        if (node->children.size() < 4) {
            errors_.push_back("Invalid FOR statement");
            return;
        }
        
        // 循环变量应该是标识符
        AST* loop_var = node->children[0].get();
        if (loop_var->node_type != ASTNodeType::IDENTIFIER) {
            errors_.push_back("FOR loop variable must be an identifier");
            return;
        }
        
        // 检查循环变量是否已定义
        auto symbol = symbol_table_->lookup(loop_var->value);
        if (!symbol) {
            errors_.push_back("Undefined loop variable: " + loop_var->value);
        } else if (!is_numeric_type(symbol->data_type)) {
            errors_.push_back("FOR loop variable must be numeric type");
        }
        
        // 分析起始值和结束值表达式
        for (size_t i = 1; i < 3; ++i) {
            analyze_node(node->children[i].get());
            std::string expr_type = infer_expression_type(node->children[i].get());
            if (!is_numeric_type(expr_type)) {
                errors_.push_back("FOR loop bound must be numeric");
            }
        }
        
        // 分析可选的步长
        size_t body_index = 3;
        if (node->children.size() > 4) {
            analyze_node(node->children[3].get());
            std::string step_type = infer_expression_type(node->children[3].get());
            if (!is_numeric_type(step_type)) {
                errors_.push_back("FOR loop step must be numeric");
            }
            body_index = 4;
        }
        
        // 分析循环体
        if (body_index < node->children.size()) {
            analyze_node(node->children[body_index].get());
        }
    }
    
    void analyze_function_call(AST* node) {
        // 检查函数是否已定义
        auto symbol = symbol_table_->lookup(node->value);
        if (!symbol) {
            // 检查是否是内建函数
            if (!is_built_in_function(node->value)) {
                errors_.push_back("Undefined function: " + node->value);
            }
        }
        
        // 分析参数
        for (auto& child : node->children) {
            analyze_node(child.get());
        }
    }
    
    void analyze_binary_operation(AST* node) {
        if (node->children.size() != 2) {
            errors_.push_back("Binary operation must have exactly 2 operands");
            return;
        }
        
        AST* left = node->children[0].get();
        AST* right = node->children[1].get();
        
        analyze_node(left);
        analyze_node(right);
        
        std::string left_type = infer_expression_type(left);
        std::string right_type = infer_expression_type(right);
        std::string op = node->value;
        
        // 类型检查
        if (!is_valid_binary_operation(op, left_type, right_type)) {
            errors_.push_back("Invalid binary operation: " + left_type + 
                " " + op + " " + right_type);
        }
        
        // 推断结果类型
        node->data_type = infer_binary_result_type(op, left_type, right_type);
    }
    
    void analyze_unary_operation(AST* node) {
        if (node->children.size() != 1) {
            errors_.push_back("Unary operation must have exactly 1 operand");
            return;
        }
        
        AST* operand = node->children[0].get();
        analyze_node(operand);
        
        std::string operand_type = infer_expression_type(operand);
        std::string op = node->value;
        
        // 类型检查
        if (!is_valid_unary_operation(op, operand_type)) {
            errors_.push_back("Invalid unary operation: " + op + " " + operand_type);
        }
        
        // 推断结果类型
        node->data_type = infer_unary_result_type(op, operand_type);
    }
    
    void analyze_identifier(AST* node) {
        auto symbol = symbol_table_->lookup(node->value);
        if (!symbol) {
            errors_.push_back("Undefined identifier: " + node->value);
            return;
        }
        
        // 设置类型信息
        node->data_type = symbol->data_type;
    }
    
    void analyze_literal(AST* node) {
        // 字面量的类型在解析阶段已经设置
        if (node->data_type.empty()) {
            errors_.push_back("Literal without type information");
        }
    }
    
    void analyze_block(AST* node) {
        // 分析块中的所有语句
        for (auto& child : node->children) {
            analyze_node(child.get());
        }
    }
    
    std::string infer_expression_type(AST* node) {
        if (!node) return "UNKNOWN";
        
        if (!node->data_type.empty()) {
            return node->data_type;
        }
        
        switch (node->node_type) {
            case ASTNodeType::IDENTIFIER: {
                auto symbol = symbol_table_->lookup(node->value);
                return symbol ? symbol->data_type : "UNKNOWN";
            }
            case ASTNodeType::LITERAL:
                return node->data_type;
            case ASTNodeType::BINARY_OPERATION:
                if (node->children.size() == 2) {
                    return infer_binary_result_type(node->value,
                        infer_expression_type(node->children[0].get()),
                        infer_expression_type(node->children[1].get()));
                }
                break;
            case ASTNodeType::UNARY_OPERATION:
                if (node->children.size() == 1) {
                    return infer_unary_result_type(node->value,
                        infer_expression_type(node->children[0].get()));
                }
                break;
            default:
                break;
        }
        
        return "UNKNOWN";
    }
    
    bool is_compatible_type(const std::string& target, const std::string& source) {
        if (target == source) return true;
        
        // 数值类型兼容性
        if (is_numeric_type(target) && is_numeric_type(source)) {
            return true; // 简化：允许所有数值类型转换
        }
        
        return false;
    }
    
    bool is_numeric_type(const std::string& type) {
        return type == "SINT" || type == "INT" || type == "DINT" || type == "LINT" ||
               type == "USINT" || type == "UINT" || type == "UDINT" || type == "ULINT" ||
               type == "REAL" || type == "LREAL";
    }
    
    bool is_integer_type(const std::string& type) {
        return type == "SINT" || type == "INT" || type == "DINT" || type == "LINT" ||
               type == "USINT" || type == "UINT" || type == "UDINT" || type == "ULINT";
    }
    
    bool is_real_type(const std::string& type) {
        return type == "REAL" || type == "LREAL";
    }
    
    bool is_built_in_function(const std::string& name) {
        // 一些内建函数
        static std::unordered_set<std::string> built_ins = {
            "ABS", "SQRT", "SIN", "COS", "TAN", "LN", "EXP",
            "MIN", "MAX", "LIMIT", "SEL", "MUX"
        };
        return built_ins.find(name) != built_ins.end();
    }
    
    bool is_valid_binary_operation(const std::string& op, 
                                   const std::string& left_type, 
                                   const std::string& right_type) {
        // 算术运算符
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            return is_numeric_type(left_type) && is_numeric_type(right_type);
        }
        
        // 比较运算符
        if (op == "=" || op == "<>" || op == "<" || op == "<=" || 
            op == ">" || op == ">=") {
            return is_compatible_type(left_type, right_type);
        }
        
        // 逻辑运算符
        if (op == "AND" || op == "OR" || op == "XOR") {
            return left_type == "BOOL" && right_type == "BOOL";
        }
        
        return false;
    }
    
    bool is_valid_unary_operation(const std::string& op, const std::string& operand_type) {
        if (op == "-") {
            return is_numeric_type(operand_type);
        }
        if (op == "NOT") {
            return operand_type == "BOOL";
        }
        return false;
    }
    
    std::string infer_binary_result_type(const std::string& op,
                                        const std::string& left_type,
                                        const std::string& right_type) {
        // 比较运算符返回BOOL
        if (op == "=" || op == "<>" || op == "<" || op == "<=" || 
            op == ">" || op == ">=") {
            return "BOOL";
        }
        
        // 逻辑运算符返回BOOL
        if (op == "AND" || op == "OR" || op == "XOR") {
            return "BOOL";
        }
        
        // 算术运算符：选择更大的类型
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            if (is_real_type(left_type) || is_real_type(right_type)) {
                return "REAL";
            }
            if (is_integer_type(left_type) && is_integer_type(right_type)) {
                return "INT"; // 简化：返回基本整数类型
            }
        }
        
        return left_type;
    }
    
    std::string infer_unary_result_type(const std::string& op, const std::string& operand_type) {
        if (op == "-") {
            return operand_type;
        }
        if (op == "NOT") {
            return "BOOL";
        }
        return operand_type;
    }
};

// SemanticAnalyzer类实现
bool SemanticAnalyzer::analyze(AST* ast, SymbolTable* symbol_table) {
    if (!ast || !symbol_table) {
        return false;
    }
    
    SemanticAnalyzerImpl analyzer(symbol_table);
    return analyzer.analyze(ast);
}

} // namespace st_compiler
} // namespace plc_runtime