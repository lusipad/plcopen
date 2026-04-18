#ifndef PLCOPEN_DEMO_FOLLOWER_SUPPORT_H
#define PLCOPEN_DEMO_FOLLOWER_SUPPORT_H

#include "Servo.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace demo_support
{
constexpr double kEncoderScale = 8192.0;

inline int32_t toEncoder(double position)
{
    return static_cast<int32_t>(std::lround(position * kEncoderScale));
}

inline double fromEncoder(int32_t position)
{
    return static_cast<double>(position) / kEncoderScale;
}

inline bool shouldSleep(int argc, char **argv)
{
    return !(argc > 1 && std::string_view(argv[1]) == "--no-sleep");
}

inline void maybeSleep(bool enabled, double frequency)
{
    if (!enabled)
        return;

    std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(1000000.0 / frequency)));
}

class MappingFollowerServo : public plcopen::Servo
{
  public:
    using MappingFn = std::function<double(double)>;

    MappingFollowerServo(plcopen::Servo *masterServo, MappingFn mapping)
        : mMasterServo(masterServo), mMapping(std::move(mapping))
    {
    }

  protected:
    void runCycle(double frequency) override
    {
        const double masterPosition = fromEncoder(mMasterServo->pos());
        setPos(toEncoder(mMapping(masterPosition)));
        Servo::runCycle(frequency);
    }

  private:
    plcopen::Servo *mMasterServo;
    MappingFn mMapping;
};

class LinearCamTable
{
  public:
    explicit LinearCamTable(std::vector<std::pair<double, double>> points) : mPoints(std::move(points)) {}

    double sample(double masterPosition) const
    {
        if (mPoints.empty())
            return 0.0;

        if (masterPosition <= mPoints.front().first)
            return mPoints.front().second;

        for (size_t i = 1; i < mPoints.size(); ++i)
        {
            const auto &prev = mPoints[i - 1];
            const auto &next = mPoints[i];
            if (masterPosition <= next.first)
            {
                const double span = next.first - prev.first;
                if (span <= 0.0)
                    return next.second;

                const double ratio = (masterPosition - prev.first) / span;
                return prev.second + (next.second - prev.second) * ratio;
            }
        }

        return mPoints.back().second;
    }

  private:
    std::vector<std::pair<double, double>> mPoints;
};
} // namespace demo_support

#endif
