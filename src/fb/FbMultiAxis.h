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

#ifndef _URANUS_FBMULTIAXIS_HPP_
#define _URANUS_FBMULTIAXIS_HPP_

#include "FbPLCOpenBase.h"
#include "CamTable.h"

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    class FbGearIn : public FbExecAxisBufferContSyncType
    {
    public:
        FB_INPUT LREAL mRatioNumerator = 1.0;
        FB_INPUT LREAL mRatioDenominator = 1.0;

    public:
        MC_ErrorCode onMasterSlaveExecPosedge(void);
    };

    class FbGearOut : public FbExecAxisType
    {
    public:
        MC_ErrorCode onAxisExecPosedge(void);
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

#endif /** _URANUS_FBMULTIAXIS_HPP_ **/
