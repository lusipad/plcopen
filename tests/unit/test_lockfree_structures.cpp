/**
 * @file test_lockfree_structures.cpp
 * @brief Comprehensive Unit Tests for Lockfree Data Structures
 * @version 1.0
 * @date 2025-09-06
 * 
 * Complete test coverage for lockfree data structures including:
 * - SPSC Queue functionality and correctness
 * - Atomic utilities and memory barriers
 * - Performance counters and spin locks
 * - Concurrent safety and race condition testing
 * - Memory ordering and cache alignment verification
 * - Performance benchmarks under load
 */

#include "test/TestFramework.h"
#include "lockfree/spsc_queue.h"
#include "lockfree/atomic_utils.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>
#include <future>
#include <numeric>
#include <algorithm>

using namespace plc_test;
using namespace plc_runtime::lockfree;

class LockfreeStructuresTest {
public:
    void setup() {
        test_iterations_ = 0;
        error_count_.store(0);
        success_count_.store(0);
    }
    
    void teardown() {
        // Cleanup any test state
    }

protected:
    std::atomic<int> test_iterations_{0};
    std::atomic<int> error_count_{0};
    std::atomic<int> success_count_{0};
    
    // Helper: Generate test data
    std::vector<int> generate_test_data(int count, int offset = 0) {
        std::vector<int> data;
        data.reserve(count);
        for (int i = 0; i < count; i++) {
            data.push_back(offset + i);
        }
        return data;
    }
    
    // Helper: Verify data sequence
    bool verify_sequence(const std::vector<int>& data, int start_value = 0) {
        for (size_t i = 0; i < data.size(); i++) {
            if (data[i] != start_value + static_cast<int>(i)) {
                return false;
            }
        }
        return true;
    }
};

int main() {
    TestRunner runner;
    
    // === SPSC Queue Basic Functionality Tests ===
    TestSuite spsc_basic_suite("SPSC Queue Basic Functionality");
    LockfreeStructuresTest test;
    
    spsc_basic_suite.add_setup([&test]() { test.setup(); });
    spsc_basic_suite.add_teardown([&test]() { test.teardown(); });
    
    spsc_basic_suite.run_test("SPSCQueue_Constructor_ValidCapacity", [&]() {
        SPSCQueue<int> queue(16);
        
        ASSERT_EQ(15, queue.capacity());  // Actual capacity is capacity-1
        ASSERT_TRUE(queue.empty());
        ASSERT_FALSE(queue.full());
        ASSERT_EQ(0, queue.size());
    });
    
    spsc_basic_suite.run_test("SPSCQueue_Constructor_PowerOfTwoAdjustment", [&]() {
        // Non-power-of-two capacity should be adjusted upward
        SPSCQueue<int> queue1(10);  // Should become 16
        ASSERT_EQ(15, queue1.capacity());
        
        SPSCQueue<int> queue2(17);  // Should become 32
        ASSERT_EQ(31, queue2.capacity());
        
        SPSCQueue<int> queue3(1);   // Should become 2
        ASSERT_EQ(1, queue3.capacity());
    });
    
    spsc_basic_suite.run_test("SPSCQueue_EnqueueDequeue_SingleElement", [&]() {
        SPSCQueue<int> queue(4);
        
        ASSERT_TRUE(queue.enqueue(42));
        ASSERT_FALSE(queue.empty());
        ASSERT_FALSE(queue.full());
        ASSERT_EQ(1, queue.size());
        
        int value;
        ASSERT_TRUE(queue.dequeue(value));
        ASSERT_EQ(42, value);
        ASSERT_TRUE(queue.empty());
        ASSERT_EQ(0, queue.size());
    });
    
    spsc_basic_suite.run_test("SPSCQueue_EnqueueDequeue_MultipleElements", [&]() {
        SPSCQueue<int> queue(8);
        auto test_data = test.generate_test_data(5);
        
        // Enqueue all elements
        for (int value : test_data) {
            ASSERT_TRUE(queue.enqueue(value));
        }
        ASSERT_EQ(5, queue.size());
        ASSERT_FALSE(queue.empty());
        ASSERT_FALSE(queue.full());
        
        // Dequeue all elements and verify order
        std::vector<int> dequeued_data;
        int value;
        while (queue.dequeue(value)) {
            dequeued_data.push_back(value);
        }
        
        ASSERT_EQ(test_data.size(), dequeued_data.size());
        ASSERT_TRUE(test.verify_sequence(dequeued_data, 0));
        ASSERT_TRUE(queue.empty());
    });
    
    spsc_basic_suite.run_test("SPSCQueue_FillToCapacity_HandlesFullCondition", [&]() {
        SPSCQueue<int> queue(4);  // Capacity = 3 (4-1)
        
        // Fill to capacity
        ASSERT_TRUE(queue.enqueue(1));
        ASSERT_TRUE(queue.enqueue(2));
        ASSERT_TRUE(queue.enqueue(3));
        ASSERT_TRUE(queue.full());
        ASSERT_EQ(3, queue.size());
        
        // Next enqueue should fail
        ASSERT_FALSE(queue.enqueue(4));
        ASSERT_TRUE(queue.full());
        
        // Dequeue one element
        int value;
        ASSERT_TRUE(queue.dequeue(value));
        ASSERT_EQ(1, value);
        ASSERT_FALSE(queue.full());
        
        // Should be able to enqueue again
        ASSERT_TRUE(queue.enqueue(4));
    });
    
    spsc_basic_suite.run_test("SPSCQueue_DequeueFromEmpty_ReturnsFalse", [&]() {
        SPSCQueue<int> queue(4);
        
        int value;
        ASSERT_FALSE(queue.dequeue(value));
        ASSERT_TRUE(queue.empty());
        
        // Add and remove one element, then try to dequeue again
        ASSERT_TRUE(queue.enqueue(42));
        ASSERT_TRUE(queue.dequeue(value));
        ASSERT_FALSE(queue.dequeue(value));
    });
    
    spsc_basic_suite.run_test("SPSCQueue_Clear_ResetsToEmpty", [&]() {
        SPSCQueue<int> queue(8);
        
        // Add some elements
        for (int i = 0; i < 5; i++) {
            ASSERT_TRUE(queue.enqueue(i));
        }
        ASSERT_FALSE(queue.empty());
        ASSERT_EQ(5, queue.size());
        
        // Clear and verify
        queue.clear();
        ASSERT_TRUE(queue.empty());
        ASSERT_EQ(0, queue.size());
        ASSERT_FALSE(queue.full());
    });
    
    runner.add_suite(std::move(spsc_basic_suite));
    
    // === SPSC Queue Pointer Specialization Tests ===
    TestSuite spsc_ptr_suite("SPSC Pointer Queue Specialization");
    spsc_ptr_suite.add_setup([&test]() { test.setup(); });
    spsc_ptr_suite.add_teardown([&test]() { test.teardown(); });
    
    spsc_ptr_suite.run_test("SPSCPtrQueue_EnqueueDequeue_UniquePtr", [&]() {
        SPSCPtrQueue<int> ptrQueue(4);
        
        // Enqueue unique_ptr
        auto ptr = std::make_unique<int>(42);
        ASSERT_TRUE(ptrQueue.enqueue(std::move(ptr)));
        ASSERT_NULL(ptr.get());  // Ownership transferred
        ASSERT_FALSE(ptrQueue.empty());
        
        // Dequeue unique_ptr
        auto dequeued_ptr = ptrQueue.dequeue();
        ASSERT_NOT_NULL(dequeued_ptr.get());
        ASSERT_EQ(42, *dequeued_ptr);
        ASSERT_TRUE(ptrQueue.empty());
    });
    
    spsc_ptr_suite.run_test("SPSCPtrQueue_DequeueFromEmpty_ReturnsNull", [&]() {
        SPSCPtrQueue<int> ptrQueue(4);
        
        auto ptr = ptrQueue.dequeue();
        ASSERT_NULL(ptr.get());
    });
    
    spsc_ptr_suite.run_test("SPSCPtrQueue_MultiplePointers_CorrectOwnership", [&]() {
        SPSCPtrQueue<int> ptrQueue(8);
        
        std::vector<int> test_values = {1, 2, 3, 4, 5};
        
        // Enqueue multiple pointers
        for (int value : test_values) {
            auto ptr = std::make_unique<int>(value);
            ASSERT_TRUE(ptrQueue.enqueue(std::move(ptr)));
        }
        
        // Dequeue and verify
        std::vector<int> dequeued_values;
        for (size_t i = 0; i < test_values.size(); i++) {
            auto ptr = ptrQueue.dequeue();
            ASSERT_NOT_NULL(ptr.get());
            dequeued_values.push_back(*ptr);
        }
        
        ASSERT_EQ(test_values, dequeued_values);
        ASSERT_TRUE(ptrQueue.empty());
    });
    
    runner.add_suite(std::move(spsc_ptr_suite));
    
    // === Atomic Utils Tests ===
    TestSuite atomic_utils_suite("Atomic Utils");
    atomic_utils_suite.add_setup([&test]() { test.setup(); });
    atomic_utils_suite.add_teardown([&test]() { test.teardown(); });
    
    atomic_utils_suite.run_test("CacheAlignedAtomic_BasicOperations", [&]() {
        CacheAlignedAtomic<int> atomic_int(42);
        
        ASSERT_EQ(42, atomic_int.load());
        
        atomic_int.store(100);
        ASSERT_EQ(100, atomic_int.load());
        
        int old_value = atomic_int.exchange(200);
        ASSERT_EQ(100, old_value);
        ASSERT_EQ(200, atomic_int.load());
    });
    
    atomic_utils_suite.run_test("CacheAlignedAtomic_CompareExchange", [&]() {
        CacheAlignedAtomic<int> atomic_int(50);
        
        int expected = 50;
        bool result = atomic_int.compare_exchange_strong(expected, 75);
        ASSERT_TRUE(result);
        ASSERT_EQ(75, atomic_int.load());
        ASSERT_EQ(50, expected);  // Should remain unchanged on success
        
        expected = 100;  // Wrong expected value
        result = atomic_int.compare_exchange_strong(expected, 125);
        ASSERT_FALSE(result);
        ASSERT_EQ(75, atomic_int.load());  // Should remain unchanged
        ASSERT_EQ(75, expected);  // Should be updated to actual value
    });
    
    atomic_utils_suite.run_test("CacheAlignedAtomic_FetchOperations", [&]() {
        CacheAlignedAtomic<int> counter(10);
        
        int old_value = counter.fetch_add(5);
        ASSERT_EQ(10, old_value);
        ASSERT_EQ(15, counter.load());
        
        old_value = counter.fetch_sub(3);
        ASSERT_EQ(15, old_value);
        ASSERT_EQ(12, counter.load());
    });
    
    atomic_utils_suite.run_test("AtomicUtils_PointerOperations", [&]() {
        int value1 = 42;
        int value2 = 84;
        std::atomic<int*> atomic_ptr(&value1);
        
        int* loaded = AtomicUtils::loadPtr(atomic_ptr);
        ASSERT_EQ(&value1, loaded);
        ASSERT_EQ(42, *loaded);
        
        AtomicUtils::storePtr(atomic_ptr, &value2);
        loaded = AtomicUtils::loadPtr(atomic_ptr);
        ASSERT_EQ(&value2, loaded);
        ASSERT_EQ(84, *loaded);
        
        // Compare exchange
        int* expected = &value2;
        bool success = AtomicUtils::compareExchangePtr(atomic_ptr, expected, &value1);
        ASSERT_TRUE(success);
        ASSERT_EQ(&value1, AtomicUtils::loadPtr(atomic_ptr));
    });
    
    atomic_utils_suite.run_test("AtomicUtils_CounterOperations", [&]() {
        std::atomic<int> counter(0);
        
        int old_value = AtomicUtils::incrementCounter(counter);
        ASSERT_EQ(0, old_value);
        ASSERT_EQ(1, counter.load());
        
        old_value = AtomicUtils::incrementCounter(counter);
        ASSERT_EQ(1, old_value);
        ASSERT_EQ(2, counter.load());
        
        old_value = AtomicUtils::decrementCounter(counter);
        ASSERT_EQ(2, old_value);
        ASSERT_EQ(1, counter.load());
    });
    
    atomic_utils_suite.run_test("AtomicUtils_BitOperations", [&]() {
        std::atomic<uint32_t> flags(0);
        
        // Set bits
        uint32_t old_value = AtomicUtils::setBit(flags, 2);
        ASSERT_EQ(0, old_value);
        ASSERT_EQ(4, flags.load());  // Bit 2 = 0x04
        
        AtomicUtils::setBit(flags, 5);
        ASSERT_EQ(36, flags.load());  // Bits 2,5 = 0x04 | 0x20 = 0x24 = 36
        
        // Test bits
        ASSERT_TRUE(AtomicUtils::testBit(flags, 2));
        ASSERT_TRUE(AtomicUtils::testBit(flags, 5));
        ASSERT_FALSE(AtomicUtils::testBit(flags, 1));
        ASSERT_FALSE(AtomicUtils::testBit(flags, 7));
        
        // Clear bit
        old_value = AtomicUtils::clearBit(flags, 2);
        ASSERT_EQ(36, old_value);
        ASSERT_EQ(32, flags.load());  // Only bit 5 = 0x20 = 32
        ASSERT_FALSE(AtomicUtils::testBit(flags, 2));
        ASSERT_TRUE(AtomicUtils::testBit(flags, 5));
    });
    
    runner.add_suite(std::move(atomic_utils_suite));
    
    // === Performance Counter Tests ===
    TestSuite perf_counter_suite("Performance Counter");
    perf_counter_suite.add_setup([&test]() { test.setup(); });
    perf_counter_suite.add_teardown([&test]() { test.teardown(); });
    
    perf_counter_suite.run_test("PerformanceCounter_InitialState", [&]() {
        PerformanceCounter counter;
        
        ASSERT_EQ(0, counter.getOperationCount());
        ASSERT_EQ(0.0, counter.getAverageLatency());
        ASSERT_EQ(0, counter.getMaxLatency());
    });
    
    perf_counter_suite.run_test("PerformanceCounter_RecordOperations", [&]() {
        PerformanceCounter counter;
        
        counter.recordOperation(100);  // 100ns
        ASSERT_EQ(1, counter.getOperationCount());
        ASSERT_EQ(100.0, counter.getAverageLatency());
        ASSERT_EQ(100, counter.getMaxLatency());
        
        counter.recordOperation(200);  // 200ns
        ASSERT_EQ(2, counter.getOperationCount());
        ASSERT_EQ(150.0, counter.getAverageLatency());  // (100+200)/2
        ASSERT_EQ(200, counter.getMaxLatency());
        
        counter.recordOperation(50);   // 50ns
        ASSERT_EQ(3, counter.getOperationCount());
        ASSERT_EQ(350.0/3.0, counter.getAverageLatency());  // (100+200+50)/3
        ASSERT_EQ(200, counter.getMaxLatency());  // Still 200
    });
    
    perf_counter_suite.run_test("PerformanceCounter_Reset", [&]() {
        PerformanceCounter counter;
        
        counter.recordOperation(100);
        counter.recordOperation(200);
        ASSERT_EQ(2, counter.getOperationCount());
        
        counter.reset();
        ASSERT_EQ(0, counter.getOperationCount());
        ASSERT_EQ(0.0, counter.getAverageLatency());
        ASSERT_EQ(0, counter.getMaxLatency());
    });
    
    runner.add_suite(std::move(perf_counter_suite));
    
    // === SpinLock Tests ===
    TestSuite spinlock_suite("SpinLock");
    spinlock_suite.add_setup([&test]() { test.setup(); });
    spinlock_suite.add_teardown([&test]() { test.teardown(); });
    
    spinlock_suite.run_test("SpinLock_BasicLockUnlock", [&]() {
        SpinLock lock;
        
        ASSERT_TRUE(lock.tryLock());  // Should succeed initially
        ASSERT_FALSE(lock.tryLock()); // Should fail when already locked
        
        lock.unlock();
        ASSERT_TRUE(lock.tryLock());  // Should succeed after unlock
        lock.unlock();
    });
    
    spinlock_suite.run_test("SpinLock_RAIIGuard", [&]() {
        SpinLock lock;
        
        {
            SpinLockGuard guard(lock);
            // Lock should be held
            ASSERT_FALSE(lock.tryLock());
        }
        // Lock should be released automatically
        ASSERT_TRUE(lock.tryLock());
        lock.unlock();
    });
    
    runner.add_suite(std::move(spinlock_suite));
    
    // === Concurrent Access Tests ===
    TestSuite concurrent_suite("Concurrent Access");
    concurrent_suite.add_setup([&test]() { test.setup(); });
    concurrent_suite.add_teardown([&test]() { test.teardown(); });
    
    concurrent_suite.run_test("SPSCQueue_SingleProducerSingleConsumer_Correctness", [&]() {
        SPSCQueue<int> queue(1024);
        const int num_items = 10000;
        std::atomic<bool> producer_done{false};
        std::atomic<int> items_consumed{0};
        
        // Producer thread
        std::thread producer([&queue, num_items, &producer_done]() {
            for (int i = 0; i < num_items; i++) {
                while (!queue.enqueue(i)) {
                    CPURelax::pause();  // Spin until space available
                }
            }
            producer_done.store(true);
        });
        
        // Consumer thread
        std::thread consumer([&queue, &producer_done, &items_consumed]() {
            int value;
            while (!producer_done.load() || !queue.empty()) {
                if (queue.dequeue(value)) {
                    items_consumed.fetch_add(1);
                } else {
                    CPURelax::pause();  // Spin until data available
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        ASSERT_EQ(num_items, items_consumed.load());
        ASSERT_TRUE(queue.empty());
    });
    
    concurrent_suite.run_test("SPSCQueue_HighThroughput_Performance", [&]() {
        SPSCQueue<int> queue(4096);  // Larger queue for performance test
        const int num_items = 100000;
        std::vector<int> consumed_data;
        consumed_data.reserve(num_items);
        
        std::atomic<bool> producer_done{false};
        auto start_time = std::chrono::steady_clock::now();
        
        // Producer thread
        std::thread producer([&queue, num_items, &producer_done]() {
            for (int i = 0; i < num_items; i++) {
                while (!queue.enqueue(i)) {
                    CPURelax::pause();
                }
            }
            producer_done.store(true);
        });
        
        // Consumer thread
        std::thread consumer([&queue, &producer_done, &consumed_data]() {
            int value;
            while (!producer_done.load() || !queue.empty()) {
                if (queue.dequeue(value)) {
                    consumed_data.push_back(value);
                } else {
                    CPURelax::pause();
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        // Verify correctness
        ASSERT_EQ(num_items, consumed_data.size());
        ASSERT_TRUE(test.verify_sequence(consumed_data, 0));
        
        // Performance check: should handle 100k items in reasonable time
        double throughput = static_cast<double>(num_items) / duration.count() * 1000000;  // items/second
        ASSERT_TRUE(throughput > 1000000);  // At least 1M items/second
    });
    
    concurrent_suite.run_test("AtomicUtils_ConcurrentCounters_ThreadSafe", [&]() {
        std::atomic<int> shared_counter(0);
        const int num_threads = 4;
        const int increments_per_thread = 10000;
        
        std::vector<std::thread> threads;
        
        // Launch threads that increment counter concurrently
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&shared_counter, increments_per_thread]() {
                for (int i = 0; i < increments_per_thread; i++) {
                    AtomicUtils::incrementCounter(shared_counter);
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify final count
        int expected_total = num_threads * increments_per_thread;
        ASSERT_EQ(expected_total, shared_counter.load());
    });
    
    concurrent_suite.run_test("SpinLock_ConcurrentAccess_MutualExclusion", [&]() {
        SpinLock lock;
        std::atomic<int> shared_resource{0};
        std::atomic<int> concurrent_access_count{0};
        const int num_threads = 4;
        const int iterations_per_thread = 1000;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&lock, &shared_resource, &concurrent_access_count, iterations_per_thread]() {
                for (int i = 0; i < iterations_per_thread; i++) {
                    SpinLockGuard guard(lock);
                    
                    // Critical section - simulate some work
                    int old_value = shared_resource.load();
                    
                    // Check if another thread is in critical section
                    if (concurrent_access_count.fetch_add(1) > 0) {
                        // This should never happen with proper locking
                        test.error_count_.fetch_add(1);
                    }
                    
                    // Simulate some work that could be interrupted
                    for (int j = 0; j < 10; j++) {
                        CPURelax::pause();
                    }
                    
                    shared_resource.store(old_value + 1);
                    concurrent_access_count.fetch_sub(1);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify results
        ASSERT_EQ(0, test.error_count_.load());  // No concurrent access detected
        ASSERT_EQ(num_threads * iterations_per_thread, shared_resource.load());
        ASSERT_EQ(0, concurrent_access_count.load());  // All threads exited critical section
    });
    
    runner.add_suite(std::move(concurrent_suite));
    
    // === Memory Ordering and Cache Alignment Tests ===
    TestSuite memory_ordering_suite("Memory Ordering and Cache Alignment");
    memory_ordering_suite.add_setup([&test]() { test.setup(); });
    memory_ordering_suite.add_teardown([&test]() { test.teardown(); });
    
    memory_ordering_suite.run_test("CacheAlignedAtomic_Alignment", [&]() {
        CacheAlignedAtomic<int> atomic1;
        CacheAlignedAtomic<int> atomic2;
        
        // Check that atomics are properly cache-aligned
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(&atomic1);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(&atomic2);
        
        ASSERT_EQ(0, addr1 % CACHE_LINE_SIZE);  // Should be cache-line aligned
        ASSERT_EQ(0, addr2 % CACHE_LINE_SIZE);
        
        // Should be on different cache lines
        ASSERT_TRUE(abs(static_cast<long>(addr2 - addr1)) >= static_cast<long>(CACHE_LINE_SIZE));
    });
    
    memory_ordering_suite.run_test("MemoryBarrier_CompilerBarrier", [&]() {
        volatile int value1 = 1;
        volatile int value2 = 2;
        
        // Compiler barrier should prevent reordering
        MemoryBarrier::compilerBarrier();
        
        // This is more of a documentation test - actual reordering prevention
        // is difficult to test reliably in unit tests
        ASSERT_EQ(1, value1);
        ASSERT_EQ(2, value2);
    });
    
    memory_ordering_suite.run_test("SPSCQueue_MemoryOrdering_Correctness", [&]() {
        SPSCQueue<std::pair<int, int>> queue(128);
        const int num_pairs = 1000;
        std::atomic<bool> producer_done{false};
        std::atomic<int> ordering_errors{0};
        
        // Producer: enqueue pairs where second value = first value + 1
        std::thread producer([&queue, num_pairs, &producer_done]() {
            for (int i = 0; i < num_pairs; i++) {
                while (!queue.enqueue({i, i + 1})) {
                    CPURelax::pause();
                }
            }
            producer_done.store(true);
        });
        
        // Consumer: verify the relationship is preserved
        std::thread consumer([&queue, &producer_done, &ordering_errors]() {
            std::pair<int, int> pair;
            while (!producer_done.load() || !queue.empty()) {
                if (queue.dequeue(pair)) {
                    if (pair.second != pair.first + 1) {
                        ordering_errors.fetch_add(1);
                    }
                } else {
                    CPURelax::pause();
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        // If memory ordering is correct, no ordering errors should occur
        ASSERT_EQ(0, ordering_errors.load());
    });
    
    runner.add_suite(std::move(memory_ordering_suite));
    
    // === Stress and Edge Case Tests ===
    TestSuite stress_suite("Stress and Edge Cases");
    stress_suite.add_setup([&test]() { test.setup(); });
    stress_suite.add_teardown([&test]() { test.teardown(); });
    
    stress_suite.run_test("SPSCQueue_RapidEnqueueDequeue_StressTest", [&]() {
        SPSCQueue<int> queue(64);  // Small queue to force blocking
        const int test_duration_ms = 1000;
        std::atomic<bool> stop_test{false};
        std::atomic<int> total_enqueued{0};
        std::atomic<int> total_dequeued{0};
        
        // Producer thread - rapid enqueue
        std::thread producer([&queue, &stop_test, &total_enqueued]() {
            int value = 0;
            while (!stop_test.load()) {
                if (queue.enqueue(value++)) {
                    total_enqueued.fetch_add(1);
                } else {
                    CPURelax::pause();
                }
            }
        });
        
        // Consumer thread - rapid dequeue  
        std::thread consumer([&queue, &stop_test, &total_dequeued]() {
            int value;
            while (!stop_test.load()) {
                if (queue.dequeue(value)) {
                    total_dequeued.fetch_add(1);
                } else {
                    CPURelax::pause();
                }
            }
            // Drain remaining items
            while (queue.dequeue(value)) {
                total_dequeued.fetch_add(1);
            }
        });
        
        // Run test for specified duration
        std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
        stop_test.store(true);
        
        producer.join();
        consumer.join();
        
        // Verify no items were lost
        ASSERT_EQ(total_enqueued.load(), total_dequeued.load());
        ASSERT_TRUE(queue.empty());
        
        // Should have processed a significant number of items
        ASSERT_TRUE(total_enqueued.load() > 10000);
    });
    
    stress_suite.run_test("PerformanceCounter_ConcurrentRecording", [&]() {
        PerformanceCounter counter;
        const int num_threads = 4;
        const int records_per_thread = 10000;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&counter, records_per_thread, t]() {
                std::random_device rd;
                std::mt19937 gen(rd() + t);
                std::uniform_int_distribution<uint64_t> latency_dist(50, 500);
                
                for (int i = 0; i < records_per_thread; i++) {
                    uint64_t latency = latency_dist(gen);
                    counter.recordOperation(latency);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify total operations recorded
        uint64_t expected_operations = num_threads * records_per_thread;
        ASSERT_EQ(expected_operations, counter.getOperationCount());
        
        // Average should be within reasonable range
        double avg_latency = counter.getAverageLatency();
        ASSERT_TRUE(avg_latency >= 50.0 && avg_latency <= 500.0);
        
        // Max should be within range
        uint64_t max_latency = counter.getMaxLatency();
        ASSERT_TRUE(max_latency >= 50 && max_latency <= 500);
    });
    
    runner.add_suite(std::move(stress_suite));
    
    // Run all tests
    bool all_passed = runner.run_all();
    return all_passed ? 0 : 1;
}