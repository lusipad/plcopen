/*
 * CamTable.cpp
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

#include "CamTable.h"

#include <algorithm>

namespace plcopen
{

    void CamTable::addPoint(double masterPosition, double slavePosition)
    {
        mPoints.emplace_back(masterPosition, slavePosition);
        std::sort(mPoints.begin(), mPoints.end(), [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
    }

    bool CamTable::empty(void) const
    {
        return mPoints.empty();
    }

    double CamTable::sample(double masterPosition) const
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

} // namespace plcopen
