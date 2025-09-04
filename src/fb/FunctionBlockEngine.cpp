/**
 * @file FunctionBlockEngine.cpp
 * @brief 函数块执行引擎实现
 */

#include "FunctionBlockEngine.h"
#include "memory/memory_manager.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>

namespace Uranus {

using namespace plc_runtime::error;
using namespace plc_runtime::memory;

FunctionBlockEngine::FunctionBlockEngine(const FunctionBlockEngineConfig& config)
    : m_config(config)
    , m_initialized(false)
    , m_running(false)
    , m_currentCycleErrors(0) {
}

FunctionBlockEngine::~FunctionBlockEngine() {
    if (m_initialized.load()) {
        shutdown();
    }
}

ErrorCode FunctionBlockEngine::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized.load()) {
        return ErrorCode::ALREADY_INITIALIZED;
    }
    
    // 检查内存管理器是否已初始化
    auto* memManager = GetMemoryManager();
    if (!memManager) {
        return ErrorCode::MEMORY_MANAGER_NOT_INITIALIZED;
    }
    
    // 初始化统计信息
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_statistics = FBEngineStatistics();
    }
    
    // 清空函数块列表
    m_functionBlocks.clear();
    m_errorFunctionBlocks.clear();
    m_currentCycleErrors = 0;
    
    // 初始化时间戳
    m_cycleStartTime = std::chrono::high_resolution_clock::now();
    m_lastWatchdogTime = m_cycleStartTime;
    
    m_initialized.store(true);
    
    return ErrorCode::SUCCESS;
}

ErrorCode FunctionBlockEngine::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized.load()) {
        return ErrorCode::NOT_INITIALIZED;
    }
    
    m_running.store(false);
    
    // 清理所有函数块
    m_functionBlocks.clear();
    m_errorFunctionBlocks.clear();
    
    m_initialized.store(false);
    
    return ErrorCode::SUCCESS;
}

ErrorCode FunctionBlockEngine::registerFunctionBlock(
    const std::string& name,
    std::shared_ptr<FunctionBlock> functionBlock,
    FBPriority priority) {
    
    if (!m_initialized.load()) {
        return ErrorCode::NOT_INITIALIZED;
    }
    
    if (name.empty() || !functionBlock) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 检查是否已存在
    if (m_functionBlocks.find(name) != m_functionBlocks.end()) {
        return ErrorCode::ALREADY_EXISTS;
    }
    
    // 检查数量限制
    if (m_functionBlocks.size() >= m_config.maxFunctionBlocks) {
        return ErrorCode::RESOURCE_LIMIT_EXCEEDED;
    }
    
    // 创建函数块信息
    FunctionBlockInfo info;
    info.name = name;
    info.instance = functionBlock;
    info.priority = priority;
    info.state = FBState::ACTIVE;
    info.enabled = true;
    info.lastExecutionTime = std::chrono::high_resolution_clock::now();
    
    // 添加到映射
    m_functionBlocks[name] = std::move(info);
    
    return ErrorCode::SUCCESS;
}

ErrorCode FunctionBlockEngine::unregisterFunctionBlock(const std::string& name) {
    if (!m_initialized.load()) {
        return ErrorCode::NOT_INITIALIZED;
    }
    
    if (name.empty()) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_functionBlocks.find(name);
    if (it == m_functionBlocks.end()) {
        return ErrorCode::NOT_FOUND;
    }
    
    // 从错误列表中移除
    auto errorIt = std::find(m_errorFunctionBlocks.begin(), m_errorFunctionBlocks.end(), name);
    if (errorIt != m_errorFunctionBlocks.end()) {
        m_errorFunctionBlocks.erase(errorIt);
    }
    
    // 移除函数块
    m_functionBlocks.erase(it);
    
    return ErrorCode::SUCCESS;
}

ErrorCode FunctionBlockEngine::enableFunctionBlock(const std::string& name, bool enabled) {
    if (!m_initialized.load()) {
        return ErrorCode::NOT_INITIALIZED;
    }
    
    if (name.empty()) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_functionBlocks.find(name);
    if (it == m_functionBlocks.end()) {
        return ErrorCode::NOT_FOUND;
    }
    
    it->second.enabled = enabled;
    if (enabled) {
        it->second.state = FBState::ACTIVE;
    } else {
        it->second.state = FBState::DISABLED;
    }
    
    return ErrorCode::SUCCESS;
}

ErrorCode FunctionBlockEngine::executeCycle() {
    if (!m_initialized.load()) {
        return ErrorCode::NOT_INITIALIZED;
    }
    
    auto cycleStartTime = std::chrono::high_resolution_clock::now();
    m_running.store(true);
    m_currentCycleErrors = 0;
    
    uint32_t executionCount = 0;
    uint32_t errorCount = 0;
    
    try {
        // 检查看门狗超时
        if (m_config.enableWatchdog && checkWatchdogTimeout()) {
            return ErrorCode::WATCHDOG_TIMEOUT;
        }
        
        // 获取排序后的函数块列表
        auto sortedFBs = getSortedFunctionBlocks();
        
        // 根据执行模式执行函数块
        switch (m_config.executionMode) {
            case FBExecutionMode::SEQUENTIAL:
            case FBExecutionMode::PRIORITY_BASED: {
                for (auto* fbInfo : sortedFBs) {
                    if (!fbInfo->enabled || fbInfo->state != FBState::ACTIVE) {
                        continue;
                    }
                    
                    auto result = executeFunctionBlock(*fbInfo);
                    executionCount++;
                    
                    if (result != ErrorCode::SUCCESS) {
                        errorCount++;
                        m_currentCycleErrors++;
                        
                        if (m_currentCycleErrors >= m_config.maxErrorsPerCycle) {
                            break; // 达到最大错误数，停止执行
                        }
                    }
                }
                break;
            }
            
            case FBExecutionMode::PARALLEL: {
                // 并行执行（简化实现，实际可能需要线程池）
                std::vector<std::thread> threads;
                std::mutex resultMutex;
                
                for (auto* fbInfo : sortedFBs) {
                    if (!fbInfo->enabled || fbInfo->state != FBState::ACTIVE) {
                        continue;
                    }
                    
                    threads.emplace_back([this, fbInfo, &executionCount, &errorCount, &resultMutex]() {
                        auto result = executeFunctionBlock(*fbInfo);
                        
                        std::lock_guard<std::mutex> lock(resultMutex);
                        executionCount++;
                        if (result != ErrorCode::SUCCESS) {
                            errorCount++;
                        }
                    });
                }
                
                // 等待所有线程完成
                for (auto& thread : threads) {
                    thread.join();
                }
                break;
            }
        }
        
    } catch (const std::exception& e) {
        // 处理异常
        errorCount++;
        return ErrorCode::EXECUTION_FAILED;
    }
    
    // 计算周期时间
    auto cycleEndTime = std::chrono::high_resolution_clock::now();
    auto cycleDuration = std::chrono::duration_cast<std::chrono::microseconds>(cycleEndTime - cycleStartTime);
    double cycleTimeUs = static_cast<double>(cycleDuration.count());
    
    // 检查周期时间限制
    if (m_config.maxCycleTimeUs > 0 && cycleTimeUs > m_config.maxCycleTimeUs) {
        return ErrorCode::CYCLE_TIME_EXCEEDED;
    }
    
    // 更新统计信息
    updateStatistics(cycleTimeUs, executionCount, errorCount);
    
    // 更新看门狗时间
    m_lastWatchdogTime = cycleEndTime;
    
    m_running.store(false);
    
    return ErrorCode::SUCCESS;
}

uint32_t FunctionBlockEngine::getFunctionBlockCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32_t>(m_functionBlocks.size());
}

const FunctionBlockInfo* FunctionBlockEngine::getFunctionBlockInfo(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_functionBlocks.find(name);
    if (it != m_functionBlocks.end()) {
        return &it->second;
    }
    
    return nullptr;
}

std::vector<FunctionBlockInfo> FunctionBlockEngine::getAllFunctionBlockInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<FunctionBlockInfo> result;
    result.reserve(m_functionBlocks.size());
    
    for (const auto& pair : m_functionBlocks) {
        result.push_back(pair.second);
    }
    
    return result;
}

FBEngineStatistics FunctionBlockEngine::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_statistics;
}

void FunctionBlockEngine::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statistics = FBEngineStatistics();
}

ErrorCode FunctionBlockEngine::setExecutionMode(FBExecutionMode mode) {
    if (m_running.load()) {
        return ErrorCode::OPERATION_IN_PROGRESS;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.executionMode = mode;
    
    return ErrorCode::SUCCESS;
}

FBExecutionMode FunctionBlockEngine::getExecutionMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.executionMode;
}

void FunctionBlockEngine::enableProfiling(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.enableProfiling = enabled;
}

bool FunctionBlockEngine::isProfilingEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.enableProfiling;
}

void FunctionBlockEngine::clearAllErrors() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& pair : m_functionBlocks) {
        if (pair.second.state == FBState::ERROR) {
            pair.second.state = FBState::ACTIVE;
        }
    }
    
    m_errorFunctionBlocks.clear();
}

std::vector<std::string> FunctionBlockEngine::getErrorFunctionBlocks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_errorFunctionBlocks;
}

ErrorCode FunctionBlockEngine::executeFunctionBlock(FunctionBlockInfo& info) {
    if (!info.instance || !info.enabled) {
        return ErrorCode::INVALID_STATE;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        // 执行函数块
        info.instance->call();
        
        // 更新执行统计
        info.executionCount++;
        info.lastExecutionTime = startTime;
        
        // 计算执行时间
        if (m_config.enableProfiling) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            double executionTimeUs = static_cast<double>(duration.count());
            
            info.totalExecutionTimeUs += executionTimeUs;
            info.maxExecutionTimeUs = std::max(info.maxExecutionTimeUs, executionTimeUs);
            info.avgExecutionTimeUs = info.totalExecutionTimeUs / info.executionCount;
        }
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        // 处理异常
        handleFunctionBlockError(info, ErrorCode::EXECUTION_FAILED);
        return ErrorCode::EXECUTION_FAILED;
    }
}

std::vector<FunctionBlockInfo*> FunctionBlockEngine::getSortedFunctionBlocks() {
    std::vector<FunctionBlockInfo*> result;
    result.reserve(m_functionBlocks.size());
    
    for (auto& pair : m_functionBlocks) {
        result.push_back(&pair.second);
    }
    
    // 按优先级排序（优先级数值越小，优先级越高）
    std::sort(result.begin(), result.end(), 
        [](const FunctionBlockInfo* a, const FunctionBlockInfo* b) {
            return static_cast<int>(a->priority) < static_cast<int>(b->priority);
        });
    
    return result;
}

void FunctionBlockEngine::updateStatistics(double cycleTimeUs, uint32_t executionCount, uint32_t errorCount) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    m_statistics.totalCycles++;
    m_statistics.totalExecutions += executionCount;
    m_statistics.totalErrors += errorCount;
    m_statistics.totalCycleTimeUs += cycleTimeUs;
    
    // 更新平均周期时间
    m_statistics.averageCycleTimeUs = m_statistics.totalCycleTimeUs / m_statistics.totalCycles;
    
    // 更新最大周期时间
    m_statistics.maxCycleTimeUs = std::max(m_statistics.maxCycleTimeUs, cycleTimeUs);
    
    // 统计函数块状态
    m_statistics.activeFunctionBlocks = 0;
    m_statistics.disabledFunctionBlocks = 0;
    m_statistics.errorFunctionBlocks = 0;
    
    for (const auto& pair : m_functionBlocks) {
        switch (pair.second.state) {
            case FBState::ACTIVE:
                m_statistics.activeFunctionBlocks++;
                break;
            case FBState::DISABLED:
                m_statistics.disabledFunctionBlocks++;
                break;
            case FBState::ERROR:
                m_statistics.errorFunctionBlocks++;
                break;
            default:
                break;
        }
    }
}

bool FunctionBlockEngine::checkWatchdogTimeout() const {
    if (!m_config.enableWatchdog) {
        return false;
    }
    
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto timeSinceLastWatchdog = std::chrono::duration_cast<std::chrono::microseconds>(
        currentTime - m_lastWatchdogTime);
    
    return timeSinceLastWatchdog.count() > m_config.watchdogTimeoutUs;
}

void FunctionBlockEngine::handleFunctionBlockError(FunctionBlockInfo& info, ErrorCode errorCode) {
    info.errorCount++;
    info.state = FBState::ERROR;
    
    // 添加到错误列表
    auto it = std::find(m_errorFunctionBlocks.begin(), m_errorFunctionBlocks.end(), info.name);
    if (it == m_errorFunctionBlocks.end()) {
        m_errorFunctionBlocks.push_back(info.name);
    }
    
    // 如果启用错误恢复，尝试恢复
    if (m_config.enableErrorRecovery) {
        // 简单的错误恢复策略：重置函数块状态
        info.state = FBState::ACTIVE;
    }
}

} // namespace Uranus