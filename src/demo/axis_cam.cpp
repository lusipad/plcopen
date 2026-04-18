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

    demo_support::LinearCamTable camTable({{0.0, 0.0}, {1.0, 0.2}, {2.0, 1.0}, {3.0, 1.5}});

    Scheduler sched;
    sched.setFrequency(frequency);

    auto *masterServo = new Servo();
    auto *camServo = new demo_support::MappingFollowerServo(
        masterServo, [&camTable](double masterPosition) { return camTable.sample(masterPosition); });

    Axis *masterAxis = sched.newAxis(1, masterServo);
    Axis *camAxis = sched.newAxis(2, camServo);

    FbPower masterPower;
    masterPower.mAxis = masterAxis;
    masterPower.mEnable = true;
    masterPower.mEnablePositive = true;
    masterPower.mEnableNegative = true;

    FbPower camPower;
    camPower.mAxis = camAxis;
    camPower.mEnable = true;
    camPower.mEnablePositive = true;
    camPower.mEnableNegative = true;

    FbMoveAbsolute moveMaster;
    moveMaster.mAxis = masterAxis;
    moveMaster.mPosition = 3.0;
    moveMaster.mVelocity = 3.0;
    moveMaster.mAcceleration = 6.0;
    moveMaster.mDeceleration = 6.0;
    moveMaster.mJerk = 48.0;

    FbReadActualPosition readMasterPos;
    readMasterPos.mAxis = masterAxis;
    readMasterPos.mEnable = true;

    FbReadActualPosition readCamPos;
    readCamPos.mAxis = camAxis;
    readCamPos.mEnable = true;

    std::cout << std::fixed << std::setprecision(4);

    for (int cycle = 0; cycle < 600; ++cycle)
    {
        sched.runCycle();

        masterPower.call();
        camPower.call();
        moveMaster.call();
        readMasterPos.call();
        readCamPos.call();

        if ((masterPower.mStatus && masterPower.mValid) && (camPower.mStatus && camPower.mValid) && !moveMaster.mExecute)
            moveMaster.mExecute = true;

        if (moveMaster.mError || masterPower.mError || camPower.mError)
            return 1;

        if (cycle % 40 == 0 || moveMaster.mDone)
        {
            std::cout << "cam demo: master=" << readMasterPos.mPosition << ", follower=" << readCamPos.mPosition << '\n';
        }

        if (moveMaster.mDone)
        {
            const double diff = std::fabs(readCamPos.mPosition - camTable.sample(readMasterPos.mPosition));
            return diff <= 1e-2 ? 0 : 1;
        }

        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    return 1;
}
