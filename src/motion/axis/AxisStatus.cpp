/*
 * AxisStatus.cpp
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

#include "AxisStatus.h"
#include <stdio.h>
namespace Uranus
{

class AxisStatus::AxisStatusImpl
{
  public:
    MC_ErrorCode statusToError(void);

    MC_AxisStatus mStatus = MC_AxisStatus::DISABLED;
};

MC_ErrorCode AxisStatus::AxisStatusImpl::statusToError(void)
{
    switch (mStatus)
    {
    case MC_AxisStatus::DISABLED:
        return MC_ErrorCode::AXISDISABLED;
    case MC_AxisStatus::STANDSTILL:
        return MC_ErrorCode::AXISSTANDSTILL;
    case MC_AxisStatus::HOMING:
        return MC_ErrorCode::AXISHOMING;
    case MC_AxisStatus::DISCRETEMOTION:
        return MC_ErrorCode::AXISDISCRETEMOTION;
    case MC_AxisStatus::CONTINUOUSMOTION:
        return MC_ErrorCode::AXISCONTINUOUSMOTION;
    case MC_AxisStatus::SYNCHRONIZEDMOTION:
        return MC_ErrorCode::AXISSYNCHRONIZEDMOTION;
    case MC_AxisStatus::STOPPING:
        return MC_ErrorCode::AXISSTOPPING;
    case MC_AxisStatus::ERRORSTOP:
        return MC_ErrorCode::AXISERRORSTOP;
    }

    return MC_ErrorCode::AXISDISABLED;
}

AxisStatus::AxisStatus()
{
    mImpl_ = new AxisStatusImpl();
    URANUS_ADD_HANDLER(onError, onErrorHandler);
    URANUS_ADD_HANDLER(onPowerStatusChanged, onPowerStatusChangedHandler);
}

AxisStatus::~AxisStatus()
{
    delete mImpl_;
}

MC_AxisStatus AxisStatus::status(void)
{
    return mImpl_->mStatus;
}

MC_ErrorCode AxisStatus::testStatus(MC_AxisStatus status)
{
    if (status == mImpl_->mStatus)
        return MC_ErrorCode::GOOD;

    switch (mImpl_->mStatus)
    {
    case MC_AxisStatus::ERRORSTOP:
    case MC_AxisStatus::DISABLED:
        goto FAILED;

    case MC_AxisStatus::STANDSTILL:
        switch (status)
        {
        case MC_AxisStatus::DISCRETEMOTION:
        case MC_AxisStatus::CONTINUOUSMOTION:
        case MC_AxisStatus::SYNCHRONIZEDMOTION:
        case MC_AxisStatus::STOPPING:
        case MC_AxisStatus::HOMING:
            goto SUCCESS;
        default:
            goto FAILED;
        }

    case MC_AxisStatus::DISCRETEMOTION:
    case MC_AxisStatus::CONTINUOUSMOTION:
    case MC_AxisStatus::SYNCHRONIZEDMOTION:
        switch (status)
        {
        case MC_AxisStatus::DISCRETEMOTION:
        case MC_AxisStatus::CONTINUOUSMOTION:
        case MC_AxisStatus::SYNCHRONIZEDMOTION:
        case MC_AxisStatus::STANDSTILL:
        case MC_AxisStatus::STOPPING:
            goto SUCCESS;
        default:
            goto FAILED;
        }

    case MC_AxisStatus::HOMING:
        switch (status)
        {
        case MC_AxisStatus::STANDSTILL:
        case MC_AxisStatus::STOPPING:
            goto SUCCESS;
        default:
            goto FAILED;
        }

    case MC_AxisStatus::STOPPING:
        switch (status)
        {
        case MC_AxisStatus::STANDSTILL:
            goto SUCCESS;
        default:
            goto FAILED;
        }

    default:
        goto FAILED;
    }

SUCCESS:
    return MC_ErrorCode::GOOD;

FAILED:
    return mImpl_->statusToError();
}

MC_ErrorCode AxisStatus::setStatus(MC_AxisStatus status)
{
    MC_ErrorCode err = testStatus(status);
    if (MC_ErrorCode::GOOD != err)
        return err;

    mImpl_->mStatus = status;
    return MC_ErrorCode::GOOD;
}

void AxisStatus::onErrorHandler(AxisBase *this_, MC_ErrorCode errorCode)
{
    AxisStatus *this__ = dynamic_cast<AxisStatus *>(this_);
    this__->mImpl_->mStatus = MC_AxisStatus::ERRORSTOP;
}

void AxisStatus::onPowerStatusChangedHandler(AxisBase *this_, bool powerStatus)
{
    AxisStatus *this__ = dynamic_cast<AxisStatus *>(this_);
    this__->mImpl_->mStatus = powerStatus ? MC_AxisStatus::STANDSTILL : MC_AxisStatus::DISABLED;
}

} // namespace Uranus
