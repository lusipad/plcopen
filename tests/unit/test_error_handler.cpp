/**
 * @file test_error_handler.cpp
 * @brief Comprehensive Unit Tests for Error Handling Module
 * @version 1.0
 * @date 2025-09-06
 * 
 * Complete test coverage for error handling components including:
 * - Error code classification and utilities
 * - Error handler functionality and recovery strategies
 * - Event listeners and notification system
 * - Error statistics and history tracking
 * - Recovery actions and conditions
 * - Thread safety and concurrent error handling
 */

#include "test/TestFramework.h"
#include "error/error_codes.h"
#include "error/error_handler.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <future>
#include <fstream>
#include <sstream>

using namespace plc_test;
using namespace plc_runtime::error;

class ErrorHandlerTest {
public:
    void setup() {
        test_listener_ = std::make_shared<TestErrorListener>();
        handler_ = std::make_unique<ErrorHandler>();
        error_count_.store(0);
        recovery_count_.store(0);
        last_error_code_ = ErrorCode::SUCCESS;
        last_recovery_action_ = RecoveryAction::NONE;
    }
    
    void teardown() {
        handler_.reset();
        test_listener_.reset();
    }

protected:
    // Test error listener for verification
    class TestErrorListener : public ErrorEventListener {
    public:
        std::atomic<int> error_notifications{0};
        std::atomic<int> recovery_notifications{0};
        ErrorContext last_error_context;
        RecoveryAction last_recovery_action{RecoveryAction::NONE};
        bool last_recovery_success{false};
        
        void onError(const ErrorContext& context) override {
            error_notifications.fetch_add(1);
            last_error_context = context;
        }
        
        void onRecovery(const ErrorContext& context, 
                       RecoveryAction action, 
                       bool success) override {
            recovery_notifications.fetch_add(1);
            last_recovery_action = action;
            last_recovery_success = success;
        }
        
        void reset() {
            error_notifications.store(0);
            recovery_notifications.store(0);
            last_recovery_action = RecoveryAction::NONE;
            last_recovery_success = false;
        }
    };

    std::unique_ptr<ErrorHandler> handler_;
    std::shared_ptr<TestErrorListener> test_listener_;
    std::atomic<int> error_count_{0};
    std::atomic<int> recovery_count_{0};
    ErrorCode last_error_code_{ErrorCode::SUCCESS};
    RecoveryAction last_recovery_action_{RecoveryAction::NONE};
    
    // Helper: Create test error context
    ErrorContext create_test_context(ErrorCode code, 
                                   const std::string& component = "TestComponent",
                                   const std::string& message = "Test error") {
        return ErrorContext(code, component, "test_function", "test_file.cpp", 123, message);
    }
};

int main() {
    TestRunner runner;
    
    // === Error Code Utilities Tests ===
    TestSuite error_codes_suite("Error Code Utilities");
    ErrorHandlerTest test;
    
    error_codes_suite.add_setup([&test]() { test.setup(); });
    error_codes_suite.add_teardown([&test]() { test.teardown(); });
    
    error_codes_suite.run_test("ErrorCodeUtils_GetCategory_ReturnsCorrectCategory", [&]() {
        ASSERT_EQ(ErrorCategory::SUCCESS, ErrorCodeUtils::getCategory(ErrorCode::SUCCESS));
        ASSERT_EQ(ErrorCategory::SCHEDULER, ErrorCodeUtils::getCategory(ErrorCode::SCHEDULER_NOT_INITIALIZED));
        ASSERT_EQ(ErrorCategory::MEMORY, ErrorCodeUtils::getCategory(ErrorCode::MEMORY_ALLOCATION_FAILED));
        ASSERT_EQ(ErrorCategory::COMPILER, ErrorCodeUtils::getCategory(ErrorCode::COMPILER_SYNTAX_ERROR));
        ASSERT_EQ(ErrorCategory::RUNTIME, ErrorCodeUtils::getCategory(ErrorCode::RUNTIME_DIVISION_BY_ZERO));
        ASSERT_EQ(ErrorCategory::SYSTEM, ErrorCodeUtils::getCategory(ErrorCode::SYSTEM_INITIALIZATION_FAILED));
        ASSERT_EQ(ErrorCategory::IO, ErrorCodeUtils::getCategory(ErrorCode::IO_DEVICE_NOT_FOUND));
        ASSERT_EQ(ErrorCategory::MOTION, ErrorCodeUtils::getCategory(ErrorCode::MOTION_AXIS_FAULT));
        ASSERT_EQ(ErrorCategory::SECURITY, ErrorCodeUtils::getCategory(ErrorCode::SECURITY_ACCESS_DENIED));
    });
    
    error_codes_suite.run_test("ErrorCodeUtils_GetSeverity_ReturnsCorrectSeverity", [&]() {
        ASSERT_EQ(ErrorSeverity::INFO, ErrorCodeUtils::getSeverity(ErrorCode::SUCCESS));
        ASSERT_EQ(ErrorSeverity::WARNING, ErrorCodeUtils::getSeverity(ErrorCode::TIMEOUT));
        ASSERT_EQ(ErrorSeverity::ERROR, ErrorCodeUtils::getSeverity(ErrorCode::MEMORY_ALLOCATION_FAILED));
        ASSERT_EQ(ErrorSeverity::CRITICAL, ErrorCodeUtils::getSeverity(ErrorCode::RUNTIME_STACK_OVERFLOW));
        ASSERT_EQ(ErrorSeverity::FATAL, ErrorCodeUtils::getSeverity(ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE));
    });
    
    error_codes_suite.run_test("ErrorCodeUtils_StatusChecks_WorkCorrectly", [&]() {
        ASSERT_TRUE(ErrorCodeUtils::isSuccess(ErrorCode::SUCCESS));
        ASSERT_FALSE(ErrorCodeUtils::isSuccess(ErrorCode::MEMORY_ALLOCATION_FAILED));
        
        ASSERT_FALSE(ErrorCodeUtils::isError(ErrorCode::SUCCESS));
        ASSERT_TRUE(ErrorCodeUtils::isError(ErrorCode::SCHEDULER_NOT_INITIALIZED));
        
        ASSERT_FALSE(ErrorCodeUtils::isFatal(ErrorCode::MEMORY_ALLOCATION_FAILED));
        ASSERT_TRUE(ErrorCodeUtils::isFatal(ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE));
        
        ASSERT_FALSE(ErrorCodeUtils::isCritical(ErrorCode::MEMORY_ALLOCATION_FAILED));
        ASSERT_TRUE(ErrorCodeUtils::isCritical(ErrorCode::RUNTIME_STACK_OVERFLOW));
        ASSERT_TRUE(ErrorCodeUtils::isCritical(ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE));
    });
    
    error_codes_suite.run_test("ErrorCodeUtils_GetMessage_ReturnsValidMessage", [&]() {
        auto message = ErrorCodeUtils::getMessage(ErrorCode::MEMORY_ALLOCATION_FAILED);
        ASSERT_FALSE(message.empty());
        
        auto description = ErrorCodeUtils::getDescription(ErrorCode::SCHEDULER_DEADLINE_MISSED);
        ASSERT_FALSE(description.empty());
        
        auto solution = ErrorCodeUtils::getSolution(ErrorCode::IO_DEVICE_NOT_FOUND);
        ASSERT_FALSE(solution.empty());
    });
    
    error_codes_suite.run_test("ErrorCodeUtils_GetErrorInfo_ReturnsCompleteInfo", [&]() {
        auto info = ErrorCodeUtils::getErrorInfo(ErrorCode::RUNTIME_DIVISION_BY_ZERO);
        
        ASSERT_EQ(ErrorCode::RUNTIME_DIVISION_BY_ZERO, info.code);
        ASSERT_EQ(ErrorCategory::RUNTIME, info.category);
        ASSERT_EQ(ErrorSeverity::CRITICAL, info.severity);
        ASSERT_FALSE(info.message.empty());
        ASSERT_FALSE(info.description.empty());
        ASSERT_FALSE(info.solution.empty());
    });
    
    error_codes_suite.run_test("ErrorCodeUtils_ToString_ReturnsValidString", [&]() {
        std::string str = ErrorCodeUtils::toString(ErrorCode::MEMORY_ALLOCATION_FAILED);
        ASSERT_FALSE(str.empty());
        ASSERT_TRUE(str.find("MEMORY") != std::string::npos);
        
        auto severity_str = ErrorCodeUtils::severityToString(ErrorSeverity::CRITICAL);
        ASSERT_FALSE(severity_str.empty());
        ASSERT_EQ("CRITICAL", severity_str);
        
        auto category_str = ErrorCodeUtils::categoryToString(ErrorCategory::SCHEDULER);
        ASSERT_FALSE(category_str.empty());
        ASSERT_EQ("SCHEDULER", category_str);
    });
    
    runner.add_suite(std::move(error_codes_suite));
    
    // === Error Handler Basic Functionality Tests ===
    TestSuite handler_basic_suite("Error Handler Basic Functionality");
    handler_basic_suite.add_setup([&test]() { test.setup(); });
    handler_basic_suite.add_teardown([&test]() { test.teardown(); });
    
    handler_basic_suite.run_test("ErrorHandler_Constructor_InitialState", [&]() {
        auto stats = test.handler_->getStatistics();
        
        ASSERT_EQ(0, stats.totalErrors.load());
        ASSERT_EQ(0, stats.fatalErrors.load());
        ASSERT_EQ(0, stats.criticalErrors.load());
        ASSERT_EQ(0, stats.errors.load());
        ASSERT_EQ(0, stats.warnings.load());
        ASSERT_EQ(ErrorCode::SUCCESS, stats.lastErrorCode);
        
        ASSERT_TRUE(test.handler_->isSystemHealthy());
        ASSERT_EQ(0.0, test.handler_->getErrorRate());
    });
    
    handler_basic_suite.run_test("ErrorHandler_HandleError_UpdatesStatistics", [&]() {
        auto context = test.create_test_context(ErrorCode::MEMORY_ALLOCATION_FAILED);
        
        RecoveryAction action = test.handler_->handleError(context);
        
        auto stats = test.handler_->getStatistics();
        ASSERT_EQ(1, stats.totalErrors.load());
        ASSERT_EQ(1, stats.errors.load());
        ASSERT_EQ(1, stats.memoryErrors.load());
        ASSERT_EQ(ErrorCode::MEMORY_ALLOCATION_FAILED, stats.lastErrorCode);
    });
    
    handler_basic_suite.run_test("ErrorHandler_HandleMultipleErrors_AccumulatesStatistics", [&]() {
        // Handle different types of errors
        test.handler_->handleError(test.create_test_context(ErrorCode::SCHEDULER_DEADLINE_MISSED));
        test.handler_->handleError(test.create_test_context(ErrorCode::MEMORY_POOL_EXHAUSTED));
        test.handler_->handleError(test.create_test_context(ErrorCode::IO_DEVICE_FAULT));
        test.handler_->handleError(test.create_test_context(ErrorCode::RUNTIME_STACK_OVERFLOW));  // Critical
        
        auto stats = test.handler_->getStatistics();
        ASSERT_EQ(4, stats.totalErrors.load());
        ASSERT_EQ(3, stats.errors.load());        // First 3 are ERROR severity
        ASSERT_EQ(1, stats.criticalErrors.load()); // Last one is CRITICAL
        ASSERT_EQ(1, stats.schedulerErrors.load());
        ASSERT_EQ(1, stats.memoryErrors.load());
        ASSERT_EQ(1, stats.ioErrors.load());
    });
    
    handler_basic_suite.run_test("ErrorHandler_AddListener_ReceivesNotifications", [&]() {
        test.handler_->addListener(test.test_listener_);
        
        auto context = test.create_test_context(ErrorCode::COMPILER_SYNTAX_ERROR, "TestComp", "Test message");
        test.handler_->handleError(context);
        
        ASSERT_EQ(1, test.test_listener_->error_notifications.load());
        ASSERT_EQ(ErrorCode::COMPILER_SYNTAX_ERROR, test.test_listener_->last_error_context.code);
        ASSERT_EQ("TestComp", test.test_listener_->last_error_context.component);
        ASSERT_EQ("Test message", test.test_listener_->last_error_context.message);
    });
    
    handler_basic_suite.run_test("ErrorHandler_RemoveListener_StopsNotifications", [&]() {
        test.handler_->addListener(test.test_listener_);
        test.handler_->handleError(test.create_test_context(ErrorCode::IO_TIMEOUT));
        ASSERT_EQ(1, test.test_listener_->error_notifications.load());
        
        test.handler_->removeListener(test.test_listener_);
        test.handler_->handleError(test.create_test_context(ErrorCode::IO_DEVICE_FAULT));
        ASSERT_EQ(1, test.test_listener_->error_notifications.load());  // Should remain 1
    });
    
    handler_basic_suite.run_test("ErrorHandler_ErrorHistory_RecordsErrors", [&]() {
        // Add some errors to history
        test.handler_->handleError(test.create_test_context(ErrorCode::MEMORY_ALLOCATION_FAILED, "Comp1"));
        test.handler_->handleError(test.create_test_context(ErrorCode::SCHEDULER_DEADLINE_MISSED, "Comp2"));
        test.handler_->handleError(test.create_test_context(ErrorCode::IO_DEVICE_FAULT, "Comp3"));
        
        auto history = test.handler_->getErrorHistory(10);
        ASSERT_EQ(3, history.size());
        
        // Should be in chronological order (most recent first)
        ASSERT_EQ(ErrorCode::IO_DEVICE_FAULT, history[0].code);
        ASSERT_EQ("Comp3", history[0].component);
        ASSERT_EQ(ErrorCode::SCHEDULER_DEADLINE_MISSED, history[1].code);
        ASSERT_EQ("Comp2", history[1].component);
        ASSERT_EQ(ErrorCode::MEMORY_ALLOCATION_FAILED, history[2].code);
        ASSERT_EQ("Comp1", history[2].component);
    });
    
    handler_basic_suite.run_test("ErrorHandler_ClearErrorHistory_RemovesAllEntries", [&]() {
        test.handler_->handleError(test.create_test_context(ErrorCode::RUNTIME_DIVISION_BY_ZERO));
        test.handler_->handleError(test.create_test_context(ErrorCode::MOTION_AXIS_FAULT));
        
        auto history_before = test.handler_->getErrorHistory();
        ASSERT_EQ(2, history_before.size());
        
        test.handler_->clearErrorHistory();
        
        auto history_after = test.handler_->getErrorHistory();
        ASSERT_EQ(0, history_after.size());
    });
    
    handler_basic_suite.run_test("ErrorHandler_ResetStatistics_ClearsCounters", [&]() {
        test.handler_->handleError(test.create_test_context(ErrorCode::MEMORY_DOUBLE_FREE));
        test.handler_->handleError(test.create_test_context(ErrorCode::SCHEDULER_QUEUE_FULL));
        
        auto stats_before = test.handler_->getStatistics();
        ASSERT_EQ(2, stats_before.totalErrors.load());
        
        test.handler_->resetStatistics();
        
        auto stats_after = test.handler_->getStatistics();
        ASSERT_EQ(0, stats_after.totalErrors.load());
        ASSERT_EQ(0, stats_after.errors.load());
        ASSERT_EQ(ErrorCode::SUCCESS, stats_after.lastErrorCode);
    });
    
    runner.add_suite(std::move(handler_basic_suite));
    
    // === Recovery Strategy Tests ===
    TestSuite recovery_suite("Recovery Strategy");
    recovery_suite.add_setup([&test]() { test.setup(); });
    recovery_suite.add_teardown([&test]() { test.teardown(); });
    
    recovery_suite.run_test("ErrorHandler_RegisterRecoveryStrategy_HandlesRecovery", [&]() {
        bool recovery_handler_called = false;
        RecoveryStrategy strategy(ErrorCode::MEMORY_ALLOCATION_FAILED, RecoveryAction::RETRY, 2, 100);
        strategy.handler = [&recovery_handler_called]() -> bool {
            recovery_handler_called = true;
            return true;  // Simulate successful recovery
        };
        
        test.handler_->registerRecoveryStrategy(strategy);
        test.handler_->addListener(test.test_listener_);
        
        auto context = test.create_test_context(ErrorCode::MEMORY_ALLOCATION_FAILED);
        RecoveryAction action = test.handler_->handleError(context);
        
        ASSERT_EQ(RecoveryAction::RETRY, action);
        ASSERT_TRUE(recovery_handler_called);
        ASSERT_EQ(1, test.test_listener_->recovery_notifications.load());
        ASSERT_EQ(RecoveryAction::RETRY, test.test_listener_->last_recovery_action);
        ASSERT_TRUE(test.test_listener_->last_recovery_success);
    });
    
    recovery_suite.run_test("ErrorHandler_RecoveryStrategyFailure_ReportsFailure", [&]() {
        RecoveryStrategy strategy(ErrorCode::IO_COMMUNICATION_ERROR, RecoveryAction::FALLBACK, 1, 50);
        strategy.handler = []() -> bool {
            return false;  // Simulate recovery failure
        };
        
        test.handler_->registerRecoveryStrategy(strategy);
        test.handler_->addListener(test.test_listener_);
        
        auto context = test.create_test_context(ErrorCode::IO_COMMUNICATION_ERROR);
        RecoveryAction action = test.handler_->handleError(context);
        
        ASSERT_EQ(RecoveryAction::FALLBACK, action);
        ASSERT_EQ(1, test.test_listener_->recovery_notifications.load());
        ASSERT_EQ(RecoveryAction::FALLBACK, test.test_listener_->last_recovery_action);
        ASSERT_FALSE(test.test_listener_->last_recovery_success);
    });
    
    recovery_suite.run_test("ErrorHandler_MultipleRecoveryStrategies_SelectsCorrectOne", [&]() {
        int memory_recovery_calls = 0;
        int scheduler_recovery_calls = 0;
        
        // Register strategies for different error codes
        RecoveryStrategy memStrategy(ErrorCode::MEMORY_POOL_EXHAUSTED, RecoveryAction::RETRY);
        memStrategy.handler = [&memory_recovery_calls]() -> bool {
            memory_recovery_calls++;
            return true;
        };
        
        RecoveryStrategy schedStrategy(ErrorCode::SCHEDULER_TASK_CREATE_FAILED, RecoveryAction::FALLBACK);
        schedStrategy.handler = [&scheduler_recovery_calls]() -> bool {
            scheduler_recovery_calls++;
            return true;
        };
        
        test.handler_->registerRecoveryStrategy(memStrategy);
        test.handler_->registerRecoveryStrategy(schedStrategy);
        
        // Test memory error recovery
        auto memContext = test.create_test_context(ErrorCode::MEMORY_POOL_EXHAUSTED);
        RecoveryAction memAction = test.handler_->handleError(memContext);
        
        ASSERT_EQ(RecoveryAction::RETRY, memAction);
        ASSERT_EQ(1, memory_recovery_calls);
        ASSERT_EQ(0, scheduler_recovery_calls);
        
        // Test scheduler error recovery
        auto schedContext = test.create_test_context(ErrorCode::SCHEDULER_TASK_CREATE_FAILED);
        RecoveryAction schedAction = test.handler_->handleError(schedContext);
        
        ASSERT_EQ(RecoveryAction::FALLBACK, schedAction);
        ASSERT_EQ(1, memory_recovery_calls);  // Should remain 1
        ASSERT_EQ(1, scheduler_recovery_calls);
    });
    
    recovery_suite.run_test("ErrorHandler_RemoveRecoveryStrategy_NoLongerHandlesRecovery", [&]() {
        bool recovery_called = false;
        RecoveryStrategy strategy(ErrorCode::COMPILER_LEXICAL_ERROR, RecoveryAction::RETRY);
        strategy.handler = [&recovery_called]() -> bool {
            recovery_called = true;
            return true;
        };
        
        test.handler_->registerRecoveryStrategy(strategy);
        
        // First error should trigger recovery
        auto context1 = test.create_test_context(ErrorCode::COMPILER_LEXICAL_ERROR);
        RecoveryAction action1 = test.handler_->handleError(context1);
        ASSERT_EQ(RecoveryAction::RETRY, action1);
        ASSERT_TRUE(recovery_called);
        
        // Remove strategy
        recovery_called = false;
        test.handler_->removeRecoveryStrategy(ErrorCode::COMPILER_LEXICAL_ERROR);
        
        // Second error should not trigger recovery
        auto context2 = test.create_test_context(ErrorCode::COMPILER_LEXICAL_ERROR);
        RecoveryAction action2 = test.handler_->handleError(context2);
        ASSERT_EQ(RecoveryAction::NONE, action2);
        ASSERT_FALSE(recovery_called);
    });
    
    recovery_suite.run_test("ErrorHandler_RecoveryCondition_CheckedBeforeExecution", [&]() {
        bool condition_checked = false;
        bool recovery_executed = false;
        
        RecoveryStrategy strategy(ErrorCode::MOTION_DRIVE_FAULT, RecoveryAction::RESTART);
        strategy.condition = [&condition_checked]() -> bool {
            condition_checked = true;
            return false;  // Condition not met
        };
        strategy.handler = [&recovery_executed]() -> bool {
            recovery_executed = true;
            return true;
        };
        
        test.handler_->registerRecoveryStrategy(strategy);
        
        auto context = test.create_test_context(ErrorCode::MOTION_DRIVE_FAULT);
        RecoveryAction action = test.handler_->handleError(context);
        
        ASSERT_TRUE(condition_checked);
        ASSERT_FALSE(recovery_executed);  // Should not execute due to failed condition
        ASSERT_EQ(RecoveryAction::NONE, action);  // Should return NONE when condition fails
    });
    
    runner.add_suite(std::move(recovery_suite));
    
    // === Error Listeners Tests ===
    TestSuite listeners_suite("Error Listeners");
    listeners_suite.add_setup([&test]() { test.setup(); });
    listeners_suite.add_teardown([&test]() { test.teardown(); });
    
    listeners_suite.run_test("DefaultErrorListener_HandleErrors_WorksCorrectly", [&]() {
        auto defaultListener = std::make_shared<DefaultErrorListener>();
        test.handler_->addListener(defaultListener);
        
        // This test mainly verifies that DefaultErrorListener doesn't crash
        // and can be added/removed without issues
        auto context = test.create_test_context(ErrorCode::SYSTEM_HARDWARE_FAULT);
        test.handler_->handleError(context);
        
        // No assertion failure means the listener worked correctly
        ASSERT_TRUE(true);
        
        test.handler_->removeListener(defaultListener);
    });
    
    listeners_suite.run_test("FileLogErrorListener_LogsToFile", [&]() {
        const std::string logPath = "/tmp/plc_error_test.log";
        
        // Remove any existing log file
        std::remove(logPath.c_str());
        
        {
            auto fileListener = std::make_shared<FileLogErrorListener>(logPath);
            test.handler_->addListener(fileListener);
            
            auto context = test.create_test_context(ErrorCode::RUNTIME_ASSERTION_FAILED, 
                                                  "TestComponent", "Test assertion failed");
            test.handler_->handleError(context);
            
            // Allow some time for file writing
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }  // FileLogErrorListener destructor should flush and close file
        
        // Verify log file exists and contains expected content
        std::ifstream logFile(logPath);
        ASSERT_TRUE(logFile.is_open());
        
        std::string line;
        bool found_error_entry = false;
        while (std::getline(logFile, line)) {
            if (line.find("RUNTIME_ASSERTION_FAILED") != std::string::npos &&
                line.find("TestComponent") != std::string::npos &&
                line.find("Test assertion failed") != std::string::npos) {
                found_error_entry = true;
                break;
            }
        }
        
        ASSERT_TRUE(found_error_entry);
        logFile.close();
        
        // Clean up
        std::remove(logPath.c_str());
    });
    
    listeners_suite.run_test("MultipleListeners_AllReceiveNotifications", [&]() {
        auto listener1 = std::make_shared<ErrorHandlerTest::TestErrorListener>();
        auto listener2 = std::make_shared<ErrorHandlerTest::TestErrorListener>();
        auto listener3 = std::make_shared<ErrorHandlerTest::TestErrorListener>();
        
        test.handler_->addListener(listener1);
        test.handler_->addListener(listener2);
        test.handler_->addListener(listener3);
        
        auto context = test.create_test_context(ErrorCode::SECURITY_ACCESS_DENIED);
        test.handler_->handleError(context);
        
        ASSERT_EQ(1, listener1->error_notifications.load());
        ASSERT_EQ(1, listener2->error_notifications.load());
        ASSERT_EQ(1, listener3->error_notifications.load());
        
        ASSERT_EQ(ErrorCode::SECURITY_ACCESS_DENIED, listener1->last_error_context.code);
        ASSERT_EQ(ErrorCode::SECURITY_ACCESS_DENIED, listener2->last_error_context.code);
        ASSERT_EQ(ErrorCode::SECURITY_ACCESS_DENIED, listener3->last_error_context.code);
    });
    
    runner.add_suite(std::move(listeners_suite));
    
    // === Concurrent Access Tests ===
    TestSuite concurrent_suite("Concurrent Access");
    concurrent_suite.add_setup([&test]() { test.setup(); });
    concurrent_suite.add_teardown([&test]() { test.teardown(); });
    
    concurrent_suite.run_test("ErrorHandler_ConcurrentErrors_ThreadSafe", [&]() {
        const int num_threads = 4;
        const int errors_per_thread = 100;
        std::atomic<int> total_handled{0};
        
        auto listener = std::make_shared<ErrorHandlerTest::TestErrorListener>();
        test.handler_->addListener(listener);
        
        std::vector<std::thread> threads;
        
        // Launch threads that generate errors concurrently
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&test, &total_handled, errors_per_thread, t]() {
                ErrorCode errors[] = {
                    ErrorCode::MEMORY_ALLOCATION_FAILED,
                    ErrorCode::SCHEDULER_DEADLINE_MISSED,
                    ErrorCode::IO_DEVICE_FAULT,
                    ErrorCode::RUNTIME_DIVISION_BY_ZERO
                };
                
                for (int i = 0; i < errors_per_thread; i++) {
                    ErrorCode error = errors[i % 4];
                    std::string component = "Thread" + std::to_string(t);
                    auto context = ErrorContext(error, component, "test_func", "test.cpp", 
                                              100 + i, "Concurrent test error");
                    
                    test.handler_->handleError(context);
                    total_handled.fetch_add(1);
                    
                    // Small delay to increase chance of contention
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify results
        int expected_total = num_threads * errors_per_thread;
        ASSERT_EQ(expected_total, total_handled.load());
        
        auto stats = test.handler_->getStatistics();
        ASSERT_EQ(expected_total, stats.totalErrors.load());
        
        // All listener notifications should have been received
        ASSERT_EQ(expected_total, listener->error_notifications.load());
        
        // Error history should contain entries (may be limited by buffer size)
        auto history = test.handler_->getErrorHistory(expected_total);
        ASSERT_TRUE(history.size() > 0);
        ASSERT_TRUE(history.size() <= expected_total);
    });
    
    concurrent_suite.run_test("ErrorHandler_ConcurrentRecovery_ThreadSafe", [&]() {
        std::atomic<int> recovery_attempts{0};
        std::atomic<int> successful_recoveries{0};
        
        RecoveryStrategy strategy(ErrorCode::COMM_CONNECTION_LOST, RecoveryAction::RETRY, 3, 10);
        strategy.handler = [&recovery_attempts, &successful_recoveries]() -> bool {
            recovery_attempts.fetch_add(1);
            
            // Simulate some recovery work
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
            bool success = (recovery_attempts.load() % 3 != 0);  // Succeed 2/3 of the time
            if (success) {
                successful_recoveries.fetch_add(1);
            }
            return success;
        };
        
        test.handler_->registerRecoveryStrategy(strategy);
        
        const int num_threads = 3;
        const int errors_per_thread = 20;
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&test, errors_per_thread]() {
                for (int i = 0; i < errors_per_thread; i++) {
                    auto context = test.create_test_context(ErrorCode::COMM_CONNECTION_LOST, 
                                                          "ConcurrentTest", "Connection lost");
                    test.handler_->handleError(context);
                    
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify that recoveries were attempted
        int expected_errors = num_threads * errors_per_thread;
        ASSERT_EQ(expected_errors, recovery_attempts.load());
        ASSERT_TRUE(successful_recoveries.load() > 0);
        ASSERT_TRUE(successful_recoveries.load() <= expected_errors);
    });
    
    runner.add_suite(std::move(concurrent_suite));
    
    // === System Health and Error Rate Tests ===
    TestSuite health_suite("System Health and Error Rate");
    health_suite.add_setup([&test]() { test.setup(); });
    health_suite.add_teardown([&test]() { test.teardown(); });
    
    health_suite.run_test("ErrorHandler_SystemHealth_ReflectsErrorState", [&]() {
        // Initially healthy
        ASSERT_TRUE(test.handler_->isSystemHealthy());
        
        // Normal errors shouldn't affect system health
        test.handler_->handleError(test.create_test_context(ErrorCode::TIMEOUT));
        test.handler_->handleError(test.create_test_context(ErrorCode::MEMORY_ALLOCATION_FAILED));
        ASSERT_TRUE(test.handler_->isSystemHealthy());
        
        // Critical error should affect system health
        test.handler_->handleError(test.create_test_context(ErrorCode::RUNTIME_STACK_OVERFLOW));
        ASSERT_FALSE(test.handler_->isSystemHealthy());
        
        // Fatal error definitely affects system health
        test.handler_->handleError(test.create_test_context(ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE));
        ASSERT_FALSE(test.handler_->isSystemHealthy());
    });
    
    health_suite.run_test("ErrorHandler_ErrorRate_CalculatedCorrectly", [&]() {
        // Initially no errors
        ASSERT_EQ(0.0, test.handler_->getErrorRate());
        
        // Generate some errors quickly
        auto start_time = std::chrono::steady_clock::now();
        
        for (int i = 0; i < 10; i++) {
            test.handler_->handleError(test.create_test_context(ErrorCode::IO_TIMEOUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration_minutes = std::chrono::duration<double>(end_time - start_time).count() / 60.0;
        
        double error_rate = test.handler_->getErrorRate();
        double expected_rate = 10.0 / duration_minutes;
        
        // Error rate should be reasonable (within 50% of expected due to timing variations)
        ASSERT_TRUE(error_rate >= expected_rate * 0.5);
        ASSERT_TRUE(error_rate <= expected_rate * 2.0);
    });
    
    runner.add_suite(std::move(health_suite));
    
    // === Error Context Tests ===
    TestSuite context_suite("Error Context");
    context_suite.add_setup([&test]() { test.setup(); });
    context_suite.add_teardown([&test]() { test.teardown(); });
    
    context_suite.run_test("ErrorContext_Constructor_SetsFieldsCorrectly", [&]() {
        auto before_time = std::chrono::system_clock::now();
        
        ErrorContext context(ErrorCode::COMPILER_TYPE_ERROR, 
                           "CompilerModule", 
                           "parseExpression", 
                           "parser.cpp", 
                           456, 
                           "Type mismatch in expression");
        
        auto after_time = std::chrono::system_clock::now();
        
        ASSERT_EQ(ErrorCode::COMPILER_TYPE_ERROR, context.code);
        ASSERT_EQ("CompilerModule", context.component);
        ASSERT_EQ("parseExpression", context.function);
        ASSERT_EQ("parser.cpp", context.file);
        ASSERT_EQ(456, context.line);
        ASSERT_EQ("Type mismatch in expression", context.message);
        
        // Timestamp should be between before and after
        ASSERT_TRUE(context.timestamp >= before_time);
        ASSERT_TRUE(context.timestamp <= after_time);
        
        // Thread ID should be set
        ASSERT_NE(0, context.threadId);
    });
    
    runner.add_suite(std::move(context_suite));
    
    // === Global Error Handler Tests ===
    TestSuite global_suite("Global Error Handler");
    global_suite.add_setup([&test]() { test.setup(); });
    global_suite.add_teardown([&test]() { test.teardown(); });
    
    global_suite.run_test("GlobalErrorHandler_Singleton_SameInstance", [&]() {
        ErrorHandler& handler1 = getGlobalErrorHandler();
        ErrorHandler& handler2 = getGlobalErrorHandler();
        
        // Should return the same instance
        ASSERT_EQ(&handler1, &handler2);
    });
    
    global_suite.run_test("ErrorHandlingMacros_WorkCorrectly", [&]() {
        auto listener = std::make_shared<ErrorHandlerTest::TestErrorListener>();
        getGlobalErrorHandler().addListener(listener);
        
        // Test PLC_HANDLE_ERROR macro
        PLC_HANDLE_ERROR(ErrorCode::SYSTEM_CONFIG_INVALID, "MacroTest", "Configuration error");
        
        ASSERT_EQ(1, listener->error_notifications.load());
        ASSERT_EQ(ErrorCode::SYSTEM_CONFIG_INVALID, listener->last_error_context.code);
        ASSERT_EQ("MacroTest", listener->last_error_context.component);
        ASSERT_EQ("Configuration error", listener->last_error_context.message);
        
        getGlobalErrorHandler().removeListener(listener);
    });
    
    runner.add_suite(std::move(global_suite));
    
    // Run all tests
    bool all_passed = runner.run_all();
    return all_passed ? 0 : 1;
}