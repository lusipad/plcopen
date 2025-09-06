/**
 * @file FunctionBlock.h
 * @brief 功能块基类定义 - IEC 61131-3标准功能块支持
 * @version MVP-1.0
 */

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>

namespace plc_runtime {
namespace fb {

/**
 * @brief 功能块接口基类
 */
class FunctionBlock {
public:
    virtual ~FunctionBlock() = default;
    
    // 功能块执行接口
    virtual void execute() = 0;
    virtual void reset() = 0;
    
    // 参数设置接口
    virtual bool set_input(const std::string& name, const void* value, size_t size) = 0;
    virtual bool get_output(const std::string& name, void* value, size_t size) const = 0;
    
    // 功能块信息
    virtual std::string get_name() const = 0;
    virtual std::string get_version() const = 0;
};

/**
 * @brief 功能块实例管理器
 */
class FunctionBlockManager {
public:
    static FunctionBlockManager& getInstance();
    
    // 功能块注册
    bool register_function_block(const std::string& name, 
                                std::unique_ptr<FunctionBlock> (*creator)());
    
    // 功能块创建
    std::unique_ptr<FunctionBlock> create_function_block(const std::string& name);
    
    // 获取已注册的功能块列表
    std::vector<std::string> get_registered_blocks() const;

private:
    FunctionBlockManager() = default;
    std::unordered_map<std::string, std::unique_ptr<FunctionBlock> (*)()> creators_;
};

} // namespace fb
} // namespace plc_runtime
EOF < /dev/null
