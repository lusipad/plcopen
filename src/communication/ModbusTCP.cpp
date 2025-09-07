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

// Windows compatibility - define ssize_t for Windows
#ifdef _WIN32
    typedef long long ssize_t;
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace plc_runtime {
namespace communication {

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
    while (total_sent < data.size()) {
        ssize_t sent = send(socket_fd_, 
                           reinterpret_cast<const char*>(data.data() + total_sent), 
                           data.size() - total_sent, 0);
        if (sent <= 0) {
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
    
    while (total_received < expected_length) {
        ssize_t received = recv(socket_fd_, 
                               reinterpret_cast<char*>(data.data() + total_received), 
                               expected_length - total_received, 0);
        if (received <= 0) {
            return false;
        }
        total_received += received;
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
    if (data.size() < 8) { // 至少需要MBAP头部(7) + 功能码(1)
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
            // 接收MBAP头部（7字节）
            std::vector<uint8_t> header_buffer(7);
            ssize_t received = recv(client_socket, reinterpret_cast<char*>(header_buffer.data()), 7, 0);
            
            if (received <= 0) {
                break; // 客户端断开连接
            }
            
            if (received != 7) {
                continue; // 不完整的头部，忽略
            }
            
            // 解析长度字段
            uint16_t length = (header_buffer[4] << 8) | header_buffer[5];
            if (length < 2 || length > 253) {
                continue; // 无效长度
            }
            
            // 接收PDU数据
            std::vector<uint8_t> pdu_buffer(length - 1);
            received = recv(client_socket, reinterpret_cast<char*>(pdu_buffer.data()), length - 1, 0);
            
            if (received != length - 1) {
                continue; // 不完整的PDU
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