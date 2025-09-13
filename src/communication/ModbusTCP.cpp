/**
 * @file ModbusTCP.cpp
 * @brief Modbus TCP通信协议实现（简化版本）
 * @version 1.0
 * @date 2025-09-06
 */

#include "communication/ModbusTCP.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <shared_mutex>

// Windows compatibility - define ssize_t for Windows
#ifdef _WIN32
    typedef long long ssize_t;
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace plc_runtime {
namespace communication {

// =============================================================================
// 内部工具函数
// =============================================================================

/**
 * @brief 检查Modbus功能码是否合法
 * @param function_code 功能码
 * @return true 如果功能码合法
 */
static bool is_valid_function_code(uint8_t function_code) {
    // 支持的标准Modbus功能码
    static const std::unordered_set<uint8_t> valid_codes = {
        1,  // Read Coils
        2,  // Read Discrete Inputs
        3,  // Read Holding Registers
        4,  // Read Input Registers
        5,  // Write Single Coil
        6,  // Write Single Register
        15, // Write Multiple Coils (0x0F)
        16, // Write Multiple Registers (0x10)
        // 可扩展支持其他功能码如 20-24 (Read/Write File Record)
    };
    
    // 检查是否为异常响应码（功能码 + 0x80）
    if (function_code >= 0x80) {
        uint8_t original_code = function_code & 0x7F;
        return valid_codes.count(original_code) > 0;
    }
    
    return valid_codes.count(function_code) > 0;
}

/**
 * @brief 检查异常码是否合法
 * @param exception_code 异常码
 * @return true 如果异常码合法
 */
[[maybe_unused]] static bool is_valid_exception_code(uint8_t exception_code) {
    // 标准Modbus异常码
    return exception_code >= 1 && exception_code <= 11; // 01-0B
}

/**
 * @brief 网络错误类型枚举
 */
enum class NetworkError {
    SUCCESS = 0,
    TIMEOUT = 1,        // 超时
    PEER_CLOSED = 2,    // 对端关闭连接
    NETWORK_ERROR = 3,  // 网络错误
    BUFFER_FULL = 4,    // 缓冲区已满
    INVALID_PARAM = 5   // 参数错误
};

/**
 * @brief 接收结果结构
 */
struct ReceiveResult {
    NetworkError error;
    size_t bytes_received;
    std::string error_message;
    
    bool is_success() const { return error == NetworkError::SUCCESS; }
};

/**
 * @brief 网络超时配置
 */
struct NetworkTimeouts {
    uint32_t connect_timeout_ms = 5000;     // 连接超时
    uint32_t read_timeout_ms = 5000;        // 读取超时
    uint32_t write_timeout_ms = 3000;       // 写入超时
    uint32_t total_timeout_ms = 30000;      // 总超时
    bool adaptive_timeout = true;           // 自适应超时
    double slow_link_factor = 2.0;          // 慢链路因子
};

/**
 * @brief 增强的字节接收函数，支持灵活的超时配置和错误分类
 * @param socket_fd 套接字描述符
 * @param buffer 接收缓冲区
 * @param expected_size 期望接收的字节数
 * @param timeouts 超时配置
 * @return 接收结果
 */
static ReceiveResult receive_exact_bytes_enhanced(int socket_fd, uint8_t* buffer, size_t expected_size, const NetworkTimeouts& timeouts) {
    if (!buffer || expected_size == 0) {
        return {NetworkError::INVALID_PARAM, 0, "Invalid parameters"};
    }
    
    size_t total_received = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_receive_time = start_time;
    
    // 计算实际超时值
    uint32_t effective_read_timeout = timeouts.read_timeout_ms;
    if (timeouts.adaptive_timeout) {
        // 根据数据大小调整超时
        if (expected_size > 1024) { // 大数据包
            effective_read_timeout = static_cast<uint32_t>(timeouts.read_timeout_ms * timeouts.slow_link_factor);
        }
    }
    
    while (total_received < expected_size) {
        auto now = std::chrono::steady_clock::now();
        
        // 检查总超时
        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (total_elapsed > timeouts.total_timeout_ms) {
            return {NetworkError::TIMEOUT, total_received, 
                   "Total timeout exceeded: " + std::to_string(total_elapsed) + "ms"};
        }
        
        // 检查读取超时
        auto read_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_receive_time).count();
        if (read_elapsed > effective_read_timeout) {
            return {NetworkError::TIMEOUT, total_received, 
                   "Read timeout: " + std::to_string(read_elapsed) + "ms"};
        }
        
        ssize_t received = recv(socket_fd, 
                               reinterpret_cast<char*>(buffer + total_received), 
                               expected_size - total_received, 0);
        
        if (received > 0) {
            total_received += received;
            last_receive_time = now; // 更新最后接收时间
        } else if (received == 0) {
            // 对端关闭连接
            return {NetworkError::PEER_CLOSED, total_received, "Peer closed connection"};
        } else {
            // 错误发生
#ifdef _WIN32
            int error_code = WSAGetLastError();
            if (error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS) {
                // 非阻塞套接字，继续等待
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return {NetworkError::NETWORK_ERROR, total_received, 
                   "Windows socket error: " + std::to_string(error_code)};
#else
            int error_code = errno;
            if (error_code == EAGAIN || error_code == EWOULDBLOCK || error_code == EINTR) {
                // 可恢复错误，继续等待
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return {NetworkError::NETWORK_ERROR, total_received, 
                   "Unix socket error: " + std::to_string(error_code)};
#endif
        }
    }
    
    return {NetworkError::SUCCESS, total_received, "Success"};
}

// 向后兼容的简化版本
static bool receive_exact_bytes(int socket_fd, uint8_t* buffer, size_t expected_size, int timeout_ms = 5000) {
    NetworkTimeouts timeouts;
    timeouts.read_timeout_ms = timeout_ms;
    timeouts.total_timeout_ms = timeout_ms;
    timeouts.adaptive_timeout = false;
    
    auto result = receive_exact_bytes_enhanced(socket_fd, buffer, expected_size, timeouts);
    return result.is_success();
}

// =============================================================================
// DefaultModbusDataMap Implementation
// =============================================================================

DefaultModbusDataMap::DefaultModbusDataMap(const Config& config) 
    : config_(config) {
    coils_.resize(config_.max_coils, false);
    discrete_inputs_.resize(config_.max_discrete_inputs, false);
    holding_registers_.resize(config_.max_holding_registers, 0);
    input_registers_.resize(config_.max_input_registers, 0);
}

bool DefaultModbusDataMap::read_coil(uint16_t address) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= coils_.size()) return false;
    return coils_[address];
}

bool DefaultModbusDataMap::write_coil(uint16_t address, bool value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= coils_.size()) return false;
    coils_[address] = value;
    return true;
}

bool DefaultModbusDataMap::read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + count > coils_.size()) return false;
    
    values.clear();
    values.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
        values.push_back(coils_[start_address + i]);
    }
    return true;
}

bool DefaultModbusDataMap::write_coils(uint16_t start_address, const std::vector<bool>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + values.size() > coils_.size()) return false;
    
    for (size_t i = 0; i < values.size(); i++) {
        coils_[start_address + i] = values[i];
    }
    return true;
}

bool DefaultModbusDataMap::read_discrete_input(uint16_t address) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= discrete_inputs_.size()) return false;
    return discrete_inputs_[address];
}

bool DefaultModbusDataMap::read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + count > discrete_inputs_.size()) return false;
    
    values.clear();
    values.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
        values.push_back(discrete_inputs_[start_address + i]);
    }
    return true;
}

uint16_t DefaultModbusDataMap::read_holding_register(uint16_t address) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= holding_registers_.size()) return 0;
    return holding_registers_[address];
}

bool DefaultModbusDataMap::write_holding_register(uint16_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= holding_registers_.size()) return false;
    holding_registers_[address] = value;
    return true;
}

bool DefaultModbusDataMap::read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + count > holding_registers_.size()) return false;
    
    values.clear();
    values.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
        values.push_back(holding_registers_[start_address + i]);
    }
    return true;
}

bool DefaultModbusDataMap::write_holding_registers(uint16_t start_address, const std::vector<uint16_t>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + values.size() > holding_registers_.size()) return false;
    
    for (size_t i = 0; i < values.size(); i++) {
        holding_registers_[start_address + i] = values[i];
    }
    return true;
}

uint16_t DefaultModbusDataMap::read_input_register(uint16_t address) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (address >= input_registers_.size()) return 0;
    return input_registers_[address];
}

bool DefaultModbusDataMap::read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (start_address + count > input_registers_.size()) return false;
    
    values.clear();
    values.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
        values.push_back(input_registers_[start_address + i]);
    }
    return true;
}

bool DefaultModbusDataMap::is_valid_address(ModbusDataArea area, uint16_t address) const {
    switch (area) {
        case ModbusDataArea::COILS: return address < config_.max_coils;
        case ModbusDataArea::DISCRETE_INPUTS: return address < config_.max_discrete_inputs;
        case ModbusDataArea::HOLDING_REGISTERS: return address < config_.max_holding_registers;
        case ModbusDataArea::INPUT_REGISTERS: return address < config_.max_input_registers;
        default: return false;
    }
}

// =============================================================================
// ModbusTcpClient Implementation 
// =============================================================================

ModbusTcpClient::ModbusTcpClient(const Config& config) 
    : config_(config) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

ModbusTcpClient::~ModbusTcpClient() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool ModbusTcpClient::connect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (connected_.load()) {
        return true;
    }
    
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.server_port);
    
    if (inet_pton(AF_INET, config_.server_host.c_str(), &server_addr.sin_addr) <= 0) {
#ifndef _WIN32
        close(socket_fd_);
#else
        closesocket(socket_fd_);
#endif
        return false;
    }
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
#ifndef _WIN32
        close(socket_fd_);
#else
        closesocket(socket_fd_);
#endif
        return false;
    }
    
    connected_.store(true);
    return true;
}

void ModbusTcpClient::disconnect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_fd_ >= 0) {
#ifndef _WIN32
        close(socket_fd_);
#else
        closesocket(socket_fd_);
#endif
        socket_fd_ = -1;
    }
    
    connected_.store(false);
}

bool ModbusTcpClient::read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::READ_HOLDING_REGISTERS);
    
    // 添加起始地址和数量
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((count >> 8) & 0xFF);
    request.pdu.data.push_back(count & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    if (!success) {
        return false;
    }
    
    // 解析响应数据
    if (response.pdu.function_code != request.pdu.function_code ||
        response.pdu.data.size() < 1) {
        return false;
    }
    
    uint8_t byte_count = response.pdu.data[0];
    if (response.pdu.data.size() != static_cast<size_t>(byte_count + 1) || byte_count != count * 2) {
        return false;
    }
    
    values.clear();
    values.reserve(count);
    
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = (response.pdu.data[1 + i * 2] << 8) | response.pdu.data[2 + i * 2];
        values.push_back(value);
    }
    
    return true;
}

bool ModbusTcpClient::write_single_register(uint16_t address, uint16_t value) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_REGISTER);
    
    // 添加地址和值
    request.pdu.data.push_back((address >> 8) & 0xFF);
    request.pdu.data.push_back(address & 0xFF);
    request.pdu.data.push_back((value >> 8) & 0xFF);
    request.pdu.data.push_back(value & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    return success && response.pdu.function_code == request.pdu.function_code;
}

bool ModbusTcpClient::read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::READ_COILS);
    
    // 添加起始地址和数量
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((count >> 8) & 0xFF);
    request.pdu.data.push_back(count & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    if (!success) {
        return false;
    }
    
    // 解析响应数据
    if (response.pdu.function_code != request.pdu.function_code ||
        response.pdu.data.size() < 1) {
        return false;
    }
    
    uint8_t byte_count = response.pdu.data[0];
    if (response.pdu.data.size() != static_cast<size_t>(byte_count + 1)) {
        return false;
    }
    
    // 提取字节数据并转换为位
    std::vector<uint8_t> coil_bytes(response.pdu.data.begin() + 1, response.pdu.data.end());
    values = ModbusUtils::bytes_to_bits(coil_bytes, count);
    
    return true;
}

bool ModbusTcpClient::write_single_coil(uint16_t address, bool value) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_COIL);
    
    // 添加地址和值（Modbus规范：TRUE=0xFF00, FALSE=0x0000）
    uint16_t coil_value = value ? 0xFF00 : 0x0000;
    request.pdu.data.push_back((address >> 8) & 0xFF);
    request.pdu.data.push_back(address & 0xFF);
    request.pdu.data.push_back((coil_value >> 8) & 0xFF);
    request.pdu.data.push_back(coil_value & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    return success && response.pdu.function_code == request.pdu.function_code;
}

bool ModbusTcpClient::write_multiple_coils(uint16_t start_address, const std::vector<bool>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_COILS);
    
    // 将位转换为字节
    std::vector<uint8_t> coil_bytes = ModbusUtils::bits_to_bytes(values);
    uint16_t quantity = values.size();
    uint8_t byte_count = coil_bytes.size();
    
    // 添加起始地址、数量和字节计数
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((quantity >> 8) & 0xFF);
    request.pdu.data.push_back(quantity & 0xFF);
    request.pdu.data.push_back(byte_count);
    
    // 添加线圈数据
    request.pdu.data.insert(request.pdu.data.end(), coil_bytes.begin(), coil_bytes.end());
    request.length = 1 + 1 + 5 + byte_count; // unit_id + function_code + header + data
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    return success && response.pdu.function_code == request.pdu.function_code;
}

bool ModbusTcpClient::read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::READ_DISCRETE_INPUTS);
    
    // 添加起始地址和数量
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((count >> 8) & 0xFF);
    request.pdu.data.push_back(count & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    if (!success) {
        return false;
    }
    
    // 解析响应数据
    if (response.pdu.function_code != request.pdu.function_code ||
        response.pdu.data.size() < 1) {
        return false;
    }
    
    uint8_t byte_count = response.pdu.data[0];
    if (response.pdu.data.size() != static_cast<size_t>(byte_count + 1)) {
        return false;
    }
    
    // 提取字节数据并转换为位
    std::vector<uint8_t> input_bytes(response.pdu.data.begin() + 1, response.pdu.data.end());
    values = ModbusUtils::bytes_to_bits(input_bytes, count);
    
    return true;
}

bool ModbusTcpClient::write_multiple_registers(uint16_t start_address, const std::vector<uint16_t>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_REGISTERS);
    
    uint16_t quantity = values.size();
    uint8_t byte_count = quantity * 2;
    
    // 添加起始地址、数量和字节计数
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((quantity >> 8) & 0xFF);
    request.pdu.data.push_back(quantity & 0xFF);
    request.pdu.data.push_back(byte_count);
    
    // 添加寄存器数据
    for (uint16_t value : values) {
        request.pdu.data.push_back((value >> 8) & 0xFF);
        request.pdu.data.push_back(value & 0xFF);
    }
    
    request.length = 1 + 1 + 5 + byte_count; // unit_id + function_code + header + data
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    return success && response.pdu.function_code == request.pdu.function_code;
}

bool ModbusTcpClient::read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) {
    ModbusTcpADU request;
    request.transaction_id = next_transaction_id();
    request.protocol_id = 0;
    request.unit_id = config_.unit_id;
    request.pdu.function_code = static_cast<uint8_t>(ModbusFunction::READ_INPUT_REGISTERS);
    
    // 添加起始地址和数量
    request.pdu.data.push_back((start_address >> 8) & 0xFF);
    request.pdu.data.push_back(start_address & 0xFF);
    request.pdu.data.push_back((count >> 8) & 0xFF);
    request.pdu.data.push_back(count & 0xFF);
    request.length = 6; // unit_id(1) + function_code(1) + data(4)
    
    ModbusTcpADU response;
    auto start_time = std::chrono::steady_clock::now();
    
    bool success = send_request(request, response);
    update_statistics(success, start_time);
    
    if (!success) {
        return false;
    }
    
    // 解析响应数据
    if (response.pdu.function_code != request.pdu.function_code ||
        response.pdu.data.size() < 1) {
        return false;
    }
    
    uint8_t byte_count = response.pdu.data[0];
    if (response.pdu.data.size() != static_cast<size_t>(byte_count + 1) || byte_count != count * 2) {
        return false;
    }
    
    values.clear();
    values.reserve(count);
    
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = (response.pdu.data[1 + i * 2] << 8) | response.pdu.data[2 + i * 2];
        values.push_back(value);
    }
    
    return true;
}

bool ModbusTcpClient::send_request(const ModbusTcpADU& request, ModbusTcpADU& response) {
    if (!connected_.load()) {
        if (!connect()) {
            return false;
        }
    }
    
    // 序列化请求
    std::vector<uint8_t> request_data = serialize_adu(request);
    
    // 发送请求
    if (!send_raw_data(request_data)) {
        return false;
    }
    
    // 接收响应头部 (MBAP Header: 7 bytes)
    std::vector<uint8_t> header_data;
    if (!receive_raw_data(header_data, 7)) {
        return false;
    }
    
    // 解析长度字段
    uint16_t length = (header_data[4] << 8) | header_data[5];
    
    // 接收剩余数据
    std::vector<uint8_t> pdu_data;
    if (!receive_raw_data(pdu_data, length - 1)) { // length包含unit_id，所以减1
        return false;
    }
    
    // 合并完整响应数据
    std::vector<uint8_t> response_data;
    response_data.insert(response_data.end(), header_data.begin(), header_data.end());
    response_data.insert(response_data.end(), pdu_data.begin(), pdu_data.end());
    
    // 反序列化响应
    return deserialize_adu(response_data, response);
}

bool ModbusTcpClient::send_raw_data(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    size_t total_sent = 0;
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout_duration = config_.timeout;
    
    while (total_sent < data.size()) {
        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout_duration) {
            std::cerr << "发送数据超时: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms" << std::endl;
            return false;
        }
        
        ssize_t sent = send(socket_fd_, 
                           reinterpret_cast<const char*>(data.data() + total_sent), 
                           data.size() - total_sent, 0);
        if (sent <= 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
            std::cerr << "发送数据错误: " << error << std::endl;
#else
            std::cerr << "发送数据错误: " << errno << std::endl;
#endif
            return false;
        }
        total_sent += sent;
    }
    
    statistics_.messages_sent.fetch_add(1);
    return true;
}

bool ModbusTcpClient::receive_raw_data(std::vector<uint8_t>& data, size_t expected_length) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    data.resize(expected_length);
    size_t total_received = 0;
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout_duration = config_.timeout;
    
    while (total_received < expected_length) {
        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout_duration) {
            std::cerr << "接收数据超时: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms" << std::endl;
            return false;
        }
        
        // 设置套接字为非阻塞模式进行超时检查
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
        
        ssize_t received = recv(socket_fd_, 
                               reinterpret_cast<char*>(data.data() + total_received), 
                               expected_length - total_received, 0);
        
        if (received > 0) {
            total_received += received;
        } else if (received == 0) {
            // 连接被对端关闭
            std::cerr << "连接被对端关闭" << std::endl;
            return false;
        } else {
            // 检查错误类型
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                std::cerr << "接收数据错误: " << error << std::endl;
                return false;
            }
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "接收数据错误: " << errno << std::endl;
                return false;
            }
#endif
            // 非阻塞模式下没有数据可读，短暂等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // 恢复阻塞模式
#ifdef _WIN32
        mode = 0;
        ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
        fcntl(socket_fd_, F_SETFL, flags);
#endif
    }
    
    statistics_.messages_received.fetch_add(1);
    return true;
}

std::vector<uint8_t> ModbusTcpClient::serialize_adu(const ModbusTcpADU& adu) {
    std::vector<uint8_t> result;
    
    // MBAP Header
    result.push_back((adu.transaction_id >> 8) & 0xFF);
    result.push_back(adu.transaction_id & 0xFF);
    result.push_back((adu.protocol_id >> 8) & 0xFF);
    result.push_back(adu.protocol_id & 0xFF);
    result.push_back((adu.length >> 8) & 0xFF);
    result.push_back(adu.length & 0xFF);
    result.push_back(adu.unit_id);
    
    // PDU
    result.push_back(adu.pdu.function_code);
    result.insert(result.end(), adu.pdu.data.begin(), adu.pdu.data.end());
    
    return result;
}

bool ModbusTcpClient::deserialize_adu(const std::vector<uint8_t>& data, ModbusTcpADU& adu) {
    // 最小长度检查：MBAP头部(7) + 功能码(1)
    if (data.size() < 8) {
        return false;
    }
    
    // 最大PDU限制，防止OOM/DoS攻击
    static constexpr size_t MAX_PDU_SIZE = 253; // Modbus标准最大PDU尺寸
    if (data.size() > MAX_PDU_SIZE + 7) { // MBAP(7) + PDU
        return false;
    }
    
    // 解析MBAP Header
    adu.transaction_id = (data[0] << 8) | data[1];
    adu.protocol_id = (data[2] << 8) | data[3];
    adu.length = (data[4] << 8) | data[5];
    adu.unit_id = data[6];
    
    // 严格校验MBAP头部
    // 1. Protocol ID必须为0（Modbus协议）
    if (adu.protocol_id != 0) {
        return false;
    }
    
    // 2. Length字段与实际PDU一致性检查
    // Length = Unit ID(1) + PDU length
    size_t expected_total_length = adu.length + 6; // MBAP前6字节 + Length字段指示的内容
    if (data.size() != expected_total_length) {
        return false;
    }
    
    // 3. Length字段范围检查（Unit ID + 至少一个功能码）
    if (adu.length < 2) {
        return false;
    }
    
    // 4. Unit ID范围检查（通常为1-247，0和255为特殊用途）
    if (adu.unit_id == 0 || adu.unit_id > 247) {
        // 可以选择接受或拒绝，这里采用宽松策略
        // return false; // 严格模式下可开启
    }
    
    // 5. MBAP Length边界值检查
    // Length = Unit ID(1) + Function Code(1) + Data
    // 最小值: 2 (仅Unit ID + Function Code)
    // 最大值: 255 (Modbus协议限制)
    static constexpr uint16_t MIN_MBAP_LENGTH = 2;
    static constexpr uint16_t MAX_MBAP_LENGTH = 255;
    
    if (adu.length < MIN_MBAP_LENGTH || adu.length > MAX_MBAP_LENGTH) {
        return false;
    }
    
    // 6. Length与PDU实际长度的精确匹配检查
    // actual_pdu_length = data.size() - 6 (MBAP头部6字节) - 1 (Unit ID)
    size_t actual_pdu_length = data.size() - 7; // MBAP头部(6) + Unit ID(1)
    size_t expected_pdu_length = adu.length - 1; // Length包括Unit ID，所以PDU = Length - 1
    
    if (actual_pdu_length != expected_pdu_length) {
        // Length字段与实际PDU长度不匹配
        return false;
    }
    
    // 解析PDU
    adu.pdu.function_code = data[7];
    adu.pdu.data.clear();
    if (data.size() > 8) {
        adu.pdu.data.insert(adu.pdu.data.end(), data.begin() + 8, data.end());
    }
    
    // 功能码合法性检查
    if (!is_valid_function_code(adu.pdu.function_code)) {
        return false;
    }
    
    return true;
}

void ModbusTcpClient::update_statistics(bool success, const std::chrono::steady_clock::time_point& start_time) {
    if (!success) {
        statistics_.errors_occurred.fetch_add(1);
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto response_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    statistics_.average_response_time = response_time; // 简化处理，实际应该计算平均值
    statistics_.last_communication = end_time;
}

void ModbusTcpClient::reset_statistics() {
    statistics_.reset();
}

// =============================================================================
// ModbusUtils Implementation
// =============================================================================

uint16_t ModbusUtils::bytes_to_uint16(uint8_t high_byte, uint8_t low_byte) {
    return (static_cast<uint16_t>(high_byte) << 8) | low_byte;
}

void ModbusUtils::uint16_to_bytes(uint16_t value, uint8_t& high_byte, uint8_t& low_byte) {
    high_byte = (value >> 8) & 0xFF;
    low_byte = value & 0xFF;
}

std::vector<bool> ModbusUtils::bytes_to_bits(const std::vector<uint8_t>& bytes, uint16_t bit_count) {
    std::vector<bool> bits;
    bits.reserve(bit_count);
    
    for (uint16_t i = 0; i < bit_count; i++) {
        uint8_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        
        if (byte_index < bytes.size()) {
            bool bit_value = (bytes[byte_index] & (1 << bit_index)) != 0;
            bits.push_back(bit_value);
        } else {
            bits.push_back(false);
        }
    }
    
    return bits;
}

std::vector<uint8_t> ModbusUtils::bits_to_bytes(const std::vector<bool>& bits) {
    size_t byte_count = (bits.size() + 7) / 8; // 向上取整
    std::vector<uint8_t> bytes(byte_count, 0);
    
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i]) {
            size_t byte_index = i / 8;
            size_t bit_index = i % 8;
            bytes[byte_index] |= (1 << bit_index);
        }
    }
    
    return bytes;
}

std::string ModbusUtils::bytes_to_hex_string(const std::vector<uint8_t>& bytes) {
    std::string result;
    const char* hex_chars = "0123456789ABCDEF";
    
    for (uint8_t byte : bytes) {
        result += hex_chars[(byte >> 4) & 0x0F];
        result += hex_chars[byte & 0x0F];
        result += " ";
    }
    
    if (!result.empty()) {
        result.pop_back(); // 移除最后的空格
    }
    
    return result;
}

std::string ModbusUtils::adu_to_string(const ModbusTcpADU& adu) {
    std::string result = "ModbusTCP ADU:\n";
    result += "  Transaction ID: " + std::to_string(adu.transaction_id) + "\n";
    result += "  Protocol ID: " + std::to_string(adu.protocol_id) + "\n";
    result += "  Length: " + std::to_string(adu.length) + "\n";
    result += "  Unit ID: " + std::to_string(adu.unit_id) + "\n";
    result += "  Function Code: 0x" + std::to_string(adu.pdu.function_code) + "\n";
    result += "  Data: " + bytes_to_hex_string(adu.pdu.data);
    
    return result;
}

//=============================================================================
// ModbusTcpServer Implementation
//=============================================================================

ModbusTcpServer::ModbusTcpServer(const Config& config) 
    : config_(config) {
    data_map_ = std::make_shared<DefaultModbusDataMap>();
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

ModbusTcpServer::~ModbusTcpServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool ModbusTcpServer::start() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (running_.load()) {
        return true;
    }
    
    // 创建服务器套接字
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        return false;
    }
    
    // 设置地址重用
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);
    
    if (config_.bind_address == "0.0.0.0") {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config_.bind_address.c_str(), &server_addr.sin_addr) <= 0) {
#ifndef _WIN32
            close(server_socket_);
#else
            closesocket(server_socket_);
#endif
            return false;
        }
    }
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
#ifndef _WIN32
        close(server_socket_);
#else
        closesocket(server_socket_);
#endif
        return false;
    }
    
    // 开始监听
    if (listen(server_socket_, config_.max_connections) < 0) {
#ifndef _WIN32
        close(server_socket_);
#else
        closesocket(server_socket_);
#endif
        return false;
    }
    
    running_.store(true);
    
    // 启动客户端接受线程
    accept_thread_ = std::thread(&ModbusTcpServer::accept_clients, this);
    
    return true;
}

void ModbusTcpServer::stop() {
    running_.store(false);
    
    if (server_socket_ >= 0) {
#ifndef _WIN32
        close(server_socket_);
#else
        closesocket(server_socket_);
#endif
        server_socket_ = -1;
    }
    
    // 等待接受线程结束
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // 等待所有客户端线程结束
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
}

void ModbusTcpServer::set_data_map(std::shared_ptr<ModbusDataMap> data_map) {
    data_map_ = data_map;
}

void ModbusTcpServer::set_client_connected_callback(ClientConnectedCallback callback) {
    client_connected_callback_ = callback;
}

void ModbusTcpServer::set_client_disconnected_callback(ClientDisconnectedCallback callback) {
    client_disconnected_callback_ = callback;
}

void ModbusTcpServer::set_request_received_callback(RequestReceivedCallback callback) {
    request_received_callback_ = callback;
}

void ModbusTcpServer::reset_statistics() {
    statistics_.reset();
}

size_t ModbusTcpServer::get_active_connections() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return client_threads_.size();
}

void ModbusTcpServer::accept_clients() {
    while (running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (running_.load()) {
                continue; // 如果服务器仍在运行，继续接受连接
            } else {
                break; // 服务器已停止
            }
        }
        
        // 获取客户端IP地址
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        uint16_t client_port = ntohs(client_addr.sin_port);
        
        // 检查连接数限制
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (client_threads_.size() >= config_.max_connections) {
#ifndef _WIN32
                close(client_socket);
#else
                closesocket(client_socket);
#endif
                continue;
            }
        }
        
        // 触发客户端连接回调
        if (client_connected_callback_) {
            client_connected_callback_(client_ip, client_port);
        }
        
        // 启动客户端处理线程
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_threads_.emplace_back(&ModbusTcpServer::handle_client, this, 
                                       client_socket, std::string(client_ip), client_port);
        }
    }
}

void ModbusTcpServer::handle_client(int client_socket, const std::string& client_ip, uint16_t client_port) {
    bool client_active = true;
    
    try {
        while (running_.load() && client_active) {
            // 健壮的MBAP头部接收（7字节）
            std::vector<uint8_t> header_buffer(7);
            if (!receive_exact_bytes(client_socket, header_buffer.data(), 7, 5000)) {
                break; // 客户端断开或超时
            }
            
            // 解析并验证MBAP长度字段
            uint16_t length = (header_buffer[4] << 8) | header_buffer[5];
            if (length < 2 || length > 253) {
                // 无效长度，跳过违法数据并继续处理
                continue;
            }
            
            // 验证Protocol ID（必须为0）
            uint16_t protocol_id = (header_buffer[2] << 8) | header_buffer[3];
            if (protocol_id != 0) {
                // 非Modbus协议，跳过
                continue;
            }
            
            // 健壮的PDU数据接收
            std::vector<uint8_t> pdu_buffer(length - 1); // length包括unit_id，所以PDU长度为length-1
            if (!receive_exact_bytes(client_socket, pdu_buffer.data(), length - 1, 5000)) {
                break; // PDU接收失败或超时
            }
            
            // 构建完整的请求数据
            std::vector<uint8_t> request_data;
            request_data.insert(request_data.end(), header_buffer.begin(), header_buffer.end());
            request_data.insert(request_data.end(), pdu_buffer.begin(), pdu_buffer.end());
            
            // 反序列化请求
            ModbusTcpADU request;
            if (!deserialize_adu(request_data, request)) {
                continue; // 无效请求
            }
            
            // 触发请求接收回调
            if (request_received_callback_) {
                request_received_callback_(client_ip, request.pdu);
            }
            
            // 处理请求
            ModbusPDU response_pdu = process_request(request.pdu);
            
            // 构建响应ADU
            ModbusTcpADU response;
            response.transaction_id = request.transaction_id;
            response.protocol_id = request.protocol_id;
            response.unit_id = request.unit_id;
            response.pdu = response_pdu;
            response.length = 2 + response_pdu.data.size(); // unit_id(1) + function_code(1) + data
            
            // 发送响应
            std::vector<uint8_t> response_data = serialize_adu(response);
            ssize_t sent = send(client_socket, reinterpret_cast<const char*>(response_data.data()), 
                              response_data.size(), 0);
            
            if (sent <= 0) {
                break; // 发送失败，客户端可能断开
            }
            
            statistics_.messages_sent.fetch_add(1);
            statistics_.messages_received.fetch_add(1);
        }
        
    } catch (const std::exception& e) {
        // 处理异常，记录错误
        statistics_.errors_occurred.fetch_add(1);
    }
    
    // 关闭客户端套接字
#ifndef _WIN32
    close(client_socket);
#else
    closesocket(client_socket);
#endif
    
    // 触发客户端断开回调
    if (client_disconnected_callback_) {
        client_disconnected_callback_(client_ip, client_port);
    }
}

ModbusPDU ModbusTcpServer::process_request(const ModbusPDU& request) {
    if (!data_map_) {
        return create_exception_response(request.function_code, ModbusException::SLAVE_DEVICE_FAILURE);
    }
    
    try {
        switch (static_cast<ModbusFunction>(request.function_code)) {
            case ModbusFunction::READ_COILS:
                return handle_read_coils(request.data);
                
            case ModbusFunction::READ_DISCRETE_INPUTS:
                return handle_read_discrete_inputs(request.data);
                
            case ModbusFunction::READ_HOLDING_REGISTERS:
                return handle_read_holding_registers(request.data);
                
            case ModbusFunction::READ_INPUT_REGISTERS:
                return handle_read_input_registers(request.data);
                
            case ModbusFunction::WRITE_SINGLE_COIL:
                return handle_write_single_coil(request.data);
                
            case ModbusFunction::WRITE_SINGLE_REGISTER:
                return handle_write_single_register(request.data);
                
            case ModbusFunction::WRITE_MULTIPLE_COILS:
                return handle_write_multiple_coils(request.data);
                
            case ModbusFunction::WRITE_MULTIPLE_REGISTERS:
                return handle_write_multiple_registers(request.data);
                
            default:
                return create_exception_response(request.function_code, ModbusException::ILLEGAL_FUNCTION);
        }
    } catch (const std::exception&) {
        return create_exception_response(request.function_code, ModbusException::SLAVE_DEVICE_FAILURE);
    }
}

ModbusPDU ModbusTcpServer::handle_read_coils(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_COILS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    
    if (quantity == 0 || quantity > 2000) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_COILS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<bool> coils;
    if (!data_map_->read_coils(start_address, quantity, coils)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_COILS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    // 将布尔值打包成字节
    std::vector<uint8_t> packed_data = ModbusUtils::bits_to_bytes(coils);
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::READ_COILS);
    response.data.push_back(static_cast<uint8_t>(packed_data.size()));
    response.data.insert(response.data.end(), packed_data.begin(), packed_data.end());
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_read_discrete_inputs(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_DISCRETE_INPUTS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    
    if (quantity == 0 || quantity > 2000) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_DISCRETE_INPUTS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<bool> inputs;
    if (!data_map_->read_discrete_inputs(start_address, quantity, inputs)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_DISCRETE_INPUTS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    std::vector<uint8_t> packed_data = ModbusUtils::bits_to_bytes(inputs);
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::READ_DISCRETE_INPUTS);
    response.data.push_back(static_cast<uint8_t>(packed_data.size()));
    response.data.insert(response.data.end(), packed_data.begin(), packed_data.end());
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_read_holding_registers(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_HOLDING_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    
    if (quantity == 0 || quantity > 125) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_HOLDING_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<uint16_t> registers;
    if (!data_map_->read_holding_registers(start_address, quantity, registers)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_HOLDING_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::READ_HOLDING_REGISTERS);
    response.data.push_back(static_cast<uint8_t>(quantity * 2)); // 字节计数
    
    for (uint16_t reg : registers) {
        response.data.push_back((reg >> 8) & 0xFF);
        response.data.push_back(reg & 0xFF);
    }
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_read_input_registers(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_INPUT_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    
    if (quantity == 0 || quantity > 125) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_INPUT_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<uint16_t> registers;
    if (!data_map_->read_input_registers(start_address, quantity, registers)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::READ_INPUT_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::READ_INPUT_REGISTERS);
    response.data.push_back(static_cast<uint8_t>(quantity * 2));
    
    for (uint16_t reg : registers) {
        response.data.push_back((reg >> 8) & 0xFF);
        response.data.push_back(reg & 0xFF);
    }
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_write_single_coil(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_COIL), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t address = (data[0] << 8) | data[1];
    uint16_t value = (data[2] << 8) | data[3];
    
    if (value != 0x0000 && value != 0xFF00) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_COIL), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    bool coil_value = (value == 0xFF00);
    if (!data_map_->write_coil(address, coil_value)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_COIL), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    // 响应与请求相同
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_COIL);
    response.data = data;
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_write_single_register(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_REGISTER), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t address = (data[0] << 8) | data[1];
    uint16_t value = (data[2] << 8) | data[3];
    
    if (!data_map_->write_holding_register(address, value)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_REGISTER), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    // 响应与请求相同
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_SINGLE_REGISTER);
    response.data = data;
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_write_multiple_coils(const std::vector<uint8_t>& data) {
    if (data.size() < 6) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_COILS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    uint8_t byte_count = data[4];
    
    if (quantity == 0 || quantity > 1968 || byte_count != (quantity + 7) / 8 || 
        data.size() != static_cast<size_t>(5 + byte_count)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_COILS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<uint8_t> coil_bytes(data.begin() + 5, data.end());
    std::vector<bool> coils = ModbusUtils::bytes_to_bits(coil_bytes, quantity);
    
    if (!data_map_->write_coils(start_address, coils)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_COILS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_COILS);
    response.data.insert(response.data.end(), data.begin(), data.begin() + 4);
    
    return response;
}

ModbusPDU ModbusTcpServer::handle_write_multiple_registers(const std::vector<uint8_t>& data) {
    if (data.size() < 6) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t quantity = (data[2] << 8) | data[3];
    uint8_t byte_count = data[4];
    
    if (quantity == 0 || quantity > 123 || byte_count != quantity * 2 || 
        data.size() != static_cast<size_t>(5 + byte_count)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_VALUE);
    }
    
    std::vector<uint16_t> registers;
    for (size_t i = 5; i < data.size(); i += 2) {
        uint16_t value = (data[i] << 8) | data[i + 1];
        registers.push_back(value);
    }
    
    if (!data_map_->write_holding_registers(start_address, registers)) {
        return create_exception_response(static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_REGISTERS), 
                                       ModbusException::ILLEGAL_DATA_ADDRESS);
    }
    
    ModbusPDU response;
    response.function_code = static_cast<uint8_t>(ModbusFunction::WRITE_MULTIPLE_REGISTERS);
    response.data.insert(response.data.end(), data.begin(), data.begin() + 4);
    
    return response;
}

ModbusPDU ModbusTcpServer::create_exception_response(uint8_t function_code, ModbusException exception) {
    ModbusPDU response;
    response.function_code = function_code | 0x80; // 设置异常响应标志
    response.data.push_back(static_cast<uint8_t>(exception));
    return response;
}

std::vector<uint8_t> ModbusTcpServer::serialize_adu(const ModbusTcpADU& adu) {
    std::vector<uint8_t> result;
    
    // MBAP Header
    result.push_back((adu.transaction_id >> 8) & 0xFF);
    result.push_back(adu.transaction_id & 0xFF);
    result.push_back((adu.protocol_id >> 8) & 0xFF);
    result.push_back(adu.protocol_id & 0xFF);
    result.push_back((adu.length >> 8) & 0xFF);
    result.push_back(adu.length & 0xFF);
    result.push_back(adu.unit_id);
    
    // PDU
    result.push_back(adu.pdu.function_code);
    result.insert(result.end(), adu.pdu.data.begin(), adu.pdu.data.end());
    
    return result;
}

bool ModbusTcpServer::deserialize_adu(const std::vector<uint8_t>& data, ModbusTcpADU& adu) {
    if (data.size() < 8) {
        return false;
    }
    
    // 解析MBAP Header
    adu.transaction_id = (data[0] << 8) | data[1];
    adu.protocol_id = (data[2] << 8) | data[3];
    adu.length = (data[4] << 8) | data[5];
    adu.unit_id = data[6];
    
    // 解析PDU
    adu.pdu.function_code = data[7];
    adu.pdu.data.clear();
    if (data.size() > 8) {
        adu.pdu.data.insert(adu.pdu.data.end(), data.begin() + 8, data.end());
    }
    
    return true;
}

} // namespace communication
} // namespace plc_runtime