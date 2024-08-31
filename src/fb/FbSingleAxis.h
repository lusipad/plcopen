/*
 * FbSingleAxis.h
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

#ifndef _URANUS_FBSINGLEAXIS_HPP_
#define _URANUS_FBSINGLEAXIS_HPP_

#include "FbPLCOpenBase.h"

namespace Uranus
{

#pragma pack(push)
#pragma pack(4)

    class FbPower : public FbBaseType
    {
    public:
        FB_INPUT AXIS_REF mAxis;

        FB_INPUT BOOL mEnable = false;
        FB_INPUT BOOL mEnablePositive = false;
        FB_INPUT BOOL mEnableNegative = false;

        FB_OUTPUT BOOL mStatus = false;
        FB_OUTPUT BOOL mValid = false;

    public:
        void call(void);

        void onOperationError(MC_ErrorCode errorCode, int32_t customId);
    };

    class FbHome : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mPosition = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbStop : public FbExecAxisType
    {
    public:
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
        void onExecNegedge(void);
    };

    class FbHalt : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbMoveAbsolute : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mPosition = 0;
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;
        FB_INPUT MC_DIRECTION mDirection = MC_Direction::CURRENT;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbMoveRelative : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mDistance = 0;
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbMoveAdditive : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mDistance = 0;
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbMoveVelocity : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

        FB_OUTPUT BOOL &mInVelocity = mDone;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbReadStatus : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT BOOL mErrorStop = false;
        FB_OUTPUT BOOL mDisabled = false;
        FB_OUTPUT BOOL mStopping = false;
        FB_OUTPUT BOOL mHoming = false;
        FB_OUTPUT BOOL mStandstill = false;
        FB_OUTPUT BOOL mDiscreteMotion = false;
        FB_OUTPUT BOOL mContinuousMotion = false;
        FB_OUTPUT BOOL mSynchronizedMotion = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    class FbReadMotionState : public FbReadInfoAxisType
    {
    public:
        FB_INPUT MC_SOURCE mSource = MC_Source::SETVALUE;

        FB_OUTPUT BOOL mConstantVelocity = false;
        FB_OUTPUT BOOL mAccelerating = false;
        FB_OUTPUT BOOL mDecelerating = false;
        FB_OUTPUT BOOL mDirectionPositive = false;
        FB_OUTPUT BOOL mDirectionNegative = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };
    
    // TODO: 这里直接提供 FbAxisEnableType 更合理
    class FbReadAxisError : public FbEnableType
    {
    public:
        FB_INPUT AXIS_REF mAxis = nullptr;
        FB_OUTPUT BOOL mValid = false;
        FB_OUTPUT BOOL mBusy = false;
        FB_OUTPUT MC_SERVOERRORCODE mAxisErrorID = 0;

    public:
        void call(void);
        MC_ErrorCode onEnableTrue(void);
        MC_ErrorCode onEnableFalse(void);
    };

    class FbReset : public FbWriteInfoAxisType
    {
    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    enum class MC_Parameter
    {
        COMMANDED_POSITION = 1,
        SWLIMIT_POS = 2,
        SWLIMIT_NEG = 3,
        ENABLE_LIMIT_POS = 4,
        ENABLE_LIMIT_NEG = 5,
        ENABLE_POS_LAG_MONITORING = 6,
        MAX_POSITION_LAG = 7,
        MAX_VELOCITY_SYSTEM = 8,
        MAX_VELOCITY_APPL = 9,
        ACTUAL_VELOCITY = 10,
        COMMANDED_VELOCITY = 11,
        MAX_ACCELERATION_SYSTEM = 12,
        MAX_ACCELERATION_APPL = 13,
        MAX_DECELERATION_SYSTEM = 14,
        MAX_DECELERATION_APPL = 15,
        MAX_JERK_SYSTEM = 16,
        MAX_JERK_APPL = 17,
    } ;

    class FbReadActualPosition : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT LREAL mPosition = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    class FbReadCommandPosition : public FbReadActualPosition
    {
    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
    };

    class FbReadActualVelocity : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT LREAL mVelocity = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    // TODO: 协议里没有这个功能
    class FbReadCommandVelocity : public FbReadActualVelocity
    {
    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
    };

    // TODO: 协议里没有这个功能
    class FbEmergencyStop : public FbWriteInfoAxisType
    {
    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

#pragma pack(pop)

}

#endif /** _URANUS_FBSINGLEAXIS_HPP_ **/
