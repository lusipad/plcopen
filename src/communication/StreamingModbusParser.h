/**
 * @file StreamingModbusParser.h
 * @brief 健壮的Modbus TCP流式解析器
 * @version 1.0
 * @date 2025-09-09
 * 
 * 专门处理TCP粘包、拆包、半包等极端网络场景的流式解析器
 */

#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include <chrono>
#include "communication/ModbusTCP.h"

namespace plc_runtime {
namespace communication {

/**
 * @brief 解析状态枚举
 */
enum class ParseState {
    WAITING_HEADER,     // 等待MBAP头部
    WAITING_PDU,        // 等待PDU数据
    COMPLETE,           // 解析完成
    ERROR               // 解析错误
};

/**
 * @brief 解析结果
 */
struct ParseResult {
    ParseState state;
    std::optional<ModbusTcpADU> adu;
    std::string error_message;
    size_t bytes_consumed;      // 本次解析消耗的字节数
    size_t bytes_needed;        // 还需要的字节数（如果状态为WAITING_*）
};

/**
 * @brief 流式Modbus TCP解析器
 * 
 * 特性：
 * - 处理任意大小的数据分片
 * - 自动处理粘包和拆包
 * - 支持多个完整包的连续解析
 * - 异常恢复和错误检测
 * - 内存使用优化
 */
class StreamingModbusParser {
public:
    /**
     * @brief 解析器配置
     */
    struct Config {
        size_t max_buffer_size = 4096;         // 最大缓冲区大小
        size_t max_packet_size = 260;          // 最大Modbus ADU大小
        uint32_t parse_timeout_ms = 5000;      // 解析超时时间
        bool strict_validation = true;         // 严格验证模式
        bool enable_recovery = true;           // 启用错误恢复
    };

    explicit StreamingModbusParser(const Config& config = {});
    ~StreamingModbusParser() = default;

    /**
     * @brief 添加接收到的数据进行解析
     * @param data 新接收的数据
     * @param size 数据大小
     * @return 解析结果
     */
    ParseResult feed_data(const uint8_t* data, size_t size);

    /**
     * @brief 添加数据（vector版本）
     */
    ParseResult feed_data(const std::vector<uint8_t>& data);

    /**
     * @brief 重置解析器状态
     */
    void reset();

    /**
     * @brief 检查是否有完整的包可用
     */
    bool has_complete_packet() const;

    /**
     * @brief 获取下一个完整的包
     */
    std::optional<ModbusTcpADU> get_next_packet();

    /**
     * @brief 获取当前状态
     */
    ParseState get_state() const { return state_; }

    /**
     * @brief 获取缓冲区使用情况
     */
    size_t get_buffer_usage() const { return buffer_.size(); }

    /**
     * @brief 设置包完成回调
     */
    void set_packet_callback(std::function<void(const ModbusTcpADU&)> callback);

    /**
     * @brief 设置错误回调
     */
    void set_error_callback(std::function<void(const std::string&)> callback);

private:
    Config config_;
    ParseState state_;
    std::vector<uint8_t> buffer_;           // 累积缓冲区
    std::vector<ModbusTcpADU> completed_packets_;  // 完成的包队列
    
    size_t expected_packet_size_;           // 期望的包大小
    std::chrono::steady_clock::time_point last_activity_;
    
    // 回调函数
    std::function<void(const ModbusTcpADU&)> packet_callback_;
    std::function<void(const std::string&)> error_callback_;

    /**
     * @brief 内部解析逻辑
     */
    ParseResult parse_internal();

    /**
     * @brief 解析MBAP头部
     */
    ParseResult parse_mbap_header();

    /**
     * @brief 解析PDU数据
     */
    ParseResult parse_pdu_data();

    /**
     * @brief 验证完整的ADU
     */
    bool validate_adu(const ModbusTcpADU& adu) const;

    /**
     * @brief 错误恢复：寻找下一个可能的有效MBAP头部
     */
    size_t find_next_sync_point();

    /**
     * @brief 检查解析超时
     */
    bool is_parse_timeout() const;

    /**
     * @brief 调用错误回调
     */
    void notify_error(const std::string& error);

    /**
     * @brief 调用包完成回调
     */
    void notify_packet_complete(const ModbusTcpADU& adu);
};

/**
 * @brief 流式解析器的模糊测试助手
 */
class StreamingParserFuzzTester {
public:
    struct FuzzConfig {
        size_t min_fragment_size = 1;      // 最小分片大小
        size_t max_fragment_size = 64;     // 最大分片大小
        size_t total_iterations = 1000;    // 测试迭代次数
        double corruption_rate = 0.01;     // 数据损坏率
        bool inject_duplicates = true;     // 注入重复数据
        bool inject_truncation = true;     // 注入截断
        bool inject_reordering = false;    // 注入重排序（TCP不会出现）
    };

    explicit StreamingParserFuzzTester(const FuzzConfig& config = {});

    /**
     * @brief 执行模糊测试
     */
    bool run_fuzz_test(StreamingModbusParser& parser);

    /**
     * @brief 生成随机Modbus包
     */
    std::vector<uint8_t> generate_random_packet();

    /**
     * @brief 将包分片成随机大小的片段
     */
    std::vector<std::vector<uint8_t>> fragment_packet(const std::vector<uint8_t>& packet);

    /**
     * @brief 注入各种异常
     */
    std::vector<uint8_t> inject_anomalies(const std::vector<uint8_t>& data);

private:
    FuzzConfig config_;
    std::mt19937 rng_;
};

} // namespace communication
} // namespace plc_runtime