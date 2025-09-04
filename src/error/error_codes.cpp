/**
 * @file error_codes.cpp
 * @brief 错误码工具类实现
 */

#include "error/error_codes.h"
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace plc_runtime {
namespace error {

/**
 * @brief 错误信息映射表
 */
static const std::unordered_map<ErrorCode, ErrorInfo> ERROR_INFO_MAP = {
    // 成功和通用状态
    {ErrorCode::SUCCESS, {ErrorCode::SUCCESS, ErrorSeverity::INFO, ErrorCategory::SUCCESS, 
        "Success", "Operation completed successfully", "No action required"}},
    {ErrorCode::PENDING, {ErrorCode::PENDING, ErrorSeverity::INFO, ErrorCategory::SUCCESS,
        "Pending", "Operation is pending", "Wait for completion"}},
    {ErrorCode::TIMEOUT, {ErrorCode::TIMEOUT, ErrorSeverity::WARNING, ErrorCategory::SUCCESS,
        "Timeout", "Operation timed out", "Check system load and retry"}},
    {ErrorCode::CANCELLED, {ErrorCode::CANCELLED, ErrorSeverity::INFO, ErrorCategory::SUCCESS,
        "Cancelled", "Operation was cancelled", "No action required"}},
    {ErrorCode::NOT_IMPLEMENTED, {ErrorCode::NOT_IMPLEMENTED, ErrorSeverity::ERROR, ErrorCategory::SUCCESS,
        "Not implemented", "Feature not implemented", "Use alternative method or wait for implementation"}},
    {ErrorCode::NOT_SUPPORTED, {ErrorCode::NOT_SUPPORTED, ErrorSeverity::ERROR, ErrorCategory::SUCCESS,
        "Not supported", "Operation not supported", "Use alternative method"}},
    
    // 调度器错误
    {ErrorCode::SCHEDULER_NOT_INITIALIZED, {ErrorCode::SCHEDULER_NOT_INITIALIZED, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Scheduler not initialized", "Real-time scheduler has not been initialized", "Initialize scheduler before use"}},
    {ErrorCode::SCHEDULER_ALREADY_RUNNING, {ErrorCode::SCHEDULER_ALREADY_RUNNING, ErrorSeverity::WARNING, ErrorCategory::SCHEDULER,
        "Scheduler already running", "Scheduler is already in running state", "Stop scheduler before restarting"}},
    {ErrorCode::SCHEDULER_NOT_RUNNING, {ErrorCode::SCHEDULER_NOT_RUNNING, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Scheduler not running", "Scheduler is not in running state", "Start scheduler before task operations"}},
    {ErrorCode::SCHEDULER_TASK_CREATE_FAILED, {ErrorCode::SCHEDULER_TASK_CREATE_FAILED, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Task creation failed", "Failed to create real-time task", "Check system resources and task parameters"}},
    {ErrorCode::SCHEDULER_TASK_NOT_FOUND, {ErrorCode::SCHEDULER_TASK_NOT_FOUND, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Task not found", "Specified task does not exist", "Verify task ID and create task if needed"}},
    {ErrorCode::SCHEDULER_TASK_ALREADY_EXISTS, {ErrorCode::SCHEDULER_TASK_ALREADY_EXISTS, ErrorSeverity::WARNING, ErrorCategory::SCHEDULER,
        "Task already exists", "Task with same ID already exists", "Use different task ID or remove existing task"}},
    {ErrorCode::SCHEDULER_DEADLINE_MISSED, {ErrorCode::SCHEDULER_DEADLINE_MISSED, ErrorSeverity::CRITICAL, ErrorCategory::SCHEDULER,
        "Deadline missed", "Task failed to meet its deadline", "Reduce system load or increase task period"}},
    {ErrorCode::SCHEDULER_PRIORITY_INVALID, {ErrorCode::SCHEDULER_PRIORITY_INVALID, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Invalid priority", "Task priority is out of valid range", "Use priority between 1 and 99"}},
    {ErrorCode::SCHEDULER_PERIOD_INVALID, {ErrorCode::SCHEDULER_PERIOD_INVALID, ErrorSeverity::ERROR, ErrorCategory::SCHEDULER,
        "Invalid period", "Task period is invalid", "Use positive period value"}},
    {ErrorCode::SCHEDULER_QUEUE_FULL, {ErrorCode::SCHEDULER_QUEUE_FULL, ErrorSeverity::WARNING, ErrorCategory::SCHEDULER,
        "Queue full", "Scheduler queue is full", "Reduce task creation rate or increase queue size"}},
    
    // 内存管理错误
    {ErrorCode::MEMORY_ALLOCATION_FAILED, {ErrorCode::MEMORY_ALLOCATION_FAILED, ErrorSeverity::ERROR, ErrorCategory::MEMORY,
        "Memory allocation failed", "Failed to allocate memory", "Check available memory and reduce memory usage"}},
    {ErrorCode::MEMORY_DEALLOCATION_FAILED, {ErrorCode::MEMORY_DEALLOCATION_FAILED, ErrorSeverity::ERROR, ErrorCategory::MEMORY,
        "Memory deallocation failed", "Failed to deallocate memory", "Check pointer validity"}},
    {ErrorCode::MEMORY_POOL_EXHAUSTED, {ErrorCode::MEMORY_POOL_EXHAUSTED, ErrorSeverity::CRITICAL, ErrorCategory::MEMORY,
        "Memory pool exhausted", "Memory pool has no available blocks", "Increase pool size or reduce memory usage"}},
    {ErrorCode::MEMORY_POOL_NOT_FOUND, {ErrorCode::MEMORY_POOL_NOT_FOUND, ErrorSeverity::ERROR, ErrorCategory::MEMORY,
        "Memory pool not found", "Specified memory pool does not exist", "Create memory pool or use correct pool ID"}},
    {ErrorCode::MEMORY_INVALID_POINTER, {ErrorCode::MEMORY_INVALID_POINTER, ErrorSeverity::ERROR, ErrorCategory::MEMORY,
        "Invalid pointer", "Pointer is null or invalid", "Check pointer before use"}},
    {ErrorCode::MEMORY_DOUBLE_FREE, {ErrorCode::MEMORY_DOUBLE_FREE, ErrorSeverity::CRITICAL, ErrorCategory::MEMORY,
        "Double free detected", "Attempt to free already freed memory", "Fix memory management logic"}},
    {ErrorCode::MEMORY_LEAK_DETECTED, {ErrorCode::MEMORY_LEAK_DETECTED, ErrorSeverity::WARNING, ErrorCategory::MEMORY,
        "Memory leak detected", "Memory was allocated but not freed", "Fix memory management to prevent leaks"}},
    {ErrorCode::MEMORY_CORRUPTION_DETECTED, {ErrorCode::MEMORY_CORRUPTION_DETECTED, ErrorSeverity::FATAL, ErrorCategory::MEMORY,
        "Memory corruption detected", "Memory corruption has been detected", "Restart system and fix memory access bugs"}},
    {ErrorCode::MEMORY_BUDGET_EXCEEDED, {ErrorCode::MEMORY_BUDGET_EXCEEDED, ErrorSeverity::ERROR, ErrorCategory::MEMORY,
        "Memory budget exceeded", "Memory usage exceeded allocated budget", "Reduce memory usage or increase budget"}},
    
    // 编译器错误
    {ErrorCode::COMPILER_LEXICAL_ERROR, {ErrorCode::COMPILER_LEXICAL_ERROR, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Lexical error", "Invalid character or token in source code", "Fix syntax errors in source code"}},
    {ErrorCode::COMPILER_SYNTAX_ERROR, {ErrorCode::COMPILER_SYNTAX_ERROR, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Syntax error", "Invalid syntax in source code", "Fix syntax according to language specification"}},
    {ErrorCode::COMPILER_SEMANTIC_ERROR, {ErrorCode::COMPILER_SEMANTIC_ERROR, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Semantic error", "Semantic error in source code", "Fix semantic errors in source code"}},
    {ErrorCode::COMPILER_TYPE_ERROR, {ErrorCode::COMPILER_TYPE_ERROR, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Type error", "Type mismatch in expression", "Fix type compatibility issues"}},
    {ErrorCode::COMPILER_UNDEFINED_SYMBOL, {ErrorCode::COMPILER_UNDEFINED_SYMBOL, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Undefined symbol", "Symbol is used but not defined", "Define symbol before use"}},
    {ErrorCode::COMPILER_DUPLICATE_SYMBOL, {ErrorCode::COMPILER_DUPLICATE_SYMBOL, ErrorSeverity::ERROR, ErrorCategory::COMPILER,
        "Duplicate symbol", "Symbol is defined multiple times", "Remove duplicate definitions"}},
    
    // 运行时错误
    {ErrorCode::RUNTIME_DIVISION_BY_ZERO, {ErrorCode::RUNTIME_DIVISION_BY_ZERO, ErrorSeverity::ERROR, ErrorCategory::RUNTIME,
        "Division by zero", "Attempt to divide by zero", "Check divisor before division"}},
    {ErrorCode::RUNTIME_ARRAY_INDEX_OUT_OF_BOUNDS, {ErrorCode::RUNTIME_ARRAY_INDEX_OUT_OF_BOUNDS, ErrorSeverity::ERROR, ErrorCategory::RUNTIME,
        "Array index out of bounds", "Array index exceeds array bounds", "Check array index before access"}},
    {ErrorCode::RUNTIME_NULL_POINTER_ACCESS, {ErrorCode::RUNTIME_NULL_POINTER_ACCESS, ErrorSeverity::CRITICAL, ErrorCategory::RUNTIME,
        "Null pointer access", "Attempt to access null pointer", "Check pointer validity before access"}},
    {ErrorCode::RUNTIME_STACK_OVERFLOW, {ErrorCode::RUNTIME_STACK_OVERFLOW, ErrorSeverity::CRITICAL, ErrorCategory::RUNTIME,
        "Stack overflow", "Stack has exceeded its limit", "Reduce recursion depth or increase stack size"}},
    {ErrorCode::RUNTIME_EMERGENCY_STOP, {ErrorCode::RUNTIME_EMERGENCY_STOP, ErrorSeverity::CRITICAL, ErrorCategory::RUNTIME,
        "Emergency stop", "Emergency stop has been activated", "Clear emergency condition and reset system"}},
    
    // 系统错误
    {ErrorCode::SYSTEM_INITIALIZATION_FAILED, {ErrorCode::SYSTEM_INITIALIZATION_FAILED, ErrorSeverity::FATAL, ErrorCategory::SYSTEM,
        "System initialization failed", "System failed to initialize properly", "Check system configuration and restart"}},
    {ErrorCode::SYSTEM_CONFIG_INVALID, {ErrorCode::SYSTEM_CONFIG_INVALID, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
        "Invalid configuration", "System configuration is invalid", "Fix configuration file and restart"}},
    {ErrorCode::SYSTEM_RESOURCE_EXHAUSTED, {ErrorCode::SYSTEM_RESOURCE_EXHAUSTED, ErrorSeverity::CRITICAL, ErrorCategory::SYSTEM,
        "System resources exhausted", "System has run out of resources", "Reduce system load or add more resources"}},
    {ErrorCode::SYSTEM_PERMISSION_DENIED, {ErrorCode::SYSTEM_PERMISSION_DENIED, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
        "Permission denied", "Insufficient permissions for operation", "Run with appropriate permissions"}},
    {ErrorCode::SYSTEM_DEVICE_NOT_FOUND, {ErrorCode::SYSTEM_DEVICE_NOT_FOUND, ErrorSeverity::ERROR, ErrorCategory::SYSTEM,
        "Device not found", "Required device is not available", "Check device connection and drivers"}},
    
    // 通信错误
    {ErrorCode::COMM_CONNECTION_FAILED, {ErrorCode::COMM_CONNECTION_FAILED, ErrorSeverity::ERROR, ErrorCategory::COMMUNICATION,
        "Connection failed", "Failed to establish connection", "Check network connectivity and configuration"}},
    {ErrorCode::COMM_CONNECTION_LOST, {ErrorCode::COMM_CONNECTION_LOST, ErrorSeverity::WARNING, ErrorCategory::COMMUNICATION,
        "Connection lost", "Network connection was lost", "Check network stability and reconnect"}},
    {ErrorCode::COMM_CONNECTION_TIMEOUT, {ErrorCode::COMM_CONNECTION_TIMEOUT, ErrorSeverity::WARNING, ErrorCategory::COMMUNICATION,
        "Connection timeout", "Connection attempt timed out", "Check network latency and increase timeout"}},
    {ErrorCode::COMM_PROTOCOL_ERROR, {ErrorCode::COMM_PROTOCOL_ERROR, ErrorSeverity::ERROR, ErrorCategory::COMMUNICATION,
        "Protocol error", "Communication protocol error", "Check protocol configuration and compatibility"}},
    {ErrorCode::COMM_INVALID_MESSAGE, {ErrorCode::COMM_INVALID_MESSAGE, ErrorSeverity::WARNING, ErrorCategory::COMMUNICATION,
        "Invalid message", "Received invalid message format", "Check message format and protocol version"}},
    
    // I/O错误
    {ErrorCode::IO_DEVICE_NOT_FOUND, {ErrorCode::IO_DEVICE_NOT_FOUND, ErrorSeverity::ERROR, ErrorCategory::IO,
        "I/O device not found", "I/O device is not available", "Check device connection and configuration"}},
    {ErrorCode::IO_DEVICE_FAULT, {ErrorCode::IO_DEVICE_FAULT, ErrorSeverity::ERROR, ErrorCategory::IO,
        "I/O device fault", "I/O device has reported a fault", "Check device status and wiring"}},
    {ErrorCode::IO_COMMUNICATION_ERROR, {ErrorCode::IO_COMMUNICATION_ERROR, ErrorSeverity::WARNING, ErrorCategory::IO,
        "I/O communication error", "Communication with I/O device failed", "Check wiring and device power"}},
    {ErrorCode::IO_TIMEOUT, {ErrorCode::IO_TIMEOUT, ErrorSeverity::WARNING, ErrorCategory::IO,
        "I/O timeout", "I/O operation timed out", "Check device response time and increase timeout"}},
    {ErrorCode::IO_INVALID_ADDRESS, {ErrorCode::IO_INVALID_ADDRESS, ErrorSeverity::ERROR, ErrorCategory::IO,
        "Invalid I/O address", "I/O address is out of range", "Use valid I/O address range"}},
    
    // 运动控制错误
    {ErrorCode::MOTION_AXIS_NOT_FOUND, {ErrorCode::MOTION_AXIS_NOT_FOUND, ErrorSeverity::ERROR, ErrorCategory::MOTION,
        "Axis not found", "Motion axis does not exist", "Check axis configuration and ID"}},
    {ErrorCode::MOTION_AXIS_FAULT, {ErrorCode::MOTION_AXIS_FAULT, ErrorSeverity::ERROR, ErrorCategory::MOTION,
        "Axis fault", "Motion axis has reported a fault", "Check axis status and clear fault"}},
    {ErrorCode::MOTION_LIMIT_SWITCH_ACTIVE, {ErrorCode::MOTION_LIMIT_SWITCH_ACTIVE, ErrorSeverity::CRITICAL, ErrorCategory::MOTION,
        "Limit switch active", "Motion limit switch is active", "Move axis away from limit and reset"}},
    {ErrorCode::MOTION_POSITION_ERROR_TOO_LARGE, {ErrorCode::MOTION_POSITION_ERROR_TOO_LARGE, ErrorSeverity::ERROR, ErrorCategory::MOTION,
        "Position error too large", "Position following error exceeds limit", "Check tuning parameters and load"}},
    {ErrorCode::MOTION_VELOCITY_LIMIT_EXCEEDED, {ErrorCode::MOTION_VELOCITY_LIMIT_EXCEEDED, ErrorSeverity::WARNING, ErrorCategory::MOTION,
        "Velocity limit exceeded", "Commanded velocity exceeds axis limit", "Reduce velocity command"}},
    
    // 安全系统错误
    {ErrorCode::SECURITY_ACCESS_DENIED, {ErrorCode::SECURITY_ACCESS_DENIED, ErrorSeverity::ERROR, ErrorCategory::SECURITY,
        "Access denied", "Access to resource denied", "Check user permissions and authentication"}},
    {ErrorCode::SECURITY_AUTHENTICATION_FAILED, {ErrorCode::SECURITY_AUTHENTICATION_FAILED, ErrorSeverity::ERROR, ErrorCategory::SECURITY,
        "Authentication failed", "User authentication failed", "Check credentials and try again"}},
    {ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE, {ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE, ErrorSeverity::CRITICAL, ErrorCategory::SECURITY,
        "Emergency stop active", "Emergency stop button is active", "Release emergency stop and reset system"}}
};

ErrorCategory ErrorCodeUtils::getCategory(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second.category;
    }
    
    // 根据错误码范围推断类别
    uint32_t codeValue = static_cast<uint32_t>(code);
    if (codeValue < 100) return ErrorCategory::SUCCESS;
    else if (codeValue < 200) return ErrorCategory::SCHEDULER;
    else if (codeValue < 300) return ErrorCategory::MEMORY;
    else if (codeValue < 400) return ErrorCategory::COMPILER;
    else if (codeValue < 500) return ErrorCategory::RUNTIME;
    else if (codeValue < 600) return ErrorCategory::SYSTEM;
    else if (codeValue < 700) return ErrorCategory::COMMUNICATION;
    else if (codeValue < 800) return ErrorCategory::IO;
    else if (codeValue < 900) return ErrorCategory::MOTION;
    else return ErrorCategory::SECURITY;
}

ErrorSeverity ErrorCodeUtils::getSeverity(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second.severity;
    }
    
    // 默认严重程度
    if (code == ErrorCode::SUCCESS) {
        return ErrorSeverity::INFO;
    }
    
    // 根据错误类型推断严重程度
    auto category = getCategory(code);
    switch (category) {
        case ErrorCategory::MEMORY:
        case ErrorCategory::SECURITY:
            return ErrorSeverity::CRITICAL;
        case ErrorCategory::SYSTEM:
        case ErrorCategory::RUNTIME:
            return ErrorSeverity::ERROR;
        default:
            return ErrorSeverity::WARNING;
    }
}

std::string_view ErrorCodeUtils::getMessage(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second.message;
    }
    return "Unknown error";
}

std::string_view ErrorCodeUtils::getDescription(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second.description;
    }
    return "No description available";
}

std::string_view ErrorCodeUtils::getSolution(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second.solution;
    }
    return "No solution available";
}

ErrorInfo ErrorCodeUtils::getErrorInfo(ErrorCode code) noexcept {
    auto it = ERROR_INFO_MAP.find(code);
    if (it != ERROR_INFO_MAP.end()) {
        return it->second;
    }
    
    // 返回默认错误信息
    return {code, getSeverity(code), getCategory(code), 
            "Unknown error", "No description available", "No solution available"};
}

std::string ErrorCodeUtils::toString(ErrorCode code) noexcept {
    std::ostringstream oss;
    oss << "E" << std::setfill('0') << std::setw(3) << static_cast<uint32_t>(code);
    return oss.str();
}

std::string_view ErrorCodeUtils::severityToString(ErrorSeverity severity) noexcept {
    switch (severity) {
        case ErrorSeverity::INFO: return "INFO";
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR: return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        case ErrorSeverity::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string_view ErrorCodeUtils::categoryToString(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::SUCCESS: return "SUCCESS";
        case ErrorCategory::SCHEDULER: return "SCHEDULER";
        case ErrorCategory::MEMORY: return "MEMORY";
        case ErrorCategory::COMPILER: return "COMPILER";
        case ErrorCategory::RUNTIME: return "RUNTIME";
        case ErrorCategory::SYSTEM: return "SYSTEM";
        case ErrorCategory::COMMUNICATION: return "COMMUNICATION";
        case ErrorCategory::IO: return "IO";
        case ErrorCategory::MOTION: return "MOTION";
        case ErrorCategory::SECURITY: return "SECURITY";
        default: return "UNKNOWN";
    }
}

ErrorCode ErrorCodeUtils::fromString(const std::string& str) noexcept {
    // 尝试解析 "E123" 格式
    if (str.length() >= 4 && str[0] == 'E') {
        try {
            uint32_t code = std::stoul(str.substr(1));
            return static_cast<ErrorCode>(code);
        } catch (...) {
            // 解析失败
        }
    }
    
    // 尝试直接解析数字
    try {
        uint32_t code = std::stoul(str);
        return static_cast<ErrorCode>(code);
    } catch (...) {
        return ErrorCode::SYSTEM_CONFIG_INVALID;
    }
}

} // namespace error
} // namespace plc_runtime