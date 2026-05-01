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
#include "FunctionBlock.h"
#include "ProfilePlanner.h"

#include <cmath>

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
    double mStartMasterPosition = 0.0;
    MC_CAM_REF mCamTable;
    double mCamMasterOffset = 0.0;
    double mCamSlaveOffset = 0.0;
    double mCamMasterScaling = 1.0;
    double mCamSlaveScaling = 1.0;
    bool mHoldArmed = true;

  protected:
    MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override
    {
        AxisSync *slave = dynamic_cast<AxisSync *>(queue);
        if (!mMaster)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (mMode == SyncMode::CAM && (!mCamTable || mCamTable->empty()))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        if (!mMaster->powerStatus())
            return MC_ErrorCode::AXIS_POWER_OFF;

        if (mWaitForMasterSyncPosition)
        {
            if (!mMasterSyncPositionInitialized)
            {
                mStartMasterPosition = masterPosition();
                mMasterSyncPositionInitialized = true;
            }

            const double currentMasterPosition = masterPosition();
            const bool positiveApproach = mMasterSyncPosition >= mStartMasterPosition;
            const bool syncReached = positiveApproach ? currentMasterPosition >= mMasterSyncPosition
                                                      : currentMasterPosition <= mMasterSyncPosition;
            if (!syncReached)
                return MC_ErrorCode::GOOD;

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

        const MC_ErrorCode err = slave->setPosition(targetPosition, targetVelocity, targetAcceleration);
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

  private:
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
                                        double masterSyncPosition, double slaveSyncPosition,
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

        const double ratio = ratioNumerator / ratioDenominator;
        mImpl_->mGearPhaseOffset = slaveSyncPosition - masterSyncPosition * ratio;

        return pushAndNewData(
            [master, ratio, masterSyncPosition, masterValueSource](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::GEAR;
                node->mMasterValueSource = masterValueSource;
                node->mRatio = ratio;
                node->mWaitForMasterSyncPosition = true;
                node->mMasterSyncPosition = masterSyncPosition;
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

        if (!camTable || camTable->empty())
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

} // namespace plcopen
