#include "io/IOSystem.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#endif

namespace Uranus {

// Linux Sysfs GPIO Implementation
LinuxSysfsGPIO::~LinuxSysfsGPIO() {
    cleanup();
}

bool LinuxSysfsGPIO::export_pin(uint32_t pin) {
#ifdef _WIN32
    return false;
#else
    std::ofstream export_file("/sys/class/gpio/export");
    if (!export_file.is_open()) {
        return false;
    }
    export_file << pin;
    export_file.close();
    
    // Wait for the pin directory to be created
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    std::string pin_dir = "/sys/class/gpio/gpio" + std::to_string(pin);
    struct stat st;
    return stat(pin_dir.c_str(), &st) == 0;
#endif
}

bool LinuxSysfsGPIO::unexport_pin(uint32_t pin) {
#ifdef _WIN32
    return false;
#else
    std::ofstream unexport_file("/sys/class/gpio/unexport");
    if (!unexport_file.is_open()) {
        return false;
    }
    unexport_file << pin;
    return true;
#endif
}

bool LinuxSysfsGPIO::set_direction(uint32_t pin, GPIODirection direction) {
#ifdef _WIN32
    return false;
#else
    std::string direction_path = "/sys/class/gpio/gpio" + std::to_string(pin) + "/direction";
    std::ofstream direction_file(direction_path);
    if (!direction_file.is_open()) {
        return false;
    }
    
    direction_file << (direction == GPIODirection::INPUT ? "in" : "out");
    return true;
#endif
}

bool LinuxSysfsGPIO::set_edge(uint32_t pin, const std::string& edge) {
#ifdef _WIN32
    return false;
#else
    std::string edge_path = "/sys/class/gpio/gpio" + std::to_string(pin) + "/edge";
    std::ofstream edge_file(edge_path);
    if (!edge_file.is_open()) {
        return false;
    }
    
    edge_file << edge;
    return true;
#endif
}

bool LinuxSysfsGPIO::configure_pin(const GPIOConfig& config) {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    // Check if pin is already configured
    auto it = configured_pins_.find(config.pin_number);
    if (it != configured_pins_.end()) {
        // Close existing file descriptor
#ifndef _WIN32
        if (it->second.fd >= 0) {
            close(it->second.fd);
        }
#endif
        // Unexport the pin
        unexport_pin(config.pin_number);
    }
    
    // Export the pin
    if (!export_pin(config.pin_number)) {
        return false;
    }
    
    // Set direction
    if (!set_direction(config.pin_number, config.direction)) {
        unexport_pin(config.pin_number);
        return false;
    }
    
    // Set edge detection for inputs
    if (config.direction == GPIODirection::INPUT) {
        if (!set_edge(config.pin_number, "both")) {
            unexport_pin(config.pin_number);
            return false;
        }
    }
    
    // Open value file
#ifdef _WIN32
    int fd = -1; // Not supported on Windows
#else
    std::string value_path = "/sys/class/gpio/gpio" + std::to_string(config.pin_number) + "/value";
    int fd = open(value_path.c_str(), O_RDWR);
    if (fd < 0) {
        unexport_pin(config.pin_number);
        return false;
    }
#endif
    
    // Store pin configuration
    PinInfo& pin_info = configured_pins_[config.pin_number];
    pin_info.fd = fd;
    pin_info.config = config;
    pin_info.exported = true;
    pin_info.last_change_ns = get_current_time_ns();
    
    return true;
}

bool LinuxSysfsGPIO::read_pin(uint32_t pin, bool& value) {
#ifdef _WIN32
    return false;
#else
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = configured_pins_.find(pin);
    if (it == configured_pins_.end() || it->second.fd < 0) {
        return false;
    }
    
    char buffer[2];
    lseek(it->second.fd, 0, SEEK_SET);
    ssize_t bytes_read = read(it->second.fd, buffer, sizeof(buffer));
    
    if (bytes_read <= 0) {
        return false;
    }
    
    bool raw_value = (buffer[0] == '1');
    value = it->second.config.active_low ? !raw_value : raw_value;
    
    return true;
#endif
}

bool LinuxSysfsGPIO::write_pin(uint32_t pin, bool value) {
#ifdef _WIN32
    return false;
#else
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    auto it = configured_pins_.find(pin);
    if (it == configured_pins_.end() || it->second.fd < 0) {
        return false;
    }
    
    if (it->second.config.direction != GPIODirection::OUTPUT) {
        return false;
    }
    
    bool raw_value = it->second.config.active_low ? !value : value;
    const char* data = raw_value ? "1" : "0";
    
    lseek(it->second.fd, 0, SEEK_SET);
    ssize_t bytes_written = write(it->second.fd, data, 1);
    
    return bytes_written == 1;
#endif
}

bool LinuxSysfsGPIO::read_batch(const std::vector<uint32_t>& pins, 
                               std::vector<bool>& values) {
    values.resize(pins.size());
    bool success = true;
    
    for (size_t i = 0; i < pins.size(); ++i) {
        if (!read_pin(pins[i], values[i])) {
            success = false;
        }
    }
    
    return success;
}

bool LinuxSysfsGPIO::write_batch(const std::vector<uint32_t>& pins, 
                                const std::vector<bool>& values) {
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

bool LinuxSysfsGPIO::is_pin_configured(uint32_t pin) const {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    return configured_pins_.find(pin) != configured_pins_.end();
}

void LinuxSysfsGPIO::cleanup() {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    
    for (auto& pair : configured_pins_) {
#ifndef _WIN32
        if (pair.second.fd >= 0) {
            close(pair.second.fd);
        }
#endif
        if (pair.second.exported) {
            unexport_pin(pair.first);
        }
    }
    
    configured_pins_.clear();
}

uint64_t get_current_time_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

// IOSystem Implementation
IOSystem::IOSystem() 
    : process_image_manager_(std::make_unique<ProcessImageManager>()),
      running_(false),
      scan_interval_us_(1000) {
}

IOSystem::~IOSystem() {
    shutdown();
}

bool IOSystem::initialize(std::unique_ptr<GPIODriver> gpio_driver,
                         std::unique_ptr<SchedulerIOInterface> scheduler_interface) {
    if (!gpio_driver || !scheduler_interface) {
        return false;
    }
    
    gpio_driver_ = std::move(gpio_driver);
    scheduler_interface_ = std::move(scheduler_interface);
    
    return true;
}

void IOSystem::shutdown() {
    stop_scanning();
    
    if (gpio_driver_) {
        gpio_driver_->cleanup();
    }
}

bool IOSystem::add_input_point(uint32_t address, IOType type, IODataType data_type) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    if (input_mapping_.find(address) != input_mapping_.end()) {
        return false; // Address already exists
    }
    
    size_t index = input_mapping_.size();
    if (index >= ProcessImage::MAX_IO_POINTS) {
        return false; // Too many points
    }
    
    input_mapping_[address] = index;
    
    // Initialize both buffers
    for (int i = 0; i < 2; ++i) {
        IOPoint& point = process_image_manager_->images_[i].input_points[index];
        point.address = address;
        point.type = type;
        point.data_type = data_type;
        point.status = IOStatus::OK;
        point.timestamp_ns = get_current_time_ns();
    }
    
    return true;
}

bool IOSystem::add_output_point(uint32_t address, IOType type, IODataType data_type) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    if (output_mapping_.find(address) != output_mapping_.end()) {
        return false; // Address already exists
    }
    
    size_t index = output_mapping_.size();
    if (index >= ProcessImage::MAX_IO_POINTS) {
        return false; // Too many points
    }
    
    output_mapping_[address] = index;
    
    // Initialize both buffers
    for (int i = 0; i < 2; ++i) {
        IOPoint& point = process_image_manager_->images_[i].output_points[index];
        point.address = address;
        point.type = type;
        point.data_type = data_type;
        point.status = IOStatus::OK;
        point.timestamp_ns = get_current_time_ns();
    }
    
    return true;
}

bool IOSystem::remove_input_point(uint32_t address) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    return input_mapping_.erase(address) > 0;
}

bool IOSystem::remove_output_point(uint32_t address) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    return output_mapping_.erase(address) > 0;
}

bool IOSystem::read_digital_input(uint32_t address, bool& value) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    auto it = input_mapping_.find(address);
    if (it == input_mapping_.end()) {
        return false;
    }
    
    ProcessImage* image = process_image_manager_->get_read_buffer();
    const IOPoint& point = image->input_points[it->second];
    
    if (point.type != IOType::DIGITAL_INPUT || point.status != IOStatus::OK) {
        return false;
    }
    
    value = point.value.digital_value;
    return true;
}

bool IOSystem::read_analog_input(uint32_t address, double& value) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    auto it = input_mapping_.find(address);
    if (it == input_mapping_.end()) {
        return false;
    }
    
    ProcessImage* image = process_image_manager_->get_read_buffer();
    const IOPoint& point = image->input_points[it->second];
    
    if (point.type != IOType::ANALOG_INPUT || point.status != IOStatus::OK) {
        return false;
    }
    
    switch (point.data_type) {
        case IODataType::INT16:
            value = static_cast<double>(point.value.analog_value);
            break;
        case IODataType::FLOAT:
        case IODataType::DOUBLE:
            value = static_cast<double>(point.value.real_value);
            break;
        default:
            return false;
    }
    
    return true;
}

bool IOSystem::write_digital_output(uint32_t address, bool value) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    auto it = output_mapping_.find(address);
    if (it == output_mapping_.end()) {
        return false;
    }
    
    ProcessImage* image = process_image_manager_->get_write_buffer();
    IOPoint& point = image->output_points[it->second];
    
    if (point.type != IOType::DIGITAL_OUTPUT) {
        return false;
    }
    
    point.value.digital_value = value;
    point.timestamp_ns = get_current_time_ns();
    point.status = IOStatus::OK;
    
    return true;
}

bool IOSystem::write_analog_output(uint32_t address, double value) {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    
    auto it = output_mapping_.find(address);
    if (it == output_mapping_.end()) {
        return false;
    }
    
    ProcessImage* image = process_image_manager_->get_write_buffer();
    IOPoint& point = image->output_points[it->second];
    
    if (point.type != IOType::ANALOG_OUTPUT) {
        return false;
    }
    
    switch (point.data_type) {
        case IODataType::INT16:
            point.value.analog_value = static_cast<int16_t>(value);
            break;
        case IODataType::FLOAT:
        case IODataType::DOUBLE:
            point.value.real_value = static_cast<float>(value);
            break;
        default:
            return false;
    }
    
    point.timestamp_ns = get_current_time_ns();
    point.status = IOStatus::OK;
    
    return true;
}

ProcessImage* IOSystem::get_process_image_read() {
    return process_image_manager_->get_read_buffer();
}

ProcessImage* IOSystem::get_process_image_write() {
    return process_image_manager_->get_write_buffer();
}

void IOSystem::swap_process_images() {
    process_image_manager_->swap_buffers();
}

bool IOSystem::start_scanning(uint32_t interval_us) {
    if (running_.load()) {
        return false;
    }
    
    scan_interval_us_.store(interval_us);
    running_.store(true);
    
    io_thread_ = std::thread(&IOSystem::io_scan_loop, this);
    return true;
}

void IOSystem::stop_scanning() {
    if (running_.load()) {
        running_.store(false);
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }
}

void IOSystem::set_scan_interval(uint32_t interval_us) {
    scan_interval_us_.store(interval_us);
}

IOStatistics IOSystem::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

void IOSystem::reset_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_ = IOStatistics();
}

bool IOSystem::configure_gpio_pin(const GPIOConfig& config) {
    if (!gpio_driver_) {
        return false;
    }
    return gpio_driver_->configure_pin(config);
}

void IOSystem::io_scan_loop() {
    auto next_scan = std::chrono::high_resolution_clock::now();
    const auto scan_interval = std::chrono::microseconds(scan_interval_us_.load());
    
    while (running_.load()) {
        auto scan_start = std::chrono::high_resolution_clock::now();
        
        // Notify scheduler of I/O phase start
        if (scheduler_interface_) {
            scheduler_interface_->notify_io_phase_start(IOPhase::INPUT_READ);
        }
        
        // Scan inputs
        scan_inputs();
        
        // Notify scheduler of input phase complete
        if (scheduler_interface_) {
            auto input_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - scan_start).count();
            scheduler_interface_->notify_io_phase_complete(IOPhase::INPUT_READ, input_duration);
        }
        
        // Swap buffers
        swap_process_images();
        
        // Notify scheduler of output phase start
        if (scheduler_interface_) {
            scheduler_interface_->notify_io_phase_start(IOPhase::OUTPUT_WRITE);
        }
        
        // Scan outputs
        scan_outputs();
        
        auto scan_end = std::chrono::high_resolution_clock::now();
        auto scan_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            scan_end - scan_start).count();
        
        // Notify scheduler of output phase complete
        if (scheduler_interface_) {
            scheduler_interface_->notify_io_phase_complete(IOPhase::OUTPUT_WRITE, 
                scan_duration - (scheduler_interface_ ? 
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    scan_end - scan_start).count() / 2 : 0));
        }
        
        // Update statistics
        update_statistics(scan_duration);
        
        // Check if we're exceeding I/O budget
        if (scheduler_interface_ && scheduler_interface_->is_io_budget_exceeded()) {
            std::cerr << "I/O budget exceeded, scan time: " << scan_duration << "ns" << std::endl;
        }
        
        // Sleep until next scan
        next_scan += scan_interval;
        std::this_thread::sleep_until(next_scan);
    }
}

void IOSystem::scan_inputs() {
    if (!gpio_driver_) {
        return;
    }
    
    ProcessImage* write_image = process_image_manager_->get_write_buffer();
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    for (const auto& pair : input_mapping_) {
        uint32_t address = pair.first;
        size_t index = pair.second;
        IOPoint& point = write_image->input_points[index];
        
        if (point.type == IOType::DIGITAL_INPUT) {
            bool value;
            if (gpio_driver_->read_pin(address, value)) {
                point.value.digital_value = value;
                point.status = IOStatus::OK;
            } else {
                point.status = IOStatus::ERROR;
            }
            point.timestamp_ns = get_current_time_ns();
        }
    }
    
    write_image->input_version.fetch_add(1);
    write_image->last_update_ns.store(get_current_time_ns());
}

void IOSystem::scan_outputs() {
    if (!gpio_driver_) {
        return;
    }
    
    ProcessImage* read_image = process_image_manager_->get_read_buffer();
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    for (const auto& pair : output_mapping_) {
        uint32_t address = pair.first;
        size_t index = pair.second;
        const IOPoint& point = read_image->output_points[index];
        
        if (point.type == IOType::DIGITAL_OUTPUT && point.status == IOStatus::OK) {
            gpio_driver_->write_pin(address, point.value.digital_value);
        }
    }
    
    read_image->output_version.fetch_add(1);
}

void IOSystem::update_statistics(uint64_t scan_time_ns) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    statistics_.input_scan_count++;
    statistics_.output_scan_count++;
    statistics_.total_scan_time_ns += scan_time_ns;
    
    if (scan_time_ns > statistics_.max_scan_time_ns) {
        statistics_.max_scan_time_ns = scan_time_ns;
    }
    
    if (scan_time_ns < statistics_.min_scan_time_ns) {
        statistics_.min_scan_time_ns = scan_time_ns;
    }
    
    statistics_.avg_scan_time_ns = static_cast<double>(statistics_.total_scan_time_ns) / 
                                   statistics_.input_scan_count;
}

uint64_t IOSystem::get_current_time_ns() const {
    return ::Uranus::get_current_time_ns();
}

} // namespace Uranus