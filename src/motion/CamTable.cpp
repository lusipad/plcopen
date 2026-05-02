#include "CamTable.h"

#include <algorithm>
#include <cmath>

namespace plcopen
{

    void CamTable::addPoint(double masterPosition, double slavePosition)
    {
        mPoints.emplace_back(masterPosition, slavePosition);
        std::sort(mPoints.begin(), mPoints.end(), [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
    }

    void CamTable::setPeriodic(bool periodic)
    {
        mPeriodic = periodic;
    }

    bool CamTable::periodic(void) const
    {
        return mPeriodic;
    }

    bool CamTable::empty(void) const
    {
        return mPoints.empty();
    }

    bool CamTable::valid(void) const
    {
        for (size_t i = 0; i < mPoints.size(); ++i)
        {
            const auto &point = mPoints[i];
            if (!std::isfinite(point.first) || !std::isfinite(point.second))
                return false;

            if (i > 0 && point.first <= mPoints[i - 1].first)
                return false;
        }

        return true;
    }

    double CamTable::sample(double masterPosition) const
    {
        if (mPoints.empty())
            return 0.0;

        if (mPeriodic && mPoints.size() >= 2)
        {
            const double firstMaster = mPoints.front().first;
            const double lastMaster = mPoints.back().first;
            const double period = lastMaster - firstMaster;
            if (period > 0.0)
            {
                masterPosition = std::fmod(masterPosition - firstMaster, period);
                if (masterPosition < 0.0)
                    masterPosition += period;
                masterPosition += firstMaster;
            }
        }

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

} // namespace plcopen
