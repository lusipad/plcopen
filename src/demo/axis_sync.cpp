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

    Scheduler sched;
    sched.setFrequency(frequency);

    auto *masterServo = new Servo();
    auto *syncServo = new demo_support::MappingFollowerServo(masterServo, [](double masterPosition) { return masterPosition; });

    Axis *masterAxis = sched.newAxis(1, masterServo);
    Axis *syncAxis = sched.newAxis(2, syncServo);

    FbPower masterPower;
    masterPower.mAxis = masterAxis;
    masterPower.mEnable = true;
    masterPower.mEnablePositive = true;
    masterPower.mEnableNegative = true;

    FbPower syncPower;
    syncPower.mAxis = syncAxis;
    syncPower.mEnable = true;
    syncPower.mEnablePositive = true;
    syncPower.mEnableNegative = true;

    FbMoveAbsolute moveMaster;
    moveMaster.mAxis = masterAxis;
    moveMaster.mPosition = 3.0;
    moveMaster.mVelocity = 4.0;
    moveMaster.mAcceleration = 8.0;
    moveMaster.mDeceleration = 8.0;
    moveMaster.mJerk = 64.0;

    FbReadActualPosition readMasterPos;
    readMasterPos.mAxis = masterAxis;
    readMasterPos.mEnable = true;

    FbReadActualPosition readSyncPos;
    readSyncPos.mAxis = syncAxis;
    readSyncPos.mEnable = true;

    std::cout << std::fixed << std::setprecision(4);

    for (int cycle = 0; cycle < 600; ++cycle)
    {
        sched.runCycle();

        masterPower.call();
        syncPower.call();
        moveMaster.call();
        readMasterPos.call();
        readSyncPos.call();

        if ((masterPower.mStatus && masterPower.mValid) && (syncPower.mStatus && syncPower.mValid) && !moveMaster.mExecute)
            moveMaster.mExecute = true;

        if (moveMaster.mError || masterPower.mError || syncPower.mError)
            return 1;

        if (cycle % 40 == 0 || moveMaster.mDone)
        {
            std::cout << "sync demo: master=" << readMasterPos.mPosition << ", follower=" << readSyncPos.mPosition << '\n';
        }

        if (moveMaster.mDone)
        {
            const bool converged = demo_support::waitForConvergence(
                80,
                [&]() {
                    sched.runCycle();
                    masterPower.call();
                    syncPower.call();
                    readMasterPos.call();
                    readSyncPos.call();
                },
                [&]() { return std::fabs(readMasterPos.mPosition - readSyncPos.mPosition) <= 1e-2; });
            return converged ? 0 : 1;
        }

        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    return 1;
}
