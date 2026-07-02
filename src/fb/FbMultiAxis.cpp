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

    void FbGroupStop::call(void)
    {
        const bool risingEdge = mExecute && !mExecutePrevious;
        if (risingEdge)
            mCommandAborted = false;

        if (mCommandAborted)
        {
            if (!mExecute)
            {
                mCommandAborted = false;
                mDone = false;
                mBusy = false;
                clearError();
            }
            mExecutePrevious = mExecute;
            return;
        }

        FbComExecuteType::call();
        if (mStopRequested && !mExecute)
        {
            if (mAxesGroup)
                mAxesGroup->releaseStop(this);
            if (mAxesGroup && mAxesGroup->stopOwnedBy(this))
                mBusy = true;
            else
                mStopRequested = false;
        }
        mExecutePrevious = mExecute;
    }

    MC_ErrorCode FbGroupStop::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        err = mAxesGroup->startStop(this, mDeceleration, mJerk);
        if (err != MC_ErrorCode::GOOD)
            return err;

        mStopRequested = true;
        isDone = mAxesGroup->stopComplete();
        return MC_ErrorCode::GOOD;
    }

    void FbGroupStop::onOperationAborted(int32_t customId)
    {
        (void)customId;
        mDone = false;
        mBusy = false;
        mCommandAborted = true;
        mStopRequested = false;
        clearError();
    }

    MC_ErrorCode FbGroupReset::onExecTriggered(bool &isDone)
    {
        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;

        return mAxesGroup->reset(isDone);
    }

    MC_ErrorCode FbGroupLinearMoveType::onExecPosedge(void)
    {
        clearCommandAcceptance();

        MC_ErrorCode err = requireGroup(mAxesGroup);
        if (err != MC_ErrorCode::GOOD)
            return err;
        if (mCoordSystem != MC_CoordSystem::ACS || mTransitionVelocity != MC_TransitionVelocity::ZERO ||
            mTransitionMode != MC_TransitionMode::NONE || mOrientationMode != MC_OrientationMode::LINEAR)
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        for (double parameter : mTransitionParameter)
        {
            if (parameter != 0.0)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;
        }

        MC_COMMAND_ID commandId = 0;
        err = mAxesGroup->addLinearMove(
            this,
            positionRef(),
            isRelative(),
            mVelocity,
            mAcceleration,
            mDeceleration,
            mJerk,
            mBufferMode,
            commandId);
        if (err != MC_ErrorCode::GOOD)
            return err;

        mCommandAccepted = true;
        mCommandID = commandId;
        return MC_ErrorCode::GOOD;
    }

    void FbGroupLinearMoveType::onOperationActive(int32_t customId)
    {
        if (isCurrentCommand(customId))
            FbSeqExecuteType::onOperationActive(customId);
    }

    void FbGroupLinearMoveType::onOperationAborted(int32_t customId)
    {
        if (!isCurrentCommand(customId))
            return;
        FbSeqExecuteType::onOperationAborted(customId);
        clearCommandAcceptance();
    }

    void FbGroupLinearMoveType::onOperationDone(int32_t customId)
    {
        if (!isCurrentCommand(customId))
            return;
        FbSeqExecuteType::onOperationDone(customId);
        clearCommandAcceptance();
    }

    void FbGroupLinearMoveType::onOperationError(MC_ErrorCode errorCode, int32_t customId)
    {
        if (customId != 0 && !isCurrentCommand(customId))
            return;
        FbSeqExecuteType::onOperationError(errorCode, customId);
        clearCommandAcceptance();
    }

    bool FbGroupLinearMoveType::isCurrentCommand(int32_t customId) const
    {
        return mCommandID != 0 && static_cast<MC_COMMAND_ID>(customId) == mCommandID;
    }

    void FbGroupLinearMoveType::clearCommandAcceptance(void)
    {
        mCommandAccepted = false;
        mCommandID = 0;
    }

    const MC_POS_REF &FbMoveLinearAbsolute::positionRef(void) const
    {
        return mPosition;
    }

    bool FbMoveLinearAbsolute::isRelative(void) const
    {
        return false;
    }

    const MC_POS_REF &FbMoveLinearRelative::positionRef(void) const
    {
        return mDistance;
    }

    bool FbMoveLinearRelative::isRelative(void) const
    {
        return true;
    }

    MC_ErrorCode FbCombineAxes::onExecPosedge(void)
    {
        if (!mMaster1 || !mMaster2 || !mSlave)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (mMaster1 == mMaster2 || mMaster1 == mSlave || mMaster2 == mSlave)
            return MC_ErrorCode::AXIS_ALREADY_IN_GROUP;

        return mSlave->addCombineAxes(
            this,
            mMaster1,
            mMaster2,
            mGearRatioNumeratorM1,
            mGearRatioDenominatorM1,
            mGearRatioNumeratorM2,
            mGearRatioDenominatorM2,
            mCombineMode,
            mMasterValueSourceM1,
            mMasterValueSourceM2,
            mBufferMode);
    }

    void FbCombineAxes::onOperationDone(int32_t customId)
    {
        mCommandAborted = false;
        mBusy = mActive = mDone = true;
        clearError();
    }

    MC_ErrorCode FbGearIn::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addGearIn(this, mMaster, mRatioNumerator, mRatioDenominator, mMasterValueSource, mBufferMode);
    }

    MC_ErrorCode FbGearInPos::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addGearInPos(this, mMaster, mRatioNumerator, mRatioDenominator, mMasterSyncPosition,
                                    mSlaveSyncPosition, mMasterStartDistance, mVelocity, mAcceleration,
                                    mDeceleration, mJerk, mMasterValueSource, mBufferMode);
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
            mPhaseVelocity = mVelocity;
            mPhaseAcceleration = mAcceleration;
            mPhaseDeceleration = mDeceleration;
            mPhaseJerk = mJerk;
            mPhaseTargetValid = true;
        }

        return mAxis->moveGearPhaseOffset(
            mPhaseTarget, mPhaseVelocity, mPhaseAcceleration, mPhaseDeceleration, mPhaseJerk, isDone);
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
            mPhaseVelocity = mVelocity;
            mPhaseAcceleration = mAcceleration;
            mPhaseDeceleration = mDeceleration;
            mPhaseJerk = mJerk;
            mPhaseTargetValid = true;
        }

        return mAxis->moveGearPhaseOffset(
            mPhaseTarget, mPhaseVelocity, mPhaseAcceleration, mPhaseDeceleration, mPhaseJerk, isDone);
    }

    MC_ErrorCode FbCamTableSelect::onExecTriggered(bool &isDone)
    {
        if (!mCamTable || mCamTable->empty() || !mCamTable->valid())
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
