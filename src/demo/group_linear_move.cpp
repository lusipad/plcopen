#include "AxesGroup.h"
#include "Axis.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"
#include "follower_demo_support.h"

#include <cmath>
#include <iostream>

using namespace plcopen;

int main(int argc, char **argv)
{
    const bool sleepEnabled = demo_support::shouldSleep(argc, argv);
    constexpr double frequency = 100.0;

    Scheduler scheduler;
    if (scheduler.setFrequency(frequency) != MC_ErrorCode::GOOD)
        return 1;

    Axis *x = scheduler.newAxis(1, new Servo());
    Axis *y = scheduler.newAxis(2, new Servo());
    if (!x || !y)
        return 1;

    FbPower xPower;
    xPower.mAxis = x;
    xPower.mEnable = true;
    xPower.mEnablePositive = true;
    xPower.mEnableNegative = true;

    FbPower yPower;
    yPower.mAxis = y;
    yPower.mEnable = true;
    yPower.mEnablePositive = true;
    yPower.mEnableNegative = true;

    for (int cycle = 0; cycle < 20 && !(xPower.mStatus && yPower.mStatus); ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
        demo_support::maybeSleep(sleepEnabled, frequency);
    }
    if (!(xPower.mStatus && yPower.mStatus))
        return 1;

    AxesGroup group;
    if (group.addAxis(x) != MC_ErrorCode::GOOD || group.addAxis(y) != MC_ErrorCode::GOOD ||
        group.enable() != MC_ErrorCode::GOOD)
        return 1;

    FbMoveLinearAbsolute move;
    move.mAxesGroup = &group;
    move.mPosition.mCount = 2;
    move.mPosition.mValues[0] = 3.0;
    move.mPosition.mValues[1] = 4.0;
    move.mVelocity = 2.0;
    move.mAcceleration = 4.0;
    move.mDeceleration = 4.0;
    move.mExecute = true;
    move.call();
    if (!move.mCommandAccepted || move.mCommandID == 0 || move.mError)
        return 1;

    for (int cycle = 0; cycle < 1000 && !move.mDone; ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
        move.call();
        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    if (!move.mDone || move.mError || std::fabs(x->cmdPosition() - 3.0) > 1e-8 ||
        std::fabs(y->cmdPosition() - 4.0) > 1e-8)
        return 1;

    std::cout << "group linear move: (" << x->cmdPosition() << ", " << y->cmdPosition() << ")\n";
    return 0;
}
