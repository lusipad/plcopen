/**
 * @file PLCTypes.h
 * @brief PLC相关类型定义
 * 
 * 定义PLC运行时使用的基本类型和错误代码
 */

#pragma once

#include <cstdint>

namespace Uranus {

/**
 * @enum MC_ErrorCode
 * @brief Motion Control错误代码
 */
enum class MC_ErrorCode : uint32_t {
    GOOD = 0,                           // 无错误
    
    // 系统错误 (1-99)
    MEMORY_ALLOCATION_FAILED = 1,       // 内存分配失败
    TASK_SCHEDULING_FAILED = 2,         // 任务调度失败
    SYSTEM_OVERLOAD = 3,                // 系统过载
    INVALID_PARAMETER = 4,              // 无效参数
    OPERATION_TIMEOUT = 5,              // 操作超时
    
    // 轴相关错误 (100-199)
    AXIS_NO_TEXIST = 100,               // 轴不存在
    AXIS_NOT_ENABLED = 101,             // 轴未使能
    AXIS_IN_ERROR_STATE = 102,          // 轴处于错误状态
    AXIS_LIMIT_EXCEEDED = 103,          // 轴限位超出
    AXIS_FOLLOWING_ERROR = 104,         // 轴跟随误差
    
    // 运动相关错误 (200-299)
    MOTION_COMMAND_INVALID = 200,       // 运动命令无效
    MOTION_PARAMETER_INVALID = 201,     // 运动参数无效
    MOTION_BUFFER_FULL = 202,           // 运动缓冲区满
    MOTION_PROFILE_ERROR = 203,         // 运动轮廓错误
    
    // I/O相关错误 (300-399)
    IO_DEVICE_NOT_FOUND = 300,          // I/O设备未找到
    IO_COMMUNICATION_ERROR = 301,       // I/O通信错误
    IO_CONFIGURATION_ERROR = 302,       // I/O配置错误
    
    // 通信错误 (400-499)
    COMMUNICATION_TIMEOUT = 400,        // 通信超时
    COMMUNICATION_PROTOCOL_ERROR = 401, // 通信协议错误
    COMMUNICATION_CHECKSUM_ERROR = 402, // 通信校验错误
    
    // 用户定义错误 (1000+)
    USER_DEFINED_ERROR = 1000           // 用户定义错误起始值
};

/**
 * @enum DataType
 * @brief PLC数据类型
 */
enum class DataType {
    BOOL,           // 布尔型
    SINT,           // 8位有符号整数
    USINT,          // 8位无符号整数
    INT,            // 16位有符号整数
    UINT,           // 16位无符号整数
    DINT,           // 32位有符号整数
    UDINT,          // 32位无符号整数
    LINT,           // 64位有符号整数
    ULINT,          // 64位无符号整数
    REAL,           // 32位浮点数
    LREAL,          // 64位浮点数
    STRING,         // 字符串
    PLC_TIME,       // 时间类型
    PLC_DATE,       // 日期类型
    TIME_OF_DAY,    // 时刻类型
    DATE_AND_TIME   // 日期时间类型
};

/**
 * @struct PLCValue
 * @brief PLC值联合体
 */
struct PLCValue {
    DataType type;
    union {
        bool boolValue;
        int8_t sintValue;
        uint8_t usintValue;
        int16_t intValue;
        uint16_t uintValue;
        int32_t dintValue;
        uint32_t udintValue;
        int64_t lintValue;
        uint64_t ulintValue;
        float realValue;
        double lrealValue;
        char* stringValue;
        uint64_t timeValue;     // 时间值，以纳秒为单位
    };
    
    PLCValue() : type(DataType::BOOL), boolValue(false) {}
    
    explicit PLCValue(bool value) : type(DataType::BOOL), boolValue(value) {}
    explicit PLCValue(int32_t value) : type(DataType::DINT), dintValue(value) {}
    explicit PLCValue(uint32_t value) : type(DataType::UDINT), udintValue(value) {}
    explicit PLCValue(float value) : type(DataType::REAL), realValue(value) {}
    explicit PLCValue(double value) : type(DataType::LREAL), lrealValue(value) {}
};

/**
 * @enum ExecutionState
 * @brief 执行状态
 */
enum class ExecutionState {
    IDLE,           // 空闲
    RUNNING,        // 运行中
    PAUSED,         // 暂停
    STOPPED,        // 停止
    ERROR           // 错误
};

/**
 * @enum Priority
 * @brief 优先级定义
 */
enum class Priority {
    LOWEST = 0,
    LOW = 1,
    NORMAL = 2,
    HIGH = 3,
    HIGHEST = 4,
    CRITICAL = 5
};

// 常用类型别名
using TaskID = uint32_t;
using AxisID = uint32_t;
using DeviceID = uint32_t;
using ErrorID = uint32_t;

// 时间相关常量 (纳秒)
const uint64_t NS_PER_US = 1000ULL;
const uint64_t NS_PER_MS = 1000000ULL;
const uint64_t NS_PER_SEC = 1000000000ULL;

} // namespace Uranus