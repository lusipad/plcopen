/**
 * @file structured_logger.cpp
 * @brief 结构化日志系统实现
 * @version 1.0
 * @date 2025-09-09
 */

#include "logging/structured_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace plc_runtime {
namespace logging {

// =============================================================================
// ConsoleSink 实现
// =============================================================================

namespace {
    const char* get_color_code(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "\033[37m";   // 白色
            case LogLevel::DEBUG: return "\033[36m";   // 青色
            case LogLevel::INFO:  return "\033[32m";   // 绿色
            case LogLevel::WARN:  return "\033[33m";   // 黄色
            case LogLevel::ERROR: return "\033[31m";   // 红色
            case LogLevel::FATAL: return "\033[35m";   // 紫色
            default: return "\033[0m";
        }
    }
    
    const char* get_reset_code() {
        return "\033[0m";
    }
    
    bool is_terminal() {
#ifdef _WIN32
        return isatty(fileno(stdout)) || isatty(fileno(stderr));
#else
        return isatty(STDOUT_FILENO) || isatty(STDERR_FILENO);
#endif
    }
}

void ConsoleSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(console_mutex_);
    
    // 格式化时间戳
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    // 彩色输出（如果支持）
    bool use_color = use_colors_ && is_terminal();
    
    std::ostream& out = (entry.level >= LogLevel::ERROR) ? std::cerr : std::cout;
    
    if (use_color) {
        out << get_color_code(entry.level);
    }
    
    // 基本信息：时间戳 [级别] [模块] 消息
    out << oss.str() << " ["
        << log_level_to_string(entry.level) << "] ["
        << log_module_to_string(entry.module) << "] ";
    
    if (!entry.transaction_id.empty()) {
        out << "[TXN:" << entry.transaction_id << "] ";
    }
    
    if (!entry.function_code.empty()) {
        out << "[FC:" << entry.function_code << "] ";
    }
    
    if (entry.duration.count() > 0) {
        out << "[" << entry.duration.count() << "μs] ";
    }
    
    out << entry.message;
    
    // 附加字段
    if (!entry.fields.empty()) {
        out << " {";
        bool first = true;
        for (const auto& [key, value] : entry.fields) {
            if (!first) out << ", ";
            out << key << "=" << value;
            first = false;
        }
        out << "}";
    }
    
    if (use_color) {
        out << get_reset_code();
    }
    
    out << std::endl;
}

void ConsoleSink::flush() {
    std::cout.flush();
    std::cerr.flush();
}

// =============================================================================
// FileSink 实现
// =============================================================================

FileSink::FileSink(const std::string& filename, bool rotate, size_t max_size)
    : base_filename_(filename)
    , auto_rotate_(rotate)
    , max_file_size_(max_size)
    , current_size_(0) {
    
    file_.open(filename, std::ios::app);
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot open log file: " + filename);
    }
    
    // 获取现有文件大小
    file_.seekp(0, std::ios::end);
    current_size_ = file_.tellp();
}

FileSink::~FileSink() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FileSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (auto_rotate_) {
        rotate_if_needed();
    }
    
    // 格式化输出
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    
    file_ << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    file_ << '.' << std::setfill('0') << std::setw(3) << ms.count();
    file_ << " [" << log_level_to_string(entry.level) << "]";
    file_ << " [" << log_module_to_string(entry.module) << "]";
    
    if (!entry.transaction_id.empty()) {
        file_ << " [TXN:" << entry.transaction_id << "]";
    }
    
    if (!entry.function_code.empty()) {
        file_ << " [FC:" << entry.function_code << "]";
    }
    
    if (entry.duration.count() > 0) {
        file_ << " [" << entry.duration.count() << "μs]";
    }
    
    file_ << " " << entry.message;
    
    // 附加字段
    if (!entry.fields.empty()) {
        file_ << " {";
        bool first = true;
        for (const auto& [key, value] : entry.fields) {
            if (!first) file_ << ", ";
            file_ << key << "=" << value;
            first = false;
        }
        file_ << "}";
    }
    
    file_ << std::endl;
    
    // 更新文件大小
    std::streampos pos = file_.tellp();
    if (pos >= 0 && static_cast<size_t>(pos) > current_size_) {
        current_size_ = static_cast<size_t>(pos);
    }
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    file_.flush();
}

void FileSink::rotate_if_needed() {
    if (current_size_ >= max_file_size_) {
        file_.close();
        
        // 重命名旧文件
        std::string old_filename = generate_filename();
        std::filesystem::rename(base_filename_, old_filename);
        
        // 创建新文件
        file_.open(base_filename_, std::ios::out);
        current_size_ = 0;
    }
}

std::string FileSink::generate_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << base_filename_ << "." 
        << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    return oss.str();
}

// =============================================================================
// JsonSink 实现
// =============================================================================

void JsonSink::write(const LogEntry& entry) {
    std::string json_str = format_as_json(entry);
    LogEntry json_entry(LogLevel::INFO, LogModule::CORE, json_str);
    file_sink_.write(json_entry);
}

std::string JsonSink::format_as_json(const LogEntry& entry) {
    std::ostringstream oss;
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    
    oss << "{"
        << "\"timestamp\":\"" << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z\""
        << ",\"level\":\"" << log_level_to_string(entry.level) << "\""
        << ",\"module\":\"" << log_module_to_string(entry.module) << "\""
        << ",\"message\":\"" << entry.message << "\"";
    
    if (!entry.transaction_id.empty()) {
        oss << ",\"transaction_id\":\"" << entry.transaction_id << "\"";
    }
    
    if (!entry.function_code.empty()) {
        oss << ",\"function_code\":\"" << entry.function_code << "\"";
    }
    
    if (entry.duration.count() > 0) {
        oss << ",\"duration_us\":" << entry.duration.count();
    }
    
    oss << ",\"thread_id\":\"" << entry.thread_id << "\"";
    
    // 附加字段
    for (const auto& [key, value] : entry.fields) {
        oss << ",\"" << key << "\":\"" << value << "\"";
    }
    
    oss << "}";
    
    return oss.str();
}

// =============================================================================
// RateLimiter 实现
// =============================================================================

bool RateLimiter::should_allow(const std::string& key) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    cleanup_old_windows();
    
    auto now = std::chrono::system_clock::now();
    auto& window = windows_[key];
    
    // 初始化窗口
    if (window.window_start == std::chrono::system_clock::time_point{}) {
        window.window_start = now;
    }
    
    // 检查是否需要重置窗口
    if (now - window.window_start >= config_.window_duration) {
        window.window_start = now;
        window.event_count = 0;
        window.burst_count = 0;
        window.last_burst_start = std::chrono::system_clock::time_point{};
    }
    
    // 检查突发限制
    if (window.last_burst_start != std::chrono::system_clock::time_point{} &&
        now - window.last_burst_start < config_.burst_duration) {
        if (window.burst_count >= config_.max_burst_events) {
            window.dropped_count++;
            return false;
        }
        window.burst_count++;
    } else {
        // 开始新的突发
        window.last_burst_start = now;
        window.burst_count = 1;
    }
    
    // 检查窗口限制
    if (window.event_count >= config_.max_events_per_window) {
        window.dropped_count++;
        return false;
    }
    
    window.event_count++;
    return true;
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    windows_.clear();
}

size_t RateLimiter::get_dropped_count(const std::string& key) const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    auto it = windows_.find(key);
    return it != windows_.end() ? it->second.dropped_count : 0;
}

void RateLimiter::cleanup_old_windows() {
    auto now = std::chrono::system_clock::now();
    auto it = windows_.begin();
    
    while (it != windows_.end()) {
        if (now - it->second.window_start > config_.window_duration * 2) {
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
}

// =============================================================================
// Sampler 实现
// =============================================================================

bool Sampler::should_sample(LogLevel level, LogModule module) {
    (void)module; // 消除未使用参数警告
    reset_minute_counter_if_needed();
    
    // 如果已达到每分钟最大采样数，拒绝采样
    if (samples_this_minute_.load() >= config_.max_samples_per_minute) {
        return false;
    }
    
    // 重要级别总是采样
    if (level >= LogLevel::ERROR) {
        samples_this_minute_.fetch_add(1);
        return true;
    }
    
    // 根据采样率决定
    double current_rate = current_sample_rate_.load();
    if (generate_random() < current_rate) {
        samples_this_minute_.fetch_add(1);
        return true;
    }
    
    return false;
}

void Sampler::update_load(size_t current_log_rate) {
    if (config_.adaptive_sampling) {
        double new_rate = calculate_adaptive_rate(current_log_rate);
        current_sample_rate_.store(new_rate);
    }
}

double Sampler::generate_random() const {
    // 简单的线性同余生成器
    thread_local uint64_t state = rng_seed_;
    state = state * 1103515245 + 12345;
    return (state & 0x7FFFFFFF) / double(0x7FFFFFFF);
}

void Sampler::reset_minute_counter_if_needed() {
    auto now = std::chrono::system_clock::now();
    auto one_minute = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::minutes(1));
    if (now - minute_start_ >= one_minute) {
        std::lock_guard<std::mutex> lock(sampler_mutex_);
        if (now - minute_start_ >= one_minute) {
            minute_start_ = now;
            samples_this_minute_.store(0);
        }
    }
}

double Sampler::calculate_adaptive_rate(size_t current_load) const {
    // 自适应算法：负载高时降低采样率
    if (current_load <= 100) return 1.0;
    if (current_load <= 1000) return 0.5;
    if (current_load <= 5000) return 0.1;
    return 0.01; // 极高负载时仅采样1%
}

// =============================================================================
// StructuredLogger 实现
// =============================================================================

StructuredLogger::StructuredLogger(const Config& config) : config_(config) {
    if (config_.enable_rate_limiting) {
        rate_limiter_ = std::make_unique<RateLimiter>(config_.rate_limiter);
    }
    
    if (config_.enable_sampling) {
        sampler_ = std::make_unique<Sampler>(config_.sampler);
    }
    
    if (config_.async_logging) {
        start();
    }
}

StructuredLogger::~StructuredLogger() {
    stop();
}

void StructuredLogger::add_sink(std::unique_ptr<LogSink> sink) {
    sinks_.push_back(std::move(sink));
}

void StructuredLogger::clear_sinks() {
    sinks_.clear();
}

void StructuredLogger::set_level(LogLevel level) {
    config_.min_level = level;
}

void StructuredLogger::log(LogLevel level, LogModule module, const std::string& message) {
    if (!should_log(level, module)) {
        return;
    }
    
    LogEntry entry(level, module, message);
    
    if (config_.async_logging) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (log_queue_.size() < config_.buffer_size) {
            log_queue_.push(std::move(entry));
            queue_cv_.notify_one();
        } else {
            stats_.dropped_logs.fetch_add(1);
        }
    } else {
        write_entry(entry);
    }
    
    stats_.total_logs.fetch_add(1);
}

void StructuredLogger::log(LogLevel level, LogModule module, const std::string& txn_id, 
                          const std::string& message) {
    if (!should_log(level, module)) {
        return;
    }
    
    LogEntry entry(level, module, message);
    entry.transaction_id = txn_id;
    
    if (config_.async_logging) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (log_queue_.size() < config_.buffer_size) {
            log_queue_.push(std::move(entry));
            queue_cv_.notify_one();
        } else {
            stats_.dropped_logs.fetch_add(1);
        }
    } else {
        write_entry(entry);
    }
    
    stats_.total_logs.fetch_add(1);
}

void StructuredLogger::log(LogLevel level, LogModule module, const std::string& txn_id, 
                          const std::string& function_code, std::chrono::microseconds duration,
                          const std::string& message) {
    if (!should_log(level, module)) {
        return;
    }
    
    LogEntry entry(level, module, message);
    entry.transaction_id = txn_id;
    entry.function_code = function_code;
    entry.duration = duration;
    
    if (config_.async_logging) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (log_queue_.size() < config_.buffer_size) {
            log_queue_.push(std::move(entry));
            queue_cv_.notify_one();
        } else {
            stats_.dropped_logs.fetch_add(1);
        }
    } else {
        write_entry(entry);
    }
    
    stats_.total_logs.fetch_add(1);
}

void StructuredLogger::log(LogLevel level, LogModule module, const std::string& message,
                          const std::unordered_map<std::string, std::string>& fields) {
    if (!should_log(level, module)) {
        return;
    }
    
    LogEntry entry(level, module, message);
    entry.fields = fields;
    
    if (config_.async_logging) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (log_queue_.size() < config_.buffer_size) {
            log_queue_.push(std::move(entry));
            queue_cv_.notify_one();
        } else {
            stats_.dropped_logs.fetch_add(1);
        }
    } else {
        write_entry(entry);
    }
    
    stats_.total_logs.fetch_add(1);
}

void StructuredLogger::log_error(error::ErrorCode error_code, LogModule module,
                                 const std::string& context) {
    auto error_info = error::ErrorCodeUtils::getErrorInfo(error_code);
    auto severity = error::ErrorCodeUtils::getSeverity(error_code);
    
    // 映射错误严重程度到日志级别
    LogLevel log_level = LogLevel::INFO;
    switch (severity) {
        case error::ErrorSeverity::INFO:
            log_level = LogLevel::INFO;
            break;
        case error::ErrorSeverity::WARNING:
            log_level = LogLevel::WARN;
            break;
        case error::ErrorSeverity::ERROR:
            log_level = LogLevel::ERROR;
            break;
        case error::ErrorSeverity::CRITICAL:
        case error::ErrorSeverity::FATAL:
            log_level = LogLevel::FATAL;
            break;
    }
    
    std::string message = std::string(error_info.message);
    if (!context.empty()) {
        message += " (" + context + ")";
    }
    
    // 添加错误码相关字段
    std::unordered_map<std::string, std::string> fields;
    fields["error_code"] = error::ErrorCodeUtils::toString(error_code);
    fields["error_category"] = std::string(error::ErrorCodeUtils::categoryToString(error_info.category));
    fields["error_severity"] = std::string(error::ErrorCodeUtils::severityToString(severity));
    fields["solution"] = std::string(error_info.solution);
    
    log(log_level, module, message, fields);
}

void StructuredLogger::flush() {
    if (config_.async_logging) {
        // 等待队列清空
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return log_queue_.empty(); });
    }
    
    for (auto& sink : sinks_) {
        sink->flush();
    }
}

void StructuredLogger::reset_statistics() {
    stats_.total_logs.store(0);
    stats_.dropped_logs.store(0);
    stats_.sampled_logs.store(0);
    stats_.rate_limited_logs.store(0);
    stats_.start_time = std::chrono::system_clock::now();
}

void StructuredLogger::start() {
    if (!running_.exchange(true)) {
        worker_thread_ = std::make_unique<std::thread>(&StructuredLogger::worker_loop, this);
    }
}

void StructuredLogger::stop() {
    if (running_.exchange(false)) {
        queue_cv_.notify_all();
        if (worker_thread_ && worker_thread_->joinable()) {
            worker_thread_->join();
        }
        worker_thread_.reset();
    }
}

void StructuredLogger::worker_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        queue_cv_.wait_for(lock, config_.flush_interval, [this] {
            return !log_queue_.empty() || !running_.load();
        });
        
        while (!log_queue_.empty()) {
            LogEntry entry = std::move(log_queue_.front());
            log_queue_.pop();
            
            lock.unlock();
            write_entry(entry);
            lock.lock();
        }
    }
    
    // 处理剩余的日志条目
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!log_queue_.empty()) {
        write_entry(log_queue_.front());
        log_queue_.pop();
    }
}

void StructuredLogger::write_entry(const LogEntry& entry) {
    for (auto& sink : sinks_) {
        try {
            sink->write(entry);
        } catch (const std::exception& e) {
            // 日志写入失败，输出到stderr
            std::cerr << "Log sink error: " << e.what() << std::endl;
        }
    }
}

bool StructuredLogger::should_log(LogLevel level, LogModule module) {
    // 检查最小级别
    if (level < config_.min_level) {
        return false;
    }
    
    // 检查限速器
    if (rate_limiter_ && !rate_limiter_->should_allow()) {
        stats_.rate_limited_logs.fetch_add(1);
        return false;
    }
    
    // 检查采样器
    if (sampler_ && !sampler_->should_sample(level, module)) {
        stats_.sampled_logs.fetch_add(1);
        return false;
    }
    
    return true;
}

std::string StructuredLogger::generate_transaction_id() {
    static thread_local uint64_t counter = 0;
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << std::hex << timestamp << "-" << std::this_thread::get_id() << "-" << ++counter;
    return oss.str();
}

// =============================================================================
// GlobalLogger 实现
// =============================================================================

std::unique_ptr<StructuredLogger> GlobalLogger::logger_;
std::once_flag GlobalLogger::init_flag_;

StructuredLogger& GlobalLogger::instance() {
    std::call_once(init_flag_, []() {
        if (!logger_) {
            StructuredLogger::Config config;
            config.min_level = LogLevel::INFO;
            config.async_logging = true;
            
            logger_ = std::make_unique<StructuredLogger>(config);
            
            // 默认添加控制台输出
            logger_->add_sink(std::make_unique<ConsoleSink>());
        }
    });
    
    return *logger_;
}

void GlobalLogger::configure(const StructuredLogger::Config& config) {
    logger_ = std::make_unique<StructuredLogger>(config);
}

} // namespace logging
} // namespace plc_runtime