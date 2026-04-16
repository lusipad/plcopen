#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ProfilePlanner.h"

#include <cmath>

using namespace plcopen;

namespace
{
struct PlannerRunResult
{
    double finalPosition = 0.0;
    double finalVelocity = 0.0;
    double maxAbsVelocity = 0.0;
    int finalStatus = 0;
};

PlannerRunResult executePlanner(ProfilePlanner& planner, int maxSteps = 20000)
{
    PlannerRunResult result;

    for (int step = 0; step < maxSteps; ++step)
    {
        const bool done = planner.execute();
        result.finalPosition = planner.getPosition();
        result.finalVelocity = planner.getVelocity();
        result.maxAbsVelocity = std::max(result.maxAbsVelocity, std::fabs(result.finalVelocity));
        result.finalStatus = planner.readStatus();

        REQUIRE(std::isfinite(result.finalPosition));
        REQUIRE(std::isfinite(result.finalVelocity));
        REQUIRE(std::isfinite(planner.getAcceleration()));

        if (done)
            return result;
    }

    FAIL("planner did not finish within the expected number of steps");
}
}

TEST_CASE("ProfilePlanner rejects zero cruise velocity input", "[planner][boundary]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE_FALSE(planner.plan(0.0, 10.0, 0.0, 0.0, 0.0, 5.0, 5.0));
}

TEST_CASE("ProfilePlanner handles very short moves by reducing peak velocity", "[planner][boundary]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE(planner.plan(0.0, 0.001, 0.0, 10.0, 0.0, 20.0, 20.0));

    const PlannerRunResult result = executePlanner(planner);

    REQUIRE(result.finalPosition == Catch::Approx(0.001).margin(1e-6));
    REQUIRE(result.finalVelocity == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(result.maxAbsVelocity < 10.0);
}

TEST_CASE("ProfilePlanner rejects unreachable end velocity requests", "[planner][boundary]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE_FALSE(planner.plan(0.0, 1.0, 0.0, 10.0, 10.0, 1.0, 1.0));
}

TEST_CASE("ProfilePlanner stays finite under very high acceleration inputs", "[planner][boundary]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE(planner.plan(0.0, 1.0, 0.0, 100.0, 0.0, 1000000.0, 1000000.0));

    const PlannerRunResult result = executePlanner(planner);

    REQUIRE(result.finalPosition == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(result.finalVelocity == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(result.finalStatus == 0);
}
