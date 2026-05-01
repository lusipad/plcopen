#include "AxesGroup.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"
#include "follower_demo_support.h"

#include <iostream>

using namespace plcopen;

namespace
{
const char *groupStatusName(MC_GroupStatus status)
{
    switch (status)
    {
    case MC_GroupStatus::DISABLED:
        return "DISABLED";
    case MC_GroupStatus::STANDBY:
        return "STANDBY";
    case MC_GroupStatus::HOMING:
        return "HOMING";
    case MC_GroupStatus::MOVING:
        return "MOVING";
    case MC_GroupStatus::STOPPING:
        return "STOPPING";
    case MC_GroupStatus::ERRORSTOP:
        return "ERRORSTOP";
    }

    return "UNKNOWN";
}
} // namespace

int main(int argc, char **argv)
{
    const bool sleepEnabled = demo_support::shouldSleep(argc, argv);
    constexpr double frequency = 100.0;

    Scheduler sched;
    if (sched.setFrequency(frequency) != MC_ErrorCode::GOOD)
        return 1;

    Axis *master = sched.newAxis(1, new Servo());
    Axis *slave = sched.newAxis(2, new Servo());
    if (!master || !slave)
        return 1;

    AxesGroup group;

    FbPower masterPower;
    masterPower.mAxis = master;
    masterPower.mEnable = true;
    masterPower.mEnablePositive = true;
    masterPower.mEnableNegative = true;

    FbPower slavePower;
    slavePower.mAxis = slave;
    slavePower.mEnable = true;
    slavePower.mEnablePositive = true;
    slavePower.mEnableNegative = true;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = master;

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = slave;

    FbGroupEnable enable;
    enable.mAxesGroup = &group;

    FbGroupReadStatus readStatus;
    readStatus.mAxesGroup = &group;
    readStatus.mEnable = true;

    FbGroupDisable disable;
    disable.mAxesGroup = &group;

    std::cout << "axes-group demo: group=" << groupStatusName(group.status()) << '\n';

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        sched.runCycle();
        masterPower.call();
        slavePower.call();
        if (masterPower.mError || slavePower.mError)
            return 1;
        if (masterPower.mStatus && masterPower.mValid && slavePower.mStatus && slavePower.mValid)
            break;

        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    if (!(masterPower.mStatus && masterPower.mValid && slavePower.mStatus && slavePower.mValid))
        return 1;

    addMaster.mExecute = true;
    addMaster.call();
    if (!addMaster.mDone || addMaster.mError)
        return 1;
    addMaster.mExecute = false;
    addMaster.call();

    addSlave.mExecute = true;
    addSlave.call();
    if (!addSlave.mDone || addSlave.mError)
        return 1;
    addSlave.mExecute = false;
    addSlave.call();

    std::cout << "axes-group demo: members=" << group.memberCount() << '\n';

    readStatus.call();
    if (!readStatus.mValid || !readStatus.mDisabled)
        return 1;

    enable.mExecute = true;
    enable.call();
    if (!enable.mDone || enable.mError)
        return 1;
    enable.mExecute = false;
    enable.call();

    readStatus.call();
    if (!readStatus.mValid || !readStatus.mStandby)
        return 1;

    std::cout << "axes-group demo: group=" << groupStatusName(group.status()) << " after enable\n";

    disable.mExecute = true;
    disable.call();
    if (!disable.mDone || disable.mError)
        return 1;
    disable.mExecute = false;
    disable.call();

    readStatus.call();
    if (!readStatus.mValid || !readStatus.mDisabled)
        return 1;

    std::cout << "axes-group demo: group=" << groupStatusName(group.status()) << " after disable\n";
    return 0;
}
