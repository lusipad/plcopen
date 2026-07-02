#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "GroupLinearPlanner.h"

#include <limits>

using namespace plcopen;

TEST_CASE("GroupLinearPlanner maps one scalar profile onto a straight multi-axis path", "[planner][group][linear]")
{
    GroupLinearPlanner planner;
    REQUIRE(planner.setFrequency(100));

    MC_POS_REF start{};
    start.mCount = 3;
    start.mValues[0] = 0.0;
    start.mValues[1] = 0.0;
    start.mValues[2] = 5.0;

    MC_POS_REF target{};
    target.mCount = 3;
    target.mValues[0] = 3.0;
    target.mValues[1] = 4.0;
    target.mValues[2] = 5.0;

    REQUIRE(planner.plan(start, target, 2.0, 4.0, 4.0, 0.0));
    REQUIRE(planner.count() == 3);

    bool done = false;
    for (int cycle = 0; cycle < 1000 && !done; ++cycle)
    {
        done = planner.execute();
        REQUIRE(4.0 * planner.position(0) == Catch::Approx(3.0 * planner.position(1)).margin(1e-9));
        REQUIRE(planner.position(2) == Catch::Approx(5.0).margin(1e-9));
    }

    REQUIRE(done);
    REQUIRE(planner.position(0) == Catch::Approx(3.0).margin(1e-9));
    REQUIRE(planner.position(1) == Catch::Approx(4.0).margin(1e-9));
    REQUIRE(planner.position(2) == Catch::Approx(5.0).margin(1e-9));
    REQUIRE(planner.velocity(0) == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(planner.velocity(1) == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("GroupLinearPlanner completes a zero-distance path without moving", "[planner][group][linear][boundary]")
{
    GroupLinearPlanner planner;
    REQUIRE(planner.setFrequency(100));

    MC_POS_REF position{};
    position.mCount = 2;
    position.mValues[0] = 2.0;
    position.mValues[1] = -3.0;

    REQUIRE(planner.plan(position, position, 1.0, 2.0, 2.0, 0.0));
    REQUIRE(planner.execute());
    REQUIRE(planner.position(0) == Catch::Approx(2.0));
    REQUIRE(planner.position(1) == Catch::Approx(-3.0));
}

TEST_CASE("GroupLinearPlanner completes a very short mixed-direction path", "[planner][group][linear][boundary]")
{
    GroupLinearPlanner planner;
    REQUIRE(planner.setFrequency(1000));

    MC_POS_REF start{};
    start.mCount = 2;
    MC_POS_REF target = start;
    target.mValues[0] = 1e-9;
    target.mValues[1] = -2e-9;

    REQUIRE(planner.plan(start, target, 1.0, 2.0, 2.0, 8.0));
    bool done = false;
    for (int cycle = 0; cycle < 20 && !done; ++cycle)
        done = planner.execute();

    REQUIRE(done);
    REQUIRE(planner.position(0) == Catch::Approx(target.mValues[0]).margin(1e-15));
    REQUIRE(planner.position(1) == Catch::Approx(target.mValues[1]).margin(1e-15));
}

TEST_CASE("GroupLinearPlanner rejects invalid dimensions and dynamics", "[planner][group][linear][validation]")
{
    GroupLinearPlanner planner;
    REQUIRE(planner.setFrequency(100));

    MC_POS_REF start{};
    start.mCount = 2;
    MC_POS_REF target = start;
    target.mValues[0] = 1.0;

    SECTION("dimension mismatch")
    {
        target.mCount = 3;
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 2.0, 2.0, 0.0));
    }

    SECTION("too few axes")
    {
        start.mCount = target.mCount = 1;
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 2.0, 2.0, 0.0));
    }

    SECTION("non-positive dynamics")
    {
        REQUIRE_FALSE(planner.plan(start, target, 0.0, 2.0, 2.0, 0.0));
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 0.0, 2.0, 0.0));
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 2.0, 0.0, 0.0));
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 2.0, 2.0, -1.0));
    }

    SECTION("invalid start velocity")
    {
        REQUIRE_FALSE(planner.plan(start, target, -1.0, 1.0, 2.0, 2.0, 0.0));
        REQUIRE_FALSE(planner.plan(start, target, 2.0, 1.0, 2.0, 2.0, 0.0));
        REQUIRE_FALSE(planner.plan(start, start, 1.0, 1.0, 2.0, 2.0, 0.0));
    }

    SECTION("non-finite position")
    {
        target.mValues[1] = std::numeric_limits<double>::infinity();
        REQUIRE_FALSE(planner.plan(start, target, 1.0, 2.0, 2.0, 0.0));
    }
}
