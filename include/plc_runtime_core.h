/**
 * @file plc_runtime_core.h
 * @brief PLC运行时核心系统主头文件
 * 
 * 基于深度技术评审和ADR决策的核心系统接口定义
 * 
 * @version 1.0.0
 * @date 2024-01-XX
 */

#ifndef PLC_RUNTIME_CORE_H
#define PLC_RUNTIME_CORE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

// 版本信息
#define PLC_RUNTIME_VERSION_MAJOR 1
#define PLC_RUNTIME_VERSION_MINOR 0
#define PLC_RUNTIME_VERSION_PATCH 0

namespace plc_runtime {

// 前向声明
class RealtimeScheduler;
class MemoryManager;
class IOManager;
class CommunicationManager;
class FunctionBlockEngine;
class STCompiler;
class MotionController;

/**
 * @brief 系统配置结构
 */
struct SystemConfig {
    // 调度器配置
    uint32_t schedulerCyclePeriodUs = 1000;  // 1ms调度周期
    uint32_t maxTasks = 256;                 // 最大任务数
    
    // 内存配置
    size_t totalMemoryBudget = 64 * 1024 * 1024;  // 64MB总内存预算
    size_t realtimeMemoryBudget = 16 * 1024 * 1024; // 16MB实时内存预算
    
    // I/O配置
    uint32_t maxDigitalIO = 4096;            // 最大数字I/O点数
    uint32_t maxAnalogIO = 1024;             // 最大模拟I/O点数
    uint32_t ioScanPeriodUs = 100;           // 100μs I/O扫描周期
    
    // 通信配置
    uint32_t maxConnections = 64;            // 最大连接数
    uint32_t communicationTimeoutMs = 5000;  // 5s通信超时
    
    // 运动控制配置
    uint32_t maxAxes = 32;                   // 最大轴数
    double positionAccuracy = 1.0;           // 1μm位置精度
    double velocityAccuracy = 0.1;           // 0.1%速度精度
};

/**
 * @brief 系统状态枚举
 */
enum class SystemState {
    UNINITIALIZED,  // 未初始化
    INITIALIZING,   // 初始化中
    READY,          // 就绪
    RUNNING,        // 运行中
    STOPPING,       // 停止中
    STOPPED,        // 已停止
    ERROR           // 错误状态
};

/**
 * @brief 错误代码枚举 (基于ADR-006)
 */
enum class ErrorCode : uint32_t {
    SUCCESS = 0,
    
    // 系统错误 (E500-E599)
    SYSTEM_INIT_FAILED = 500,
    SYSTEM_CONFIG_INVALID = 501,
    SYSTEM_RESOURCE_EXHAUSTED = 502,
    
    // 调度器错误 (E100-E199)
    SCHEDULER_INIT_FAILED = 100,
    SCHEDULER_TASK_CREATE_FAILED = 101,
    SCHEDULER_DEADLINE_MISSED = 102,
    
    // 内存错误 (E200-E299)
    MEMORY_ALLOCATION_FAILED = 200,
    MEMORY_POOL_EXHAUSTED = 201,
    MEMORY_LEAK_DETECTED = 202,
    
    // I/O错误 (E700-E799)
    IO_DEVICE_NOT_FOUND = 700,
    IO_COMMUNICATION_FAILED = 701,
    IO_TIMEOUT = 702,
    
    // 通信错误 (E600-E699)
    COMM_CONNECTION_FAILED = 600,
    COMM_PROTOCOL_ERROR = 601,
    COMM_TIMEOUT = 602,
    
    // 编译器错误 (E300-E399)
    COMPILER_SYNTAX_ERROR = 300,
    COMPILER_SEMANTIC_ERROR = 301,
    COMPILER_CODEGEN_ERROR = 302,
    
    // 运动控制错误 (E800-E899)
    MOTION_AXIS_NOT_FOUND = 800,
    MOTION_LIMIT_EXCEEDED = 801,
    MOTION_EMERGENCY_STOP = 802
};

/**
 * @brief 性能统计结构
 */
struct PerformanceStats {
    // 调度器统计
    double averageSchedulingLatencyUs = 0.0;
    double maxSchedulingLatencyUs = 0.0;
    uint64_t deadlineMissCount = 0;
    
    // I/O统计
    double averageIOScanTimeUs = 0.0;
    double maxIOScanTimeUs = 0.0;
    uint64_t ioErrorCount = 0;
    
    // 内存统计
    size_t totalMemoryUsed = 0;
    size_t peakMemoryUsed = 0;
    uint32_t memoryLeakCount = 0;
    
    // 通信统计
    uint64_t totalPacketsSent = 0;
    uint64_t totalPacketsReceived = 0;
    uint64_t communicationErrors = 0;
    
    // 系统运行时间
    std::chrono::steady_clock::duration uptime{0};
};

/**
 * @brief PLC运行时核心系统主类
 * 
 * 这是整个PLC运行时系统的主要接口类，负责协调各个子系统的工作。
 * 基于ADR决策实现确定性实时性能和无锁架构。
 */
class PLCRuntimeCore {
public:
    /**
     * @brief 构造函数
     * @param config 系统配置
     */
    explicit PLCRuntimeCore(const SystemConfig& config = SystemConfig{});
    
    /**
     * @brief 析构函数
     */
    ~PLCRuntimeCore();
    
    // 禁用拷贝和移动
    PLCRuntimeCore(const PLCRuntimeCore&) = delete;
    PLCRuntimeCore& operator=(const PLCRuntimeCore&) = delete;
    PLCRuntimeCore(PLCRuntimeCore&&) = delete;
    PLCRuntimeCore& operator=(PLCRuntimeCore&&) = delete;
    
    /**
     * @brief 初始化系统
     * @return 错误代码
     */
    ErrorCode initialize();
    
    /**
     * @brief 启动系统
     * @return 错误代码
     */
    ErrorCode start();
    
    /**
     * @brief 停止系统
     * @return 错误代码
     */
    ErrorCode stop();
    
    /**
     * @brief 关闭系统
     * @return 错误代码
     */
    ErrorCode shutdown();
    
    /**
     * @brief 获取系统状态
     * @return 当前系统状态
     */
    SystemState getState() const;
    
    /**
     * @brief 获取性能统计
     * @return 性能统计数据
     */
    PerformanceStats getPerformanceStats() const;
    
    /**
     * @brief 获取实时调度器
     * @return 调度器指针
     */
    RealtimeScheduler* getScheduler() const;
    
    /**
     * @brief 获取内存管理器
     * @return 内存管理器指针
     */
    MemoryManager* getMemoryManager() const;
    
    /**
     * @brief 获取I/O管理器
     * @return I/O管理器指针
     */
    IOManager* getIOManager() const;
    
    /**
     * @brief 获取通信管理器
     * @return 通信管理器指针
     */
    CommunicationManager* getCommunicationManager() const;
    
    /**
     * @brief 获取功能块引擎
     * @return 功能块引擎指针
     */
    FunctionBlockEngine* getFunctionBlockEngine() const;
    
    /**
     * @brief 获取ST编译器
     * @return ST编译器指针
     */
    STCompiler* getSTCompiler() const;
    
    /**
     * @brief 获取运动控制器
     * @return 运动控制器指针
     */
    MotionController* getMotionController() const;
    
    /**
     * @brief 加载PLC程序
     * @param programPath 程序文件路径
     * @return 错误代码
     */
    ErrorCode loadProgram(const std::string& programPath);
    
    /**
     * @brief 卸载PLC程序
     * @return 错误代码
     */
    ErrorCode unloadProgram();
    
    /**
     * @brief 运行单个扫描周期
     * @return 错误代码
     */
    ErrorCode runScanCycle();
    
    /**
     * @brief 设置紧急停止
     */
    void emergencyStop();
    
    /**
     * @brief 重置紧急停止
     */
    void resetEmergencyStop();
    
    /**
     * @brief 检查是否处于紧急停止状态
     * @return true如果处于紧急停止状态
     */
    bool isEmergencyStopped() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * @brief 获取版本字符串
 * @return 版本字符串
 */
std::string getVersionString();

/**
 * @brief 获取构建信息
 * @return 构建信息字符串
 */
std::string getBuildInfo();

/**
 * @brief 检查实时环境
 * @return true如果运行在实时环境中
 */
bool isRealtimeEnvironment();

/**
 * @brief 错误代码转字符串
 * @param code 错误代码
 * @return 错误描述字符串
 */
std::string errorCodeToString(ErrorCode code);

} // namespace plc_runtime

#endif // PLC_RUNTIME_CORE_H
"