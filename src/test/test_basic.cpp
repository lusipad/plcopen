/*
 * test_basic.cpp - Basic tests for PLCOpen library
 */

#include <catch2/catch_test_macros.hpp>
#include "FbSingleAxis.h"
#include "Scheduler.h"

using namespace plcopen;

TEST_CASE("Scheduler validates frequency input", "[scheduler]")
{
    Scheduler sched;

    REQUIRE(sched.setFrequency(1000.0) == MC_ErrorCode::GOOD);
    REQUIRE(sched.frequency() == 1000.0);
    REQUIRE(sched.setFrequency(-100.0) == MC_ErrorCode::FREQUENCY_ILLEGAL);
    REQUIRE(sched.setFrequency(0.0) == MC_ErrorCode::FREQUENCY_ILLEGAL);
}

TEST_CASE("Scheduler manages axis instances by id", "[scheduler][axis]")
{
    Scheduler sched;
    REQUIRE(sched.setFrequency(100.0) == MC_ErrorCode::GOOD);

    Axis* axis1 = sched.newAxis(1, nullptr);
    REQUIRE(axis1 != nullptr);

    Axis* retrieved_axis = sched.axis(1);
    REQUIRE(retrieved_axis == axis1);

    Axis* axis2 = sched.newAxis(1, nullptr);
    REQUIRE(axis2 == nullptr);

    Axis* non_existent = sched.axis(999);
    REQUIRE(non_existent == nullptr);

    sched.release();
}

TEST_CASE("PLCOpen base types keep their expected sizes", "[types]")
{
    REQUIRE(sizeof(DWORD) == 4);
    REQUIRE(sizeof(BOOL) == 1);
    REQUIRE(sizeof(WORD) == 2);
    REQUIRE(sizeof(UDINT) == 4);
    REQUIRE(sizeof(ULINT) == 8);
    REQUIRE(sizeof(CHAR) == 1);
    REQUIRE(sizeof(WCHAR) == 2);
    REQUIRE(sizeof(TIME) == 4);
    REQUIRE(sizeof(LTIME) == 8);
    REQUIRE(sizeof(DATE) == 4);
    REQUIRE(sizeof(LDATE) == 8);
    REQUIRE(sizeof(TIME_OF_DAY) == 4);
    REQUIRE(sizeof(TOD) == 4);
    REQUIRE(sizeof(DATE_AND_TIME) == 8);
    REQUIRE(sizeof(DT) == 8);
    REQUIRE(sizeof(WSTRING) == sizeof(void*));
}
