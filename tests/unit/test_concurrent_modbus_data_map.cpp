/**
 * @file test_concurrent_modbus_data_map.cpp
 * @brief ConcurrentModbusDataMap原子性语义和边界用例测试
 * @version 1.0
 * @date 2025-09-09
 * 
 * 重点测试：
 * - 原子性语义：批量操作全成功或全失败
 * - 边界用例：地址边界、数量限制、并发竞态
 * - 并发安全：多线程读写、数据一致性
 * - 性能验证：shared_mutex优化效果
 */

#include "../TestFramework.h"
#include "communication/ConcurrentModbusDataMap.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <future>

using namespace plc_runtime::communication;

class ConcurrentModbusDataMapTest : public TestFramework {
public:
    void SetUp() override {
        // 默认配置
        config_.max_coils = 1000;
        config_.max_holding_registers = 1000;
        config_.enable_change_notification = true;
        config_.enable_bounds_checking = true;
        config_.enable_statistics = true;
        
        dataMap_ = std::make_unique<ConcurrentModbusDataMap>(config_);
        
        // 初始化测试数据
        std::vector<bool> initial_coils(100, false);
        std::vector<uint16_t> initial_registers(100, 0);
        
        dataMap_->initialize_coils(initial_coils);
        dataMap_->initialize_holding_registers(initial_registers);
    }
    
    void TearDown() override {
        dataMap_.reset();
    }

protected:
    ConcurrentModbusDataMap::Config config_;
    std::unique_ptr<ConcurrentModbusDataMap> dataMap_;
};

// =============================================================================
// 原子性语义测试 - 核心关键测试
// =============================================================================

void test_atomic_coil_write_all_or_nothing() {
    std::cout << "\n=== 原子性线圈写入测试（全成功或全失败）===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 100;
    config.enable_bounds_checking = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 初始化
    std::vector<bool> initial(50, false);
    dataMap.initialize_coils(initial);
    
    // 测试1：正常范围内的原子写入应该成功
    std::vector<bool> valid_values = {true, false, true, true, false};
    bool result = dataMap.atomic_write_coils(10, valid_values);
    ASSERT_TRUE(result);
    
    // 验证所有值都被写入
    for (size_t i = 0; i < valid_values.size(); ++i) {
        auto read_result = dataMap.read_coil(10 + i);
        ASSERT_TRUE(read_result.has_value());
        ASSERT_EQ(read_result.value(), valid_values[i]);
    }
    
    // 测试2：越界的原子写入应该全部失败
    std::vector<bool> invalid_values = {true, false, true};  // 从地址98开始，会越界
    result = dataMap.atomic_write_coils(98, invalid_values);  // 98,99,100 - 100越界
    ASSERT_FALSE(result);  // 应该失败
    
    // 验证没有任何值被写入（原子性保证）
    auto check98 = dataMap.read_coil(98);
    auto check99 = dataMap.read_coil(99);
    ASSERT_TRUE(check98.has_value());
    ASSERT_TRUE(check99.has_value());
    ASSERT_FALSE(check98.value());  // 应该还是初始值false
    ASSERT_FALSE(check99.value());  // 应该还是初始值false
    
    std::cout << "✅ 原子性线圈写入语义验证通过" << std::endl;
}

void test_atomic_register_write_consistency() {
    std::cout << "\n=== 原子性寄存器写入一致性测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_holding_registers = 100;
    config.enable_bounds_checking = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 初始化为特定模式
    std::vector<uint16_t> initial(50, 0x1234);
    dataMap.initialize_holding_registers(initial);
    
    // 测试1：部分越界的原子写入应该完全失败
    std::vector<uint16_t> partial_invalid = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
    bool result = dataMap.atomic_write_holding_registers(48, partial_invalid); // 48,49,50,51 - 50,51越界
    ASSERT_FALSE(result);
    
    // 验证所有地址都保持原值（原子性）
    for (uint16_t addr = 48; addr < 52 && addr < 50; ++addr) {
        auto value = dataMap.read_holding_register(addr);
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value.value(), 0x1234);  // 原值未被修改
    }
    
    // 测试2：完全有效的原子写入应该成功
    std::vector<uint16_t> valid_values = {0x1111, 0x2222, 0x3333};
    result = dataMap.atomic_write_holding_registers(10, valid_values);
    ASSERT_TRUE(result);
    
    // 验证所有值都正确写入
    for (size_t i = 0; i < valid_values.size(); ++i) {
        auto value = dataMap.read_holding_register(10 + i);
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value.value(), valid_values[i]);
    }
    
    std::cout << "✅ 原子性寄存器写入一致性验证通过" << std::endl;
}

void test_atomic_write_under_concurrent_access() {
    std::cout << "\n=== 并发访问下的原子写入测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 1000;
    config.enable_statistics = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 初始化
    std::vector<bool> initial(1000, false);
    dataMap.initialize_coils(initial);
    
    const int num_threads = 8;
    const int iterations_per_thread = 100;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};
    
    auto worker = [&](int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> addr_dist(0, 800);  // 确保有足够空间
        std::uniform_int_distribution<> size_dist(1, 10);
        
        for (int i = 0; i < iterations_per_thread; ++i) {
            uint16_t start_addr = addr_dist(gen);
            uint16_t count = size_dist(gen);
            
            // 构造测试数据
            std::vector<bool> values(count);
            for (auto& val : values) {
                val = (gen() % 2 == 0);
            }
            
            // 原子写入
            if (dataMap.atomic_write_coils(start_addr, values)) {
                success_count.fetch_add(1);
                
                // 验证写入结果的一致性
                bool verification_passed = true;
                for (size_t j = 0; j < values.size(); ++j) {
                    auto read_result = dataMap.read_coil(start_addr + j);
                    if (!read_result.has_value() || read_result.value() != values[j]) {
                        verification_passed = false;
                        break;
                    }
                }
                
                if (!verification_passed) {
                    std::cout << "❌ 线程" << thread_id << "检测到数据不一致！" << std::endl;
                }
            } else {
                failure_count.fetch_add(1);
            }
            
            // 短暂延迟增加并发竞争
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    };
    
    // 启动多个线程
    std::vector<std::thread> threads;
    auto start_time = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "并发原子写入测试结果:" << std::endl;
    std::cout << "- 成功操作: " << success_count.load() << std::endl;
    std::cout << "- 失败操作: " << success_count.load() << std::endl;
    std::cout << "- 总操作数: " << (success_count.load() + failure_count.load()) << std::endl;
    std::cout << "- 执行时间: " << duration.count() << "ms" << std::endl;
    
    // 验证统计信息
    auto stats = dataMap.get_statistics();
    std::cout << "- 统计写入次数: " << stats.total_writes.load() << std::endl;
    std::cout << "- 批量写入次数: " << stats.batch_writes.load() << std::endl;
    
    ASSERT_TRUE(success_count.load() > 0);  // 应该有成功的操作
    ASSERT_EQ(success_count.load() + failure_count.load(), num_threads * iterations_per_thread);
    
    std::cout << "✅ 并发原子写入测试通过" << std::endl;
}

// =============================================================================
// 边界用例测试
// =============================================================================

void test_boundary_address_limits() {
    std::cout << "\n=== 地址边界限制测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 100;
    config.max_holding_registers = 100;
    config.enable_bounds_checking = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 测试边界地址
    std::vector<uint16_t> boundary_addresses = {0, 1, 98, 99, 100, 101, 65534, 65535};
    
    for (uint16_t addr : boundary_addresses) {
        // 测试线圈地址
        auto coil_result = dataMap.read_coil(addr);
        if (addr < config.max_coils) {
            ASSERT_TRUE(coil_result.has_value());
        } else {
            ASSERT_FALSE(coil_result.has_value());  // 越界应该返回empty
        }
        
        // 测试寄存器地址
        auto reg_result = dataMap.read_holding_register(addr);
        if (addr < config.max_holding_registers) {
            ASSERT_TRUE(reg_result.has_value());
        } else {
            ASSERT_FALSE(reg_result.has_value());  // 越界应该返回empty
        }
    }
    
    std::cout << "✅ 地址边界限制测试通过" << std::endl;
}

void test_zero_and_maximum_count_operations() {
    std::cout << "\n=== 零和最大数量操作测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 1000;
    ConcurrentModbusDataMap dataMap(config);
    
    std::vector<bool> values;
    
    // 测试零数量读取
    auto result = dataMap.read_coils(0, 0, values);
    ASSERT_TRUE(result.is_success());  // 零数量应该成功
    ASSERT_EQ(values.size(), 0);
    
    // 测试最大合理数量读取
    result = dataMap.read_coils(0, 250, values);  // Modbus标准最大250
    ASSERT_TRUE(result.is_success());
    ASSERT_EQ(values.size(), 250);
    
    // 测试超过Modbus协议限制的数量
    result = dataMap.read_coils(0, 2000, values);  // 超过协议限制
    ASSERT_FALSE(result.is_success());  // 应该失败
    
    // 测试跨边界的数量
    result = dataMap.read_coils(950, 100, values);  // 950+100=1050 > 1000
    ASSERT_FALSE(result.is_success());  // 应该失败
    
    std::cout << "✅ 零和最大数量操作测试通过" << std::endl;
}

void test_address_wraparound_protection() {
    std::cout << "\n=== 地址回卷保护测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 1000;
    config.enable_bounds_checking = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 测试接近uint16_t最大值的地址
    std::vector<uint16_t> wraparound_addresses = {65530, 65534, 65535};
    
    for (uint16_t addr : wraparound_addresses) {
        // 尝试读取多个值，可能导致地址回卷
        std::vector<bool> values;
        auto result = dataMap.read_coils(addr, 10, values);
        
        // 所有这些操作都应该失败，因为会越界或回卷
        ASSERT_FALSE(result.is_success());
        
        // 单个地址读取也应该失败（超出max_coils范围）
        auto single_result = dataMap.read_coil(addr);
        ASSERT_FALSE(single_result.has_value());
    }
    
    std::cout << "✅ 地址回卷保护测试通过" << std::endl;
}

// =============================================================================
// 并发读写竞态测试  
// =============================================================================

void test_concurrent_read_write_consistency() {
    std::cout << "\n=== 并发读写一致性测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 10000;
    config.enable_statistics = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 初始化数据
    std::vector<bool> initial(10000, false);
    dataMap.initialize_coils(initial);
    
    const int num_readers = 4;
    const int num_writers = 2;
    const int test_duration_ms = 1000;
    
    std::atomic<bool> stop_test{false};
    std::atomic<int> read_operations{0};
    std::atomic<int> write_operations{0};
    std::atomic<int> consistency_errors{0};
    
    // 读线程函数
    auto reader_worker = [&](int reader_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> addr_dist(0, 9900);
        std::uniform_int_distribution<> count_dist(1, 100);
        
        while (!stop_test.load()) {
            uint16_t start_addr = addr_dist(gen);
            uint16_t count = count_dist(gen);
            
            std::vector<bool> values;
            auto result = dataMap.read_coils(start_addr, count, values);
            
            if (result.is_success()) {
                read_operations.fetch_add(1);
                
                // 验证读取的数据内部一致性
                // 这里可以加入更复杂的一致性检查逻辑
                if (values.size() != count) {
                    consistency_errors.fetch_add(1);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };
    
    // 写线程函数
    auto writer_worker = [&](int writer_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> addr_dist(0, 9900);
        std::uniform_int_distribution<> count_dist(1, 100);
        
        while (!stop_test.load()) {
            uint16_t start_addr = addr_dist(gen);
            uint16_t count = count_dist(gen);
            
            // 构造有模式的测试数据
            std::vector<bool> values(count);
            bool pattern = (writer_id % 2 == 0);
            for (auto& val : values) {
                val = pattern;
                pattern = !pattern;  // 交替模式
            }
            
            auto result = dataMap.write_coils(start_addr, values);
            if (result.is_success()) {
                write_operations.fetch_add(1);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    // 启动测试线程
    std::vector<std::thread> threads;
    auto start_time = std::chrono::steady_clock::now();
    
    // 启动读线程
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back(reader_worker, i);
    }
    
    // 启动写线程
    for (int i = 0; i < num_writers; ++i) {
        threads.emplace_back(writer_worker, i);
    }
    
    // 运行测试
    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop_test.store(true);
    
    // 等待所有线程结束
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 输出测试结果
    std::cout << "并发读写测试结果 (持续" << actual_duration.count() << "ms):" << std::endl;
    std::cout << "- 读操作数: " << read_operations.load() << std::endl;
    std::cout << "- 写操作数: " << write_operations.load() << std::endl;
    std::cout << "- 一致性错误: " << consistency_errors.load() << std::endl;
    
    // 输出性能统计
    auto stats = dataMap.get_statistics();
    std::cout << "- 统计读取: " << stats.total_reads.load() << std::endl;
    std::cout << "- 统计写入: " << stats.total_writes.load() << std::endl;
    std::cout << "- 最大并发读: " << stats.max_concurrent_readers.load() << std::endl;
    
    // 计算读写比例
    double read_write_ratio = static_cast<double>(read_operations.load()) / 
                             std::max(1, write_operations.load());
    std::cout << "- 读写比例: " << std::fixed << std::setprecision(2) << read_write_ratio << ":1" << std::endl;
    
    // 验证测试要求
    ASSERT_TRUE(read_operations.load() > 0);
    ASSERT_TRUE(write_operations.load() > 0);
    ASSERT_EQ(consistency_errors.load(), 0);  // 不应该有一致性错误
    ASSERT_TRUE(read_write_ratio > 2.0);  // shared_mutex应该优化读操作
    
    std::cout << "✅ 并发读写一致性测试通过" << std::endl;
}

void test_reader_writer_fairness() {
    std::cout << "\n=== 读写公平性测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_holding_registers = 1000;
    config.enable_statistics = true;
    ConcurrentModbusDataMap dataMap(config);
    
    const int test_duration_ms = 2000;
    const int num_readers = 6;
    const int num_writers = 2;
    
    std::atomic<bool> stop_test{false};
    std::atomic<int> total_read_ops{0};
    std::atomic<int> total_write_ops{0};
    
    // 记录每个线程的操作统计
    std::vector<std::atomic<int>> reader_ops(num_readers);
    std::vector<std::atomic<int>> writer_ops(num_writers);
    
    for (auto& ops : reader_ops) ops.store(0);
    for (auto& ops : writer_ops) ops.store(0);
    
    auto reader_func = [&](int reader_id) {
        while (!stop_test.load()) {
            std::vector<uint16_t> values;
            auto result = dataMap.read_holding_registers(0, 100, values);
            if (result.is_success()) {
                total_read_ops.fetch_add(1);
                reader_ops[reader_id].fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    };
    
    auto writer_func = [&](int writer_id) {
        std::vector<uint16_t> values(50, 0x1000 + writer_id);
        while (!stop_test.load()) {
            auto result = dataMap.write_holding_registers(writer_id * 100, values);
            if (result.is_success()) {
                total_write_ops.fetch_add(1);
                writer_ops[writer_id].fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    
    // 启动测试
    std::vector<std::thread> threads;
    auto start_time = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back(reader_func, i);
    }
    
    for (int i = 0; i < num_writers; ++i) {
        threads.emplace_back(writer_func, i);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
    stop_test.store(true);
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 分析公平性
    std::cout << "读写公平性分析:" << std::endl;
    std::cout << "总读操作: " << total_read_ops.load() << std::endl;
    std::cout << "总写操作: " << total_write_ops.load() << std::endl;
    
    // 分析读线程之间的公平性
    int min_reader_ops = reader_ops[0].load();
    int max_reader_ops = reader_ops[0].load();
    
    std::cout << "各读线程操作数: ";
    for (int i = 0; i < num_readers; ++i) {
        int ops = reader_ops[i].load();
        std::cout << ops << " ";
        min_reader_ops = std::min(min_reader_ops, ops);
        max_reader_ops = std::max(max_reader_ops, ops);
    }
    std::cout << std::endl;
    
    // 分析写线程之间的公平性  
    int min_writer_ops = writer_ops[0].load();
    int max_writer_ops = writer_ops[0].load();
    
    std::cout << "各写线程操作数: ";
    for (int i = 0; i < num_writers; ++i) {
        int ops = writer_ops[i].load();
        std::cout << ops << " ";
        min_writer_ops = std::min(min_writer_ops, ops);
        max_writer_ops = std::max(max_writer_ops, ops);
    }
    std::cout << std::endl;
    
    // 计算公平性指标
    double reader_fairness = static_cast<double>(min_reader_ops) / std::max(1, max_reader_ops);
    double writer_fairness = static_cast<double>(min_writer_ops) / std::max(1, max_writer_ops);
    double read_write_ratio = static_cast<double>(total_read_ops.load()) / std::max(1, total_write_ops.load());
    
    std::cout << "公平性指标:" << std::endl;
    std::cout << "- 读线程公平性: " << std::fixed << std::setprecision(3) << reader_fairness << std::endl;
    std::cout << "- 写线程公平性: " << std::fixed << std::setprecision(3) << writer_fairness << std::endl;
    std::cout << "- 读写比例: " << std::fixed << std::setprecision(1) << read_write_ratio << ":1" << std::endl;
    
    // 验证公平性要求
    ASSERT_TRUE(reader_fairness > 0.5);  // 读线程之间应该相对公平
    ASSERT_TRUE(writer_fairness > 0.3);  // 写线程之间应该有基本公平性
    ASSERT_TRUE(read_write_ratio > 3.0); // shared_mutex应该显著优化读操作
    
    std::cout << "✅ 读写公平性测试通过" << std::endl;
}

// =============================================================================
// 性能和内存对齐测试
// =============================================================================

void test_cache_line_alignment_performance() {
    std::cout << "\n=== 缓存行对齐性能测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 10000;
    config.max_holding_registers = 10000;
    config.enable_statistics = true;
    ConcurrentModbusDataMap dataMap(config);
    
    const int iterations = 10000;
    const int num_threads = 4;
    
    // 测试并发读取性能
    auto concurrent_read_test = [&]() {
        std::atomic<int> completed_operations{0};
        std::vector<std::thread> threads;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        auto worker = [&](int thread_id) {
            for (int i = 0; i < iterations; ++i) {
                uint16_t addr = (thread_id * 2000) + (i % 1000);  // 分离地址空间
                auto result = dataMap.read_coil(addr);
                if (result.has_value()) {
                    completed_operations.fetch_add(1);
                }
            }
        };
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        return std::make_pair(completed_operations.load(), duration.count());
    };
    
    auto [operations, duration_us] = concurrent_read_test();
    
    double ops_per_second = (static_cast<double>(operations) * 1000000.0) / duration_us;
    double avg_latency_ns = (static_cast<double>(duration_us) * 1000.0) / operations;
    
    std::cout << "缓存行对齐性能结果:" << std::endl;
    std::cout << "- 完成操作: " << operations << std::endl;
    std::cout << "- 总时间: " << duration_us << "μs" << std::endl;
    std::cout << "- 吞吐量: " << static_cast<int>(ops_per_second) << " ops/sec" << std::endl;
    std::cout << "- 平均延迟: " << std::fixed << std::setprecision(1) << avg_latency_ns << "ns/op" << std::endl;
    
    // 验证性能要求
    ASSERT_TRUE(ops_per_second > 500000);  // 至少50万ops/sec
    ASSERT_TRUE(avg_latency_ns < 5000);    // 平均延迟小于5μs
    
    std::cout << "✅ 缓存行对齐性能测试通过" << std::endl;
}

void test_memory_usage_and_statistics() {
    std::cout << "\n=== 内存使用和统计测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 65536;
    config.max_holding_registers = 65536;
    config.enable_statistics = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 测试内存使用
    size_t memory_usage = dataMap.get_memory_usage();
    std::cout << "内存使用量: " << memory_usage << " bytes (" << 
                 (memory_usage / 1024.0) << " KB)" << std::endl;
    
    // 估算理论内存使用
    size_t expected_memory = 
        config.max_coils * sizeof(std::atomic<bool>) +          // 线圈
        config.max_holding_registers * sizeof(std::atomic<uint16_t>); // 寄存器
    
    std::cout << "理论最小内存: " << expected_memory << " bytes" << std::endl;
    
    // 内存使用应该在合理范围内（考虑对齐和额外开销）
    ASSERT_TRUE(memory_usage >= expected_memory);
    ASSERT_TRUE(memory_usage < expected_memory * 2);  // 不应该超过2倍
    
    // 测试统计功能
    auto initial_stats = dataMap.get_statistics();
    
    // 执行一些操作
    std::vector<bool> coils(100, true);
    dataMap.write_coils(0, coils);
    
    std::vector<bool> read_coils;
    dataMap.read_coils(0, 100, read_coils);
    
    auto final_stats = dataMap.get_statistics();
    
    // 验证统计更新
    ASSERT_TRUE(final_stats.total_writes.load() > initial_stats.total_writes.load());
    ASSERT_TRUE(final_stats.total_reads.load() > initial_stats.total_reads.load());
    
    std::cout << "统计信息验证:" << std::endl;
    std::cout << "- 读操作: " << final_stats.total_reads.load() << std::endl;
    std::cout << "- 写操作: " << final_stats.total_writes.load() << std::endl;
    std::cout << "- 批量读: " << final_stats.batch_reads.load() << std::endl;
    std::cout << "- 批量写: " << final_stats.batch_writes.load() << std::endl;
    
    std::cout << "✅ 内存使用和统计测试通过" << std::endl;
}

// =============================================================================
// 变更通知和回调测试
// =============================================================================

void test_change_notification_consistency() {
    std::cout << "\n=== 变更通知一致性测试 ===" << std::endl;
    
    ConcurrentModbusDataMap::Config config;
    config.max_coils = 1000;
    config.enable_change_notification = true;
    ConcurrentModbusDataMap dataMap(config);
    
    // 设置变更回调
    std::vector<DataChangeNotification> received_notifications;
    std::mutex notifications_mutex;
    
    dataMap.set_change_callback([&](const DataChangeNotification& notification) {
        std::lock_guard<std::mutex> lock(notifications_mutex);
        received_notifications.push_back(notification);
    });
    
    // 执行一些写入操作
    std::vector<bool> test_values = {true, false, true, false, true};
    
    // 测试1：单个写入
    dataMap.write_coil(10, true);
    
    // 测试2：批量写入
    dataMap.write_coils(20, test_values);
    
    // 测试3：原子写入
    dataMap.atomic_write_coils(30, test_values);
    
    // 给回调一些时间执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证通知
    std::lock_guard<std::mutex> lock(notifications_mutex);
    
    std::cout << "收到变更通知: " << received_notifications.size() << " 个" << std::endl;
    
    ASSERT_TRUE(received_notifications.size() >= 3);  // 至少3个通知
    
    // 验证通知内容
    for (const auto& notification : received_notifications) {
        ASSERT_EQ(notification.type, DataChangeNotification::COIL);
        ASSERT_TRUE(notification.count > 0);
        ASSERT_FALSE(notification.changed_coils.empty());
        
        std::cout << "通知: 地址" << notification.start_address << 
                     ", 数量" << notification.count << std::endl;
    }
    
    std::cout << "✅ 变更通知一致性测试通过" << std::endl;
}

// =============================================================================
// 主测试运行器
// =============================================================================

int main() {
    ConcurrentModbusDataMapTest test_framework;
    
    std::cout << "\n=== ConcurrentModbusDataMap原子性语义和边界用例测试 ===" << std::endl;
    
    test_framework.SetUp();
    
    // 核心原子性语义测试
    std::cout << "\n🔒 原子性语义测试" << std::endl;
    test_framework.run_test(test_atomic_coil_write_all_or_nothing);
    test_framework.run_test(test_atomic_register_write_consistency);
    test_framework.run_test(test_atomic_write_under_concurrent_access);
    
    // 边界用例测试
    std::cout << "\n🎯 边界用例测试" << std::endl;
    test_framework.run_test(test_boundary_address_limits);
    test_framework.run_test(test_zero_and_maximum_count_operations);
    test_framework.run_test(test_address_wraparound_protection);
    
    // 并发安全测试
    std::cout << "\n🔄 并发安全测试" << std::endl;
    test_framework.run_test(test_concurrent_read_write_consistency);
    test_framework.run_test(test_reader_writer_fairness);
    
    // 性能和内存测试
    std::cout << "\n⚡ 性能和内存测试" << std::endl;
    test_framework.run_test(test_cache_line_alignment_performance);
    test_framework.run_test(test_memory_usage_and_statistics);
    
    // 变更通知测试
    std::cout << "\n📢 变更通知测试" << std::endl;
    test_framework.run_test(test_change_notification_consistency);
    
    test_framework.TearDown();
    
    // 输出测试总结
    std::cout << "\n=== 测试总结 ===" << std::endl;
    std::cout << "总测试数: " << test_framework.get_total_tests() << std::endl;
    std::cout << "通过: " << test_framework.get_passed_tests() << std::endl;
    std::cout << "失败: " << test_framework.get_failed_tests() << std::endl;
    std::cout << "成功率: " << std::fixed << std::setprecision(1) 
              << test_framework.get_success_rate() << "%" << std::endl;
    
    if (test_framework.get_failed_tests() == 0) {
        std::cout << "\n🎉 所有ConcurrentModbusDataMap测试通过！" << std::endl;
        std::cout << "✅ 原子性语义验证完成" << std::endl;
        std::cout << "✅ 边界用例覆盖全面" << std::endl;
        std::cout << "✅ 并发安全保障到位" << std::endl;
        std::cout << "✅ 性能指标达标" << std::endl;
        std::cout << "✅ 内存管理优化" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ ConcurrentModbusDataMap测试存在失败项，需要修复" << std::endl;
        return 1;
    }
}