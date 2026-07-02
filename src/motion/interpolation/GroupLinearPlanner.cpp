#include "GroupLinearPlanner.h"

#include <cmath>

namespace plcopen
{

bool GroupLinearPlanner::setFrequency(uint32_t frequency)
{
    return mPathPlanner.setFrequency(frequency);
}

bool GroupLinearPlanner::plan(
    const MC_POS_REF &start,
    const MC_POS_REF &target,
    double velocity,
    double acceleration,
    double deceleration,
    double jerk)
{
    return plan(start, target, 0.0, velocity, acceleration, deceleration, jerk);
}

bool GroupLinearPlanner::plan(
    const MC_POS_REF &start,
    const MC_POS_REF &target,
    double startVelocity,
    double velocity,
    double acceleration,
    double deceleration,
    double jerk)
{
    if (start.mCount < 2 || start.mCount > PLCOPEN_AXESGROUP_IDENT_NUM || start.mCount != target.mCount)
        return false;

    if (!std::isfinite(startVelocity) || !std::isfinite(velocity) || !std::isfinite(acceleration) ||
        !std::isfinite(deceleration) || !std::isfinite(jerk) || startVelocity < 0.0 || velocity <= 0.0 ||
        acceleration <= 0.0 || deceleration <= 0.0 || jerk < 0.0 || startVelocity > velocity)
        return false;

    double delta[PLCOPEN_AXESGROUP_IDENT_NUM] = {0};
    double pathLengthSquared = 0.0;
    for (std::size_t index = 0; index < start.mCount; ++index)
    {
        if (!std::isfinite(start.mValues[index]) || !std::isfinite(target.mValues[index]))
            return false;

        delta[index] = target.mValues[index] - start.mValues[index];
        pathLengthSquared += delta[index] * delta[index];
    }

    const double pathLength = std::sqrt(pathLengthSquared);
    if (!std::isfinite(pathLength))
        return false;

    mCount = start.mCount;
    for (std::size_t index = 0; index < mCount; ++index)
    {
        mStart[index] = start.mValues[index];
        mDirection[index] = pathLength > 0.0 ? delta[index] / pathLength : 0.0;
    }

    if (pathLength == 0.0)
    {
        if (startVelocity > 0.0)
            return false;
        mComplete = true;
        return true;
    }

    if (!mPathPlanner.plan(0.0, pathLength, startVelocity, velocity, 0.0, acceleration, deceleration, jerk))
        return false;

    mComplete = false;
    return true;
}

bool GroupLinearPlanner::execute(void)
{
    if (!mComplete)
        mComplete = mPathPlanner.execute();
    return mComplete;
}

std::size_t GroupLinearPlanner::count(void) const
{
    return mCount;
}

double GroupLinearPlanner::position(std::size_t index)
{
    return mStart[index] + mDirection[index] * mPathPlanner.getPosition();
}

double GroupLinearPlanner::velocity(std::size_t index)
{
    return mDirection[index] * mPathPlanner.getVelocity();
}

double GroupLinearPlanner::acceleration(std::size_t index)
{
    return mDirection[index] * mPathPlanner.getAcceleration();
}

} // namespace plcopen
