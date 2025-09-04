/**
 * @file error_handler.h
 * @brief 错误处理器
 * 
 * 基于ADR-006错误处理策略的统一错误处理系统
 * 提供错误记录、恢复策略和通知机制
 */

#ifndef ERROR_ERROR_HANDLER_H
#define ERROR_ERROR_HANDLER_H

#include "error_codes.h"
#include "lockfree/atomic_utils.h"
#include <functional>
#include <memory>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>
#include <array>
#include <fstream>

namespace plc_runtime {
namespace error {

/**
 * @brief 错误上下文信息
 */
struct ErrorContext {
    ErrorCode code;
    std::string component;      // 发生错误的组件
    std::string function;       // 发生错误的函数
    std::string file;          // 发生错误的文件
    int line;                  // 发生错误的行号
    std::string message;       // 自定义错误消息
    std::chrono::system_clock::time_point timestamp;
    uint64_t threadId;         // 线程ID
    
    ErrorContext() = default;
    
    ErrorContext(ErrorCode code, 
                const std::string& component,
                const std::string& function,
                const std::string& file,
                int line,
                const std::string& message = "")
        : code(code)
        , component(component)
        , function(function)
        , file(file)
        , line(line)
        , message(message)
        , timestamp(std::chrono::system_clock::now())
        , threadId(std::hash<std::thread::id>{}(std::this_thread::get_id())) {}
};

/**
 * @brief 错误恢复动作
 */
enum class RecoveryAction {
    NONE,           // 无动作
    RETRY,          // 重试操作
    FALLBACK,       // 使用备用方案
    RESTART,        // 重启组件
    SHUTDOWN,       // 关闭系统
    EMERGENCY_STOP  // 紧急停止
};

/**
 * @brief 错误恢复策略
 */
struct RecoveryStrategy {
    ErrorCode errorCode;
    RecoveryAction action;
    uint32_t maxRetries;        // 最大重试次数
    uint32_t retryDelayMs;      // 重试延迟 (毫秒)
    std::function<bool()> condition;  // 恢复条件检查
    std::function<bool()> handler;    // 恢复处理函数
    
    RecoveryStrategy() = default;
    
    RecoveryStrategy(ErrorCode code, RecoveryAction act, 
                    uint32_t retries = 0, uint32_t delay = 0)
        : errorCode(code), action(act), maxRetries(retries), retryDelayMs(delay) {}
};

/**
 * @brief 错误事件监听器
 */
class ErrorEventListener {
public:
    virtual ~ErrorEventListener() = default;
    
    /**
     * @brief 错误发生时的回调
     * @param context 错误上下文
     */
    virtual void onError(const ErrorContext& context) = 0;
    
    /**
     * @brief 错误恢复时的回调
     * @param context 错误上下文
     * @param action 恢复动作
     * @param success 恢复是否成功
     */
    virtual void onRecovery(const ErrorContext& context, 
                           RecoveryAction action, 
                           bool success) = 0;
};

/**
 * @brief 错误统计信息
 */
struct ErrorStatistics {
    lockfree::CacheAlignedAtomic<uint64_t> totalErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> fatalErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> criticalErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> errors{0};
    lockfree::CacheAlignedAtomic<uint64_t> warnings{0};
    lockfree::CacheAlignedAtomic<uint64_t> recoveredErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> unrecoveredErrors{0};
    
    // 按类别统计
    lockfree::CacheAlignedAtomic<uint64_t> schedulerErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> memoryErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> ioErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> communicationErrors{0};
    lockfree::CacheAlignedAtomic<uint64_t> motionErrors{0};
    
    std::chrono::system_clock::time_point lastErrorTime;
    ErrorCode lastErrorCode{ErrorCode::SUCCESS};
    
    /**
     * @brief 重置统计信息
     */
    void reset() {
        totalErrors.store(0);
        fatalErrors.store(0);
        criticalErrors.store(0);
        errors.store(0);
        warnings.store(0);
        recoveredErrors.store(0);
        unrecoveredErrors.store(0);
        schedulerErrors.store(0);
        memoryErrors.store(0);
        ioErrors.store(0);
        communicationErrors.store(0);
        motionErrors.store(0);
        lastErrorCode = ErrorCode::SUCCESS;
    }
};

/**
 * @brief 错误处理器
 */
class ErrorHandler {
private:
    // 恢复策略映射
    std::unordered_map<ErrorCode, RecoveryStrategy> recoveryStrategies_;
    
    // 事件监听器列表
    std::vector<std::shared_ptr<ErrorEventListener>> listeners_;
    
    // 错误统计
    ErrorStatistics statistics_;
    
    // 错误历史 (环形缓冲区)
    static constexpr size_t MAX_ERROR_HISTORY = 1000;
    std::array<ErrorContext, MAX_ERROR_HISTORY> errorHistory_;
    lockfree::CacheAlignedAtomic<size_t> historyIndex_{0};
    
    // 线程安全保护 (仅用于配置操作)
    mutable lockfree::SpinLock configLock_;
    
public:
    /**
     * @brief 构造函数
     */
    ErrorHandler();
    
    /**
     * @brief 析构函数
     */
    ~ErrorHandler() = default;
    
    // 禁用拷贝和移动
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    ErrorHandler(ErrorHandler&&) = delete;
    ErrorHandler& operator=(ErrorHandler&&) = delete;
    
    /**
     * @brief 处理错误
     * @param context 错误上下文
     * @return 恢复动作
     */
    RecoveryAction handleError(const ErrorContext& context);
    
    /**
     * @brief 注册恢复策略
     * @param strategy 恢复策略
     */
    void registerRecoveryStrategy(const RecoveryStrategy& strategy);
    
    /**
     * @brief 移除恢复策略
     * @param errorCode 错误码
     */
    void removeRecoveryStrategy(ErrorCode errorCode);
    
    /**
     * @brief 添加事件监听器
     * @param listener 监听器
     */
    void addListener(std::shared_ptr<ErrorEventListener> listener);
    
    /**
     * @brief 移除事件监听器
     * @param listener 监听器
     */
    void removeListener(std::shared_ptr<ErrorEventListener> listener);
    
    /**
     * @brief 获取错误统计
     * @return 错误统计信息
     */
    ErrorStatistics getStatistics() const;
    
    /**
     * @brief 获取错误历史
     * @param maxCount 最大返回数量
     * @return 错误历史列表
     */
    std::vector<ErrorContext> getErrorHistory(size_t maxCount = 100) const;
    
    /**
     * @brief 清除错误历史
     */
    void clearErrorHistory();
    
    /**
     * @brief 重置错误统计
     */
    void resetStatistics();
    
    /**
     * @brief 检查系统健康状态
     * @return true 如果系统健康
     */
    bool isSystemHealthy() const;
    
    /**
     * @brief 获取错误率 (每分钟错误数)
     * @return 错误率
     */
    double getErrorRate() const;

private:
    /**
     * @brief 执行恢复策略
     * @param context 错误上下文
     * @param strategy 恢复策略
     * @return 恢复是否成功
     */
    bool executeRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
    
    /**
     * @brief 通知监听器
     * @param context 错误上下文
     */
    void notifyListeners(const ErrorContext& context);
    
    /**
     * @brief 通知恢复监听器
     * @param context 错误上下文
     * @param action 恢复动作
     * @param success 恢复是否成功
     */
    void notifyRecoveryListeners(const ErrorContext& context, 
                                RecoveryAction action, 
                                bool success);
    
    /**
     * @brief 更新统计信息
     * @param context 错误上下文
     */
    void updateStatistics(const ErrorContext& context);
    
    /**
     * @brief 记录错误历史
     * @param context 错误上下文
     */
    void recordErrorHistory(const ErrorContext& context);
    
    /**
     * @brief 注册默认恢复策略
     */
    void registerDefaultRecoveryStrategies();
    
    /**
     * @brief 执行重试恢复
     */
    bool executeRetryRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
    
    /**
     * @brief 执行备用方案恢复
     */
    bool executeFallbackRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
    
    /**
     * @brief 执行重启恢复
     */
    bool executeRestartRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
    
    /**
     * @brief 执行关闭恢复
     */
    bool executeShutdownRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
    
    /**
     * @brief 执行紧急停止恢复
     */
    bool executeEmergencyStopRecovery(const ErrorContext& context, const RecoveryStrategy& strategy);
};

/**
 * @brief 全局错误处理器实例
 */
ErrorHandler& getGlobalErrorHandler();

/**
 * @brief 错误处理宏
 */
#define PLC_HANDLE_ERROR(code, component, message) \
    do { \
        plc_runtime::error::ErrorContext ctx((code), (component), __FUNCTION__, __FILE__, __LINE__, (message)); \
        plc_runtime::error::getGlobalErrorHandler().handleError(ctx); \
    } while(0)

#define PLC_HANDLE_ERROR_RETURN(code, component, message) \
    do { \
        plc_runtime::error::ErrorContext ctx((code), (component), __FUNCTION__, __FILE__, __LINE__, (message)); \
        plc_runtime::error::getGlobalErrorHandler().handleError(ctx); \
        return (code); \
    } while(0)

#define PLC_HANDLE_ERROR_IF_FAILED(expr, component, message) \
    do { \
        auto __result = (expr); \
        if (PLC_FAILED(__result)) { \
            PLC_HANDLE_ERROR(__result, component, message); \
        } \
    } while(0)

/**
 * @brief 默认错误监听器实现
 */
class DefaultErrorListener : public ErrorEventListener {
public:
    void onError(const ErrorContext& context) override;
    void onRecovery(const ErrorContext& context, RecoveryAction action, bool success) override;
};

/**
 * @brief 文件日志错误监听器
 */
class FileLogErrorListener : public ErrorEventListener {
private:
    std::string logFilePath_;
    std::ofstream logFile_;
    mutable lockfree::SpinLock fileLock_;
    
public:
    explicit FileLogErrorListener(const std::string& logFilePath);
    ~FileLogErrorListener();
    
    void onError(const ErrorContext& context) override;
    void onRecovery(const ErrorContext& context, RecoveryAction action, bool success) override;

private:
    void writeLogEntry(const std::string& entry);
    std::string formatErrorEntry(const ErrorContext& context);
    std::string formatRecoveryEntry(const ErrorContext& context, RecoveryAction action, bool success);
};

/**
 * @brief 系统日志错误监听器 (syslog)
 */
class SyslogErrorListener : public ErrorEventListener {
private:
    std::string facility_;
    
public:
    explicit SyslogErrorListener(const std::string& facility = "plc-runtime");
    ~SyslogErrorListener();
    
    void onError(const ErrorContext& context) override;
    void onRecovery(const ErrorContext& context, RecoveryAction action, bool success) override;

private:
    int severityToSyslogPriority(ErrorSeverity severity);
};

} // namespace error
} // namespace plc_runtime

#endif // ERROR_ERROR_HANDLER_H