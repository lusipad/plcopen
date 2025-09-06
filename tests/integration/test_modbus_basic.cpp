/**
 * @file test_modbus_basic.cpp
 * @brief Modbus TCP基础功能测试
 * @version 1.0
 * @date 2025-09-06
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "communication/ModbusTCP.h"

using namespace plc_runtime::communication;

// 简单测试框架
#define TEST_PASS() do { std::cout << " [通过]" << std::endl; return true; } while(0)
#define TEST_FAIL(msg) do { std::cout << " [失败] " << msg << std::endl; return false; } while(0)
#define ASSERT(cond) do { if (!(cond)) TEST_FAIL(#cond); } while(0)

bool test_data_map() {
    std::cout << "测试数据映射基础功能...";
    
    DefaultModbusDataMap data_map;
    
    // 测试线圈
    ASSERT(data_map.write_coil(0, true));
    ASSERT(data_map.read_coil(0) == true);
    
    // 测试寄存器
    ASSERT(data_map.write_holding_register(0, 1234));
    ASSERT(data_map.read_holding_register(0) == 1234);
    
    TEST_PASS();
}

bool test_utilities() {
    std::cout << "测试工具函数...";
    
    // 测试字节转换
    uint16_t value = ModbusUtils::bytes_to_uint16(0x12, 0x34);
    ASSERT(value == 0x1234);
    
    uint8_t high, low;
    ModbusUtils::uint16_to_bytes(0x5678, high, low);
    ASSERT(high == 0x56 && low == 0x78);
    
    // 测试位字节转换
    std::vector<bool> bits = {true, false, true, false, true, false, false, false};
    auto bytes = ModbusUtils::bits_to_bytes(bits);
    ASSERT(bytes.size() == 1);
    ASSERT(bytes[0] == 0x15); // 二进制: 10101000 (LSB在前) = 0x15
    
    auto restored_bits = ModbusUtils::bytes_to_bits(bytes, 8);
    ASSERT(restored_bits.size() == 8);
    ASSERT(restored_bits[0] == true);
    ASSERT(restored_bits[1] == false);
    ASSERT(restored_bits[2] == true);
    
    TEST_PASS();
}

bool test_client_server_basic() {
    std::cout << "测试客户端服务器基础通信...";
    
    // 创建服务器配置
    ModbusTcpServer::Config server_config;
    server_config.port = 15020; // 使用高端口避免权限问题
    server_config.bind_address = "127.0.0.1";
    
    // 创建服务器
    std::unique_ptr<ModbusTcpServer> server;
    try {
        server = std::make_unique<ModbusTcpServer>(server_config);
        
        // 设置数据映射
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        // 启动服务器
        if (!server->start()) {
            TEST_FAIL("服务器启动失败");
        }
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (!server->is_running()) {
            TEST_FAIL("服务器未运行");
        }
        
        // 预设测试数据
        data_map->write_holding_register(0, 9999);
        data_map->write_coil(0, true);
        
        // 创建客户端
        ModbusTcpClient::Config client_config;
        client_config.server_host = "127.0.0.1";
        client_config.server_port = 15020;
        
        ModbusTcpClient client(client_config);
        
        // 连接客户端
        if (!client.connect()) {
            server->stop();
            TEST_FAIL("客户端连接失败");
        }
        
        // 测试读取寄存器
        std::vector<uint16_t> registers;
        if (!client.read_holding_registers(0, 1, registers)) {
            client.disconnect();
            server->stop();
            TEST_FAIL("读取寄存器失败");
        }
        
        ASSERT(registers.size() == 1);
        ASSERT(registers[0] == 9999);
        
        // 测试写入寄存器
        if (!client.write_single_register(1, 5555)) {
            client.disconnect();
            server->stop();
            TEST_FAIL("写入寄存器失败");
        }
        
        // 验证写入
        ASSERT(data_map->read_holding_register(1) == 5555);
        
        // 测试读取线圈
        std::vector<bool> coils;
        if (!client.read_coils(0, 1, coils)) {
            client.disconnect();
            server->stop();
            TEST_FAIL("读取线圈失败");
        }
        
        ASSERT(coils.size() == 1);
        ASSERT(coils[0] == true);
        
        // 测试写入线圈
        if (!client.write_single_coil(1, false)) {
            client.disconnect();
            server->stop();
            TEST_FAIL("写入线圈失败");
        }
        
        // 验证写入
        ASSERT(data_map->read_coil(1) == false);
        
        client.disconnect();
        server->stop();
        
        // 等待清理
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
    } catch (const std::exception& e) {
        if (server) server->stop();
        TEST_FAIL(std::string("异常: ") + e.what());
    }
    
    TEST_PASS();
}

bool test_statistics() {
    std::cout << "测试统计功能...";
    
    ModbusTcpClient::Config config;
    config.server_host = "127.0.0.1";
    config.server_port = 12345; // 不存在的端口，用于测试统计
    
    ModbusTcpClient client(config);
    
    // 尝试连接（应该失败）
    client.connect();
    
    // 获取统计信息
    auto stats = client.get_statistics();
    
    // 验证统计信息结构
    ASSERT(stats.messages_sent >= 0);
    ASSERT(stats.messages_received >= 0);
    ASSERT(stats.errors_occurred >= 0);
    
    TEST_PASS();
}

int main() {
    std::cout << "=== Modbus TCP 基础功能测试 ===" << std::endl;
    
    int passed = 0;
    int total = 0;
    
    // 运行测试
    if (test_data_map()) passed++; total++;
    if (test_utilities()) passed++; total++;
    if (test_client_server_basic()) passed++; total++;
    if (test_statistics()) passed++; total++;
    
    std::cout << "\n=== 测试结果 ===" << std::endl;
    std::cout << "通过: " << passed << "/" << total;
    if (total > 0) {
        std::cout << " (" << (100 * passed / total) << "%)";
    }
    std::cout << std::endl;
    
    if (passed == total) {
        std::cout << "✓ 所有基础测试通过！" << std::endl;
        return 0;
    } else {
        std::cout << "✗ 有测试失败" << std::endl;
        return 1;
    }
}