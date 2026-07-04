#include "AxesGroup.h"
#include "Axis.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace plcopen;

namespace
{
constexpr double kFrequency = 100.0;
constexpr int kPowerCycles = 20;
constexpr int kMaxMoveCycles = 1000;

void writeSample(std::ostream &out, int tick, int axisIndex, const Axis *axis)
{
    out << "{\"tick\":" << tick << ",\"axis\":" << axisIndex << ",\"position\":" << axis->cmdPosition()
        << ",\"velocity\":" << axis->cmdVelocity() << ",\"acceleration\":" << axis->cmdAcceleration()
        << ",\"source\":\"golden-replay-recorder-smoke\"}\n";
}

void writePair(std::ostream &out, int tick, const Axis *x, const Axis *y)
{
    writeSample(out, tick, 0, x);
    writeSample(out, tick, 1, y);
}
} // namespace

int main(int argc, char **argv)
{
    std::string outputPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc)
        {
            outputPath = argv[++i];
        }
        else if (arg == "--help")
        {
            std::cout << "usage: golden_replay_recorder [--output path]\n";
            return 0;
        }
        else
        {
            std::cerr << "unknown argument: " << arg << '\n';
            return 2;
        }
    }

    std::ofstream outputFile;
    std::ostream *out = &std::cout;
    if (!outputPath.empty())
    {
        outputFile.open(outputPath);
        if (!outputFile)
        {
            std::cerr << "failed to open output: " << outputPath << '\n';
            return 1;
        }
        out = &outputFile;
    }
    *out << std::fixed << std::setprecision(9);

    Scheduler scheduler;
    if (scheduler.setFrequency(kFrequency) != MC_ErrorCode::GOOD)
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

    int sampleTick = 0;
    for (int cycle = 0; cycle < kPowerCycles && !(xPower.mStatus && yPower.mStatus); ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
        writePair(*out, sampleTick++, x, y);
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
    writePair(*out, sampleTick++, x, y);

    if (!move.mCommandAccepted || move.mCommandID == 0 || move.mError)
        return 1;

    for (int cycle = 0; cycle < kMaxMoveCycles && !move.mDone; ++cycle)
    {
        scheduler.runCycle();
        xPower.call();
        yPower.call();
        move.call();
        writePair(*out, sampleTick++, x, y);
    }

    return (!move.mDone || move.mError) ? 1 : 0;
}
