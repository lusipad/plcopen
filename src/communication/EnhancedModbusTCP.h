/**
 * @file EnhancedModbusTCP.h
 * @brief 使用NetworkManager的增强Modbus TCP实现
 * @version 1.0
 * @date 2025-09-09
 * 
 * 基于NetworkManager实现的企业级Modbus TCP通信，特性包括：
 * - 自动资源管理和清理
 * - 跨平台网络错误处理
 * - 连接池和连接复用
 * - 信号处理和优雅关闭
 * - 高级超时和重试机制
 * - 全面的统计和监控
 */

#pragma once

#include "NetworkManager.h"
#include "ModbusTCP.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>

namespace plc_runtime {
namespace communication {

/**
 * @brief 增强的Modbus TCP客户端
 */
class EnhancedModbusTcpClient {
public:
    struct Config {
        std::string server_host = "localhost";
        uint16_t server_port = 502;
        uint8_t unit_id = 1;
        
        // 超时配置
        std::chrono::milliseconds connect_timeout{5000};
        std::chrono::milliseconds request_timeout{3000};
        std::chrono::milliseconds total_timeout{30000};
        
        // 重试配置
        int max_retries = 3;
        std::chrono::milliseconds retry_delay{1000};
        bool auto_reconnect = true;
        
        // 连接池配置
        bool use_connection_pool = true;
        size_t max_pool_size = 5;
        std::chrono::minutes pool_idle_timeout{5};
        
        // 套接字配置
        SocketConfig socket_config;
    };
    
    explicit EnhancedModbusTcpClient(const Config& config = Config{});
    ~EnhancedModbusTcpClient();
    
    // 禁用拷贝
    EnhancedModbusTcpClient(const EnhancedModbusTcpClient&) = delete;
    EnhancedModbusTcpClient& operator=(const EnhancedModbusTcpClient&) = delete;
    
    // 连接管理
    NetworkResult connect();
    void disconnect();
    bool is_connected() const;
    
    // Modbus操作 - 返回详细的NetworkResult
    NetworkResult read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values);
    NetworkResult write_single_coil(uint16_t address, bool value);
    NetworkResult write_multiple_coils(uint16_t start_address, const std::vector<bool>& values);
    
    NetworkResult read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values);
    
    NetworkResult read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values);
    NetworkResult write_single_register(uint16_t address, uint16_t value);
    NetworkResult write_multiple_registers(uint16_t start_address, const std::vector<uint16_t>& values);
    
    NetworkResult read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values);
    
    // 统计信息
    const NetworkStatistics& get_network_statistics() const { return network_stats_; }
    ModbusStatisticsSnapshot get_modbus_statistics() const;
    void reset_statistics();
    
    // 配置更新
    void update_config(const Config& new_config);
    const Config& get_config() const { return config_; }

private:
    Config config_;
    std::unique_ptr<ConnectionPool> connection_pool_;
    std::shared_ptr<ConnectionPool::ConnectionInfo> current_connection_;
    
    mutable std::mutex client_mutex_;
    std::atomic<bool> connected_{false};
    std::atomic<uint16_t> transaction_id_{1};
    
    // 统计信息
    mutable NetworkStatistics network_stats_;
    ModbusStatistics modbus_stats_;
    
    // 内部方法
    NetworkResult ensure_connection();
    NetworkResult send_request_with_retry(const ModbusTcpADU& request, ModbusTcpADU& response);
    NetworkResult send_modbus_request(const ModbusTcpADU& request, ModbusTcpADU& response);
    
    uint16_t next_transaction_id() { return transaction_id_.fetch_add(1); }
    void update_statistics(const NetworkResult& result, size_t bytes_sent, size_t bytes_received);
};

/**
 * @brief 增强的Modbus TCP服务器
 */
class EnhancedModbusTcpServer {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        uint16_t port = 502;
        uint8_t unit_id = 1;
        
        // 连接管理
        size_t max_connections = 50;
        std::chrono::minutes client_timeout{30};
        bool enable_broadcast = false;
        
        // 套接字配置
        SocketConfig socket_config;
        
        // 工作线程配置
        size_t worker_threads = 4;
        size_t max_pending_requests = 1000;
    };
    
    // 回调函数类型定义
    using ClientConnectedCallback = std::function<void(const std::string& client_ip, uint16_t client_port)>;
    using ClientDisconnectedCallback = std::function<void(const std::string& client_ip, uint16_t client_port)>;
    using RequestReceivedCallback = std::function<void(const std::string& client_ip, const ModbusPDU& request)>;
    using ErrorCallback = std::function<void(const std::string& error_message, NetworkErrorCode error_code)>;
    
    explicit EnhancedModbusTcpServer(const Config& config = Config{});
    ~EnhancedModbusTcpServer();
    
    // 禁用拷贝
    EnhancedModbusTcpServer(const EnhancedModbusTcpServer&) = delete;
    EnhancedModbusTcpServer& operator=(const EnhancedModbusTcpServer&) = delete;
    
    // 服务器管理
    NetworkResult start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // 数据映射
    void set_data_map(std::shared_ptr<ModbusDataMap> data_map);
    
    // 回调设置
    void set_client_connected_callback(ClientConnectedCallback callback);
    void set_client_disconnected_callback(ClientDisconnectedCallback callback);
    void set_request_received_callback(RequestReceivedCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // 统计信息
    const NetworkStatistics& get_network_statistics() const { return network_stats_; }
    ModbusStatisticsSnapshot get_modbus_statistics() const;
    void reset_statistics();
    size_t get_active_connections() const;
    
    // 优雅关闭
    void request_shutdown();
    bool wait_for_shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

private:
    Config config_;
    std::shared_ptr<ModbusDataMap> data_map_;
    
    std::unique_ptr<ManagedSocket> server_socket_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    
    // 线程管理
    std::thread accept_thread_;
    std::vector<std::thread> worker_threads_;
    std::queue<std::function<void()>> work_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 客户端管理
    struct ClientSession {
        std::unique_ptr<ManagedSocket> socket;
        std::string ip;
        uint16_t port;
        std::chrono::steady_clock::time_point last_activity;
        std::atomic<bool> active{true};
        
        ClientSession(std::unique_ptr<ManagedSocket> sock, const std::string& client_ip, uint16_t client_port)
            : socket(std::move(sock)), ip(client_ip), port(client_port), 
              last_activity(std::chrono::steady_clock::now()) {}
    };
    
    std::vector<std::shared_ptr<ClientSession>> active_clients_;
    mutable std::mutex clients_mutex_;
    
    // 统计信息
    mutable NetworkStatistics network_stats_;
    ModbusStatistics modbus_stats_;
    
    // 回调函数
    ClientConnectedCallback client_connected_callback_;
    ClientDisconnectedCallback client_disconnected_callback_;
    RequestReceivedCallback request_received_callback_;
    ErrorCallback error_callback_;
    std::mutex callbacks_mutex_;
    
    // 内部方法
    void accept_clients();
    void worker_thread();
    void handle_client_session(std::shared_ptr<ClientSession> session);
    void cleanup_inactive_clients();
    
    ModbusPDU process_modbus_request(const ModbusPDU& request);
    void notify_error(const std::string& message, NetworkErrorCode code);
    void update_statistics(const NetworkResult& result, size_t bytes_sent, size_t bytes_received);
    
    // Modbus处理方法
    ModbusPDU handle_read_coils(const std::vector<uint8_t>& data);
    ModbusPDU handle_read_discrete_inputs(const std::vector<uint8_t>& data);
    ModbusPDU handle_read_holding_registers(const std::vector<uint8_t>& data);
    ModbusPDU handle_read_input_registers(const std::vector<uint8_t>& data);
    ModbusPDU handle_write_single_coil(const std::vector<uint8_t>& data);
    ModbusPDU handle_write_single_register(const std::vector<uint8_t>& data);
    ModbusPDU handle_write_multiple_coils(const std::vector<uint8_t>& data);
    ModbusPDU handle_write_multiple_registers(const std::vector<uint8_t>& data);
    ModbusPDU create_exception_response(uint8_t function_code, ModbusException exception);
    
    std::vector<uint8_t> serialize_adu(const ModbusTcpADU& adu);
    bool deserialize_adu(const std::vector<uint8_t>& data, ModbusTcpADU& adu);
};

/**
 * @brief 网络资源管理器 - 全局单例
 * 
 * 提供全局网络资源管理，包括：
 * - 网络库初始化/清理
 * - 信号处理安装
 * - 全局统计信息收集
 */
class NetworkResourceManager {
public:
    static NetworkResourceManager& getInstance();
    
    // 初始化和清理
    NetworkResult initialize();
    void cleanup();
    bool is_initialized() const;
    
    // 信号处理
    void install_signal_handlers();
    void set_shutdown_callback(std::function<void()> callback);
    bool is_shutdown_requested() const;
    
    // 全局统计
    struct GlobalStatistics {
        std::atomic<uint64_t> total_clients_created{0};
        std::atomic<uint64_t> total_servers_created{0};
        std::atomic<uint64_t> total_connections_made{0};
        std::atomic<uint64_t> total_bytes_sent{0};
        std::atomic<uint64_t> total_bytes_received{0};
        std::atomic<uint64_t> total_errors{0};
        std::chrono::steady_clock::time_point start_time;
        
        GlobalStatistics() : start_time(std::chrono::steady_clock::now()) {}
        
        void reset() {
            total_clients_created.store(0);
            total_servers_created.store(0);
            total_connections_made.store(0);
            total_bytes_sent.store(0);
            total_bytes_received.store(0);
            total_errors.store(0);
            start_time = std::chrono::steady_clock::now();
        }
    };
    
    const GlobalStatistics& get_global_statistics() const { return global_stats_; }
    void reset_global_statistics() { global_stats_.reset(); }

private:
    NetworkResourceManager() = default;
    ~NetworkResourceManager() { cleanup(); }
    
    NetworkResourceManager(const NetworkResourceManager&) = delete;
    NetworkResourceManager& operator=(const NetworkResourceManager&) = delete;
    
    std::atomic<bool> initialized_{false};
    GlobalStatistics global_stats_;
    std::function<void()> shutdown_callback_;
    std::mutex callback_mutex_;
    
    static void signal_handler_wrapper();
};

} // namespace communication
} // namespace plc_runtime