/**
 * @file structured_logger.h
 * @brief 结构化日志系统与限速采样机制
 * @version 1.0
 * @date 2025-09-09
 * 
 * 特性：
 * - 结构化日志格式（JSON/键值对）
 * - 模块化日志分类（Module/TxnID/FC/耗时）
 * - 限速和采样机制防止日志洪水
 * - 多线程安全的日志缓冲
 * - 可配置的日志级别和输出目标
 * - 与统一错误码体系集成
 */

#ifndef LOGGING_STRUCTURED_LOGGER_H
#define LOGGING_STRUCTURED_LOGGER_H

#include "error/standard_error_category.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <functional>

namespace plc_runtime {
namespace logging {

/**
 * @brief 日志级别
 */
enum class LogLevel : uint8_t {
    TRACE = 0,    // 详细跟踪信息
    DEBUG = 1,    // 调试信息
    INFO = 2,     // 一般信息
    WARN = 3,     // 警告
    ERROR = 4,    // 错误
    FATAL = 5     // 致命错误
};

/**
 * @brief 日志模块
 */
enum class LogModule : uint16_t {
    CORE = 0,           // 核心系统
    SCHEDULER = 1,      // 调度器
    MEMORY = 2,         // 内存管理
    COMPILER = 3,       // 编译器
    RUNTIME = 4,        // 运行时
    COMMUNICATION = 5,  // 通信系统
    IO = 6,            // I/O系统
    MOTION = 7,        // 运动控制
    SECURITY = 8,      // 安全系统
    NETWORK = 9,       // 网络管理
    USER = 1000        // 用户模块起始
};

/**
 * @brief 日志条目结构
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    LogModule module;
    std::string transaction_id;
    std::string function_code;
    std::chrono::microseconds duration;
    std::string message;
    std::unordered_map<std::string, std::string> fields;
    std::thread::id thread_id;
    
    LogEntry() = default;
    LogEntry(LogLevel lvl, LogModule mod, const std::string& msg)
        : timestamp(std::chrono::system_clock::now())
        , level(lvl)
        , module(mod)
        , message(msg)
        , thread_id(std::this_thread::get_id()) {}
};

/**
 * @brief 限速器配置
 */
struct RateLimiterConfig {
    std::chrono::milliseconds window_duration{1000};  // 时间窗口
    size_t max_events_per_window{100};                // 窗口内最大事件数
    std::chrono::milliseconds burst_duration{100};    // 突发允许时长
    size_t max_burst_events{10};                      // 突发最大事件数
    
    RateLimiterConfig() = default;
    RateLimiterConfig(std::chrono::milliseconds window, size_t max_events)
        : window_duration(window), max_events_per_window(max_events) {}
};

/**
 * @brief 采样器配置
 */
struct SamplerConfig {
    double sample_rate{1.0};                          // 采样率 (0.0-1.0)
    size_t max_samples_per_minute{1000};              // 每分钟最大采样数
    bool adaptive_sampling{true};                     // 自适应采样
    std::chrono::milliseconds adaptive_window{60000}; // 自适应窗口
    
    SamplerConfig() = default;
    SamplerConfig(double rate) : sample_rate(rate) {}
};

/**
 * @brief 日志输出目标
 */
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

/**
 * @brief 控制台输出
 */
class ConsoleSink : public LogSink {
public:
    explicit ConsoleSink(bool use_colors = true) : use_colors_(use_colors) {}
    void write(const LogEntry& entry) override;
    void flush() override;

private:
    bool use_colors_;
    std::mutex console_mutex_;
};

/**
 * @brief 文件输出
 */
class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& filename, bool rotate = true, size_t max_size = 100*1024*1024);
    ~FileSink() override;
    
    void write(const LogEntry& entry) override;
    void flush() override;

private:
    std::string base_filename_;
    std::ofstream file_;
    bool auto_rotate_;
    size_t max_file_size_;
    size_t current_size_;
    std::mutex file_mutex_;
    
    void rotate_if_needed();
    std::string generate_filename();
};

/**
 * @brief JSON格式输出
 */
class JsonSink : public LogSink {
public:
    explicit JsonSink(const std::string& filename) : file_sink_(filename) {}
    
    void write(const LogEntry& entry) override;
    void flush() override { file_sink_.flush(); }

private:
    FileSink file_sink_;
    std::string format_as_json(const LogEntry& entry);
};

/**
 * @brief 限速器实现
 */
class RateLimiter {
public:
    explicit RateLimiter(const RateLimiterConfig& config) : config_(config) {}
    
    bool should_allow(const std::string& key = "");
    void reset();
    size_t get_dropped_count(const std::string& key = "") const;

private:
    struct WindowState {
        std::chrono::system_clock::time_point window_start;
        size_t event_count{0};
        size_t dropped_count{0};
        std::chrono::system_clock::time_point last_burst_start;
        size_t burst_count{0};
    };
    
    RateLimiterConfig config_;
    std::unordered_map<std::string, WindowState> windows_;
    mutable std::mutex windows_mutex_;
    
    void cleanup_old_windows();
};

/**
 * @brief 采样器实现
 */
class Sampler {
public:
    explicit Sampler(const SamplerConfig& config) : config_(config), rng_seed_(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    
    bool should_sample(LogLevel level, LogModule module);
    void update_load(size_t current_log_rate);
    double get_current_rate() const { return current_sample_rate_; }

private:
    SamplerConfig config_;
    std::atomic<double> current_sample_rate_{1.0};
    std::atomic<size_t> samples_this_minute_{0};
    std::chrono::system_clock::time_point minute_start_{std::chrono::system_clock::now()};
    mutable std::mutex sampler_mutex_;
    uint64_t rng_seed_;
    
    double generate_random() const;
    void reset_minute_counter_if_needed();
    double calculate_adaptive_rate(size_t current_load) const;
};

/**
 * @brief 结构化日志记录器
 */
class StructuredLogger {
public:
    /**
     * @brief 配置结构
     */
    struct Config {
        LogLevel min_level{LogLevel::INFO};
        bool async_logging{true};
        size_t buffer_size{10000};
        std::chrono::milliseconds flush_interval{1000};
        RateLimiterConfig rate_limiter;
        SamplerConfig sampler;
        bool enable_rate_limiting{true};
        bool enable_sampling{false};
        
        Config() = default;
    };
    
    /**
     * @brief 构造函数
     */
    explicit StructuredLogger(const Config& config = Config{});
    
    /**
     * @brief 析构函数
     */
    ~StructuredLogger();
    
    /**
     * @brief 添加日志输出目标
     */
    void add_sink(std::unique_ptr<LogSink> sink);
    
    /**
     * @brief 移除所有输出目标
     */
    void clear_sinks();
    
    /**
     * @brief 设置最小日志级别
     */
    void set_level(LogLevel level);
    
    /**
     * @brief 获取当前日志级别
     */
    LogLevel get_level() const { return config_.min_level; }
    
    /**
     * @brief 记录日志
     */
    void log(LogLevel level, LogModule module, const std::string& message);
    
    /**
     * @brief 带事务ID的日志记录
     */
    void log(LogLevel level, LogModule module, const std::string& txn_id, 
             const std::string& message);
    
    /**
     * @brief 带完整上下文的日志记录
     */
    void log(LogLevel level, LogModule module, const std::string& txn_id, 
             const std::string& function_code, std::chrono::microseconds duration,
             const std::string& message);
    
    /**
     * @brief 带自定义字段的日志记录
     */
    void log(LogLevel level, LogModule module, const std::string& message,
             const std::unordered_map<std::string, std::string>& fields);
    
    /**
     * @brief 记录错误码
     */
    void log_error(error::ErrorCode error_code, LogModule module = LogModule::CORE,
                   const std::string& context = "");
    
    /**
     * @brief 记录结果类型错误
     */
    template<typename T>
    void log_result_error(const error::Result<T>& result, LogModule module = LogModule::CORE,
                          const std::string& context = "") {
        if (result.is_error()) {
            log_error(result.error(), module, context);
        }
    }
    
    /**
     * @brief 刷新缓冲区
     */
    void flush();
    
    /**
     * @brief 获取统计信息
     */
    struct Statistics {
        std::atomic<size_t> total_logs{0};
        std::atomic<size_t> dropped_logs{0};
        std::atomic<size_t> sampled_logs{0};
        std::atomic<size_t> rate_limited_logs{0};
        std::chrono::system_clock::time_point start_time;
        
        Statistics() : start_time(std::chrono::system_clock::now()) {}
        
        double get_log_rate() const {
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            return duration > 0 ? static_cast<double>(total_logs.load()) / duration : 0.0;
        }
    };
    
    const Statistics& get_statistics() const { return stats_; }
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();
    
    /**
     * @brief 启动/停止异步日志
     */
    void start();
    void stop();

private:
    Config config_;
    std::vector<std::unique_ptr<LogSink>> sinks_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::unique_ptr<Sampler> sampler_;
    Statistics stats_;
    
    // 异步日志
    std::queue<LogEntry> log_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> worker_thread_;
    
    void worker_loop();
    void write_entry(const LogEntry& entry);
    bool should_log(LogLevel level, LogModule module);
    std::string generate_transaction_id();
};

/**
 * @brief 便利宏定义
 */
#define PLC_LOG_TRACE(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::TRACE, module, msg)

#define PLC_LOG_DEBUG(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::DEBUG, module, msg)

#define PLC_LOG_INFO(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::INFO, module, msg)

#define PLC_LOG_WARN(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::WARN, module, msg)

#define PLC_LOG_ERROR(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::ERROR, module, msg)

#define PLC_LOG_FATAL(logger, module, msg) \
    (logger).log(plc_runtime::logging::LogLevel::FATAL, module, msg)

#define PLC_LOG_TXN(logger, level, module, txn_id, msg) \
    (logger).log(level, module, txn_id, msg)

#define PLC_LOG_PERF(logger, module, txn_id, func_code, duration, msg) \
    (logger).log(plc_runtime::logging::LogLevel::INFO, module, txn_id, func_code, duration, msg)

#define PLC_LOG_ERROR_CODE(logger, error_code, module, context) \
    (logger).log_error(error_code, module, context)

#define PLC_LOG_RESULT(logger, result, module, context) \
    (logger).log_result_error(result, module, context)

/**
 * @brief 全局日志记录器
 */
class GlobalLogger {
public:
    static StructuredLogger& instance();
    static void configure(const StructuredLogger::Config& config);

private:
    static std::unique_ptr<StructuredLogger> logger_;
    static std::once_flag init_flag_;
};

/**
 * @brief 便利函数
 */
inline std::string_view log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

inline std::string_view log_module_to_string(LogModule module) {
    switch (module) {
        case LogModule::CORE: return "CORE";
        case LogModule::SCHEDULER: return "SCHEDULER";
        case LogModule::MEMORY: return "MEMORY";
        case LogModule::COMPILER: return "COMPILER";
        case LogModule::RUNTIME: return "RUNTIME";
        case LogModule::COMMUNICATION: return "COMMUNICATION";
        case LogModule::IO: return "IO";
        case LogModule::MOTION: return "MOTION";
        case LogModule::SECURITY: return "SECURITY";
        case LogModule::NETWORK: return "NETWORK";
        default: return "USER";
    }
}

} // namespace logging
} // namespace plc_runtime

#endif // LOGGING_STRUCTURED_LOGGER_H