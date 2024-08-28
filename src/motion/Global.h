/*
 * Global.h
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

#ifndef _URANUS_MOTION_GLOBAL_HPP_
#define _URANUS_MOTION_GLOBAL_HPP_

#include <cstdint>
#include <cstddef>

namespace Uranus
{

#pragma pack(push)
#pragma pack(4)

#define URANUS_AXESGROUP_IDENT_NUM 8
#define URANUS_CARTESIAN_DIMENSION3 3
#define URANUS_CARTESIAN_DIMENSION6 6
#define URANUS_TRANSITIONPARAMETER_NUM 4

    typedef uint32_t MC_ServoErrorCode;

    enum class MC_ServoControlMode
    {
        POSITION = 0,
        VELOCITY = 1,
        TORQUE = 2,
    };

    enum class MC_ErrorCode
    {
        GOOD = 0x0, // 成功

        QUEUEFULL = 0x1,            // 轴队列已满
        AXISENCODEROVERFLOW = 0x2,  // 轴编码器溢出
        AXISPOWEROFF = 0x3,         // 轴未使能
        AXISPOWERON = 0x4,          // 轴已功能
        FREQUENCYILLEGAL = 0x5,     // 频率不合法
        AXISNOTEXIST = 0x8,         // 轴ID号不存在
        AXISBUSY = 0xA,             // 轴正忙，有功能块正在控制轴运动
        FAILEDTOBUFFER = 0xF,       // 不支持以buffer形式添加
        BLENDINGMODEILLEGAL = 0x10, // BufferMode值非法
        PARAMETERNOTSUPPORT = 0x14, // 不支持该参数号
        OVERRIDEILLEGAL = 0x17,     // OVERRIDE值非法
        SHIFTINGMODEILLEGAL = 0x19, // 移动模式非法
        SOURCEILLEGAL = 0x1A,       // 获取源非法
        CONTROLMODEILLEGAL = 0x23,  // 控制模式设置错误

        POSILLEGAL = 0x100,        // 位置不合法
        ACCILLEGAL = 0x101,        // 加/减速度不合法
        VELILLEGAL = 0x102,        // 速度不合法
        AXISHARDWARE = 0x103,      // 硬件错误
        VELLIMITTOOLOW = 0x104,    // 由于配置文件限制，无法到达跟随的速度（电子齿轮，凸轮中）
        ENDVELCANNOTREACH = 0x105, // 实际终速度过高无法到达预设速度

        CMDPPOSOVERLIMIT = 0x106,  // 指令位置超出配置文件正向限制
        CMDNPOSOVERLIMIT = 0x107,  // 指令位置超出配置文件负向限制
        FORBIDDENPPOSMOVE = 0x108, // 禁止正向移动
        FORBIDDENNPOSMOVE = 0x109, // 禁止负向移动

        POSLAGOVERLIMIT = 0x10A, // 轴跟随误差超限
        CMDVELOVERLIMIT = 0x10B, // 轴指令速度超出限制
        CMDACCOVERLIMIT = 0x10C, // 轴指令加速度超出限制
        POSINFINITY = 0x10E,     // 轴设定位置不合法

        SOFTWAREEMGS = 0x1EE,  // 用户急停
        SYSTEMEMGS = 0x1EF,    // 系统急停
        COMMUNICATION = 0x1F0, // 硬件通信异常

        /** 配置错误**/
        CFGAXISIDILLEGAL = 0x201,
        CFGUNITRATIOOUTOFRANGE = 0x202,
        CFGCONTROLMODEILLEGAL = 0x203,
        CFGVELLIMITILLEGAL = 0x204,
        CFGACCLIMITILLEGAL = 0x205,
        CFGPOSLAGILLEGAL = 0x206,
        CFGPKPILLEGAL = 0x207,
        CFGFEEDFORWORDILLEGAL = 0x208,
        CFGMODULOILLEGAL = 0x209,

        HOMINGVELILLEGAL = 0x210,
        HOMINGACCILLEGAL = 0x211,
        HOMINGSIGILLEGAL = 0x212,
        HOMINGMODEILLEGAL = 0x214,
        HOMEPOSITIONILLEGAL = 0x215,

        AXISDISABLED = 0x500,
        AXISSTANDSTILL = 0x501,
        AXISHOMING = 0x502,
        AXISDISCRETEMOTION = 0x503,
        AXISCONTINUOUSMOTION = 0x504,
        AXISSYNCHRONIZEDMOTION = 0x505,
        AXISSTOPPING = 0x506,
        AXISERRORSTOP = 0x507,
    };

    enum class MC_AxisStatus
    {
        DISABLED = 0,
        STANDSTILL = 1,
        HOMING = 2,
        DISCRETEMOTION = 3,
        CONTINUOUSMOTION = 4,
        SYNCHRONIZEDMOTION = 5,
        STOPPING = 6,
        ERRORSTOP = 7,
    };

    enum class MC_GroupStatus
    {
        DISABLED = 0,
        STANDBY = 1,
        HOMING = 2,
        MOVING = 3,
        STOPPING = 4,
        ERRORSTOP = 5,
    };

    enum class MC_MotionState
    {
        INPOSITION = 0,
        CONSTANTVELOCITY = 1,
        ACCELERATING = 2,
        DECELERATING = 3,
    };

    enum class MC_BufferMode
    {
        ABORTING = 0,
        BUFFERED = 1,
        BLENDINGLOW = 2,
        BLENDINGPREVIOUS = 3,
        BLENDINGNEXT = 4,
        BLENDINGHIGH = 5,
        BLENDINGCNC = 128,
    };

    enum class MC_Direction
    {
        POSITIVE = 1,
        SHORTESTWAY = 2,
        NEGATIVE = 3,
        CURRENT = 4,
    };

    enum class MC_Source
    {
        SETVALUE = 0,
        ACTUALVALUE = 1,
    };

    enum class MC_ShiftingMode
    {
        ABSOLUTE = 0,
        RELATIVE = 1,
        ADDITIVE = 2,
    };

    // TODO: 回零要调整，应该是按照固定、直接设定等方式的回零
    enum class MC_HomingMode
    {
        DIRECT = 1000, // 直接以当前位置作为零点

        MODE5 = 1005, // 负向移动寻找回零开关，触发后正向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为上升沿触发
        MODE6 = 1006, // 负向移动寻找回零开关，触发后正向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为下降沿触发
        MODE7 = 1007, // 正向移动寻找回零开关，触发后负向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为上升沿触发
        MODE8 = 1008, // 正向移动寻找回零开关，触发后负向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为下降沿触发
    };

    enum class MC_TouchProbeStatus
    {
        NOTEXIST = 0,
        RESETTING = 1,
        RESETED = 2,
        TIGGERING = 3,
        TIGGERED = 4,
    };

    enum class MC_ControlMode
    {
        POSOPENLOOP = 0,
        VELCLOSELOOP = 1,
        VELOPENLOOP = 2,
    };

    enum class MC_CoordSystem
    {
        ACS = 0,
        MCS = 1,
        PCS = 2,
    };

    enum class MC_CircMode
    {
        BORDER = 0,
        CENTER = 1,
        RADIUS = 2,
        VECTOR = 1000,
    };

    enum class MC_CircPath
    {
        CLOCKWISE = 0,
        COUNTERCLOCKWISE = 1,
    };

    enum class MC_TransitionMode
    {
        NONE = 0,
        STARTVELOCITY = 1,    // 不支持
        CONSTANTVELOCITY = 2, // 不支持
        CORNERDISTANCE = 3,
        MAXCORNERDEVIATION = 4,
    };

    enum class MC_LogLevel
    {
        ERROR = 0,
        WARN = 1,
        INFO = 2,
        DEBUG = 3,
    };

    //////////////////////////////////////////////////////////////

    struct AxisMetricInfo
    {
        double mDevUnitRatio = 8192; // 设备编码器单位比率
        double mModulo = 0;          // 模数值
    };

    struct AxisRangeLimitInfo
    {
        bool mSwLimitPositive = false; // 正向限位启用标志位
        bool mSwLimitNegative = false; // 负向限位启用标志位
        double mLimitPositive = 0;     // 正向限位位置
        double mLimitNegative = 0;     // 负向限位位置
    };

    struct AxisMotionLimitInfo
    {
        double mVelLimit = 1000;   // 速度限制
        double mAccLimit = 5000;   // 加速度限制
        double mPosLagLimit = 150; // 跟随误差限制
    };

    struct AxisControlInfo
    {
        MC_ControlMode mControlMode = MC_ControlMode::POSOPENLOOP; // 控制模式
        double mPKp = 10;                                         // 闭环位置Kp
        double mFF = 0;                                           // 位置前馈
    };

    struct AxisHomingInfo
    {
        uint8_t *mHomingSig = 0;                          // 回零信号地址
        uint8_t mHomingSigBitOffset = 0;                  // 回零信号偏移
        MC_HomingMode mHomingMode = MC_HomingMode::DIRECT; // 回零模式
        double mHomingVelSearch = 0;                      // 寻找回零信号速度
        double mHomingVelRegression = 0;                  // 返回零位速度
        double mHomingAcc = 0;                            // 回零加速度
        double mHomingJerk = 0;                           // 回零加加速
    };

    struct AxisConfig
    {
        AxisMetricInfo mMetricInfo;
        AxisRangeLimitInfo mRangeLimitInfo;
        AxisMotionLimitInfo mMotionLimitInfo;
        AxisControlInfo mControlInfo;
        AxisHomingInfo mHomingInfo;
    };

    //////////////////////////////////////////////////////////////
    /*
    struct GroupMotionLimitInfo
    {
        double mLinVelLimit = 1000;     //线速度限制
        double mLinAccLimit = 5000;     //线加速度限制
        double mAngVelLimit = 3600;     //角速度限制
        double mAngAccLimit = 18000;    //角加速度限制
    };*/

    struct GroupConfig
    {
        //    GroupMotionLimitInfo mMotionLimitInfo;
    };

#pragma pack(pop)

}

#endif /** _URANUS_MOTION_GLOBAL_HPP_ **/
