/**
 * @file FunctionBlockEngine.h
 * @brief 函数块执行引擎
 * 
 * 提供函数块的注册、管理和执行功能，支持优先级调度、错误处理和性能分析
 */

#pragma once

#include "function_block/FunctionBlock.h"
#include <cstdint>

// 临时错误码定义（等待错误处理系统重构）
namespace plc_runtime {
namespace error {
    enum class ErrorCode : uint32_t {
        OK = 0,
        ERROR = 1,
        INVALID_PARAM = 2,
        NOT_FOUND = 3,
        TIMEOUT = 4
    };
}
}
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>

namespace Uranus {

/**
 * @brief 函数块执行模式
 */
enum class FBExecutionMode {
    SEQUENTIAL,     ///< 顺序执行
    PARALLEL,       ///< 并行执行
    PRIORITY_BASED  ///< 基于优先级执行
};

/**
 * @brief 函数块优先级
 */
enum class FBPriority {
    CRITICAL = 0,   ///< 关键优先级
    HIGH = 1,       ///< 高优先级
    NORMAL = 2,     ///< 普通优先级
    LOW = 3,        ///< 低优先级
    BACKGROUND = 4  ///< 后台优先级
};

/**
 * @brief 函数块状态
 */
enum class FBState {
    INACTIVE,       ///< 未激活
    ACTIVE,         ///< 激活
    DISABLED,       ///< 禁用
    ERROR           ///< 错误状态
};

/**
 * @brief 函数块信息
 */
struct FunctionBlockInfo {
    std::string name;                           ///< 函数块名称
    std::shared_ptr<FunctionBlock> instance;   ///< 函数块实例
    FBPriority priority;                        ///< 优先级
    FBState state;                              ///< 状态
    bool enabled;                               ///< 是否启用
    uint64_t executionCount;                    ///< 执行次数
    uint64_t errorCount;                        ///< 错误次数
    double totalExecutionTimeUs;               ///< 总执行时间（微秒）
    double maxExecutionTimeUs;                  ///< 最大执行时间（微秒）
    double avgExecutionTimeUs;                  ///< 平均执行时间（微秒）
    std::chrono::high_resolution_clock::time_point lastExecutionTime; ///< 最后执行时间
    
    FunctionBlockInfo() 
        : priority(FBPriority::NORMAL)
        , state(FBState::INACTIVE)
        , enabled(true)
        , executionCount(0)
        , errorCount(0)
        , totalExecutionTimeUs(0.0)
        , maxExecutionTimeUs(0.0)
        , avgExecutionTimeUs(0.0) {}
};

/**
 * @brief 函数块执行引擎统计信息
 */
struct FBEngineStatistics {
    uint64_t totalCycles;                       ///< 总周期数
    uint64_t totalExecutions;                   ///< 总执行次数
    uint64_t totalErrors;                       ///< 总错误次数
    double averageCycleTimeUs;                  ///< 平均周期时间（微秒）
    double maxCycleTimeUs;                      ///< 最大周期时间（微秒）
    double totalCycleTimeUs;                    ///< 总周期时间（微秒）
    uint32_t activeFunctionBlocks;              ///< 活跃函数块数量
    uint32_t disabledFunctionBlocks;            ///< 禁用函数块数量
    uint32_t errorFunctionBlocks;               ///< 错误函数块数量
    
    FBEngineStatistics()
        : totalCycles(0)
        , totalExecutions(0)
        , totalErrors(0)
        , averageCycleTimeUs(0.0)
        , maxCycleTimeUs(0.0)
        , totalCycleTimeUs(0.0)
        , activeFunctionBlocks(0)
        , disabledFunctionBlocks(0)
        , errorFunctionBlocks(0) {}
};

/**
 * @brief 函数块执行引擎配置
 */
struct FunctionBlockEngineConfig {
    uint32_t maxFunctionBlocks;                 ///< 最大函数块数量
    FBExecutionMode executionMode;              ///< 执行模式
    bool enableProfiling;                       ///< 启用性能分析
    bool enableErrorRecovery;                   ///< 启用错误恢复
    uint32_t maxErrorsPerCycle;                 ///< 每周期最大错误数
    double maxCycleTimeUs;                      ///< 最大周期时间（微秒）
    bool enableWatchdog;                        ///< 启用看门狗
    double watchdogTimeoutUs;                   ///< 看门狗超时时间（微秒）
    
    FunctionBlockEngineConfig()
        : maxFunctionBlocks(1000)
        , executionMode(FBExecutionMode::SEQUENTIAL)
        , enableProfiling(false)
        , enableErrorRecovery(true)
        , maxErrorsPerCycle(10)
        , maxCycleTimeUs(1000.0)
        , enableWatchdog(false)
        , watchdogTimeoutUs(5000.0) {}
};

/**
 * @brief 函数块执行引擎
 * 
 * 负责管理和执行所有注册的函数块，提供优先级调度、错误处理和性能监控功能
 */
class FunctionBlockEngine {
public:
    /**
     * @brief 构造函数
     * @param config 引擎配置
     */
    explicit FunctionBlockEngine(const FunctionBlockEngineConfig& config = FunctionBlockEngineConfig());
    
    /**
     * @brief 析构函数
     */
    ~FunctionBlockEngine();
    
    /**
     * @brief 初始化引擎
     * @return 错误码
     */
    plc_runtime::error::ErrorCode initialize();
    
    /**
     * @brief 关闭引擎
     * @return 错误码
     */
    plc_runtime::error::ErrorCode shutdown();
    
    /**
     * @brief 注册函数块
     * @param name 函数块名称
     * @param functionBlock 函数块实例
     * @param priority 优先级
     * @return 错误码
     */
    plc_runtime::error::ErrorCode registerFunctionBlock(
        const std::string& name,
        std::shared_ptr<FunctionBlock> functionBlock,
        FBPriority priority = FBPriority::NORMAL
    );
    
    /**
     * @brief 注销函数块
     * @param name 函数块名称
     * @return 错误码
     */
    plc_runtime::error::ErrorCode unregisterFunctionBlock(const std::string& name);
    
    /**
     * @brief 启用/禁用函数块
     * @param name 函数块名称
     * @param enabled 是否启用
     * @return 错误码
     */
    plc_runtime::error::ErrorCode enableFunctionBlock(const std::string& name, bool enabled);
    
    /**
     * @brief 执行一个周期
     * @return 错误码
     */
    plc_runtime::error::ErrorCode executeCycle();
    
    /**
     * @brief 获取函数块数量
     * @return 函数块数量
     */
    uint32_t getFunctionBlockCount() const;
    
    /**
     * @brief 获取函数块信息
     * @param name 函数块名称
     * @return 函数块信息指针，如果不存在返回nullptr
     */
    const FunctionBlockInfo* getFunctionBlockInfo(const std::string& name) const;
    
    /**
     * @brief 获取所有函数块信息
     * @return 函数块信息列表
     */
    std::vector<FunctionBlockInfo> getAllFunctionBlockInfo() const;
    
    /**
     * @brief 获取统计信息
     * @return 统计信息
     */
    FBEngineStatistics getStatistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics();
    
    /**
     * @brief 设置执行模式
     * @param mode 执行模式
     * @return 错误码
     */
    plc_runtime::error::ErrorCode setExecutionMode(FBExecutionMode mode);
    
    /**
     * @brief 获取执行模式
     * @return 执行模式
     */
    FBExecutionMode getExecutionMode() const;
    
    /**
     * @brief 启用/禁用性能分析
     * @param enabled 是否启用
     */
    void enableProfiling(bool enabled);
    
    /**
     * @brief 检查是否启用性能分析
     * @return 是否启用
     */
    bool isProfilingEnabled() const;
    
    /**
     * @brief 清除所有错误
     */
    void clearAllErrors();
    
    /**
     * @brief 获取错误函数块列表
     * @return 错误函数块名称列表
     */
    std::vector<std::string> getErrorFunctionBlocks() const;
    
private:
    /**
     * @brief 执行单个函数块
     * @param info 函数块信息
     * @return 错误码
     */
    plc_runtime::error::ErrorCode executeFunctionBlock(FunctionBlockInfo& info);
    
    /**
     * @brief 按优先级排序函数块
     * @return 排序后的函数块信息指针列表
     */
    std::vector<FunctionBlockInfo*> getSortedFunctionBlocks();
    
    /**
     * @brief 更新统计信息
     * @param cycleTimeUs 周期时间（微秒）
     * @param executionCount 执行次数
     * @param errorCount 错误次数
     */
    void updateStatistics(double cycleTimeUs, uint32_t executionCount, uint32_t errorCount);
    
    /**
     * @brief 检查看门狗超时
     * @return 是否超时
     */
    bool checkWatchdogTimeout() const;
    
    /**
     * @brief 处理函数块错误
     * @param info 函数块信息
     * @param errorCode 错误码
     */
    void handleFunctionBlockError(FunctionBlockInfo& info, plc_runtime::error::ErrorCode errorCode);
    
private:
    FunctionBlockEngineConfig m_config;                                     ///< 引擎配置
    std::unordered_map<std::string, FunctionBlockInfo> m_functionBlocks;   ///< 函数块映射
    mutable std::mutex m_mutex;                                             ///< 互斥锁
    std::atomic<bool> m_initialized;                                        ///< 是否已初始化
    std::atomic<bool> m_running;                                            ///< 是否正在运行
    
    // 统计信息
    mutable std::mutex m_statsMutex;                                        ///< 统计信息互斥锁
    FBEngineStatistics m_statistics;                                        ///< 统计信息
    
    // 性能监控
    std::chrono::high_resolution_clock::time_point m_cycleStartTime;        ///< 周期开始时间
    std::chrono::high_resolution_clock::time_point m_lastWatchdogTime;      ///< 最后看门狗时间
    
    // 错误处理
    uint32_t m_currentCycleErrors;                                          ///< 当前周期错误数
    std::vector<std::string> m_errorFunctionBlocks;                        ///< 错误函数块列表
};

} // namespace Uranus