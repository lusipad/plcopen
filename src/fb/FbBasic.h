/*
 * FbBasic.h
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

#ifndef _URANUS_FBBASIC_HPP_
#define _URANUS_FBBASIC_HPP_

#include "FbPLCOpenBase.h"

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    /**
     * @brief Base class for non-motion IEC function blocks.
     */
    class FbBasicType : public FunctionBlock
    {
    public:
        virtual void call(void) = 0;
    };

    /**
     * @brief Helper for timers that advance on a fixed scan cycle.
     */
    class FbCycleTimeAwareType : public FbBasicType
    {
    public:
        bool setCycleTime(TIME cycleTime);
        TIME cycleTime(void) const;

    protected:
        TIME advanceElapsed(TIME currentElapsed, TIME targetElapsed) const;

    private:
        TIME mCycleTime = 1;
    };

    /**
     * @brief Rising-edge detector.
     */
    class FbRTrig : public FbBasicType
    {
    public:
        FB_INPUT BOOL mCLK = false;
        FB_OUTPUT BOOL mQ = false;

    public:
        void call(void);

    private:
        BOOL mPreviousCLK = false;
        BOOL mInitialized = false;
    };

    /**
     * @brief Falling-edge detector.
     */
    class FbFTrig : public FbBasicType
    {
    public:
        FB_INPUT BOOL mCLK = false;
        FB_OUTPUT BOOL mQ = false;

    public:
        void call(void);

    private:
        BOOL mPreviousCLK = false;
        BOOL mInitialized = false;
    };

    /**
     * @brief Set-dominant bistable latch.
     */
    class FbSr : public FbBasicType
    {
    public:
        FB_INPUT BOOL mS1 = false;
        FB_INPUT BOOL mR = false;
        FB_OUTPUT BOOL mQ1 = false;

    public:
        void call(void);
    };

    /**
     * @brief Reset-dominant bistable latch.
     */
    class FbRs : public FbBasicType
    {
    public:
        FB_INPUT BOOL mS = false;
        FB_INPUT BOOL mR1 = false;
        FB_OUTPUT BOOL mQ1 = false;

    public:
        void call(void);
    };

    /**
     * @brief Turn-on delay timer.
     */
    class FbTon : public FbCycleTimeAwareType
    {
    public:
        FB_INPUT BOOL mIN = false;
        FB_INPUT TIME mPT = 0;
        FB_OUTPUT BOOL mQ = false;
        FB_OUTPUT TIME mET = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousIN = false;
    };

    /**
     * @brief Turn-off delay timer.
     */
    class FbTof : public FbCycleTimeAwareType
    {
    public:
        FB_INPUT BOOL mIN = false;
        FB_INPUT TIME mPT = 0;
        FB_OUTPUT BOOL mQ = false;
        FB_OUTPUT TIME mET = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousIN = false;
    };

    /**
     * @brief Pulse timer.
     */
    class FbTp : public FbCycleTimeAwareType
    {
    public:
        FB_INPUT BOOL mIN = false;
        FB_INPUT TIME mPT = 0;
        FB_OUTPUT BOOL mQ = false;
        FB_OUTPUT TIME mET = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousIN = false;
        BOOL mInitialized = false;
        BOOL mTiming = false;
    };

    /**
     * @brief Real-time clock accumulator.
     */
    class FbRtc : public FbCycleTimeAwareType
    {
    public:
        FB_INPUT BOOL mEN = false;
        FB_INPUT DT mPDT = 0;
        FB_OUTPUT BOOL mQ = false;
        FB_OUTPUT DT mCDT = 0;

    public:
        void call(void);

    private:
        BOOL mRunning = false;
    };

    /**
     * @brief Up-counter.
     */
    class FbCtu : public FbBasicType
    {
    public:
        FB_INPUT BOOL mCU = false;
        FB_INPUT BOOL mR = false;
        FB_INPUT INT mPV = 0;
        FB_OUTPUT BOOL mQ = false;
        FB_OUTPUT INT mCV = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousCU = false;
        BOOL mInitialized = false;
    };

    /**
     * @brief Down-counter.
     */
    class FbCtd : public FbBasicType
    {
    public:
        FB_INPUT BOOL mCD = false;
        FB_INPUT BOOL mLD = false;
        FB_INPUT INT mPV = 0;
        FB_OUTPUT BOOL mQ = true;
        FB_OUTPUT INT mCV = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousCD = false;
        BOOL mInitialized = false;
    };

    /**
     * @brief Up/down counter.
     */
    class FbCtud : public FbBasicType
    {
    public:
        FB_INPUT BOOL mCU = false;
        FB_INPUT BOOL mCD = false;
        FB_INPUT BOOL mR = false;
        FB_INPUT BOOL mLD = false;
        FB_INPUT INT mPV = 0;
        FB_OUTPUT BOOL mQU = false;
        FB_OUTPUT BOOL mQD = true;
        FB_OUTPUT INT mCV = 0;

    public:
        void call(void);

    private:
        BOOL mPreviousCU = false;
        BOOL mPreviousCD = false;
        BOOL mInitializedCU = false;
        BOOL mInitializedCD = false;
    };

#pragma pack(pop)

} // namespace plcopen

#endif /** _URANUS_FBBASIC_HPP_ **/
