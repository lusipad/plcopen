/**
 * @file FunctionBlockEngine.cpp
 * @brief 函数块执行引擎实现
 */

#include "function_block/FunctionBlockEngine.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace Uranus {

FunctionBlockEngine::FunctionBlockEngine(const FunctionBlockEngineConfig& config)
    : config_(config) {
}

FunctionBlockEngine::~FunctionBlockEngine() {
    shutdown();
}

plc_runtime::error::ErrorCode FunctionBlockEngine::initialize() {
    if (initialized_) {
        return plc_runtime::error::ErrorCode::SUCCESS;
    }
    
    // 初始化统计信息
    statistics_ = FunctionBlockStatistics{};
    
    // 预留执行顺序容器空间
    executionOrder_.reserve(config_.maxFunctionBlocks);
    
    initialized_ = true;
    return plc_runtime::error::ErrorCode::SUCCESS;
}

void FunctionBlockEngine::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // 清理所有函数块
    functionBlocks_.clear();
    executionOrder_.clear();
    
    initialized_ = false;
}

plc_runtime::error::ErrorCode FunctionBlockEngine::registerFunctionBlock(
    const std::string& name,
    std::shared_ptr<FbBaseType> functionBlock,
    FBPriority priority) {
    
    if (!initialized_) {
        return plc_runtime::error::ErrorCode::INITIALIZATION_FAILED;
    }
    
    if (!functionBlock) {
        return plc_runtime::error::ErrorCode::INVALID_PARAMETER;
    }
    
    // 检查是否已存在
    if (functionBlocks_.find(name) != functionBlocks_.end()) {
        return plc_runtime::error::ErrorCode::RESOURCE_ALREADY_EXISTS;
    }
    
    // 检查数量限制
    if (functionBlocks_.size() >= config_.maxFunctionBlocks) {
        return plc_runtime::error::ErrorCode::RESOURCE_LIMIT_EXCEEDED;
    }
    
    // 创建函数块信息
    FunctionBlockInfo info;
    info.name = name;
    info.functionBlock = functionBlock;
    info.priority = priority;
    info.enabled = true;
    info.executionCount = 0;
    info.totalExecutionTimeUs = 0.0;
    info.lastExecutionTimeUs = 0.0;
    
    // 添加到容器
    functionBlocks_[name] = info;
    
    // 更新执行顺序
    updateExecutionOrder();
    
    return plc_runtime::error::ErrorCode::SUCCESS;
}

plc_runtime::error::ErrorCode FunctionBlockEngine::unregisterFunctionBlock(const std::string& name) {
    if (!initialized_) {
        return plc_runtime::error::ErrorCode::INITIALIZATION_FAILED;
    }
    
    auto it = functionBlocks_.find(name);
    if (it == functionBlocks_.end()) {
        return plc_runtime::error::ErrorCode::RESOURCE_NOT_FOUND;
    }
    
    // 从容器中移除
    functionBlocks_.erase(it);
    
    // 更新执行顺序
    updateExecutionOrder();
    
    return plc_runtime::error::ErrorCode::SUCCESS;
}

plc_runtime::error::ErrorCode FunctionBlockEngine::enableFunctionBlock(const std::string& name, bool enabled) {
    if (!initialized_) {
        return plc_runtime::error::ErrorCode::INITIALIZATION_FAILED;
    }
    
    auto it = functionBlocks_.find(name);
    if (it == functionBlocks_.end()) {
        return plc_runtime::error::ErrorCode::RESOURCE_NOT_FOUND;
    }
    
    it->second.enabled = enabled;
    return plc_runtime::error::ErrorCode::SUCCESS;
}

plc_runtime::error::ErrorCode FunctionBlockEngine::executeCycle() {
    if (!initialized_) {
        return plc_runtime::error::ErrorCode::INITIALIZATION_FAILED;
    }
    
    // 记录周期开始时间
    if (config_.enableProfiling) {
        cycleStartTime_ = std::chrono::high_resolution_clock::now();
    }
    
    size_t errorCount = 0;
    size_t executionCount = 0;
    
    // 按执行顺序执行函数块
    for (const auto& name : executionOrder_) {
        auto it = functionBlocks_.find(name);
        if (it != functionBlocks_.end() && it->second.enabled) {
            if (executeFunctionBlock(it->second)) {
                executionCount++;
            } else {
                errorCount++;
                if (errorCount >= config_.maxErrorsPerCycle) {
                    break;
                }
            }
        }
    }
    
    // 更新统计信息
    if (config_.enableProfiling) {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - cycleStartTime_);
        updateStatistics(static_cast<double>(duration.count()));
    }
    
    statistics_.totalCycles++;
    statistics_.totalExecutions += executionCount;
    statistics_.totalErrors += errorCount;
    
    return plc_runtime::error::ErrorCode::SUCCESS;
}

size_t FunctionBlockEngine::getFunctionBlockCount() const {
    return functionBlocks_.size();
}

FunctionBlockStatistics FunctionBlockEngine::getStatistics() const {
    return statistics_;
}

const FunctionBlockInfo* FunctionBlockEngine::getFunctionBlockInfo(const std::string& name) const {
    auto it = functionBlocks_.find(name);
    if (it != functionBlocks_.end()) {
        return &it->second;
    }
    return nullptr;
}

void FunctionBlockEngine::resetStatistics() {
    statistics_ = FunctionBlockStatistics{};
    
    // 重置所有函数块的统计信息
    for (auto& pair : functionBlocks_) {
        pair.second.executionCount = 0;
        pair.second.totalExecutionTimeUs = 0.0;
        pair.second.lastExecutionTimeUs = 0.0;
    }
}

void FunctionBlockEngine::updateExecutionOrder() {
    executionOrder_.clear();
    
    // 根据执行模式确定执行顺序
    switch (config_.executionMode) {
        case FBExecutionMode::SEQUENTIAL:
            // 按注册顺序执行
            for (const auto& pair : functionBlocks_) {
                executionOrder_.push_back(pair.first);
            }
            break;
            
        case FBExecutionMode::PRIORITY:
            // 按优先级排序
            std::vector<std::pair<std::string, FBPriority>> priorityList;
            for (const auto& pair : functionBlocks_) {
                priorityList.emplace_back(pair.first, pair.second.priority);
            }
            
            std::sort(priorityList.begin(), priorityList.end(),
                [](const auto& a, const auto& b) {
                    return static_cast<int>(a.second) > static_cast<int>(b.second);
                });
            
            for (const auto& item : priorityList) {
                executionOrder_.push_back(item.first);
            }
            break;
            
        case FBExecutionMode::PARALLEL:
            // 并行执行模式暂时按顺序执行
            for (const auto& pair : functionBlocks_) {
                executionOrder_.push_back(pair.first);
            }
            break;
    }
}

bool FunctionBlockEngine::executeFunctionBlock(FunctionBlockInfo& info) {
    if (!info.functionBlock || !info.enabled) {
        return false;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        // 执行函数块
        info.functionBlock->call();
        
        // 更新执行统计
        info.executionCount++;
        
        if (config_.enableProfiling) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            info.lastExecutionTimeUs = static_cast<double>(duration.count());
            info.totalExecutionTimeUs += info.lastExecutionTimeUs;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        // 记录异常
        std::cerr << "Function block " << info.name << " execution failed: " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        // 记录未知异常
        std::cerr << "Function block " << info.name << " execution failed: unknown exception" << std::endl;
        return false;
    }
}

void FunctionBlockEngine::updateStatistics(double cycleTimeUs) {
    if (statistics_.totalCycles == 0) {
        statistics_.minCycleTimeUs = cycleTimeUs;
        statistics_.maxCycleTimeUs = cycleTimeUs;
        statistics_.averageCycleTimeUs = cycleTimeUs;
    } else {
        // 更新最小/最大周期时间
        if (cycleTimeUs < statistics_.minCycleTimeUs) {
            statistics_.minCycleTimeUs = cycleTimeUs;
        }
        if (cycleTimeUs > statistics_.maxCycleTimeUs) {
            statistics_.maxCycleTimeUs = cycleTimeUs;
        }
        
        // 更新平均周期时间
        statistics_.averageCycleTimeUs = 
            (statistics_.averageCycleTimeUs * statistics_.totalCycles + cycleTimeUs) / 
            (statistics_.totalCycles + 1);
    }
}

} // namespace Uranus