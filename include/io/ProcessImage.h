/**
 * @file ProcessImage.h
 * @brief 过程映像双缓冲系统实现
 * @version MVP-1.0
 * @date 2025-09-04
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace plc_runtime {
namespace io {

/**
 * @brief I/O点数据类型
 */
enum class IODataType : uint8_t {
    BOOL = 1,      // 1位布尔值
    BYTE = 8,      // 8位字节
    WORD = 16,     // 16位字
    DWORD = 32,    // 32位双字
    REAL = 32      // 32位实数
};

/**
 * @brief I/O点类型
 */
enum class IOType : uint8_t {
    DIGITAL_INPUT = 0,   // 数字输入 DI
    DIGITAL_OUTPUT = 1,  // 数字输出 DO
    ANALOG_INPUT = 2,    // 模拟输入 AI
    ANALOG_OUTPUT = 3    // 模拟输出 AO
};

/**
 * @brief I/O状态
 */
enum class IOStatus : uint8_t {
    VALID = 0,      // 数据有效
    INVALID = 1,    // 数据无效
    ERROR = 2,      // 通信错误
    TIMEOUT = 3     // 超时
};

/**
 * @brief I/O点定义
 */
struct alignas(64) IOPoint {
    uint32_t address;              // I/O地址
    IOType type;                   // I/O类型
    IODataType data_type;          // 数据类型
    IOStatus status;               // I/O状态
    uint8_t reserved;              // 对齐保留
    
    // 数据联合体
    union {
        bool digital_value;        // 布尔值
        uint8_t byte_value;        // 字节值
        uint16_t word_value;       // 字值
        uint32_t dword_value;      // 双字值
        float real_value;          // 实数值
        uint64_t raw_value;        // 原始值
    } value;
    
    uint64_t timestamp_ns;         // 时间戳(纳秒)
    uint32_t quality;              // 数据质量
    
    IOPoint() noexcept : address(0), type(IOType::DIGITAL_INPUT), 
                        data_type(IODataType::BOOL), status(IOStatus::INVALID),
                        reserved(0), timestamp_ns(0), quality(0) {
        value.raw_value = 0;
    }
};

/**
 * @brief 过程映像区
 */
class alignas(4096) ProcessImage {
public:
    static constexpr size_t MAX_IO_POINTS = 2048;    // 最大I/O点数
    static constexpr size_t MAX_INPUT_POINTS = 1024;  // 最大输入点数
    static constexpr size_t MAX_OUTPUT_POINTS = 1024; // 最大输出点数
    
private:
    // I/O点数组 - 按缓存行对齐
    alignas(64) IOPoint input_points_[MAX_INPUT_POINTS];   // 输入点
    alignas(64) IOPoint output_points_[MAX_OUTPUT_POINTS]; // 输出点
    
    // 版本控制
    std::atomic<uint64_t> input_version_;      // 输入版本号
    std::atomic<uint64_t> output_version_;     // 输出版本号
    std::atomic<uint64_t> last_update_ns_;     // 最后更新时间
    
    // 统计信息
    std::atomic<uint32_t> input_count_;        // 输入点数量
    std::atomic<uint32_t> output_count_;       // 输出点数量
    std::atomic<uint32_t> error_count_;        // 错误计数
    std::atomic<uint32_t> update_count_;       // 更新计数
    
public:
    ProcessImage() noexcept 
        : input_version_(0), output_version_(0), last_update_ns_(0),
          input_count_(0), output_count_(0), error_count_(0), update_count_(0) {
        // 初始化I/O点数组
        for (size_t i = 0; i < MAX_INPUT_POINTS; ++i) {
            input_points_[i] = IOPoint{};
        }
        for (size_t i = 0; i < MAX_OUTPUT_POINTS; ++i) {
            output_points_[i] = IOPoint{};
        }
    }
    
    // 禁用拷贝和移动
    ProcessImage(const ProcessImage&) = delete;
    ProcessImage& operator=(const ProcessImage&) = delete;
    ProcessImage(ProcessImage&&) = delete;
    ProcessImage& operator=(ProcessImage&&) = delete;
    
    /**
     * @brief 获取输入点
     */
    const IOPoint& get_input_point(uint32_t index) const noexcept {
        if (index < MAX_INPUT_POINTS) {
            return input_points_[index];
        }
        static const IOPoint invalid_point{};
        return invalid_point;
    }
    
    /**
     * @brief 获取输出点
     */
    const IOPoint& get_output_point(uint32_t index) const noexcept {
        if (index < MAX_OUTPUT_POINTS) {
            return output_points_[index];
        }
        static const IOPoint invalid_point{};
        return invalid_point;
    }
    
    /**
     * @brief 设置输入点
     */
    bool set_input_point(uint32_t index, const IOPoint& point) noexcept {
        if (index >= MAX_INPUT_POINTS) {
            return false;
        }
        
        input_points_[index] = point;
        input_points_[index].timestamp_ns = get_current_time_ns();
        input_version_.fetch_add(1, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief 设置输出点
     */
    bool set_output_point(uint32_t index, const IOPoint& point) noexcept {
        if (index >= MAX_OUTPUT_POINTS) {
            return false;
        }
        
        output_points_[index] = point;
        output_points_[index].timestamp_ns = get_current_time_ns();
        output_version_.fetch_add(1, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief 获取输入版本号
     */
    uint64_t get_input_version() const noexcept {
        return input_version_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取输出版本号
     */
    uint64_t get_output_version() const noexcept {
        return output_version_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取最后更新时间
     */
    uint64_t get_last_update_time() const noexcept {
        return last_update_ns_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 标记更新完成
     */
    void mark_update_complete() noexcept {
        last_update_ns_.store(get_current_time_ns(), std::memory_order_release);
        update_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief 获取统计信息
     */
    struct Statistics {
        uint32_t input_count;
        uint32_t output_count;
        uint32_t error_count;
        uint32_t update_count;
        uint64_t last_update_ns;
    };
    
    Statistics get_statistics() const noexcept {
        return {
            input_count_.load(std::memory_order_relaxed),
            output_count_.load(std::memory_order_relaxed),
            error_count_.load(std::memory_order_relaxed),
            update_count_.load(std::memory_order_relaxed),
            last_update_ns_.load(std::memory_order_relaxed)
        };
    }
    
private:
    /**
     * @brief 获取当前时间(纳秒)
     */
    static uint64_t get_current_time_ns() noexcept {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        return static_cast<uint64_t>(ns);
    }
};

/**
 * @brief 过程映像管理器 - 双缓冲实现
 */
class ProcessImageManager {
public:
    /**
     * @brief 配置参数
     */
    struct Config {
        bool auto_swap = true;              // 自动切换缓冲区
        uint64_t swap_interval_ns = 1000000; // 切换间隔(1ms)
        bool enable_statistics = true;      // 启用统计
        size_t max_swap_retries = 3;        // 最大重试次数
        
        Config() = default;
    };
    
private:
    // 双缓冲区
    std::unique_ptr<ProcessImage> images_[2];
    
    // 缓冲区控制
    std::atomic<int> active_buffer_;       // 当前活动缓冲区(0或1)
    std::atomic<bool> swap_pending_;       // 切换挂起标志
    std::atomic<bool> swap_in_progress_;   // 切换进行中标志
    
    // 配置和统计
    Config config_;
    mutable std::mutex stats_mutex_;
    uint64_t swap_count_;
    uint64_t swap_errors_;
    uint64_t last_swap_time_ns_;
    
public:
    explicit ProcessImageManager(const Config& config = Config{})
        : active_buffer_(0), swap_pending_(false), swap_in_progress_(false),
          config_(config), swap_count_(0), swap_errors_(0), last_swap_time_ns_(0) {
        
        // 创建双缓冲区
        images_[0] = std::make_unique<ProcessImage>();
        images_[1] = std::make_unique<ProcessImage>();
    }
    
    ~ProcessImageManager() = default;
    
    // 禁用拷贝和移动
    ProcessImageManager(const ProcessImageManager&) = delete;
    ProcessImageManager& operator=(const ProcessImageManager&) = delete;
    
    /**
     * @brief 获取读缓冲区(用户读取)
     */
    const ProcessImage* get_read_buffer() const noexcept {
        int buffer_index = active_buffer_.load(std::memory_order_acquire);
        return images_[buffer_index].get();
    }
    
    /**
     * @brief 获取写缓冲区(I/O更新)
     */
    ProcessImage* get_write_buffer() noexcept {
        int buffer_index = active_buffer_.load(std::memory_order_acquire);
        return images_[1 - buffer_index].get(); // 返回非活动缓冲区
    }
    
    /**
     * @brief 执行缓冲区切换
     */
    bool swap_buffers() noexcept {
        // 检查是否已在切换中
        bool expected = false;
        if (!swap_in_progress_.compare_exchange_weak(expected, true, 
                                                    std::memory_order_acquire)) {
            return false; // 已在切换中
        }
        
        try {
            // 原子切换活动缓冲区
            int old_buffer = active_buffer_.load(std::memory_order_relaxed);
            int new_buffer = 1 - old_buffer;
            
            active_buffer_.store(new_buffer, std::memory_order_release);
            
            // 标记写缓冲区更新完成
            images_[old_buffer]->mark_update_complete();
            
            // 更新统计信息
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                swap_count_++;
                last_swap_time_ns_ = get_current_time_ns();
            }
            
            swap_pending_.store(false, std::memory_order_relaxed);
            swap_in_progress_.store(false, std::memory_order_release);
            
            return true;
            
        } catch (...) {
            // 切换失败，恢复状态
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                swap_errors_++;
            }
            
            swap_pending_.store(false, std::memory_order_relaxed);
            swap_in_progress_.store(false, std::memory_order_release);
            
            return false;
        }
    }
    
    /**
     * @brief 检查是否可以安全切换
     */
    bool is_swap_safe() const noexcept {
        return !swap_in_progress_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 请求缓冲区切换
     */
    void request_swap() noexcept {
        swap_pending_.store(true, std::memory_order_relaxed);
    }
    
    /**
     * @brief 检查是否有挂起的切换请求
     */
    bool has_pending_swap() const noexcept {
        return swap_pending_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief 获取管理器统计信息
     */
    struct ManagerStatistics {
        uint64_t swap_count;
        uint64_t swap_errors;
        uint64_t last_swap_time_ns;
        ProcessImage::Statistics read_buffer_stats;
        ProcessImage::Statistics write_buffer_stats;
    };
    
    ManagerStatistics get_statistics() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        
        const auto* read_buf = get_read_buffer();
        auto* write_buf = const_cast<ProcessImageManager*>(this)->get_write_buffer();
        
        return {
            swap_count_,
            swap_errors_,
            last_swap_time_ns_,
            read_buf ? read_buf->get_statistics() : ProcessImage::Statistics{},
            write_buf ? write_buf->get_statistics() : ProcessImage::Statistics{}
        };
    }
    
private:
    static uint64_t get_current_time_ns() noexcept {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        return static_cast<uint64_t>(ns);
    }
};

} // namespace io
} // namespace plc_runtime