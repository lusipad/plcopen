#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <cstdint>

namespace Uranus {

// I/O data types
enum class IOType {
    DIGITAL_INPUT,
    DIGITAL_OUTPUT,
    ANALOG_INPUT,
    ANALOG_OUTPUT
};

enum class IODataType {
    BOOL,
    INT16,
    UINT16,
    INT32,
    UINT32,
    FLOAT,
    DOUBLE
};

enum class IOStatus {
    OK,
    ERROR,
    DISCONNECTED,
    TIMEOUT,
    OVERRANGE,
    UNDERRANGE
};

enum class IOPhase {
    INPUT_READ,
    OUTPUT_WRITE,
    DIAGNOSTICS
};

// Process Image Point
struct IOPoint {
    uint32_t address;          // I/O address
    IOType type;               // I/O type (DI/DO/AI/AO)
    IODataType data_type;      // Data type
    union {
        bool digital_value;
        int16_t analog_value;
        float real_value;
    } value;
    uint64_t timestamp_ns;     // Timestamp
    IOStatus status;           // I/O status
    
    IOPoint() : address(0), type(IOType::DIGITAL_INPUT), 
                data_type(IODataType::BOOL), timestamp_ns(0), 
                status(IOStatus::OK) {
        value.digital_value = false;
    }
};

// Process Image with double buffering
struct ProcessImage {
    static constexpr size_t MAX_IO_POINTS = 1000;
    IOPoint input_points[MAX_IO_POINTS];
    IOPoint output_points[MAX_IO_POINTS];
    
    std::atomic<uint32_t> input_version;   // Input version number
    std::atomic<uint32_t> output_version;  // Output version number
    std::atomic<uint64_t> last_update_ns;  // Last update time
    
    ProcessImage() : input_version(0), output_version(0), last_update_ns(0) {}
};

// Process Image Manager with double buffering
class ProcessImageManager {
private:
    ProcessImage images_[2];  // Double buffer
    std::atomic<int> active_buffer_;
    std::atomic<bool> swap_pending_;
    mutable std::mutex swap_mutex_;
    
public:
    ProcessImageManager() : active_buffer_(0), swap_pending_(false) {}
    
    ProcessImage* get_read_buffer() {
        return &images_[active_buffer_.load()];
    }
    
    ProcessImage* get_write_buffer() {
        return &images_[1 - active_buffer_.load()];
    }
    
    void swap_buffers() {
        std::lock_guard<std::mutex> lock(swap_mutex_);
        if (!swap_pending_.load()) {
            swap_pending_.store(true);
            active_buffer_.store(1 - active_buffer_.load());
            swap_pending_.store(false);
        }
    }
    
    bool is_swap_safe() const {
        return !swap_pending_.load();
    }
};

// GPIO Configuration
enum class GPIODirection {
    INPUT,
    OUTPUT
};

enum class GPIOPull {
    NONE,
    UP,
    DOWN
};

struct GPIOConfig {
    uint32_t pin_number;
    GPIODirection direction;   // INPUT/OUTPUT
    GPIOPull pull_mode;       // NONE/UP/DOWN
    bool active_low;
    uint32_t debounce_ms;
    
    GPIOConfig() : pin_number(0), direction(GPIODirection::INPUT),
                   pull_mode(GPIOPull::NONE), active_low(false),
                   debounce_ms(0) {}
};

// GPIO Driver Interface
class GPIODriver {
public:
    virtual ~GPIODriver() = default;
    
    virtual bool configure_pin(const GPIOConfig& config) = 0;
    virtual bool read_pin(uint32_t pin, bool& value) = 0;
    virtual bool write_pin(uint32_t pin, bool value) = 0;
    virtual bool read_batch(const std::vector<uint32_t>& pins, 
                           std::vector<bool>& values) = 0;
    virtual bool write_batch(const std::vector<uint32_t>& pins, 
                            const std::vector<bool>& values) = 0;
    virtual bool is_pin_configured(uint32_t pin) const = 0;
    virtual void cleanup() = 0;
};

// Linux Sysfs GPIO Implementation
class LinuxSysfsGPIO : public GPIODriver {
private:
    struct PinInfo {
        int fd;
        GPIOConfig config;
        uint64_t last_change_ns;
        bool exported;
        
        PinInfo() : fd(-1), last_change_ns(0), exported(false) {}
    };
    
    std::unordered_map<uint32_t, PinInfo> configured_pins_;
    mutable std::mutex pins_mutex_;
    
    bool export_pin(uint32_t pin);
    bool unexport_pin(uint32_t pin);
    bool set_direction(uint32_t pin, GPIODirection direction);
    bool set_edge(uint32_t pin, const std::string& edge);
    
public:
    LinuxSysfsGPIO() = default;
    ~LinuxSysfsGPIO() override;
    
    bool configure_pin(const GPIOConfig& config) override;
    bool read_pin(uint32_t pin, bool& value) override;
    bool write_pin(uint32_t pin, bool value) override;
    bool read_batch(const std::vector<uint32_t>& pins, 
                   std::vector<bool>& values) override;
    bool write_batch(const std::vector<uint32_t>& pins, 
                    const std::vector<bool>& values) override;
    bool is_pin_configured(uint32_t pin) const override;
    void cleanup() override;
};

// I/O Statistics
struct IOStatistics {
    uint64_t input_scan_count;
    uint64_t output_scan_count;
    uint64_t error_count;
    uint64_t total_scan_time_ns;
    uint64_t max_scan_time_ns;
    uint64_t min_scan_time_ns;
    double avg_scan_time_ns;
    
    IOStatistics() : input_scan_count(0), output_scan_count(0),
                     error_count(0), total_scan_time_ns(0),
                     max_scan_time_ns(0), min_scan_time_ns(UINT64_MAX),
                     avg_scan_time_ns(0.0) {}
};

// Scheduler I/O Interface
class SchedulerIOInterface {
public:
    virtual ~SchedulerIOInterface() = default;
    
    virtual void notify_io_phase_start(IOPhase phase) = 0;
    virtual void notify_io_phase_complete(IOPhase phase, uint64_t duration_ns) = 0;
    virtual bool is_io_budget_exceeded() const = 0;
    virtual void request_emergency_stop() = 0;
};

// Main I/O System
class IOSystem {
private:
    std::unique_ptr<ProcessImageManager> process_image_manager_;
    std::unique_ptr<GPIODriver> gpio_driver_;
    std::unique_ptr<SchedulerIOInterface> scheduler_interface_;
    
    IOStatistics statistics_;
    mutable std::mutex stats_mutex_;
    
    std::atomic<bool> running_;
    std::atomic<uint32_t> scan_interval_us_;
    std::thread io_thread_;
    
    // I/O point mappings
    std::unordered_map<uint32_t, size_t> input_mapping_;   // address -> index
    std::unordered_map<uint32_t, size_t> output_mapping_;  // address -> index
    mutable std::mutex mapping_mutex_;
    
    void io_scan_loop();
    void scan_inputs();
    void scan_outputs();
    void update_statistics(uint64_t scan_time_ns);
    uint64_t get_current_time_ns() const;
    
public:
    IOSystem();
    ~IOSystem();
    
    // Initialization
    bool initialize(std::unique_ptr<GPIODriver> gpio_driver,
                   std::unique_ptr<SchedulerIOInterface> scheduler_interface);
    void shutdown();
    
    // I/O Point Management
    bool add_input_point(uint32_t address, IOType type, IODataType data_type);
    bool add_output_point(uint32_t address, IOType type, IODataType data_type);
    bool remove_input_point(uint32_t address);
    bool remove_output_point(uint32_t address);
    
    // I/O Operations
    bool read_digital_input(uint32_t address, bool& value);
    bool read_analog_input(uint32_t address, double& value);
    bool write_digital_output(uint32_t address, bool value);
    bool write_analog_output(uint32_t address, double value);
    
    // Process Image Access
    ProcessImage* get_process_image_read();
    ProcessImage* get_process_image_write();
    void swap_process_images();
    
    // Scanning Control
    bool start_scanning(uint32_t interval_us = 1000); // 1ms default
    void stop_scanning();
    bool is_scanning() const { return running_.load(); }
    
    // Configuration
    void set_scan_interval(uint32_t interval_us);
    uint32_t get_scan_interval() const { return scan_interval_us_.load(); }
    
    // Statistics
    IOStatistics get_statistics() const;
    void reset_statistics();
    
    // GPIO Configuration
    bool configure_gpio_pin(const GPIOConfig& config);
};

} // namespace Uranus