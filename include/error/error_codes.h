/**
 * @file error_codes.h
 * @brief 统一错误码定义
 * 
 * 基于ADR-006错误处理策略的统一错误码系统
 * 提供分类清晰、易于维护的错误码定义
 * 
 * 错误码分类:
 * - E000-E099: 成功和通用状态
 * - E100-E199: 调度器错误
 * - E200-E299: 内存管理错误
 * - E300-E399: 编译器错误
 * - E400-E499: 运行时错误
 * - E500-E599: 系统错误
 * - E600-E699: 通信错误
 * - E700-E799: I/O错误
 * - E800-E899: 运动控制错误
 * - E900-E999: 安全系统错误
 */

#ifndef ERROR_ERROR_CODES_H
#define ERROR_ERROR_CODES_H

#include <cstdint>
#include <string>
#include <string_view>

namespace plc_runtime {
namespace error {

/**
 * @brief 错误码类型
 */
using ErrorCodeType = uint32_t;

/**
 * @brief 错误严重程度
 */
enum class ErrorSeverity : uint8_t {
    INFO = 0,       // 信息
    WARNING = 1,    // 警告
    ERROR = 2,      // 错误
    CRITICAL = 3,   // 严重错误
    FATAL = 4       // 致命错误
};

/**
 * @brief 错误类别
 */
enum class ErrorCategory : uint8_t {
    SUCCESS = 0,        // 成功
    SCHEDULER = 1,      // 调度器
    MEMORY = 2,         // 内存管理
    COMPILER = 3,       // 编译器
    RUNTIME = 4,        // 运行时
    SYSTEM = 5,         // 系统
    COMMUNICATION = 6,  // 通信
    IO = 7,            // I/O
    MOTION = 8,        // 运动控制
    SECURITY = 9       // 安全
};

/**
 * @brief 错误码定义
 */
enum class ErrorCode : ErrorCodeType {
    // 成功和通用状态 (E000-E099)
    SUCCESS = 0,
    PENDING = 1,
    TIMEOUT = 2,
    CANCELLED = 3,
    NOT_IMPLEMENTED = 4,
    NOT_SUPPORTED = 5,
    
    // 调度器错误 (E100-E199)
    SCHEDULER_NOT_INITIALIZED = 100,
    SCHEDULER_ALREADY_RUNNING = 101,
    SCHEDULER_NOT_RUNNING = 102,
    SCHEDULER_TASK_CREATE_FAILED = 103,
    SCHEDULER_TASK_NOT_FOUND = 104,
    SCHEDULER_TASK_ALREADY_EXISTS = 105,
    SCHEDULER_DEADLINE_MISSED = 106,
    SCHEDULER_PRIORITY_INVALID = 107,
    SCHEDULER_PERIOD_INVALID = 108,
    SCHEDULER_QUEUE_FULL = 109,
    SCHEDULER_CONTEXT_SWITCH_FAILED = 110,
    SCHEDULER_TIMER_ERROR = 111,
    
    // 内存管理错误 (E200-E299)
    MEMORY_ALLOCATION_FAILED = 200,
    MEMORY_DEALLOCATION_FAILED = 201,
    MEMORY_POOL_EXHAUSTED = 202,
    MEMORY_POOL_NOT_FOUND = 203,
    MEMORY_INVALID_POINTER = 204,
    MEMORY_DOUBLE_FREE = 205,
    MEMORY_LEAK_DETECTED = 206,
    MEMORY_CORRUPTION_DETECTED = 207,
    MEMORY_BUDGET_EXCEEDED = 208,
    MEMORY_ALIGNMENT_ERROR = 209,
    MEMORY_OUT_OF_BOUNDS = 210,
    MEMORY_FRAGMENTATION_HIGH = 211,
    
    // 编译器错误 (E300-E399)
    COMPILER_LEXICAL_ERROR = 300,
    COMPILER_SYNTAX_ERROR = 301,
    COMPILER_SEMANTIC_ERROR = 302,
    COMPILER_TYPE_ERROR = 303,
    COMPILER_UNDEFINED_SYMBOL = 304,
    COMPILER_DUPLICATE_SYMBOL = 305,
    COMPILER_SCOPE_ERROR = 306,
    COMPILER_CODEGEN_ERROR = 307,
    COMPILER_OPTIMIZATION_ERROR = 308,
    COMPILER_LINK_ERROR = 309,
    COMPILER_FILE_NOT_FOUND = 310,
    COMPILER_FILE_READ_ERROR = 311,
    
    // 运行时错误 (E400-E499)
    RUNTIME_DIVISION_BY_ZERO = 400,
    RUNTIME_ARRAY_INDEX_OUT_OF_BOUNDS = 401,
    RUNTIME_NULL_POINTER_ACCESS = 402,
    RUNTIME_STACK_OVERFLOW = 403,
    RUNTIME_STACK_UNDERFLOW = 404,
    RUNTIME_INVALID_OPERATION = 405,
    RUNTIME_TYPE_MISMATCH = 406,
    RUNTIME_CONVERSION_ERROR = 407,
    RUNTIME_ASSERTION_FAILED = 408,
    RUNTIME_EXCEPTION_UNHANDLED = 409,
    RUNTIME_WATCHDOG_TIMEOUT = 410,
    RUNTIME_EMERGENCY_STOP = 411,
    
    // 系统错误 (E500-E599)
    SYSTEM_INITIALIZATION_FAILED = 500,
    SYSTEM_SHUTDOWN_FAILED = 501,
    SYSTEM_CONFIG_INVALID = 502,
    SYSTEM_CONFIG_NOT_FOUND = 503,
    SYSTEM_RESOURCE_EXHAUSTED = 504,
    SYSTEM_PERMISSION_DENIED = 505,
    SYSTEM_DEVICE_NOT_FOUND = 506,
    SYSTEM_DEVICE_BUSY = 507,
    SYSTEM_OPERATION_NOT_PERMITTED = 508,
    SYSTEM_INTERRUPT_ERROR = 509,
    SYSTEM_CLOCK_ERROR = 510,
    SYSTEM_HARDWARE_FAULT = 511,
    
    // 通信错误 (E600-E699)
    COMM_CONNECTION_FAILED = 600,
    COMM_CONNECTION_LOST = 601,
    COMM_CONNECTION_TIMEOUT = 602,
    COMM_PROTOCOL_ERROR = 603,
    COMM_INVALID_MESSAGE = 604,
    COMM_MESSAGE_TOO_LARGE = 605,
    COMM_BUFFER_OVERFLOW = 606,
    COMM_CHECKSUM_ERROR = 607,
    COMM_AUTHENTICATION_FAILED = 608,
    COMM_ENCRYPTION_ERROR = 609,
    COMM_NETWORK_UNREACHABLE = 610,
    COMM_PORT_IN_USE = 611,
    
    // I/O错误 (E700-E799)
    IO_DEVICE_NOT_FOUND = 700,
    IO_DEVICE_NOT_READY = 701,
    IO_DEVICE_FAULT = 702,
    IO_COMMUNICATION_ERROR = 703,
    IO_TIMEOUT = 704,
    IO_INVALID_ADDRESS = 705,
    IO_INVALID_DATA = 706,
    IO_CONFIGURATION_ERROR = 707,
    IO_CALIBRATION_ERROR = 708,
    IO_OVERRANGE = 709,
    IO_UNDERRANGE = 710,
    IO_WIRE_BREAK = 711,
    
    // 运动控制错误 (E800-E899)
    MOTION_AXIS_NOT_FOUND = 800,
    MOTION_AXIS_NOT_ENABLED = 801,
    MOTION_AXIS_FAULT = 802,
    MOTION_LIMIT_SWITCH_ACTIVE = 803,
    MOTION_POSITION_ERROR_TOO_LARGE = 804,
    MOTION_VELOCITY_LIMIT_EXCEEDED = 805,
    MOTION_ACCELERATION_LIMIT_EXCEEDED = 806,
    MOTION_JERK_LIMIT_EXCEEDED = 807,
    MOTION_TRAJECTORY_ERROR = 808,
    MOTION_INTERPOLATION_ERROR = 809,
    MOTION_ENCODER_ERROR = 810,
    MOTION_DRIVE_FAULT = 811,
    
    // 安全系统错误 (E900-E999)
    SECURITY_ACCESS_DENIED = 900,
    SECURITY_AUTHENTICATION_FAILED = 901,
    SECURITY_AUTHORIZATION_FAILED = 902,
    SECURITY_CERTIFICATE_INVALID = 903,
    SECURITY_CERTIFICATE_EXPIRED = 904,
    SECURITY_SIGNATURE_INVALID = 905,
    SECURITY_ENCRYPTION_FAILED = 906,
    SECURITY_DECRYPTION_FAILED = 907,
    SECURITY_KEY_NOT_FOUND = 908,
    SECURITY_INTRUSION_DETECTED = 909,
    SECURITY_SAFETY_DOOR_OPEN = 910,
    SECURITY_EMERGENCY_STOP_ACTIVE = 911
};

/**
 * @brief 错误信息结构
 */
struct ErrorInfo {
    ErrorCode code;
    ErrorSeverity severity;
    ErrorCategory category;
    std::string_view message;
    std::string_view description;
    std::string_view solution;
};

/**
 * @brief 错误码工具类
 */
class ErrorCodeUtils {
public:
    /**
     * @brief 获取错误码的类别
     * @param code 错误码
     * @return 错误类别
     */
    static ErrorCategory getCategory(ErrorCode code) noexcept;
    
    /**
     * @brief 获取错误码的严重程度
     * @param code 错误码
     * @return 错误严重程度
     */
    static ErrorSeverity getSeverity(ErrorCode code) noexcept;
    
    /**
     * @brief 获取错误码的消息
     * @param code 错误码
     * @return 错误消息
     */
    static std::string_view getMessage(ErrorCode code) noexcept;
    
    /**
     * @brief 获取错误码的详细描述
     * @param code 错误码
     * @return 错误描述
     */
    static std::string_view getDescription(ErrorCode code) noexcept;
    
    /**
     * @brief 获取错误码的解决方案
     * @param code 错误码
     * @return 解决方案
     */
    static std::string_view getSolution(ErrorCode code) noexcept;
    
    /**
     * @brief 获取完整的错误信息
     * @param code 错误码
     * @return 错误信息结构
     */
    static ErrorInfo getErrorInfo(ErrorCode code) noexcept;
    
    /**
     * @brief 检查错误码是否表示成功
     * @param code 错误码
     * @return true 如果表示成功
     */
    static bool isSuccess(ErrorCode code) noexcept {
        return code == ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 检查错误码是否表示错误
     * @param code 错误码
     * @return true 如果表示错误
     */
    static bool isError(ErrorCode code) noexcept {
        return code != ErrorCode::SUCCESS;
    }
    
    /**
     * @brief 检查错误码是否是致命错误
     * @param code 错误码
     * @return true 如果是致命错误
     */
    static bool isFatal(ErrorCode code) noexcept {
        return getSeverity(code) == ErrorSeverity::FATAL;
    }
    
    /**
     * @brief 检查错误码是否是严重错误
     * @param code 错误码
     * @return true 如果是严重错误
     */
    static bool isCritical(ErrorCode code) noexcept {
        auto severity = getSeverity(code);
        return severity == ErrorSeverity::CRITICAL || severity == ErrorSeverity::FATAL;
    }
    
    /**
     * @brief 将错误码转换为字符串
     * @param code 错误码
     * @return 错误码字符串表示
     */
    static std::string toString(ErrorCode code) noexcept;
    
    /**
     * @brief 将错误严重程度转换为字符串
     * @param severity 错误严重程度
     * @return 严重程度字符串表示
     */
    static std::string_view severityToString(ErrorSeverity severity) noexcept;
    
    /**
     * @brief 将错误类别转换为字符串
     * @param category 错误类别
     * @return 类别字符串表示
     */
    static std::string_view categoryToString(ErrorCategory category) noexcept;
    
    /**
     * @brief 从字符串解析错误码
     * @param str 错误码字符串
     * @return 错误码，如果解析失败返回SYSTEM_CONFIG_INVALID
     */
    static ErrorCode fromString(const std::string& str) noexcept;
};

/**
 * @brief 错误码宏定义
 */
#define PLC_SUCCESS(code) (plc_runtime::error::ErrorCodeUtils::isSuccess(code))
#define PLC_FAILED(code) (plc_runtime::error::ErrorCodeUtils::isError(code))
#define PLC_FATAL(code) (plc_runtime::error::ErrorCodeUtils::isFatal(code))
#define PLC_CRITICAL(code) (plc_runtime::error::ErrorCodeUtils::isCritical(code))

/**
 * @brief 错误检查宏
 */
#define PLC_RETURN_IF_FAILED(expr) \
    do { \
        auto __result = (expr); \
        if (PLC_FAILED(__result)) { \
            return __result; \
        } \
    } while(0)

#define PLC_RETURN_IF_NULL(ptr, error_code) \
    do { \
        if ((ptr) == nullptr) { \
            return (error_code); \
        } \
    } while(0)

#define PLC_ASSERT_SUCCESS(expr) \
    do { \
        auto __result = (expr); \
        assert(PLC_SUCCESS(__result)); \
    } while(0)

} // namespace error
} // namespace plc_runtime

#endif // ERROR_ERROR_CODES_H