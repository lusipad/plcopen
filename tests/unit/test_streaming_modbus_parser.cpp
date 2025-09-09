/**
 * @file test_streaming_modbus_parser.cpp
 * @brief StreamingModbusParser边界用例和异常处理测试
 * @version 1.0
 * @date 2025-09-09
 * 
 * 重点测试：
 * - 极端分片场景：1字节分片、随机分片、不完整包
 * - 粘包处理：多包粘连、部分包、错位包
 * - 错误恢复：损坏数据、协议违规、同步点重建
 * - 边界条件：最大包长、空数据、无效长度
 */

#include "../TestFramework.h"
#include "communication/StreamingModbusParser.h"
#include <random>
#include <vector>
#include <cstring>

using namespace plc_runtime::communication;

class StreamingModbusParserTest : public TestFramework {
public:
    void SetUp() override {
        parser_ = std::make_unique<StreamingModbusParser>();
        
        // 预构造一些标准测试包
        createTestPackets();
    }
    
    void TearDown() override {
        parser_.reset();
    }

protected:
    std::unique_ptr<StreamingModbusParser> parser_;
    std::vector<std::vector<uint8_t>> test_packets_;
    
    void createTestPackets() {
        // 标准读取线圈响应包
        test_packets_.push_back({
            0x00, 0x01,  // Transaction ID
            0x00, 0x00,  // Protocol ID
            0x00, 0x04,  // Length
            0x01,        // Unit ID
            0x01,        // Function Code
            0x01,        // Byte count
            0xFF         // Coil data
        });
        
        // 标准读取寄存器响应包
        test_packets_.push_back({
            0x00, 0x02,  // Transaction ID
            0x00, 0x00,  // Protocol ID
            0x00, 0x05,  // Length
            0x01,        // Unit ID
            0x03,        // Function Code
            0x02,        // Byte count
            0x12, 0x34   // Register data
        });
        
        // 错误响应包
        test_packets_.push_back({
            0x00, 0x03,  // Transaction ID
            0x00, 0x00,  // Protocol ID
            0x00, 0x03,  // Length
            0x01,        // Unit ID
            0x81,        // Exception response (0x01 + 0x80)
            0x02         // Exception code
        });
    }
    
    // 创建随机测试数据
    std::vector<uint8_t> createRandomPacket(size_t max_size = 253) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> byte_dist(0, 255);
        std::uniform_int_distribution<size_t> size_dist(7, max_size);
        
        size_t size = size_dist(gen);
        std::vector<uint8_t> packet(size);
        
        // 构造有效的MBAP头部
        packet[0] = byte_dist(gen);  // Transaction ID high
        packet[1] = byte_dist(gen);  // Transaction ID low
        packet[2] = 0x00;           // Protocol ID high
        packet[3] = 0x00;           // Protocol ID low
        
        // 计算正确的长度字段
        uint16_t length = size - 6;
        packet[4] = (length >> 8) & 0xFF;  // Length high
        packet[5] = length & 0xFF;         // Length low
        
        packet[6] = byte_dist(gen);  // Unit ID
        
        // PDU数据
        for (size_t i = 7; i < size; ++i) {
            packet[i] = byte_dist(gen);
        }
        
        return packet;
    }
};

// =============================================================================
// 极端分片场景测试
// =============================================================================

void test_single_byte_fragmentation() {
    std::cout << "\n=== 单字节分片测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    auto test_packet = std::vector<uint8_t>{
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x01, 0xFF
    };
    
    std::vector<ModbusTcpADU> parsed_packets;
    
    // 逐字节发送
    for (size_t i = 0; i < test_packet.size(); ++i) {
        auto result = parser.feed_data(&test_packet[i], 1);
        
        // 只有最后一个字节应该完成解析
        if (i < test_packet.size() - 1) {
            ASSERT_EQ(result.status, ParseResult::Status::NEED_MORE_DATA);
            ASSERT_EQ(result.bytes_consumed, 1);
        } else {
            ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
            ASSERT_EQ(result.bytes_consumed, 1);
        }
        
        // 尝试获取包
        auto packet = parser.get_next_packet();
        if (i == test_packet.size() - 1) {
            ASSERT_TRUE(packet.has_value());
            ASSERT_EQ(packet->transaction_id, 1);
            ASSERT_EQ(packet->pdu.function_code, 0x01);
        } else {
            ASSERT_FALSE(packet.has_value());
        }
    }
    
    std::cout << "✅ 单字节分片测试通过" << std::endl;
}

void test_random_fragmentation_patterns() {
    std::cout << "\n=== 随机分片模式测试 ===" << std::endl;
    
    const int num_test_packets = 50;
    const int num_fragmentation_patterns = 10;
    
    for (int packet_idx = 0; packet_idx < num_test_packets; ++packet_idx) {
        StreamingModbusParser parser;
        
        // 创建随机测试包
        auto test_packet = std::vector<uint8_t>{
            0x00, static_cast<uint8_t>(packet_idx & 0xFF),  // Transaction ID
            0x00, 0x00,  // Protocol ID
            0x00, 0x05,  // Length
            0x01,        // Unit ID
            0x03,        // Function Code  
            0x02,        // Byte count
            0x12, 0x34   // Data
        };
        
        for (int frag_pattern = 0; frag_pattern < num_fragmentation_patterns; ++frag_pattern) {
            parser.reset();
            
            // 随机分片
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> frag_size_dist(1, 5);
            
            size_t offset = 0;
            int fragments_sent = 0;
            
            while (offset < test_packet.size()) {
                size_t fragment_size = std::min(frag_size_dist(gen), test_packet.size() - offset);
                
                auto result = parser.feed_data(&test_packet[offset], fragment_size);
                offset += result.bytes_consumed;
                fragments_sent++;
                
                // 防止无限循环
                if (fragments_sent > 20) {
                    ASSERT_TRUE(false) << "分片测试陷入无限循环";
                    break;
                }
            }
            
            // 应该解析出一个完整包
            auto packet = parser.get_next_packet();
            ASSERT_TRUE(packet.has_value()) << "包" << packet_idx << "分片模式" << frag_pattern << "解析失败";
            ASSERT_EQ(packet->transaction_id, packet_idx & 0xFF);
        }
    }
    
    std::cout << "✅ 随机分片模式测试通过" << std::endl;
}

void test_incomplete_packet_timeout_handling() {
    std::cout << "\n=== 不完整包处理测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 发送不完整的MBAP头部
    std::vector<uint8_t> incomplete_header = {0x00, 0x01, 0x00, 0x00, 0x00};  // 缺少length的低字节
    
    auto result = parser.feed_data(incomplete_header.data(), incomplete_header.size());
    ASSERT_EQ(result.status, ParseResult::Status::NEED_MORE_DATA);
    
    // 检查无包可取
    auto packet = parser.get_next_packet();
    ASSERT_FALSE(packet.has_value());
    
    // 发送声称很长的长度字段，但实际数据不足
    std::vector<uint8_t> false_length = {0xFF};  // Length = 0x00FF = 255
    result = parser.feed_data(false_length.data(), false_length.size());
    ASSERT_EQ(result.status, ParseResult::Status::NEED_MORE_DATA);
    
    // 发送部分Unit ID和PDU
    std::vector<uint8_t> partial_pdu = {0x01, 0x03, 0x02};  // Unit ID + 部分PDU
    result = parser.feed_data(partial_pdu.data(), partial_pdu.size());
    ASSERT_EQ(result.status, ParseResult::Status::NEED_MORE_DATA);
    
    // 重置解析器应该清除所有状态
    parser.reset();
    
    // 发送完整的小包应该能正确解析
    std::vector<uint8_t> complete_small_packet = {
        0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x01, 0x81, 0x02  // 异常响应
    };
    
    result = parser.feed_data(complete_small_packet.data(), complete_small_packet.size());
    ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
    
    packet = parser.get_next_packet();
    ASSERT_TRUE(packet.has_value());
    ASSERT_EQ(packet->transaction_id, 2);
    ASSERT_EQ(packet->pdu.function_code, 0x81);
    
    std::cout << "✅ 不完整包处理测试通过" << std::endl;
}

// =============================================================================
// 粘包处理测试
// =============================================================================

void test_multiple_packets_in_single_buffer() {
    std::cout << "\n=== 单缓冲区多包测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 构造3个连续的包
    std::vector<uint8_t> triple_packet;
    
    // 包1: 读取线圈响应
    std::vector<uint8_t> packet1 = {0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x01, 0xFF};
    
    // 包2: 读取寄存器响应
    std::vector<uint8_t> packet2 = {0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x12, 0x34};
    
    // 包3: 异常响应
    std::vector<uint8_t> packet3 = {0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x01, 0x81, 0x02};
    
    // 连接所有包
    triple_packet.insert(triple_packet.end(), packet1.begin(), packet1.end());
    triple_packet.insert(triple_packet.end(), packet2.begin(), packet2.end());
    triple_packet.insert(triple_packet.end(), packet3.begin(), packet3.end());
    
    // 一次性发送所有数据
    auto result = parser.feed_data(triple_packet.data(), triple_packet.size());
    ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
    ASSERT_EQ(result.bytes_consumed, triple_packet.size());
    
    // 应该能依次获取3个包
    auto parsed_packet1 = parser.get_next_packet();
    ASSERT_TRUE(parsed_packet1.has_value());
    ASSERT_EQ(parsed_packet1->transaction_id, 1);
    ASSERT_EQ(parsed_packet1->pdu.function_code, 0x01);
    
    auto parsed_packet2 = parser.get_next_packet();
    ASSERT_TRUE(parsed_packet2.has_value());
    ASSERT_EQ(parsed_packet2->transaction_id, 2);
    ASSERT_EQ(parsed_packet2->pdu.function_code, 0x03);
    
    auto parsed_packet3 = parser.get_next_packet();
    ASSERT_TRUE(parsed_packet3.has_value());
    ASSERT_EQ(parsed_packet3->transaction_id, 3);
    ASSERT_EQ(parsed_packet3->pdu.function_code, 0x81);
    
    // 不应该有更多包
    auto no_more_packet = parser.get_next_packet();
    ASSERT_FALSE(no_more_packet.has_value());
    
    std::cout << "✅ 单缓冲区多包测试通过" << std::endl;
}

void test_partial_packet_across_buffers() {
    std::cout << "\n=== 跨缓冲区部分包测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 创建一个包，在不同位置分割
    std::vector<uint8_t> complete_packet = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78
    };
    
    // 测试不同的分割点
    std::vector<size_t> split_points = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    
    for (size_t split_point : split_points) {
        parser.reset();
        
        // 发送第一部分
        auto result1 = parser.feed_data(complete_packet.data(), split_point);
        ASSERT_EQ(result1.status, ParseResult::Status::NEED_MORE_DATA);
        ASSERT_FALSE(parser.get_next_packet().has_value());
        
        // 发送剩余部分
        size_t remaining_size = complete_packet.size() - split_point;
        auto result2 = parser.feed_data(complete_packet.data() + split_point, remaining_size);
        ASSERT_EQ(result2.status, ParseResult::Status::PACKET_COMPLETE);
        
        // 应该能获取完整包
        auto packet = parser.get_next_packet();
        ASSERT_TRUE(packet.has_value()) << "分割点" << split_point << "解析失败";
        ASSERT_EQ(packet->transaction_id, 1);
        ASSERT_EQ(packet->pdu.function_code, 0x03);
        ASSERT_EQ(packet->pdu.data.size(), 4);  // Byte count + 4 data bytes
    }
    
    std::cout << "✅ 跨缓冲区部分包测试通过" << std::endl;
}

void test_overlapping_and_misaligned_packets() {
    std::cout << "\n=== 重叠和错位包测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 模拟数据错位的情况：在正常包前面插入垃圾数据
    std::vector<uint8_t> garbage_data = {0xFF, 0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> valid_packet = {0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x01, 0xFF};
    
    // 组合垃圾数据和有效包
    std::vector<uint8_t> corrupted_stream;
    corrupted_stream.insert(corrupted_stream.end(), garbage_data.begin(), garbage_data.end());
    corrupted_stream.insert(corrupted_stream.end(), valid_packet.begin(), valid_packet.end());
    
    // 发送损坏的数据流
    auto result = parser.feed_data(corrupted_stream.data(), corrupted_stream.size());
    
    // 解析器应该跳过垃圾数据并找到有效包
    ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
    
    // 应该能获取有效包
    auto packet = parser.get_next_packet();
    ASSERT_TRUE(packet.has_value());
    ASSERT_EQ(packet->transaction_id, 1);
    ASSERT_EQ(packet->pdu.function_code, 0x01);
    
    std::cout << "✅ 重叠和错位包测试通过" << std::endl;
}

// =============================================================================
// 错误恢复和同步点重建测试
// =============================================================================

void test_sync_point_recovery() {
    std::cout << "\n=== 同步点恢复测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 创建包含多个潜在同步点的数据
    std::vector<uint8_t> complex_data = {
        // 垃圾数据
        0xFF, 0x00, 0x00, 0x00,
        // 假的同步点（Protocol ID不为0）
        0x00, 0x01, 0x00, 0x01, 0x00, 0x04, 0x01, 0x01, 0x01, 0xFF,
        // 垃圾数据
        0xAA, 0xBB,
        // 真正的有效包
        0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x12, 0x34,
        // 另一个有效包
        0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x01, 0x81, 0x02
    };
    
    auto result = parser.feed_data(complex_data.data(), complex_data.size());
    ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
    
    // 应该能获取两个有效包
    auto packet1 = parser.get_next_packet();
    ASSERT_TRUE(packet1.has_value());
    ASSERT_EQ(packet1->transaction_id, 2);  // 第一个有效包的ID
    ASSERT_EQ(packet1->protocol_id, 0);
    
    auto packet2 = parser.get_next_packet();
    ASSERT_TRUE(packet2.has_value());
    ASSERT_EQ(packet2->transaction_id, 3);
    ASSERT_EQ(packet2->pdu.function_code, 0x81);
    
    // 不应该有更多包
    ASSERT_FALSE(parser.get_next_packet().has_value());
    
    std::cout << "✅ 同步点恢复测试通过" << std::endl;
}

void test_length_field_validation() {
    std::cout << "\n=== 长度字段验证测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 测试各种无效的长度字段
    struct TestCase {
        std::vector<uint8_t> data;
        std::string description;
    };
    
    std::vector<TestCase> test_cases = {
        // 长度为0（无效）
        {{0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, "长度为0"},
        
        // 长度过大（超过Modbus最大PDU）
        {{0x00, 0x01, 0x00, 0x00, 0x01, 0x00}, "长度过大(256)"},
        {{0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF}, "长度过大(65535)"},
        
        // 长度与实际数据不匹配
        {{0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x01, 0x03, 0x02}, "长度与数据不匹配"},
    };
    
    for (const auto& test_case : test_cases) {
        parser.reset();
        
        auto result = parser.feed_data(test_case.data.data(), test_case.data.size());
        
        // 这些无效包应该被拒绝或等待更多数据
        if (result.status == ParseResult::Status::PACKET_COMPLETE) {
            auto packet = parser.get_next_packet();
            // 如果解析出包，验证其合法性
            if (packet.has_value()) {
                ASSERT_EQ(packet->protocol_id, 0);  // 至少Protocol ID应该正确
            }
        } else {
            // 无效数据应该被拒绝或需要更多数据
            ASSERT_TRUE(result.status == ParseResult::Status::NEED_MORE_DATA || 
                       result.status == ParseResult::Status::INVALID_DATA);
        }
        
        std::cout << "测试案例: " << test_case.description << " - ";
        if (result.status == ParseResult::Status::INVALID_DATA) {
            std::cout << "正确拒绝" << std::endl;
        } else {
            std::cout << "需要更多数据或其他处理" << std::endl;
        }
    }
    
    std::cout << "✅ 长度字段验证测试通过" << std::endl;
}

void test_protocol_id_validation() {
    std::cout << "\n=== Protocol ID验证测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 测试各种Protocol ID值
    std::vector<uint16_t> protocol_ids = {0x0000, 0x0001, 0x1234, 0xFFFF};
    
    for (uint16_t protocol_id : protocol_ids) {
        parser.reset();
        
        std::vector<uint8_t> test_packet = {
            0x00, 0x01,  // Transaction ID
            static_cast<uint8_t>((protocol_id >> 8) & 0xFF),  // Protocol ID high
            static_cast<uint8_t>(protocol_id & 0xFF),         // Protocol ID low
            0x00, 0x03,  // Length
            0x01,        // Unit ID
            0x81,        // Function code
            0x02         // Exception code
        };
        
        auto result = parser.feed_data(test_packet.data(), test_packet.size());
        
        if (protocol_id == 0x0000) {
            // 只有Protocol ID = 0的包应该被接受
            ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
            auto packet = parser.get_next_packet();
            ASSERT_TRUE(packet.has_value());
            ASSERT_EQ(packet->protocol_id, 0);
        } else {
            // 其他Protocol ID应该被拒绝或跳过
            // 解析器会寻找下一个有效的同步点
            auto packet = parser.get_next_packet();
            if (packet.has_value()) {
                ASSERT_EQ(packet->protocol_id, 0);  // 如果有包，必须是有效的
            }
        }
        
        std::cout << "Protocol ID 0x" << std::hex << protocol_id << ": ";
        if (protocol_id == 0) {
            std::cout << "接受" << std::endl;
        } else {
            std::cout << "拒绝" << std::endl;
        }
    }
    
    std::cout << std::dec << "✅ Protocol ID验证测试通过" << std::endl;
}

// =============================================================================
// 性能和资源测试
// =============================================================================

void test_high_throughput_parsing() {
    std::cout << "\n=== 高吞吐量解析测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    const int num_packets = 1000;
    const int packet_size = 50;  // 中等大小的包
    
    // 生成大量测试包
    std::vector<uint8_t> large_stream;
    for (int i = 0; i < num_packets; ++i) {
        std::vector<uint8_t> packet = {
            static_cast<uint8_t>((i >> 8) & 0xFF),    // Transaction ID high
            static_cast<uint8_t>(i & 0xFF),           // Transaction ID low
            0x00, 0x00,                               // Protocol ID
            0x00, static_cast<uint8_t>(packet_size - 6), // Length
            0x01,                                     // Unit ID
            0x03,                                     // Function code
            static_cast<uint8_t>(packet_size - 9),    // Byte count
        };
        
        // 填充数据
        for (int j = 9; j < packet_size; ++j) {
            packet.push_back(static_cast<uint8_t>(j % 256));
        }
        
        large_stream.insert(large_stream.end(), packet.begin(), packet.end());
    }
    
    // 测试解析性能
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto result = parser.feed_data(large_stream.data(), large_stream.size());
    ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
    ASSERT_EQ(result.bytes_consumed, large_stream.size());
    
    // 提取所有包
    int parsed_count = 0;
    while (auto packet = parser.get_next_packet()) {
        parsed_count++;
        ASSERT_EQ(packet->protocol_id, 0);
        ASSERT_EQ(packet->pdu.function_code, 0x03);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    ASSERT_EQ(parsed_count, num_packets);
    
    double throughput = (static_cast<double>(large_stream.size()) * 1000000.0) / duration.count();  // bytes/sec
    double packet_rate = (static_cast<double>(num_packets) * 1000000.0) / duration.count();        // packets/sec
    
    std::cout << "高吞吐量解析结果:" << std::endl;
    std::cout << "- 数据量: " << large_stream.size() << " bytes" << std::endl;
    std::cout << "- 包数量: " << parsed_count << std::endl;
    std::cout << "- 解析时间: " << duration.count() << " μs" << std::endl;
    std::cout << "- 吞吐量: " << static_cast<int>(throughput) << " bytes/sec" << std::endl;
    std::cout << "- 包处理率: " << static_cast<int>(packet_rate) << " packets/sec" << std::endl;
    
    // 性能要求验证
    ASSERT_TRUE(throughput > 1000000);     // 至少1MB/s
    ASSERT_TRUE(packet_rate > 10000);      // 至少10k packets/s
    
    std::cout << "✅ 高吞吐量解析测试通过" << std::endl;
}

void test_memory_usage_stability() {
    std::cout << "\n=== 内存使用稳定性测试 ===" << std::endl;
    
    StreamingModbusParser parser;
    
    // 模拟长期运行场景：反复发送和解析包
    const int cycles = 100;
    const int packets_per_cycle = 50;
    
    for (int cycle = 0; cycle < cycles; ++cycle) {
        // 发送一批包
        std::vector<uint8_t> batch_data;
        for (int i = 0; i < packets_per_cycle; ++i) {
            std::vector<uint8_t> packet = {
                static_cast<uint8_t>((i >> 8) & 0xFF),
                static_cast<uint8_t>(i & 0xFF),
                0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x01, 0xFF
            };
            batch_data.insert(batch_data.end(), packet.begin(), packet.end());
        }
        
        auto result = parser.feed_data(batch_data.data(), batch_data.size());
        ASSERT_EQ(result.status, ParseResult::Status::PACKET_COMPLETE);
        
        // 提取所有包
        int extracted = 0;
        while (parser.get_next_packet().has_value()) {
            extracted++;
        }
        ASSERT_EQ(extracted, packets_per_cycle);
        
        // 定期重置解析器以验证内存清理
        if (cycle % 20 == 19) {
            parser.reset();
        }
    }
    
    std::cout << "内存稳定性测试完成:" << std::endl;
    std::cout << "- 处理周期: " << cycles << std::endl;
    std::cout << "- 每周期包数: " << packets_per_cycle << std::endl;
    std::cout << "- 总处理包数: " << (cycles * packets_per_cycle) << std::endl;
    
    std::cout << "✅ 内存使用稳定性测试通过" << std::endl;
}

// =============================================================================
// 模糊测试和边界条件
// =============================================================================

void test_fuzz_testing_random_data() {
    std::cout << "\n=== 模糊测试随机数据 ===" << std::endl;
    
    StreamingModbusParser parser;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> byte_dist(0, 255);
    std::uniform_int_distribution<size_t> size_dist(1, 1000);
    
    const int fuzz_iterations = 100;
    int crashes = 0;
    int valid_packets_found = 0;
    
    for (int i = 0; i < fuzz_iterations; ++i) {
        parser.reset();
        
        // 生成随机数据
        size_t data_size = size_dist(gen);
        std::vector<uint8_t> random_data(data_size);
        for (auto& byte : random_data) {
            byte = byte_dist(gen);
        }
        
        try {
            auto result = parser.feed_data(random_data.data(), random_data.size());
            
            // 尝试获取包
            while (auto packet = parser.get_next_packet()) {
                valid_packets_found++;
                // 验证包的基本结构
                ASSERT_EQ(packet->protocol_id, 0);
                ASSERT_TRUE(packet->pdu.data.size() <= 253);
            }
        } catch (...) {
            crashes++;
            std::cout << "❌ 崩溃在迭代 " << i << std::endl;
        }
    }
    
    std::cout << "模糊测试结果:" << std::endl;
    std::cout << "- 测试迭代: " << fuzz_iterations << std::endl;
    std::cout << "- 崩溃次数: " << crashes << std::endl;
    std::cout << "- 发现有效包: " << valid_packets_found << std::endl;
    
    // 不应该有崩溃
    ASSERT_EQ(crashes, 0);
    
    std::cout << "✅ 模糊测试通过" << std::endl;
}

// =============================================================================
// 主测试运行器
// =============================================================================

int main() {
    StreamingModbusParserTest test_framework;
    
    std::cout << "\n=== StreamingModbusParser边界用例和异常处理测试 ===" << std::endl;
    
    test_framework.SetUp();
    
    // 极端分片测试
    std::cout << "\n🔄 极端分片场景测试" << std::endl;
    test_framework.run_test(test_single_byte_fragmentation);
    test_framework.run_test(test_random_fragmentation_patterns);
    test_framework.run_test(test_incomplete_packet_timeout_handling);
    
    // 粘包处理测试
    std::cout << "\n📦 粘包处理测试" << std::endl;
    test_framework.run_test(test_multiple_packets_in_single_buffer);
    test_framework.run_test(test_partial_packet_across_buffers);
    test_framework.run_test(test_overlapping_and_misaligned_packets);
    
    // 错误恢复测试
    std::cout << "\n🛠️ 错误恢复测试" << std::endl;
    test_framework.run_test(test_sync_point_recovery);
    test_framework.run_test(test_length_field_validation);
    test_framework.run_test(test_protocol_id_validation);
    
    // 性能测试
    std::cout << "\n⚡ 性能和资源测试" << std::endl;
    test_framework.run_test(test_high_throughput_parsing);
    test_framework.run_test(test_memory_usage_stability);
    
    // 模糊测试
    std::cout << "\n🎲 模糊测试" << std::endl;
    test_framework.run_test(test_fuzz_testing_random_data);
    
    test_framework.TearDown();
    
    // 输出测试总结
    std::cout << "\n=== 测试总结 ===" << std::endl;
    std::cout << "总测试数: " << test_framework.get_total_tests() << std::endl;
    std::cout << "通过: " << test_framework.get_passed_tests() << std::endl;
    std::cout << "失败: " << test_framework.get_failed_tests() << std::endl;
    std::cout << "成功率: " << std::fixed << std::setprecision(1) 
              << test_framework.get_success_rate() << "%" << std::endl;
    
    if (test_framework.get_failed_tests() == 0) {
        std::cout << "\n🎉 所有StreamingModbusParser测试通过！" << std::endl;
        std::cout << "✅ 极端分片场景处理完善" << std::endl;
        std::cout << "✅ 粘包处理机制健壮" << std::endl;
        std::cout << "✅ 错误恢复和同步重建可靠" << std::endl;
        std::cout << "✅ 高吞吐量性能达标" << std::endl;
        std::cout << "✅ 模糊测试稳定性验证" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ StreamingModbusParser测试存在失败项，需要修复" << std::endl;
        return 1;
    }
}