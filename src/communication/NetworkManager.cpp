/**
 * @file NetworkManager.cpp
 * @brief 跨平台网络资源管理和错误处理实现
 * @version 1.0
 * @date 2025-09-09
 */

#include "NetworkManager.h"
#include <cstring>
#include <sstream>
#include <algorithm>
#include <thread>

#ifdef _WIN32
    #include <iphlpapi.h>
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <ifaddrs.h>
    #include <sys/time.h>
#endif

namespace plc_runtime {
namespace communication {

// =============================================================================
// NetworkInitializer Implementation
// =============================================================================

NetworkInitializer& NetworkInitializer::getInstance() {
    static NetworkInitializer instance;
    return instance;
}

NetworkInitializer::~NetworkInitializer() {
    cleanup();
}

NetworkErrorCode NetworkInitializer::initialize() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    
    if (initialized_.load()) {
        return NetworkErrorCode::SUCCESS;
    }
    
#ifdef _WIN32
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data_);
    if (result != 0) {
        return NetworkErrorCode::INIT_FAILED;
    }
    
    // 验证Winsock版本
    if (LOBYTE(wsa_data_.wVersion) != 2 || HIBYTE(wsa_data_.wVersion) != 2) {
        WSACleanup();
        return NetworkErrorCode::INIT_FAILED;
    }
#endif
    
    initialized_.store(true);
    return NetworkErrorCode::SUCCESS;
}

void NetworkInitializer::cleanup() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    
    if (initialized_.load()) {
#ifdef _WIN32
        WSACleanup();
#endif
        initialized_.store(false);
    }
}

// =============================================================================
// ManagedSocket Implementation
// =============================================================================

ManagedSocket::ManagedSocket() : socket_(INVALID_SOCKET) {
    NetworkInitializer::getInstance().initialize();
}

ManagedSocket::ManagedSocket(SOCKET socket) : socket_(socket) {
    NetworkInitializer::getInstance().initialize();
}

ManagedSocket::~ManagedSocket() {
    close();
}

ManagedSocket::ManagedSocket(ManagedSocket&& other) noexcept 
    : socket_(other.socket_), closed_(other.closed_.load()) {
    other.socket_ = INVALID_SOCKET;
    other.closed_.store(true);
}

ManagedSocket& ManagedSocket::operator=(ManagedSocket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        closed_.store(other.closed_.load());
        other.socket_ = INVALID_SOCKET;
        other.closed_.store(true);
    }
    return *this;
}

NetworkResult ManagedSocket::create(int family, int type, int protocol) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ != INVALID_SOCKET) {
        close();
    }
    
    socket_ = ::socket(family, type, protocol);
    if (socket_ == INVALID_SOCKET) {
        return get_last_error("create socket");
    }
    
    closed_.store(false);
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::bind(const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (address == "0.0.0.0" || address.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0) {
            return NetworkResult(NetworkErrorCode::INVALID_ADDRESS, "Invalid address: " + address);
        }
    }
    
    if (::bind(socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        return get_last_error("bind");
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::listen(int backlog) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
    if (::listen(socket_, backlog) == SOCKET_ERROR) {
        return get_last_error("listen");
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::connect(const std::string& address, uint16_t port, 
                                    std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0) {
        return NetworkResult(NetworkErrorCode::INVALID_ADDRESS, "Invalid address: " + address);
    }
    
    // 设置非阻塞模式进行超时连接
    bool was_blocking = true;
    if (timeout.count() > 0) {
        auto result = set_non_blocking(true);
        if (!result.is_success()) {
            return result;
        }
        was_blocking = false;
    }
    
    int connect_result = ::connect(socket_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (connect_result == 0) {
        // 立即连接成功
        if (!was_blocking) {
            set_non_blocking(false);
        }
        return NetworkResult(NetworkErrorCode::SUCCESS);
    }
    
#ifdef _WIN32
    int error = WSAGetLastError();
    bool would_block = (error == WSAEWOULDBLOCK) || (error == WSAEINPROGRESS);
#else
    int error = errno;
    bool would_block = (error == EINPROGRESS) || (error == EAGAIN);
#endif
    
    if (!would_block) {
        if (!was_blocking) {
            set_non_blocking(false);
        }
        return get_last_error("connect");
    }
    
    // 等待连接完成
    auto wait_result = wait_for_io(true, timeout);
    if (!was_blocking) {
        set_non_blocking(false);
    }
    
    if (!wait_result.is_success()) {
        return wait_result;
    }
    
    // 检查连接是否真正成功
    int sock_error = 0;
    socklen_t len = sizeof(sock_error);
    if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, (char*)&sock_error, &len) == SOCKET_ERROR) {
        return get_last_error("getsockopt");
    }
    
    if (sock_error != 0) {
        return NetworkResult(map_system_error(sock_error), 
                           "Connection failed: " + std::to_string(sock_error));
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

std::unique_ptr<ManagedSocket> ManagedSocket::accept(std::string& client_ip, uint16_t& client_port,
                                                    std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return nullptr;
    }
    
    // 如果指定了超时，等待连接
    if (timeout.count() >= 0) {
        auto wait_result = wait_for_io(false, timeout);
        if (!wait_result.is_success()) {
            return nullptr;
        }
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    SOCKET client_socket = ::accept(socket_, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_socket == INVALID_SOCKET) {
        return nullptr;
    }
    
    // 提取客户端信息
    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
        client_ip = ip_str;
    } else {
        client_ip = "unknown";
    }
    client_port = ntohs(client_addr.sin_port);
    
    return std::make_unique<ManagedSocket>(client_socket);
}

NetworkResult ManagedSocket::send(const void* data, size_t size, 
                                 std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET || closed_.load()) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not valid");
    }
    
    if (!data || size == 0) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Invalid data parameters");
    }
    
    // 等待套接字可写
    if (timeout.count() > 0) {
        auto wait_result = wait_for_io(true, timeout);
        if (!wait_result.is_success()) {
            return wait_result;
        }
    }
    
#ifdef _WIN32
    int sent = ::send(socket_, static_cast<const char*>(data), static_cast<int>(size), 0);
#else
    ssize_t sent = ::send(socket_, data, size, MSG_NOSIGNAL);
#endif
    
    if (sent == SOCKET_ERROR) {
        return get_last_error("send");
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS, "", static_cast<size_t>(sent));
}

NetworkResult ManagedSocket::receive(void* buffer, size_t size, size_t& received,
                                    std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET || closed_.load()) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not valid");
    }
    
    if (!buffer || size == 0) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Invalid buffer parameters");
    }
    
    // 等待套接字可读
    if (timeout.count() > 0) {
        auto wait_result = wait_for_io(false, timeout);
        if (!wait_result.is_success()) {
            received = 0;
            return wait_result;
        }
    }
    
#ifdef _WIN32
    int recv_result = ::recv(socket_, static_cast<char*>(buffer), static_cast<int>(size), 0);
#else
    ssize_t recv_result = ::recv(socket_, buffer, size, 0);
#endif
    
    if (recv_result == SOCKET_ERROR) {
        received = 0;
        return get_last_error("receive");
    } else if (recv_result == 0) {
        received = 0;
        return NetworkResult(NetworkErrorCode::PEER_CLOSED, "Peer closed connection");
    }
    
    received = static_cast<size_t>(recv_result);
    return NetworkResult(NetworkErrorCode::SUCCESS, "", received);
}

NetworkResult ManagedSocket::send_all(const void* data, size_t size, 
                                     std::chrono::milliseconds timeout) {
    if (!data || size == 0) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Invalid data parameters");
    }
    
    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (total_sent < size) {
        // 计算剩余超时时间
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto remaining_timeout = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        
        if (remaining_timeout.count() <= 0) {
            return NetworkResult(NetworkErrorCode::TIMEOUT, 
                               "Send timeout after " + std::to_string(total_sent) + " bytes", 
                               total_sent);
        }
        
        auto result = send(bytes + total_sent, size - total_sent, remaining_timeout);
        if (!result.is_success()) {
            result.bytes_transferred = total_sent;
            return result;
        }
        
        total_sent += result.bytes_transferred;
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS, "", total_sent);
}

NetworkResult ManagedSocket::receive_exact(void* buffer, size_t size, 
                                          std::chrono::milliseconds timeout) {
    if (!buffer || size == 0) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Invalid buffer parameters");
    }
    
    char* bytes = static_cast<char*>(buffer);
    size_t total_received = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (total_received < size) {
        // 计算剩余超时时间
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto remaining_timeout = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        
        if (remaining_timeout.count() <= 0) {
            return NetworkResult(NetworkErrorCode::TIMEOUT, 
                               "Receive timeout after " + std::to_string(total_received) + " bytes", 
                               total_received);
        }
        
        size_t received = 0;
        auto result = receive(bytes + total_received, size - total_received, received, remaining_timeout);
        
        if (!result.is_success()) {
            result.bytes_transferred = total_received;
            return result;
        }
        
        total_received += received;
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS, "", total_received);
}

NetworkResult ManagedSocket::configure(const SocketConfig& config) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
    // 设置地址重用
    if (config.reuse_address) {
        int opt = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                      (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            return get_last_error("set reuse address");
        }
    }
    
    // 设置保持连接
    if (config.keep_alive) {
        int opt = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, 
                      (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            return get_last_error("set keep alive");
        }
    }
    
    // 设置TCP_NODELAY
    if (config.no_delay) {
        int opt = 1;
        if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, 
                      (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            return get_last_error("set no delay");
        }
    }
    
    // 设置发送缓冲区大小
    if (config.send_buffer_size > 0) {
        int size = config.send_buffer_size;
        if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, 
                      (const char*)&size, sizeof(size)) == SOCKET_ERROR) {
            return get_last_error("set send buffer size");
        }
    }
    
    // 设置接收缓冲区大小
    if (config.recv_buffer_size > 0) {
        int size = config.recv_buffer_size;
        if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, 
                      (const char*)&size, sizeof(size)) == SOCKET_ERROR) {
            return get_last_error("set receive buffer size");
        }
    }
    
    // 设置超时
    auto timeout_result = set_timeout(config.send_timeout, config.recv_timeout);
    if (!timeout_result.is_success()) {
        return timeout_result;
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::set_non_blocking(bool non_blocking) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) == SOCKET_ERROR) {
        return get_last_error("set non-blocking");
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        return get_last_error("get socket flags");
    }
    
    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    if (fcntl(socket_, F_SETFL, flags) == -1) {
        return get_last_error("set socket flags");
    }
#endif
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::set_timeout(std::chrono::milliseconds send_timeout, 
                                        std::chrono::milliseconds recv_timeout) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not created");
    }
    
#ifdef _WIN32
    // Windows使用毫秒
    DWORD send_ms = static_cast<DWORD>(send_timeout.count());
    DWORD recv_ms = static_cast<DWORD>(recv_timeout.count());
    
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, 
                  (const char*)&send_ms, sizeof(send_ms)) == SOCKET_ERROR) {
        return get_last_error("set send timeout");
    }
    
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, 
                  (const char*)&recv_ms, sizeof(recv_ms)) == SOCKET_ERROR) {
        return get_last_error("set receive timeout");
    }
#else
    // Unix使用timeval结构
    struct timeval send_tv, recv_tv;
    
    send_tv.tv_sec = send_timeout.count() / 1000;
    send_tv.tv_usec = (send_timeout.count() % 1000) * 1000;
    
    recv_tv.tv_sec = recv_timeout.count() / 1000;
    recv_tv.tv_usec = (recv_timeout.count() % 1000) * 1000;
    
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, 
                  &send_tv, sizeof(send_tv)) == SOCKET_ERROR) {
        return get_last_error("set send timeout");
    }
    
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, 
                  &recv_tv, sizeof(recv_tv)) == SOCKET_ERROR) {
        return get_last_error("set receive timeout");
    }
#endif
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

void ManagedSocket::close() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    if (socket_ != INVALID_SOCKET && !closed_.load()) {
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = INVALID_SOCKET;
        closed_.store(true);
    }
}

NetworkResult ManagedSocket::wait_for_io(bool for_write, std::chrono::milliseconds timeout) {
    if (socket_ == INVALID_SOCKET) {
        return NetworkResult(NetworkErrorCode::INVALID_PARAM, "Socket not valid");
    }
    
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(socket_, &fdset);
    
    struct timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    
    int result;
    if (for_write) {
        result = select(static_cast<int>(socket_) + 1, nullptr, &fdset, nullptr, &tv);
    } else {
        result = select(static_cast<int>(socket_) + 1, &fdset, nullptr, nullptr, &tv);
    }
    
    if (result == 0) {
        return NetworkResult(NetworkErrorCode::TIMEOUT, "I/O timeout");
    } else if (result == SOCKET_ERROR) {
        return get_last_error("select");
    }
    
    return NetworkResult(NetworkErrorCode::SUCCESS);
}

NetworkResult ManagedSocket::get_last_error(const std::string& operation) {
#ifdef _WIN32
    int error = WSAGetLastError();
#else
    int error = errno;
#endif
    
    NetworkErrorCode code = map_system_error(error);
    std::string message = operation;
    if (!message.empty()) {
        message += ": ";
    }
    message += error_to_string(code) + " (code " + std::to_string(error) + ")";
    
    return NetworkResult(code, message);
}

NetworkErrorCode ManagedSocket::map_system_error(int error_code) {
#ifdef _WIN32
    switch (error_code) {
        case WSAEACCES: return NetworkErrorCode::CONNECTION_REFUSED;
        case WSAEADDRINUSE: return NetworkErrorCode::ADDRESS_IN_USE;
        case WSAECONNREFUSED: return NetworkErrorCode::CONNECTION_REFUSED;
        case WSAECONNRESET: return NetworkErrorCode::PEER_CLOSED;
        case WSAENETUNREACH: return NetworkErrorCode::NETWORK_UNREACHABLE;
        case WSAETIMEDOUT: return NetworkErrorCode::TIMEOUT;
        case WSAEINTR: return NetworkErrorCode::INTERRUPTED;
        case WSAEINVAL: return NetworkErrorCode::INVALID_PARAM;
        case WSAEWOULDBLOCK: return NetworkErrorCode::TIMEOUT;
        default: return NetworkErrorCode::UNKNOWN_ERROR;
    }
#else
    switch (error_code) {
        case EACCES: return NetworkErrorCode::CONNECTION_REFUSED;
        case EADDRINUSE: return NetworkErrorCode::ADDRESS_IN_USE;
        case ECONNREFUSED: return NetworkErrorCode::CONNECTION_REFUSED;
        case ECONNRESET: return NetworkErrorCode::PEER_CLOSED;
        case ENETUNREACH: return NetworkErrorCode::NETWORK_UNREACHABLE;
        case ETIMEDOUT: return NetworkErrorCode::TIMEOUT;
        case EINTR: return NetworkErrorCode::INTERRUPTED;
        case EINVAL: return NetworkErrorCode::INVALID_PARAM;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            return NetworkErrorCode::TIMEOUT;
        default: return NetworkErrorCode::UNKNOWN_ERROR;
    }
#endif
}

std::string ManagedSocket::error_to_string(NetworkErrorCode code) {
    switch (code) {
        case NetworkErrorCode::SUCCESS: return "Success";
        case NetworkErrorCode::INIT_FAILED: return "Network initialization failed";
        case NetworkErrorCode::CREATE_SOCKET_FAILED: return "Create socket failed";
        case NetworkErrorCode::BIND_FAILED: return "Bind failed";
        case NetworkErrorCode::LISTEN_FAILED: return "Listen failed";
        case NetworkErrorCode::CONNECT_FAILED: return "Connect failed";
        case NetworkErrorCode::ACCEPT_FAILED: return "Accept failed";
        case NetworkErrorCode::SEND_FAILED: return "Send failed";
        case NetworkErrorCode::RECEIVE_FAILED: return "Receive failed";
        case NetworkErrorCode::TIMEOUT: return "Operation timeout";
        case NetworkErrorCode::PEER_CLOSED: return "Peer closed connection";
        case NetworkErrorCode::INVALID_ADDRESS: return "Invalid address";
        case NetworkErrorCode::ADDRESS_IN_USE: return "Address already in use";
        case NetworkErrorCode::NETWORK_UNREACHABLE: return "Network unreachable";
        case NetworkErrorCode::CONNECTION_REFUSED: return "Connection refused";
        case NetworkErrorCode::INTERRUPTED: return "Operation interrupted";
        case NetworkErrorCode::INVALID_PARAM: return "Invalid parameter";
        case NetworkErrorCode::BUFFER_TOO_SMALL: return "Buffer too small";
        case NetworkErrorCode::PARTIAL_SEND: return "Partial send";
        case NetworkErrorCode::UNKNOWN_ERROR: return "Unknown error";
        default: return "Unrecognized error code";
    }
}

// =============================================================================
// ConnectionPool Implementation
// =============================================================================

ConnectionPool::ConnectionPool(size_t max_connections, std::chrono::minutes idle_timeout)
    : max_connections_(max_connections), idle_timeout_(idle_timeout) {
    cleanup_thread_ = std::thread(&ConnectionPool::cleanup_worker, this);
}

ConnectionPool::~ConnectionPool() {
    cleanup_running_.store(false);
    pool_cv_.notify_all();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

std::shared_ptr<ConnectionPool::ConnectionInfo> 
ConnectionPool::acquire_connection(const std::string& address, uint16_t port) {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // 查找现有连接
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [&](const std::shared_ptr<ConnectionInfo>& conn) {
            return conn->address == address && conn->port == port && 
                   !conn->in_use.load() && conn->socket && conn->socket->is_valid();
        });
    
    if (it != connections_.end()) {
        (*it)->in_use.store(true);
        (*it)->last_used = std::chrono::steady_clock::now();
        return *it;
    }
    
    // 如果达到连接数上限，等待或创建新连接
    if (connections_.size() >= max_connections_) {
        pool_cv_.wait(lock, [this] {
            return std::any_of(connections_.begin(), connections_.end(),
                [](const std::shared_ptr<ConnectionInfo>& conn) {
                    return !conn->in_use.load();
                }) || connections_.size() < max_connections_;
        });
    }
    
    // 创建新连接
    auto conn = std::make_shared<ConnectionInfo>(address, port);
    conn->socket = std::make_unique<ManagedSocket>();
    
    // 尝试连接
    auto result = conn->socket->create();
    if (result.is_success()) {
        result = conn->socket->connect(address, port);
    }
    
    if (result.is_success()) {
        conn->in_use.store(true);
        connections_.push_back(conn);
        return conn;
    }
    
    return nullptr;
}

void ConnectionPool::release_connection(std::shared_ptr<ConnectionInfo> conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(pool_mutex_);
    conn->in_use.store(false);
    conn->last_used = std::chrono::steady_clock::now();
    pool_cv_.notify_one();
}

void ConnectionPool::cleanup_idle_connections() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [&](const std::shared_ptr<ConnectionInfo>& conn) {
                bool is_idle = !conn->in_use.load() && 
                              (now - conn->last_used > idle_timeout_);
                return is_idle || !conn->socket || !conn->socket->is_valid();
            }),
        connections_.end()
    );
}

size_t ConnectionPool::get_active_count() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return std::count_if(connections_.begin(), connections_.end(),
        [](const std::shared_ptr<ConnectionInfo>& conn) {
            return conn->in_use.load();
        });
}

size_t ConnectionPool::get_total_count() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return connections_.size();
}

void ConnectionPool::cleanup_worker() {
    while (cleanup_running_.load()) {
        std::this_thread::sleep_for(std::chrono::minutes(1));
        cleanup_idle_connections();
    }
}

// =============================================================================
// SignalManager Implementation
// =============================================================================

SignalManager& SignalManager::getInstance() {
    static SignalManager instance;
    return instance;
}

void SignalManager::install_handlers() {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // 忽略SIGPIPE
#endif
}

void SignalManager::set_shutdown_handler(SignalHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    shutdown_handler_ = handler;
}

void SignalManager::request_shutdown() {
    shutdown_requested_.store(true);
    std::lock_guard<std::mutex> lock(handler_mutex_);
    if (shutdown_handler_) {
        shutdown_handler_(0);
    }
}

#ifdef _WIN32
BOOL WINAPI SignalManager::console_ctrl_handler(DWORD ctrl_type) {
    SignalManager& manager = getInstance();
    
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            manager.request_shutdown();
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void SignalManager::signal_handler(int signal) {
    SignalManager& manager = getInstance();
    manager.request_shutdown();
}
#endif

// =============================================================================
// AddressResolver Implementation
// =============================================================================

NetworkResult AddressResolver::resolve(const std::string& hostname, uint16_t port, 
                                       ResolvedAddress& result) {
    memset(&result.addr, 0, sizeof(result.addr));
    result.addr.sin_family = AF_INET;
    result.addr.sin_port = htons(port);
    result.port = port;
    
    // 首先尝试作为IP地址解析
    if (inet_pton(AF_INET, hostname.c_str(), &result.addr.sin_addr) == 1) {
        result.ip_string = hostname;
        return NetworkResult(NetworkErrorCode::SUCCESS);
    }
    
    // 如果不是IP地址，进行DNS解析
    struct addrinfo hints, *addr_info = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &addr_info);
    if (status != 0) {
        if (addr_info) freeaddrinfo(addr_info);
        return NetworkResult(NetworkErrorCode::INVALID_ADDRESS, 
                           "DNS resolution failed: " + hostname);
    }
    
    if (addr_info && addr_info->ai_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr_info->ai_addr;
        result.addr.sin_addr = sin->sin_addr;
        
        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &result.addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
            result.ip_string = ip_str;
        }
        
        freeaddrinfo(addr_info);
        return NetworkResult(NetworkErrorCode::SUCCESS);
    }
    
    if (addr_info) freeaddrinfo(addr_info);
    return NetworkResult(NetworkErrorCode::INVALID_ADDRESS, "No valid IPv4 address found");
}

bool AddressResolver::is_valid_ipv4(const std::string& ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) == 1;
}

std::string AddressResolver::get_local_ip() {
    // 尝试连接到一个远程地址以获取本地IP
    ManagedSocket socket;
    if (socket.create().is_success()) {
        if (socket.connect("8.8.8.8", 53, std::chrono::milliseconds{1000}).is_success()) {
            struct sockaddr_in local_addr;
            socklen_t len = sizeof(local_addr);
            if (getsockname(socket.get_socket(), (struct sockaddr*)&local_addr, &len) == 0) {
                char ip_str[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
                    return std::string(ip_str);
                }
            }
        }
    }
    return "127.0.0.1"; // 回退到localhost
}

std::vector<std::string> AddressResolver::get_all_local_ips() {
    std::vector<std::string> ips;
    
#ifdef _WIN32
    ULONG buffer_size = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &buffer_size);
    
    if (buffer_size > 0) {
        std::vector<char> buffer(buffer_size);
        PIP_ADAPTER_ADDRESSES adapter_addresses = (PIP_ADAPTER_ADDRESSES)buffer.data();
        
        if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapter_addresses, &buffer_size) == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses; adapter; adapter = adapter->Next) {
                for (PIP_ADAPTER_UNICAST_ADDRESS addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
                    if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                        struct sockaddr_in* sin = (struct sockaddr_in*)addr->Address.lpSockaddr;
                        char ip_str[INET_ADDRSTRLEN];
                        if (inet_ntop(AF_INET, &sin->sin_addr, ip_str, INET_ADDRSTRLEN)) {
                            std::string ip(ip_str);
                            if (ip != "127.0.0.1") {
                                ips.push_back(ip);
                            }
                        }
                    }
                }
            }
        }
    }
#else
    struct ifaddrs *if_addrs = nullptr;
    if (getifaddrs(&if_addrs) == 0) {
        for (struct ifaddrs *ifa = if_addrs; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
                char ip_str[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &sin->sin_addr, ip_str, INET_ADDRSTRLEN)) {
                    std::string ip(ip_str);
                    if (ip != "127.0.0.1") {
                        ips.push_back(ip);
                    }
                }
            }
        }
        freeifaddrs(if_addrs);
    }
#endif
    
    // 如果没有找到任何IP，添加localhost
    if (ips.empty()) {
        ips.push_back("127.0.0.1");
    }
    
    return ips;
}

} // namespace communication
} // namespace plc_runtime