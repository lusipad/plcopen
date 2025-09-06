/**
 * @file test_modbus_tcp.cpp
 * @brief Modbus TCP通信协议集成测试
 * @version 1.0
 * @date 2025-09-06
 * 
 * 测试完整的Modbus TCP客户端和服务器功能，包括：
 * - 客户端-服务器通信
 * - 各种Modbus功能码
 * - 数据映射和存储
 * - 错误处理和异常响应
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <memory>
#include "communication/ModbusTCP.h"

using namespace plc_runtime::communication;

// 测试框架宏定义
#define ASSERT_TRUE(condition) \
    do { if (!(condition)) { \
        std::cout << "断言失败: " << #condition << " 在 " << __LINE__ << " 行" << std::endl; \
        return false; \
    } } while(0)

#define ASSERT_EQ(expected, actual) \
    do { if ((expected) != (actual)) { \
        std::cout << "断言失败: 期望 " << (expected) << ", 实际 " << (actual) << " 在 " << __LINE__ << " 行" << std::endl; \
        return false; \
    } } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

class ModbusTcpIntegrationTest {
public:
    void run_all_tests() {
        std::cout << "=== Modbus TCP 集成测试套件 ===" << std::endl;
        
        int passed = 0, total = 0;
        
        // 基础功能测试
        if (test_data_map_basic_operations()) { passed++; } total++;
        if (test_server_startup_shutdown()) { passed++; } total++;
        if (test_client_connection()) { passed++; } total++;
        
        // 读取功能测试
        if (test_read_holding_registers()) { passed++; } total++;
        if (test_read_coils()) { passed++; } total++;
        if (test_read_input_registers()) { passed++; } total++;
        if (test_read_discrete_inputs()) { passed++; } total++;
        
        // 写入功能测试
        if (test_write_single_register()) { passed++; } total++;
        if (test_write_single_coil()) { passed++; } total++;
        if (test_write_multiple_registers()) { passed++; } total++;
        if (test_write_multiple_coils()) { passed++; } total++;
        
        // 错误处理测试
        if (test_invalid_address_handling()) { passed++; } total++;
        if (test_exception_responses()) { passed++; } total++;
        
        // 并发和性能测试
        if (test_multiple_clients()) { passed++; } total++;
        if (test_performance_benchmark()) { passed++; } total++;
        
        std::cout << "\n=== 测试结果 ===" << std::endl;
        std::cout << "通过: " << passed << "/" << total << " (" 
                  << (100.0 * passed / total) << "%)" << std::endl;
        
        if (passed == total) {
            std::cout << "🎉 所有测试通过！" << std::endl;
        } else {
            std::cout << "❌ 有 " << (total - passed) << " 个测试失败" << std::endl;
        }
    }

private:
    // 基础数据映射测试
    bool test_data_map_basic_operations() {
        std::cout << "测试基础数据映射操作..." << std::endl;
        
        DefaultModbusDataMap::Config config;
        config.max_coils = 100;
        config.max_holding_registers = 100;
        
        DefaultModbusDataMap data_map(config);
        
        // 测试线圈操作
        ASSERT_TRUE(data_map.write_coil(0, true));
        ASSERT_TRUE(data_map.read_coil(0));
        ASSERT_TRUE(data_map.write_coil(0, false));
        ASSERT_FALSE(data_map.read_coil(0));
        
        // 测试寄存器操作
        ASSERT_TRUE(data_map.write_holding_register(0, 1234));
        ASSERT_EQ(1234, data_map.read_holding_register(0));
        
        // 测试批量操作
        std::vector<bool> coils = {true, false, true, true, false};
        ASSERT_TRUE(data_map.write_coils(10, coils));
        
        std::vector<bool> read_coils;
        ASSERT_TRUE(data_map.read_coils(10, 5, read_coils));
        ASSERT_EQ(5, read_coils.size());
        ASSERT_TRUE(read_coils[0]);
        ASSERT_FALSE(read_coils[1]);
        ASSERT_TRUE(read_coils[2]);
        
        return true;
    }
    
    // 服务器启动和关闭测试
    bool test_server_startup_shutdown() {
        std::cout << "测试服务器启动和关闭..." << std::endl;
        
        ModbusTcpServer::Config server_config;
        server_config.port = 5020; // 使用非标准端口避免冲突
        server_config.bind_address = "127.0.0.1";
        
        ModbusTcpServer server(server_config);
        
        // 测试启动
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(server.is_running());
        
        // 等待一段时间确保服务器稳定运行
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 测试关闭
        server.stop();
        ASSERT_FALSE(server.is_running());
        
        return true;
    }
    
    // 客户端连接测试
    bool test_client_connection() {
        std::cout << "测试客户端连接..." << std::endl;
        
        // 启动服务器
        ModbusTcpServer::Config server_config;
        server_config.port = 5021;
        server_config.bind_address = "127.0.0.1";
        
        ModbusTcpServer server(server_config);
        ASSERT_TRUE(server.start());
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // 创建客户端
        ModbusTcpClient::Config client_config;
        client_config.server_host = "127.0.0.1";
        client_config.server_port = 5021;
        
        ModbusTcpClient client(client_config);
        
        // 测试连接
        ASSERT_TRUE(client.connect());
        ASSERT_TRUE(client.is_connected());
        
        // 测试断开
        client.disconnect();
        ASSERT_FALSE(client.is_connected());
        
        server.stop();
        return true;
    }
    
    // 读取保持寄存器测试
    bool test_read_holding_registers() {
        std::cout << "测试读取保持寄存器..." << std::endl;
        
        // 设置服务器
        auto server = setup_test_server(5022);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        // 预设一些测试数据
        data_map->write_holding_register(0, 1000);
        data_map->write_holding_register(1, 2000);
        data_map->write_holding_register(2, 3000);
        
        // 创建客户端
        auto client = setup_test_client(5022);
        
        // 读取寄存器
        std::vector<uint16_t> values;
        ASSERT_TRUE(client->read_holding_registers(0, 3, values));
        
        ASSERT_EQ(3, values.size());
        ASSERT_EQ(1000, values[0]);
        ASSERT_EQ(2000, values[1]);
        ASSERT_EQ(3000, values[2]);
        
        cleanup_test_server(server);
        return true;
    }
    
    // 读取线圈测试
    bool test_read_coils() {
        std::cout << "测试读取线圈..." << std::endl;
        
        auto server = setup_test_server(5023);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        // 设置线圈状态
        data_map->write_coil(0, true);
        data_map->write_coil(1, false);
        data_map->write_coil(2, true);
        data_map->write_coil(3, true);
        data_map->write_coil(4, false);
        
        auto client = setup_test_client(5023);
        
        std::vector<bool> coils;
        ASSERT_TRUE(client->read_coils(0, 5, coils));
        
        ASSERT_EQ(5, coils.size());
        ASSERT_TRUE(coils[0]);
        ASSERT_FALSE(coils[1]);
        ASSERT_TRUE(coils[2]);
        ASSERT_TRUE(coils[3]);
        ASSERT_FALSE(coils[4]);
        
        cleanup_test_server(server);
        return true;
    }
    
    // 读取输入寄存器测试
    bool test_read_input_registers() {
        std::cout << "测试读取输入寄存器..." << std::endl;
        
        auto server = setup_test_server(5024);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5024);
        
        // 输入寄存器通常是只读的，这里测试读取功能
        std::vector<uint16_t> values;
        ASSERT_TRUE(client->read_input_registers(0, 2, values));
        ASSERT_EQ(2, values.size());
        
        cleanup_test_server(server);
        return true;
    }
    
    // 读取离散输入测试
    bool test_read_discrete_inputs() {
        std::cout << "测试读取离散输入..." << std::endl;
        
        auto server = setup_test_server(5025);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5025);
        
        std::vector<bool> inputs;
        ASSERT_TRUE(client->read_discrete_inputs(0, 8, inputs));
        ASSERT_EQ(8, inputs.size());
        
        cleanup_test_server(server);
        return true;
    }
    
    // 写单个寄存器测试
    bool test_write_single_register() {
        std::cout << "测试写单个寄存器..." << std::endl;
        
        auto server = setup_test_server(5026);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5026);
        
        // 写入寄存器
        ASSERT_TRUE(client->write_single_register(10, 5555));
        
        // 验证写入成功
        ASSERT_EQ(5555, data_map->read_holding_register(10));
        
        cleanup_test_server(server);
        return true;
    }
    
    // 写单个线圈测试
    bool test_write_single_coil() {
        std::cout << "测试写单个线圈..." << std::endl;
        
        auto server = setup_test_server(5027);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5027);
        
        // 写入线圈
        ASSERT_TRUE(client->write_single_coil(5, true));
        ASSERT_TRUE(data_map->read_coil(5));
        
        ASSERT_TRUE(client->write_single_coil(5, false));
        ASSERT_FALSE(data_map->read_coil(5));
        
        cleanup_test_server(server);
        return true;
    }
    
    // 写多个寄存器测试
    bool test_write_multiple_registers() {
        std::cout << "测试写多个寄存器..." << std::endl;
        
        auto server = setup_test_server(5028);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5028);
        
        std::vector<uint16_t> values = {100, 200, 300, 400, 500};
        ASSERT_TRUE(client->write_multiple_registers(20, values));
        
        // 验证写入
        for (size_t i = 0; i < values.size(); i++) {
            ASSERT_EQ(values[i], data_map->read_holding_register(20 + i));
        }
        
        cleanup_test_server(server);
        return true;
    }
    
    // 写多个线圈测试
    bool test_write_multiple_coils() {
        std::cout << "测试写多个线圈..." << std::endl;
        
        auto server = setup_test_server(5029);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5029);
        
        std::vector<bool> coils = {true, false, true, false, true, true, false, false};
        ASSERT_TRUE(client->write_multiple_coils(30, coils));
        
        // 验证写入
        for (size_t i = 0; i < coils.size(); i++) {
            ASSERT_EQ(coils[i], data_map->read_coil(30 + i));
        }
        
        cleanup_test_server(server);
        return true;
    }
    
    // 无效地址处理测试
    bool test_invalid_address_handling() {
        std::cout << "测试无效地址处理..." << std::endl;
        
        auto server = setup_test_server(5030);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5030);
        
        // 尝试访问超出范围的地址
        std::vector<uint16_t> values;
        ASSERT_FALSE(client->read_holding_registers(9999, 10, values));
        
        // 尝试写入超出范围的地址
        ASSERT_FALSE(client->write_single_register(9999, 123));
        
        cleanup_test_server(server);
        return true;
    }
    
    // 异常响应测试
    bool test_exception_responses() {
        std::cout << "测试异常响应..." << std::endl;
        
        auto server = setup_test_server(5031);
        auto client = setup_test_client(5031);
        
        // 测试无效功能码（通过直接访问底层可能需要扩展接口）
        // 这里测试数量为0的情况，应该产生异常
        std::vector<uint16_t> values;
        ASSERT_FALSE(client->read_holding_registers(0, 0, values)); // 数量为0应该失败
        
        cleanup_test_server(server);
        return true;
    }
    
    // 多客户端并发测试
    bool test_multiple_clients() {
        std::cout << "测试多客户端并发访问..." << std::endl;
        
        auto server = setup_test_server(5032);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        // 创建多个客户端线程
        std::vector<std::thread> client_threads;
        std::atomic<int> success_count{0};
        
        for (int i = 0; i < 3; i++) {
            client_threads.emplace_back([&success_count, i]() {
                ModbusTcpClient::Config config;
                config.server_host = "127.0.0.1";
                config.server_port = 5032;
                
                ModbusTcpClient client(config);
                if (client.connect()) {
                    // 每个客户端写入不同的值
                    if (client.write_single_register(i, 1000 + i)) {
                        success_count++;
                    }
                }
            });
        }
        
        // 等待所有客户端完成
        for (auto& thread : client_threads) {
            thread.join();
        }
        
        ASSERT_EQ(3, success_count.load());
        
        // 验证数据
        for (int i = 0; i < 3; i++) {
            ASSERT_EQ(1000 + i, data_map->read_holding_register(i));
        }
        
        cleanup_test_server(server);
        return true;
    }
    
    // 性能基准测试
    bool test_performance_benchmark() {
        std::cout << "测试性能基准..." << std::endl;
        
        auto server = setup_test_server(5033);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server->set_data_map(data_map);
        
        auto client = setup_test_client(5033);
        
        // 性能测试：1000次寄存器读写
        const int iterations = 1000;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; i++) {
            ASSERT_TRUE(client->write_single_register(0, i));
            
            std::vector<uint16_t> values;
            ASSERT_TRUE(client->read_holding_registers(0, 1, values));
            ASSERT_EQ(i, values[0]);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        std::cout << "  性能统计:" << std::endl;
        std::cout << "  - 总时间: " << duration.count() << " 微秒" << std::endl;
        std::cout << "  - 每次操作: " << duration.count() / (iterations * 2) << " 微秒" << std::endl;
        std::cout << "  - 操作/秒: " << (iterations * 2 * 1000000) / duration.count() << std::endl;
        
        // 性能断言（每次操作应该在合理时间内完成）
        ASSERT_TRUE(duration.count() / (iterations * 2) < 1000); // 每次操作少于1ms
        
        cleanup_test_server(server);
        return true;
    }
    
    // 辅助函数：创建测试服务器
    std::shared_ptr<ModbusTcpServer> setup_test_server(uint16_t port) {
        ModbusTcpServer::Config config;
        config.port = port;
        config.bind_address = "127.0.0.1";
        
        auto server = std::make_shared<ModbusTcpServer>(config);
        server->start();
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        return server;
    }
    
    // 辅助函数：创建测试客户端
    std::shared_ptr<ModbusTcpClient> setup_test_client(uint16_t port) {
        ModbusTcpClient::Config config;
        config.server_host = "127.0.0.1";
        config.server_port = port;
        
        auto client = std::make_shared<ModbusTcpClient>(config);
        client->connect();
        
        // 等待连接建立
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        return client;
    }
    
    // 辅助函数：清理测试服务器
    void cleanup_test_server(std::shared_ptr<ModbusTcpServer> server) {
        server->stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

int main() {
    try {
        ModbusTcpIntegrationTest test_suite;
        test_suite.run_all_tests();
        
        std::cout << "\n=== Modbus TCP 集成测试完成 ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "测试执行异常: " << e.what() << std::endl;
        return 1;
    }
}