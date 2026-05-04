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

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    /**
     * @brief Single-axis power block.
     */
    class FbPower : public FbBaseType
    {
    public:
        FB_INPUT AXIS_REF mAxis = nullptr;

        FB_INPUT BOOL mEnable = false;
        FB_INPUT BOOL mEnablePositive = false;
        FB_INPUT BOOL mEnableNegative = false;

        FB_OUTPUT BOOL mStatus = false;
        FB_OUTPUT BOOL mValid = false;

    public:
        void call(void);

        void onOperationError(MC_ErrorCode errorCode, int32_t customId);
    };

    /**
     * @brief Single-axis homing block.
     */
    class FbHome : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mPosition = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    /**
     * @brief Controlled stop block.
     */
    class FbStop : public FbExecAxisType
    {
    public:
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
        void onExecNegedge(void);
    };

    /**
     * @brief Single-axis halt block.
     */
    class FbHalt : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    /**
     * @brief Halt the current single-axis superimposed motion approximation.
     *
     * This stops only the independent superimposed offset trajectory and
     * leaves the base motion command active.
     */
    class FbHaltSuperimposed : public FbExecAxisBufferType
    {
    public:
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        MC_ErrorCode onAxisExecPosedge(void);
    };

    /**
     * @brief Absolute move block.
     */
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

    /**
     * @brief Relative move block.
     */
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

    /**
     * @brief Additive move block.
     */
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

    /**
     * @brief Superimposed move block.
     *
     * The current single-axis implementation runs an independent offset
     * trajectory and layers it over the base motion command.
     */
    class FbMoveSuperimposed : public FbExecAxisBufferType
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

    /**
     * @brief Velocity move block.
     *
     * `mInVelocity` aliases `mDone` and indicates that the target velocity has
     * been reached.
     */
    class FbMoveVelocity : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;
        FB_INPUT MC_DIRECTION mDirection = MC_Direction::CURRENT;

        FB_OUTPUT BOOL &mInVelocity = mDone;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        LREAL mLastVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        MC_DIRECTION mLastDirection = MC_Direction::CURRENT;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Absolute move that reaches the target position with a non-zero end velocity.
     */
    class FbMoveContinuousAbsolute : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT LREAL mPosition = 0;
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mEndVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;
        FB_INPUT MC_DIRECTION mDirection = MC_Direction::CURRENT;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        LREAL mLastPosition = 0;
        LREAL mLastVelocity = 0;
        LREAL mLastEndVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        MC_DIRECTION mLastDirection = MC_Direction::CURRENT;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Relative move that reaches the target offset with a non-zero end velocity.
     */
    class FbMoveContinuousRelative : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT LREAL mDistance = 0;
        FB_INPUT LREAL mVelocity = 0;
        FB_INPUT LREAL mEndVelocity = 0;
        FB_INPUT LREAL mAcceleration = 0;
        FB_INPUT LREAL mDeceleration = 0;
        FB_INPUT LREAL mJerk = 0;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        LREAL mCommandStartPosition = 0;
        LREAL mLastDistance = 0;
        LREAL mLastVelocity = 0;
        LREAL mLastEndVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Minimal linked position profile reference.
     *
     * Each entry describes one segment. `mNext` can link another segment for
     * sequential profile table execution.
     */
    struct MC_PositionProfileData
    {
        LREAL mPosition = 0;
        LREAL mVelocity = 0;
        LREAL mAcceleration = 0;
        LREAL mDeceleration = 0;
        LREAL mJerk = 0;
        LREAL mDuration = 0;
        MC_ShiftingMode mShiftingMode = MC_ShiftingMode::ABSOLUTE;
        MC_Direction mDirection = MC_Direction::CURRENT;
        MC_PositionProfileData *mNext = nullptr;
    };

    typedef MC_PositionProfileData *MC_POSITION_PROFILE_REF;

    /**
     * @brief Execute a minimal linked position profile reference.
     */
    class FbPositionProfile : public FbExecAxisBufferType
    {
    public:
        FB_INPUT MC_POSITION_PROFILE_REF mPositionProfile = nullptr;
        FB_INPUT LREAL mTimeScale = 1.0;
        FB_INPUT LREAL mPositionScale = 1.0;
        FB_INPUT LREAL mPositionOffset = 0.0;
        FB_INPUT BOOL mContinuousUpdate = false;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);
        void onOperationDone(int32_t customId);
        void onOperationAborted(int32_t customId);

    private:
        void resetTimedState(void);
        MC_ErrorCode startTimedSegment(MC_POSITION_PROFILE_REF profile);
        void processTimedSegment(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        MC_POSITION_PROFILE_REF mActivePositionProfile = nullptr;
        LREAL mCommandStartPosition = 0;
        LREAL mTimedSegmentStartPosition = 0;
        LREAL mTimedSegmentTargetPosition = 0;
        LREAL mTimedSegmentElapsed = 0;
        LREAL mTimedSegmentDuration = 0;
        bool mTimedSegmentDonePending = false;
        LREAL mLastPosition = 0;
        LREAL mLastVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        MC_ShiftingMode mLastShiftingMode = MC_ShiftingMode::ABSOLUTE;
        MC_Direction mLastDirection = MC_Direction::CURRENT;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Minimal linked velocity profile reference.
     *
     * Each entry describes one target velocity segment. `mNext` can link
     * another segment for sequential profile table execution.
     */
    struct MC_VelocityProfileData
    {
        LREAL mVelocity = 0;
        LREAL mAcceleration = 0;
        LREAL mDeceleration = 0;
        LREAL mJerk = 0;
        LREAL mDuration = 0;
        MC_VelocityProfileData *mNext = nullptr;
    };

    typedef MC_VelocityProfileData *MC_VELOCITY_PROFILE_REF;

    /**
     * @brief Execute a minimal linked velocity profile reference.
     */
    class FbVelocityProfile : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT MC_VELOCITY_PROFILE_REF mVelocityProfile = nullptr;
        FB_INPUT LREAL mTimeScale = 1.0;
        FB_INPUT LREAL mVelocityScale = 1.0;
        FB_INPUT LREAL mVelocityOffset = 0.0;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);
        void onOperationDone(int32_t customId);
        void onOperationAborted(int32_t customId);

    private:
        void resetTimedState(void);
        MC_ErrorCode startTimedSegment(MC_VELOCITY_PROFILE_REF profile);
        void processTimedSegment(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        MC_VELOCITY_PROFILE_REF mActiveVelocityProfile = nullptr;
        LREAL mTimedSegmentElapsed = 0;
        LREAL mTimedSegmentDuration = 0;
        LREAL mLastVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Minimal linked acceleration profile reference.
     *
     * Each entry describes one velocity target with acceleration parameters.
     * `mNext` can link another segment for sequential profile table execution.
     */
    struct MC_AccelerationProfileData
    {
        LREAL mVelocity = 0;
        LREAL mAcceleration = 0;
        LREAL mDeceleration = 0;
        LREAL mJerk = 0;
        LREAL mDuration = 0;
        MC_AccelerationProfileData *mNext = nullptr;
    };

    typedef MC_AccelerationProfileData *MC_ACCELERATION_PROFILE_REF;

    /**
     * @brief Execute a minimal linked acceleration profile reference.
     */
    class FbAccelerationProfile : public FbExecAxisBufferContType
    {
    public:
        FB_INPUT MC_ACCELERATION_PROFILE_REF mAccelerationProfile = nullptr;
        FB_INPUT LREAL mTimeScale = 1.0;
        FB_INPUT LREAL mAccelerationScale = 1.0;
        FB_INPUT LREAL mAccelerationOffset = 0.0;

    public:
        void call(void);
        MC_ErrorCode onAxisExecPosedge(void);
        void onOperationDone(int32_t customId);
        void onOperationAborted(int32_t customId);

    private:
        void resetTimedState(void);
        MC_ErrorCode startTimedSegment(MC_ACCELERATION_PROFILE_REF profile);
        void processTimedSegment(void);

    private:
        bool mContinuousUpdateSnapshotValid = false;
        MC_ACCELERATION_PROFILE_REF mActiveAccelerationProfile = nullptr;
        LREAL mTimedSegmentElapsed = 0;
        LREAL mTimedSegmentDuration = 0;
        LREAL mLastVelocity = 0;
        LREAL mLastAcceleration = 0;
        LREAL mLastDeceleration = 0;
        LREAL mLastJerk = 0;
        LREAL mLastOverride = 100;
    };

    /**
     * @brief Read the PLCopen axis state-machine state.
     */
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

    /**
     * @brief Read the current motion-state classification.
     */
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

    /**
     * @brief Read the underlying servo error code.
     */
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

    /**
     * @brief Reset the axis error state.
     */
    class FbReset : public FbWriteInfoAxisType
    {
    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Set the current user-space position while the axis is idle.
     */
    class FbSetPosition : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT LREAL mPosition = 0;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Set the motion override percentage for newly planned moves.
     */
    class FbSetOverride : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT LREAL mOverride = 100.0;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    class FbTouchProbe : public FbBaseType
    {
    public:
        FB_INPUT AXIS_REF mAxis = nullptr;
        FB_INPUT BOOL mExecute = false;
        FB_INPUT UINT mTriggerInput = 0;
        FB_INPUT BOOL mWindowOnly = false;
        FB_INPUT LREAL mFirstPosition = 0.0;
        FB_INPUT LREAL mLastPosition = 0.0;

        FB_OUTPUT BOOL mDone = false;
        FB_OUTPUT BOOL mBusy = false;
        FB_OUTPUT BOOL mActive = false;
        FB_OUTPUT BOOL mCommandAborted = false;
        FB_OUTPUT LREAL mRecordedPosition = 0.0;

    public:
        void call(void);

    private:
        bool mExecuteTrigger = false;
    };

    class FbAbortTrigger : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT UINT mTriggerInput = 0;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Parameter enum used by `MC_ReadParameter` style APIs.
     */
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
    };

    /**
     * @brief Read the actual position.
     */
    class FbReadActualPosition : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT LREAL mPosition = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read the commanded position.
     */
    class FbReadCommandPosition : public FbReadActualPosition
    {
    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
    };

    /**
     * @brief Read the actual velocity.
     */
    class FbReadActualVelocity : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT LREAL mVelocity = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read the commanded velocity.
     */
    class FbReadCommandVelocity : public FbReadActualVelocity
    {
    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
    };

    /**
     * @brief Read the actual torque.
     */
    class FbReadActualTorque : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT LREAL mTorque = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read a supported axis parameter by enum id.
     */
    class FbReadParameter : public FbReadInfoAxisType
    {
    public:
        FB_INPUT MC_Parameter mParameterNumber = MC_Parameter::COMMANDED_POSITION;
        FB_OUTPUT LREAL mValue = 0;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read a supported boolean axis parameter by enum id.
     */
    class FbReadBoolParameter : public FbReadInfoAxisType
    {
    public:
        FB_INPUT MC_Parameter mParameterNumber = MC_Parameter::ENABLE_LIMIT_POS;
        FB_OUTPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read static and runtime axis capability/status flags.
     */
    class FbReadAxisInfo : public FbReadInfoAxisType
    {
    public:
        FB_OUTPUT BOOL mHomeAbsSwitch = false;
        FB_OUTPUT BOOL mLimitSwitchPos = false;
        FB_OUTPUT BOOL mLimitSwitchNeg = false;
        FB_OUTPUT BOOL mSimulation = false;
        FB_OUTPUT BOOL mCommunicationReady = false;
        FB_OUTPUT BOOL mReadyForPowerOn = false;
        FB_OUTPUT BOOL mPowerOn = false;
        FB_OUTPUT BOOL mIsHomed = false;
        FB_OUTPUT BOOL mAxisWarning = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Write a supported numeric axis parameter by enum id.
     */
    class FbWriteParameter : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT MC_Parameter mParameterNumber = MC_Parameter::SWLIMIT_POS;
        FB_INPUT LREAL mValue = 0;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Write a supported boolean axis parameter by enum id.
     */
    class FbWriteBoolParameter : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT MC_Parameter mParameterNumber = MC_Parameter::ENABLE_LIMIT_POS;
        FB_INPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Read a digital input through the servo extension channel.
     */
    class FbReadDigitalInput : public FbReadInfoAxisType
    {
    public:
        FB_INPUT UINT mInputNumber = 0;
        FB_OUTPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Read a digital output through the servo extension channel.
     */
    class FbReadDigitalOutput : public FbReadInfoAxisType
    {
    public:
        FB_INPUT UINT mOutputNumber = 0;
        FB_OUTPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);
    };

    /**
     * @brief Write a digital output through the servo extension channel.
     */
    class FbWriteDigitalOutput : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT UINT mOutputNumber = 0;
        FB_INPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    class FbDigitalCamSwitch : public FbReadInfoAxisType
    {
    public:
        FB_INPUT UINT mOutputNumber = 0;
        FB_INPUT LREAL mOnPosition = 0;
        FB_INPUT LREAL mOffPosition = 0;
        FB_INPUT LREAL mPeriod = 0;
        FB_OUTPUT BOOL mValue = false;

    public:
        MC_ErrorCode onAxisEnable(bool &isDone);
        void onDisable(void);

    private:
        BOOL mOutputActive = false;
        UINT mActiveOutputNumber = 0;
    };

    /**
     * @brief Write a torque setpoint to the underlying servo abstraction.
     */
    class FbTorqueControl : public FbWriteInfoAxisType
    {
    public:
        FB_INPUT LREAL mTorque = 0;
        FB_OUTPUT BOOL mInTorque = false;

    public:
        void call(void);
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

    /**
     * @brief Trigger the underlying servo emergency stop.
     */
    class FbEmergencyStop : public FbWriteInfoAxisType
    {
    public:
        MC_ErrorCode onAxisTriggered(bool &isDone);
    };

#pragma pack(pop)

}

#endif /** _URANUS_FBSINGLEAXIS_HPP_ **/
