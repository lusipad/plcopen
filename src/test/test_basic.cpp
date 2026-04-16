/*
 * test_basic.cpp - Basic tests for PLCOpen library
 */

#include "FbSingleAxis.h"
#include "Scheduler.h"
#include <iostream>

using namespace plcopen;

class SimpleTest
{
public:
    static void assert_true(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cout << "FAIL: " << message << std::endl;
            failed_tests++;
        }
        else
        {
            std::cout << "PASS: " << message << std::endl;
            passed_tests++;
        }
        total_tests++;
    }

    static void print_summary()
    {
        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Total tests: " << total_tests << std::endl;
        std::cout << "Passed: " << passed_tests << std::endl;
        std::cout << "Failed: " << failed_tests << std::endl;
        std::cout << "Success rate: " << (100.0 * passed_tests / total_tests) << "%" << std::endl;
    }

    static int get_failed_count() { return failed_tests; }

    static int total_tests;
    static int passed_tests;
    static int failed_tests;
};

int SimpleTest::total_tests = 0;
int SimpleTest::passed_tests = 0;
int SimpleTest::failed_tests = 0;

void test_scheduler_basic()
{
    std::cout << "\n--- Testing Scheduler Basic Functions ---" << std::endl;
    
    Scheduler sched;
    
    MC_ErrorCode result = sched.setFrequency(1000.0);
    SimpleTest::assert_true(result == MC_ErrorCode::GOOD, "Setting normal frequency should succeed");
    
    double freq = sched.frequency();
    SimpleTest::assert_true(freq == 1000.0, "Frequency should be set correctly to 1000.0");
    
    result = sched.setFrequency(-100.0);
    SimpleTest::assert_true(result == MC_ErrorCode::FREQUENCY_ILLEGAL, "Negative frequency should be rejected");
    
    result = sched.setFrequency(0.0);
    SimpleTest::assert_true(result == MC_ErrorCode::FREQUENCY_ILLEGAL, "Zero frequency should be rejected");
}

void test_axis_creation()
{
    std::cout << "\n--- Testing Axis Creation ---" << std::endl;
    
    Scheduler sched;
    sched.setFrequency(100.0);
    
    Axis* axis1 = sched.newAxis(1, nullptr);
    SimpleTest::assert_true(axis1 != nullptr, "Should be able to create axis");
    
    Axis* retrieved_axis = sched.axis(1);
    SimpleTest::assert_true(retrieved_axis == axis1, "Should be able to retrieve axis by ID");
    
    Axis* axis2 = sched.newAxis(1, nullptr);
    SimpleTest::assert_true(axis2 == nullptr, "Duplicate axis ID should return nullptr");
    
    Axis* non_existent = sched.axis(999);
    SimpleTest::assert_true(non_existent == nullptr, "Non-existent axis ID should return nullptr");
    
    sched.release();
}

void test_type_definitions()
{
    std::cout << "\n--- Testing Type Definitions ---" << std::endl;
    
    // Test DWORD type size (fixed)
    if (sizeof(DWORD) == 4) {
        std::cout << "PASS: DWORD should be 32-bit (4 bytes)" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: DWORD should be 32-bit, but got " << sizeof(DWORD) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    // Test other basic types
    if (sizeof(BOOL) == 1) {
        std::cout << "PASS: BOOL should be 1 byte" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: BOOL should be 1 byte, but got " << sizeof(BOOL) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(WORD) == 2) {
        std::cout << "PASS: WORD should be 2 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: WORD should be 2 bytes, but got " << sizeof(WORD) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(UDINT) == 4) {
        std::cout << "PASS: UDINT should be 4 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: UDINT should be 4 bytes, but got " << sizeof(UDINT) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(ULINT) == 8) {
        std::cout << "PASS: ULINT should be 8 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: ULINT should be 8 bytes, but got " << sizeof(ULINT) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;

    std::cout << std::endl << "\n--- Testing New IEC 61131-3 Types ---" << std::endl;
    
    // Test newly added character types
    if (sizeof(CHAR) == 1) {
        std::cout << "PASS: CHAR should be 1 byte" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: CHAR should be 1 byte, but got " << sizeof(CHAR) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(WCHAR) == 2) {
        std::cout << "PASS: WCHAR should be 2 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: WCHAR should be 2 bytes, but got " << sizeof(WCHAR) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    // Test time types
    if (sizeof(TIME) == 4) {
        std::cout << "PASS: TIME should be 4 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: TIME should be 4 bytes, but got " << sizeof(TIME) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(LTIME) == 8) {
        std::cout << "PASS: LTIME should be 8 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: LTIME should be 8 bytes, but got " << sizeof(LTIME) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    // Test date types
    if (sizeof(DATE) == 4) {
        std::cout << "PASS: DATE should be 4 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: DATE should be 4 bytes, but got " << sizeof(DATE) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(LDATE) == 8) {
        std::cout << "PASS: LDATE should be 8 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: LDATE should be 8 bytes, but got " << sizeof(LDATE) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    // Test time and date types
    if (sizeof(TIME_OF_DAY) == 4) {
        std::cout << "PASS: TIME_OF_DAY should be 4 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: TIME_OF_DAY should be 4 bytes, but got " << sizeof(TIME_OF_DAY) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(TOD) == 4) {
        std::cout << "PASS: TOD should be 4 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: TOD should be 4 bytes, but got " << sizeof(TOD) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(DATE_AND_TIME) == 8) {
        std::cout << "PASS: DATE_AND_TIME should be 8 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: DATE_AND_TIME should be 8 bytes, but got " << sizeof(DATE_AND_TIME) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
    
    if (sizeof(DT) == 8) {
        std::cout << "PASS: DT should be 8 bytes" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: DT should be 8 bytes, but got " << sizeof(DT) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;

    // Test string pointer types
    if (sizeof(WSTRING) == sizeof(void*)) {
        std::cout << "PASS: WSTRING should be pointer size (" << sizeof(void*) << " bytes)" << std::endl;
        SimpleTest::passed_tests++;
    } else {
        std::cout << "FAIL: WSTRING should be pointer size, but got " << sizeof(WSTRING) << " bytes" << std::endl;
        SimpleTest::failed_tests++;
    }
    SimpleTest::total_tests++;
}

int main()
{
    std::cout << "Starting PLCOpen library basic tests..." << std::endl;
    
    test_scheduler_basic();
    test_axis_creation();
    test_type_definitions();
    
    SimpleTest::print_summary();
    
    return (SimpleTest::get_failed_count() > 0) ? 1 : 0;
} 