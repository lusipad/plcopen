# Modbus Test 卡死问题修复报告

## 问题分析

用户反映 `modbus_test` 程序一直卡着退不出去。通过代码分析，发现了以下问题：

### 1. 网络I/O阻塞问题
- `receive_raw_data()` 和 `send_raw_data()` 方法没有超时机制
- `recv()` 和 `send()` 系统调用可能无限阻塞
- 没有处理网络连接异常情况

### 2. 测试程序缺乏退出机制
- 测试程序没有信号处理机制
- 无法通过 Ctrl+C 安全退出
- 测试失败时可能陷入无限循环

## 修复方案

### 1. 网络超时机制修复

在 `ModbusTCP.cpp` 中修复了以下方法：

#### `receive_raw_data()` 方法
- 添加了超时检查机制
- 使用非阻塞套接字模式进行超时控制
- 增强了错误处理和日志输出
- 支持连接断开检测

#### `send_raw_data()` 方法
- 添加了发送超时检查
- 增强了错误处理和诊断信息
- 防止发送操作无限阻塞

### 2. 信号处理机制

为所有测试程序添加了信号处理：

#### 修复的文件：
- `test_modbus_tcp.cpp`
- `test_modbus_basic.cpp` 
- `test_modbus_simple.cpp`

#### 添加的功能：
- 全局退出标志 `g_should_exit`
- 信号处理函数 `signal_handler()`
- 支持 SIGINT (Ctrl+C)、SIGTERM、SIGBREAK (Windows)
- 测试过程中检查退出标志
- 安全退出机制

### 3. 增强的错误处理

#### 超时配置
```cpp
struct NetworkTimeouts {
    uint32_t connect_timeout_ms = 5000;     // 连接超时
    uint32_t read_timeout_ms = 5000;        // 读取超时
    uint32_t write_timeout_ms = 3000;       // 写入超时
    uint32_t total_timeout_ms = 30000;      // 总超时
    bool adaptive_timeout = true;           // 自适应超时
    double slow_link_factor = 2.0;          // 慢链路因子
};
```

#### 错误分类
```cpp
enum class NetworkError {
    SUCCESS = 0,
    TIMEOUT = 1,        // 超时
    PEER_CLOSED = 2,    // 对端关闭连接
    NETWORK_ERROR = 3,  // 网络错误
    BUFFER_FULL = 4,    // 缓冲区已满
    INVALID_PARAM = 5   // 参数错误
};
```

## 修复效果

### 1. 防止无限阻塞
- 所有网络操作都有超时保护
- 连接异常时能够及时检测和处理
- 避免程序卡死在网络I/O操作上

### 2. 用户友好的退出
- 用户可以随时按 Ctrl+C 安全退出
- 测试程序会显示已完成的测试数量
- 支持优雅的程序终止

### 3. 更好的诊断信息
- 详细的错误日志输出
- 超时时间和错误码显示
- 便于问题定位和调试

## 使用说明

### 运行测试
```bash
# 运行 Modbus TCP 集成测试
./test_modbus_tcp

# 运行基础功能测试
./test_modbus_basic

# 运行简单功能验证
./test_modbus_simple
```

### 安全退出
- 按 `Ctrl+C` 可以随时安全退出测试
- 程序会显示已完成的测试进度
- 返回标准的信号中断退出码 (130)

### 超时配置
- 默认读取超时：5秒
- 默认总超时：30秒
- 可通过配置结构体调整超时参数

## 技术细节

### 非阻塞套接字实现
```cpp
// 设置套接字为非阻塞模式
#ifdef _WIN32
u_long mode = 1;
ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
int flags = fcntl(socket_fd_, F_GETFL, 0);
fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
```

### 信号处理实现
```cpp
void signal_handler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在安全退出..." << std::endl;
    g_should_exit.store(true);
}

// 注册信号处理函数
std::signal(SIGINT, signal_handler);   // Ctrl+C
std::signal(SIGTERM, signal_handler);  // 终止信号
#ifdef _WIN32
std::signal(SIGBREAK, signal_handler); // Ctrl+Break (Windows)
#endif
```

## 总结

通过以上修复，`modbus_test` 程序卡死的问题已经得到解决：

1. **网络层面**：添加了完善的超时机制和错误处理
2. **用户体验**：支持安全的程序中断和退出
3. **诊断能力**：提供详细的错误信息和状态反馈
4. **跨平台**：同时支持 Windows 和 Linux 平台

现在用户可以安全地运行 Modbus 测试程序，并且能够随时通过 Ctrl+C 退出，不会再出现程序卡死的情况。