/**
 * @file GPIODriver.h
 * @brief GPIO驱动基础框架和高性能实现
 * @version MVP-1.0
 * @date 2025-09-04
 */

#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <string>

namespace plc_runtime {
namespace io {

/**
 * @brief GPIO方向
 */
enum class GPIODirection : uint8_t {
    INPUT = 0,
    OUTPUT = 1
};

/**
 * @brief GPIO上拉/下拉模式
 */
enum class GPIOPullMode : uint8_t {
    NONE = 0,      // 无上拉下拉
    UP = 1,        // 上拉
    DOWN = 2       // 下拉
};

/**
 * @brief GPIO触发模式
 */
enum class GPIOTriggerMode : uint8_t {
    NONE = 0,      // 无触发
    RISING = 1,    // 上升沿
    FALLING = 2,   // 下降沿
    BOTH = 3       // 双沿
};

/**
 * @brief GPIO配置结构
 */
struct GPIOConfig {
    uint32_t pin_number;           // GPIO引脚号
    GPIODirection direction;       // 方向
    GPIOPullMode pull_mode;        // 上拉/下拉模式
    GPIOTriggerMode trigger_mode;  // 触发模式
    bool active_low;               // 是否低电平有效
    uint32_t debounce_ms;          // 消抖时间(毫秒)
    
    GPIOConfig() : pin_number(0), direction(GPIODirection::INPUT),
                  pull_mode(GPIOPullMode::NONE), trigger_mode(GPIOTriggerMode::NONE),
                  active_low(false), debounce_ms(0) {}
    
    GPIOConfig(uint32_t pin, GPIODirection dir) 
        : pin_number(pin), direction(dir), pull_mode(GPIOPullMode::NONE),
          trigger_mode(GPIOTriggerMode::NONE), active_low(false), debounce_ms(0) {}
};

/**
 * @brief GPIO状态信息
 */
struct GPIOStatus {
    bool is_configured;            // 是否已配置
    bool last_value;               // 最后读取的值
    uint64_t last_change_ns;       // 最后变化时间
    uint32_t change_count;         // 变化计数
    uint32_t error_count;          // 错误计数
    
    GPIOStatus() : is_configured(false), last_value(false), 
                  last_change_ns(0), change_count(0), error_count(0) {}
};

/**
 * @brief GPIO中断回调函数类型
 */
using GPIOInterruptCallback = std::function<void(uint32_t pin, bool value, uint64_t timestamp_ns)>;

/**
 * @brief GPIO驱动抽象基类
 */
class GPIODriver {
public:
    virtual ~GPIODriver() = default;
    
    /**
     * @brief 配置GPIO引脚
     */
    virtual bool configure_pin(const GPIOConfig& config) = 0;
    
    /**
     * @brief 移除GPIO引脚配置
     */
    virtual bool unconfigure_pin(uint32_t pin) = 0;
    
    /**
     * @brief 读取单个GPIO引脚
     */
    virtual bool read_pin(uint32_t pin, bool& value) = 0;
    
    /**
     * @brief 写入单个GPIO引脚
     */
    virtual bool write_pin(uint32_t pin, bool value) = 0;
    
    /**
     * @brief 批量读取GPIO引脚
     */
    virtual bool read_batch(const std::vector<uint32_t>& pins, 
                           std::vector<bool>& values) = 0;
    
    /**
     * @brief 批量写入GPIO引脚
     */
    virtual bool write_batch(const std::vector<uint32_t>& pins, 
                            const std::vector<bool>& values) = 0;
    
    /**
     * @brief 获取引脚状态
     */
    virtual bool get_pin_status(uint32_t pin, GPIOStatus& status) = 0;
    
    /**
     * @brief 设置中断回调
     */
    virtual bool set_interrupt_callback(uint32_t pin, GPIOInterruptCallback callback) = 0;
    
    /**
     * @brief 清除中断回调
     */
    virtual bool clear_interrupt_callback(uint32_t pin) = 0;
    
    /**
     * @brief 获取驱动名称
     */
    virtual std::string get_driver_name() const = 0;
    
    /**
     * @brief 获取支持的引脚列表
     */
    virtual std::vector<uint32_t> get_supported_pins() const = 0;
    
    /**
     * @brief 清理所有资源
     */
    virtual void cleanup() = 0;
};

/**
 * @brief Linux Sysfs GPIO驱动实现
 */
class LinuxSysfsGPIODriver : public GPIODriver {
public:
    /**
     * @brief 性能配置
     */
    struct PerformanceConfig {
        bool enable_fast_path;               // 启用快速路径优化
        bool cache_file_descriptors;         // 缓存文件描述符
        bool use_memory_mapping;             // 使用内存映射(高级)
        uint32_t batch_size_threshold;       // 批量操作阈值
        uint32_t poll_timeout_ms;            // 轮询超时
        
        PerformanceConfig() : enable_fast_path(true), cache_file_descriptors(true), 
                             use_memory_mapping(false), batch_size_threshold(8), 
                             poll_timeout_ms(1) {}
    };
    
private:
    /**
     * @brief 引脚信息
     */
    struct PinInfo {
        GPIOConfig config;                // 配置
        int value_fd;                     // 值文件描述符
        int direction_fd;                 // 方向文件描述符
        bool exported;                    // 是否已导出
        GPIOStatus status;                // 状态信息
        GPIOInterruptCallback callback;   // 中断回调
        std::atomic<bool> callback_active; // 回调活跃状态
        
        PinInfo() : value_fd(-1), direction_fd(-1), exported(false), callback_active(false) {}
    };
    
    // 引脚管理
    mutable std::mutex pins_mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<PinInfo>> pins_;
    
    // 性能配置
    PerformanceConfig perf_config_;
    
    // 中断处理
    std::atomic<bool> interrupt_thread_running_;
    std::thread interrupt_thread_;
    std::vector<int> epoll_fds_;
    int epoll_fd_;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    struct {
        uint64_t read_count;
        uint64_t write_count;
        uint64_t error_count;
        uint64_t interrupt_count;
        uint64_t total_read_time_ns;
        uint64_t total_write_time_ns;
    } statistics_;
    
public:
    explicit LinuxSysfsGPIODriver(const PerformanceConfig& config = PerformanceConfig{});
    ~LinuxSysfsGPIODriver() override;
    
    // 禁用拷贝和移动
    LinuxSysfsGPIODriver(const LinuxSysfsGPIODriver&) = delete;
    LinuxSysfsGPIODriver& operator=(const LinuxSysfsGPIODriver&) = delete;
    
    // GPIODriver接口实现
    bool configure_pin(const GPIOConfig& config) override;
    bool unconfigure_pin(uint32_t pin) override;
    bool read_pin(uint32_t pin, bool& value) override;
    bool write_pin(uint32_t pin, bool value) override;
    bool read_batch(const std::vector<uint32_t>& pins, std::vector<bool>& values) override;
    bool write_batch(const std::vector<uint32_t>& pins, const std::vector<bool>& values) override;
    bool get_pin_status(uint32_t pin, GPIOStatus& status) override;
    bool set_interrupt_callback(uint32_t pin, GPIOInterruptCallback callback) override;
    bool clear_interrupt_callback(uint32_t pin) override;
    std::string get_driver_name() const override;
    std::vector<uint32_t> get_supported_pins() const override;
    void cleanup() override;
    
    /**
     * @brief 获取性能统计
     */
    struct PerformanceStatistics {
        uint64_t read_count;
        uint64_t write_count;
        uint64_t error_count;
        uint64_t interrupt_count;
        double avg_read_time_ns;
        double avg_write_time_ns;
        uint64_t uptime_ns;
    };
    
    PerformanceStatistics get_performance_statistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();
    
private:
    // 系统接口
    bool export_pin(uint32_t pin);
    bool unexport_pin(uint32_t pin);
    bool set_pin_direction(uint32_t pin, GPIODirection direction);
    bool set_pin_edge(uint32_t pin, GPIOTriggerMode mode);
    
    // 文件描述符管理
    int open_pin_value_fd(uint32_t pin);
    int open_pin_direction_fd(uint32_t pin);
    void close_pin_fds(PinInfo& pin_info);
    
    // 快速I/O路径
    bool read_pin_fast(PinInfo& pin_info, bool& value);
    bool write_pin_fast(PinInfo& pin_info, bool value);
    
    // 批量操作优化
    bool read_batch_optimized(const std::vector<uint32_t>& pins, std::vector<bool>& values);
    bool write_batch_optimized(const std::vector<uint32_t>& pins, const std::vector<bool>& values);
    
    // 回退方法
    bool read_pin_fallback(uint32_t pin, bool& value);
    bool write_pin_fallback(uint32_t pin, bool value);
    
    // 中断处理
    void start_interrupt_thread();
    void stop_interrupt_thread();
    void interrupt_thread_loop();
    void handle_interrupt(uint32_t pin, PinInfo& pin_info);
    
    // 工具函数
    static uint64_t get_current_time_ns();
    static std::string gpio_path(uint32_t pin, const std::string& attribute = {});
    void update_statistics(bool is_read, uint64_t duration_ns, bool success);
};

/**
 * @brief 虚拟GPIO驱动(用于测试和仿真)
 */
class VirtualGPIODriver : public GPIODriver {
private:
    struct VirtualPin {
        GPIOConfig config;
        bool value;
        GPIOStatus status;
        GPIOInterruptCallback callback;
        
        VirtualPin() : value(false) {}
    };
    
    mutable std::mutex pins_mutex_;
    std::unordered_map<uint32_t, VirtualPin> virtual_pins_;
    
    // 仿真参数
    bool simulate_delays_;
    uint32_t read_delay_us_;
    uint32_t write_delay_us_;
    double error_rate_;
    
public:
    /**
     * @brief 仿真配置
     */
    struct SimulationConfig {
        bool simulate_delays;             // 仿真延迟
        uint32_t read_delay_us;          // 读取延迟
        uint32_t write_delay_us;         // 写入延迟
        double error_rate;               // 错误率(0.0-1.0)
        uint32_t max_pins;               // 最大引脚数
        
        SimulationConfig() : simulate_delays(false), read_delay_us(1), 
                           write_delay_us(1), error_rate(0.0), max_pins(1024) {}
    };
    
    explicit VirtualGPIODriver(const SimulationConfig& config = SimulationConfig{});
    ~VirtualGPIODriver() override = default;
    
    // GPIODriver接口实现
    bool configure_pin(const GPIOConfig& config) override;
    bool unconfigure_pin(uint32_t pin) override;
    bool read_pin(uint32_t pin, bool& value) override;
    bool write_pin(uint32_t pin, bool value) override;
    bool read_batch(const std::vector<uint32_t>& pins, std::vector<bool>& values) override;
    bool write_batch(const std::vector<uint32_t>& pins, const std::vector<bool>& values) override;
    bool get_pin_status(uint32_t pin, GPIOStatus& status) override;
    bool set_interrupt_callback(uint32_t pin, GPIOInterruptCallback callback) override;
    bool clear_interrupt_callback(uint32_t pin) override;
    std::string get_driver_name() const override;
    std::vector<uint32_t> get_supported_pins() const override;
    void cleanup() override;
    
    /**
     * @brief 设置仿真值(用于测试)
     */
    bool set_simulated_value(uint32_t pin, bool value);
    
    /**
     * @brief 触发仿真中断(用于测试)
     */
    bool trigger_simulated_interrupt(uint32_t pin);
    
private:
    void simulate_delay(uint32_t delay_us) const;
    bool simulate_error() const;
    static uint64_t get_current_time_ns();
};

/**
 * @brief GPIO驱动工厂
 */
class GPIODriverFactory {
public:
    enum class DriverType {
        LINUX_SYSFS,    // Linux Sysfs驱动
        VIRTUAL,        // 虚拟驱动
        AUTO_DETECT     // 自动检测
    };
    
    /**
     * @brief 创建GPIO驱动
     */
    static std::unique_ptr<GPIODriver> create_driver(DriverType type);
    
    /**
     * @brief 自动检测最佳驱动
     */
    static std::unique_ptr<GPIODriver> create_best_driver();
    
    /**
     * @brief 检测系统GPIO能力
     */
    static bool detect_gpio_capability();
    
private:
    static bool is_linux_sysfs_available();
    static std::vector<uint32_t> detect_available_pins();
};

} // namespace io
} // namespace plc_runtime