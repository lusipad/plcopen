/**
 * @file ModbusTCP.h
 * @brief Modbus TCP通信协议实现
 * @version 1.0
 * @date 2025-09-06
 * 
 * 实现标准Modbus TCP协议，支持主站和从站模式
 * 符合Modbus Application Protocol Specification V1.1b3
 */

#ifndef COMMUNICATION_MODBUS_TCP_H
#define COMMUNICATION_MODBUS_TCP_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>

// 前向声明测试类
class ModbusMBAPBoundaryTest;

namespace plc_runtime {
namespace communication {

/**
 * @brief Modbus功能码
 */
enum class ModbusFunction : uint8_t {
    READ_COILS = 0x01,
    READ_DISCRETE_INPUTS = 0x02,
    READ_HOLDING_REGISTERS = 0x03,
    READ_INPUT_REGISTERS = 0x04,
    WRITE_SINGLE_COIL = 0x05,
    WRITE_SINGLE_REGISTER = 0x06,
    WRITE_MULTIPLE_COILS = 0x0F,
    WRITE_MULTIPLE_REGISTERS = 0x10,
    
    // 异常响应
    EXCEPTION_RESPONSE = 0x80
};

/**
 * @brief Modbus异常码
 */
enum class ModbusException : uint8_t {
    ILLEGAL_FUNCTION = 0x01,
    ILLEGAL_DATA_ADDRESS = 0x02,
    ILLEGAL_DATA_VALUE = 0x03,
    SLAVE_DEVICE_FAILURE = 0x04,
    ACKNOWLEDGE = 0x05,
    SLAVE_DEVICE_BUSY = 0x06,
    MEMORY_PARITY_ERROR = 0x08,
    GATEWAY_PATH_UNAVAILABLE = 0x0A,
    GATEWAY_TARGET_FAILED = 0x0B
};

/**
 * @brief Modbus PDU (Protocol Data Unit)
 */
struct ModbusPDU {
    uint8_t function_code;
    std::vector<uint8_t> data;
    
    ModbusPDU() : function_code(0) {}
    ModbusPDU(uint8_t func, const std::vector<uint8_t>& d) 
        : function_code(func), data(d) {}
};

/**
 * @brief Modbus ADU (Application Data Unit) for TCP
 */
struct ModbusTcpADU {
    uint16_t transaction_id;
    uint16_t protocol_id = 0;
    uint16_t length;
    uint8_t unit_id;
    ModbusPDU pdu;
    
    ModbusTcpADU() : transaction_id(0), length(0), unit_id(0) {}
};

/**
 * @brief Modbus数据区域
 */
enum class ModbusDataArea {
    COILS,              // 线圈 (0x区)
    DISCRETE_INPUTS,    // 离散输入 (1x区)
    INPUT_REGISTERS,    // 输入寄存器 (3x区)
    HOLDING_REGISTERS   // 保持寄存器 (4x区)
};

/**
 * @brief Modbus通信统计
 */
struct ModbusStatistics {
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> errors_occurred{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> invalid_responses{0};
    std::chrono::steady_clock::time_point last_communication;
    std::chrono::milliseconds average_response_time{0};
    
    // 重置统计信息的方法
    void reset() {
        messages_sent.store(0);
        messages_received.store(0);
        errors_occurred.store(0);
        timeouts.store(0);
        invalid_responses.store(0);
        last_communication = std::chrono::steady_clock::time_point{};
        average_response_time = std::chrono::milliseconds{0};
    }
};

/**
 * @brief 可复制的统计快照
 */
struct ModbusStatisticsSnapshot {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t errors_occurred;
    uint64_t timeouts;
    uint64_t invalid_responses;
    std::chrono::steady_clock::time_point last_communication;
    std::chrono::milliseconds average_response_time;
    
    // 从原子统计结构创建快照
    static ModbusStatisticsSnapshot from_atomic(const ModbusStatistics& stats) {
        ModbusStatisticsSnapshot snapshot;
        snapshot.messages_sent = stats.messages_sent.load();
        snapshot.messages_received = stats.messages_received.load();
        snapshot.errors_occurred = stats.errors_occurred.load();
        snapshot.timeouts = stats.timeouts.load();
        snapshot.invalid_responses = stats.invalid_responses.load();
        snapshot.last_communication = stats.last_communication;
        snapshot.average_response_time = stats.average_response_time;
        return snapshot;
    }
};

/**
 * @brief Modbus数据映射接口
 */
class ModbusDataMap {
public:
    virtual ~ModbusDataMap() = default;
    
    // 线圈操作
    virtual bool read_coil(uint16_t address) = 0;
    virtual bool write_coil(uint16_t address, bool value) = 0;
    virtual bool read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values) = 0;
    virtual bool write_coils(uint16_t start_address, const std::vector<bool>& values) = 0;
    
    // 离散输入操作
    virtual bool read_discrete_input(uint16_t address) = 0;
    virtual bool read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values) = 0;
    
    // 寄存器操作
    virtual uint16_t read_holding_register(uint16_t address) = 0;
    virtual bool write_holding_register(uint16_t address, uint16_t value) = 0;
    virtual bool read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) = 0;
    virtual bool write_holding_registers(uint16_t start_address, const std::vector<uint16_t>& values) = 0;
    
    virtual uint16_t read_input_register(uint16_t address) = 0;
    virtual bool read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) = 0;
    
    // 地址验证
    virtual bool is_valid_address(ModbusDataArea area, uint16_t address) const = 0;
};

/**
 * @brief 默认内存映射实现
 */
class DefaultModbusDataMap : public ModbusDataMap {
public:
    struct Config {
        uint16_t max_coils;
        uint16_t max_discrete_inputs;
        uint16_t max_holding_registers;
        uint16_t max_input_registers;
        
        Config() : max_coils(10000), max_discrete_inputs(10000), 
                  max_holding_registers(10000), max_input_registers(10000) {}
    };
    
    explicit DefaultModbusDataMap(const Config& config = Config());
    
    // 实现ModbusDataMap接口
    bool read_coil(uint16_t address) override;
    bool write_coil(uint16_t address, bool value) override;
    bool read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values) override;
    bool write_coils(uint16_t start_address, const std::vector<bool>& values) override;
    
    bool read_discrete_input(uint16_t address) override;
    bool read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values) override;
    
    uint16_t read_holding_register(uint16_t address) override;
    bool write_holding_register(uint16_t address, uint16_t value) override;
    bool read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) override;
    bool write_holding_registers(uint16_t start_address, const std::vector<uint16_t>& values) override;
    
    uint16_t read_input_register(uint16_t address) override;
    bool read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) override;
    
    bool is_valid_address(ModbusDataArea area, uint16_t address) const override;

private:
    Config config_;
    std::vector<bool> coils_;
    std::vector<bool> discrete_inputs_;
    std::vector<uint16_t> holding_registers_;
    std::vector<uint16_t> input_registers_;
    mutable std::mutex data_mutex_;
};

/**
 * @brief Modbus TCP客户端（主站）
 */
class ModbusTcpClient {
    // 友元声明，允许测试类访问私有成员
    friend class ::ModbusMBAPBoundaryTest;
    
public:
    struct Config {
        std::string server_host;
        uint16_t server_port;
        std::chrono::milliseconds timeout;
        uint8_t unit_id;
        bool auto_reconnect;
        uint16_t max_retries;
        
        Config() : server_host("localhost"), server_port(502), timeout(5000),
                  unit_id(1), auto_reconnect(true), max_retries(3) {}
    };
    
    explicit ModbusTcpClient(const Config& config = Config());
    ~ModbusTcpClient();
    
    // 禁用拷贝
    ModbusTcpClient(const ModbusTcpClient&) = delete;
    ModbusTcpClient& operator=(const ModbusTcpClient&) = delete;
    
    // 连接管理
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_.load(); }
    
    // 线圈操作
    bool read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values);
    bool write_single_coil(uint16_t address, bool value);
    bool write_multiple_coils(uint16_t start_address, const std::vector<bool>& values);
    
    // 离散输入操作
    bool read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool>& values);
    
    // 寄存器操作
    bool read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values);
    bool write_single_register(uint16_t address, uint16_t value);
    bool write_multiple_registers(uint16_t start_address, const std::vector<uint16_t>& values);
    
    bool read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values);
    
    // 统计信息
    ModbusStatisticsSnapshot get_statistics() const { return ModbusStatisticsSnapshot::from_atomic(statistics_); }
    void reset_statistics();
    
    // 测试辅助方法（公开用于单元测试）
    bool deserialize_adu(const std::vector<uint8_t>& data, ModbusTcpADU& adu);

private:
    Config config_;
    std::atomic<bool> connected_{false};
    int socket_fd_ = -1;
    std::atomic<uint16_t> transaction_id_{1};
    ModbusStatistics statistics_;
    mutable std::mutex socket_mutex_;
    
    // 内部方法
    bool send_request(const ModbusTcpADU& request, ModbusTcpADU& response);
    bool send_raw_data(const std::vector<uint8_t>& data);
    bool receive_raw_data(std::vector<uint8_t>& data, size_t expected_length);
    std::vector<uint8_t> serialize_adu(const ModbusTcpADU& adu);
    uint16_t next_transaction_id() { return transaction_id_.fetch_add(1); }
    void update_statistics(bool success, const std::chrono::steady_clock::time_point& start_time);
};

/**
 * @brief Modbus TCP服务器（从站）
 */
class ModbusTcpServer {
public:
    struct Config {
        std::string bind_address;
        uint16_t port;
        uint8_t unit_id;
        uint16_t max_connections;
        std::chrono::milliseconds client_timeout;
        bool enable_broadcast;
        
        Config() : bind_address("0.0.0.0"), port(502), unit_id(1),
                  max_connections(10), client_timeout(30000), enable_broadcast(false) {}
    };
    
    explicit ModbusTcpServer(const Config& config = Config());
    ~ModbusTcpServer();
    
    // 禁用拷贝
    ModbusTcpServer(const ModbusTcpServer&) = delete;
    ModbusTcpServer& operator=(const ModbusTcpServer&) = delete;
    
    // 服务器管理
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // 数据映射
    void set_data_map(std::shared_ptr<ModbusDataMap> data_map);
    
    // 事件回调
    using ClientConnectedCallback = std::function<void(const std::string& client_ip, uint16_t client_port)>;
    using ClientDisconnectedCallback = std::function<void(const std::string& client_ip, uint16_t client_port)>;
    using RequestReceivedCallback = std::function<void(const std::string& client_ip, const ModbusPDU& request)>;
    
    void set_client_connected_callback(ClientConnectedCallback callback);
    void set_client_disconnected_callback(ClientDisconnectedCallback callback);
    void set_request_received_callback(RequestReceivedCallback callback);
    
    // 统计信息
    ModbusStatisticsSnapshot get_statistics() const { return ModbusStatisticsSnapshot::from_atomic(statistics_); }
    void reset_statistics();
    size_t get_active_connections() const;

private:
    Config config_;
    std::shared_ptr<ModbusDataMap> data_map_;
    std::atomic<bool> running_{false};
    int server_socket_ = -1;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    ModbusStatistics statistics_;
    mutable std::mutex clients_mutex_;
    
    // 回调函数
    ClientConnectedCallback client_connected_callback_;
    ClientDisconnectedCallback client_disconnected_callback_;
    RequestReceivedCallback request_received_callback_;
    
    // 内部方法
    void accept_clients();
    void handle_client(int client_socket, const std::string& client_ip, uint16_t client_port);
    ModbusPDU process_request(const ModbusPDU& request);
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
 * @brief Modbus实用工具类
 */
class ModbusUtils {
public:
    // 数据转换
    static uint16_t bytes_to_uint16(uint8_t high_byte, uint8_t low_byte);
    static void uint16_to_bytes(uint16_t value, uint8_t& high_byte, uint8_t& low_byte);
    static std::vector<bool> bytes_to_bits(const std::vector<uint8_t>& bytes, uint16_t bit_count);
    static std::vector<uint8_t> bits_to_bytes(const std::vector<bool>& bits);
    
    // CRC计算（用于Modbus RTU，TCP不需要）
    static uint16_t calculate_crc16(const std::vector<uint8_t>& data);
    
    // 地址转换
    static uint16_t plc_address_to_modbus(const std::string& plc_address);
    static std::string modbus_address_to_plc(ModbusDataArea area, uint16_t modbus_address);
    
    // 调试工具
    static std::string adu_to_string(const ModbusTcpADU& adu);
    static std::string pdu_to_string(const ModbusPDU& pdu);
    static std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes);
};

} // namespace communication
} // namespace plc_runtime

#endif // COMMUNICATION_MODBUS_TCP_H