/*
 * AxisHoming.cpp
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

#include "AxisHoming.h"
#include "AxisMove.h"
#include "Event.h"
#include "FunctionBlock.h"
#include "MathUtils.h"
#include "ProfilePlanner.h"

namespace plcopen
{
namespace
{
    bool homingModeUsesIndex(MC_HomingMode mode)
    {
        switch (mode)
        {
        case MC_HomingMode::MODE1:
        case MC_HomingMode::MODE2:
        case MC_HomingMode::MODE3:
        case MC_HomingMode::MODE4:
        case MC_HomingMode::MODE9:
        case MC_HomingMode::MODE10:
        case MC_HomingMode::MODE11:
        case MC_HomingMode::MODE12:
        case MC_HomingMode::MODE13:
        case MC_HomingMode::MODE14:
            return true;
        default:
            return false;
        }
    }
}

    enum class MC_HomingStep
    {
        INIT = 0,
        SEARCHSIG = 1,
        REGRESSION_SIG = 2,
        TOSIG = 3,
        SEARCH_INDEX = 4,
    };

    struct AxisHomingInfoEx : public AxisHomingInfo
    {
        bool mHomingSigVal = false; // 回零信号比对值
        bool mStopOnIndex = false;
        bool mIndexSearchAfterSwitch = false;
    };

    constexpr double kHomingOverrideEpsilon = 1e-9;

    double homingOverrideFactor(AxisHoming *axis)
    {
        AxisMove *axisMove = dynamic_cast<AxisMove *>(axis);
        return axisMove ? axisMove->override() * 0.01 : 1.0;
    }

    MC_ErrorCode setHomingPosition(AxisHoming *axis, double position, double velocity, double acceleration)
    {
        AxisMove *axisMove = dynamic_cast<AxisMove *>(axis);
        return axisMove ? axisMove->setPosition(position, velocity, acceleration)
                        : axis->setPosition(position, velocity, acceleration);
    }

    class AxisHoming::AxisHomingImpl
    {
    public:
        AxisHomingInfoEx mHomingInfo;
        ProfilePlanner mPlanner;
    };

    class HomingNode : virtual public AxisExeclNode
    {
    public:
        double mPos = 0;
        double mFinalPos = 0;
        MC_HomingStep mHomingStep = MC_HomingStep::INIT;
        double mLastOverrideFactor = 1.0;
        bool mOverrideSnapshotValid = false;

    protected:
        virtual MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override;
        virtual void onPositionOffset(ExeclQueue *queue, double offset) override;

    private:
        bool overrideChanged(double overrideFactor) const;
        void planSearch(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                        double overrideFactor);
        void planRegression(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                            double overrideFactor);
        void planToSignal(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                          double overrideFactor);
        void planIndexSearch(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                             double overrideFactor);
        bool signalActive(uint8_t *signal, uint8_t bitOffset, bool activeValue) const;
    };

    bool HomingNode::overrideChanged(double overrideFactor) const
    {
        return !mOverrideSnapshotValid || fabs(overrideFactor - mLastOverrideFactor) > kHomingOverrideEpsilon;
    }

    void HomingNode::planSearch(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                                double overrideFactor)
    {
        const double velocity = homingInfo->mHomingVelSearch * overrideFactor;
        const double acceleration = homingInfo->mHomingAcc * overrideFactor;
        const double jerk = homingInfo->mHomingJerk * overrideFactor;
        const double endPos = ProfilePlanner::calculateDist(
            axis->cmdVelocity(),
            velocity,
            acceleration,
            acceleration,
            jerk);

        planner->plan(axis->cmdPosition(), axis->cmdPosition() + endPos, axis->cmdVelocity(),
                      velocity, velocity, acceleration, acceleration, jerk);
        mLastOverrideFactor = overrideFactor;
        mOverrideSnapshotValid = true;
    }

    void HomingNode::planRegression(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                                    double overrideFactor)
    {
        const double velocity = homingInfo->mHomingVelRegression * overrideFactor;
        const double acceleration = homingInfo->mHomingAcc * overrideFactor;
        const double jerk = homingInfo->mHomingJerk * overrideFactor;
        const double endPos = ProfilePlanner::calculateDist(
            axis->cmdVelocity(),
            velocity,
            acceleration,
            acceleration,
            jerk);

        planner->plan(axis->cmdPosition(), axis->cmdPosition() + endPos, axis->cmdVelocity(),
                      velocity, velocity, acceleration, acceleration, jerk);
        mLastOverrideFactor = overrideFactor;
        mOverrideSnapshotValid = true;
    }

    void HomingNode::planToSignal(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                                  double overrideFactor)
    {
        const double velocity = homingInfo->mHomingVelRegression * overrideFactor;
        const double acceleration = homingInfo->mHomingAcc * overrideFactor;
        const double jerk = homingInfo->mHomingJerk * overrideFactor;
        const double endPos = ProfilePlanner::calculateDist(
            axis->cmdVelocity(),
            __EPSILON,
            acceleration,
            acceleration,
            jerk);

        planner->plan(axis->cmdPosition(), axis->cmdPosition() + endPos, axis->cmdVelocity(),
                      velocity, 0.0, acceleration, acceleration, jerk);
        mLastOverrideFactor = overrideFactor;
        mOverrideSnapshotValid = true;
    }

    void HomingNode::planIndexSearch(AxisHoming *axis, AxisHomingInfoEx *homingInfo, ProfilePlanner *planner,
                                     double overrideFactor)
    {
        const double velocity = homingInfo->mHomingVelRegression * overrideFactor;
        const double acceleration = homingInfo->mHomingAcc * overrideFactor;
        const double jerk = homingInfo->mHomingJerk * overrideFactor;
        const double endPos = ProfilePlanner::calculateDist(
            axis->cmdVelocity(),
            velocity,
            acceleration,
            acceleration,
            jerk);

        planner->plan(axis->cmdPosition(), axis->cmdPosition() + endPos, axis->cmdVelocity(),
                      velocity, velocity, acceleration, acceleration, jerk);
        mLastOverrideFactor = overrideFactor;
        mOverrideSnapshotValid = true;
    }

    bool HomingNode::signalActive(uint8_t *signal, uint8_t bitOffset, bool activeValue) const
    {
        return signal && ((((*signal) >> bitOffset) & 0x1) == activeValue);
    }

    MC_ErrorCode HomingNode::onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat)
    {
        AxisHoming *axis = dynamic_cast<AxisHoming *>(queue);
        ProfilePlanner *planner = &axis->mImpl_->mPlanner;
        AxisHomingInfoEx *homingInfo = &axis->mImpl_->mHomingInfo;
        const double overrideFactor = homingOverrideFactor(axis);
        MC_ErrorCode err;

        switch (mHomingStep)
        {
        case MC_HomingStep::INIT:
            if (!homingInfo->mHomingSig)
            { // 当前位置作为零点
                mFinalPos = axis->actPosition();
                err = axis->setHomePosition(mPos - mFinalPos);
                if (err == MC_ErrorCode::GOOD)
                {
                    axis->printLog(MC_LogLevel::INFO, "homing complete, new pos %lf\n", axis->homePosition());
                    stat = ExeclNodeExecStat::DONE;
                }

                return err;
            }
            else
            { // 启动回零流程
                axis->printLog(MC_LogLevel::INFO, "homing start, vel %lf, sig %p, bitoffset %d, trig %d\n",
                               homingInfo->mHomingVelSearch, homingInfo->mHomingSig, homingInfo->mHomingSigBitOffset,
                               homingInfo->mHomingSigVal);

                if (!homingInfo->mHomingVelSearch || !homingInfo->mHomingVelRegression)
                    return MC_ErrorCode::HOMING_VEL_ILLEGAL;

                if (!homingInfo->mHomingAcc)
                    return MC_ErrorCode::HOMING_ACC_ILLEGAL;

                planSearch(axis, homingInfo, planner, overrideFactor);
                mHomingStep = MC_HomingStep::SEARCHSIG;
            }
            break;

        case MC_HomingStep::SEARCHSIG:
            if (signalActive(homingInfo->mHomingSig, homingInfo->mHomingSigBitOffset, homingInfo->mHomingSigVal))
            {
                if (homingInfo->mStopOnIndex && !homingInfo->mIndexSearchAfterSwitch)
                {
                    planIndexSearch(axis, homingInfo, planner, overrideFactor);
                    mHomingStep = MC_HomingStep::SEARCH_INDEX;
                    axis->printLog(MC_LogLevel::INFO, "homing searching index, vel %lf\n",
                                   homingInfo->mHomingVelRegression);
                    break;
                }

                planRegression(axis, homingInfo, planner, overrideFactor);
                mHomingStep = MC_HomingStep::REGRESSION_SIG;

                axis->printLog(MC_LogLevel::INFO, "homing regressing, vel %lf\n", homingInfo->mHomingVelRegression);
            }
            else if (overrideChanged(overrideFactor))
            {
                planSearch(axis, homingInfo, planner, overrideFactor);
            }

            break;

        case MC_HomingStep::REGRESSION_SIG:
            if (!signalActive(homingInfo->mHomingSig, homingInfo->mHomingSigBitOffset, homingInfo->mHomingSigVal))
            {
            HOMINGSTEP_TOSIG:
                if (homingInfo->mStopOnIndex)
                {
                    planIndexSearch(axis, homingInfo, planner, overrideFactor);
                    mHomingStep = MC_HomingStep::SEARCH_INDEX;
                    break;
                }

                mFinalPos = axis->actPosition();

                planToSignal(axis, homingInfo, planner, overrideFactor);
                mHomingStep = MC_HomingStep::TOSIG;
            }
            else if (overrideChanged(overrideFactor))
            {
                planRegression(axis, homingInfo, planner, overrideFactor);
            }

            break;

        case MC_HomingStep::TOSIG:
            if (overrideChanged(overrideFactor))
                planToSignal(axis, homingInfo, planner, overrideFactor);

            if (planner->execute())
                stat = ExeclNodeExecStat::DONE;

            err = setHomingPosition(axis, planner->getPosition(), planner->getVelocity(), planner->getAcceleration());

            if (stat == ExeclNodeExecStat::DONE && err == MC_ErrorCode::GOOD)
            {
                err = axis->setHomePosition(mPos - mFinalPos);
                axis->printLog(MC_LogLevel::INFO, "homing complete, new pos %lf\n", axis->homePosition());
            }

            return err;

        case MC_HomingStep::SEARCH_INDEX:
        {
            const bool plannerDone = planner->execute();

            err = setHomingPosition(axis, planner->getPosition(), planner->getVelocity(), planner->getAcceleration());
            if (err != MC_ErrorCode::GOOD)
                return err;

            if (signalActive(homingInfo->mHomingIndexSig, homingInfo->mHomingIndexSigBitOffset, true))
            {
                mFinalPos = planner->getPosition();
                err = setHomingPosition(axis, mFinalPos, 0.0, 0.0);
                if (err == MC_ErrorCode::GOOD)
                {
                    err = axis->setHomePosition(mPos - mFinalPos);
                    if (err == MC_ErrorCode::GOOD)
                    {
                        err = setHomingPosition(axis, mFinalPos, 0.0, 0.0);
                    }

                    if (err == MC_ErrorCode::GOOD)
                        stat = ExeclNodeExecStat::DONE;
                }
                return err;
            }

            if (plannerDone || overrideChanged(overrideFactor))
                planIndexSearch(axis, homingInfo, planner, overrideFactor);

            return MC_ErrorCode::GOOD;
        }
        }

        planner->execute();

        err = setHomingPosition(axis, planner->getPosition(), planner->getVelocity(), planner->getAcceleration());

        return err;
    }

    void HomingNode::onPositionOffset(ExeclQueue *queue, double offset)
    {
    }

    AxisHoming::AxisHoming()
    {
        mImpl_ = new AxisHomingImpl();

        URANUS_ADD_HANDLER(onPowerStatusChanged, onPowerStatusChangedHandler);
        URANUS_ADD_HANDLER(onPositionOffset, onPositionOffsetHandler);
    }

    AxisHoming::~AxisHoming()
    {
        delete mImpl_;
    }

    MC_ErrorCode AxisHoming::setHomingInfo(const AxisHomingInfo &info)
    {
        if (info.mHomingMode != MC_HomingMode::DIRECT)
        {
            if (!info.mHomingVelSearch || !info.mHomingVelRegression)
                return MC_ErrorCode::HOMING_VEL_ILLEGAL;

            if (!info.mHomingAcc)
                return MC_ErrorCode::HOMING_ACC_ILLEGAL;

            if (!info.mHomingSig)
                return MC_ErrorCode::HOMING_SIG_ILLEGAL;

            if (info.mHomingSigBitOffset < 0 || info.mHomingSigBitOffset > 7)
                return MC_ErrorCode::HOMING_SIG_ILLEGAL;

            if (homingModeUsesIndex(info.mHomingMode))
            {
                if (!info.mHomingIndexSig)
                    return MC_ErrorCode::HOMING_SIG_ILLEGAL;

                if (info.mHomingIndexSigBitOffset < 0 || info.mHomingIndexSigBitOffset > 7)
                    return MC_ErrorCode::HOMING_SIG_ILLEGAL;
            }
        }

        mImpl_->mHomingInfo.mHomingSig = info.mHomingSig;
        mImpl_->mHomingInfo.mHomingSigBitOffset = info.mHomingSigBitOffset;
        mImpl_->mHomingInfo.mHomingIndexSig = info.mHomingIndexSig;
        mImpl_->mHomingInfo.mHomingIndexSigBitOffset = info.mHomingIndexSigBitOffset;
        mImpl_->mHomingInfo.mHomingMode = info.mHomingMode;
        mImpl_->mHomingInfo.mHomingVelSearch = fabs(info.mHomingVelSearch);
        mImpl_->mHomingInfo.mHomingVelRegression = fabs(info.mHomingVelRegression);
        mImpl_->mHomingInfo.mHomingAcc = fabs(info.mHomingAcc);
        mImpl_->mHomingInfo.mHomingJerk = fabs(info.mHomingJerk);
        mImpl_->mHomingInfo.mHomingSigVal = false;
        mImpl_->mHomingInfo.mStopOnIndex = false;
        mImpl_->mHomingInfo.mIndexSearchAfterSwitch = true;

        switch (info.mHomingMode)
        {
        case MC_HomingMode::DIRECT:
            mImpl_->mHomingInfo.mHomingSig = nullptr;
            mImpl_->mHomingInfo.mHomingIndexSig = nullptr;
            return MC_ErrorCode::GOOD;

        case MC_HomingMode::MODE1:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE2:
            mImpl_->mHomingInfo.mHomingVelSearch = -fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = true;
            break;

        case MC_HomingMode::MODE3:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE4:
            mImpl_->mHomingInfo.mHomingVelSearch = fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = -fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = true;
            break;

        case MC_HomingMode::MODE5:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE6:
            mImpl_->mHomingInfo.mHomingVelSearch = -fabs(info.mHomingVelSearch);

            mImpl_->mHomingInfo.mHomingVelRegression = fabs(info.mHomingVelRegression);
            break;

        case MC_HomingMode::MODE7:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE8:
            mImpl_->mHomingInfo.mHomingVelSearch = fabs(info.mHomingVelSearch);

            mImpl_->mHomingInfo.mHomingVelRegression = -fabs(info.mHomingVelRegression);
            break;

        case MC_HomingMode::MODE9:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE10:
            mImpl_->mHomingInfo.mHomingVelSearch = -fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = -fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = false;
            break;

        case MC_HomingMode::MODE11:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            [[fallthrough]];
        case MC_HomingMode::MODE12:
            mImpl_->mHomingInfo.mHomingVelSearch = fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = false;
            break;

        case MC_HomingMode::MODE13:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            mImpl_->mHomingInfo.mHomingVelSearch = -fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = true;
            break;

        case MC_HomingMode::MODE14:
            mImpl_->mHomingInfo.mHomingSigVal = true;
            mImpl_->mHomingInfo.mHomingVelSearch = fabs(info.mHomingVelSearch);
            mImpl_->mHomingInfo.mHomingVelRegression = -fabs(info.mHomingVelRegression);
            mImpl_->mHomingInfo.mStopOnIndex = true;
            mImpl_->mHomingInfo.mIndexSearchAfterSwitch = true;
            break;

        default:
            return MC_ErrorCode::HOMING_MODE_ILLEGAL;
        }

        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxisHoming::addHoming(FunctionBlock *fb, double pos, MC_BufferMode bufferMode, int32_t customId)
    {
        if (!isDefinedBufferMode(bufferMode))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;

        if (!std::isfinite(pos))
            return MC_ErrorCode::POS_ILLEGAL;

        HomingNode *node;
        MC_ErrorCode err = pushAndNewData(
            [&node, pos](void *baseNode) -> AxisExeclNode *
            {
                node = (HomingNode *)baseNode;
                new (node) HomingNode();
                node->mPos = pos;
                return node;
            },
            !usesQueuedBufferModeSemantics(bufferMode),
            fb,
            MC_AxisStatus::HOMING,
            MC_AxisStatus::STANDSTILL,
            customId,
            bufferMode);

        if (MC_ErrorCode::GOOD != err)
            return err;

        return MC_ErrorCode::GOOD;
    }

    void AxisHoming::onPowerStatusChangedHandler(AxisBase *this_, bool powerStatus)
    {
        AxisHoming *this__ = dynamic_cast<AxisHoming *>(this_);
        if (powerStatus)
            this__->mImpl_->mPlanner.setFrequency(this__->frequency());
    }

    void AxisHoming::onPositionOffsetHandler(AxisBase *this_, double positionOffset)
    {
        AxisHoming *this__ = dynamic_cast<AxisHoming *>(this_);
        this__->mImpl_->mPlanner.setPositionOffset(positionOffset);
    }

} // namespace plcopen
