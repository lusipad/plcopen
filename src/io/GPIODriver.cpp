/**
 * @file GPIODriver.cpp
 * @brief GPIO驱动实现
 * @version MVP-1.0
 */

#include "io/GPIODriver.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <errno.h>
#endif

namespace plc_runtime {
namespace io {

// =============================================================================
// LinuxSysfsGPIODriver Implementation
// =============================================================================

LinuxSysfsGPIODriver::LinuxSysfsGPIODriver(const PerformanceConfig& config)
    : perf_config_(config), interrupt_thread_running_(false), epoll_fd_(-1) {
    
    // 清空统计信息
    std::lock_guard<std::mutex> lock(stats_mutex_);
    memset(&statistics_, 0, sizeof(statistics_));
    
#ifdef __linux__
    // 创建epoll实例用于中断处理
    if (perf_config_.enable_fast_path) {
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ >= 0) {
            start_interrupt_thread();
        }
    }
#endif
}

LinuxSysfsGPIODriver::~LinuxSysfsGPIODriver() {
    cleanup();
}

bool LinuxSysfsGPIODriver::configure_pin(const GPIOConfig& config) {
#ifdef _WIN32
    return false; // Windows不支持
#else
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    uint32_t pin = config.pin_number;
    
    // 检查引脚是否已配置
    auto it = pins_.find(pin);
    if (it != pins_.end()) {
        // 清理现有配置
        unconfigure_pin(pin);
    }
    
    // 创建新的引脚信息
    auto pin_info = std::make_unique<PinInfo>();
    pin_info->config = config;
    
    // 导出引脚
    if (!export_pin(pin)) {
        return false;
    }
    pin_info->exported = true;
    
    // 设置方向
    if (!set_pin_direction(pin, config.direction)) {
        unexport_pin(pin);
        return false;
    }
    
    // 设置边沿触发(仅对输入)
    if (config.direction == GPIODirection::INPUT && 
        config.trigger_mode != GPIOTriggerMode::NONE) {
        if (!set_pin_edge(pin, config.trigger_mode)) {
            unexport_pin(pin);
            return false;
        }
    }
    
    // 打开文件描述符
    if (perf_config_.cache_file_descriptors) {
        pin_info->value_fd = open_pin_value_fd(pin);
        if (pin_info->value_fd < 0) {
            unexport_pin(pin);
            return false;
        }
        
        if (config.direction == GPIODirection::OUTPUT) {
            pin_info->direction_fd = open_pin_direction_fd(pin);
        }
    }
    
    // 初始化状态
    pin_info->status.is_configured = true;
    pin_info->status.last_change_ns = get_current_time_ns();
    
    // 存储引脚信息
    pins_[pin] = std::move(pin_info);
    
    return true;
#endif
}

bool LinuxSysfsGPIODriver::unconfigure_pin(uint32_t pin) {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = pins_.find(pin);
    if (it == pins_.end()) {
        return false;
    }
    
    auto& pin_info = it->second;
    
    // 停止中断回调
    if (pin_info->callback_active.load()) {
        clear_interrupt_callback(pin);
    }
    
    // 关闭文件描述符
    close_pin_fds(*pin_info);
    
    // 取消导出
    if (pin_info->exported) {
        unexport_pin(pin);
    }
    
    pins_.erase(it);
    return true;
}

bool LinuxSysfsGPIODriver::read_pin(uint32_t pin, bool& value) {
    auto start_time = get_current_time_ns();
    bool success = false;
    
    {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        
        auto it = pins_.find(pin);
        if (it != pins_.end()) {
            auto& pin_info = it->second;
            
            if (perf_config_.enable_fast_path && pin_info->value_fd >= 0) {
                success = read_pin_fast(*pin_info, value);
            } else {
                // 回退到标准路径
                success = read_pin_fallback(pin, value);
            }
            
            if (success) {
                pin_info->status.last_value = value;
                pin_info->status.last_change_ns = start_time;
            } else {
                pin_info->status.error_count++;
            }
        }
    }
    
    auto duration = get_current_time_ns() - start_time;
    update_statistics(true, duration, success);
    
    return success;
}

bool LinuxSysfsGPIODriver::write_pin(uint32_t pin, bool value) {
    auto start_time = get_current_time_ns();
    bool success = false;
    
    {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        
        auto it = pins_.find(pin);
        if (it != pins_.end()) {
            auto& pin_info = it->second;
            
            if (pin_info->config.direction != GPIODirection::OUTPUT) {
                success = false;
            } else if (perf_config_.enable_fast_path && pin_info->value_fd >= 0) {
                success = write_pin_fast(*pin_info, value);
            } else {
                // 回退到标准路径
                success = write_pin_fallback(pin, value);
            }
            
            if (success) {
                pin_info->status.last_value = value;
                pin_info->status.last_change_ns = start_time;
            } else {
                pin_info->status.error_count++;
            }
        }
    }
    
    auto duration = get_current_time_ns() - start_time;
    update_statistics(false, duration, success);
    
    return success;
}

bool LinuxSysfsGPIODriver::read_batch(const std::vector<uint32_t>& pins, 
                                     std::vector<bool>& values) {
    if (pins.empty()) {
        return true;
    }
    
    values.resize(pins.size());
    
    if (perf_config_.enable_fast_path && pins.size() >= perf_config_.batch_size_threshold) {
        return read_batch_optimized(pins, values);
    }
    
    // 标准批量读取
    bool all_success = true;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (!read_pin(pins[i], values[i])) {
            all_success = false;
        }
    }
    
    return all_success;
}

bool LinuxSysfsGPIODriver::write_batch(const std::vector<uint32_t>& pins, 
                                      const std::vector<bool>& values) {
    if (pins.size() != values.size()) {
        return false;
    }
    
    if (pins.empty()) {
        return true;
    }
    
    if (perf_config_.enable_fast_path && pins.size() >= perf_config_.batch_size_threshold) {
        return write_batch_optimized(pins, values);
    }
    
    // 标准批量写入
    bool all_success = true;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (!write_pin(pins[i], values[i])) {
            all_success = false;
        }
    }
    
    return all_success;
}

bool LinuxSysfsGPIODriver::get_pin_status(uint32_t pin, GPIOStatus& status) {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = pins_.find(pin);
    if (it == pins_.end()) {
        return false;
    }
    
    status = it->second->status;
    return true;
}

bool LinuxSysfsGPIODriver::set_interrupt_callback(uint32_t pin, GPIOInterruptCallback callback) {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = pins_.find(pin);
    if (it == pins_.end()) {
        return false;
    }
    
    auto& pin_info = it->second;
    
    if (pin_info->config.direction != GPIODirection::INPUT ||
        pin_info->config.trigger_mode == GPIOTriggerMode::NONE) {
        return false;
    }
    
    pin_info->callback = callback;
    pin_info->callback_active.store(true);
    
    // 添加到epoll
    if (epoll_fd_ >= 0 && pin_info->value_fd >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLPRI | EPOLLET;  // 边沿触发
        ev.data.u32 = pin;
        
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, pin_info->value_fd, &ev) == 0) {
            return true;
        }
    }
    
    return false;
#else
    return false;
#endif
}

bool LinuxSysfsGPIODriver::clear_interrupt_callback(uint32_t pin) {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = pins_.find(pin);
    if (it == pins_.end()) {
        return false;
    }
    
    auto& pin_info = it->second;
    pin_info->callback_active.store(false);
    pin_info->callback = nullptr;
    
    // 从epoll中移除
    if (epoll_fd_ >= 0 && pin_info->value_fd >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, pin_info->value_fd, nullptr);
    }
    
    return true;
#else
    return false;
#endif
}

std::string LinuxSysfsGPIODriver::get_driver_name() const {
    return "Linux Sysfs GPIO Driver v1.0";
}

std::vector<uint32_t> LinuxSysfsGPIODriver::get_supported_pins() const {
    // 返回通用GPIO引脚范围(实际可用引脚取决于硬件)
    std::vector<uint32_t> pins;
    for (uint32_t i = 0; i < 512; ++i) {  // BCM2835有54个GPIO，但留出扩展空间
        pins.push_back(i);
    }
    return pins;
}

void LinuxSysfsGPIODriver::cleanup() {
    stop_interrupt_thread();
    
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    for (auto& pair : pins_) {
        auto& pin_info = pair.second;
        
        // 停止回调
        if (pin_info->callback_active.load()) {
            pin_info->callback_active.store(false);
        }
        
        // 关闭文件描述符
        close_pin_fds(*pin_info);
        
        // 取消导出
        if (pin_info->exported) {
            unexport_pin(pair.first);
        }
    }
    
    pins_.clear();
    
#ifdef __linux__
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
#endif
}

LinuxSysfsGPIODriver::PerformanceStatistics LinuxSysfsGPIODriver::get_performance_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    PerformanceStatistics stats;
    stats.read_count = statistics_.read_count;
    stats.write_count = statistics_.write_count;
    stats.error_count = statistics_.error_count;
    stats.interrupt_count = statistics_.interrupt_count;
    
    stats.avg_read_time_ns = (statistics_.read_count > 0) ? 
        static_cast<double>(statistics_.total_read_time_ns) / statistics_.read_count : 0.0;
    
    stats.avg_write_time_ns = (statistics_.write_count > 0) ?
        static_cast<double>(statistics_.total_write_time_ns) / statistics_.write_count : 0.0;
    
    stats.uptime_ns = get_current_time_ns(); // 简化实现
    
    return stats;
}

void LinuxSysfsGPIODriver::reset_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    memset(&statistics_, 0, sizeof(statistics_));
}

// Private implementation methods
bool LinuxSysfsGPIODriver::export_pin(uint32_t pin) {
#ifdef __linux__
    std::ofstream export_file("/sys/class/gpio/export");
    if (!export_file.is_open()) {
        return false;
    }
    
    export_file << pin;
    export_file.close();
    
    // 等待引脚目录创建
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::string pin_dir = gpio_path(pin);
    struct stat st;
    return stat(pin_dir.c_str(), &st) == 0;
#else
    return false;
#endif
}

bool LinuxSysfsGPIODriver::unexport_pin(uint32_t pin) {
#ifdef __linux__
    std::ofstream unexport_file("/sys/class/gpio/unexport");
    if (!unexport_file.is_open()) {
        return false;
    }
    
    unexport_file << pin;
    return true;
#else
    return false;
#endif
}

bool LinuxSysfsGPIODriver::set_pin_direction(uint32_t pin, GPIODirection direction) {
#ifdef __linux__
    std::string direction_path = gpio_path(pin, "direction");
    std::ofstream direction_file(direction_path);
    if (!direction_file.is_open()) {
        return false;
    }
    
    direction_file << (direction == GPIODirection::INPUT ? "in" : "out");
    return direction_file.good();
#else
    return false;
#endif
}

bool LinuxSysfsGPIODriver::set_pin_edge(uint32_t pin, GPIOTriggerMode mode) {
#ifdef __linux__
    std::string edge_path = gpio_path(pin, "edge");
    std::ofstream edge_file(edge_path);
    if (!edge_file.is_open()) {
        return false;
    }
    
    switch (mode) {
        case GPIOTriggerMode::RISING:
            edge_file << "rising";
            break;
        case GPIOTriggerMode::FALLING:
            edge_file << "falling";
            break;
        case GPIOTriggerMode::BOTH:
            edge_file << "both";
            break;
        default:
            edge_file << "none";
            break;
    }
    
    return edge_file.good();
#else
    return false;
#endif
}

int LinuxSysfsGPIODriver::open_pin_value_fd(uint32_t pin) {
#ifdef __linux__
    std::string value_path = gpio_path(pin, "value");
    return open(value_path.c_str(), O_RDWR);
#else
    return -1;
#endif
}

int LinuxSysfsGPIODriver::open_pin_direction_fd(uint32_t pin) {
#ifdef __linux__
    std::string direction_path = gpio_path(pin, "direction");
    return open(direction_path.c_str(), O_WRONLY);
#else
    return -1;
#endif
}

void LinuxSysfsGPIODriver::close_pin_fds(PinInfo& pin_info) {
#ifdef __linux__
    if (pin_info.value_fd >= 0) {
        close(pin_info.value_fd);
        pin_info.value_fd = -1;
    }
    if (pin_info.direction_fd >= 0) {
        close(pin_info.direction_fd);
        pin_info.direction_fd = -1;
    }
#endif
}

bool LinuxSysfsGPIODriver::read_pin_fast(PinInfo& pin_info, bool& value) {
#ifdef __linux__
    if (pin_info.value_fd < 0) {
        return false;
    }
    
    char buffer[8];
    lseek(pin_info.value_fd, 0, SEEK_SET);
    ssize_t bytes_read = read(pin_info.value_fd, buffer, sizeof(buffer));
    
    if (bytes_read <= 0) {
        return false;
    }
    
    bool raw_value = (buffer[0] == '1');
    value = pin_info.config.active_low ? !raw_value : raw_value;
    
    return true;
#else
    return false;
#endif
}

bool LinuxSysfsGPIODriver::write_pin_fast(PinInfo& pin_info, bool value) {
#ifdef __linux__
    if (pin_info.value_fd < 0) {
        return false;
    }
    
    bool raw_value = pin_info.config.active_low ? !value : value;
    const char* data = raw_value ? "1" : "0";
    
    lseek(pin_info.value_fd, 0, SEEK_SET);
    ssize_t bytes_written = write(pin_info.value_fd, data, 1);
    
    return bytes_written == 1;
#else
    return false;
#endif
}

uint64_t LinuxSysfsGPIODriver::get_current_time_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(ns);
}

std::string LinuxSysfsGPIODriver::gpio_path(uint32_t pin, const std::string& attribute) {
    std::ostringstream oss;
    oss << "/sys/class/gpio/gpio" << pin;
    if (!attribute.empty()) {
        oss << "/" << attribute;
    }
    return oss.str();
}

void LinuxSysfsGPIODriver::update_statistics(bool is_read, uint64_t duration_ns, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (is_read) {
        statistics_.read_count++;
        statistics_.total_read_time_ns += duration_ns;
    } else {
        statistics_.write_count++;
        statistics_.total_write_time_ns += duration_ns;
    }
    
    if (!success) {
        statistics_.error_count++;
    }
}

// =============================================================================
// VirtualGPIODriver Implementation (for testing)
// =============================================================================

VirtualGPIODriver::VirtualGPIODriver(const SimulationConfig& config) 
    : simulate_delays_(config.simulate_delays),
      read_delay_us_(config.read_delay_us),
      write_delay_us_(config.write_delay_us),
      error_rate_(config.error_rate) {
}

bool VirtualGPIODriver::configure_pin(const GPIOConfig& config) {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto& pin = virtual_pins_[config.pin_number];
    pin.config = config;
    pin.status.is_configured = true;
    pin.status.last_change_ns = get_current_time_ns();
    
    return true;
}

bool VirtualGPIODriver::read_pin(uint32_t pin, bool& value) {
    if (simulate_delays_) {
        simulate_delay(read_delay_us_);
    }
    
    if (simulate_error()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = virtual_pins_.find(pin);
    if (it == virtual_pins_.end() || !it->second.status.is_configured) {
        return false;
    }
    
    value = it->second.value;
    it->second.status.last_value = value;
    it->second.status.last_change_ns = get_current_time_ns();
    
    return true;
}

bool VirtualGPIODriver::write_pin(uint32_t pin, bool value) {
    if (simulate_delays_) {
        simulate_delay(write_delay_us_);
    }
    
    if (simulate_error()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = virtual_pins_.find(pin);
    if (it == virtual_pins_.end() || !it->second.status.is_configured) {
        return false;
    }
    
    if (it->second.config.direction != GPIODirection::OUTPUT) {
        return false;
    }
    
    bool old_value = it->second.value;
    it->second.value = value;
    it->second.status.last_value = value;
    it->second.status.last_change_ns = get_current_time_ns();
    
    if (old_value != value) {
        it->second.status.change_count++;
    }
    
    return true;
}

std::string VirtualGPIODriver::get_driver_name() const {
    return "Virtual GPIO Driver v1.0";
}

// =============================================================================
// GPIODriverFactory Implementation
// =============================================================================

std::unique_ptr<GPIODriver> GPIODriverFactory::create_driver(DriverType type) {
    switch (type) {
        case DriverType::LINUX_SYSFS:
            if (is_linux_sysfs_available()) {
                return std::make_unique<LinuxSysfsGPIODriver>();
            }
            break;
            
        case DriverType::VIRTUAL:
            return std::make_unique<VirtualGPIODriver>();
            
        case DriverType::AUTO_DETECT:
            return create_best_driver();
    }
    
    return nullptr;
}

std::unique_ptr<GPIODriver> GPIODriverFactory::create_best_driver() {
    if (is_linux_sysfs_available()) {
        return std::make_unique<LinuxSysfsGPIODriver>();
    }
    
    // 回退到虚拟驱动
    return std::make_unique<VirtualGPIODriver>();
}

bool GPIODriverFactory::detect_gpio_capability() {
    return is_linux_sysfs_available();
}

bool GPIODriverFactory::is_linux_sysfs_available() {
#ifdef __linux__
    struct stat st;
    return stat("/sys/class/gpio", &st) == 0 && S_ISDIR(st.st_mode);
#else
    return false;
#endif
}

} // namespace io
} // namespace plc_runtime