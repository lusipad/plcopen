/**
 * @file error_handler.cpp
 * @brief 错误处理器实现
 */

#include "error/error_handler.h"
#include "error/error_codes.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <iomanip>
#include <chrono>
#include <memory>

#ifdef __linux__
#include <syslog.h>
#endif

namespace plc_runtime {
namespace error {

/**
 * @brief 错误处理器实现
 */
ErrorHandler::ErrorHandler() {
    // 注册默认恢复策略
    registerDefaultRecoveryStrategies();
}

RecoveryAction ErrorHandler::handleError(const ErrorContext& context) {
    // 更新统计信息
    updateStatistics(context);
    
    // 记录错误历史
    recordErrorHistory(context);
    
    // 通知监听器
    notifyListeners(context);
    
    // 查找恢复策略
    auto it = recoveryStrategies_.find(context.code);
    if (it != recoveryStrategies_.end()) {
        const auto& strategy = it->second;
        
        // 执行恢复策略
        bool success = executeRecovery(context, strategy);
        
        // 通知恢复监听器
        notifyRecoveryListeners(context, strategy.action, success);
        
        // 更新恢复统计
        if (success) {
            statistics_.recoveredErrors.fetch_add(1);
        } else {
            statistics_.unrecoveredErrors.fetch_add(1);
        }
        
        return strategy.action;
    }
    
    // 没有找到恢复策略，根据错误严重程度决定默认动作
    auto severity = ErrorCodeUtils::getSeverity(context.code);
    switch (severity) {
        case ErrorSeverity::FATAL:
            return RecoveryAction::EMERGENCY_STOP;
        case ErrorSeverity::CRITICAL:
            return RecoveryAction::SHUTDOWN;
        case ErrorSeverity::ERROR:
            return RecoveryAction::RESTART;
        case ErrorSeverity::WARNING:
            return RecoveryAction::FALLBACK;
        default:
            return RecoveryAction::NONE;
    }
}

void ErrorHandler::registerRecoveryStrategy(const RecoveryStrategy& strategy) {
    lockfree::SpinLockGuard lock(configLock_);
    recoveryStrategies_[strategy.errorCode] = strategy;
}

void ErrorHandler::removeRecoveryStrategy(ErrorCode errorCode) {
    lockfree::SpinLockGuard lock(configLock_);
    recoveryStrategies_.erase(errorCode);
}

void ErrorHandler::addListener(std::shared_ptr<ErrorEventListener> listener) {
    lockfree::SpinLockGuard lock(configLock_);
    listeners_.push_back(listener);
}

void ErrorHandler::removeListener(std::shared_ptr<ErrorEventListener> listener) {
    lockfree::SpinLockGuard lock(configLock_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
                      [&](const std::weak_ptr<ErrorEventListener>& weak) {
                          return weak.lock() == listener;
                      }),
        listeners_.end());
}

ErrorStatistics ErrorHandler::getStatistics() const {
    ErrorStatistics stats;
    stats.totalErrors.store(statistics_.totalErrors.load());
    stats.fatalErrors.store(statistics_.fatalErrors.load());
    stats.criticalErrors.store(statistics_.criticalErrors.load());
    stats.errors.store(statistics_.errors.load());
    stats.warnings.store(statistics_.warnings.load());
    stats.recoveredErrors.store(statistics_.recoveredErrors.load());
    stats.unrecoveredErrors.store(statistics_.unrecoveredErrors.load());
    stats.schedulerErrors.store(statistics_.schedulerErrors.load());
    stats.memoryErrors.store(statistics_.memoryErrors.load());
    stats.ioErrors.store(statistics_.ioErrors.load());
    stats.communicationErrors.store(statistics_.communicationErrors.load());
    stats.motionErrors.store(statistics_.motionErrors.load());
    stats.lastErrorCode = statistics_.lastErrorCode;
    stats.lastErrorTime = statistics_.lastErrorTime;
    return stats;
}

std::vector<ErrorContext> ErrorHandler::getErrorHistory(size_t maxCount) const {
    std::vector<ErrorContext> history;
    history.reserve(std::min(maxCount, MAX_ERROR_HISTORY));
    
    size_t currentIndex = historyIndex_.load();
    size_t count = std::min(maxCount, MAX_ERROR_HISTORY);
    
    for (size_t i = 0; i < count; ++i) {
        size_t index = (currentIndex - i - 1) % MAX_ERROR_HISTORY;
        const auto& context = errorHistory_[index];
        
        // 检查是否是有效的错误记录
        if (context.code != ErrorCode::SUCCESS) {
            history.push_back(context);
        }
    }
    
    return history;
}

void ErrorHandler::clearErrorHistory() {
    for (auto& context : errorHistory_) {
        context = ErrorContext{};
    }
    historyIndex_.store(0);
}

void ErrorHandler::resetStatistics() {
    statistics_.reset();
}

bool ErrorHandler::isSystemHealthy() const {
    // 简化实现
    return statistics_.fatalErrors.load() == 0;
}

double ErrorHandler::getErrorRate() const {
    // 简化实现
    return 0.0;
}

// 简化的私有方法实现
void ErrorHandler::updateStatistics(const ErrorContext& context) {
    statistics_.totalErrors.fetch_add(1);
    statistics_.lastErrorTime = context.timestamp;
    
    auto severity = ErrorCodeUtils::getSeverity(context.code);
    switch (severity) {
        case ErrorSeverity::FATAL:
            statistics_.fatalErrors.fetch_add(1);
            break;
        case ErrorSeverity::CRITICAL:
            statistics_.criticalErrors.fetch_add(1);
            break;
        case ErrorSeverity::ERROR:
            statistics_.errors.fetch_add(1);
            break;
        case ErrorSeverity::WARNING:
            statistics_.warnings.fetch_add(1);
            break;
        default:
            break;
    }
}

void ErrorHandler::recordErrorHistory(const ErrorContext& context) {
    size_t index = historyIndex_.fetch_add(1) % MAX_ERROR_HISTORY;
    errorHistory_[index] = context;
}

void ErrorHandler::notifyListeners(const ErrorContext& context) {
    // 简化实现 - 暂时不通知监听器
    (void)context; // 消除未使用参数警告
}

void ErrorHandler::notifyRecoveryListeners(const ErrorContext& context, 
                                         RecoveryAction action, 
                                         bool success) {
    // 简化实现 - 暂时不通知监听器
    (void)context; // 消除未使用参数警告
    (void)action;
    (void)success;
}

bool ErrorHandler::executeRecovery(const ErrorContext& context, 
                                 const RecoveryStrategy& strategy) {
    // 简化实现 - 总是返回成功
    (void)context; // 消除未使用参数警告
    (void)strategy;
    return true;
}

void ErrorHandler::registerDefaultRecoveryStrategies() {
    // 简化实现 - 暂时不注册默认策略
}

bool ErrorHandler::executeRetryRecovery(const ErrorContext& context, 
                                      const RecoveryStrategy& strategy) {
    return true;
}

bool ErrorHandler::executeFallbackRecovery(const ErrorContext& context, 
                                         const RecoveryStrategy& strategy) {
    return true;
}

bool ErrorHandler::executeRestartRecovery(const ErrorContext& context, 
                                        const RecoveryStrategy& strategy) {
    return true;
}

bool ErrorHandler::executeShutdownRecovery(const ErrorContext& context, 
                                         const RecoveryStrategy& strategy) {
    return true;
}

bool ErrorHandler::executeEmergencyStopRecovery(const ErrorContext& context, 
                                               const RecoveryStrategy& strategy) {
    return true;
}

// DefaultErrorListener 简化实现
void DefaultErrorListener::onError(const ErrorContext& context) {
    // 简化实现 - 暂时不输出
}

void DefaultErrorListener::onRecovery(const ErrorContext& context, 
                                    RecoveryAction action, 
                                    bool success) {
    // 简化实现 - 暂时不输出
}

// FileLogErrorListener 简化实现
FileLogErrorListener::FileLogErrorListener(const std::string& logFilePath) 
    : logFilePath_(logFilePath) {
}

FileLogErrorListener::~FileLogErrorListener() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void FileLogErrorListener::onError(const ErrorContext& context) {
    // 简化实现 - 暂时不写入文件
}

void FileLogErrorListener::onRecovery(const ErrorContext& context, 
                                    RecoveryAction action, 
                                    bool success) {
    // 简化实现 - 暂时不写入文件
}

void FileLogErrorListener::writeLogEntry(const std::string& entry) {
    // 简化实现 - 暂时不写入文件
}

std::string FileLogErrorListener::formatErrorEntry(const ErrorContext& context) {
    return "";
}

std::string FileLogErrorListener::formatRecoveryEntry(const ErrorContext& context, RecoveryAction action, bool success) {
    return "";
}

// SyslogErrorListener 简化实现
#ifdef __linux__
SyslogErrorListener::SyslogErrorListener(const std::string& ident) {
    openlog(ident.c_str(), LOG_PID | LOG_CONS, LOG_USER);
}

SyslogErrorListener::~SyslogErrorListener() {
    closelog();
}

void SyslogErrorListener::onError(const ErrorContext& context) {
    // 简化实现 - 暂时不写入syslog
}

void SyslogErrorListener::onRecovery(const ErrorContext& context, 
                                   RecoveryAction action, 
                                   bool success) {
    // 简化实现 - 暂时不写入syslog
}

int SyslogErrorListener::severityToSyslogPriority(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::FATAL:
            return LOG_CRIT;
        case ErrorSeverity::CRITICAL:
            return LOG_ERR;
        case ErrorSeverity::ERROR:
            return LOG_WARNING;
        case ErrorSeverity::WARNING:
            return LOG_NOTICE;
        default:
            return LOG_INFO;
    }
}
#endif

} // namespace error
} // namespace plc_runtime