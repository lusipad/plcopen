#include "io/IOSystem.h"
#include <iostream>
#include <chrono>

namespace Uranus {

// Simple Scheduler I/O Interface Implementation
class SimpleSchedulerIOInterface : public SchedulerIOInterface {
private:
    uint64_t io_budget_ns_;           // I/O time budget per cycle
    uint64_t current_phase_start_ns_; // Current phase start time
    uint64_t total_io_time_ns_;       // Total I/O time in current cycle
    bool emergency_stop_requested_;
    
    mutable std::mutex interface_mutex_;
    
    uint64_t get_current_time_ns() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
public:
    SimpleSchedulerIOInterface(uint64_t io_budget_ns = 500000) // 500μs default
        : io_budget_ns_(io_budget_ns),
          current_phase_start_ns_(0),
          total_io_time_ns_(0),
          emergency_stop_requested_(false) {
    }
    
    void notify_io_phase_start(IOPhase phase) override {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        current_phase_start_ns_ = get_current_time_ns();
        
        switch (phase) {
            case IOPhase::INPUT_READ:
                // Reset total I/O time for new cycle
                total_io_time_ns_ = 0;
                break;
            case IOPhase::OUTPUT_WRITE:
                // Continue accumulating time
                break;
            case IOPhase::DIAGNOSTICS:
                // Diagnostics phase
                break;
        }
    }
    
    void notify_io_phase_complete(IOPhase phase, uint64_t duration_ns) override {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        total_io_time_ns_ += duration_ns;
        
        // Check if we're approaching the budget limit
        if (total_io_time_ns_ > io_budget_ns_ * 0.9) { // 90% of budget
            std::cout << "Warning: I/O time approaching budget limit. "
                      << "Used: " << total_io_time_ns_ << "ns, "
                      << "Budget: " << io_budget_ns_ << "ns" << std::endl;
        }
        
        // Log phase completion
        const char* phase_name = "Unknown";
        switch (phase) {
            case IOPhase::INPUT_READ: phase_name = "INPUT_READ"; break;
            case IOPhase::OUTPUT_WRITE: phase_name = "OUTPUT_WRITE"; break;
            case IOPhase::DIAGNOSTICS: phase_name = "DIAGNOSTICS"; break;
        }
        
        if (duration_ns > 100000) { // Log if > 100μs
            std::cout << "I/O Phase " << phase_name << " completed in " 
                      << duration_ns << "ns" << std::endl;
        }
    }
    
    bool is_io_budget_exceeded() const override {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        return total_io_time_ns_ > io_budget_ns_;
    }
    
    void request_emergency_stop() override {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        emergency_stop_requested_ = true;
        std::cerr << "EMERGENCY STOP requested by I/O system!" << std::endl;
    }
    
    // Additional methods for configuration
    void set_io_budget(uint64_t budget_ns) {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        io_budget_ns_ = budget_ns;
    }
    
    uint64_t get_io_budget() const {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        return io_budget_ns_;
    }
    
    uint64_t get_current_io_time() const {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        return total_io_time_ns_;
    }
    
    bool is_emergency_stop_requested() const {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        return emergency_stop_requested_;
    }
    
    void clear_emergency_stop() {
        std::lock_guard<std::mutex> lock(interface_mutex_);
        emergency_stop_requested_ = false;
    }
};

// Factory function to create scheduler interface
std::unique_ptr<SchedulerIOInterface> create_simple_scheduler_interface(uint64_t io_budget_ns) {
    return std::make_unique<SimpleSchedulerIOInterface>(io_budget_ns);
}

// Mock GPIO Driver for testing (when hardware is not available)
class MockGPIODriver : public GPIODriver {
private:
    struct MockPin {
        GPIOConfig config;
        bool value;
        bool configured;
        
        MockPin() : value(false), configured(false) {}
    };
    
    std::unordered_map<uint32_t, MockPin> pins_;
    mutable std::mutex pins_mutex_;
    
public:
    MockGPIODriver() = default;
    ~MockGPIODriver() override = default;
    
    bool configure_pin(const GPIOConfig& config) override {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        
        MockPin& pin = pins_[config.pin_number];
        pin.config = config;
        pin.configured = true;
        pin.value = false; // Initialize to false
        
        std::cout << "Mock GPIO: Configured pin " << config.pin_number 
                  << " as " << (config.direction == GPIODirection::INPUT ? "INPUT" : "OUTPUT")
                  << std::endl;
        
        return true;
    }
    
    bool read_pin(uint32_t pin, bool& value) override {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        
        auto it = pins_.find(pin);
        if (it == pins_.end() || !it->second.configured) {
            return false;
        }
        
        if (it->second.config.direction != GPIODirection::INPUT) {
            return false;
        }
        
        // Simulate some input changes for testing
        static uint64_t counter = 0;
        counter++;
        
        // Toggle every 1000 reads for pin 0, every 500 for pin 1, etc.
        uint32_t toggle_period = 1000 / (pin + 1);
        bool simulated_value = (counter / toggle_period) % 2 == 0;
        
        it->second.value = simulated_value;
        value = it->second.config.active_low ? !it->second.value : it->second.value;
        
        return true;
    }
    
    bool write_pin(uint32_t pin, bool value) override {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        
        auto it = pins_.find(pin);
        if (it == pins_.end() || !it->second.configured) {
            return false;
        }
        
        if (it->second.config.direction != GPIODirection::OUTPUT) {
            return false;
        }
        
        bool raw_value = it->second.config.active_low ? !value : value;
        it->second.value = raw_value;
        
        static uint64_t write_counter = 0;
        write_counter++;
        
        // Log every 100th write to avoid spam
        if (write_counter % 100 == 0) {
            std::cout << "Mock GPIO: Pin " << pin << " set to " 
                      << (value ? "HIGH" : "LOW") << std::endl;
        }
        
        return true;
    }
    
    bool read_batch(const std::vector<uint32_t>& pins, 
                   std::vector<bool>& values) override {
        values.resize(pins.size());
        bool success = true;
        
        for (size_t i = 0; i < pins.size(); ++i) {
            if (!read_pin(pins[i], values[i])) {
                success = false;
            }
        }
        
        return success;
    }
    
    bool write_batch(const std::vector<uint32_t>& pins, 
                    const std::vector<bool>& values) override {
        if (pins.size() != values.size()) {
            return false;
        }
        
        bool success = true;
        for (size_t i = 0; i < pins.size(); ++i) {
            if (!write_pin(pins[i], values[i])) {
                success = false;
            }
        }
        
        return success;
    }
    
    bool is_pin_configured(uint32_t pin) const override {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        auto it = pins_.find(pin);
        return it != pins_.end() && it->second.configured;
    }
    
    void cleanup() override {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        pins_.clear();
        std::cout << "Mock GPIO: Cleanup completed" << std::endl;
    }
    
    // Additional methods for testing
    void set_pin_value(uint32_t pin, bool value) {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        auto it = pins_.find(pin);
        if (it != pins_.end() && it->second.configured) {
            it->second.value = value;
        }
    }
    
    bool get_pin_value(uint32_t pin) const {
        std::lock_guard<std::mutex> lock(pins_mutex_);
        auto it = pins_.find(pin);
        if (it != pins_.end() && it->second.configured) {
            return it->second.value;
        }
        return false;
    }
};

// Factory function to create mock GPIO driver
std::unique_ptr<GPIODriver> create_mock_gpio_driver() {
    return std::make_unique<MockGPIODriver>();
}

// Factory function to create Linux sysfs GPIO driver
std::unique_ptr<GPIODriver> create_linux_gpio_driver() {
    return std::make_unique<LinuxSysfsGPIO>();
}

} // namespace Uranus