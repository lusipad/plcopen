/*
 * FbMultiAxis.cpp
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

#include "FbMultiAxis.h"
#include "AxesGroup.h"
#include "Axis.h"

namespace plcopen
{
namespace
{
MC_ErrorCode requireGroup(AXES_GROUP_REF group)
{
    return group ? MC_ErrorCode::GOOD : MC_ErrorCode::PARAMETER_NOT_SUPPORT;
}
} // namespace

    MC_ErrorCode FbAddAxisToGroup::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!mAxis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        err = mAxesGroup->addAxis(mAxis);
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbRemoveAxisFromGroup::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!mAxis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        err = mAxesGroup->removeAxis(mAxis);
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbGroupEnable::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        err = mAxesGroup->enable();
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbGroupDisable::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        err = mAxesGroup->disable();
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbGroupReadStatus::onEnable(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        onDisable();

        switch (mAxesGroup->status())
        {
        case MC_GroupStatus::DISABLED:
            mDisabled = true;
            break;
        case MC_GroupStatus::STANDBY:
            mStandby = true;
            break;
        case MC_GroupStatus::HOMING:
            mHoming = true;
            break;
        case MC_GroupStatus::MOVING:
            mMoving = true;
            break;
        case MC_GroupStatus::STOPPING:
            mStopping = true;
            break;
        case MC_GroupStatus::ERRORSTOP:
            mErrorStop = true;
            break;
        }

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbGroupReadStatus::onDisable(void)
    {
        mErrorStop = false;
        mDisabled = false;
        mStopping = false;
        mHoming = false;
        mStandby = false;
        mMoving = false;
    }

    MC_ErrorCode FbGroupReadActualPosition::onEnable(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        Axis *axis = mAxesGroup->member(mAxisIndex);
        if (!axis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        mPosition = axis->actPosition();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbGroupReadActualPosition::onDisable(void)
    {
        mPosition = 0.0;
    }

    MC_ErrorCode FbGroupReadCommandPosition::onEnable(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        Axis *axis = mAxesGroup->member(mAxisIndex);
        if (!axis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        mPosition = axis->cmdPosition();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbGroupReadCommandPosition::onDisable(void)
    {
        mPosition = 0.0;
    }

    MC_ErrorCode FbGroupStop::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        err = mAxesGroup->stop();
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbGroupReset::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        return mAxesGroup->reset(isDone);
    }

    MC_ErrorCode FbCombineAxes::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (mAxis1 == mAxis2)
            return MC_ErrorCode::AXIS_ALREADY_IN_GROUP;

        err = mAxesGroup->canAddAxis(mAxis1);
        if (err != MC_ErrorCode::GOOD && err != MC_ErrorCode::AXIS_ALREADY_IN_GROUP)
            return err;

        err = mAxesGroup->canAddAxis(mAxis2);
        if (err != MC_ErrorCode::GOOD && err != MC_ErrorCode::AXIS_ALREADY_IN_GROUP)
            return err;

        err = mAxesGroup->addAxis(mAxis1);
        if (err != MC_ErrorCode::GOOD && err != MC_ErrorCode::AXIS_ALREADY_IN_GROUP)
            return err;

        err = mAxesGroup->addAxis(mAxis2);
        if (err != MC_ErrorCode::GOOD && err != MC_ErrorCode::AXIS_ALREADY_IN_GROUP)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbGearIn::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addGearIn(this, mMaster, mRatioNumerator, mRatioDenominator, mMasterValueSource, mBufferMode);
    }

    MC_ErrorCode FbGearInPos::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addGearInPos(this, mMaster, mRatioNumerator, mRatioDenominator, mMasterSyncPosition,
                                    mSlaveSyncPosition, mMasterValueSource, mBufferMode);
    }

    MC_ErrorCode FbGearOut::onAxisExecPosedge(void)
    {
        return mAxis->addGearOut(this);
    }

    void FbPhasingAbsolute::call(void)
    {
        if (!mExecute)
            mPhaseTargetValid = false;

        FbComExecuteType::call();
    }

    MC_ErrorCode FbPhasingAbsolute::onAxisTriggered(bool &isDone)
    {
        if (!mPhaseTargetValid)
        {
            mPhaseTarget = mPhaseShift;
            mPhaseTargetValid = true;
        }

        return mAxis->moveGearPhaseOffset(mPhaseTarget, mVelocity, mAcceleration, mDeceleration, mJerk, isDone);
    }

    void FbPhasingRelative::call(void)
    {
        if (!mExecute)
            mPhaseTargetValid = false;

        FbComExecuteType::call();
    }

    MC_ErrorCode FbPhasingRelative::onAxisTriggered(bool &isDone)
    {
        if (!mPhaseTargetValid)
        {
            mPhaseTarget = mAxis->gearPhaseOffset() + mPhaseShift;
            mPhaseTargetValid = true;
        }

        return mAxis->moveGearPhaseOffset(mPhaseTarget, mVelocity, mAcceleration, mDeceleration, mJerk, isDone);
    }

    MC_ErrorCode FbCamTableSelect::onExecTriggered(bool &isDone)
    {
        if (!mCamTable || mCamTable->empty())
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        mCamTableSelected = mCamTable;
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode FbCamIn::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addCamIn(this, mMaster, mCamTable, mMasterSyncPosition, mMasterStartDistance,
                                mMasterOffset, mSlaveOffset, mMasterScaling, mSlaveScaling,
                                mMasterValueSource, mBufferMode);
    }

    MC_ErrorCode FbCamOut::onAxisExecPosedge(void)
    {
        return mAxis->addCamOut(this);
    }

} // namespace plcopen
