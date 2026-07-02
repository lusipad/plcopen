/*
 * FbMultiAxis.h
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

#ifndef PLCOPEN_FBMULTIAXIS_HPP_
#define PLCOPEN_FBMULTIAXIS_HPP_

#include "FbPLCOpenBase.h"
#include "CamTable.h"

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    class FbAddAxisToGroup : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT AXIS_REF mAxis = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    class FbRemoveAxisFromGroup : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT AXIS_REF mAxis = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    class FbGroupEnable : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    class FbGroupDisable : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    class FbGroupReadStatus : public FbReadInfoType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;

        FB_OUTPUT BOOL mErrorStop = false;
        FB_OUTPUT BOOL mDisabled = false;
        FB_OUTPUT BOOL mStopping = false;
        FB_OUTPUT BOOL mHoming = false;
        FB_OUTPUT BOOL mStandby = false;
        FB_OUTPUT BOOL mMoving = false;

    public:
        MC_ErrorCode onEnable(bool &isDone);
        void onDisable(void);
    };

    class FbGroupReadActualPosition : public FbReadInfoType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT UINT mAxisIndex = 0;

        FB_OUTPUT LREAL mPosition = 0.0;

    public:
        MC_ErrorCode onEnable(bool &isDone);
        void onDisable(void);
    };

    class FbGroupReadCommandPosition : public FbReadInfoType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT UINT mAxisIndex = 0;

        FB_OUTPUT LREAL mPosition = 0.0;

    public:
        MC_ErrorCode onEnable(bool &isDone);
        void onDisable(void);
    };

    /** Stops the axes group on its current path and holds GroupStopping while Execute is true. */
    class FbGroupStop : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT LREAL mDeceleration = 0.0;
        FB_INPUT LREAL mJerk = 0.0;

        FB_OUTPUT BOOL mCommandAborted = false;

    public:
        void call(void) override;
        MC_ErrorCode onExecTriggered(bool &isDone);
        void onOperationAborted(int32_t customId) override;

    private:
        bool mExecutePrevious = false;
        bool mStopRequested = false;
    };

    class FbGroupReset : public FbComExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    /** Common PLCopen Part 4 contract for coordinated linear group moves. */
    class FbGroupLinearMoveType : public FbSeqExecuteType
    {
    public:
        FB_INPUT AXES_GROUP_REF mAxesGroup = nullptr;
        FB_INPUT LREAL mVelocity = 0.0;
        FB_INPUT LREAL mAcceleration = 0.0;
        FB_INPUT LREAL mDeceleration = 0.0;
        FB_INPUT LREAL mJerk = 0.0;
        FB_INPUT MC_COORD_SYSTEM mCoordSystem = MC_CoordSystem::ACS;
        FB_INPUT MC_BUFFER_MODE mBufferMode = MC_BufferMode::ABORTING;
        FB_INPUT MC_TRANSITION_VELOCITY mTransitionVelocity = MC_TransitionVelocity::ZERO;
        FB_INPUT MC_TRANSITION_MODE mTransitionMode = MC_TransitionMode::NONE;
        FB_INPUT MC_TRANSITION_PARAMETER mTransitionParameter = {0};
        FB_INPUT MC_ORIENTATION_MODE mOrientationMode = MC_OrientationMode::LINEAR;

        FB_OUTPUT BOOL mCommandAccepted = false;
        FB_OUTPUT MC_COMMAND_ID mCommandID = 0;

    public:
        MC_ErrorCode onExecPosedge(void) override final;
        void onOperationActive(int32_t customId) override;
        void onOperationAborted(int32_t customId) override;
        void onOperationDone(int32_t customId) override;
        void onOperationError(MC_ErrorCode errorCode, int32_t customId) override;

    protected:
        virtual const MC_POS_REF &positionRef(void) const = 0;
        virtual bool isRelative(void) const = 0;

    private:
        bool isCurrentCommand(int32_t customId) const;
        void clearCommandAcceptance(void);
    };

    /** Executes an ACS linear move to an absolute member-position vector. */
    class FbMoveLinearAbsolute : public FbGroupLinearMoveType
    {
    public:
        FB_INPUT MC_POS_REF mPosition{};

    protected:
        const MC_POS_REF &positionRef(void) const override;
        bool isRelative(void) const override;
    };

    /** Executes an ACS linear move by a relative member-distance vector. */
    class FbMoveLinearRelative : public FbGroupLinearMoveType
    {
    public:
        FB_INPUT MC_DISTANCE_REF mDistance{};

    protected:
        const MC_POS_REF &positionRef(void) const override;
        bool isRelative(void) const override;
    };

    class FbCombineAxes : public FbSeqExecuteType
    {
    public:
        FB_INPUT AXIS_REF mMaster1 = nullptr;
        FB_INPUT AXIS_REF mMaster2 = nullptr;
        FB_INPUT AXIS_REF mSlave = nullptr;
        FB_INPUT BOOL mContinuousUpdate = false;
        FB_INPUT MC_COMBINE_MODE mCombineMode = MC_CombineMode::mcAddAxes;
        FB_INPUT LREAL mGearRatioNumeratorM1 = 1.0;
        FB_INPUT LREAL mGearRatioDenominatorM1 = 1.0;
        FB_INPUT LREAL mGearRatioNumeratorM2 = 1.0;
        FB_INPUT LREAL mGearRatioDenominatorM2 = 1.0;
        FB_INPUT MC_SOURCE mMasterValueSourceM1 = MC_Source::SETVALUE;
        FB_INPUT MC_SOURCE mMasterValueSourceM2 = MC_Source::SETVALUE;
        FB_INPUT MC_BUFFER_MODE mBufferMode = MC_BufferMode::ABORTING;

        FB_OUTPUT BOOL &mInSync = mDone;

    public:
        MC_ErrorCode onExecPosedge(void);
        void onOperationDone(int32_t customId);
    };

    class FbGearIn : public FbExecAxisBufferContSyncType
    {
    public:
        FB_INPUT LREAL mRatioNumerator = 1.0;
        FB_INPUT LREAL mRatioDenominator = 1.0;
        FB_INPUT MC_SOURCE mMasterValueSource = MC_Source::SETVALUE;

    public:
        MC_ErrorCode onMasterSlaveExecPosedge(void);
    };

    class FbGearInPos : public FbExecAxisBufferContSyncType
    {
    public:
        FB_INPUT LREAL mRatioNumerator = 1.0;
        FB_INPUT LREAL mRatioDenominator = 1.0;
        FB_INPUT LREAL mMasterSyncPosition = 0.0;
        FB_INPUT LREAL mSlaveSyncPosition = 0.0;
        FB_INPUT LREAL mMasterStartDistance = 0.0;
        FB_INPUT LREAL mVelocity = 0.0;
        FB_INPUT LREAL mAcceleration = 0.0;
        FB_INPUT LREAL mDeceleration = 0.0;
        FB_INPUT LREAL mJerk = 0.0;
        FB_INPUT MC_SOURCE mMasterValueSource = MC_Source::SETVALUE;

    public:
        MC_ErrorCode onMasterSlaveExecPosedge(void);
    };

    class FbGearOut : public FbExecAxisType
    {
    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    class FbPhasingAbsolute : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT LREAL mPhaseShift = 0.0;
        FB_INPUT LREAL mVelocity = 0.0;
        FB_INPUT LREAL mAcceleration = 0.0;
        FB_INPUT LREAL mDeceleration = 0.0;
        FB_INPUT LREAL mJerk = 0.0;

    public:
        void call(void);
        MC_ErrorCode onAxisTriggered(bool &isDone);

    private:
        BOOL mPhaseTargetValid = false;
        LREAL mPhaseTarget = 0.0;
        LREAL mPhaseVelocity = 0.0;
        LREAL mPhaseAcceleration = 0.0;
        LREAL mPhaseDeceleration = 0.0;
        LREAL mPhaseJerk = 0.0;
    };

    class FbPhasingRelative : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT LREAL mPhaseShift = 0.0;
        FB_INPUT LREAL mVelocity = 0.0;
        FB_INPUT LREAL mAcceleration = 0.0;
        FB_INPUT LREAL mDeceleration = 0.0;
        FB_INPUT LREAL mJerk = 0.0;

    public:
        void call(void);
        MC_ErrorCode onAxisTriggered(bool &isDone);

    private:
        BOOL mPhaseTargetValid = false;
        LREAL mPhaseTarget = 0.0;
        LREAL mPhaseVelocity = 0.0;
        LREAL mPhaseAcceleration = 0.0;
        LREAL mPhaseDeceleration = 0.0;
        LREAL mPhaseJerk = 0.0;
    };

    class FbCamTableSelect : public FbComExecuteType
    {
    public:
        FB_INPUT MC_CAM_REF mCamTable = nullptr;
        FB_OUTPUT MC_CAM_REF mCamTableSelected = nullptr;

    public:
        MC_ErrorCode onExecTriggered(bool &isDone);
    };

    class FbCamIn : public FbExecAxisBufferContSyncType
    {
    public:
        FB_INPUT MC_CAM_REF mCamTable = nullptr;
        FB_INPUT LREAL mMasterSyncPosition = 0.0;
        FB_INPUT LREAL mMasterStartDistance = 0.0;
        FB_INPUT LREAL mMasterOffset = 0.0;
        FB_INPUT LREAL mSlaveOffset = 0.0;
        FB_INPUT LREAL mMasterScaling = 1.0;
        FB_INPUT LREAL mSlaveScaling = 1.0;
        FB_INPUT MC_SOURCE mMasterValueSource = MC_Source::SETVALUE;

    public:
        MC_ErrorCode onMasterSlaveExecPosedge(void);
    };

    class FbCamOut : public FbExecAxisType
    {
    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

#pragma pack(pop)

} // namespace plcopen

#endif /** PLCOPEN_FBMULTIAXIS_HPP_ **/
