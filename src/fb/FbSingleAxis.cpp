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

#include <cmath>
#include <cstring>

namespace plcopen
{
    namespace
    {
        constexpr int kDigitalInputBase = MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE;
        constexpr int kDigitalOutputBase = MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE;
        constexpr int kAxisInfoHomeSwitchInput = kDigitalInputBase;
        constexpr int kAxisInfoPositiveLimitInput = kDigitalInputBase + 1;
        constexpr int kAxisInfoNegativeLimitInput = kDigitalInputBase + 2;
        constexpr int kAxisInfoWarningInput = kDigitalInputBase + 3;
        constexpr double kVelocityReachedEpsilon = 1e-9;
        constexpr double kOverridePercentEpsilon = 1e-9;
        constexpr double kProfileSegmentVelocityEpsilon = 3e-2;

        bool readServoBool(Axis* axis, int index)
        {
            double value = 0;
            return axis->servoReadVal(index, value) && value != 0.0;
        }

        double applyVelocityDirection(double velocity, MC_DIRECTION direction)
        {
            switch (direction)
            {
            case MC_Direction::POSITIVE:
                return velocity;
            case MC_Direction::NEGATIVE:
                return -velocity;
            case MC_Direction::CURRENT:
            default:
                return velocity;
            }
        }

        bool moveVelocityDirectionSupported(MC_DIRECTION direction)
        {
            return direction == MC_Direction::POSITIVE ||
                   direction == MC_Direction::NEGATIVE ||
                   direction == MC_Direction::CURRENT;
        }

        bool profileScaleInputsValid(double timeScale, double valueScale, double valueOffset)
        {
            return timeScale > 0.0 && std::isfinite(timeScale) &&
                   valueScale != 0.0 && std::isfinite(valueScale) &&
                   std::isfinite(valueOffset);
        }

        struct PositionProfileCommand
        {
            double mPosition = 0.0;
            double mVelocity = 0.0;
            double mAcceleration = 0.0;
            double mDeceleration = 0.0;
            double mJerk = 0.0;
            double mDuration = 0.0;
            MC_ShiftingMode mShiftingMode = MC_ShiftingMode::ABSOLUTE;
            MC_Direction mDirection = MC_Direction::CURRENT;
        };

        struct VelocityProfileCommand
        {
            double mVelocity = 0.0;
            double mAcceleration = 0.0;
            double mDeceleration = 0.0;
            double mJerk = 0.0;
            double mDuration = 0.0;
        };

        bool profileDurationInputValid(double duration)
        {
            return duration >= 0.0 && std::isfinite(duration);
        }

        bool timedProfileActive(double duration)
        {
            return duration > 0.0;
        }

        MC_ErrorCode buildPositionProfileCommand(const MC_PositionProfileData *profile, double timeScale,
                                                 double positionScale, double positionOffset,
                                                 PositionProfileCommand &command)
        {
            if (!profile)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (!profileScaleInputsValid(timeScale, positionScale, positionOffset))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            const double scaleMagnitude = std::fabs(positionScale);
            const double timeScaleSquared = timeScale * timeScale;
            command.mPosition = positionOffset + profile->mPosition * positionScale;
            command.mVelocity = profile->mVelocity * scaleMagnitude / timeScale;
            command.mAcceleration = profile->mAcceleration * scaleMagnitude / timeScaleSquared;
            command.mDeceleration = profile->mDeceleration * scaleMagnitude / timeScaleSquared;
            command.mJerk = profile->mJerk * scaleMagnitude / (timeScaleSquared * timeScale);
            command.mDuration = profile->mDuration * timeScale;
            command.mShiftingMode = profile->mShiftingMode;
            command.mDirection = profile->mDirection;

            if (!profileDurationInputValid(command.mDuration))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (!std::isfinite(command.mPosition))
                return MC_ErrorCode::POS_ILLEGAL;

            return MC_ErrorCode::GOOD;
        }

        MC_ErrorCode buildVelocityProfileCommand(const MC_VelocityProfileData *profile, double timeScale,
                                                 double velocityScale, double velocityOffset,
                                                 VelocityProfileCommand &command)
        {
            if (!profile)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (!profileScaleInputsValid(timeScale, velocityScale, velocityOffset))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            const double scaleMagnitude = std::fabs(velocityScale);
            command.mVelocity = velocityOffset + profile->mVelocity * velocityScale;
            command.mAcceleration = profile->mAcceleration * scaleMagnitude / timeScale;
            command.mDeceleration = profile->mDeceleration * scaleMagnitude / timeScale;
            command.mJerk = profile->mJerk * scaleMagnitude / (timeScale * timeScale);

            command.mDuration = profile->mDuration * timeScale;
            if (!profileDurationInputValid(command.mDuration))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            return MC_ErrorCode::GOOD;
        }

        MC_ErrorCode buildAccelerationProfileCommand(const MC_AccelerationProfileData *profile, double timeScale,
                                                     double accelerationScale, double accelerationOffset,
                                                     VelocityProfileCommand &command)
        {
            if (!profile)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (!profileScaleInputsValid(timeScale, accelerationScale, accelerationOffset))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            const double scaleMagnitude = std::fabs(accelerationScale);
            command.mVelocity = profile->mVelocity;
            command.mAcceleration = accelerationOffset + profile->mAcceleration * scaleMagnitude / timeScale;
            command.mDeceleration = accelerationOffset + profile->mDeceleration * scaleMagnitude / timeScale;
            command.mJerk = profile->mJerk * scaleMagnitude / (timeScale * timeScale);

            command.mDuration = profile->mDuration * timeScale;
            if (!profileDurationInputValid(command.mDuration))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            return MC_ErrorCode::GOOD;
        }
    }

    void FbPower::call(void)
    {
        if (!mAxis)
        {
            onOperationError(MC_ErrorCode::AXIS_NO_TEXIST, 0);
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

    MC_ErrorCode FbHaltSuperimposed::onAxisExecPosedge(void)
    {
        return mAxis->addHaltSuperimposed(this, mDeceleration, mJerk, mBufferMode);
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

    MC_ErrorCode FbMoveSuperimposed::onAxisExecPosedge(void)
    {
        return mAxis->addMoveSuperimposed(this, mDistance, mVelocity, mAcceleration, mDeceleration, mJerk, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    void FbMoveVelocity::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            return;
        }

        if (mError || !mActive)
            return;

        if (!mContinuousUpdateSnapshotValid)
        {
            mLastVelocity = mVelocity;
            mLastAcceleration = mAcceleration;
            mLastDeceleration = mDeceleration;
            mLastJerk = mJerk;
            mLastDirection = mDirection;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = mBusy;
            return;
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
        {
            const double targetVelocity = mAxis ? applyVelocityDirection(mLastVelocity, mLastDirection) *
                mAxis->override() * 0.01 : applyVelocityDirection(mLastVelocity, mLastDirection);
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kVelocityReachedEpsilon)
                mDone = true;
            return;
        }

        if (!moveVelocityDirectionSupported(mDirection))
        {
            onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
            return;
        }

        if (mVelocity == mLastVelocity && mAcceleration == mLastAcceleration &&
            mDeceleration == mLastDeceleration && mJerk == mLastJerk && mDirection == mLastDirection)
        {
            const double targetVelocity = mAxis ? applyVelocityDirection(mLastVelocity, mLastDirection) *
                mAxis->override() * 0.01 : applyVelocityDirection(mLastVelocity, mLastDirection);
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kVelocityReachedEpsilon)
                mDone = true;

            if (!overrideChanged)
                return;
        }

        MC_ErrorCode err = mAxis ? mAxis->updateMoveVel(this, applyVelocityDirection(mVelocity, mDirection),
                                     mAcceleration, mDeceleration, mJerk)
                                 : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastVelocity = mVelocity;
        mLastAcceleration = mAcceleration;
        mLastDeceleration = mDeceleration;
        mLastJerk = mJerk;
        mLastDirection = mDirection;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbMoveVelocity::onAxisExecPosedge(void)
    {
        if (!moveVelocityDirectionSupported(mDirection))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        return mAxis->addMoveVel(this, applyVelocityDirection(mVelocity, mDirection),
            mAcceleration, mDeceleration, mJerk, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    void FbMoveContinuousAbsolute::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            return;
        }

        if (mError || !mActive)
            return;

        if (!mContinuousUpdateSnapshotValid)
        {
            mLastPosition = mPosition;
            mLastVelocity = mVelocity;
            mLastEndVelocity = mEndVelocity;
            mLastAcceleration = mAcceleration;
            mLastDeceleration = mDeceleration;
            mLastJerk = mJerk;
            mLastDirection = mDirection;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = true;
            return;
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
            return;

        if (mPosition == mLastPosition && mVelocity == mLastVelocity && mEndVelocity == mLastEndVelocity &&
            mAcceleration == mLastAcceleration && mDeceleration == mLastDeceleration && mJerk == mLastJerk &&
            mDirection == mLastDirection)
        {
            if (!overrideChanged)
                return;
        }

        MC_ErrorCode err = mAxis ? mAxis->updateMovePosCont(this, mPosition, mVelocity, mAcceleration, mDeceleration,
                               mEndVelocity, mJerk, MC_ShiftingMode::ABSOLUTE, mDirection)
                                 : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastPosition = mPosition;
        mLastVelocity = mVelocity;
        mLastEndVelocity = mEndVelocity;
        mLastAcceleration = mAcceleration;
        mLastDeceleration = mDeceleration;
        mLastJerk = mJerk;
        mLastDirection = mDirection;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbMoveContinuousAbsolute::onAxisExecPosedge(void)
    {
        mLastPosition = mPosition;
        mLastVelocity = mVelocity;
        mLastEndVelocity = mEndVelocity;
        mLastAcceleration = mAcceleration;
        mLastDeceleration = mDeceleration;
        mLastJerk = mJerk;
        mLastDirection = mDirection;
        mLastOverride = mAxis->override();
        mContinuousUpdateSnapshotValid = true;
        return mAxis->addMovePosCont(this, mPosition, mVelocity, mAcceleration, mDeceleration, mEndVelocity, mJerk,
            MC_ShiftingMode::ABSOLUTE, mDirection, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    void FbMoveContinuousRelative::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            return;
        }

        if (mError || !mActive)
            return;

        if (!mContinuousUpdateSnapshotValid)
        {
            mCommandStartPosition = mAxis ? mAxis->cmdPosition() : 0;
            mLastDistance = mDistance;
            mLastVelocity = mVelocity;
            mLastEndVelocity = mEndVelocity;
            mLastAcceleration = mAcceleration;
            mLastDeceleration = mDeceleration;
            mLastJerk = mJerk;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = true;
            return;
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
            return;

        if (mDistance == mLastDistance && mVelocity == mLastVelocity && mEndVelocity == mLastEndVelocity &&
            mAcceleration == mLastAcceleration && mDeceleration == mLastDeceleration && mJerk == mLastJerk)
        {
            if (!overrideChanged)
                return;
        }

        const LREAL updatedPosition = mCommandStartPosition + mDistance;
        MC_ErrorCode err = mAxis ? mAxis->updateMovePosCont(this, updatedPosition, mVelocity, mAcceleration,
                               mDeceleration, mEndVelocity, mJerk, MC_ShiftingMode::ABSOLUTE, MC_Direction::CURRENT)
                                 : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastDistance = mDistance;
        mLastVelocity = mVelocity;
        mLastEndVelocity = mEndVelocity;
        mLastAcceleration = mAcceleration;
        mLastDeceleration = mDeceleration;
        mLastJerk = mJerk;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbMoveContinuousRelative::onAxisExecPosedge(void)
    {
        mCommandStartPosition = mAxis->cmdPosition();
        mLastDistance = mDistance;
        mLastVelocity = mVelocity;
        mLastEndVelocity = mEndVelocity;
        mLastAcceleration = mAcceleration;
        mLastDeceleration = mDeceleration;
        mLastJerk = mJerk;
        mLastOverride = mAxis->override();
        mContinuousUpdateSnapshotValid = true;
        return mAxis->addMovePosCont(this, mDistance, mVelocity, mAcceleration, mDeceleration, mEndVelocity, mJerk,
            MC_ShiftingMode::RELATIVE, MC_Direction::CURRENT, mBufferMode);
    }

    ////////////////////////////////////////////////////////////

    void FbPositionProfile::resetTimedState(void)
    {
        mTimedSegmentStartPosition = 0.0;
        mTimedSegmentTargetPosition = 0.0;
        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = 0.0;
        mTimedSegmentDonePending = false;
    }

    MC_ErrorCode FbPositionProfile::startTimedSegment(MC_POSITION_PROFILE_REF profile)
    {
        PositionProfileCommand command;
        MC_ErrorCode err = buildPositionProfileCommand(
            profile, mTimeScale, mPositionScale, mPositionOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        mCommandStartPosition = mAxis ? mAxis->cmdPosition() : 0;
        LREAL targetPosition = command.mPosition;
        if (command.mShiftingMode == MC_ShiftingMode::RELATIVE ||
            command.mShiftingMode == MC_ShiftingMode::ADDITIVE)
        {
            targetPosition = mCommandStartPosition + command.mPosition;
        }
        else
        {
            targetPosition = mAxis ? mAxis->userPosToSys(mCommandStartPosition, command.mPosition, command.mDirection)
                                   : command.mPosition;
        }

        mTimedSegmentStartPosition = mAxis ? mAxis->cmdPosition() : 0;
        mTimedSegmentTargetPosition = targetPosition;
        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = command.mDuration;
        mTimedSegmentDonePending = false;
        mLastPosition = command.mPosition;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastShiftingMode = command.mShiftingMode;
        mLastDirection = command.mDirection;
        mLastOverride = mAxis ? mAxis->override() : 100;
        mContinuousUpdateSnapshotValid = true;

        err = mAxis ? mAxis->setPosition(mTimedSegmentStartPosition, 0.0, 0.0)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
            return err;

        return mAxis ? mAxis->setStatus(MC_AxisStatus::DISCRETE_MOTION)
                     : MC_ErrorCode::AXIS_NO_TEXIST;
    }

    void FbPositionProfile::processTimedSegment(void)
    {
        if (!mAxis || !mActivePositionProfile || mTimedSegmentDuration <= 0.0)
            return;

        if (mTimedSegmentDonePending)
        {
            if (mActivePositionProfile->mNext)
            {
                mActivePositionProfile = mActivePositionProfile->mNext;
                MC_ErrorCode err = startTimedSegment(mActivePositionProfile);
                if (MC_ErrorCode::GOOD != err)
                {
                    onOperationError(err, 0);
                    return;
                }

                mDone = false;
                mBusy = true;
                mActive = true;
                mCommandAborted = false;
                clearError();
                return;
            }

            mActivePositionProfile = nullptr;
            resetTimedState();
            mAxis->setStatus(MC_AxisStatus::STANDSTILL);
            FbSeqExecuteType::onOperationDone(0);
            return;
        }

        mTimedSegmentElapsed += mAxis->sampleTime();
        const double ratio = std::min(1.0, mTimedSegmentElapsed / mTimedSegmentDuration);
        const double target = mTimedSegmentStartPosition +
            (mTimedSegmentTargetPosition - mTimedSegmentStartPosition) * ratio;
        const double velocity = (mTimedSegmentTargetPosition - mTimedSegmentStartPosition) / mTimedSegmentDuration;
        MC_ErrorCode err = mAxis->setPosition(target, ratio < 1.0 ? velocity : 0.0, 0.0);
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        if (ratio < 1.0)
        {
            mDone = false;
            mBusy = true;
            mActive = true;
            mCommandAborted = false;
            clearError();
            return;
        }

        mTimedSegmentDonePending = true;
        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        clearError();
    }

    void FbPositionProfile::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            mActivePositionProfile = nullptr;
            resetTimedState();
            return;
        }

        if (mError)
            return;

        if (mActivePositionProfile && mTimedSegmentDuration > 0.0)
        {
            processTimedSegment();
            return;
        }

        if (!mActive)
            return;

        PositionProfileCommand command;
        MC_ErrorCode err = buildPositionProfileCommand(
            mPositionProfile, mTimeScale, mPositionScale, mPositionOffset, command);
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        if (!mContinuousUpdateSnapshotValid)
        {
            mCommandStartPosition = mAxis ? mAxis->cmdPosition() : 0;
            mLastPosition = command.mPosition;
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastShiftingMode = command.mShiftingMode;
            mLastDirection = command.mDirection;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = true;
            return;
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
            return;

        if (command.mPosition == mLastPosition && command.mVelocity == mLastVelocity &&
            command.mAcceleration == mLastAcceleration &&
            command.mDeceleration == mLastDeceleration && command.mJerk == mLastJerk &&
            command.mShiftingMode == mLastShiftingMode && command.mDirection == mLastDirection)
        {
            if (!overrideChanged)
                return;
        }

        LREAL targetPosition = command.mPosition;
        MC_ShiftingMode shiftingMode = command.mShiftingMode;
        MC_Direction direction = command.mDirection;
        if (command.mShiftingMode == MC_ShiftingMode::RELATIVE ||
            command.mShiftingMode == MC_ShiftingMode::ADDITIVE)
        {
            targetPosition = mCommandStartPosition + command.mPosition;
            shiftingMode = MC_ShiftingMode::ABSOLUTE;
            direction = MC_Direction::CURRENT;
        }

        err = mAxis ? mAxis->updateMovePos(this, targetPosition, command.mVelocity,
                               command.mAcceleration, command.mDeceleration,
                               command.mJerk, shiftingMode, direction)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastPosition = command.mPosition;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mTimedSegmentDuration = command.mDuration;
        mLastShiftingMode = command.mShiftingMode;
        mLastDirection = command.mDirection;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbPositionProfile::onAxisExecPosedge(void)
    {
        PositionProfileCommand command;
        MC_ErrorCode err = buildPositionProfileCommand(
            mPositionProfile, mTimeScale, mPositionScale, mPositionOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        if (timedProfileActive(command.mDuration))
        {
            mActivePositionProfile = mPositionProfile;
            return startTimedSegment(mActivePositionProfile);
        }

        mCommandStartPosition = mAxis->cmdPosition();
        mLastPosition = command.mPosition;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastShiftingMode = command.mShiftingMode;
        mLastDirection = command.mDirection;
        mLastOverride = mAxis->override();
        mActivePositionProfile = mPositionProfile;
        mContinuousUpdateSnapshotValid = true;
        return mAxis->addMovePos(this, command.mPosition, command.mVelocity,
            command.mAcceleration, command.mDeceleration, command.mJerk,
            command.mShiftingMode, command.mDirection, mBufferMode);
    }

    void FbPositionProfile::onOperationDone(int32_t customId)
    {
        if (mActivePositionProfile && mTimedSegmentDuration > 0.0)
            return;

        if (mExecute && mActivePositionProfile && mActivePositionProfile->mNext)
        {
            mActivePositionProfile = mActivePositionProfile->mNext;
            PositionProfileCommand command;
            MC_ErrorCode err = buildPositionProfileCommand(
                mActivePositionProfile, mTimeScale, mPositionScale, mPositionOffset, command);
            if (MC_ErrorCode::GOOD == err)
            {
                err = mAxis ? mAxis->addMovePos(this, command.mPosition,
                                      command.mVelocity, command.mAcceleration,
                                      command.mDeceleration, command.mJerk,
                                      command.mShiftingMode, command.mDirection,
                                      MC_BufferMode::BUFFERED)
                            : MC_ErrorCode::AXIS_NO_TEXIST;
            }
            if (MC_ErrorCode::GOOD != err)
            {
                onOperationError(err, customId);
                return;
            }

            mDone = false;
            mBusy = true;
            mActive = false;
            mCommandAborted = false;
            mLastPosition = command.mPosition;
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastShiftingMode = command.mShiftingMode;
            mLastDirection = command.mDirection;
            mLastOverride = mAxis ? mAxis->override() : 100;
            clearError();
            return;
        }

        mActivePositionProfile = nullptr;
        resetTimedState();
        FbSeqExecuteType::onOperationDone(customId);
    }

    void FbPositionProfile::onOperationAborted(int32_t customId)
    {
        mActivePositionProfile = nullptr;
        resetTimedState();
        FbSeqExecuteType::onOperationAborted(customId);
    }

    ////////////////////////////////////////////////////////////

    void FbVelocityProfile::resetTimedState(void)
    {
        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = 0.0;
    }

    MC_ErrorCode FbVelocityProfile::startTimedSegment(MC_VELOCITY_PROFILE_REF profile)
    {
        VelocityProfileCommand command;
        MC_ErrorCode err = buildVelocityProfileCommand(
            profile, mTimeScale, mVelocityScale, mVelocityOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                               command.mAcceleration, command.mDeceleration,
                               command.mJerk)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
            return err;

        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = command.mDuration;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastOverride = mAxis ? mAxis->override() : 100;
        mContinuousUpdateSnapshotValid = true;
        return MC_ErrorCode::GOOD;
    }

    void FbVelocityProfile::processTimedSegment(void)
    {
        if (!mAxis || !mActiveVelocityProfile || mTimedSegmentDuration <= 0.0)
            return;

        const double targetVelocity = mLastVelocity * mAxis->override() * 0.01;
        if (std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kProfileSegmentVelocityEpsilon)
            mTimedSegmentElapsed += mAxis->sampleTime();

        if (mTimedSegmentElapsed < mTimedSegmentDuration)
            return;

        if (mActiveVelocityProfile->mNext)
        {
            mActiveVelocityProfile = mActiveVelocityProfile->mNext;
            MC_ErrorCode err = startTimedSegment(mActiveVelocityProfile);
            if (MC_ErrorCode::GOOD != err)
            {
                onOperationError(err, 0);
                return;
            }

            mDone = false;
            mBusy = true;
            mActive = true;
            mCommandAborted = false;
            clearError();
            return;
        }

        mActiveVelocityProfile = nullptr;
        resetTimedState();
        mDone = true;
    }

    void FbVelocityProfile::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            mActiveVelocityProfile = nullptr;
            resetTimedState();
            return;
        }

        if (mError || !mActive)
            return;

        if (mActiveVelocityProfile && mTimedSegmentDuration > 0.0)
        {
            processTimedSegment();
            return;
        }

        VelocityProfileCommand command;
        MC_ErrorCode err = buildVelocityProfileCommand(
            mVelocityProfile, mTimeScale, mVelocityScale, mVelocityOffset, command);
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        if (!mContinuousUpdateSnapshotValid)
        {
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = true;
            return;
        }

        if (mActiveVelocityProfile)
        {
            const double targetVelocity = mAxis ? mLastVelocity * mAxis->override() * 0.01 : mLastVelocity;
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kProfileSegmentVelocityEpsilon)
            {
                if (mActiveVelocityProfile->mNext)
                {
                    mActiveVelocityProfile = mActiveVelocityProfile->mNext;
                    err = buildVelocityProfileCommand(
                        mActiveVelocityProfile, mTimeScale, mVelocityScale, mVelocityOffset, command);
                    if (MC_ErrorCode::GOOD == err)
                    {
                        err = mAxis->updateMoveVel(this, command.mVelocity,
                            command.mAcceleration, command.mDeceleration,
                            command.mJerk);
                    }
                    if (MC_ErrorCode::GOOD != err)
                    {
                        onOperationError(err, 0);
                        return;
                    }

                    mLastVelocity = command.mVelocity;
                    mLastAcceleration = command.mAcceleration;
                    mLastDeceleration = command.mDeceleration;
                    mLastJerk = command.mJerk;
                    mLastOverride = mAxis ? mAxis->override() : 100;
                    mDone = false;
                    clearError();
                    return;
                }

                if (mActiveVelocityProfile != mVelocityProfile)
                {
                    mActiveVelocityProfile = nullptr;
                    mDone = true;
                    return;
                }
            }
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
            return;

        if (command.mVelocity == mLastVelocity && command.mAcceleration == mLastAcceleration &&
            command.mDeceleration == mLastDeceleration && command.mJerk == mLastJerk)
        {
            const double targetVelocity = mAxis ? mLastVelocity * mAxis->override() * 0.01 : mLastVelocity;
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kVelocityReachedEpsilon)
                mDone = true;

            if (!overrideChanged)
                return;
        }

        err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                               command.mAcceleration, command.mDeceleration,
                               command.mJerk)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mTimedSegmentDuration = command.mDuration;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbVelocityProfile::onAxisExecPosedge(void)
    {
        VelocityProfileCommand command;
        MC_ErrorCode err = buildVelocityProfileCommand(
            mVelocityProfile, mTimeScale, mVelocityScale, mVelocityOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        const bool isTimed = timedProfileActive(command.mDuration);
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastOverride = mAxis->override();
        mActiveVelocityProfile = mVelocityProfile;
        mContinuousUpdateSnapshotValid = true;
        mTimedSegmentDuration = command.mDuration;
        err = mAxis->addMoveVel(this, command.mVelocity, command.mAcceleration,
            command.mDeceleration, command.mJerk, mBufferMode);
        if (MC_ErrorCode::GOOD == err && !isTimed)
            resetTimedState();
        return err;
    }

    void FbVelocityProfile::onOperationDone(int32_t customId)
    {
        if (mActiveVelocityProfile && mTimedSegmentDuration > 0.0)
            return;

        if (mExecute && mActiveVelocityProfile && mActiveVelocityProfile->mNext)
        {
            mActiveVelocityProfile = mActiveVelocityProfile->mNext;
            VelocityProfileCommand command;
            MC_ErrorCode err = buildVelocityProfileCommand(
                mActiveVelocityProfile, mTimeScale, mVelocityScale, mVelocityOffset, command);
            if (MC_ErrorCode::GOOD == err)
            {
                err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                                      command.mAcceleration, command.mDeceleration,
                                      command.mJerk)
                            : MC_ErrorCode::AXIS_NO_TEXIST;
            }
            if (MC_ErrorCode::GOOD != err)
            {
                onOperationError(err, customId);
                return;
            }

            mDone = false;
            mBusy = true;
            mActive = true;
            mCommandAborted = false;
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastOverride = mAxis ? mAxis->override() : 100;
            clearError();
            return;
        }

        mActiveVelocityProfile = nullptr;
        resetTimedState();
        FbExecAxisBufferContType::onOperationDone(customId);
    }

    void FbVelocityProfile::onOperationAborted(int32_t customId)
    {
        mActiveVelocityProfile = nullptr;
        resetTimedState();
        FbSeqExecuteType::onOperationAborted(customId);
    }

    ////////////////////////////////////////////////////////////

    void FbAccelerationProfile::resetTimedState(void)
    {
        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = 0.0;
    }

    MC_ErrorCode FbAccelerationProfile::startTimedSegment(MC_ACCELERATION_PROFILE_REF profile)
    {
        VelocityProfileCommand command;
        MC_ErrorCode err = buildAccelerationProfileCommand(
            profile, mTimeScale, mAccelerationScale, mAccelerationOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                               command.mAcceleration, command.mDeceleration,
                               command.mJerk)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
            return err;

        mTimedSegmentElapsed = 0.0;
        mTimedSegmentDuration = command.mDuration;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastOverride = mAxis ? mAxis->override() : 100;
        mContinuousUpdateSnapshotValid = true;
        return MC_ErrorCode::GOOD;
    }

    void FbAccelerationProfile::processTimedSegment(void)
    {
        if (!mAxis || !mActiveAccelerationProfile || mTimedSegmentDuration <= 0.0)
            return;

        const double targetVelocity = mLastVelocity * mAxis->override() * 0.01;
        if (std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kProfileSegmentVelocityEpsilon)
            mTimedSegmentElapsed += mAxis->sampleTime();

        if (mTimedSegmentElapsed < mTimedSegmentDuration)
            return;

        if (mActiveAccelerationProfile->mNext)
        {
            mActiveAccelerationProfile = mActiveAccelerationProfile->mNext;
            MC_ErrorCode err = startTimedSegment(mActiveAccelerationProfile);
            if (MC_ErrorCode::GOOD != err)
            {
                onOperationError(err, 0);
                return;
            }

            mDone = false;
            mBusy = true;
            mActive = true;
            mCommandAborted = false;
            clearError();
            return;
        }

        mActiveAccelerationProfile = nullptr;
        resetTimedState();
        mDone = true;
    }

    void FbAccelerationProfile::call(void)
    {
        FbSeqExecuteType::call();

        if (!mExecute)
        {
            mContinuousUpdateSnapshotValid = false;
            mActiveAccelerationProfile = nullptr;
            resetTimedState();
            return;
        }

        if (mError || !mActive)
            return;

        if (mActiveAccelerationProfile && mTimedSegmentDuration > 0.0)
        {
            processTimedSegment();
            return;
        }

        VelocityProfileCommand command;
        MC_ErrorCode err = buildAccelerationProfileCommand(
            mAccelerationProfile, mTimeScale, mAccelerationScale, mAccelerationOffset, command);
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        if (!mContinuousUpdateSnapshotValid)
        {
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastOverride = mAxis ? mAxis->override() : 100;
            mContinuousUpdateSnapshotValid = true;
            return;
        }

        if (mActiveAccelerationProfile)
        {
            const double targetVelocity = mAxis ? mLastVelocity * mAxis->override() * 0.01 : mLastVelocity;
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kProfileSegmentVelocityEpsilon)
            {
                if (mActiveAccelerationProfile->mNext)
                {
                    mActiveAccelerationProfile = mActiveAccelerationProfile->mNext;
                    err = buildAccelerationProfileCommand(
                        mActiveAccelerationProfile, mTimeScale, mAccelerationScale, mAccelerationOffset, command);
                    if (MC_ErrorCode::GOOD == err)
                    {
                        err = mAxis->updateMoveVel(this, command.mVelocity,
                            command.mAcceleration, command.mDeceleration,
                            command.mJerk);
                    }
                    if (MC_ErrorCode::GOOD != err)
                    {
                        onOperationError(err, 0);
                        return;
                    }

                    mLastVelocity = command.mVelocity;
                    mLastAcceleration = command.mAcceleration;
                    mLastDeceleration = command.mDeceleration;
                    mLastJerk = command.mJerk;
                    mLastOverride = mAxis ? mAxis->override() : 100;
                    mDone = false;
                    clearError();
                    return;
                }

                if (mActiveAccelerationProfile != mAccelerationProfile)
                {
                    mActiveAccelerationProfile = nullptr;
                    mDone = true;
                    return;
                }
            }
        }

        const double currentOverride = mAxis ? mAxis->override() : 100;
        const bool overrideChanged = std::fabs(currentOverride - mLastOverride) > kOverridePercentEpsilon;
        if (!mContinuousUpdate && !overrideChanged)
            return;

        if (command.mVelocity == mLastVelocity &&
            command.mAcceleration == mLastAcceleration &&
            command.mDeceleration == mLastDeceleration && command.mJerk == mLastJerk)
        {
            const double targetVelocity = mAxis ? mLastVelocity * mAxis->override() * 0.01 : mLastVelocity;
            if (mAxis && std::fabs(mAxis->cmdVelocity() - targetVelocity) <= kVelocityReachedEpsilon)
                mDone = true;

            if (!overrideChanged)
                return;
        }

        err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                               command.mAcceleration, command.mDeceleration,
                               command.mJerk)
                    : MC_ErrorCode::AXIS_NO_TEXIST;
        if (MC_ErrorCode::GOOD != err)
        {
            onOperationError(err, 0);
            return;
        }

        mDone = false;
        mBusy = true;
        mActive = true;
        mCommandAborted = false;
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mTimedSegmentDuration = command.mDuration;
        mLastOverride = currentOverride;
        clearError();
    }

    MC_ErrorCode FbAccelerationProfile::onAxisExecPosedge(void)
    {
        VelocityProfileCommand command;
        MC_ErrorCode err = buildAccelerationProfileCommand(
            mAccelerationProfile, mTimeScale, mAccelerationScale, mAccelerationOffset, command);
        if (MC_ErrorCode::GOOD != err)
            return err;

        const bool isTimed = timedProfileActive(command.mDuration);
        mLastVelocity = command.mVelocity;
        mLastAcceleration = command.mAcceleration;
        mLastDeceleration = command.mDeceleration;
        mLastJerk = command.mJerk;
        mLastOverride = mAxis->override();
        mActiveAccelerationProfile = mAccelerationProfile;
        mContinuousUpdateSnapshotValid = true;
        mTimedSegmentDuration = command.mDuration;
        err = mAxis->addMoveVel(this, command.mVelocity, command.mAcceleration,
            command.mDeceleration, command.mJerk, mBufferMode);
        if (MC_ErrorCode::GOOD == err && !isTimed)
            resetTimedState();
        return err;
    }

    void FbAccelerationProfile::onOperationDone(int32_t customId)
    {
        if (mActiveAccelerationProfile && mTimedSegmentDuration > 0.0)
            return;

        if (mExecute && mActiveAccelerationProfile && mActiveAccelerationProfile->mNext)
        {
            mActiveAccelerationProfile = mActiveAccelerationProfile->mNext;
            VelocityProfileCommand command;
            MC_ErrorCode err = buildAccelerationProfileCommand(
                mActiveAccelerationProfile, mTimeScale, mAccelerationScale, mAccelerationOffset, command);
            if (MC_ErrorCode::GOOD == err)
            {
                err = mAxis ? mAxis->updateMoveVel(this, command.mVelocity,
                                      command.mAcceleration, command.mDeceleration,
                                      command.mJerk)
                            : MC_ErrorCode::AXIS_NO_TEXIST;
            }
            if (MC_ErrorCode::GOOD != err)
            {
                onOperationError(err, customId);
                return;
            }

            mDone = false;
            mBusy = true;
            mActive = true;
            mCommandAborted = false;
            mLastVelocity = command.mVelocity;
            mLastAcceleration = command.mAcceleration;
            mLastDeceleration = command.mDeceleration;
            mLastJerk = command.mJerk;
            mLastOverride = mAxis ? mAxis->override() : 100;
            clearError();
            return;
        }

        mActiveAccelerationProfile = nullptr;
        resetTimedState();
        FbExecAxisBufferContType::onOperationDone(customId);
    }

    void FbAccelerationProfile::onOperationAborted(int32_t customId)
    {
        mActiveAccelerationProfile = nullptr;
        resetTimedState();
        FbSeqExecuteType::onOperationAborted(customId);
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
        case MC_AxisStatus::DISCRETE_MOTION:
            mDiscreteMotion = true;
            break;
        case MC_AxisStatus::CONTINUOUS_MOTION:
            mContinuousMotion = true;
            break;
        case MC_AxisStatus::SYNCHRONIZED_MOTION:
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
            return MC_ErrorCode::SOURCE_ILLEGAL;
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
        const MC_ErrorCode err = mEnable ? onEnableTrue() : onEnableFalse();
        if (err != MC_ErrorCode::GOOD)
        {
            mValid = false;
            mAxisErrorID = 0;
            FbBaseType::onOperationError(err, 0);
            return;
        }

        mError = false;
    }

    MC_ErrorCode FbReadAxisError::onEnableTrue(void)
    {
        if (!mAxis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

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

    MC_ErrorCode FbSetPosition::onAxisTriggered(bool &isDone)
    {
        if (mAxis->status() != MC_AxisStatus::STANDSTILL)
            return MC_ErrorCode::AXIS_STANDSTILL;

        const double systemActualPosition = mAxis->actPosition();
        const MC_ErrorCode err = mAxis->setHomePosition(mPosition - systemActualPosition);
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbSetOverride::onAxisTriggered(bool &isDone)
    {
        const MC_ErrorCode err = mAxis->setOverride(mOverride);
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    void FbTouchProbe::call(void)
    {
        if (mExecute && !mExecuteTrigger)
        {
            mDone = mCommandAborted = false;
            mBusy = mActive = true;
            mRecordedPosition = 0.0;
            clearError();

            if (!mAxis)
            {
                mBusy = mActive = false;
                onOperationError(MC_ErrorCode::AXIS_NO_TEXIST, 0);
            }
            else
            {
                if (mWindowOnly && (!std::isfinite(mFirstPosition) || !std::isfinite(mLastPosition) ||
                                    mFirstPosition > mLastPosition))
                {
                    mBusy = mActive = false;
                    onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
                    mExecuteTrigger = mExecute;
                    return;
                }

                double value = 0;
                if (!mAxis->servoReadVal(kDigitalInputBase + static_cast<int>(mTriggerInput), value))
                {
                    mBusy = mActive = false;
                    onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
                }
                else
                {
                    const MC_ErrorCode err = mAxis->armTouchProbe(static_cast<int>(mTriggerInput), value != 0.0);
                    if (err != MC_ErrorCode::GOOD)
                    {
                        mBusy = mActive = false;
                        onOperationError(err, 0);
                    }
                }
            }
        }
        else if (mExecute && mBusy)
        {
            if (!mAxis->touchProbeArmed(static_cast<int>(mTriggerInput)))
            {
                mBusy = mActive = false;
                mCommandAborted = true;
                clearError();
            }
            else
            {
                double value = 0;
                if (!mAxis->servoReadVal(kDigitalInputBase + static_cast<int>(mTriggerInput), value))
                {
                    mBusy = mActive = false;
                    onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
                }
                else
                {
                    bool triggered = false;
                    if (mWindowOnly && (!std::isfinite(mFirstPosition) || !std::isfinite(mLastPosition) ||
                                        mFirstPosition > mLastPosition))
                    {
                        mBusy = mActive = false;
                        onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
                        mExecuteTrigger = mExecute;
                        return;
                    }

                    double recordedPosition = mAxis->actPosition();
                    mAxis->servoReadLatchedPosition(
                        kDigitalInputBase + static_cast<int>(mTriggerInput),
                        recordedPosition);
                    const bool captureEnabled = !mWindowOnly ||
                        (recordedPosition >= mFirstPosition && recordedPosition <= mLastPosition);
                    const MC_ErrorCode err = mAxis->updateTouchProbe(
                        static_cast<int>(mTriggerInput),
                        value != 0.0,
                        triggered,
                        captureEnabled);
                    if (err != MC_ErrorCode::GOOD)
                    {
                        mBusy = mActive = false;
                        onOperationError(err, 0);
                    }
                    else if (triggered)
                    {
                        mRecordedPosition = recordedPosition;
                        mBusy = mActive = false;
                        mDone = true;
                        clearError();
                    }
                }
            }
        }
        else if (!mExecute)
        {
            if (mAxis)
                mAxis->abortTouchProbe(static_cast<int>(mTriggerInput));
            mDone = mBusy = mActive = mCommandAborted = false;
            clearError();
        }

        mExecuteTrigger = mExecute;
    }

    MC_ErrorCode FbAbortTrigger::onAxisTriggered(bool &isDone)
    {
        double value = 0;
        if (!mAxis->servoReadVal(kDigitalInputBase + static_cast<int>(mTriggerInput), value))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        const MC_ErrorCode err = mAxis->abortTouchProbe(static_cast<int>(mTriggerInput));
        if (err != MC_ErrorCode::GOOD)
            return err;

        isDone = true;
        return MC_ErrorCode::GOOD;
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

    MC_ErrorCode FbReadActualTorque::onAxisEnable(bool &isDone)
    {
        mTorque = mAxis->actTorque();
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadActualTorque::onDisable(void)
    {
        mTorque = 0;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadParameter::onAxisEnable(bool &isDone)
    {
        switch (mParameterNumber)
        {
        case MC_Parameter::COMMANDED_POSITION:
            mValue = mAxis->cmdPosition();
            break;
        case MC_Parameter::SWLIMIT_POS:
            mValue = mAxis->rangeLimitInfo().mLimitPositive;
            break;
        case MC_Parameter::SWLIMIT_NEG:
            mValue = mAxis->rangeLimitInfo().mLimitNegative;
            break;
        case MC_Parameter::ENABLE_LIMIT_POS:
            mValue = mAxis->rangeLimitInfo().mSwLimitPositive ? 1.0 : 0.0;
            break;
        case MC_Parameter::ENABLE_LIMIT_NEG:
            mValue = mAxis->rangeLimitInfo().mSwLimitNegative ? 1.0 : 0.0;
            break;
        case MC_Parameter::ENABLE_POS_LAG_MONITORING:
            mValue = mAxis->motionLimitInfo().mEnablePosLagMonitoring ? 1.0 : 0.0;
            break;
        case MC_Parameter::MAX_POSITION_LAG:
            mValue = mAxis->motionLimitInfo().mPosLagLimit;
            break;
        case MC_Parameter::MAX_VELOCITY_SYSTEM:
        case MC_Parameter::MAX_VELOCITY_APPL:
            mValue = mAxis->motionLimitInfo().mVelLimit;
            break;
        case MC_Parameter::MAX_ACCELERATION_SYSTEM:
        case MC_Parameter::MAX_ACCELERATION_APPL:
        case MC_Parameter::MAX_DECELERATION_SYSTEM:
        case MC_Parameter::MAX_DECELERATION_APPL:
            mValue = mAxis->motionLimitInfo().mAccLimit;
            break;
        case MC_Parameter::MAX_JERK_SYSTEM:
        case MC_Parameter::MAX_JERK_APPL:
            mValue = mAxis->motionLimitInfo().mJerkLimit;
            break;
        case MC_Parameter::ACTUAL_VELOCITY:
            mValue = mAxis->actVelocity();
            break;
        case MC_Parameter::COMMANDED_VELOCITY:
            mValue = mAxis->cmdVelocity();
            break;
        default:
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;
        }

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadParameter::onDisable(void)
    {
        mValue = 0;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadBoolParameter::onAxisEnable(bool &isDone)
    {
        switch (mParameterNumber)
        {
        case MC_Parameter::ENABLE_LIMIT_POS:
            mValue = mAxis->rangeLimitInfo().mSwLimitPositive;
            break;
        case MC_Parameter::ENABLE_LIMIT_NEG:
            mValue = mAxis->rangeLimitInfo().mSwLimitNegative;
            break;
        case MC_Parameter::ENABLE_POS_LAG_MONITORING:
            mValue = mAxis->motionLimitInfo().mEnablePosLagMonitoring;
            break;
        default:
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;
        }

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadBoolParameter::onDisable(void)
    {
        mValue = false;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadAxisInfo::onAxisEnable(bool &isDone)
    {
        const AxisRangeLimitInfo &rangeLimit = mAxis->rangeLimitInfo();
        const double position = mAxis->actPosition();

        mHomeAbsSwitch = readServoBool(mAxis, kAxisInfoHomeSwitchInput);
        mLimitSwitchPos = readServoBool(mAxis, kAxisInfoPositiveLimitInput) ||
            (rangeLimit.mSwLimitPositive && position > rangeLimit.mLimitPositive);
        mLimitSwitchNeg = readServoBool(mAxis, kAxisInfoNegativeLimitInput) ||
            (rangeLimit.mSwLimitNegative && position < rangeLimit.mLimitNegative);
        mSimulation = true;
        mCommunicationReady = mAxis->servoCommunicationReady() && mAxis->errorCode() == MC_ErrorCode::GOOD;
        mReadyForPowerOn = mAxis->servoReadyForPowerOn() && mAxis->errorCode() == MC_ErrorCode::GOOD;
        mPowerOn = mAxis->powerStatus();
        mIsHomed = mAxis->isHomed();
        mAxisWarning = mAxis->servoWarning() || readServoBool(mAxis, kAxisInfoWarningInput);

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadAxisInfo::onDisable(void)
    {
        mHomeAbsSwitch = false;
        mLimitSwitchPos = false;
        mLimitSwitchNeg = false;
        mSimulation = false;
        mCommunicationReady = false;
        mReadyForPowerOn = false;
        mPowerOn = false;
        mIsHomed = false;
        mAxisWarning = false;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbWriteParameter::onAxisTriggered(bool &isDone)
    {
        if (!std::isfinite(mValue))
        {
            if (mParameterNumber == MC_Parameter::MAX_JERK_SYSTEM ||
                mParameterNumber == MC_Parameter::MAX_JERK_APPL)
                return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;

            return MC_ErrorCode::POS_ILLEGAL;
        }

        switch (mParameterNumber)
        {
        case MC_Parameter::SWLIMIT_POS:
        {
            AxisRangeLimitInfo info = mAxis->rangeLimitInfo();
            info.mLimitPositive = mValue;
            const MC_ErrorCode err = mAxis->setRangeLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::SWLIMIT_NEG:
        {
            AxisRangeLimitInfo info = mAxis->rangeLimitInfo();
            info.mLimitNegative = mValue;
            const MC_ErrorCode err = mAxis->setRangeLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::MAX_POSITION_LAG:
        {
            AxisMotionLimitInfo info = mAxis->motionLimitInfo();
            info.mPosLagLimit = mValue;
            const MC_ErrorCode err = mAxis->setMotionLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::MAX_VELOCITY_SYSTEM:
        case MC_Parameter::MAX_VELOCITY_APPL:
        {
            AxisMotionLimitInfo info = mAxis->motionLimitInfo();
            info.mVelLimit = mValue;
            const MC_ErrorCode err = mAxis->setMotionLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::MAX_ACCELERATION_SYSTEM:
        case MC_Parameter::MAX_ACCELERATION_APPL:
        case MC_Parameter::MAX_DECELERATION_SYSTEM:
        case MC_Parameter::MAX_DECELERATION_APPL:
        {
            AxisMotionLimitInfo info = mAxis->motionLimitInfo();
            info.mAccLimit = mValue;
            const MC_ErrorCode err = mAxis->setMotionLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::MAX_JERK_SYSTEM:
        case MC_Parameter::MAX_JERK_APPL:
        {
            AxisMotionLimitInfo info = mAxis->motionLimitInfo();
            info.mJerkLimit = mValue;
            const MC_ErrorCode err = mAxis->setMotionLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        default:
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;
        }

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbWriteBoolParameter::onAxisTriggered(bool &isDone)
    {
        switch (mParameterNumber)
        {
        case MC_Parameter::ENABLE_LIMIT_POS:
        {
            AxisRangeLimitInfo info = mAxis->rangeLimitInfo();
            info.mSwLimitPositive = mValue;
            const MC_ErrorCode err = mAxis->setRangeLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::ENABLE_LIMIT_NEG:
        {
            AxisRangeLimitInfo info = mAxis->rangeLimitInfo();
            info.mSwLimitNegative = mValue;
            const MC_ErrorCode err = mAxis->setRangeLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        case MC_Parameter::ENABLE_POS_LAG_MONITORING:
        {
            AxisMotionLimitInfo info = mAxis->motionLimitInfo();
            info.mEnablePosLagMonitoring = mValue;
            const MC_ErrorCode err = mAxis->setMotionLimitInfo(info);
            if (err != MC_ErrorCode::GOOD)
                return err;
            break;
        }
        default:
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;
        }

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadDigitalInput::onAxisEnable(bool &isDone)
    {
        double value = 0;
        if (!mAxis->servoReadVal(kDigitalInputBase + static_cast<int>(mInputNumber), value))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        mValue = value != 0.0;
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadDigitalInput::onDisable(void)
    {
        mValue = false;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbReadDigitalOutput::onAxisEnable(bool &isDone)
    {
        double value = 0;
        if (!mAxis->servoReadVal(kDigitalOutputBase + static_cast<int>(mOutputNumber), value))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        mValue = value != 0.0;
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbReadDigitalOutput::onDisable(void)
    {
        mValue = false;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbWriteDigitalOutput::onAxisTriggered(bool &isDone)
    {
        if (!mAxis->servoWriteVal(kDigitalOutputBase + static_cast<int>(mOutputNumber), mValue ? 1.0 : 0.0))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbDigitalCamSwitch::onAxisEnable(bool &isDone)
    {
        if (!std::isfinite(mOnPosition) || !std::isfinite(mOffPosition))
            return MC_ErrorCode::POS_ILLEGAL;

        if (!std::isfinite(mPeriod) || mPeriod < 0.0)
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        const double position = mAxis->actPosition();
        if (mPeriod > 0.0)
        {
            const auto normalize = [this](double value) {
                value = std::fmod(value, mPeriod);
                if (value < 0.0)
                    value += mPeriod;
                return value;
            };

            const double periodicPosition = normalize(position);
            const double onPosition = normalize(mOnPosition);
            const double offPosition = normalize(mOffPosition);
            mValue = onPosition <= offPosition
                         ? periodicPosition >= onPosition && periodicPosition <= offPosition
                         : periodicPosition >= onPosition || periodicPosition <= offPosition;
        }
        else
        {
            const double low = mOnPosition <= mOffPosition ? mOnPosition : mOffPosition;
            const double high = mOnPosition <= mOffPosition ? mOffPosition : mOnPosition;
            mValue = position >= low && position <= high;
        }

        if (mOutputActive && mActiveOutputNumber != mOutputNumber)
        {
            mAxis->servoWriteVal(kDigitalOutputBase + static_cast<int>(mActiveOutputNumber), 0.0);
            mOutputActive = false;
        }

        if (!mAxis->servoWriteVal(kDigitalOutputBase + static_cast<int>(mOutputNumber), mValue ? 1.0 : 0.0))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        mActiveOutputNumber = mOutputNumber;
        mOutputActive = true;
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    void FbDigitalCamSwitch::onDisable(void)
    {
        mValue = false;
        if (mAxis && mOutputActive)
            mAxis->servoWriteVal(kDigitalOutputBase + static_cast<int>(mActiveOutputNumber), 0.0);

        mOutputActive = false;
    }

    ////////////////////////////////////////////////////////////

    void FbTorqueControl::call(void)
    {
        FbWriteInfoAxisType::call();

        if (!mExecute)
        {
            if (mInTorque && mAxis)
                mAxis->setTorque(0.0);
            mInTorque = false;
        }
    }

    MC_ErrorCode FbTorqueControl::onAxisTriggered(bool &isDone)
    {
        if (!std::isfinite(mTorque))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        const MC_ErrorCode err = mAxis->setTorque(mTorque);
        if (err != MC_ErrorCode::GOOD)
            return err;

        mInTorque = true;
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

    ////////////////////////////////////////////////////////////

    MC_ErrorCode FbEmergencyStop::onAxisTriggered(bool &isDone)
    {
        mAxis->emergStop(MC_ErrorCode::SOFTWARE_EMGS);
        isDone = true;
        return MC_ErrorCode::GOOD;
    }

} // namespace plcopen
