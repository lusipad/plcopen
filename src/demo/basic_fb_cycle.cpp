#include "FbBasic.h"
#include "follower_demo_support.h"

#include <iostream>

using namespace plcopen;

int main(int argc, char **argv)
{
    const bool sleepEnabled = demo_support::shouldSleep(argc, argv);
    constexpr double frequency = 100.0;
    constexpr TIME cycleTime = 10;

    FbTon ton;
    FbCtu ctu;

    if (!ton.setCycleTime(cycleTime))
        return 1;

    ton.mPT = 30;
    ctu.mPV = 3;

    for (int cycle = 0; cycle < 6; ++cycle)
    {
        ton.mIN = (cycle >= 1);
        ctu.mCU = (cycle == 1 || cycle == 3 || cycle == 5);

        ton.call();
        ctu.call();

        std::cout << "basic demo: cycle=" << cycle << ", TON.Q=" << ton.mQ << ", TON.ET=" << ton.mET
                  << ", CTU.CV=" << ctu.mCV << ", CTU.Q=" << ctu.mQ << '\n';

        demo_support::maybeSleep(sleepEnabled, frequency);
    }

    return (ton.mQ && ton.mET == ton.mPT && ctu.mCV == 3 && ctu.mQ) ? 0 : 1;
}
