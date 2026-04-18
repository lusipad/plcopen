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
    double maxAccelerationDelta = 0.0;
    double firstAcceleration = 0.0;
    int finalStatus = 0;
};

PlannerRunResult executePlanner(ProfilePlanner& planner, int maxSteps = 20000)
{
    PlannerRunResult result;
    bool capturedFirstAcceleration = false;
    double previousAcceleration = planner.getAcceleration();

    for (int step = 0; step < maxSteps; ++step)
    {
        const bool done = planner.execute();
        result.finalPosition = planner.getPosition();
        result.finalVelocity = planner.getVelocity();
        result.maxAbsVelocity = std::max(result.maxAbsVelocity, std::fabs(result.finalVelocity));
        result.finalStatus = planner.readStatus();

        const double currentAcceleration = planner.getAcceleration();
        if (!capturedFirstAcceleration)
        {
            result.firstAcceleration = currentAcceleration;
            capturedFirstAcceleration = true;
        }
        result.maxAccelerationDelta = std::max(result.maxAccelerationDelta, std::fabs(currentAcceleration - previousAcceleration));
        previousAcceleration = currentAcceleration;

        REQUIRE(std::isfinite(result.finalPosition));
        REQUIRE(std::isfinite(result.finalVelocity));
        REQUIRE(std::isfinite(currentAcceleration));

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

TEST_CASE("ProfilePlanner builds a jerk-limited path when jerk is provided", "[planner][jerk]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE(planner.plan(0.0, 1.0, 0.0, 2.0, 0.0, 8.0, 8.0, 64.0));

    const PlannerRunResult result = executePlanner(planner);

    REQUIRE(result.finalPosition == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(result.finalVelocity == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(result.firstAcceleration < 8.0);
    REQUIRE(result.maxAccelerationDelta < 8.0);
}

TEST_CASE("ProfilePlanner falls back to the legacy path when jerk is zero", "[planner][jerk]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE(planner.plan(0.0, 1.0, 0.0, 2.0, 0.0, 8.0, 8.0, 0.0));

    const PlannerRunResult result = executePlanner(planner);

    REQUIRE(result.finalPosition == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(result.finalVelocity == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(result.firstAcceleration == Catch::Approx(8.0).margin(1e-6));
}

TEST_CASE("ProfilePlanner keeps advancing after a jerk-limited ramp reaches a non-zero end velocity", "[planner][jerk]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(1000));
    REQUIRE(planner.plan(0.0, 1.0, 0.0, 2.0, 1.0, 8.0, 8.0, 64.0));

    const PlannerRunResult result = executePlanner(planner);
    const double positionAtDone = result.finalPosition;

    REQUIRE(planner.execute());
    REQUIRE(planner.getVelocity() == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(planner.getPosition() > positionAtDone);
}

TEST_CASE("ProfilePlanner can brake to zero with jerk-limited deceleration", "[planner][jerk]")
{
    ProfilePlanner planner;
    REQUIRE(planner.setFrequency(100));

    const double brakingDistance = ProfilePlanner::calculateDist(1.0, 0.0, 4.0, 4.0, 32.0);
    REQUIRE(std::isfinite(brakingDistance));
    REQUIRE(planner.plan(0.0, brakingDistance, 1.0, 1.0, 0.0, 4.0, 4.0, 32.0));

    const PlannerRunResult result = executePlanner(planner);

    REQUIRE(result.finalPosition == Catch::Approx(brakingDistance).margin(1e-6));
    REQUIRE(result.finalVelocity == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(result.finalStatus == 0);
}
