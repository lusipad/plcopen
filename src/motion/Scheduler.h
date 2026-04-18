/*
 * Scheduler.h
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

#ifndef _URANUS_SCHEDULER_HPP_
#define _URANUS_SCHEDULER_HPP_

#include "Global.h"
#include "Servo.h"

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    class Axis;
    /**
     * @brief 单线程周期调度器。
     *
     * 用户按固定频率调用 `runCycle()`，调度器依次推进所有轴与功能块。
     */
    class Scheduler
    {
    public:
        Scheduler();

        virtual ~Scheduler();

        /// 执行一个调度周期。
        void runCycle(void);

        /// 设定调度频率。
        MC_ErrorCode setFrequency(double frequency);

        /// 获取当前调度频率。
        double frequency(void) const;

        /// 获取当前 tick，每次 `runCycle()` 后自增。
        uint32_t tick(void) const;

        /**
         * @brief 新建一个轴实例。
         * @param axisId 轴 ID，要求在当前调度器中唯一。
         * @param servo 伺服抽象实例。
         * @return 创建成功时返回轴指针，否则返回空指针。
         */
        Axis *newAxis(int32_t axisId, Servo *servo);

        /// 通过 ID 获取轴实例。
        Axis *axis(int32_t axisId) const;

        /// 应用一组轴配置。
        MC_ErrorCode setAxisConfig(Axis *axis, const AxisConfig &config);

        /// 直接设置轴的用户坐标零点。
        MC_ErrorCode setAxisHomePosition(Axis *axis, double homePos);

        /// 获取轴链表中的第一个轴。
        Axis *axisListFirst(void) const;

        /// 获取给定轴之后的下一个轴。
        Axis *axisListNext(const Axis *one) const;

        /// 释放当前调度器创建的所有轴。
        void release(void);

    protected:
        virtual void vprintLog(MC_LogLevel level, const char *fmt, va_list ap) {}

    private:
        class SchedulerImpl;
        SchedulerImpl *mImpl_;
        friend class Axis;
    };

#pragma pack(pop)

}

#endif /** _URANUS_SCHEDULER_HPP_ **/
