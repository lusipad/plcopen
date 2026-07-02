#include "AxesGroup.h"
#include "Axis.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

#include <cmath>

int main()
{
    using namespace plcopen;

    Scheduler scheduler;
    if (scheduler.setFrequency(100.0) != MC_ErrorCode::GOOD)
        return 1;
    Axis *x = scheduler.newAxis(1, nullptr);
    Axis *y = scheduler.newAxis(2, nullptr);
    if (!x || !y)
        return 2;

    FbPower xPower;
    xPower.mAxis = x;
    xPower.mEnable = xPower.mEnablePositive = xPower.mEnableNegative = true;
    FbPower yPower;
    yPower.mAxis = y;
    yPower.mEnable = yPower.mEnablePositive = yPower.mEnableNegative = true;
    for (int cycle = 0; cycle < 20 && !(xPower.mStatus && yPower.mStatus); ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
    }

    AxesGroup group;
    if (!(xPower.mStatus && yPower.mStatus) || group.addAxis(x) != MC_ErrorCode::GOOD ||
        group.addAxis(y) != MC_ErrorCode::GOOD || group.enable() != MC_ErrorCode::GOOD)
        return 3;

    FbMoveLinearAbsolute move;
    move.mAxesGroup = &group;
    move.mPosition.mCount = 2;
    move.mPosition.mValues[0] = 3.0;
    move.mPosition.mValues[1] = 4.0;
    move.mVelocity = 2.0;
    move.mAcceleration = move.mDeceleration = 4.0;
    move.mExecute = true;
    move.call();
    for (int cycle = 0; cycle < 1000 && !move.mDone; ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
        move.call();
    }

    return move.mDone && !move.mError && std::fabs(x->cmdPosition() - 3.0) < 1e-8 &&
                   std::fabs(y->cmdPosition() - 4.0) < 1e-8
               ? 0
               : 4;
}
