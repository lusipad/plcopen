/*
 * FbBasic.cpp
 *
 * Copyright 2020 (C) SYMG(Shanghai) Intelligence System Co.,Ltd
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "FbBasic.h"

#include <algorithm>
#include <limits>

namespace plcopen
{
namespace
{
INT normalizedPreset(INT preset)
{
    return std::max<INT>(preset, 0);
}

bool risingEdge(bool current, bool& previous, bool& initialized)
{
    if (!initialized)
    {
        previous = current;
        initialized = true;
        return false;
    }

    const bool edge = current && !previous;
    previous = current;
    return edge;
}

bool fallingEdge(bool current, bool& previous, bool& initialized)
{
    if (!initialized)
    {
        previous = current;
        initialized = true;
        return false;
    }

    const bool edge = !current && previous;
    previous = current;
    return edge;
}

DT saturatingAdvanceDateTime(DT current, TIME delta)
{
    const DT maxValue = std::numeric_limits<DT>::max();
    if (current >= maxValue - delta)
        return maxValue;

    return current + delta;
}
} // namespace

    bool FbCycleTimeAwareType::setCycleTime(TIME cycleTime)
    {
        if (cycleTime == 0)
            return false;

        mCycleTime = cycleTime;
        return true;
    }

    TIME FbCycleTimeAwareType::cycleTime(void) const
    {
        return mCycleTime;
    }

    TIME FbCycleTimeAwareType::advanceElapsed(TIME currentElapsed, TIME targetElapsed) const
    {
        if (currentElapsed >= targetElapsed)
            return targetElapsed;

        const TIME remaining = targetElapsed - currentElapsed;
        return currentElapsed + std::min(mCycleTime, remaining);
    }

    ////////////////////////////////////////////////////////////

    void FbRTrig::call(void)
    {
        mQ = risingEdge(mCLK, mPreviousCLK, mInitialized);
    }

    ////////////////////////////////////////////////////////////

    void FbFTrig::call(void)
    {
        mQ = fallingEdge(mCLK, mPreviousCLK, mInitialized);
    }

    ////////////////////////////////////////////////////////////

    void FbSr::call(void)
    {
        mQ1 = mS1 || (!mR && mQ1);
    }

    ////////////////////////////////////////////////////////////

    void FbRs::call(void)
    {
        mQ1 = !mR1 && (mS || mQ1);
    }

    ////////////////////////////////////////////////////////////

    void FbTon::call(void)
    {
        if (!mIN)
        {
            mQ = false;
            mET = 0;
        }
        else if (!mPreviousIN)
        {
            mQ = (mPT == 0);
            mET = 0;
        }
        else if (mPT == 0)
        {
            mQ = true;
            mET = 0;
        }
        else
        {
            mET = advanceElapsed(mET, mPT);
            mQ = (mET >= mPT);
        }

        mPreviousIN = mIN;
    }

    ////////////////////////////////////////////////////////////

    void FbTof::call(void)
    {
        if (mIN)
        {
            mQ = true;
            mET = 0;
        }
        else if (mPreviousIN)
        {
            mET = 0;
            mQ = (mPT != 0);
        }
        else if (mQ && mPT != 0)
        {
            mET = advanceElapsed(mET, mPT);
            mQ = (mET < mPT);
        }

        mPreviousIN = mIN;
    }

    ////////////////////////////////////////////////////////////

    void FbTp::call(void)
    {
        if (!mInitialized)
        {
            mPreviousIN = mIN;
            mInitialized = true;
            return;
        }

        if (!mTiming && mIN && !mPreviousIN)
        {
            mTiming = (mPT != 0);
            mQ = mTiming;
            mET = 0;
        }
        else if (mTiming)
        {
            mET = advanceElapsed(mET, mPT);
            if (mET >= mPT)
            {
                mQ = false;
                mTiming = false;
            }
            else
            {
                mQ = true;
            }
        }

        mPreviousIN = mIN;
    }

    ////////////////////////////////////////////////////////////

    void FbRtc::call(void)
    {
        if (!mEN)
        {
            mQ = false;
            mCDT = 0;
            mRunning = false;
            return;
        }

        mQ = true;
        if (!mRunning)
        {
            mCDT = mPDT;
            mRunning = true;
            return;
        }

        mCDT = saturatingAdvanceDateTime(mCDT, cycleTime());
    }

    ////////////////////////////////////////////////////////////

    void FbCtu::call(void)
    {
        const bool cuRisingEdge = risingEdge(mCU, mPreviousCU, mInitialized);
        const INT preset = normalizedPreset(mPV);

        if (mR)
        {
            mCV = 0;
        }
        else if (cuRisingEdge && mCV < std::numeric_limits<INT>::max())
        {
            ++mCV;
        }

        mQ = (mCV >= preset);
    }

    ////////////////////////////////////////////////////////////

    void FbCtd::call(void)
    {
        const bool cdRisingEdge = risingEdge(mCD, mPreviousCD, mInitialized);
        const INT preset = normalizedPreset(mPV);

        if (mLD)
        {
            mCV = preset;
        }
        else if (cdRisingEdge && mCV > 0)
        {
            --mCV;
        }

        mQ = (mCV <= 0);
    }

    ////////////////////////////////////////////////////////////

    void FbCtud::call(void)
    {
        const bool cuRisingEdge = risingEdge(mCU, mPreviousCU, mInitializedCU);
        const bool cdRisingEdge = risingEdge(mCD, mPreviousCD, mInitializedCD);
        const INT preset = normalizedPreset(mPV);

        if (mR)
        {
            mCV = 0;
        }
        else if (mLD)
        {
            mCV = preset;
        }
        else if (!(cuRisingEdge && cdRisingEdge))
        {
            if (cuRisingEdge && mCV < std::numeric_limits<INT>::max())
            {
                ++mCV;
            }
            else if (cdRisingEdge && mCV > 0)
            {
                --mCV;
            }
        }

        mQU = (mCV >= preset);
        mQD = (mCV <= 0);
    }

} // namespace plcopen
