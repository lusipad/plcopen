/**
 * @file StreamingModbusParser.cpp
 * @brief 健壮的Modbus TCP流式解析器实现
 */

#include "StreamingModbusParser.h"
#include <algorithm>
#include <random>
#include <cstring>

namespace plc_runtime {
namespace communication {

StreamingModbusParser::StreamingModbusParser(const Config& config) 
    : config_(config), state_(ParseState::WAITING_HEADER), expected_packet_size_(0) {
    buffer_.reserve(config_.max_buffer_size);
    last_activity_ = std::chrono::steady_clock::now();
}

ParseResult StreamingModbusParser::feed_data(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return {state_, std::nullopt, "Invalid input data", 0, 0};
    }

    // 检查缓冲区空间
    if (buffer_.size() + size > config_.max_buffer_size) {
        if (config_.enable_recovery) {
            // 尝试清理缓冲区
            size_t sync_point = find_next_sync_point();
            if (sync_point > 0) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + sync_point);
            } else {
                // 无法恢复，清空缓冲区
                buffer_.clear();
                state_ = ParseState::WAITING_HEADER;
            }
        } else {
            notify_error("Buffer overflow");
            return {ParseState::ERROR, std::nullopt, "Buffer overflow", 0, 0};
        }
    }

    // 添加新数据到缓冲区
    buffer_.insert(buffer_.end(), data, data + size);
    last_activity_ = std::chrono::steady_clock::now();

    // 持续解析直到没有更多完整包
    ParseResult result;
    do {
        result = parse_internal();
        if (result.state == ParseState::COMPLETE && result.adu) {
            completed_packets_.push_back(*result.adu);
            notify_packet_complete(*result.adu);
        }
    } while (result.state == ParseState::COMPLETE);

    return result;
}

ParseResult StreamingModbusParser::feed_data(const std::vector<uint8_t>& data) {
    return feed_data(data.data(), data.size());
}

ParseResult StreamingModbusParser::parse_internal() {
    // 检查超时
    if (is_parse_timeout()) {
        if (config_.enable_recovery) {
            reset();
            return {ParseState::WAITING_HEADER, std::nullopt, "Parse timeout, reset", 0, 7};
        } else {
            return {ParseState::ERROR, std::nullopt, "Parse timeout", 0, 0};
        }
    }

    switch (state_) {
        case ParseState::WAITING_HEADER:
            return parse_mbap_header();
        
        case ParseState::WAITING_PDU:
            return parse_pdu_data();
        
        case ParseState::COMPLETE:
            // 应该不会到达这里
            state_ = ParseState::WAITING_HEADER;
            return {ParseState::WAITING_HEADER, std::nullopt, "", 0, 7};
        
        case ParseState::ERROR:
            if (config_.enable_recovery) {
                size_t sync_point = find_next_sync_point();
                if (sync_point > 0) {
                    buffer_.erase(buffer_.begin(), buffer_.begin() + sync_point);
                    state_ = ParseState::WAITING_HEADER;
                    return parse_internal();
                }
            }
            return {ParseState::ERROR, std::nullopt, "Unrecoverable error", 0, 0};
    }

    return {ParseState::ERROR, std::nullopt, "Unknown state", 0, 0};
}

ParseResult StreamingModbusParser::parse_mbap_header() {
    static constexpr size_t MBAP_HEADER_SIZE = 7;
    
    if (buffer_.size() < MBAP_HEADER_SIZE) {
        return {ParseState::WAITING_HEADER, std::nullopt, "", 0, MBAP_HEADER_SIZE - buffer_.size()};
    }

    // 解析MBAP头部
    uint16_t transaction_id = (buffer_[0] << 8) | buffer_[1];
    uint16_t protocol_id = (buffer_[2] << 8) | buffer_[3];
    uint16_t length = (buffer_[4] << 8) | buffer_[5];
    uint8_t unit_id = buffer_[6];

    // 基本验证
    if (protocol_id != 0) {
        if (config_.enable_recovery) {
            size_t sync_point = find_next_sync_point();
            if (sync_point > 0) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + sync_point);
                return parse_internal();
            }
        }
        notify_error("Invalid protocol ID: " + std::to_string(protocol_id));
        return {ParseState::ERROR, std::nullopt, "Invalid protocol ID", 0, 0};
    }

    if (length < 2 || length > 255) {
        if (config_.enable_recovery) {
            // 跳过这个字节，寻找下一个可能的同步点
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
            return parse_internal();
        }
        notify_error("Invalid length field: " + std::to_string(length));
        return {ParseState::ERROR, std::nullopt, "Invalid length", 0, 0};
    }

    // 计算期望的总包大小
    expected_packet_size_ = MBAP_HEADER_SIZE + (length - 1); // length包含unit_id
    
    if (expected_packet_size_ > config_.max_packet_size) {
        if (config_.enable_recovery) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 1);
            return parse_internal();
        }
        notify_error("Packet too large: " + std::to_string(expected_packet_size_));
        return {ParseState::ERROR, std::nullopt, "Packet too large", 0, 0};
    }

    state_ = ParseState::WAITING_PDU;
    return {ParseState::WAITING_PDU, std::nullopt, "", 0, expected_packet_size_ - buffer_.size()};
}

ParseResult StreamingModbusParser::parse_pdu_data() {
    if (buffer_.size() < expected_packet_size_) {
        return {ParseState::WAITING_PDU, std::nullopt, "", 0, expected_packet_size_ - buffer_.size()};
    }

    // 提取完整的ADU数据
    std::vector<uint8_t> adu_data(buffer_.begin(), buffer_.begin() + expected_packet_size_);
    
    // 解析ADU
    ModbusTcpADU adu;
    adu.transaction_id = (adu_data[0] << 8) | adu_data[1];
    adu.protocol_id = (adu_data[2] << 8) | adu_data[3];
    adu.length = (adu_data[4] << 8) | adu_data[5];
    adu.unit_id = adu_data[6];
    
    // 解析PDU
    if (adu_data.size() > 7) {
        adu.pdu.function_code = adu_data[7];
        if (adu_data.size() > 8) {
            adu.pdu.data.assign(adu_data.begin() + 8, adu_data.end());
        }
    }

    // 验证完整的ADU
    if (config_.strict_validation && !validate_adu(adu)) {
        if (config_.enable_recovery) {
            // 移除这个无效的包，继续寻找下一个
            buffer_.erase(buffer_.begin(), buffer_.begin() + expected_packet_size_);
            state_ = ParseState::WAITING_HEADER;
            return parse_internal();
        }
        notify_error("ADU validation failed");
        return {ParseState::ERROR, std::nullopt, "ADU validation failed", 0, 0};
    }

    // 成功解析，移除已处理的数据
    buffer_.erase(buffer_.begin(), buffer_.begin() + expected_packet_size_);
    state_ = ParseState::WAITING_HEADER;
    
    return {ParseState::COMPLETE, adu, "", expected_packet_size_, 0};
}

bool StreamingModbusParser::validate_adu(const ModbusTcpADU& adu) const {
    // 检查功能码合法性
    static const std::vector<uint8_t> valid_function_codes = {
        1, 2, 3, 4, 5, 6, 15, 16
    };
    
    uint8_t fc = adu.pdu.function_code;
    bool is_exception = (fc & 0x80) != 0;
    
    if (is_exception) {
        fc &= 0x7F; // 移除异常标志
    }
    
    bool valid_fc = std::find(valid_function_codes.begin(), valid_function_codes.end(), fc) 
                    != valid_function_codes.end();
    
    if (!valid_fc) {
        return false;
    }

    // 检查Unit ID范围（可选的严格检查）
    if (config_.strict_validation && adu.unit_id > 247) {
        return false;
    }

    // 检查长度一致性
    size_t expected_pdu_size = adu.length - 1; // length包含unit_id
    size_t actual_pdu_size = 1 + adu.pdu.data.size(); // function_code + data
    
    return expected_pdu_size == actual_pdu_size;
}

size_t StreamingModbusParser::find_next_sync_point() {
    // 寻找可能的MBAP头部开始位置
    for (size_t i = 1; i < buffer_.size() - 6; ++i) {
        // 检查Protocol ID是否为0
        if (buffer_[i + 2] == 0 && buffer_[i + 3] == 0) {
            // 检查Length字段是否合理
            uint16_t length = (buffer_[i + 4] << 8) | buffer_[i + 5];
            if (length >= 2 && length <= 255) {
                return i;
            }
        }
    }
    
    // 如果找不到同步点，保留最后6个字节（可能是不完整的头部）
    return buffer_.size() > 6 ? buffer_.size() - 6 : 0;
}

bool StreamingModbusParser::is_parse_timeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity_);
    return elapsed.count() > config_.parse_timeout_ms;
}

void StreamingModbusParser::reset() {
    buffer_.clear();
    completed_packets_.clear();
    state_ = ParseState::WAITING_HEADER;
    expected_packet_size_ = 0;
    last_activity_ = std::chrono::steady_clock::now();
}

bool StreamingModbusParser::has_complete_packet() const {
    return !completed_packets_.empty();
}

std::optional<ModbusTcpADU> StreamingModbusParser::get_next_packet() {
    if (completed_packets_.empty()) {
        return std::nullopt;
    }
    
    auto packet = completed_packets_.front();
    completed_packets_.erase(completed_packets_.begin());
    return packet;
}

void StreamingModbusParser::set_packet_callback(std::function<void(const ModbusTcpADU&)> callback) {
    packet_callback_ = std::move(callback);
}

void StreamingModbusParser::set_error_callback(std::function<void(const std::string&)> callback) {
    error_callback_ = std::move(callback);
}

void StreamingModbusParser::notify_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

void StreamingModbusParser::notify_packet_complete(const ModbusTcpADU& adu) {
    if (packet_callback_) {
        packet_callback_(adu);
    }
}

// 模糊测试实现
StreamingParserFuzzTester::StreamingParserFuzzTester(const FuzzConfig& config) 
    : config_(config), rng_(std::random_device{}()) {
}

bool StreamingParserFuzzTester::run_fuzz_test(StreamingModbusParser& parser) {
    size_t successful_parses = 0;
    size_t total_packets = 0;

    for (size_t i = 0; i < config_.total_iterations; ++i) {
        parser.reset();
        
        // 生成随机包
        auto packet = generate_random_packet();
        total_packets++;
        
        // 将包分片
        auto fragments = fragment_packet(packet);
        
        // 注入异常
        for (auto& fragment : fragments) {
            fragment = inject_anomalies(fragment);
        }
        
        // 逐个喂给解析器
        bool parse_success = false;
        for (const auto& fragment : fragments) {
            auto result = parser.feed_data(fragment);
            if (result.state == ParseState::COMPLETE) {
                parse_success = true;
            }
        }
        
        // 检查是否有完整包
        while (parser.has_complete_packet()) {
            auto packet_opt = parser.get_next_packet();
            if (packet_opt) {
                successful_parses++;
                break; // 只计算第一个成功解析的包
            }
        }
    }
    
    // 成功率应该在合理范围内（考虑到注入的异常）
    double success_rate = static_cast<double>(successful_parses) / total_packets;
    return success_rate >= 0.3; // 至少30%的包能成功解析（在有异常注入的情况下）
}

std::vector<uint8_t> StreamingParserFuzzTester::generate_random_packet() {
    std::uniform_int_distribution<uint16_t> transaction_dist(0, 65535);
    std::uniform_int_distribution<uint8_t> unit_dist(1, 247);
    std::uniform_int_distribution<uint8_t> func_dist(1, 16);
    std::uniform_int_distribution<size_t> data_size_dist(0, 50);
    
    ModbusTcpADU adu;
    adu.transaction_id = transaction_dist(rng_);
    adu.protocol_id = 0;
    adu.unit_id = unit_dist(rng_);
    adu.pdu.function_code = func_dist(rng_);
    
    // 生成随机数据
    size_t data_size = data_size_dist(rng_);
    adu.pdu.data.resize(data_size);
    for (size_t i = 0; i < data_size; ++i) {
        adu.pdu.data[i] = static_cast<uint8_t>(rng_() & 0xFF);
    }
    
    adu.length = 2 + data_size; // unit_id + function_code + data
    
    // 序列化为字节数组
    std::vector<uint8_t> packet;
    packet.push_back((adu.transaction_id >> 8) & 0xFF);
    packet.push_back(adu.transaction_id & 0xFF);
    packet.push_back((adu.protocol_id >> 8) & 0xFF);
    packet.push_back(adu.protocol_id & 0xFF);
    packet.push_back((adu.length >> 8) & 0xFF);
    packet.push_back(adu.length & 0xFF);
    packet.push_back(adu.unit_id);
    packet.push_back(adu.pdu.function_code);
    packet.insert(packet.end(), adu.pdu.data.begin(), adu.pdu.data.end());
    
    return packet;
}

std::vector<std::vector<uint8_t>> StreamingParserFuzzTester::fragment_packet(const std::vector<uint8_t>& packet) {
    std::vector<std::vector<uint8_t>> fragments;
    
    std::uniform_int_distribution<size_t> fragment_size_dist(config_.min_fragment_size, config_.max_fragment_size);
    
    size_t offset = 0;
    while (offset < packet.size()) {
        size_t fragment_size = std::min(fragment_size_dist(rng_), packet.size() - offset);
        
        std::vector<uint8_t> fragment(packet.begin() + offset, packet.begin() + offset + fragment_size);
        fragments.push_back(fragment);
        
        offset += fragment_size;
    }
    
    return fragments;
}

std::vector<uint8_t> StreamingParserFuzzTester::inject_anomalies(const std::vector<uint8_t>& data) {
    auto result = data;
    
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    
    // 数据损坏
    if (prob_dist(rng_) < config_.corruption_rate) {
        if (!result.empty()) {
            std::uniform_int_distribution<size_t> pos_dist(0, result.size() - 1);
            size_t pos = pos_dist(rng_);
            result[pos] ^= 0xFF; // 翻转所有位
        }
    }
    
    // 注入重复数据
    if (config_.inject_duplicates && prob_dist(rng_) < 0.05) {
        std::uniform_int_distribution<size_t> dup_size_dist(1, std::min(size_t(10), result.size()));
        size_t dup_size = dup_size_dist(rng_);
        
        std::uniform_int_distribution<size_t> pos_dist(0, result.size());
        size_t pos = pos_dist(rng_);
        
        std::vector<uint8_t> dup_data(dup_size, 0xAA);
        result.insert(result.begin() + pos, dup_data.begin(), dup_data.end());
    }
    
    // 截断（但不是最后一个片段的情况下）
    if (config_.inject_truncation && prob_dist(rng_) < 0.02 && result.size() > 1) {
        std::uniform_int_distribution<size_t> truncate_size_dist(1, result.size() - 1);
        size_t new_size = result.size() - truncate_size_dist(rng_);
        result.resize(new_size);
    }
    
    return result;
}

} // namespace communication
} // namespace plc_runtime