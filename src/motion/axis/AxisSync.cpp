/*
 * AxisSync.cpp
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

#include "AxisSync.h"
#include "AxesGroup.h"
#include "Axis.h"
#include "CamTable.h"
#include "FbMultiAxis.h"
#include "FunctionBlock.h"
#include "ProfilePlanner.h"

#include <cmath>
#include <limits>

namespace plcopen
{
namespace
{
MC_ErrorCode validateSyncGroupPrecondition(AxisSync *slave, Axis *master)
{
    AxesGroup *slaveGroup = slave->group();
    AxesGroup *masterGroup = master->group();

    if (!slaveGroup || !masterGroup || slaveGroup != masterGroup)
        return MC_ErrorCode::AXIS_GROUP_MISMATCH;

    return slaveGroup->status() == MC_GroupStatus::DISABLED ? MC_ErrorCode::GROUP_DISABLED : MC_ErrorCode::GOOD;
}

enum class SyncMode
{
    GEAR,
    CAM,
};

bool isDefinedMasterValueSource(MC_Source source)
{
    return source == MC_Source::SETVALUE || source == MC_Source::ACTUALVALUE;
}

bool isDefinedCombineMode(MC_CombineMode mode)
{
    return mode == MC_CombineMode::mcAddAxes || mode == MC_CombineMode::mcSubAxes;
}

struct CombineInputConfig
{
    MC_CombineMode mCombineMode = MC_CombineMode::mcAddAxes;
    MC_Source mMasterValueSourceM1 = MC_Source::SETVALUE;
    MC_Source mMasterValueSourceM2 = MC_Source::SETVALUE;
    double mRatioM1 = 1.0;
    double mRatioM2 = 1.0;
};

MC_ErrorCode makeCombineInputConfig(
    MC_CombineMode combineMode,
    double gearRatioNumeratorM1,
    double gearRatioDenominatorM1,
    double gearRatioNumeratorM2,
    double gearRatioDenominatorM2,
    MC_Source masterValueSourceM1,
    MC_Source masterValueSourceM2,
    CombineInputConfig &config)
{
    if (!isDefinedCombineMode(combineMode))
        return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

    if (!isDefinedMasterValueSource(masterValueSourceM1) || !isDefinedMasterValueSource(masterValueSourceM2))
        return MC_ErrorCode::SOURCE_ILLEGAL;

    if (gearRatioDenominatorM1 == 0.0 || gearRatioDenominatorM2 == 0.0 ||
        !std::isfinite(gearRatioNumeratorM1) || !std::isfinite(gearRatioDenominatorM1) ||
        !std::isfinite(gearRatioNumeratorM2) || !std::isfinite(gearRatioDenominatorM2))
        return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

    config.mCombineMode = combineMode;
    config.mMasterValueSourceM1 = masterValueSourceM1;
    config.mMasterValueSourceM2 = masterValueSourceM2;
    config.mRatioM1 = gearRatioNumeratorM1 / gearRatioDenominatorM1;
    config.mRatioM2 = gearRatioNumeratorM2 / gearRatioDenominatorM2;
    return MC_ErrorCode::GOOD;
}

double sourcePosition(Axis *axis, MC_Source source)
{
    return source == MC_Source::ACTUALVALUE ? axis->actPosition() : axis->cmdPosition();
}

double sourceVelocity(Axis *axis, MC_Source source)
{
    return source == MC_Source::ACTUALVALUE ? axis->actVelocity() : axis->cmdVelocity();
}

double sourceAcceleration(Axis *axis, MC_Source source)
{
    return source == MC_Source::ACTUALVALUE ? axis->actAcceleration() : axis->cmdAcceleration();
}

class SyncNode : virtual public AxisExeclNode
{
  public:
    Axis *mMaster = nullptr;
    SyncMode mMode = SyncMode::GEAR;
    MC_Source mMasterValueSource = MC_Source::SETVALUE;
    double mRatio = 1.0;
    bool mWaitForMasterSyncPosition = false;
    bool mMasterSyncPositionInitialized = false;
    double mMasterSyncPosition = 0.0;
    double mMasterStartDistance = 0.0;
    double mStartMasterPosition = 0.0;
    double mStartSlavePosition = 0.0;
    double mSlaveSyncPosition = 0.0;
    bool mSlaveSyncPositionExplicit = false;
    double mApproachVelocity = 0.0;
    double mApproachAcceleration = 0.0;
    double mApproachDeceleration = 0.0;
    double mApproachJerk = 0.0;
    uint32_t mApproachFrequency = 100;
    ProfilePlanner mApproachPlanner;
    bool mApproachPlannerActive = false;
    double mApproachTargetPosition = std::numeric_limits<double>::quiet_NaN();
    MC_CAM_REF mCamTable;
    double mCamMasterOffset = 0.0;
    double mCamSlaveOffset = 0.0;
    double mCamMasterScaling = 1.0;
    double mCamSlaveScaling = 1.0;
    bool mHoldArmed = true;
    bool mStartSyncNotified = false;

  protected:
    MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override
    {
        AxisSync *slave = dynamic_cast<AxisSync *>(queue);
        if (!mMaster)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (!mMaster->powerStatus())
            return MC_ErrorCode::AXIS_POWER_OFF;

        MC_ErrorCode err = updateContinuousInputs(slave);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (mMode == SyncMode::CAM && (!mCamTable || mCamTable->empty() || !mCamTable->valid()))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (mWaitForMasterSyncPosition)
        {
            if (!mMasterSyncPositionInitialized)
            {
                const double currentMasterPosition = masterPosition();
                if (mMasterStartDistance > 0.0)
                {
                    const double direction = mMasterSyncPosition >= currentMasterPosition ? 1.0 : -1.0;
                    mStartMasterPosition = mMasterSyncPosition - direction * mMasterStartDistance;
                }
                else
                {
                    mStartMasterPosition = currentMasterPosition;
                }
                mStartSlavePosition = slave->cmdPosition();
                mMasterSyncPositionInitialized = true;
            }

            const double currentMasterPosition = masterPosition();
            const bool positiveApproach = mMasterSyncPosition >= mStartMasterPosition;
            const bool approachStarted = positiveApproach ? currentMasterPosition >= mStartMasterPosition
                                                          : currentMasterPosition <= mStartMasterPosition;
            const bool syncReached = positiveApproach ? currentMasterPosition >= mMasterSyncPosition
                                                      : currentMasterPosition <= mMasterSyncPosition;
            if (!syncReached)
            {
                if (!approachStarted)
                    return MC_ErrorCode::GOOD;

                notifyStartSync();

                const double span = mMasterSyncPosition - mStartMasterPosition;
                if (span != 0.0)
                {
                    const double slaveSyncPosition = mSlaveSyncPositionExplicit
                        ? mSlaveSyncPosition
                        : camTargetAtMasterSyncPosition();
                    if (mApproachVelocity > 0.0)
                    {
                        err = profileApproachToSync(slave, slaveSyncPosition);
                    }
                    else
                    {
                        double progress = (currentMasterPosition - mStartMasterPosition) / span;
                        if (progress < 0.0)
                            progress = 0.0;
                        else if (progress > 1.0)
                            progress = 1.0;

                        const double targetPosition =
                            mStartSlavePosition + (slaveSyncPosition - mStartSlavePosition) * progress;
                        const double targetVelocity =
                            masterVelocity() * (slaveSyncPosition - mStartSlavePosition) / span;
                        err = slave->setPosition(targetPosition, targetVelocity, 0.0);
                    }
                    if (err != MC_ErrorCode::GOOD)
                        return err;
                }
                return MC_ErrorCode::GOOD;
            }

            mWaitForMasterSyncPosition = false;
        }

        double targetPosition = 0.0;
        double targetVelocity = 0.0;
        double targetAcceleration = 0.0;

        if (mMode == SyncMode::GEAR)
        {
            targetPosition = masterPosition() * mRatio + slave->gearPhaseOffset();
            targetVelocity = masterVelocity() * mRatio;
            targetAcceleration = masterAcceleration() * mRatio;
        }
        else
        {
            const double camMasterPosition = (masterPosition() - mCamMasterOffset) / mCamMasterScaling;
            targetPosition = mCamSlaveOffset + mCamSlaveScaling * mCamTable->sample(camMasterPosition);
            targetVelocity = 0.0;
            targetAcceleration = 0.0;
        }

        const MC_ErrorCode setPositionErr = slave->setPosition(targetPosition, targetVelocity, targetAcceleration);
        if (setPositionErr != MC_ErrorCode::GOOD)
            return setPositionErr;

        if (mHoldArmed)
        {
            stat = ExeclNodeExecStat::DONE;
            mHoldArmed = false;
        }

        return MC_ErrorCode::GOOD;
    }

    void onDone(ExeclQueue *queue, bool &isHold) override
    {
        AxisExeclNode::onDone(queue, isHold);
        isHold = true;
    }

    void onPositionOffset(ExeclQueue *queue, double positionOffset) override
    {
    }

  private:
    MC_ErrorCode updateGearInputs(double ratioNumerator, double ratioDenominator, MC_Source masterValueSource)
    {
        if (!isDefinedMasterValueSource(masterValueSource))
            return MC_ErrorCode::SOURCE_ILLEGAL;

        if (ratioDenominator == 0.0 || !std::isfinite(ratioNumerator) || !std::isfinite(ratioDenominator))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        mRatio = ratioNumerator / ratioDenominator;
        mMasterValueSource = masterValueSource;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode updateContinuousInputs(AxisSync *slave)
    {
        if (auto *fb = dynamic_cast<const FbGearIn *>(mFb))
        {
            if (!fb->mContinuousUpdate)
                return MC_ErrorCode::GOOD;

            return updateGearInputs(fb->mRatioNumerator, fb->mRatioDenominator, fb->mMasterValueSource);
        }

        if (auto *fb = dynamic_cast<const FbGearInPos *>(mFb))
        {
            if (!fb->mContinuousUpdate)
                return MC_ErrorCode::GOOD;

            MC_ErrorCode err = updateGearInputs(fb->mRatioNumerator, fb->mRatioDenominator, fb->mMasterValueSource);
            if (err != MC_ErrorCode::GOOD)
                return err;

            if (!std::isfinite(fb->mMasterSyncPosition) || !std::isfinite(fb->mSlaveSyncPosition))
                return MC_ErrorCode::POS_ILLEGAL;

            if (!std::isfinite(fb->mMasterStartDistance) || fb->mMasterStartDistance < 0.0)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (fb->mVelocity < 0.0 || !std::isfinite(fb->mVelocity))
                return MC_ErrorCode::VEL_ILLEGAL;

            if (fb->mVelocity > 0.0)
            {
                if (fb->mAcceleration <= 0.0 || !std::isfinite(fb->mAcceleration) ||
                    fb->mDeceleration <= 0.0 || !std::isfinite(fb->mDeceleration))
                    return MC_ErrorCode::ACC_ILLEGAL;

                if (fb->mJerk < 0.0 || !std::isfinite(fb->mJerk))
                    return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;
            }

            if (mWaitForMasterSyncPosition)
            {
                const bool syncWindowChanged =
                    mMasterSyncPosition != fb->mMasterSyncPosition ||
                    mSlaveSyncPosition != fb->mSlaveSyncPosition ||
                    mMasterStartDistance != fb->mMasterStartDistance;

                mMasterSyncPosition = fb->mMasterSyncPosition;
                mSlaveSyncPosition = fb->mSlaveSyncPosition;
                mMasterStartDistance = fb->mMasterStartDistance;
                mApproachVelocity = fb->mVelocity;
                mApproachAcceleration = fb->mAcceleration;
                mApproachDeceleration = fb->mDeceleration;
                mApproachJerk = fb->mJerk;

                if (syncWindowChanged)
                {
                    mMasterSyncPositionInitialized = false;
                    mApproachPlannerActive = false;
                    mApproachTargetPosition = std::numeric_limits<double>::quiet_NaN();
                }

                return slave->setGearPhaseOffset(mSlaveSyncPosition - mMasterSyncPosition * mRatio);
            }

            return MC_ErrorCode::GOOD;
        }

        if (auto *fb = dynamic_cast<const FbCamIn *>(mFb))
        {
            if (!fb->mContinuousUpdate)
                return MC_ErrorCode::GOOD;

            if (!isDefinedMasterValueSource(fb->mMasterValueSource))
                return MC_ErrorCode::SOURCE_ILLEGAL;

            if (!fb->mCamTable || fb->mCamTable->empty() || !fb->mCamTable->valid())
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (!std::isfinite(fb->mMasterStartDistance) || fb->mMasterStartDistance < 0.0)
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (fb->mMasterStartDistance > 0.0 && !std::isfinite(fb->mMasterSyncPosition))
                return MC_ErrorCode::POS_ILLEGAL;

            if (!std::isfinite(fb->mMasterOffset) || !std::isfinite(fb->mSlaveOffset))
                return MC_ErrorCode::POS_ILLEGAL;

            if (!std::isfinite(fb->mMasterScaling) || fb->mMasterScaling == 0.0 ||
                !std::isfinite(fb->mSlaveScaling))
                return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

            if (mWaitForMasterSyncPosition)
            {
                const bool syncWindowChanged =
                    mMasterSyncPosition != fb->mMasterSyncPosition ||
                    mMasterStartDistance != fb->mMasterStartDistance;

                mMasterSyncPosition = fb->mMasterSyncPosition;
                mMasterStartDistance = fb->mMasterStartDistance;

                if (syncWindowChanged)
                    mMasterSyncPositionInitialized = false;
            }

            mMasterValueSource = fb->mMasterValueSource;
            mCamTable = fb->mCamTable;
            mCamMasterOffset = fb->mMasterOffset;
            mCamSlaveOffset = fb->mSlaveOffset;
            mCamMasterScaling = fb->mMasterScaling;
            mCamSlaveScaling = fb->mSlaveScaling;
        }

        return MC_ErrorCode::GOOD;
    }

    double masterPosition() const
    {
        return mMasterValueSource == MC_Source::ACTUALVALUE ? mMaster->actPosition() : mMaster->cmdPosition();
    }

    double masterVelocity() const
    {
        return mMasterValueSource == MC_Source::ACTUALVALUE ? mMaster->actVelocity() : mMaster->cmdVelocity();
    }

    double masterAcceleration() const
    {
        return mMasterValueSource == MC_Source::ACTUALVALUE ? mMaster->actAcceleration() : mMaster->cmdAcceleration();
    }

    double camTargetAtMasterSyncPosition() const
    {
        if (!mCamTable || mCamTable->empty())
            return mStartSlavePosition;

        const double camMasterPosition = (mMasterSyncPosition - mCamMasterOffset) / mCamMasterScaling;
        return mCamSlaveOffset + mCamSlaveScaling * mCamTable->sample(camMasterPosition);
    }

    MC_ErrorCode profileApproachToSync(AxisSync *slave, double slaveSyncPosition)
    {
        const double startPosition = slave->cmdPosition();
        if (std::fabs(slaveSyncPosition - startPosition) <= 1e-9)
            return slave->setPosition(slaveSyncPosition, 0.0, 0.0);

        if (!mApproachPlannerActive || mApproachTargetPosition != slaveSyncPosition)
        {
            mApproachPlanner.setFrequency(mApproachFrequency);
            const bool planned =
                mApproachPlanner.plan(startPosition, slaveSyncPosition, slave->cmdVelocity(), mApproachVelocity,
                                      0.0, mApproachAcceleration, mApproachDeceleration, mApproachJerk);
            if (!planned)
                return slave->setPosition(slaveSyncPosition, 0.0, 0.0);

            mApproachPlannerActive = true;
            mApproachTargetPosition = slaveSyncPosition;
        }

        if (mApproachPlanner.execute())
            return slave->setPosition(slaveSyncPosition, 0.0, 0.0);

        return slave->setPosition(
            mApproachPlanner.getPosition(), mApproachPlanner.getVelocity(), mApproachPlanner.getAcceleration());
    }

    void notifyStartSync()
    {
        if (mStartSyncNotified)
            return;

        if (mFb)
            mFb->onOperationStartSync(mNodeCustomId);
        mStartSyncNotified = true;
    }
};

class CombineAxesNode : virtual public AxisExeclNode
{
  public:
    Axis *mMaster1 = nullptr;
    Axis *mMaster2 = nullptr;
    CombineInputConfig mLatchedInputConfig;
    bool mHoldArmed = true;

  protected:
    MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override
    {
        AxisSync *slave = dynamic_cast<AxisSync *>(queue);
        if (!mMaster1 || !mMaster2)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (!mMaster1->powerStatus() || !mMaster2->powerStatus())
            return MC_ErrorCode::AXIS_POWER_OFF;

        CombineInputConfig config = mLatchedInputConfig;
        const auto *fb = dynamic_cast<const FbCombineAxes *>(mFb);
        if (fb && fb->mContinuousUpdate)
        {
            MC_ErrorCode err = makeCombineInputConfig(
                fb->mCombineMode,
                fb->mGearRatioNumeratorM1,
                fb->mGearRatioDenominatorM1,
                fb->mGearRatioNumeratorM2,
                fb->mGearRatioDenominatorM2,
                fb->mMasterValueSourceM1,
                fb->mMasterValueSourceM2,
                config);
            if (err != MC_ErrorCode::GOOD)
                return err;
        }

        const double combineSign = config.mCombineMode == MC_CombineMode::mcAddAxes ? 1.0 : -1.0;
        const double position =
            sourcePosition(mMaster1, config.mMasterValueSourceM1) * config.mRatioM1 +
            combineSign * sourcePosition(mMaster2, config.mMasterValueSourceM2) * config.mRatioM2;
        const double velocity =
            sourceVelocity(mMaster1, config.mMasterValueSourceM1) * config.mRatioM1 +
            combineSign * sourceVelocity(mMaster2, config.mMasterValueSourceM2) * config.mRatioM2;
        const double acceleration =
            sourceAcceleration(mMaster1, config.mMasterValueSourceM1) * config.mRatioM1 +
            combineSign * sourceAcceleration(mMaster2, config.mMasterValueSourceM2) * config.mRatioM2;

        const MC_ErrorCode err = slave->setPosition(position, velocity, acceleration);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (mHoldArmed)
        {
            stat = ExeclNodeExecStat::DONE;
            mHoldArmed = false;
        }

        return MC_ErrorCode::GOOD;
    }

    void onDone(ExeclQueue *queue, bool &isHold) override
    {
        AxisExeclNode::onDone(queue, isHold);
        isHold = true;
    }

    void onPositionOffset(ExeclQueue *queue, double positionOffset) override
    {
    }
};

class SyncOutNode : virtual public AxisExeclNode
{
  protected:
    MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override
    {
        stat = ExeclNodeExecStat::FASTDONE;
        return MC_ErrorCode::GOOD;
    }

    void onPositionOffset(ExeclQueue *queue, double positionOffset) override
    {
    }
};
} // namespace

    class AxisSync::AxisSyncImpl
    {
    public:
        double mGearPhaseOffset = 0.0;
        ProfilePlanner mPhasePlanner;
        bool mPhaseMoveActive = false;
        double mPhaseMoveTarget = 0.0;
    };

    AxisSync::AxisSync()
    {
        mImpl_ = new AxisSyncImpl();
    }

    AxisSync::~AxisSync()
    {
        delete mImpl_;
    }

    MC_ErrorCode AxisSync::addGearIn(FunctionBlock *fb, Axis *master, double ratioNumerator, double ratioDenominator,
                                     MC_Source masterValueSource, MC_BufferMode bufferMode, int32_t customId)
    {
        if (!master)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        MC_ErrorCode err = validateSyncGroupPrecondition(this, master);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (!isDefinedMasterValueSource(masterValueSource))
            return MC_ErrorCode::SOURCE_ILLEGAL;

        if (ratioDenominator == 0 || !std::isfinite(ratioNumerator) || !std::isfinite(ratioDenominator))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        const double ratio = ratioNumerator / ratioDenominator;
        mImpl_->mGearPhaseOffset = 0.0;

        return pushAndNewData(
            [master, ratio, masterValueSource](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::GEAR;
                node->mMasterValueSource = masterValueSource;
                node->mRatio = ratio;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            customId);
    }

    MC_ErrorCode AxisSync::addGearInPos(FunctionBlock *fb, Axis *master, double ratioNumerator, double ratioDenominator,
                                        double masterSyncPosition, double slaveSyncPosition, double masterStartDistance,
                                        double velocity, double acceleration, double deceleration, double jerk,
                                        MC_Source masterValueSource, MC_BufferMode bufferMode, int32_t customId)
    {
        if (!master)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        MC_ErrorCode err = validateSyncGroupPrecondition(this, master);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (!isDefinedMasterValueSource(masterValueSource))
            return MC_ErrorCode::SOURCE_ILLEGAL;

        if (ratioDenominator == 0 || !std::isfinite(ratioNumerator) || !std::isfinite(ratioDenominator))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (!std::isfinite(masterSyncPosition) || !std::isfinite(slaveSyncPosition))
            return MC_ErrorCode::POS_ILLEGAL;

        if (!std::isfinite(masterStartDistance) || masterStartDistance < 0.0)
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (velocity < 0.0 || !std::isfinite(velocity))
            return MC_ErrorCode::VEL_ILLEGAL;

        if (velocity > 0.0)
        {
            if (acceleration <= 0.0 || !std::isfinite(acceleration) ||
                deceleration <= 0.0 || !std::isfinite(deceleration))
                return MC_ErrorCode::ACC_ILLEGAL;

            if (jerk < 0.0 || !std::isfinite(jerk))
                return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;
        }

        const double ratio = ratioNumerator / ratioDenominator;
        mImpl_->mGearPhaseOffset = slaveSyncPosition - masterSyncPosition * ratio;

        return pushAndNewData(
            [master,
             ratio,
             masterSyncPosition,
             slaveSyncPosition,
             masterStartDistance,
             velocity,
             acceleration,
             deceleration,
             jerk,
             approachFrequency = static_cast<uint32_t>(frequency()),
             masterValueSource](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::GEAR;
                node->mMasterValueSource = masterValueSource;
                node->mRatio = ratio;
                node->mWaitForMasterSyncPosition = true;
                node->mMasterSyncPosition = masterSyncPosition;
                node->mMasterStartDistance = masterStartDistance;
                node->mSlaveSyncPosition = slaveSyncPosition;
                node->mSlaveSyncPositionExplicit = true;
                node->mApproachVelocity = velocity;
                node->mApproachAcceleration = acceleration;
                node->mApproachDeceleration = deceleration;
                node->mApproachJerk = jerk;
                node->mApproachFrequency = approachFrequency;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            customId);
    }

    MC_ErrorCode AxisSync::setGearPhaseOffset(double phaseOffset)
    {
        if (!std::isfinite(phaseOffset))
            return MC_ErrorCode::POS_ILLEGAL;

        mImpl_->mGearPhaseOffset = phaseOffset;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxisSync::addGearPhaseOffset(double phaseShift)
    {
        if (!std::isfinite(phaseShift))
            return MC_ErrorCode::POS_ILLEGAL;

        mImpl_->mGearPhaseOffset += phaseShift;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxisSync::moveGearPhaseOffset(double phaseOffset, double velocity, double acceleration,
                                               double deceleration, double jerk, bool &isDone)
    {
        isDone = false;

        if (!std::isfinite(phaseOffset))
            return MC_ErrorCode::POS_ILLEGAL;

        if (velocity == 0.0)
        {
            mImpl_->mGearPhaseOffset = phaseOffset;
            mImpl_->mPhaseMoveActive = false;
            isDone = true;
            return MC_ErrorCode::GOOD;
        }

        if (velocity < 0 || !std::isfinite(velocity))
            return MC_ErrorCode::VEL_ILLEGAL;

        if (acceleration <= 0 || !std::isfinite(acceleration) || deceleration <= 0 || !std::isfinite(deceleration))
            return MC_ErrorCode::ACC_ILLEGAL;

        if (jerk < 0.0 || !std::isfinite(jerk))
            return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;

        if (!mImpl_->mPhaseMoveActive || mImpl_->mPhaseMoveTarget != phaseOffset)
        {
            mImpl_->mPhaseMoveTarget = phaseOffset;
            mImpl_->mPhaseMoveActive = true;
            mImpl_->mPhasePlanner.setFrequency(static_cast<uint32_t>(frequency()));
            const bool planned = mImpl_->mPhasePlanner.plan(
                mImpl_->mGearPhaseOffset, phaseOffset, 0.0, velocity, 0.0, acceleration, deceleration, jerk);
            if (!planned)
            {
                mImpl_->mGearPhaseOffset = phaseOffset;
                mImpl_->mPhaseMoveActive = false;
                isDone = true;
                return MC_ErrorCode::GOOD;
            }
        }

        if (mImpl_->mPhasePlanner.execute())
        {
            mImpl_->mGearPhaseOffset = phaseOffset;
            mImpl_->mPhaseMoveActive = false;
            isDone = true;
        }
        else
        {
            mImpl_->mGearPhaseOffset = mImpl_->mPhasePlanner.getPosition();
        }

        return MC_ErrorCode::GOOD;
    }

    double AxisSync::gearPhaseOffset(void) const
    {
        return mImpl_->mGearPhaseOffset;
    }

    MC_ErrorCode AxisSync::addGearOut(FunctionBlock *fb, int32_t customId)
    {
        return pushAndNewData(
            [](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncOutNode *>(baseNode);
                new (node) SyncOutNode();
                return node;
            },
            true,
            fb,
            MC_AxisStatus::STANDSTILL,
            MC_AxisStatus::STANDSTILL,
            customId);
    }

    MC_ErrorCode AxisSync::addCamIn(FunctionBlock *fb, Axis *master, MC_CAM_REF camTable,
                                    double masterSyncPosition, double masterStartDistance,
                                    double masterOffset, double slaveOffset,
                                    double masterScaling, double slaveScaling,
                                    MC_Source masterValueSource, MC_BufferMode bufferMode, int32_t customId)
    {
        if (!master)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        MC_ErrorCode err = validateSyncGroupPrecondition(this, master);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (!isDefinedMasterValueSource(masterValueSource))
            return MC_ErrorCode::SOURCE_ILLEGAL;

        if (!camTable || camTable->empty() || !camTable->valid())
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (!std::isfinite(masterStartDistance) || masterStartDistance < 0.0)
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (masterStartDistance > 0.0 && !std::isfinite(masterSyncPosition))
            return MC_ErrorCode::POS_ILLEGAL;

        if (!std::isfinite(masterOffset) || !std::isfinite(slaveOffset))
            return MC_ErrorCode::POS_ILLEGAL;

        if (!std::isfinite(masterScaling) || masterScaling == 0.0 || !std::isfinite(slaveScaling))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        return pushAndNewData(
            [master,
             camTable,
             masterSyncPosition,
             masterStartDistance,
             masterOffset,
             slaveOffset,
             masterScaling,
             slaveScaling,
             masterValueSource](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::CAM;
                node->mMasterValueSource = masterValueSource;
                node->mWaitForMasterSyncPosition = masterStartDistance > 0.0;
                node->mMasterSyncPosition = masterSyncPosition;
                node->mMasterStartDistance = masterStartDistance;
                node->mCamMasterOffset = masterOffset;
                node->mCamSlaveOffset = slaveOffset;
                node->mCamMasterScaling = masterScaling;
                node->mCamSlaveScaling = slaveScaling;
                node->mCamTable = camTable;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            customId);
    }

    MC_ErrorCode AxisSync::addCamOut(FunctionBlock *fb, int32_t customId)
    {
        return addGearOut(fb, customId);
    }

    MC_ErrorCode AxisSync::addCombineAxes(FunctionBlock *fb, Axis *master1, Axis *master2,
                                          double gearRatioNumeratorM1, double gearRatioDenominatorM1,
                                          double gearRatioNumeratorM2, double gearRatioDenominatorM2,
                                          MC_CombineMode combineMode,
                                          MC_Source masterValueSourceM1, MC_Source masterValueSourceM2,
                                          MC_BufferMode bufferMode, int32_t customId)
    {
        if (!master1 || !master2)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        Axis *slaveAxis = dynamic_cast<Axis *>(this);
        if (master1 == master2 || master1 == slaveAxis || master2 == slaveAxis)
            return MC_ErrorCode::AXIS_ALREADY_IN_GROUP;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        CombineInputConfig config;
        MC_ErrorCode err = makeCombineInputConfig(
            combineMode,
            gearRatioNumeratorM1,
            gearRatioDenominatorM1,
            gearRatioNumeratorM2,
            gearRatioDenominatorM2,
            masterValueSourceM1,
            masterValueSourceM2,
            config);
        if (err != MC_ErrorCode::GOOD)
            return err;

        return pushAndNewData(
            [master1, master2, config](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<CombineAxesNode *>(baseNode);
                new (node) CombineAxesNode();
                node->mMaster1 = master1;
                node->mMaster2 = master2;
                node->mLatchedInputConfig = config;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            customId);
    }

} // namespace plcopen
