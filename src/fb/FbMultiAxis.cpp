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
#include "Axis.h"

namespace plcopen
{

    MC_ErrorCode FbGearIn::onMasterSlaveExecPosedge(void)
    {
        return mSlave->addGearIn(this, mMaster, mRatioNumerator, mRatioDenominator, mBufferMode);
    }

    MC_ErrorCode FbGearOut::onAxisExecPosedge(void)
    {
        return mAxis->addGearOut(this);
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
        return mSlave->addCamIn(this, mMaster, mCamTable, mBufferMode);
    }

    MC_ErrorCode FbCamOut::onAxisExecPosedge(void)
    {
        return mAxis->addCamOut(this);
    }

} // namespace plcopen
