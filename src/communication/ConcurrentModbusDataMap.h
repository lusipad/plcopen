/**
 * @file ConcurrentModbusDataMap.h
 * @brief 高并发Modbus数据映射实现
 * @version 1.0
 * @date 2025-09-09
 * 
 * 针对读多写少场景优化的Modbus数据映射，支持批量操作和原子性保证
 */

#pragma once

#include <vector>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace plc_runtime {
namespace communication {

/**
 * @brief 批量操作结果
 */
struct BatchOperationResult {
    size_t successful_operations = 0;
    size_t failed_operations = 0;
    std::vector<std::pair<uint16_t, std::string>> errors; // address -> error_message
    
    bool is_success() const { return failed_operations == 0; }
    double success_rate() const { 
        size_t total = successful_operations + failed_operations;
        return total > 0 ? static_cast<double>(successful_operations) / total : 1.0;
    }
};

/**
 * @brief 数据变更通知
 */
struct DataChangeNotification {
    enum Type { COIL, DISCRETE_INPUT, HOLDING_REGISTER, INPUT_REGISTER };
    
    Type type;
    uint16_t start_address;
    uint16_t count;
    std::vector<uint16_t> changed_values; // 对于寄存器
    std::vector<bool> changed_coils;      // 对于线圈
};

/**
 * @brief 高并发Modbus数据映射
 * 
 * 特性：
 * - 读写锁优化：读多写少场景性能优化
 * - 批量操作：原子性批量读写
 * - 变更通知：数据变更回调
 * - 范围验证：地址边界检查
 * - 内存对齐：缓存友好的数据布局
 */
class ConcurrentModbusDataMap {
public:
    /**
     * @brief 配置结构
     */
    struct Config {
        uint32_t max_coils = 65536;
        uint32_t max_discrete_inputs = 65536;
        uint32_t max_holding_registers = 65536;
        uint32_t max_input_registers = 65536;
        bool enable_change_notification = false;
        bool enable_bounds_checking = true;
        bool enable_statistics = false;
    };

    /**
     * @brief 统计信息
     */
    struct Statistics {
        std::atomic<uint64_t> total_reads{0};
        std::atomic<uint64_t> total_writes{0};
        std::atomic<uint64_t> batch_reads{0};
        std::atomic<uint64_t> batch_writes{0};
        std::atomic<uint64_t> bounds_errors{0};
        std::atomic<uint64_t> concurrent_readers{0};
        std::atomic<uint64_t> concurrent_writers{0};
        std::atomic<uint64_t> max_concurrent_readers{0};
        
        void reset() {
            total_reads.store(0);
            total_writes.store(0);
            batch_reads.store(0);
            batch_writes.store(0);
            bounds_errors.store(0);
            concurrent_readers.store(0);
            concurrent_writers.store(0);
            max_concurrent_readers.store(0);
        }
    };

    explicit ConcurrentModbusDataMap(const Config& config = {});
    ~ConcurrentModbusDataMap() = default;

    // 禁止拷贝和赋值（避免数据竞争）
    ConcurrentModbusDataMap(const ConcurrentModbusDataMap&) = delete;
    ConcurrentModbusDataMap& operator=(const ConcurrentModbusDataMap&) = delete;

    // === 单个操作接口 ===
    
    /**
     * @brief 读取单个线圈
     */
    std::optional<bool> read_coil(uint16_t address) const;
    
    /**
     * @brief 写入单个线圈
     */
    bool write_coil(uint16_t address, bool value);
    
    /**
     * @brief 读取单个离散输入
     */
    std::optional<bool> read_discrete_input(uint16_t address) const;
    
    /**
     * @brief 读取单个保持寄存器
     */
    std::optional<uint16_t> read_holding_register(uint16_t address) const;
    
    /**
     * @brief 写入单个保持寄存器
     */
    bool write_holding_register(uint16_t address, uint16_t value);
    
    /**
     * @brief 读取单个输入寄存器
     */
    std::optional<uint16_t> read_input_register(uint16_t address) const;

    // === 批量操作接口 ===
    
    /**
     * @brief 批量读取线圈
     */
    BatchOperationResult read_coils(uint16_t start_address, uint16_t count, std::vector<bool>& values) const;
    
    /**
     * @brief 批量写入线圈
     */
    BatchOperationResult write_coils(uint16_t start_address, const std::vector<bool>& values);
    
    /**
     * @brief 批量读取保持寄存器
     */
    BatchOperationResult read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t>& values) const;
    
    /**
     * @brief 批量写入保持寄存器
     */
    BatchOperationResult write_holding_registers(uint16_t start_address, const std::vector<uint16_t>& values);

    // === 原子性批量操作 ===
    
    /**
     * @brief 原子性批量写入线圈（全部成功或全部失败）
     */
    bool atomic_write_coils(uint16_t start_address, const std::vector<bool>& values);
    
    /**
     * @brief 原子性批量写入寄存器
     */
    bool atomic_write_holding_registers(uint16_t start_address, const std::vector<uint16_t>& values);

    // === 变更通知 ===
    
    /**
     * @brief 设置数据变更回调
     */
    void set_change_callback(std::function<void(const DataChangeNotification&)> callback);
    
    /**
     * @brief 移除变更回调
     */
    void clear_change_callback();

    // === 状态和统计 ===
    
    /**
     * @brief 获取配置
     */
    const Config& get_config() const { return config_; }
    
    /**
     * @brief 获取统计信息
     */
    const Statistics& get_statistics() const { return statistics_; }
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics() { statistics_.reset(); }
    
    /**
     * @brief 获取内存使用情况
     */
    size_t get_memory_usage() const;

    // === 数据初始化 ===
    
    /**
     * @brief 批量初始化线圈
     */
    void initialize_coils(const std::vector<bool>& initial_values, uint16_t start_address = 0);
    
    /**
     * @brief 批量初始化保持寄存器
     */
    void initialize_holding_registers(const std::vector<uint16_t>& initial_values, uint16_t start_address = 0);

private:
    Config config_;
    mutable Statistics statistics_;
    
    // 数据存储（缓存行对齐优化）
    alignas(64) std::vector<std::atomic<bool>> coils_;
    alignas(64) std::vector<std::atomic<bool>> discrete_inputs_;
    alignas(64) std::vector<std::atomic<uint16_t>> holding_registers_;
    alignas(64) std::vector<std::atomic<uint16_t>> input_registers_;
    
    // 读写锁（按数据类型分离，减少锁竞争）
    mutable std::shared_mutex coils_mutex_;
    mutable std::shared_mutex discrete_inputs_mutex_;
    mutable std::shared_mutex holding_registers_mutex_;
    mutable std::shared_mutex input_registers_mutex_;
    
    // 变更通知
    std::function<void(const DataChangeNotification&)> change_callback_;
    std::mutex callback_mutex_;
    
    // 辅助方法
    bool is_valid_coil_address(uint16_t address) const;
    bool is_valid_holding_register_address(uint16_t address) const;
    bool is_valid_discrete_input_address(uint16_t address) const;
    bool is_valid_input_register_address(uint16_t address) const;
    
    void notify_change(const DataChangeNotification& notification);
    void update_reader_stats() const;
    void update_writer_stats() const;
};

/**
 * @brief RAII范围锁管理器
 */
template<typename MutexType>
class ScopedStatsUpdater {
public:
    ScopedStatsUpdater(MutexType& mutex, std::atomic<uint64_t>& counter, std::atomic<uint64_t>& max_counter)
        : lock_(mutex), counter_(counter), max_counter_(max_counter) {
        auto current = counter_.fetch_add(1) + 1;
        auto current_max = max_counter_.load();
        while (current > current_max && !max_counter_.compare_exchange_weak(current_max, current)) {
            current_max = max_counter_.load();
        }
    }
    
    ~ScopedStatsUpdater() {
        counter_.fetch_sub(1);
    }
    
private:
    std::shared_lock<MutexType> lock_;
    std::atomic<uint64_t>& counter_;
    std::atomic<uint64_t>& max_counter_;
};

} // namespace communication
} // namespace plc_runtime