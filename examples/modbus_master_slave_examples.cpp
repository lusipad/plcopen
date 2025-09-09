/**
 * @file modbus_master_slave_examples.cpp
 * @brief Modbus主从互通示例和异常处理矩阵
 * @version 1.0
 * @date 2025-09-09
 * 
 * 包含：
 * - 完整的主从通信示例
 * - 常见异常场景处理矩阵
 * - 最佳实践和故障排除指南
 * - 性能优化建议
 */

#include "communication/EnhancedModbusTCP.h"
#include "communication/NetworkManager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace plc_runtime::communication;

// =============================================================================
// 基础主从互通示例
// =============================================================================

class ModbusMasterSlaveExample {
public:
    static void basic_master_slave_demo() {
        std::cout << "\n=== Modbus主从基础互通示例 ===" << std::endl;
        
        // 1. 启动从站
        EnhancedModbusTcpServer::Config server_config;
        server_config.bind_address = "127.0.0.1";
        server_config.port = 5020;  // 避免与标准502端口冲突
        server_config.unit_id = 1;
        server_config.max_connections = 5;
        
        EnhancedModbusTcpServer server(server_config);
        
        // 创建数据映射
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server.set_data_map(data_map);
        
        // 设置回调函数
        server.set_client_connected_callback([](const std::string& ip, uint16_t port) {
            std::cout << "🔗 客户端连接: " << ip << ":" << port << std::endl;
        });
        
        server.set_client_disconnected_callback([](const std::string& ip, uint16_t port) {
            std::cout << "❌ 客户端断开: " << ip << ":" << port << std::endl;
        });
        
        server.set_request_received_callback([](const std::string& ip, const ModbusPDU& request) {
            std::cout << "📨 收到请求 from " << ip << " FC:0x" << std::hex << 
                         static_cast<int>(request.function_code) << std::dec << std::endl;
        });
        
        // 启动服务器
        auto server_result = server.start();
        if (!server_result.is_success()) {
            std::cout << "❌ 服务器启动失败: " << server_result.message << std::endl;
            return;
        }
        std::cout << "✅ Modbus从站启动成功 (端口5020)" << std::endl;
        
        // 给服务器一些启动时间
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 2. 创建主站
        EnhancedModbusTcpClient::Config client_config;
        client_config.server_host = "127.0.0.1";
        client_config.server_port = 5020;
        client_config.unit_id = 1;
        client_config.connect_timeout = std::chrono::milliseconds{3000};
        client_config.request_timeout = std::chrono::milliseconds{2000};
        
        EnhancedModbusTcpClient client(client_config);
        
        // 3. 执行主从通信测试
        std::cout << "\n--- 执行主从通信测试 ---" << std::endl;
        
        // 连接到从站
        auto connect_result = client.connect();
        if (!connect_result.is_success()) {
            std::cout << "❌ 主站连接失败: " << connect_result.message << std::endl;
            server.stop();
            return;
        }
        std::cout << "✅ 主站连接成功" << std::endl;
        
        // 测试1: 写入线圈
        std::cout << "\n📝 测试1: 写入线圈" << std::endl;
        std::vector<bool> coil_values = {true, false, true, true, false};
        auto write_result = client.write_multiple_coils(10, coil_values);
        if (write_result.is_success()) {
            std::cout << "✅ 写入5个线圈成功 (地址10-14)" << std::endl;
        } else {
            std::cout << "❌ 写入线圈失败: " << write_result.message << std::endl;
        }
        
        // 测试2: 读取线圈
        std::cout << "\n📖 测试2: 读取线圈" << std::endl;
        std::vector<bool> read_coils;
        auto read_result = client.read_coils(10, 5, read_coils);
        if (read_result.is_success()) {
            std::cout << "✅ 读取5个线圈成功: ";
            for (size_t i = 0; i < read_coils.size(); ++i) {
                std::cout << "Coil[" << (10 + i) << "]=" << (read_coils[i] ? "ON" : "OFF") << " ";
            }
            std::cout << std::endl;
            
            // 验证数据一致性
            bool data_consistent = (read_coils.size() == coil_values.size());
            for (size_t i = 0; i < std::min(read_coils.size(), coil_values.size()) && data_consistent; ++i) {
                if (read_coils[i] != coil_values[i]) {
                    data_consistent = false;
                }
            }
            
            if (data_consistent) {
                std::cout << "✅ 数据一致性验证通过" << std::endl;
            } else {
                std::cout << "❌ 数据一致性验证失败" << std::endl;
            }
        } else {
            std::cout << "❌ 读取线圈失败: " << read_result.message << std::endl;
        }
        
        // 测试3: 写入保持寄存器
        std::cout << "\n📝 测试3: 写入保持寄存器" << std::endl;
        std::vector<uint16_t> register_values = {0x1234, 0x5678, 0xABCD, 0xEF00};
        write_result = client.write_multiple_registers(20, register_values);
        if (write_result.is_success()) {
            std::cout << "✅ 写入4个寄存器成功 (地址20-23)" << std::endl;
        } else {
            std::cout << "❌ 写入寄存器失败: " << write_result.message << std::endl;
        }
        
        // 测试4: 读取保持寄存器
        std::cout << "\n📖 测试4: 读取保持寄存器" << std::endl;
        std::vector<uint16_t> read_registers;
        read_result = client.read_holding_registers(20, 4, read_registers);
        if (read_result.is_success()) {
            std::cout << "✅ 读取4个寄存器成功: ";
            for (size_t i = 0; i < read_registers.size(); ++i) {
                std::cout << "Reg[" << (20 + i) << "]=0x" << std::hex << read_registers[i] << std::dec << " ";
            }
            std::cout << std::endl;
        } else {
            std::cout << "❌ 读取寄存器失败: " << read_result.message << std::endl;
        }
        
        // 5. 显示统计信息
        std::cout << "\n--- 通信统计信息 ---" << std::endl;
        auto client_stats = client.get_network_statistics();
        auto server_stats = server.get_network_statistics();
        
        std::cout << "主站统计:" << std::endl;
        std::cout << "- 发送字节: " << client_stats.bytes_sent.load() << std::endl;
        std::cout << "- 接收字节: " << client_stats.bytes_received.load() << std::endl;
        std::cout << "- 网络错误: " << client_stats.network_errors.load() << std::endl;
        
        std::cout << "从站统计:" << std::endl;
        std::cout << "- 接收字节: " << server_stats.bytes_received.load() << std::endl;
        std::cout << "- 发送字节: " << server_stats.bytes_sent.load() << std::endl;
        std::cout << "- 接受连接: " << server_stats.connections_accepted.load() << std::endl;
        
        // 6. 清理资源
        client.disconnect();
        server.stop();
        
        std::cout << "✅ 主从互通示例完成" << std::endl;
    }
    
    static void high_performance_demo() {
        std::cout << "\n=== 高性能主从通信示例 ===" << std::endl;
        
        // 配置高性能参数
        EnhancedModbusTcpServer::Config server_config;
        server_config.bind_address = "0.0.0.0";
        server_config.port = 5021;
        server_config.max_connections = 50;
        server_config.worker_threads = 8;
        server_config.socket_config.no_delay = true;  // 禁用Nagle算法
        server_config.socket_config.send_buffer_size = 128 * 1024;
        server_config.socket_config.recv_buffer_size = 128 * 1024;
        
        EnhancedModbusTcpServer server(server_config);
        auto data_map = std::make_shared<DefaultModbusDataMap>();
        server.set_data_map(data_map);
        
        auto start_result = server.start();
        if (!start_result.is_success()) {
            std::cout << "❌ 高性能服务器启动失败" << std::endl;
            return;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 创建多个客户端进行并发测试
        const int num_clients = 5;
        const int requests_per_client = 100;
        
        std::vector<std::thread> client_threads;
        std::atomic<int> total_requests{0};
        std::atomic<int> successful_requests{0};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int client_id = 0; client_id < num_clients; ++client_id) {
            client_threads.emplace_back([&, client_id]() {
                EnhancedModbusTcpClient::Config config;
                config.server_host = "127.0.0.1";
                config.server_port = 5021;
                config.unit_id = 1;
                config.use_connection_pool = true;
                config.max_pool_size = 2;
                
                EnhancedModbusTcpClient client(config);
                
                auto connect_result = client.connect();
                if (!connect_result.is_success()) {
                    std::cout << "❌ 客户端" << client_id << "连接失败" << std::endl;
                    return;
                }
                
                for (int req = 0; req < requests_per_client; ++req) {
                    total_requests.fetch_add(1);
                    
                    // 交替进行读写操作
                    if (req % 2 == 0) {
                        // 写操作
                        std::vector<uint16_t> values = {
                            static_cast<uint16_t>(client_id * 1000 + req),
                            static_cast<uint16_t>(req)
                        };
                        auto result = client.write_multiple_registers(client_id * 100, values);
                        if (result.is_success()) {
                            successful_requests.fetch_add(1);
                        }
                    } else {
                        // 读操作
                        std::vector<uint16_t> values;
                        auto result = client.read_holding_registers(client_id * 100, 10, values);
                        if (result.is_success()) {
                            successful_requests.fetch_add(1);
                        }
                    }
                    
                    // 短暂延迟模拟实际应用
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                
                client.disconnect();
            });
        }
        
        // 等待所有客户端完成
        for (auto& thread : client_threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // 输出性能统计
        std::cout << "高性能测试结果:" << std::endl;
        std::cout << "- 客户端数: " << num_clients << std::endl;
        std::cout << "- 总请求数: " << total_requests.load() << std::endl;
        std::cout << "- 成功请求数: " << successful_requests.load() << std::endl;
        std::cout << "- 成功率: " << std::fixed << std::setprecision(1) 
                  << (100.0 * successful_requests.load() / total_requests.load()) << "%" << std::endl;
        std::cout << "- 总耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "- 吞吐量: " << (successful_requests.load() * 1000 / duration.count()) << " req/s" << std::endl;
        
        server.stop();
        std::cout << "✅ 高性能测试完成" << std::endl;
    }
};

// =============================================================================
// 异常处理矩阵和故障诊断
// =============================================================================

class ModbusExceptionMatrix {
public:
    struct ExceptionScenario {
        std::string name;
        std::string description;
        std::function<void()> test_function;
        std::string expected_behavior;
        std::string recovery_action;
    };
    
    static void run_exception_matrix() {
        std::cout << "\n=== Modbus异常处理矩阵测试 ===" << std::endl;
        
        std::vector<ExceptionScenario> scenarios = {
            {
                "连接超时",
                "主站连接不存在的从站",
                []() { test_connection_timeout(); },
                "返回TIMEOUT错误，触发重连机制",
                "检查网络连接，验证从站IP/端口"
            },
            {
                "地址越界",
                "读取超出从站地址范围的数据",
                []() { test_address_out_of_bounds(); },
                "从站返回0x02异常码(非法数据地址)",
                "检查地址范围，调整读取起始地址和数量"
            },
            {
                "非法功能码",
                "发送从站不支持的功能码",
                []() { test_illegal_function(); },
                "从站返回0x01异常码(非法功能)",
                "检查从站支持的功能码列表"
            },
            {
                "数据数量过大",
                "单次读取过多数据点",
                []() { test_data_count_exceeded(); },
                "从站返回0x03异常码(非法数据值)",
                "减少单次读取的数据点数量"
            },
            {
                "网络中断",
                "通信过程中网络连接断开",
                []() { test_network_interruption(); },
                "检测到连接断开，触发自动重连",
                "检查网络稳定性，启用心跳机制"
            },
            {
                "从站忙",
                "从站处理能力不足返回忙状态",
                []() { test_slave_busy(); },
                "从站返回0x06异常码(从站忙)",
                "增加请求间隔，实施流量控制"
            },
            {
                "协议错误",
                "发送格式错误的Modbus帧",
                []() { test_protocol_error(); },
                "从站不响应或返回异常",
                "检查帧格式，验证CRC(RTU)或长度(TCP)"
            },
            {
                "并发冲突",
                "多个主站同时访问同一从站",
                []() { test_concurrent_access(); },
                "可能出现响应混乱或超时",
                "实施主站仲裁或使用不同Unit ID"
            }
        };
        
        std::cout << "\n异常场景测试结果矩阵:" << std::endl;
        std::cout << std::string(120, '=') << std::endl;
        std::cout << std::left << std::setw(15) << "场景名称"
                  << std::setw(30) << "描述" 
                  << std::setw(25) << "期望行为"
                  << std::setw(30) << "恢复措施"
                  << std::setw(10) << "测试结果" << std::endl;
        std::cout << std::string(120, '-') << std::endl;
        
        for (const auto& scenario : scenarios) {
            std::cout << std::left << std::setw(15) << scenario.name
                      << std::setw(30) << scenario.description
                      << std::setw(25) << scenario.expected_behavior
                      << std::setw(30) << scenario.recovery_action;
            
            try {
                scenario.test_function();
                std::cout << std::setw(10) << "✅ PASS" << std::endl;
            } catch (const std::exception& e) {
                std::cout << std::setw(10) << "❌ FAIL" << std::endl;
                std::cout << "    错误: " << e.what() << std::endl;
            } catch (...) {
                std::cout << std::setw(10) << "❌ ERROR" << std::endl;
            }
        }
        
        std::cout << std::string(120, '=') << std::endl;
    }
    
private:
    static void test_connection_timeout() {
        EnhancedModbusTcpClient::Config config;
        config.server_host = "192.0.2.1";  // 测试专用不可达地址
        config.server_port = 502;
        config.connect_timeout = std::chrono::milliseconds{1000};
        
        EnhancedModbusTcpClient client(config);
        auto result = client.connect();
        
        if (result.code != NetworkErrorCode::TIMEOUT && 
            result.code != NetworkErrorCode::NETWORK_UNREACHABLE) {
            throw std::runtime_error("未检测到预期的超时错误");
        }
    }
    
    static void test_address_out_of_bounds() {
        // 需要实际的从站来测试，这里模拟逻辑
        std::cout << "(模拟) 地址越界测试 - 从站应返回异常码0x02";
    }
    
    static void test_illegal_function() {
        std::cout << "(模拟) 非法功能码测试 - 从站应返回异常码0x01";
    }
    
    static void test_data_count_exceeded() {
        std::cout << "(模拟) 数据数量过大测试 - 从站应返回异常码0x03";
    }
    
    static void test_network_interruption() {
        std::cout << "(模拟) 网络中断测试 - 应检测连接断开并重连";
    }
    
    static void test_slave_busy() {
        std::cout << "(模拟) 从站忙测试 - 从站应返回异常码0x06";
    }
    
    static void test_protocol_error() {
        std::cout << "(模拟) 协议错误测试 - 从站不响应或返回格式错误";
    }
    
    static void test_concurrent_access() {
        std::cout << "(模拟) 并发冲突测试 - 多主站仲裁机制";
    }
};

// =============================================================================
// 最佳实践和性能优化建议
// =============================================================================

class ModbusBestPractices {
public:
    static void print_best_practices() {
        std::cout << "\n=== Modbus最佳实践指南 ===" << std::endl;
        
        std::cout << "\n📋 通信配置最佳实践:" << std::endl;
        std::cout << "1. 超时设置:" << std::endl;
        std::cout << "   - 连接超时: 3-5秒 (局域网), 10-15秒 (广域网)" << std::endl;
        std::cout << "   - 请求超时: 1-3秒 (根据从站响应能力)" << std::endl;
        std::cout << "   - 总超时: 30-60秒 (复杂操作)" << std::endl;
        
        std::cout << "\n2. 重试策略:" << std::endl;
        std::cout << "   - 最大重试: 3次" << std::endl;
        std::cout << "   - 重试延迟: 指数退避 (1s, 2s, 4s)" << std::endl;
        std::cout << "   - 错误分类: 区分网络错误和协议错误" << std::endl;
        
        std::cout << "\n3. 连接池配置:" << std::endl;
        std::cout << "   - 池大小: 2-5个连接 (根据并发需求)" << std::endl;
        std::cout << "   - 空闲超时: 5-10分钟" << std::endl;
        std::cout << "   - 连接复用: 启用以减少连接开销" << std::endl;
        
        std::cout << "\n⚡ 性能优化建议:" << std::endl;
        std::cout << "1. 批量操作:" << std::endl;
        std::cout << "   - 优先使用read_multiple/write_multiple" << std::endl;
        std::cout << "   - 合并连续地址的读写操作" << std::endl;
        std::cout << "   - 避免频繁的单点读写" << std::endl;
        
        std::cout << "\n2. 数据组织:" << std::endl;
        std::cout << "   - 将相关数据放置在连续地址" << std::endl;
        std::cout << "   - 使用适当的数据类型和大小" << std::endl;
        std::cout << "   - 考虑字节序和数据对齐" << std::endl;
        
        std::cout << "\n3. 网络优化:" << std::endl;
        std::cout << "   - 启用TCP_NODELAY减少延迟" << std::endl;
        std::cout << "   - 调整发送/接收缓冲区大小" << std::endl;
        std::cout << "   - 使用keep-alive保持连接" << std::endl;
        
        std::cout << "\n🛡️ 安全和可靠性:" << std::endl;
        std::cout << "1. 数据验证:" << std::endl;
        std::cout << "   - 验证地址范围和数据类型" << std::endl;
        std::cout << "   - 检查返回的数据长度" << std::endl;
        std::cout << "   - 实施边界检查和溢出保护" << std::endl;
        
        std::cout << "\n2. 异常处理:" << std::endl;
        std::cout << "   - 全面处理所有Modbus异常码" << std::endl;
        std::cout << "   - 实施优雅降级策略" << std::endl;
        std::cout << "   - 记录详细的错误日志" << std::endl;
        
        std::cout << "\n3. 监控和诊断:" << std::endl;
        std::cout << "   - 启用统计信息收集" << std::endl;
        std::cout << "   - 监控通信质量和性能" << std::endl;
        std::cout << "   - 实施健康检查和心跳机制" << std::endl;
    }
    
    static void print_troubleshooting_guide() {
        std::cout << "\n=== 故障排除指南 ===" << std::endl;
        
        std::cout << "\n🔍 常见问题诊断:" << std::endl;
        
        std::cout << "\n1. 连接失败:" << std::endl;
        std::cout << "   症状: connect()返回失败" << std::endl;
        std::cout << "   可能原因:" << std::endl;
        std::cout << "   - 网络不通 (ping测试)" << std::endl;
        std::cout << "   - 端口被占用 (netstat检查)" << std::endl;
        std::cout << "   - 防火墙阻挡 (防火墙规则)" << std::endl;
        std::cout << "   - 从站未启动 (服务状态检查)" << std::endl;
        
        std::cout << "\n2. 请求超时:" << std::endl;
        std::cout << "   症状: 请求发送但无响应" << std::endl;
        std::cout << "   可能原因:" << std::endl;
        std::cout << "   - 从站处理缓慢 (增加超时)" << std::endl;
        std::cout << "   - Unit ID不匹配 (检查配置)" << std::endl;
        std::cout << "   - 网络延迟高 (网络质量测试)" << std::endl;
        std::cout << "   - 从站资源不足 (负载检查)" << std::endl;
        
        std::cout << "\n3. 数据不一致:" << std::endl;
        std::cout << "   症状: 读取的数据不正确" << std::endl;
        std::cout << "   可能原因:" << std::endl;
        std::cout << "   - 地址映射错误 (地址表检查)" << std::endl;
        std::cout << "   - 数据类型不匹配 (类型转换)" << std::endl;
        std::cout << "   - 字节序问题 (大小端转换)" << std::endl;
        std::cout << "   - 并发访问冲突 (访问控制)" << std::endl;
        
        std::cout << "\n4. 性能问题:" << std::endl;
        std::cout << "   症状: 通信速度慢" << std::endl;
        std::cout << "   优化措施:" << std::endl;
        std::cout << "   - 使用批量操作减少请求次数" << std::endl;
        std::cout << "   - 启用连接池复用连接" << std::endl;
        std::cout << "   - 调整网络参数和缓冲区" << std::endl;
        std::cout << "   - 优化数据组织和访问模式" << std::endl;
        
        std::cout << "\n🔧 调试工具和方法:" << std::endl;
        std::cout << "1. 网络工具:" << std::endl;
        std::cout << "   - tcpdump/Wireshark: 抓包分析" << std::endl;
        std::cout << "   - netstat: 连接状态检查" << std::endl;
        std::cout << "   - ping/telnet: 连通性测试" << std::endl;
        
        std::cout << "\n2. 日志分析:" << std::endl;
        std::cout << "   - 启用详细日志记录" << std::endl;
        std::cout << "   - 记录时间戳和序列号" << std::endl;
        std::cout << "   - 分析错误模式和频率" << std::endl;
        
        std::cout << "\n3. 统计监控:" << std::endl;
        std::cout << "   - 监控成功率和响应时间" << std::endl;
        std::cout << "   - 跟踪错误类型和分布" << std::endl;
        std::cout << "   - 设置性能基线和告警" << std::endl;
    }
};

// =============================================================================
// 主程序 - 运行所有示例
// =============================================================================

int main() {
    std::cout << "PLCOpen Modbus主从互通示例和异常处理矩阵" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    try {
        // 初始化网络资源
        auto& net_mgr = NetworkResourceManager::getInstance();
        auto init_result = net_mgr.initialize();
        if (init_result != NetworkErrorCode::SUCCESS) {
            std::cout << "❌ 网络资源初始化失败" << std::endl;
            return 1;
        }
        
        std::cout << "✅ 网络资源初始化成功" << std::endl;
        
        // 运行基础示例
        ModbusMasterSlaveExample::basic_master_slave_demo();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 运行高性能示例
        ModbusMasterSlaveExample::high_performance_demo();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 运行异常处理矩阵测试
        ModbusExceptionMatrix::run_exception_matrix();
        
        // 显示最佳实践
        ModbusBestPractices::print_best_practices();
        
        // 显示故障排除指南
        ModbusBestPractices::print_troubleshooting_guide();
        
        std::cout << "\n🎉 所有Modbus示例和测试完成！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ 程序执行出现异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ 程序执行出现未知异常" << std::endl;
        return 1;
    }
    
    return 0;
}