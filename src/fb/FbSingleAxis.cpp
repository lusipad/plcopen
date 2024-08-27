/*
 * FbSingleAxis.cpp
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

#include "FbSingleAxis.h"
#include "Axis.h"

#include <cstring>

namespace Uranus
{

    void FbPower::call(void)
    {
        if (!mAxis)
        {
            onOperationError(MC_ErrorCode::AXISNOTEXIST, 0);
            return;
        }

        MC_ErrorCode err;
        bool isDone;
        err = mAxis->setPower(mEnable, mEnablePositive, mEnableNegative, isDone);
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mStatus = isDone ? mEnable : false;
        mValid = isDone;
        clearError();
    }

    void FbPower::onOperationError(MC_ErrorCode errorCode, int32_t customId)
    {
        mStatus = mValid = false;
        FbBaseType::onOperationError(errorCode, customId);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbHome::onAxisExecPosedge(void)
    {
        return mAxis->addHoming(this, mPosition, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbStop::onAxisExecPosedge(void)
    {
        return mAxis->addStop(this, mDeceleration, mJerk);
    }

    void FbStop::onExecNegedge(void)
    {
        mAxis->cancelStopLater();
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbHalt::onAxisExecPosedge(void)
    {
        return mAxis->addHalt(this, mDeceleration, mJerk, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbMoveAbsolute::onAxisExecPosedge(void)
    {
        return mAxis->addMovePos(this, mPosition, mVelocity, mAcceleration, mDeceleration, mJerk, MC_ShiftingMode::ABSOLUTE,
                                 mDirection, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbMoveRelative::onAxisExecPosedge(void)
    {
        return mAxis->addMovePos(this, mDistance, mVelocity, mAcceleration, mDeceleration, mJerk, MC_ShiftingMode::RELATIVE,
            MC_Direction::CURRENT, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbMoveAdditive::onAxisExecPosedge(void)
    {
        return mAxis->addMovePos(this, mDistance, mVelocity, mAcceleration, mDeceleration, mJerk, MC_ShiftingMode::ADDITIVE,
            MC_Direction::CURRENT, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbMoveVelocity::onAxisExecPosedge(void)
    {
        return mAxis->addMoveVel(this, mVelocity, mAcceleration, mDeceleration, mJerk, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadStatus::onAxisEnable(bool &isDone)
    {
        onDisable();

        switch (mAxis->status())
        {
        case MC_AxisStatus::DISABLED:
            mDisabled = true;
            break;
        case MC_AxisStatus::STANDSTILL:
            mStandstill = true;
            break;
        case MC_AxisStatus::HOMING:
            mHoming = true;
            break;
        case MC_AxisStatus::DISCRETEMOTION:
            mDiscreteMotion = true;
            break;
        case MC_AxisStatus::CONTINUOUSMOTION:
            mContinuousMotion = true;
            break;
        case MC_AxisStatus::SYNCHRONIZEDMOTION:
            mSynchronizedMotion = true;
            break;
        case MC_AxisStatus::STOPPING:
            mStopping = true;
            break;
        case MC_AxisStatus::ERRORSTOP:
            mErrorStop = true;
            break;
        }

        isDone = true;

        return MC_ErrorCode::GOOD;
    }

    void FbReadStatus::onDisable(void)
    {
        memset(&mErrorStop, 0, &mSynchronizedMotion - &mErrorStop + sizeof(mSynchronizedMotion));
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadMotionState::onAxisEnable(bool &isDone)
    {
        double acc, vel;
        switch (mSource)
        {
        case MC_Source::SETVALUE:
            acc = mAxis->cmdAcceleration();
            vel = mAxis->cmdVelocity();
            break;

        case MC_Source::ACTUALVALUE:
            acc = mAxis->actAcceleration();
            vel = mAxis->actVelocity();
            break;

        default:
            return MC_ErrorCode::SOURCEILLEGAL;
        }

        onDisable();

        if (vel > 0)
        {
            if (acc < 0)
                mDecelerating = true;
            else if (acc > 0)
                mAccelerating = true;
            else
                mConstantVelocity = true;

            mDirectionPositive = true;
        }
        else if (vel < 0)
        {
            if (acc > 0)
                mDecelerating = true;
            else if (acc < 0)
                mAccelerating = true;
            else
                mConstantVelocity = true;

            mDirectionNegative = true;
        }
        else
        {
            if (acc)
                mAccelerating = true;
        }

        isDone = true;

        return MC_ErrorCode::GOOD;
    }

    void FbReadMotionState::onDisable(void)
    {
        memset(&mConstantVelocity, 0, &mDirectionNegative - &mConstantVelocity + sizeof(mDirectionNegative));
    }

    ////////////////////////////////////////////////////////////

    void FbReadAxisError::call(void)
    {
        mEnable ? onEnableTrue() : onEnableFalse();
    }

    MC_ErrorCode FbReadAxisError::onEnableTrue(void)
    {
        if (!mAxis)
            return MC_ErrorCode::AXISNOTEXIST;

        mValid = true;
        mError = false;
        mErrorID = mAxis->errorCode();
        mAxisErrorID = mAxis->devErrorCode();
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbReadAxisError::onEnableFalse(void)
    {
        mValid = false;
        mErrorID = MC_ErrorCode::GOOD;
        mAxisErrorID = 0;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReset::onAxisTriggered(bool &isDone)
    {
        return mAxis->resetError(isDone);
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadActualPosition::onAxisEnable(bool &isDone)
    {
        mPosition = mAxis->actPosition();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadActualPosition::onDisable(void)
    {
        mPosition = 0;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadCommandPosition::onAxisEnable(bool &isDone)
    {
        mPosition = mAxis->cmdPosition();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadActualVelocity::onAxisEnable(bool &isDone)
    {
        mVelocity = mAxis->actVelocity();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadActualVelocity::onDisable(void)
    {
        mVelocity = 0;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadCommandVelocity::onAxisEnable(bool &isDone)
    {
        mVelocity = mAxis->cmdVelocity();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbEmergencyStop::onAxisTriggered(bool &isDone)
    {
        mAxis->emergStop(MC_ErrorCode::SOFTWAREEMGS);
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

} // namespace Uranus
