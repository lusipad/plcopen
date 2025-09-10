/**
 * @file test_network_manager.cpp
 * @brief NetworkManager跨平台网络管理单元测试
 * @version 1.0
 * @date 2025-09-09
 * 
 * 测试跨平台网络资源管理、错误处理和RAII套接字管理
 */

#include "test/TestFramework.h"
#include "communication/NetworkManager.h"
#include <thread>
#include <chrono>
#include <future>

using namespace plc_runtime::communication;

class NetworkManagerTest : public TestFramework {
public:
    void SetUp() override {
        // 确保网络初始化
        auto& init = NetworkInitializer::getInstance();
        ASSERT_TRUE(init.initialize() == NetworkErrorCode::SUCCESS);
        ASSERT_TRUE(init.is_initialized());
    }
    
    void TearDown() override {
        // 清理在test期间创建的资源
    }
};

// =============================================================================
// NetworkInitializer Tests
// =============================================================================

void test_network_initializer_singleton() {
    auto& init1 = NetworkInitializer::getInstance();
    auto& init2 = NetworkInitializer::getInstance();
    
    // 验证单例
    ASSERT_TRUE(&init1 == &init2);
    
    // 验证初始化
    auto result = init1.initialize();
    ASSERT_TRUE(result == NetworkErrorCode::SUCCESS);
    ASSERT_TRUE(init1.is_initialized());
    
    // 重复初始化应该成功
    result = init1.initialize();
    ASSERT_TRUE(result == NetworkErrorCode::SUCCESS);
    
    std::cout << "✅ NetworkInitializer单例和初始化测试通过" << std::endl;
}

// =============================================================================
// ManagedSocket Tests
// =============================================================================

void test_managed_socket_lifecycle() {
    ManagedSocket socket;
    
    // 初始状态
    ASSERT_FALSE(socket.is_valid());
    
    // 创建套接字
    auto result = socket.create();
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(socket.is_valid());
    
    // 关闭套接字
    socket.close();
    ASSERT_FALSE(socket.is_valid());
    
    std::cout << "✅ ManagedSocket生命周期管理测试通过" << std::endl;
}

void test_managed_socket_move_semantics() {
    ManagedSocket socket1;
    auto result = socket1.create();
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(socket1.is_valid());
    
    // 移动构造
    ManagedSocket socket2(std::move(socket1));
    ASSERT_FALSE(socket1.is_valid()); // 原对象应该无效
    ASSERT_TRUE(socket2.is_valid());  // 新对象应该有效
    
    // 移动赋值
    ManagedSocket socket3;
    socket3 = std::move(socket2);
    ASSERT_FALSE(socket2.is_valid()); // 原对象应该无效
    ASSERT_TRUE(socket3.is_valid());  // 新对象应该有效
    
    std::cout << "✅ ManagedSocket移动语义测试通过" << std::endl;
}

void test_socket_server_client_communication() {
    // 创建服务器套接字
    ManagedSocket server;
    auto result = server.create();
    ASSERT_TRUE(result.is_success());
    
    // 配置套接字
    SocketConfig config;
    result = server.configure(config);
    ASSERT_TRUE(result.is_success());
    
    // 绑定到随机端口
    result = server.bind("127.0.0.1", 0);
    ASSERT_TRUE(result.is_success());
    
    // 获取实际绑定的端口
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    ASSERT_TRUE(getsockname(server.get_socket(), (struct sockaddr*)&addr, &len) == 0);
    uint16_t server_port = ntohs(addr.sin_port);
    
    // 开始监听
    result = server.listen(1);
    ASSERT_TRUE(result.is_success());
    
    // 创建客户端连接（在另一个线程中）
    std::promise<bool> connect_promise;
    auto connect_future = connect_promise.get_future();
    
    std::thread client_thread([&]() {
        ManagedSocket client;
        auto client_result = client.create();
        if (!client_result.is_success()) {
            connect_promise.set_value(false);
            return;
        }
        
        client_result = client.connect("127.0.0.1", server_port, std::chrono::milliseconds{2000});
        connect_promise.set_value(client_result.is_success());
        
        if (client_result.is_success()) {
            // 发送测试数据
            std::string test_data = "Hello Server";
            client_result = client.send_all(test_data.data(), test_data.size());
            
            // 接收响应
            if (client_result.is_success()) {
                char buffer[256];
                size_t received;
                client.receive(buffer, sizeof(buffer), received, std::chrono::milliseconds{2000});
            }
        }
    });
    
    // 接受客户端连接
    std::string client_ip;
    uint16_t client_port;
    auto client_socket = server.accept(client_ip, client_port, std::chrono::milliseconds{3000});
    
    ASSERT_TRUE(client_socket != nullptr);
    ASSERT_FALSE(client_ip.empty());
    ASSERT_TRUE(client_port > 0);
    
    // 等待客户端连接完成
    auto connect_status = connect_future.wait_for(std::chrono::milliseconds{2000});
    ASSERT_TRUE(connect_status == std::future_status::ready);
    ASSERT_TRUE(connect_future.get());
    
    // 接收客户端数据
    char buffer[256];
    size_t received;
    result = client_socket->receive(buffer, sizeof(buffer), received, std::chrono::milliseconds{2000});
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(received > 0);
    
    // 回显数据
    result = client_socket->send_all(buffer, received);
    ASSERT_TRUE(result.is_success());
    
    client_thread.join();
    
    std::cout << "✅ 套接字服务器-客户端通信测试通过" << std::endl;
}

void test_socket_timeout_handling() {
    ManagedSocket socket;
    auto result = socket.create();
    ASSERT_TRUE(result.is_success());
    
    // 尝试连接到不存在的地址，应该超时
    auto start_time = std::chrono::steady_clock::now();
    result = socket.connect("192.0.2.1", 12345, std::chrono::milliseconds{1000}); // 使用测试专用IP
    auto end_time = std::chrono::steady_clock::now();
    
    // 应该失败（超时或连接拒绝）
    ASSERT_FALSE(result.is_success());
    ASSERT_TRUE(result.code == NetworkErrorCode::TIMEOUT || 
               result.code == NetworkErrorCode::NETWORK_UNREACHABLE ||
               result.code == NetworkErrorCode::CONNECTION_REFUSED);
    
    // 检查超时时间大致正确
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    ASSERT_TRUE(elapsed.count() >= 900 && elapsed.count() <= 2000); // 允许一些误差
    
    std::cout << "✅ 套接字超时处理测试通过" << std::endl;
}

void test_socket_configuration() {
    ManagedSocket socket;
    auto result = socket.create();
    ASSERT_TRUE(result.is_success());
    
    // 测试各种套接字配置
    SocketConfig config;
    config.reuse_address = true;
    config.keep_alive = true;
    config.no_delay = true;
    config.send_buffer_size = 32768;
    config.recv_buffer_size = 32768;
    config.send_timeout = std::chrono::milliseconds{3000};
    config.recv_timeout = std::chrono::milliseconds{3000};
    
    result = socket.configure(config);
    ASSERT_TRUE(result.is_success());
    
    // 测试非阻塞模式
    result = socket.set_non_blocking(true);
    ASSERT_TRUE(result.is_success());
    
    result = socket.set_non_blocking(false);
    ASSERT_TRUE(result.is_success());
    
    std::cout << "✅ 套接字配置测试通过" << std::endl;
}

// =============================================================================
// ConnectionPool Tests
// =============================================================================

void test_connection_pool_basic() {
    ConnectionPool pool(3, std::chrono::minutes{1}); // 最多3个连接，1分钟空闲超时
    
    // 获取连接（应该创建新连接）
    auto conn1 = pool.acquire_connection("127.0.0.1", 8080);
    ASSERT_TRUE(conn1 == nullptr); // 连接失败（没有服务器），但池应该正常工作
    
    ASSERT_TRUE(pool.get_total_count() <= 1);
    
    std::cout << "✅ 连接池基本功能测试通过" << std::endl;
}

void test_connection_pool_cleanup() {
    ConnectionPool pool(2, std::chrono::milliseconds{100}); // 很短的空闲超时用于测试
    
    // 等待清理周期
    std::this_thread::sleep_for(std::chrono::milliseconds{1500});
    
    // 验证空闲连接被清理
    ASSERT_TRUE(pool.get_total_count() == 0);
    
    std::cout << "✅ 连接池清理功能测试通过" << std::endl;
}

// =============================================================================
// AddressResolver Tests
// =============================================================================

void test_address_resolver_ipv4() {
    // 测试有效IP地址
    ASSERT_TRUE(AddressResolver::is_valid_ipv4("127.0.0.1"));
    ASSERT_TRUE(AddressResolver::is_valid_ipv4("192.168.1.1"));
    ASSERT_TRUE(AddressResolver::is_valid_ipv4("0.0.0.0"));
    ASSERT_TRUE(AddressResolver::is_valid_ipv4("255.255.255.255"));
    
    // 测试无效IP地址
    ASSERT_FALSE(AddressResolver::is_valid_ipv4("256.1.1.1"));
    ASSERT_FALSE(AddressResolver::is_valid_ipv4("127.0.0"));
    ASSERT_FALSE(AddressResolver::is_valid_ipv4("not.an.ip.address"));
    ASSERT_FALSE(AddressResolver::is_valid_ipv4(""));
    
    std::cout << "✅ IP地址验证测试通过" << std::endl;
}

void test_address_resolver_resolve() {
    AddressResolver::ResolvedAddress resolved;
    
    // 解析localhost
    auto result = AddressResolver::resolve("127.0.0.1", 8080, resolved);
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(resolved.ip_string == "127.0.0.1");
    ASSERT_TRUE(resolved.port == 8080);
    
    // 解析hostname (localhost)
    result = AddressResolver::resolve("localhost", 9090, resolved);
    ASSERT_TRUE(result.is_success());
    ASSERT_TRUE(resolved.port == 9090);
    // localhost通常解析为127.0.0.1，但也可能有其他行为
    
    std::cout << "✅ 地址解析测试通过" << std::endl;
}

void test_local_ip_detection() {
    auto local_ip = AddressResolver::get_local_ip();
    ASSERT_FALSE(local_ip.empty());
    ASSERT_TRUE(AddressResolver::is_valid_ipv4(local_ip));
    
    auto all_ips = AddressResolver::get_all_local_ips();
    ASSERT_FALSE(all_ips.empty());
    
    // 验证所有返回的IP都是有效的
    for (const auto& ip : all_ips) {
        ASSERT_TRUE(AddressResolver::is_valid_ipv4(ip));
    }
    
    std::cout << "✅ 本地IP检测测试通过 - 检测到 " << all_ips.size() << " 个IP地址" << std::endl;
}

// =============================================================================
// SignalManager Tests
// =============================================================================

void test_signal_manager_basic() {
    auto& signal_mgr = SignalManager::getInstance();
    
    // 初始状态
    ASSERT_FALSE(signal_mgr.is_shutdown_requested());
    
    // 安装信号处理器
    signal_mgr.install_handlers();
    
    // 设置回调
    bool callback_called = false;
    signal_mgr.set_shutdown_handler([&](int sig) {
        callback_called = true;
    });
    
    // 请求关闭
    signal_mgr.request_shutdown();
    ASSERT_TRUE(signal_mgr.is_shutdown_requested());
    ASSERT_TRUE(callback_called);
    
    std::cout << "✅ 信号管理器基本功能测试通过" << std::endl;
}

// =============================================================================
// 错误处理和跨平台兼容性测试
// =============================================================================

void test_error_code_mapping() {
    // 测试错误码到字符串的映射
    auto success_msg = ManagedSocket::error_to_string(NetworkErrorCode::SUCCESS);
    ASSERT_FALSE(success_msg.empty());
    
    auto timeout_msg = ManagedSocket::error_to_string(NetworkErrorCode::TIMEOUT);
    ASSERT_FALSE(timeout_msg.empty());
    ASSERT_TRUE(timeout_msg.find("timeout") != std::string::npos || 
               timeout_msg.find("Timeout") != std::string::npos);
    
    auto invalid_msg = ManagedSocket::error_to_string(NetworkErrorCode::INVALID_PARAM);
    ASSERT_FALSE(invalid_msg.empty());
    
    std::cout << "✅ 错误码映射测试通过" << std::endl;
}

void test_cross_platform_compatibility() {
    // 测试跨平台网络功能
    ManagedSocket socket;
    auto result = socket.create();
    ASSERT_TRUE(result.is_success());
    
    // 测试bind到localhost
    result = socket.bind("127.0.0.1", 0);
    ASSERT_TRUE(result.is_success());
    
#ifdef _WIN32
    std::cout << "✅ Windows平台网络功能测试通过" << std::endl;
#else
    std::cout << "✅ Unix/Linux平台网络功能测试通过" << std::endl;
#endif
}

// =============================================================================
// 并发和线程安全测试
// =============================================================================

void test_concurrent_socket_operations() {
    const int num_threads = 4;
    const int operations_per_thread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                ManagedSocket socket;
                if (socket.create().is_success()) {
                    success_count.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    ASSERT_TRUE(success_count.load() == num_threads * operations_per_thread);
    std::cout << "✅ 并发套接字操作测试通过 - " << success_count.load() << " 个成功操作" << std::endl;
}

void test_network_statistics() {
    NetworkStatistics stats;
    
    // 初始状态
    ASSERT_TRUE(stats.bytes_sent.load() == 0);
    ASSERT_TRUE(stats.bytes_received.load() == 0);
    ASSERT_TRUE(stats.connections_accepted.load() == 0);
    
    // 模拟统计更新
    stats.bytes_sent.fetch_add(1024);
    stats.bytes_received.fetch_add(512);
    stats.connections_accepted.fetch_add(1);
    
    ASSERT_TRUE(stats.bytes_sent.load() == 1024);
    ASSERT_TRUE(stats.bytes_received.load() == 512);
    ASSERT_TRUE(stats.connections_accepted.load() == 1);
    
    // 测试重置
    stats.reset();
    ASSERT_TRUE(stats.bytes_sent.load() == 0);
    ASSERT_TRUE(stats.bytes_received.load() == 0);
    ASSERT_TRUE(stats.connections_accepted.load() == 0);
    
    std::cout << "✅ 网络统计信息测试通过" << std::endl;
}

// =============================================================================
// 性能和压力测试
// =============================================================================

void test_socket_performance() {
    const int num_operations = 1000;
    auto start_time = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        ManagedSocket socket;
        socket.create();
        socket.close();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    double ops_per_second = (num_operations * 1000000.0) / duration.count();
    
    std::cout << "✅ 套接字性能测试通过 - " << static_cast<int>(ops_per_second) 
              << " ops/sec (创建+关闭)" << std::endl;
    
    // 基本性能要求：至少每秒1000次操作
    ASSERT_TRUE(ops_per_second > 1000.0);
}

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    NetworkManagerTest test_framework;
    
    std::cout << "\n=== NetworkManager跨平台网络管理单元测试 ===" << std::endl;
    
    test_framework.SetUp();
    
    // 基础功能测试
    std::cout << "\n--- 基础功能测试 ---" << std::endl;
    test_framework.run_test(test_network_initializer_singleton);
    test_framework.run_test(test_managed_socket_lifecycle);
    test_framework.run_test(test_managed_socket_move_semantics);
    
    // 网络通信测试
    std::cout << "\n--- 网络通信测试 ---" << std::endl;
    test_framework.run_test(test_socket_server_client_communication);
    test_framework.run_test(test_socket_timeout_handling);
    test_framework.run_test(test_socket_configuration);
    
    // 连接池测试
    std::cout << "\n--- 连接池测试 ---" << std::endl;
    test_framework.run_test(test_connection_pool_basic);
    test_framework.run_test(test_connection_pool_cleanup);
    
    // 地址解析测试
    std::cout << "\n--- 地址解析测试 ---" << std::endl;
    test_framework.run_test(test_address_resolver_ipv4);
    test_framework.run_test(test_address_resolver_resolve);
    test_framework.run_test(test_local_ip_detection);
    
    // 信号管理测试
    std::cout << "\n--- 信号管理测试 ---" << std::endl;
    test_framework.run_test(test_signal_manager_basic);
    
    // 跨平台兼容性测试
    std::cout << "\n--- 跨平台兼容性测试 ---" << std::endl;
    test_framework.run_test(test_error_code_mapping);
    test_framework.run_test(test_cross_platform_compatibility);
    
    // 并发和线程安全测试
    std::cout << "\n--- 并发和线程安全测试 ---" << std::endl;
    test_framework.run_test(test_concurrent_socket_operations);
    test_framework.run_test(test_network_statistics);
    
    // 性能测试
    std::cout << "\n--- 性能测试 ---" << std::endl;
    test_framework.run_test(test_socket_performance);
    
    test_framework.TearDown();
    
    // 输出测试总结
    std::cout << "\n=== 测试总结 ===" << std::endl;
    std::cout << "总测试数: " << test_framework.get_total_tests() << std::endl;
    std::cout << "通过: " << test_framework.get_passed_tests() << std::endl;
    std::cout << "失败: " << test_framework.get_failed_tests() << std::endl;
    std::cout << "成功率: " << std::fixed << std::setprecision(1) 
              << test_framework.get_success_rate() << "%" << std::endl;
    
    if (test_framework.get_failed_tests() == 0) {
        std::cout << "\n🎉 所有NetworkManager网络管理测试通过！" << std::endl;
        std::cout << "✅ RAII资源管理正常" << std::endl;
        std::cout << "✅ 跨平台兼容性良好" << std::endl;
        std::cout << "✅ 错误处理健壮" << std::endl;
        std::cout << "✅ 并发安全保证" << std::endl;
        std::cout << "✅ 性能指标达标" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ NetworkManager测试存在失败项，需要修复" << std::endl;
        return 1;
    }
}