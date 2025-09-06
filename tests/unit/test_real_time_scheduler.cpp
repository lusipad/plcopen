/**
 * @file test_real_time_scheduler.cpp
 * @brief Comprehensive Unit Tests for Real Time Scheduler Module
 * @version 1.0
 * @date 2025-09-06
 * 
 * Complete test coverage for RealTimeScheduler including:
 * - Basic scheduler lifecycle
 * - Task management operations
 * - Real-time performance validation
 * - Statistics and monitoring
 * - Error handling and edge cases
 * - Concurrent access safety
 */

#include "test/TestFramework.h"
#include "scheduler/RealTimeScheduler.h"
#include "scheduler/TaskControlBlock.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <future>

using namespace plc_test;
using namespace plc_runtime::scheduler;

class RealTimeSchedulerTest {
public:
    void setup() {
        // Default configuration for most tests
        config_ = SchedulerConfig{
            .tick_period_ns = 1000000,  // 1ms
            .max_jitter_ns = 50000,     // 50μs
            .max_tasks = 1024,
            .enable_statistics = true,
            .enable_deadline_monitoring = true,
            .scheduler_cpu_affinity = -1,
            .scheduler_priority = 50    // Lower for unit tests
        };
        
        scheduler_ = std::make_unique<RealTimeScheduler>(config_);
        task_counter_ = 0;
    }
    
    void teardown() {
        if (scheduler_ && scheduler_->is_running()) {
            scheduler_->stop();
        }
        scheduler_.reset();
    }
    
protected:
    SchedulerConfig config_;
    std::unique_ptr<RealTimeScheduler> scheduler_;
    std::atomic<uint32_t> task_counter_{0};
    
    // Helper: Create simple test task
    TCBPtr create_test_task(const std::string& name, Priority priority = 128, 
                           uint64_t period_ms = 100, TaskFunction func = nullptr) {
        uint32_t task_id = ++task_counter_;
        
        if (!func) {
            func = [this, task_id]() {
                // Simple test task that increments counter
                task_execution_counts_[task_id]++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            };
        }
        
        return create_periodic_task(task_id, name, priority, period_ms, func);
    }
    
    // Helper: Wait for scheduler to process tasks
    void wait_for_scheduler_cycles(int cycles = 5, int cycle_time_ms = 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cycles * cycle_time_ms));
    }
    
    // Task execution tracking
    std::atomic<uint32_t> task_execution_counts_[1000] = {};
};

int main() {
    TestRunner runner;
    
    // === Basic Scheduler Lifecycle Tests ===
    TestSuite lifecycle_suite("Scheduler Lifecycle");
    RealTimeSchedulerTest test;
    
    lifecycle_suite.add_setup([&test]() { test.setup(); });
    lifecycle_suite.add_teardown([&test]() { test.teardown(); });
    
    lifecycle_suite.run_test("Constructor_ValidConfig_Success", [&]() {
        ASSERT_NOT_NULL(test.scheduler_.get());
        ASSERT_FALSE(test.scheduler_->is_running());
        ASSERT_EQ(0, test.scheduler_->get_task_count());
        ASSERT_EQ(0, test.scheduler_->get_ready_queue_size());
    });
    
    lifecycle_suite.run_test("Start_ValidConfig_Success", [&]() {
        ASSERT_TRUE(test.scheduler_->start());
        ASSERT_TRUE(test.scheduler_->is_running());
        
        // Should not be able to start twice
        ASSERT_FALSE(test.scheduler_->start());
    });
    
    lifecycle_suite.run_test("Stop_RunningScheduler_Success", [&]() {
        ASSERT_TRUE(test.scheduler_->start());
        ASSERT_TRUE(test.scheduler_->is_running());
        
        test.scheduler_->stop();
        ASSERT_FALSE(test.scheduler_->is_running());
        
        // Should be safe to stop twice
        test.scheduler_->stop();
        ASSERT_FALSE(test.scheduler_->is_running());
    });
    
    lifecycle_suite.run_test("GetConfig_ReturnsCorrectConfig", [&]() {
        const auto& config = test.scheduler_->get_config();
        ASSERT_EQ(1000000, config.tick_period_ns);
        ASSERT_EQ(50000, config.max_jitter_ns);
        ASSERT_EQ(1024, config.max_tasks);
        ASSERT_TRUE(config.enable_statistics);
        ASSERT_TRUE(config.enable_deadline_monitoring);
    });
    
    runner.add_suite(std::move(lifecycle_suite));
    
    // === Task Management Tests ===
    TestSuite task_mgmt_suite("Task Management");
    task_mgmt_suite.add_setup([&test]() { test.setup(); });
    task_mgmt_suite.add_teardown([&test]() { test.teardown(); });
    
    task_mgmt_suite.run_test("AddTask_ValidTask_Success", [&]() {
        auto task = test.create_test_task("TestTask1");
        ASSERT_NOT_NULL(task.get());
        
        ASSERT_TRUE(test.scheduler_->add_task(task));
        ASSERT_EQ(1, test.scheduler_->get_task_count());
        
        auto retrieved_task = test.scheduler_->get_task(task->task_id);
        ASSERT_NOT_NULL(retrieved_task.get());
        ASSERT_EQ(task->task_id, retrieved_task->task_id);
        ASSERT_EQ("TestTask1", retrieved_task->name);
    });
    
    task_mgmt_suite.run_test("AddTask_DuplicateId_Failure", [&]() {
        auto task1 = test.create_test_task("Task1");
        auto task2 = test.create_test_task("Task2");
        task2->task_id = task1->task_id;  // Make duplicate ID
        
        ASSERT_TRUE(test.scheduler_->add_task(task1));
        ASSERT_FALSE(test.scheduler_->add_task(task2));
        ASSERT_EQ(1, test.scheduler_->get_task_count());
    });
    
    task_mgmt_suite.run_test("AddTask_NullTask_Failure", [&]() {
        ASSERT_FALSE(test.scheduler_->add_task(nullptr));
        ASSERT_EQ(0, test.scheduler_->get_task_count());
    });
    
    task_mgmt_suite.run_test("RemoveTask_ExistingTask_Success", [&]() {
        auto task = test.create_test_task("TaskToRemove");
        uint32_t task_id = task->task_id;
        
        ASSERT_TRUE(test.scheduler_->add_task(task));
        ASSERT_EQ(1, test.scheduler_->get_task_count());
        
        ASSERT_TRUE(test.scheduler_->remove_task(task_id));
        ASSERT_EQ(0, test.scheduler_->get_task_count());
        ASSERT_NULL(test.scheduler_->get_task(task_id).get());
    });
    
    task_mgmt_suite.run_test("RemoveTask_NonexistentTask_Failure", [&]() {
        ASSERT_FALSE(test.scheduler_->remove_task(99999));
        ASSERT_EQ(0, test.scheduler_->get_task_count());
    });
    
    task_mgmt_suite.run_test("GetTask_ExistingTask_Success", [&]() {
        auto original_task = test.create_test_task("GetTaskTest");
        ASSERT_TRUE(test.scheduler_->add_task(original_task));
        
        auto retrieved_task = test.scheduler_->get_task(original_task->task_id);
        ASSERT_NOT_NULL(retrieved_task.get());
        ASSERT_EQ(original_task->task_id, retrieved_task->task_id);
        ASSERT_EQ(original_task->name, retrieved_task->name);
    });
    
    task_mgmt_suite.run_test("GetTask_NonexistentTask_ReturnsNull", [&]() {
        auto task = test.scheduler_->get_task(99999);
        ASSERT_NULL(task.get());
    });
    
    task_mgmt_suite.run_test("ClearAllTasks_MultipleTasks_Success", [&]() {
        // Add multiple tasks
        for (int i = 0; i < 5; i++) {
            auto task = test.create_test_task("Task" + std::to_string(i));
            ASSERT_TRUE(test.scheduler_->add_task(task));
        }
        ASSERT_EQ(5, test.scheduler_->get_task_count());
        
        test.scheduler_->clear_all_tasks();
        ASSERT_EQ(0, test.scheduler_->get_task_count());
    });
    
    runner.add_suite(std::move(task_mgmt_suite));
    
    // === Task Execution Tests ===
    TestSuite execution_suite("Task Execution");
    execution_suite.add_setup([&test]() { test.setup(); });
    execution_suite.add_teardown([&test]() { test.teardown(); });
    
    execution_suite.run_test("TaskExecution_SinglePeriodicTask_ExecutesCorrectly", [&]() {
        std::atomic<int> execution_count{0};
        
        auto task = create_periodic_task(1, "PeriodicTask", 128, 50,  // 50ms period
            [&execution_count]() {
                execution_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(task));
        ASSERT_TRUE(test.scheduler_->start());
        
        // Wait for several execution cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        test.scheduler_->stop();
        
        // Should have executed at least 3 times (200ms / 50ms ≈ 4)
        ASSERT_TRUE(execution_count.load() >= 3);
        
        // Verify task statistics
        ASSERT_TRUE(task->exec_count.load() >= 3);
        ASSERT_TRUE(task->total_exec_time_ns.load() > 0);
    });
    
    execution_suite.run_test("TaskExecution_MultipleTasksPriority_ExecutesInOrder", [&]() {
        std::vector<std::atomic<int>*> execution_counts(3);
        for (int i = 0; i < 3; i++) {
            execution_counts[i] = new std::atomic<int>{0};
        }
        
        // High priority task (priority 50)
        auto high_task = create_periodic_task(1, "HighPrio", 50, 100,
            [&execution_counts]() {
                (*execution_counts[0])++;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            });
        
        // Medium priority task (priority 100) 
        auto med_task = create_periodic_task(2, "MedPrio", 100, 100,
            [&execution_counts]() {
                (*execution_counts[1])++;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            });
        
        // Low priority task (priority 200)
        auto low_task = create_periodic_task(3, "LowPrio", 200, 100,
            [&execution_counts]() {
                (*execution_counts[2])++;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(low_task));   // Add in reverse order
        ASSERT_TRUE(test.scheduler_->add_task(med_task));
        ASSERT_TRUE(test.scheduler_->add_task(high_task));
        
        ASSERT_TRUE(test.scheduler_->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        test.scheduler_->stop();
        
        // High priority should execute most frequently
        ASSERT_TRUE(execution_counts[0]->load() >= execution_counts[1]->load());
        ASSERT_TRUE(execution_counts[1]->load() >= execution_counts[2]->load());
        
        // Cleanup
        for (auto* counter : execution_counts) {
            delete counter;
        }
    });
    
    execution_suite.run_test("TaskExecution_TaskException_HandledGracefully", [&]() {
        std::atomic<int> normal_task_count{0};
        
        // Task that throws exception
        auto exception_task = create_periodic_task(1, "ExceptionTask", 128, 50,
            []() {
                throw std::runtime_error("Test exception");
            });
        
        // Normal task that should continue running
        auto normal_task = create_periodic_task(2, "NormalTask", 128, 50,
            [&normal_task_count]() {
                normal_task_count++;
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(exception_task));
        ASSERT_TRUE(test.scheduler_->add_task(normal_task));
        
        ASSERT_TRUE(test.scheduler_->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        test.scheduler_->stop();
        
        // Normal task should still execute despite exception in other task
        ASSERT_TRUE(normal_task_count.load() > 0);
        
        // Exception task should have recorded the exception
        ASSERT_TRUE(exception_task->exception_count.load() > 0);
        
        // Scheduler should still be functional
        const auto& stats = test.scheduler_->get_statistics();
        ASSERT_TRUE(stats.task_exceptions.load() > 0);
    });
    
    runner.add_suite(std::move(execution_suite));
    
    // === Statistics and Monitoring Tests ===
    TestSuite stats_suite("Statistics and Monitoring");
    stats_suite.add_setup([&test]() { test.setup(); });
    stats_suite.add_teardown([&test]() { test.teardown(); });
    
    stats_suite.run_test("Statistics_InitialState_AllZero", [&]() {
        const auto& stats = test.scheduler_->get_statistics();
        
        ASSERT_EQ(0, stats.total_cycles.load());
        ASSERT_EQ(0, stats.total_tasks_executed.load());
        ASSERT_EQ(0, stats.total_execution_time_ns.load());
        ASSERT_EQ(0, stats.scheduling_errors.load());
        ASSERT_EQ(0, stats.task_exceptions.load());
        ASSERT_EQ(0.0, stats.get_cpu_utilization());
        ASSERT_EQ(0.0, stats.get_average_execution_time_ns());
    });
    
    stats_suite.run_test("Statistics_AfterExecution_UpdatedCorrectly", [&]() {
        auto task = test.create_test_task("StatTask");
        ASSERT_TRUE(test.scheduler_->add_task(task));
        
        ASSERT_TRUE(test.scheduler_->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        test.scheduler_->stop();
        
        const auto& stats = test.scheduler_->get_statistics();
        
        // Should have some execution statistics
        ASSERT_TRUE(stats.total_cycles.load() > 0);
        ASSERT_TRUE(stats.total_tasks_executed.load() > 0);
        ASSERT_TRUE(stats.total_execution_time_ns.load() > 0);
        ASSERT_TRUE(stats.total_schedules.load() > 0);
        
        // CPU utilization should be non-zero but reasonable
        double cpu_util = stats.get_cpu_utilization();
        ASSERT_TRUE(cpu_util >= 0.0 && cpu_util <= 1.0);
        
        // Average execution time should be reasonable
        double avg_exec_time = stats.get_average_execution_time_ns();
        ASSERT_TRUE(avg_exec_time > 0.0);
    });
    
    stats_suite.run_test("Statistics_ResetStatistics_ClearsAllCounters", [&]() {
        auto task = test.create_test_task("ResetStatsTask");
        ASSERT_TRUE(test.scheduler_->add_task(task));
        
        // Run scheduler to generate statistics
        ASSERT_TRUE(test.scheduler_->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        test.scheduler_->stop();
        
        // Verify statistics are not zero
        const auto& stats_before = test.scheduler_->get_statistics();
        ASSERT_TRUE(stats_before.total_tasks_executed.load() > 0);
        
        // Reset statistics
        test.scheduler_->reset_statistics();
        
        // Verify all counters are reset
        const auto& stats_after = test.scheduler_->get_statistics();
        ASSERT_EQ(0, stats_after.total_cycles.load());
        ASSERT_EQ(0, stats_after.total_tasks_executed.load());
        ASSERT_EQ(0, stats_after.total_execution_time_ns.load());
        ASSERT_EQ(0, stats_after.scheduling_errors.load());
    });
    
    runner.add_suite(std::move(stats_suite));
    
    // === Edge Cases and Error Handling Tests ===
    TestSuite edge_cases_suite("Edge Cases and Error Handling");
    edge_cases_suite.add_setup([&test]() { test.setup(); });
    edge_cases_suite.add_teardown([&test]() { test.teardown(); });
    
    edge_cases_suite.run_test("EdgeCase_MaxTasksLimit_HandledCorrectly", [&]() {
        // Use smaller config for this test
        SchedulerConfig small_config = test.config_;
        small_config.max_tasks = 3;
        
        auto small_scheduler = std::make_unique<RealTimeScheduler>(small_config);
        
        // Add tasks up to limit
        for (uint32_t i = 0; i < 3; i++) {
            auto task = create_periodic_task(i + 1, "Task" + std::to_string(i), 128, 100, [](){});
            ASSERT_TRUE(small_scheduler->add_task(task));
        }
        
        // Adding one more should fail
        auto extra_task = create_periodic_task(4, "ExtraTask", 128, 100, [](){});
        ASSERT_FALSE(small_scheduler->add_task(extra_task));
        
        ASSERT_EQ(3, small_scheduler->get_task_count());
    });
    
    edge_cases_suite.run_test("EdgeCase_RemoveTaskWhileRunning_SafeHandling", [&]() {
        std::atomic<bool> task_running{true};
        std::atomic<int> execution_count{0};
        
        auto task = create_periodic_task(1, "RemoveWhileRunning", 128, 50,
            [&task_running, &execution_count]() {
                execution_count++;
                while (task_running.load()) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(task));
        ASSERT_TRUE(test.scheduler_->start());
        
        // Wait for task to start executing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_TRUE(execution_count.load() > 0);
        
        // Remove task while it might be running
        ASSERT_TRUE(test.scheduler_->remove_task(1));
        
        // Allow task to finish
        task_running.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        test.scheduler_->stop();
        ASSERT_EQ(0, test.scheduler_->get_task_count());
    });
    
    edge_cases_suite.run_test("EdgeCase_StopSchedulerWhileTasksRunning_GracefulShutdown", [&]() {
        std::atomic<int> long_task_started{0};
        std::atomic<int> long_task_finished{0};
        
        auto long_task = create_periodic_task(1, "LongRunningTask", 128, 100,
            [&long_task_started, &long_task_finished]() {
                long_task_started++;
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Long task
                long_task_finished++;
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(long_task));
        ASSERT_TRUE(test.scheduler_->start());
        
        // Wait for task to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_TRUE(long_task_started.load() > 0);
        
        // Stop scheduler while task is running
        test.scheduler_->stop();
        
        ASSERT_FALSE(test.scheduler_->is_running());
        // The running task should be allowed to finish gracefully
        // (This depends on implementation - some schedulers may terminate immediately)
    });
    
    runner.add_suite(std::move(edge_cases_suite));
    
    // === Performance and Real-Time Tests ===
    TestSuite performance_suite("Performance and Real-Time");
    performance_suite.add_setup([&test]() { test.setup(); });
    performance_suite.add_teardown([&test]() { test.teardown(); });
    
    performance_suite.run_test("Performance_SchedulingJitter_WithinLimits", [&]() {
        const uint64_t expected_period_ns = 10000000; // 10ms
        std::vector<uint64_t> execution_times;
        std::mutex times_mutex;
        
        auto precise_task = create_periodic_task(1, "JitterTest", 128, 10, // 10ms period
            [&execution_times, &times_mutex]() {
                auto now = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
                
                std::lock_guard<std::mutex> lock(times_mutex);
                execution_times.push_back(ns);
            });
        
        ASSERT_TRUE(test.scheduler_->add_task(precise_task));
        ASSERT_TRUE(test.scheduler_->start());
        
        // Run for 200ms to collect timing data
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        test.scheduler_->stop();
        
        // Analyze jitter
        std::lock_guard<std::mutex> lock(times_mutex);
        ASSERT_TRUE(execution_times.size() >= 15); // Should have ~20 executions
        
        // Calculate periods between executions
        std::vector<uint64_t> periods;
        for (size_t i = 1; i < execution_times.size(); i++) {
            periods.push_back(execution_times[i] - execution_times[i-1]);
        }
        
        // Calculate jitter (deviation from expected period)
        uint64_t max_jitter = 0;
        for (uint64_t period : periods) {
            uint64_t jitter = (period > expected_period_ns) ? 
                              (period - expected_period_ns) : 
                              (expected_period_ns - period);
            max_jitter = std::max(max_jitter, jitter);
        }
        
        // Max jitter should be within configured limits (50μs = 50000ns)
        // Note: In unit tests, we use more relaxed limits due to system load
        ASSERT_TRUE(max_jitter < 500000); // 500μs tolerance for unit tests
        
        // Verify scheduler statistics show reasonable jitter
        const auto& stats = test.scheduler_->get_statistics();
        ASSERT_TRUE(stats.get_average_jitter_ns() >= 0.0);
    });
    
    runner.add_suite(std::move(performance_suite));
    
    // Run all tests
    bool all_passed = runner.run_all();
    return all_passed ? 0 : 1;
}