/**
 * @file standard_error_category.cpp
 * @brief 基于std::error_category的统一错误码体系实现
 * @version 1.0
 * @date 2025-09-09
 */

#include "error/standard_error_category.h"
#include <unordered_map>

namespace plc_runtime {
namespace error {

// =============================================================================
// PlcErrorCategory 基类实现
// =============================================================================

std::string PlcErrorCategory::message(int ev) const {
    ErrorCode code = to_plc_error_code(ev);
    auto info = ErrorCodeUtils::getErrorInfo(code);
    
    // 组合详细的错误消息
    std::string msg;
    msg.reserve(256);
    msg += info.message;
    msg += " (";
    msg += info.description;
    msg += ")";
    
    return msg;
}

bool PlcErrorCategory::equivalent(int code, const std::error_condition& condition) const noexcept {
    return default_error_condition(code) == condition;
}

bool PlcErrorCategory::equivalent(const std::error_code& code, int condition) const noexcept {
    return *this == code.category() && code.value() == condition;
}

std::error_condition PlcErrorCategory::default_error_condition(int ev) const noexcept {
    ErrorCode code = to_plc_error_code(ev);
    
    // 映射到通用错误条件
    switch (code) {
        case ErrorCode::SUCCESS:
            return make_error_condition(PlcErrorCondition::success);
            
        case ErrorCode::TIMEOUT:
        case ErrorCode::COMM_CONNECTION_TIMEOUT:
        case ErrorCode::IO_TIMEOUT:
        case ErrorCode::RUNTIME_WATCHDOG_TIMEOUT:
            return make_error_condition(PlcErrorCondition::timeout);
            
        case ErrorCode::MEMORY_POOL_EXHAUSTED:
        case ErrorCode::MEMORY_BUDGET_EXCEEDED:
        case ErrorCode::SYSTEM_RESOURCE_EXHAUSTED:
        case ErrorCode::SCHEDULER_QUEUE_FULL:
            return make_error_condition(PlcErrorCondition::resource_exhausted);
            
        case ErrorCode::SCHEDULER_PRIORITY_INVALID:
        case ErrorCode::SCHEDULER_PERIOD_INVALID:
        case ErrorCode::MEMORY_INVALID_POINTER:
        case ErrorCode::IO_INVALID_ADDRESS:
        case ErrorCode::IO_INVALID_DATA:
        case ErrorCode::RUNTIME_ARRAY_INDEX_OUT_OF_BOUNDS:
        case ErrorCode::RUNTIME_DIVISION_BY_ZERO:
            return make_error_condition(PlcErrorCondition::invalid_argument);
            
        case ErrorCode::SYSTEM_PERMISSION_DENIED:
        case ErrorCode::SECURITY_ACCESS_DENIED:
        case ErrorCode::SECURITY_AUTHENTICATION_FAILED:
        case ErrorCode::SECURITY_AUTHORIZATION_FAILED:
            return make_error_condition(PlcErrorCondition::permission_denied);
            
        case ErrorCode::SCHEDULER_TASK_NOT_FOUND:
        case ErrorCode::MEMORY_POOL_NOT_FOUND:
        case ErrorCode::SYSTEM_DEVICE_NOT_FOUND:
        case ErrorCode::IO_DEVICE_NOT_FOUND:
        case ErrorCode::MOTION_AXIS_NOT_FOUND:
        case ErrorCode::COMPILER_UNDEFINED_SYMBOL:
        case ErrorCode::COMPILER_FILE_NOT_FOUND:
        case ErrorCode::SYSTEM_CONFIG_NOT_FOUND:
        case ErrorCode::SECURITY_KEY_NOT_FOUND:
            return make_error_condition(PlcErrorCondition::not_found);
            
        case ErrorCode::SCHEDULER_TASK_ALREADY_EXISTS:
        case ErrorCode::COMPILER_DUPLICATE_SYMBOL:
        case ErrorCode::SYSTEM_DEVICE_BUSY:
        case ErrorCode::COMM_PORT_IN_USE:
            return make_error_condition(PlcErrorCondition::already_exists);
            
        case ErrorCode::COMM_PROTOCOL_ERROR:
        case ErrorCode::COMM_INVALID_MESSAGE:
        case ErrorCode::COMM_CHECKSUM_ERROR:
        case ErrorCode::COMM_MESSAGE_TOO_LARGE:
        case ErrorCode::COMM_BUFFER_OVERFLOW:
            return make_error_condition(PlcErrorCondition::protocol_error);
            
        case ErrorCode::SYSTEM_HARDWARE_FAULT:
        case ErrorCode::IO_DEVICE_FAULT:
        case ErrorCode::IO_WIRE_BREAK:
        case ErrorCode::MOTION_AXIS_FAULT:
        case ErrorCode::MOTION_DRIVE_FAULT:
        case ErrorCode::MOTION_ENCODER_ERROR:
            return make_error_condition(PlcErrorCondition::hardware_fault);
            
        case ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE:
        case ErrorCode::SECURITY_SAFETY_DOOR_OPEN:
        case ErrorCode::RUNTIME_EMERGENCY_STOP:
        case ErrorCode::MOTION_LIMIT_SWITCH_ACTIVE:
        case ErrorCode::SECURITY_INTRUSION_DETECTED:
            return make_error_condition(PlcErrorCondition::safety_violation);
            
        default:
            // 对于未明确映射的错误，根据类别返回通用条件
            auto category = ErrorCodeUtils::getCategory(code);
            auto severity = ErrorCodeUtils::getSeverity(code);
            
            if (severity >= ErrorSeverity::CRITICAL) {
                if (category == ErrorCategory::SECURITY || category == ErrorCategory::MOTION) {
                    return make_error_condition(PlcErrorCondition::safety_violation);
                } else {
                    return make_error_condition(PlcErrorCondition::hardware_fault);
                }
            } else if (category == ErrorCategory::COMMUNICATION) {
                return make_error_condition(PlcErrorCondition::protocol_error);
            } else {
                return make_error_condition(PlcErrorCondition::invalid_argument);
            }
    }
}

// =============================================================================
// PlcGenericErrorCategory 实现
// =============================================================================

std::string PlcGenericErrorCategory::message(int ev) const {
    ErrorCode code = to_plc_error_code(ev);
    auto info = ErrorCodeUtils::getErrorInfo(code);
    
    // 为通用类别提供更详细的格式化
    std::string msg;
    msg.reserve(512);
    
    // 添加错误码标识
    msg += "[";
    msg += ErrorCodeUtils::toString(code);
    msg += "] ";
    
    // 添加类别和严重程度
    msg += "[";
    msg += ErrorCodeUtils::categoryToString(info.category);
    msg += "/";
    msg += ErrorCodeUtils::severityToString(info.severity);
    msg += "] ";
    
    // 添加消息和描述
    msg += info.message;
    if (!info.description.empty()) {
        msg += " - ";
        msg += info.description;
    }
    
    // 添加解决方案
    if (!info.solution.empty()) {
        msg += " (Solution: ";
        msg += info.solution;
        msg += ")";
    }
    
    return msg;
}

// =============================================================================
// PlcErrorConditionCategory 实现
// =============================================================================

std::string PlcErrorConditionCategory::message(int ev) const {
    switch (static_cast<PlcErrorCondition>(ev)) {
        case PlcErrorCondition::success:
            return "Operation completed successfully";
        case PlcErrorCondition::timeout:
            return "Operation timed out";
        case PlcErrorCondition::resource_exhausted:
            return "System resources are exhausted";
        case PlcErrorCondition::invalid_argument:
            return "Invalid argument provided";
        case PlcErrorCondition::permission_denied:
            return "Permission denied";
        case PlcErrorCondition::not_found:
            return "Resource not found";
        case PlcErrorCondition::already_exists:
            return "Resource already exists";
        case PlcErrorCondition::protocol_error:
            return "Communication protocol error";
        case PlcErrorCondition::hardware_fault:
            return "Hardware fault detected";
        case PlcErrorCondition::safety_violation:
            return "Safety system violation";
        default:
            return "Unknown error condition";
    }
}

} // namespace error
} // namespace plc_runtime