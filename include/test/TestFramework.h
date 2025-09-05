/**
 * @file TestFramework.h
 * @brief Lightweight TDD Test Framework for PLC Runtime
 * @version MVP-1.0
 * @date 2025-09-04
 */

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <exception>
#include <chrono>
#include <type_traits>

namespace plc_test {

/**
 * @brief Helper function for converting values to string in assertions
 */
template<typename T>
std::string to_assert_string(const T& value) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return "\"" + value + "\"";
    } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
        return std::string("\"") + value + "\"";
    } else {
        return "<complex_type>";
    }
}

/**
 * @brief Test result structure
 */
struct TestResult {
    std::string test_name;
    bool passed;
    std::string error_message;
    std::chrono::milliseconds execution_time;
    
    TestResult(const std::string& name) 
        : test_name(name), passed(false), execution_time(0) {}
};

/**
 * @brief Test suite class
 */
class TestSuite {
private:
    std::string suite_name_;
    std::vector<TestResult> results_;
    std::vector<std::function<void()>> setup_functions_;
    std::vector<std::function<void()>> teardown_functions_;
    
public:
    explicit TestSuite(const std::string& name) : suite_name_(name) {}
    
    void add_setup(std::function<void()> setup_fn) {
        setup_functions_.push_back(setup_fn);
    }
    
    void add_teardown(std::function<void()> teardown_fn) {
        teardown_functions_.push_back(teardown_fn);
    }
    
    void run_test(const std::string& test_name, std::function<void()> test_fn) {
        TestResult result(test_name);
        
        try {
            // Setup
            for (auto& setup : setup_functions_) {
                setup();
            }
            
            // Execute test with timing
            auto start = std::chrono::steady_clock::now();
            test_fn();
            auto end = std::chrono::steady_clock::now();
            
            result.execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            result.passed = true;
            
            // Teardown
            for (auto& teardown : teardown_functions_) {
                teardown();
            }
            
        } catch (const std::exception& e) {
            result.passed = false;
            result.error_message = e.what();
        } catch (...) {
            result.passed = false;
            result.error_message = "Unknown exception";
        }
        
        results_.push_back(result);
        
        // Print immediate result
        std::cout << "  " << (result.passed ? "[PASS]" : "[FAIL]") 
                  << " " << test_name;
        if (!result.passed) {
            std::cout << " - " << result.error_message;
        }
        std::cout << " (" << result.execution_time.count() << "ms)" << std::endl;
    }
    
    void print_summary() {
        int passed = 0;
        int failed = 0;
        std::chrono::milliseconds total_time(0);
        
        for (const auto& result : results_) {
            if (result.passed) {
                passed++;
            } else {
                failed++;
            }
            total_time += result.execution_time;
        }
        
        std::cout << "\n=== " << suite_name_ << " Test Suite Summary ===" << std::endl;
        std::cout << "Total: " << results_.size() << " tests" << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Total time: " << total_time.count() << "ms" << std::endl;
        
        if (failed > 0) {
            std::cout << "\nFailed tests:" << std::endl;
            for (const auto& result : results_) {
                if (!result.passed) {
                    std::cout << "  - " << result.test_name << ": " << result.error_message << std::endl;
                }
            }
        }
        
        std::cout << std::endl;
    }
    
    bool all_passed() const {
        for (const auto& result : results_) {
            if (!result.passed) {
                return false;
            }
        }
        return true;
    }
    
    const std::vector<TestResult>& get_results() const {
        return results_;
    }
};

/**
 * @brief Assertion macros
 */
#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        throw std::runtime_error(std::string("Assertion failed: " #condition " at line ") + std::to_string(__LINE__)); \
    }

#define ASSERT_FALSE(condition) \
    if (condition) { \
        throw std::runtime_error(std::string("Assertion failed: !(" #condition ") at line ") + std::to_string(__LINE__)); \
    }

#define ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        throw std::runtime_error("Assertion failed: expected " + plc_test::to_assert_string(expected) + " but got " + plc_test::to_assert_string(actual) + " at line " + std::to_string(__LINE__)); \
    }

#define ASSERT_NE(expected, actual) \
    if ((expected) == (actual)) { \
        throw std::runtime_error("Assertion failed: expected different values but both are " + plc_test::to_assert_string(expected) + " at line " + std::to_string(__LINE__)); \
    }

#define ASSERT_NULL(ptr) \
    if ((ptr) != nullptr) { \
        throw std::runtime_error(std::string("Assertion failed: expected nullptr but got valid pointer at line ") + std::to_string(__LINE__)); \
    }

#define ASSERT_NOT_NULL(ptr) \
    if ((ptr) == nullptr) { \
        throw std::runtime_error(std::string("Assertion failed: expected valid pointer but got nullptr at line ") + std::to_string(__LINE__)); \
    }

#define ASSERT_THROWS(statement) \
    { \
        bool threw_exception = false; \
        try { \
            statement; \
        } catch (...) { \
            threw_exception = true; \
        } \
        if (!threw_exception) { \
            throw std::runtime_error(std::string("Assertion failed: expected exception but none was thrown at line ") + std::to_string(__LINE__)); \
        } \
    }

/**
 * @brief Test runner for multiple suites
 */
class TestRunner {
private:
    std::vector<TestSuite> suites_;
    
public:
    void add_suite(TestSuite suite) {
        suites_.push_back(std::move(suite));
    }
    
    bool run_all() {
        std::cout << "=== PLC Runtime TDD Test Suite ===" << std::endl;
        std::cout << "Running " << suites_.size() << " test suites..." << std::endl << std::endl;
        
        bool all_passed = true;
        int total_tests = 0;
        int total_passed = 0;
        int total_failed = 0;
        
        for (auto& suite : suites_) {
            suite.print_summary();
            
            if (!suite.all_passed()) {
                all_passed = false;
            }
            
            for (const auto& result : suite.get_results()) {
                total_tests++;
                if (result.passed) {
                    total_passed++;
                } else {
                    total_failed++;
                }
            }
        }
        
        std::cout << "=== Final Summary ===" << std::endl;
        std::cout << "Total suites: " << suites_.size() << std::endl;
        std::cout << "Total tests: " << total_tests << std::endl;
        std::cout << "Total passed: " << total_passed << std::endl;
        std::cout << "Total failed: " << total_failed << std::endl;
        std::cout << "Success rate: " << (total_tests > 0 ? (total_passed * 100 / total_tests) : 0) << "%" << std::endl;
        
        if (all_passed) {
            std::cout << "\nALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "\nSOME TESTS FAILED!" << std::endl;
        }
        
        return all_passed;
    }
};

} // namespace plc_test