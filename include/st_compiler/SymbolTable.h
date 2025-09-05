/**
 * @file SymbolTable.h
 * @brief ST编译器符号表管理
 * @version MVP-1.0
 */

#pragma once

#include "STCompiler.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief 符号表作用域
 */
class Scope {
public:
    explicit Scope(const std::string& name, Scope* parent = nullptr);
    ~Scope() = default;
    
    // 符号管理
    bool add_variable(const VariableInfo& var);
    VariableInfo* lookup_variable(const std::string& name);
    const VariableInfo* lookup_variable(const std::string& name) const;
    
    // 作用域管理
    const std::string& get_name() const { return name_; }
    Scope* get_parent() const { return parent_; }
    size_t get_variable_count() const { return variables_.size(); }
    
    // 内存分配
    size_t calculate_size() const;
    void assign_offsets(size_t base_offset = 0);
    
private:
    std::string name_;
    Scope* parent_;
    std::unordered_map<std::string, VariableInfo> variables_;
};

/**
 * @brief 符号表管理器
 */
class SymbolTable {
public:
    SymbolTable();
    ~SymbolTable() = default;
    
    // 作用域管理
    void enter_scope(const std::string& name);
    void exit_scope();
    Scope* get_current_scope() const { return current_scope_; }
    
    // 变量管理
    bool add_variable(const VariableInfo& var);
    VariableInfo* lookup_variable(const std::string& name);
    const VariableInfo* lookup_variable(const std::string& name) const;
    
    // 全局变量
    bool add_global_variable(const VariableInfo& var);
    const std::vector<VariableInfo>& get_global_variables() const;
    
    // 统计信息
    size_t get_total_variables() const;
    size_t get_total_memory_size() const;
    
private:
    std::unique_ptr<Scope> global_scope_;
    Scope* current_scope_;
    std::vector<std::unique_ptr<Scope>> all_scopes_;
};

} // namespace st_compiler
} // namespace plc_runtime