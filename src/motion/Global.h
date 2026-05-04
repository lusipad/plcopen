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

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

#define URANUS_AXESGROUP_IDENT_NUM 8
#define URANUS_CARTESIAN_DIMENSION3 3
#define URANUS_CARTESIAN_DIMENSION6 6
#define URANUS_TRANSITIONPARAMETER_NUM 4

    /**
     * @brief Servo::readVal/writeVal extension-channel bases used by the digital IO FBs.
     */
    inline constexpr int MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE = 10000;
    inline constexpr int MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE = 20000;

    typedef uint32_t MC_ServoErrorCode;

    enum class MC_ServoControlMode
    {
        POSITION = 0,
        VELOCITY = 1,
        TORQUE = 2,
    };

    /**
     * @brief 公共错误码集合。
     */
    enum class MC_ErrorCode
    {
        GOOD = 0x0, // 成功

        QUEUEFULL = 0x1,            // 轴队列已满
        AXIS_ENCODER_OVERFLOW = 0x2,  // 轴编码器溢出
        AXIS_POWER_OFF = 0x3,         // 轴未使能
        AXIS_POWER_ON = 0x4,          // 轴已功能
        FREQUENCY_ILLEGAL = 0x5,     // 频率不合法
        AXIS_NO_TEXIST = 0x8,         // 轴ID号不存在
        AXIS_BUSY = 0xA,             // 轴正忙，有功能块正在控制轴运动
        FAILED_TO_BUFFER = 0xF,       // 不支持以buffer形式添加
        BLENDING_MODE_ILLEGAL = 0x10, // BufferMode值非法
        PARAMETER_NOT_SUPPORT = 0x14, // 不支持该参数号
        OVERRIDE_ILLEGAL = 0x17,     // OVERRIDE值非法
        SHIFTING_MODE_ILLEGAL = 0x19, // 移动模式非法
        SOURCE_ILLEGAL = 0x1A,       // 获取源非法
        CONTROL_MODE_ILLEGAL = 0x23,  // 控制模式设置错误

        POS_ILLEGAL = 0x100,        // 位置不合法
        ACC_ILLEGAL = 0x101,        // 加/减速度不合法
        VEL_ILLEGAL = 0x102,        // 速度不合法
        AXIS_HARDWARE = 0x103,      // 硬件错误
        VEL_LIMIT_TOOLOW = 0x104,    // 由于配置文件限制，无法到达跟随的速度（电子齿轮，凸轮中）
        END_VEL_CANNOT_REACH = 0x105, // 实际终速度过高无法到达预设速度

        CMD_PPOS_OVERLIMIT = 0x106,  // 指令位置超出配置文件正向限制
        CMD_NPOS_OVERLIMIT = 0x107,  // 指令位置超出配置文件负向限制
        FORBIDDEN_PPOS_MOVE = 0x108, // 禁止正向移动
        FORBIDDEN_NPOS_MOVE = 0x109, // 禁止负向移动

        POS_LAG_OVERLIMIT = 0x10A, // 轴跟随误差超限
        CMD_VEL_OVERLIMIT = 0x10B, // 轴指令速度超出限制
        CMD_ACC_OVERLIMIT = 0x10C, // 轴指令加速度超出限制
        POS_INFINITY = 0x10E,     // 轴设定位置不合法

        SOFTWARE_EMGS = 0x1EE,  // 用户急停
        SYSTEM_EMGS = 0x1EF,    // 系统急停
        COMMUNICATION = 0x1F0, // 硬件通信异常

        /** 配置错误**/
        CFG_AXIS_ID_ILLEGAL = 0x201,
        CFG_UNIT_RATIO_OUT_OF_RANGE = 0x202,
        CFG_CONTROL_MODE_ILLEGAL = 0x203,
        CFG_VEL_LIMIT_ILLEGAL = 0x204,
        CFG_ACC_LIMIT_ILLEGAL = 0x205,
        CFG_POS_LAG_ILLEGAL = 0x206,
        CFG_PKP_ILLEGAL = 0x207,
        CFG_FEED_FORWORD_ILLEGAL = 0x208,
        CFG_MODULO_ILLEGAL = 0x209,
        CFG_JERK_LIMIT_ILLEGAL = 0x20A,

        HOMING_VEL_ILLEGAL = 0x210,
        HOMING_ACC_ILLEGAL = 0x211,
        HOMING_SIG_ILLEGAL = 0x212,
        HOMING_MODE_ILLEGAL = 0x214,
        HOME_POSITION_ILLEGAL = 0x215,

        AXIS_DISABLED = 0x500,
        AXIS_STANDSTILL = 0x501,
        AXIS_HOMING = 0x502,
        AXIS_DISCRETE_MOTION = 0x503,
        AXIS_CONTINUOUS_MOTION = 0x504,
        AXIS_SYNCHRONIZED_MOTION = 0x505,
        AXIS_STOPPING = 0x506,
        AXISE_RRORSTOP = 0x507,
        GROUP_DISABLED = 0x508,
        GROUP_STANDBY = 0x509,
        GROUP_HOMING = 0x50A,
        GROUP_MOVING = 0x50B,
        GROUP_STOPPING = 0x50C,
        GROUP_ERRORSTOP = 0x50D,

        GROUP_MEMBER_NOT_READY = 0x520,
        AXIS_ALREADY_IN_GROUP = 0x521,
        AXIS_IN_OTHER_GROUP = 0x522,
        AXIS_GROUP_MISMATCH = 0x523,
    };

    /**
     * @brief PLCopen 单轴状态机。
     */
    enum class MC_AxisStatus
    {
        DISABLED = 0,
        STANDSTILL = 1,
        HOMING = 2,
        DISCRETE_MOTION = 3,
        CONTINUOUS_MOTION = 4,
        SYNCHRONIZED_MOTION = 5,
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

    /**
     * @brief 运动状态分类。
     */
    enum class MC_MotionState
    {
        INPOSITION = 0,
        CONSTANT_VELOCITY = 1,
        ACCELERATING = 2,
        DECELERATING = 3,
    };

    /**
     * @brief PLCopen buffer mode.
     *
     * 当前实现区分以下语义：
     * - `ABORTING`：立即打断当前命令。
     * - `BUFFERED`：排队，并等待前一条 MoveNode 到达终点后启动。
     * - `BLENDING_LOW` / `BLENDING_PREVIOUS` / `BLENDING_NEXT` / `BLENDING_CNC`：
     *   在前一条 MoveNode 减速到标称速度 30% 以下时提前接续。
     * - `BLENDING_HIGH`：在前一条 MoveNode 减速到标称速度 70% 以下时提前接续。
     *
     * Homing / Sync 节点当前不参与 blending，仍走排队语义。
     */
    enum class MC_BufferMode
    {
        ABORTING = 0,
        BUFFERED = 1,
        BLENDING_LOW = 2,
        BLENDING_PREVIOUS = 3,
        BLENDING_NEXT = 4,
        BLENDING_HIGH = 5,
        BLENDING_CNC = 128,
    };

    /**
     * @brief 判断一个 buffer mode 是否是当前实现认识的公开枚举值。
     */
    inline constexpr bool isDefinedBufferMode(MC_BufferMode mode)
    {
        switch (mode)
        {
        case MC_BufferMode::ABORTING:
        case MC_BufferMode::BUFFERED:
        case MC_BufferMode::BLENDING_LOW:
        case MC_BufferMode::BLENDING_PREVIOUS:
        case MC_BufferMode::BLENDING_NEXT:
        case MC_BufferMode::BLENDING_HIGH:
        case MC_BufferMode::BLENDING_CNC:
            return true;
        default:
            return false;
        }
    }

    /**
     * @brief 判断一个 buffer mode 是否在当前实现中走“不立即打断”的排队语义。
     */
    inline constexpr bool usesQueuedBufferModeSemantics(MC_BufferMode mode)
    {
        switch (mode)
        {
        case MC_BufferMode::BUFFERED:
        case MC_BufferMode::BLENDING_LOW:
        case MC_BufferMode::BLENDING_PREVIOUS:
        case MC_BufferMode::BLENDING_NEXT:
        case MC_BufferMode::BLENDING_HIGH:
        case MC_BufferMode::BLENDING_CNC:
            return true;
        default:
            return false;
        }
    }

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

    /**
     * @brief CombineAxes 的组合方式。
     */
    enum class MC_CombineMode
    {
        mcAddAxes = 0,
        mcSubAxes = 1,
    };

    // TODO: 回零要调整，应该是按照固定、直接设定等方式的回零
    /**
     * @brief 当前实现支持的 homing 模式。
     */
    enum class MC_HomingMode
    {
        DIRECT = 1000, // 直接以当前位置作为零点

        MODE1 = 1001,
        MODE2 = 1002,
        MODE3 = 1003,
        MODE4 = 1004,
        MODE5 = 1005, // 负向移动寻找回零开关，触发后正向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为上升沿触发
        MODE6 = 1006, // 负向移动寻找回零开关，触发后正向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为下降沿触发
        MODE7 = 1007, // 正向移动寻找回零开关，触发后负向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为上升沿触发
        MODE8 = 1008, // 正向移动寻找回零开关，触发后负向移动离开回零开关，最终停留在刚离开回零开关处，回零开关为下降沿触发
        MODE9 = 1009,
        MODE10 = 1010,
        MODE11 = 1011,
        MODE12 = 1012,
        MODE13 = 1013,
        MODE14 = 1014,
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
        START_VELOCITY = 1,    // 不支持
        CONSTANT_VELOCITY = 2, // 不支持
        CORNER_DISTANCE = 3,
        MAX_CORNER_DEVIATION = 4,
    };

    enum class MC_LogLevel
    {
        ERROR = 0,
        WARN = 1,
        INFO = 2,
        DEBUG = 3,
    };

    //////////////////////////////////////////////////////////////

    /**
     * @brief 轴的编码器与模数配置。
     */
    struct AxisMetricInfo
    {
        double mDevUnitRatio = 8192; // 设备编码器单位比率
        double mModulo = 0;          // 模数值
    };

    /**
     * @brief 轴的软限位配置。
     */
    struct AxisRangeLimitInfo
    {
        bool mSwLimitPositive = false; // 正向限位启用标志位
        bool mSwLimitNegative = false; // 负向限位启用标志位
        double mLimitPositive = 0;     // 正向限位位置
        double mLimitNegative = 0;     // 负向限位位置
    };

    /**
     * @brief 轴的运动极限配置。
     */
    struct AxisMotionLimitInfo
    {
        double mVelLimit = 1000;   // 速度限制
        double mAccLimit = 5000;   // 加速度限制
        double mPosLagLimit = 150; // 跟随误差限制
        bool mEnablePosLagMonitoring = true; // 跟随误差监控启用标志位
        double mJerkLimit = 0;     // jerk 配置存取值，当前不作为统一运行时限幅
    };

    /**
     * @brief 轴控制器配置。
     */
    struct AxisControlInfo
    {
        MC_ControlMode mControlMode = MC_ControlMode::POSOPENLOOP; // 控制模式
        double mPKp = 10;                                         // 闭环位置Kp
        double mFF = 0;                                           // 位置前馈
    };

    /**
     * @brief Axis homing configuration.
     */
    struct AxisHomingInfo
    {
        uint8_t *mHomingSig = 0;                          // 回零信号地址
        uint8_t mHomingSigBitOffset = 0;                  // 回零信号偏移
        uint8_t *mHomingIndexSig = 0;                     // 回零 index 信号地址
        uint8_t mHomingIndexSigBitOffset = 0;             // 回零 index 信号偏移
        MC_HomingMode mHomingMode = MC_HomingMode::DIRECT; // 回零模式
        double mHomingVelSearch = 0;                      // 寻找回零信号速度
        double mHomingVelRegression = 0;                  // 返回零位速度
        double mHomingAcc = 0;                            // 回零加速度
        double mHomingJerk = 0;                           // 回零加加速
    };

    /**
     * @brief Axis runtime configuration bundle.
     */
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
