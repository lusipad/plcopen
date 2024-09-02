/*
 * axis_move_oscilloscope.cpp
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
 * 绘制输出的波形图，方便查看每个周期内的状态变化
 */

#include "FbSingleAxis.h"
#include "Scheduler.h"
#include <iomanip>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <queue>
#include <map>
#include <utility> 
#include <algorithm>
#include <cmath>

using namespace Uranus;
using namespace std;

/// <summary>
/// 存储周期的波形数据
/// </summary>
class Oscilloscope
{
public:
    void clearScreen() {
        // ANSI escape code to clear the screen
        std::cout << "\033[2J\033[1;1H";
    }

    void Print()
    {
        clearScreen();
        // 获取info queue最顶层的30个数据并且打印
        for each (auto item in data)
        {
            std::cout << item.first << ":\t";

            for (auto iter = item.second.begin(); iter != item.second.end(); ++iter)
            {
                *iter ? std::cout << '-' : std::cout << '_';
            }

            std::cout << endl;
        }
    }

    void Add(const string& key, bool value)
    {
        data[key].push_back(value);
    }

private:
    std::map<std::string, std::vector<bool>> data;
};

int main(void)
{
    cout.precision(8);

    Scheduler sched;
    double frequency = 100; // 定义外部周期调度的频率
    int32_t axisId = 1;     // id 可随意定义，各轴不重复即可
    sched.setFrequency(frequency);
    Axis *axis = sched.newAxis(axisId, new Servo());

    // 功能块初始化
    FbPower power;
    power.mAxis = axis;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;

    FbMoveAbsolute moveAbs1;
    moveAbs1.mAxis = axis;
    moveAbs1.mPosition = 10;
    moveAbs1.mVelocity = 400;
    moveAbs1.mAcceleration = 500;
    moveAbs1.mDeceleration = 500;

    FbMoveAbsolute moveAbs2;
    moveAbs2.mAxis = axis;
    moveAbs2.mPosition = 20;
    moveAbs2.mVelocity = 200;
    moveAbs2.mAcceleration = 300;
    moveAbs2.mDeceleration = 300;
    moveAbs2.mBufferMode = MC_BufferMode::BUFFERED;

    FbReadActualPosition readPos;
    readPos.mAxis = axis;
    readPos.mEnable = true;

    FbReadCommandVelocity readVel;
    readVel.mAxis = axis;
    readVel.mEnable = true;

    double t = 0;
    bool moveAbs1AlreadyDone = false;
    // “实时”周期任务
    Oscilloscope oscope;

    while (1)
    {
        // 调度器周期处理
        sched.runCycle();

        // 功能块调用
        power.call();
        moveAbs1.call();
        moveAbs2.call();
        readPos.call();
        readVel.call();

        bool isPowerOn = power.mStatus && power.mValid;
        if (isPowerOn && !moveAbs1.mExecute)
        {
            moveAbs1.mExecute = isPowerOn;
        }

        if (moveAbs1.mDone && !moveAbs1AlreadyDone)
        { 
            moveAbs1AlreadyDone = true;
        }

        moveAbs2.mExecute = moveAbs1.mBusy;

        if (moveAbs2.mDone)
        { 
            break;
        }

        this_thread::sleep_for(std::chrono::microseconds((long)(1000000 / frequency)));
        t += 1 / frequency;

        oscope.Add("M1:Done", moveAbs1.mDone);
        oscope.Add("M1:Busy", moveAbs1.mBusy);
        oscope.Add("M1:Active", moveAbs1.mActive);
        oscope.Add("M1:Error", moveAbs1.mError);
        oscope.Add("M2:Done", moveAbs2.mDone);
        oscope.Add("M2:Busy", moveAbs2.mBusy);
        oscope.Add("M2:Active", moveAbs2.mActive);
        oscope.Add("M2:Error", moveAbs2.mError);

        oscope.Print();
    }

    return 0;
}
