/**
 * @file AST.h
 * @brief 抽象语法树定义
 * @version MVP-1.0
 */

#pragma once

#include "STCompiler.h"
#include <vector>
#include <memory>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief AST节点类型
 */
enum class ASTNodeType {
    // 程序结构
    PROGRAM,
    FUNCTION_BLOCK,
    FUNCTION,
    
    // 变量声明
    VAR_DECLARATION,
    VAR_LIST,
    
    // 语句
    ASSIGNMENT,
    IF_STATEMENT,
    CASE_STATEMENT, 
    FOR_STATEMENT,
    WHILE_STATEMENT,
    RETURN_STATEMENT,
    
    // 表达式
    BINARY_OP,
    UNARY_OP,
    FUNCTION_CALL,
    VARIABLE_REF,
    LITERAL,
    
    // 字面量类型
    BOOL_LITERAL,
    INT_LITERAL,
    REAL_LITERAL,
    STRING_LITERAL
};

/**
 * @brief AST节点基类
 */
class ASTNode {
public:
    explicit ASTNode(ASTNodeType type, size_t line = 0, size_t column = 0);
    virtual ~ASTNode() = default;
    
    ASTNodeType get_type() const { return type_; }
    size_t get_line() const { return line_; }
    size_t get_column() const { return column_; }
    
    // 子节点管理
    void add_child(std::unique_ptr<ASTNode> child);
    const std::vector<std::unique_ptr<ASTNode>>& get_children() const;
    size_t get_child_count() const;
    
    // 访问器模式支持
    virtual void accept(class ASTVisitor& visitor) = 0;
    
protected:
    ASTNodeType type_;
    size_t line_;
    size_t column_;
    std::vector<std::unique_ptr<ASTNode>> children_;
};

/**
 * @brief 程序节点
 */
class ProgramNode : public ASTNode {
public:
    explicit ProgramNode(const std::string& name, size_t line = 0, size_t column = 0);
    
    const std::string& get_name() const { return name_; }
    void accept(ASTVisitor& visitor) override;
    
private:
    std::string name_;
};

/**
 * @brief 变量声明节点
 */
class VariableDeclarationNode : public ASTNode {
public:
    VariableDeclarationNode(const std::string& name, STDataType data_type, 
                          StorageClass storage_class, size_t line = 0, size_t column = 0);
    
    const std::string& get_name() const { return name_; }
    STDataType get_data_type() const { return data_type_; }
    StorageClass get_storage_class() const { return storage_class_; }
    
    void accept(ASTVisitor& visitor) override;
    
private:
    std::string name_;
    STDataType data_type_;
    StorageClass storage_class_;
};

/**
 * @brief 赋值语句节点
 */
class AssignmentNode : public ASTNode {
public:
    AssignmentNode(std::unique_ptr<ASTNode> lhs, std::unique_ptr<ASTNode> rhs,
                   size_t line = 0, size_t column = 0);
    
    ASTNode* get_lhs() const { return lhs_.get(); }
    ASTNode* get_rhs() const { return rhs_.get(); }
    
    void accept(ASTVisitor& visitor) override;
    
private:
    std::unique_ptr<ASTNode> lhs_;
    std::unique_ptr<ASTNode> rhs_;
};

/**
 * @brief 二元操作节点
 */
class BinaryOpNode : public ASTNode {
public:
    enum class Operator {
        ADD, SUB, MUL, DIV, MOD,    // 算术操作
        EQ, NE, LT, LE, GT, GE,     // 比较操作
        AND, OR, XOR                // 逻辑操作
    };
    
    BinaryOpNode(Operator op, std::unique_ptr<ASTNode> left, std::unique_ptr<ASTNode> right,
                 size_t line = 0, size_t column = 0);
    
    Operator get_operator() const { return operator_; }
    ASTNode* get_left() const { return left_.get(); }
    ASTNode* get_right() const { return right_.get(); }
    
    void accept(ASTVisitor& visitor) override;
    
private:
    Operator operator_;
    std::unique_ptr<ASTNode> left_;
    std::unique_ptr<ASTNode> right_;
};

/**
 * @brief 变量引用节点
 */
class VariableRefNode : public ASTNode {
public:
    explicit VariableRefNode(const std::string& name, size_t line = 0, size_t column = 0);
    
    const std::string& get_name() const { return name_; }
    
    void accept(ASTVisitor& visitor) override;
    
private:
    std::string name_;
};

/**
 * @brief 字面量节点
 */
class LiteralNode : public ASTNode {
public:
    explicit LiteralNode(ASTNodeType literal_type, const std::string& value,
                        size_t line = 0, size_t column = 0);
    
    const std::string& get_value() const { return value_; }
    STDataType get_data_type() const;
    
    void accept(ASTVisitor& visitor) override;
    
private:
    std::string value_;
};

/**
 * @brief AST访问器接口
 */
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    virtual void visit(ProgramNode& node) = 0;
    virtual void visit(VariableDeclarationNode& node) = 0;
    virtual void visit(AssignmentNode& node) = 0;
    virtual void visit(BinaryOpNode& node) = 0;
    virtual void visit(VariableRefNode& node) = 0;
    virtual void visit(LiteralNode& node) = 0;
};

/**
 * @brief AST工厂类
 */
class AST {
public:
    AST();
    ~AST() = default;
    
    void set_root(std::unique_ptr<ASTNode> root);
    ASTNode* get_root() const { return root_.get(); }
    
    // AST遍历
    void traverse(ASTVisitor& visitor);
    
    // 统计信息
    size_t get_node_count() const;
    size_t get_depth() const;
    
private:
    std::unique_ptr<ASTNode> root_;
    size_t calculate_depth(ASTNode* node) const;
    void count_nodes(ASTNode* node, size_t& count) const;
};

} // namespace st_compiler
} // namespace plc_runtime