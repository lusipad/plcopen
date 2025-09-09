/**
 * @file standard_error_category.h
 * @brief 基于std::error_category的统一错误码体系
 * @version 1.0
 * @date 2025-09-09
 * 
 * 将PLCOpen错误码系统与C++标准错误处理框架集成
 * 提供与std::error_code和std::system_error的完全兼容性
 */

#ifndef ERROR_STANDARD_ERROR_CATEGORY_H
#define ERROR_STANDARD_ERROR_CATEGORY_H

#include "error/error_codes.h"
#include <system_error>
#include <string>
#include <memory>

namespace plc_runtime {
namespace error {

/**
 * @brief PLCOpen错误类别基类
 * 
 * 扩展std::error_category为PLCOpen错误码系统提供标准化支持
 */
class PlcErrorCategory : public std::error_category {
public:
    /**
     * @brief 获取错误类别名称
     */
    const char* name() const noexcept override = 0;
    
    /**
     * @brief 获取错误消息
     */
    std::string message(int ev) const override;
    
    /**
     * @brief 检查错误代码是否等效于错误条件
     */
    bool equivalent(int code, const std::error_condition& condition) const noexcept override;
    
    /**
     * @brief 检查错误代码是否等效于另一个错误代码
     */
    bool equivalent(const std::error_code& code, int condition) const noexcept override;
    
    /**
     * @brief 获取默认错误条件
     */
    std::error_condition default_error_condition(int ev) const noexcept override;

protected:
    /**
     * @brief 获取PLCOpen错误码
     */
    ErrorCode to_plc_error_code(int ev) const noexcept {
        return static_cast<ErrorCode>(ev);
    }
};

/**
 * @brief 调度器错误类别
 */
class SchedulerErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_scheduler";
    }
    
    static const SchedulerErrorCategory& instance() {
        static SchedulerErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 内存管理错误类别
 */
class MemoryErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_memory";
    }
    
    static const MemoryErrorCategory& instance() {
        static MemoryErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 编译器错误类别
 */
class CompilerErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_compiler";
    }
    
    static const CompilerErrorCategory& instance() {
        static CompilerErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 运行时错误类别
 */
class RuntimeErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_runtime";
    }
    
    static const RuntimeErrorCategory& instance() {
        static RuntimeErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 系统错误类别
 */
class SystemErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_system";
    }
    
    static const SystemErrorCategory& instance() {
        static SystemErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 通信错误类别
 */
class CommunicationErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_communication";
    }
    
    static const CommunicationErrorCategory& instance() {
        static CommunicationErrorCategory instance;
        return instance;
    }
};

/**
 * @brief I/O错误类别
 */
class IoErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_io";
    }
    
    static const IoErrorCategory& instance() {
        static IoErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 运动控制错误类别
 */
class MotionErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_motion";
    }
    
    static const MotionErrorCategory& instance() {
        static MotionErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 安全系统错误类别
 */
class SecurityErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_security";
    }
    
    static const SecurityErrorCategory& instance() {
        static SecurityErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 通用PLCOpen错误类别
 * 
 * 处理所有PLCOpen错误码的统一类别
 */
class PlcGenericErrorCategory : public PlcErrorCategory {
public:
    const char* name() const noexcept override {
        return "plc_generic";
    }
    
    std::string message(int ev) const override;
    
    static const PlcGenericErrorCategory& instance() {
        static PlcGenericErrorCategory instance;
        return instance;
    }
};

/**
 * @brief 错误条件枚举
 * 
 * 定义通用错误条件，用于跨类别的错误比较
 */
enum class PlcErrorCondition {
    success = 0,
    timeout = 1,
    resource_exhausted = 2,
    invalid_argument = 3,
    permission_denied = 4,
    not_found = 5,
    already_exists = 6,
    protocol_error = 7,
    hardware_fault = 8,
    safety_violation = 9
};

/**
 * @brief PLCOpen错误条件类别
 */
class PlcErrorConditionCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "plc_condition";
    }
    
    std::string message(int ev) const override;
    
    static const PlcErrorConditionCategory& instance() {
        static PlcErrorConditionCategory instance;
        return instance;
    }
};

/**
 * @brief 便利函数：创建std::error_code
 */
inline std::error_code make_error_code(ErrorCode ec) noexcept {
    const std::error_category* category = &PlcGenericErrorCategory::instance();
    
    // 根据错误码类别选择特定的错误类别
    auto error_category = ErrorCodeUtils::getCategory(ec);
    switch (error_category) {
        case ErrorCategory::SCHEDULER:
            category = &SchedulerErrorCategory::instance();
            break;
        case ErrorCategory::MEMORY:
            category = &MemoryErrorCategory::instance();
            break;
        case ErrorCategory::COMPILER:
            category = &CompilerErrorCategory::instance();
            break;
        case ErrorCategory::RUNTIME:
            category = &RuntimeErrorCategory::instance();
            break;
        case ErrorCategory::SYSTEM:
            category = &SystemErrorCategory::instance();
            break;
        case ErrorCategory::COMMUNICATION:
            category = &CommunicationErrorCategory::instance();
            break;
        case ErrorCategory::IO:
            category = &IoErrorCategory::instance();
            break;
        case ErrorCategory::MOTION:
            category = &MotionErrorCategory::instance();
            break;
        case ErrorCategory::SECURITY:
            category = &SecurityErrorCategory::instance();
            break;
        default:
            category = &PlcGenericErrorCategory::instance();
            break;
    }
    
    return std::error_code(static_cast<int>(ec), *category);
}

/**
 * @brief 便利函数：创建std::error_condition
 */
inline std::error_condition make_error_condition(PlcErrorCondition ec) noexcept {
    return std::error_condition(static_cast<int>(ec), PlcErrorConditionCategory::instance());
}

/**
 * @brief PLCOpen异常类
 * 
 * 基于std::system_error的PLCOpen特定异常类
 */
class PlcException : public std::system_error {
public:
    /**
     * @brief 构造函数
     * @param ec PLCOpen错误码
     * @param what_arg 附加错误信息
     */
    explicit PlcException(ErrorCode ec, const std::string& what_arg = "")
        : std::system_error(make_error_code(ec), what_arg)
        , plc_error_code_(ec) {}
    
    /**
     * @brief 构造函数
     * @param ec PLCOpen错误码
     * @param what_arg 附加错误信息
     */
    explicit PlcException(ErrorCode ec, const char* what_arg)
        : std::system_error(make_error_code(ec), what_arg)
        , plc_error_code_(ec) {}
    
    /**
     * @brief 获取PLCOpen错误码
     */
    ErrorCode plc_error_code() const noexcept {
        return plc_error_code_;
    }
    
    /**
     * @brief 获取错误信息
     */
    ErrorInfo error_info() const noexcept {
        return ErrorCodeUtils::getErrorInfo(plc_error_code_);
    }
    
    /**
     * @brief 获取错误严重程度
     */
    ErrorSeverity severity() const noexcept {
        return ErrorCodeUtils::getSeverity(plc_error_code_);
    }
    
    /**
     * @brief 获取错误类别
     */
    ErrorCategory plc_category() const noexcept {
        return ErrorCodeUtils::getCategory(plc_error_code_);
    }

private:
    ErrorCode plc_error_code_;
};

/**
 * @brief 结果类型包装器
 * 
 * 提供类似Rust的Result类型，支持错误处理而无需异常
 */
template<typename T>
class Result {
public:
    /**
     * @brief 成功构造函数
     */
    Result(T&& value) : value_(std::move(value)), error_code_(ErrorCode::SUCCESS) {}
    Result(const T& value) : value_(value), error_code_(ErrorCode::SUCCESS) {}
    
    /**
     * @brief 错误构造函数
     */
    explicit Result(ErrorCode error) : error_code_(error) {
        if (error == ErrorCode::SUCCESS) {
            throw std::invalid_argument("Cannot create error Result with SUCCESS");
        }
    }
    
    /**
     * @brief 从std::error_code构造
     */
    explicit Result(const std::error_code& ec) {
        if (ec.category() == PlcGenericErrorCategory::instance() ||
            ec.category() == SchedulerErrorCategory::instance() ||
            ec.category() == MemoryErrorCategory::instance() ||
            ec.category() == CompilerErrorCategory::instance() ||
            ec.category() == RuntimeErrorCategory::instance() ||
            ec.category() == SystemErrorCategory::instance() ||
            ec.category() == CommunicationErrorCategory::instance() ||
            ec.category() == IoErrorCategory::instance() ||
            ec.category() == MotionErrorCategory::instance() ||
            ec.category() == SecurityErrorCategory::instance()) {
            error_code_ = static_cast<ErrorCode>(ec.value());
        } else {
            error_code_ = ErrorCode::SYSTEM_CONFIG_INVALID; // 默认错误
        }
    }
    
    /**
     * @brief 检查是否成功
     */
    bool is_success() const noexcept {
        return error_code_ == ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 检查是否失败
     */
    bool is_error() const noexcept {
        return error_code_ != ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 获取值（必须检查is_success()）
     */
    const T& value() const {
        if (is_error()) {
            throw PlcException(error_code_, "Attempting to access value of failed Result");
        }
        return value_;
    }
    
    /**
     * @brief 获取值（移动语义）
     */
    T&& move_value() {
        if (is_error()) {
            throw PlcException(error_code_, "Attempting to access value of failed Result");
        }
        return std::move(value_);
    }
    
    /**
     * @brief 获取错误码
     */
    ErrorCode error() const noexcept {
        return error_code_;
    }
    
    /**
     * @brief 获取std::error_code
     */
    std::error_code error_code() const noexcept {
        return make_error_code(error_code_);
    }
    
    /**
     * @brief 获取错误信息
     */
    ErrorInfo error_info() const noexcept {
        return ErrorCodeUtils::getErrorInfo(error_code_);
    }
    
    /**
     * @brief 如果失败则返回默认值
     */
    T value_or(T&& default_value) const {
        return is_success() ? value_ : std::forward<T>(default_value);
    }
    
    /**
     * @brief 如果失败则抛出异常
     */
    const T& value_or_throw() const {
        if (is_error()) {
            throw PlcException(error_code_);
        }
        return value_;
    }

private:
    T value_{};
    ErrorCode error_code_;
};

/**
 * @brief void特化的结果类型
 */
template<>
class Result<void> {
public:
    /**
     * @brief 成功构造函数
     */
    Result() : error_code_(ErrorCode::SUCCESS) {}
    
    /**
     * @brief 错误构造函数
     */
    explicit Result(ErrorCode error) : error_code_(error) {
        if (error == ErrorCode::SUCCESS) {
            throw std::invalid_argument("Cannot create error Result with SUCCESS");
        }
    }
    
    /**
     * @brief 从std::error_code构造
     */
    explicit Result(const std::error_code& ec) {
        if (ec.category() == PlcGenericErrorCategory::instance()) {
            error_code_ = static_cast<ErrorCode>(ec.value());
        } else {
            error_code_ = ErrorCode::SYSTEM_CONFIG_INVALID;
        }
    }
    
    /**
     * @brief 检查是否成功
     */
    bool is_success() const noexcept {
        return error_code_ == ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 检查是否失败
     */
    bool is_error() const noexcept {
        return error_code_ != ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 获取错误码
     */
    ErrorCode error() const noexcept {
        return error_code_;
    }
    
    /**
     * @brief 获取std::error_code
     */
    std::error_code error_code() const noexcept {
        return make_error_code(error_code_);
    }
    
    /**
     * @brief 获取错误信息
     */
    ErrorInfo error_info() const noexcept {
        return ErrorCodeUtils::getErrorInfo(error_code_);
    }
    
    /**
     * @brief 如果失败则抛出异常
     */
    void value_or_throw() const {
        if (is_error()) {
            throw PlcException(error_code_);
        }
    }

private:
    ErrorCode error_code_;
};

/**
 * @brief 便利类型别名
 */
using VoidResult = Result<void>;

/**
 * @brief 便利宏定义
 */
#define PLC_TRY(expr) \
    do { \
        auto __result = (expr); \
        if (__result.is_error()) { \
            return Result<void>(__result.error()); \
        } \
    } while(0)

#define PLC_TRY_ASSIGN(var, expr) \
    do { \
        auto __result = (expr); \
        if (__result.is_error()) { \
            return decltype(__result)(__result.error()); \
        } \
        (var) = __result.move_value(); \
    } while(0)

} // namespace error
} // namespace plc_runtime

// 启用std::error_code的ADL查找
namespace std {
    template<>
    struct is_error_code_enum<plc_runtime::error::ErrorCode> : true_type {};
    
    template<>
    struct is_error_condition_enum<plc_runtime::error::PlcErrorCondition> : true_type {};
}

#endif // ERROR_STANDARD_ERROR_CATEGORY_H