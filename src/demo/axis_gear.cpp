#include "FbSingleAxis.h"
#include "Scheduler.h"
#include "follower_demo_support.h"

#include <cmath>
#include <iomanip>
#include <iostream>

using namespace plcopen;

int main(int argc, char **argv)
{
    const bool sleepEnabled = demo_support::shouldSleep(argc, argv);
    constexpr double frequency = 200.0;
    constexpr double ratio = 2.0;

    Scheduler sched;
    sched.setFrequency(frequency);

    auto *masterServo = new Servo();
    auto *gearServo = new demo_support::MappingFollowerServo(
        masterServo, [ratio](double masterPosition) { return masterPosition * ratio; });

    Axis *masterAxis = sched.newAxis(1, masterServo);
    Axis *gearAxis = sched.newAxis(2, gearServo);

    FbPower masterPower;
    masterPower.mAxis = masterAxis;
    masterPower.mEnable = true;
    masterPower.mEnablePositive = true;
    masterPower.mEnableNegative = true;

    FbPower gearPower;
    gearPower.mAxis = gearAxis;
    gearPower.mEnable = true;
    gearPower.mEnablePositive = true;
    gearPower.mEnableNegative = true;

    FbMoveAbsolute moveMaster;
    moveMaster.mAxis = masterAxis;
    moveMaster.mPosition = 2.0;
    moveMaster.mVelocity = 3.0;
    moveMaster.mAcceleration = 6.0;
    moveMaster.mDeceleration = 6.0;
    moveMaster.mJerk = 48.0;

    FbReadActualPosition readMasterPos;
    readMasterPos.mAxis = masterAxis;
    readMasterPos.mEnable = true;

    FbReadActualPosition readGearPos;
    readGearPos.mAxis = gearAxis;
    readGearPos.mEnable = true;

    std::cout << std::fixed << std::setprecision(4);

    for (int cycle = 0; cycle < 600; ++cycle)
    {
        sched.runCycle();

        masterPower.call();
        gearPower.call();
        moveMaster.call();
        readMasterPos.call();
        readGearPos.call();

        if ((masterPower.mStatus && masterPower.mValid) && (gearPower.mStatus && gearPower.mValid) && !moveMaster.mExecute)
            moveMaster.mExecute = true;

        if (moveMaster.mError || masterPower.mError || gearPower.mError)
            return 1;

        if (cycle % 40 == 0 || moveMaster.mDone)
        {
            std::cout << "gear demo: master=" << readMasterPos.mPosition << ", follower=" << readGearPos.mPosition << '\n';
        }

        if (moveMaster.mDone)
        {
            const bool converged = demo_support::waitForConvergence(
                80,
                [&]() {
                    sched.runCycle();
                    masterPower.call();
                    gearPower.call();
                    readMasterPos.call();
                    readGearPos.call();
                },
                [&]() { return std::fabs(readGearPos.mPosition - readMasterPos.mPosition * ratio) <= 1e-2; });
            return converged ? 0 : 1;
        }

        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    return 1;
}
