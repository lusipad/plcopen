/**
 * @file test_modbus_mbap_boundary.cpp
 * @brief Modbus TCP MBAP头部边界值和健壮性测试
 * @version 1.0
 * @date 2025-09-09
 * 
 * 专门测试MBAP头部的各种边界条件、恶意输入和异常场景
 */

#include <gtest/gtest.h>
#include "communication/ModbusTCP.h"
#include <vector>
#include <cstdint>

using namespace plc_runtime::communication;

class ModbusMBAPBoundaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建标准配置的客户端用于测试
        client = std::make_unique<ModbusTcpClient>();
    }
    
    void TearDown() override {
        client.reset();
    }
    
    // 构建MBAP数据包的辅助函数
    std::vector<uint8_t> buildMBAPPacket(
        uint16_t transaction_id,
        uint16_t protocol_id, 
        uint16_t length,
        uint8_t unit_id,
        uint8_t function_code,
        const std::vector<uint8_t>& data = {}) {
        
        std::vector<uint8_t> packet;
        
        // MBAP Header (7 bytes)
        packet.push_back((transaction_id >> 8) & 0xFF);  // Transaction ID高字节
        packet.push_back(transaction_id & 0xFF);         // Transaction ID低字节
        packet.push_back((protocol_id >> 8) & 0xFF);     // Protocol ID高字节
        packet.push_back(protocol_id & 0xFF);            // Protocol ID低字节
        packet.push_back((length >> 8) & 0xFF);          // Length高字节
        packet.push_back(length & 0xFF);                 // Length低字节
        packet.push_back(unit_id);                       // Unit ID
        
        // PDU
        packet.push_back(function_code);                 // Function Code
        packet.insert(packet.end(), data.begin(), data.end()); // Data
        
        return packet;
    }
    
    std::unique_ptr<ModbusTcpClient> client;
};

// 测试Protocol ID边界值
TEST_F(ModbusMBAPBoundaryTest, ProtocolIdBoundaryValues) {
    ModbusTcpADU adu;
    
    // Protocol ID = 0 (合法)
    auto packet_valid = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_TRUE(client->deserialize_adu(packet_valid, adu));
    EXPECT_EQ(adu.protocol_id, 0);
    
    // Protocol ID = 1 (非法)
    auto packet_invalid1 = buildMBAPPacket(0x0001, 0x0001, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_FALSE(client->deserialize_adu(packet_invalid1, adu));
    
    // Protocol ID = 65535 (非法)
    auto packet_invalid2 = buildMBAPPacket(0x0001, 0xFFFF, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_FALSE(client->deserialize_adu(packet_invalid2, adu));
}

// 测试Length字段边界值
TEST_F(ModbusMBAPBoundaryTest, LengthFieldBoundaryValues) {
    ModbusTcpADU adu;
    
    // Length = 2 (最小合法值: Unit ID + Function Code)
    auto packet_min = buildMBAPPacket(0x0001, 0x0000, 0x0002, 0x01, 0x03);
    EXPECT_TRUE(client->deserialize_adu(packet_min, adu));
    EXPECT_EQ(adu.length, 2);
    
    // Length = 1 (非法: 小于最小值)
    auto packet_too_small = buildMBAPPacket(0x0001, 0x0000, 0x0001, 0x01, 0x03);
    EXPECT_FALSE(client->deserialize_adu(packet_too_small, adu));
    
    // Length = 0 (非法)
    auto packet_zero = buildMBAPPacket(0x0001, 0x0000, 0x0000, 0x01, 0x03);
    EXPECT_FALSE(client->deserialize_adu(packet_zero, adu));
    
    // Length = 255 (最大合法值)
    std::vector<uint8_t> max_data(253, 0x42); // 255 - 1(Unit ID) - 1(Function Code) = 253
    auto packet_max = buildMBAPPacket(0x0001, 0x0000, 0x00FF, 0x01, 0x03, max_data);
    EXPECT_TRUE(client->deserialize_adu(packet_max, adu));
    EXPECT_EQ(adu.length, 255);
    
    // Length = 256 (非法: 超过最大值)
    auto packet_too_large = buildMBAPPacket(0x0001, 0x0000, 0x0100, 0x01, 0x03, max_data);
    EXPECT_FALSE(client->deserialize_adu(packet_too_large, adu));
}

// 测试Length与实际数据长度不一致的情况
TEST_F(ModbusMBAPBoundaryTest, LengthDataMismatch) {
    ModbusTcpADU adu;
    
    // Length声明6字节，但实际提供4字节数据
    std::vector<uint8_t> packet = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x01, 0x03, {0x00, 0x01}); // 只有2字节数据
    EXPECT_FALSE(client->deserialize_adu(packet, adu));
    
    // Length声明4字节，但实际提供6字节数据  
    packet = buildMBAPPacket(0x0001, 0x0000, 0x0004, 0x01, 0x03, {0x00, 0x01, 0x02, 0x03}); // 4字节数据但Length指示4
    EXPECT_FALSE(client->deserialize_adu(packet, adu));
    
    // 正确匹配的情况
    packet = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01}); // Length=6, 实际PDU=5字节
    EXPECT_TRUE(client->deserialize_adu(packet, adu));
}

// 测试Unit ID边界值
TEST_F(ModbusMBAPBoundaryTest, UnitIdBoundaryValues) {
    ModbusTcpADU adu;
    
    // Unit ID = 1 (合法)
    auto packet_valid = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_TRUE(client->deserialize_adu(packet_valid, adu));
    EXPECT_EQ(adu.unit_id, 1);
    
    // Unit ID = 247 (最大合法值)
    packet_valid = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0xF7, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_TRUE(client->deserialize_adu(packet_valid, adu));
    EXPECT_EQ(adu.unit_id, 247);
    
    // Unit ID = 0 (广播地址，在严格模式下可能被拒绝)
    auto packet_broadcast = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x00, 0x03, {0x00, 0x00, 0x00, 0x01});
    // 当前实现允许Unit ID = 0，但可以通过配置拒绝
    EXPECT_TRUE(client->deserialize_adu(packet_broadcast, adu));
    
    // Unit ID = 255 (保留地址)
    auto packet_reserved = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0xFF, 0x03, {0x00, 0x00, 0x00, 0x01});
    EXPECT_TRUE(client->deserialize_adu(packet_reserved, adu)); // 当前宽松策略
}

// 测试MBAP数据包最小长度
TEST_F(ModbusMBAPBoundaryTest, MinimumPacketSize) {
    ModbusTcpADU adu;
    
    // 7字节MBAP头部 + 1字节功能码 = 8字节最小包
    std::vector<uint8_t> min_packet = {0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 0x03};
    EXPECT_TRUE(client->deserialize_adu(min_packet, adu));
    
    // 7字节包（缺少功能码）
    std::vector<uint8_t> too_small = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01};
    EXPECT_FALSE(client->deserialize_adu(too_small, adu));
    
    // 6字节包（缺少Unit ID和功能码）
    std::vector<uint8_t> way_too_small = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
    EXPECT_FALSE(client->deserialize_adu(way_too_small, adu));
    
    // 空包
    std::vector<uint8_t> empty_packet;
    EXPECT_FALSE(client->deserialize_adu(empty_packet, adu));
}

// 测试最大数据包大小
TEST_F(ModbusMBAPBoundaryTest, MaximumPacketSize) {
    ModbusTcpADU adu;
    
    // Modbus TCP最大ADU = 260字节 (MBAP头部7字节 + 最大PDU 253字节)
    static constexpr size_t MAX_MODBUS_ADU_SIZE = 260;
    
    // 构建最大合法包
    std::vector<uint8_t> max_data(246, 0x42); // 253 - 1(Unit ID) - 1(Function Code) - 5(示例数据头) = 246
    std::vector<uint8_t> pdu_data = {0x00, 0x00, 0x00, 0xF6}; // 读取246个寄存器
    pdu_data.insert(pdu_data.end(), max_data.begin(), max_data.end());
    
    auto max_packet = buildMBAPPacket(0x0001, 0x0000, 0x00FF, 0x01, 0x03, pdu_data);
    EXPECT_EQ(max_packet.size(), MAX_MODBUS_ADU_SIZE);
    EXPECT_TRUE(client->deserialize_adu(max_packet, adu));
    
    // 超过最大大小的包
    std::vector<uint8_t> oversized_data(260, 0x42);
    max_packet.insert(max_packet.end(), oversized_data.begin(), oversized_data.end());
    EXPECT_FALSE(client->deserialize_adu(max_packet, adu));
}

// 测试截断数据包
TEST_F(ModbusMBAPBoundaryTest, TruncatedPackets) {
    ModbusTcpADU adu;
    
    // 构建完整的包然后截断不同部分
    auto complete_packet = buildMBAPPacket(0x0001, 0x0000, 0x0006, 0x01, 0x03, {0x00, 0x00, 0x00, 0x01});
    
    // 截断在Transaction ID
    auto truncated1 = std::vector<uint8_t>(complete_packet.begin(), complete_packet.begin() + 1);
    EXPECT_FALSE(client->deserialize_adu(truncated1, adu));
    
    // 截断在Protocol ID
    auto truncated2 = std::vector<uint8_t>(complete_packet.begin(), complete_packet.begin() + 3);
    EXPECT_FALSE(client->deserialize_adu(truncated2, adu));
    
    // 截断在Length字段
    auto truncated3 = std::vector<uint8_t>(complete_packet.begin(), complete_packet.begin() + 5);
    EXPECT_FALSE(client->deserialize_adu(truncated3, adu));
    
    // 截断在Unit ID
    auto truncated4 = std::vector<uint8_t>(complete_packet.begin(), complete_packet.begin() + 6);
    EXPECT_FALSE(client->deserialize_adu(truncated4, adu));
    
    // 截断在PDU数据中间
    auto truncated5 = std::vector<uint8_t>(complete_packet.begin(), complete_packet.begin() + 10);
    EXPECT_FALSE(client->deserialize_adu(truncated5, adu));
}

// 压力测试：大量边界值组合
TEST_F(ModbusMBAPBoundaryTest, StressBoundaryValueCombinations) {
    ModbusTcpADU adu;
    
    // 测试所有Transaction ID边界值与其他参数的组合
    std::vector<uint16_t> transaction_ids = {0x0000, 0x0001, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF};
    std::vector<uint8_t> unit_ids = {0x01, 0x10, 0x7F, 0xF7};
    std::vector<uint8_t> function_codes = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0F, 0x10};
    
    int valid_combinations = 0;
    int total_combinations = 0;
    
    for (auto tid : transaction_ids) {
        for (auto uid : unit_ids) {
            for (auto fc : function_codes) {
                total_combinations++;
                
                auto packet = buildMBAPPacket(tid, 0x0000, 0x0006, uid, fc, {0x00, 0x00, 0x00, 0x01});
                
                if (client->deserialize_adu(packet, adu)) {
                    valid_combinations++;
                    EXPECT_EQ(adu.transaction_id, tid);
                    EXPECT_EQ(adu.unit_id, uid);
                    EXPECT_EQ(adu.pdu.function_code, fc);
                }
            }
        }
    }
    
    // 验证所有合法组合都通过了测试
    EXPECT_EQ(valid_combinations, total_combinations);
    EXPECT_GT(total_combinations, 100); // 确保测试了足够多的组合
}

// 运行所有测试的主函数
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}