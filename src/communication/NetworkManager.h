/**
 * @file NetworkManager.h
 * @brief 跨平台网络资源管理和错误处理
 * @version 1.0
 * @date 2025-09-09
 * 
 * 提供统一的跨平台网络接口，包括：
 * - 自动网络库初始化和清理
 * - RAII套接字资源管理
 * - 统一错误处理和映射
 * - 信号处理和优雅关闭
 * - 高级超时和连接管理
 */

#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <signal.h>
    #include <sys/select.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

namespace plc_runtime {
namespace communication {

/**
 * @brief 跨平台网络错误代码
 */
enum class NetworkErrorCode {
    SUCCESS = 0,
    INIT_FAILED,           // 网络库初始化失败
    CREATE_SOCKET_FAILED,  // 创建套接字失败
    BIND_FAILED,           // 绑定失败
    LISTEN_FAILED,         // 监听失败
    CONNECT_FAILED,        // 连接失败
    ACCEPT_FAILED,         // 接受连接失败
    SEND_FAILED,           // 发送失败
    RECEIVE_FAILED,        // 接收失败
    TIMEOUT,               // 超时
    PEER_CLOSED,           // 对端关闭连接
    INVALID_ADDRESS,       // 无效地址
    ADDRESS_IN_USE,        // 地址已被使用
    NETWORK_UNREACHABLE,   // 网络不可达
    CONNECTION_REFUSED,    // 连接被拒绝
    INTERRUPTED,           // 操作被中断
    INVALID_PARAM,         // 参数无效
    BUFFER_TOO_SMALL,      // 缓冲区太小
    PARTIAL_SEND,          // 部分发送
    UNKNOWN_ERROR          // 未知错误
};

/**
 * @brief 网络操作结果
 */
struct NetworkResult {
    NetworkErrorCode code;
    std::string message;
    size_t bytes_transferred;
    
    NetworkResult(NetworkErrorCode c = NetworkErrorCode::SUCCESS, 
                  const std::string& msg = "", 
                  size_t bytes = 0)
        : code(c), message(msg), bytes_transferred(bytes) {}
    
    bool is_success() const { return code == NetworkErrorCode::SUCCESS; }
    bool is_timeout() const { return code == NetworkErrorCode::TIMEOUT; }
    bool is_peer_closed() const { return code == NetworkErrorCode::PEER_CLOSED; }
};

/**
 * @brief 套接字配置
 */
struct SocketConfig {
    bool reuse_address = true;          // 地址重用
    bool keep_alive = true;             // 保持连接
    bool no_delay = true;               // 禁用Nagle算法
    int send_buffer_size = 65536;       // 发送缓冲区大小
    int recv_buffer_size = 65536;       // 接收缓冲区大小
    std::chrono::milliseconds send_timeout{5000};    // 发送超时
    std::chrono::milliseconds recv_timeout{5000};    // 接收超时
};

/**
 * @brief 网络库初始化管理器（单例）
 */
class NetworkInitializer {
public:
    static NetworkInitializer& getInstance();
    ~NetworkInitializer();
    
    NetworkErrorCode initialize();
    void cleanup();
    
    bool is_initialized() const { return initialized_.load(); }

private:
    NetworkInitializer() = default;
    NetworkInitializer(const NetworkInitializer&) = delete;
    NetworkInitializer& operator=(const NetworkInitializer&) = delete;
    
    std::atomic<bool> initialized_{false};
    std::mutex init_mutex_;
    
#ifdef _WIN32
    WSADATA wsa_data_;
#endif
};

/**
 * @brief RAII套接字管理器
 */
class ManagedSocket {
public:
    ManagedSocket();
    explicit ManagedSocket(SOCKET socket);
    ~ManagedSocket();
    
    // 移动语义
    ManagedSocket(ManagedSocket&& other) noexcept;
    ManagedSocket& operator=(ManagedSocket&& other) noexcept;
    
    // 禁用拷贝
    ManagedSocket(const ManagedSocket&) = delete;
    ManagedSocket& operator=(const ManagedSocket&) = delete;
    
    // 套接字操作
    NetworkResult create(int family = AF_INET, int type = SOCK_STREAM, int protocol = 0);
    NetworkResult bind(const std::string& address, uint16_t port);
    NetworkResult listen(int backlog = 5);
    NetworkResult connect(const std::string& address, uint16_t port, 
                         std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});
    std::unique_ptr<ManagedSocket> accept(std::string& client_ip, uint16_t& client_port,
                                          std::chrono::milliseconds timeout = std::chrono::milliseconds{-1});
    
    // 数据传输
    NetworkResult send(const void* data, size_t size, 
                      std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});
    NetworkResult receive(void* buffer, size_t size, size_t& received,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});
    NetworkResult send_all(const void* data, size_t size, 
                          std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});
    NetworkResult receive_exact(void* buffer, size_t size, 
                               std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});
    
    // 套接字配置
    NetworkResult configure(const SocketConfig& config);
    NetworkResult set_non_blocking(bool non_blocking = true);
    NetworkResult set_timeout(std::chrono::milliseconds send_timeout, 
                             std::chrono::milliseconds recv_timeout);
    
    // 状态管理
    void close();
    bool is_valid() const { return socket_ != INVALID_SOCKET; }
    SOCKET get_socket() const { return socket_; }
    
    // 错误检查
    static NetworkResult get_last_error(const std::string& operation = "");
    static std::string error_to_string(NetworkErrorCode code);

private:
    SOCKET socket_;
    std::atomic<bool> closed_{false};
    mutable std::mutex socket_mutex_;
    
    NetworkResult wait_for_io(bool for_write, std::chrono::milliseconds timeout);
    static NetworkErrorCode map_system_error(int error_code);
};

/**
 * @brief 连接池管理器
 */
class ConnectionPool {
public:
    struct ConnectionInfo {
        std::string address;
        uint16_t port;
        std::chrono::steady_clock::time_point last_used;
        std::atomic<bool> in_use{false};
        std::unique_ptr<ManagedSocket> socket;
        
        ConnectionInfo(const std::string& addr, uint16_t p)
            : address(addr), port(p), last_used(std::chrono::steady_clock::now()) {}
    };
    
    explicit ConnectionPool(size_t max_connections = 10, 
                           std::chrono::minutes idle_timeout = std::chrono::minutes{5});
    ~ConnectionPool();
    
    std::shared_ptr<ConnectionInfo> acquire_connection(const std::string& address, uint16_t port);
    void release_connection(std::shared_ptr<ConnectionInfo> conn);
    void cleanup_idle_connections();
    size_t get_active_count() const;
    size_t get_total_count() const;

private:
    mutable std::mutex pool_mutex_;
    std::vector<std::shared_ptr<ConnectionInfo>> connections_;
    std::condition_variable pool_cv_;
    size_t max_connections_;
    std::chrono::minutes idle_timeout_;
    std::atomic<bool> cleanup_running_{true};
    std::thread cleanup_thread_;
    
    void cleanup_worker();
};

/**
 * @brief 信号处理管理器
 */
class SignalManager {
public:
    using SignalHandler = std::function<void(int)>;
    
    static SignalManager& getInstance();
    
    void install_handlers();
    void set_shutdown_handler(SignalHandler handler);
    void request_shutdown();
    bool is_shutdown_requested() const { return shutdown_requested_.load(); }
    
private:
    SignalManager() = default;
    std::atomic<bool> shutdown_requested_{false};
    SignalHandler shutdown_handler_;
    std::mutex handler_mutex_;
    
#ifdef _WIN32
    static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type);
#else
    static void signal_handler(int signal);
#endif
};

/**
 * @brief 网络统计信息
 */
struct NetworkStatistics {
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> connections_accepted{0};
    std::atomic<uint64_t> connections_failed{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> network_errors{0};
    std::chrono::steady_clock::time_point start_time;
    
    NetworkStatistics() : start_time(std::chrono::steady_clock::now()) {}
    
    void reset() {
        bytes_sent.store(0);
        bytes_received.store(0);
        connections_accepted.store(0);
        connections_failed.store(0);
        timeouts.store(0);
        network_errors.store(0);
        start_time = std::chrono::steady_clock::now();
    }
    
    double get_uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = now - start_time;
        return std::chrono::duration<double>(duration).count();
    }
};

/**
 * @brief 地址解析器
 */
class AddressResolver {
public:
    struct ResolvedAddress {
        struct sockaddr_in addr;
        std::string ip_string;
        uint16_t port;
        
        ResolvedAddress() { memset(&addr, 0, sizeof(addr)); }
    };
    
    static NetworkResult resolve(const std::string& hostname, uint16_t port, 
                                ResolvedAddress& result);
    static bool is_valid_ipv4(const std::string& ip);
    static std::string get_local_ip();
    static std::vector<std::string> get_all_local_ips();
};

} // namespace communication
} // namespace plc_runtime