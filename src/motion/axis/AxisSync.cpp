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
#include "Axis.h"
#include "CamTable.h"
#include "FunctionBlock.h"

#include <cmath>

namespace plcopen
{
namespace
{
enum class SyncMode
{
    GEAR,
    CAM,
};

class SyncNode : virtual public AxisExeclNode
{
  public:
    Axis *mMaster = nullptr;
    SyncMode mMode = SyncMode::GEAR;
    double mRatio = 1.0;
    MC_CAM_REF mCamTable;
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

        double targetPosition = 0.0;
        double targetVelocity = 0.0;
        double targetAcceleration = 0.0;

        if (mMode == SyncMode::GEAR)
        {
            targetPosition = mMaster->cmdPosition() * mRatio;
            targetVelocity = mMaster->cmdVelocity() * mRatio;
            targetAcceleration = mMaster->cmdAcceleration() * mRatio;
        }
        else
        {
            targetPosition = mCamTable->sample(mMaster->cmdPosition());
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

    AxisSync::AxisSync()
    {
    }

    AxisSync::~AxisSync()
    {
    }

    MC_ErrorCode AxisSync::addGearIn(FunctionBlock *fb, Axis *master, double ratioNumerator, double ratioDenominator,
                                     MC_BufferMode bufferMode, int32_t customId)
    {
        if (!master)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (ratioDenominator == 0 || !std::isfinite(ratioNumerator) || !std::isfinite(ratioDenominator))
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        const double ratio = ratioNumerator / ratioDenominator;

        return pushAndNewData(
            [master, ratio](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::GEAR;
                node->mRatio = ratio;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            MC_AxisStatus::SYNCHRONIZED_MOTION,
            customId);
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

    MC_ErrorCode AxisSync::addCamIn(FunctionBlock *fb, Axis *master, MC_CAM_REF camTable, MC_BufferMode bufferMode,
                                    int32_t customId)
    {
        if (!master)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (!camTable || camTable->empty())
            return MC_ErrorCode::PARAMETER_NOT_SUPPORT;

        return pushAndNewData(
            [master, camTable](void *baseNode) -> AxisExeclNode * {
                auto *node = reinterpret_cast<SyncNode *>(baseNode);
                new (node) SyncNode();
                node->mMaster = master;
                node->mMode = SyncMode::CAM;
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
