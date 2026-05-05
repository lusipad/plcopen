/*
 * Servo.h
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

#ifndef PLCOPEN_SERVO_HPP_
#define PLCOPEN_SERVO_HPP_

#include "Global.h"
#include <stdarg.h>

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    /**
     * @brief Abstract servo interface.
     *
     * The default implementation is an in-memory simulated servo. Derived
     * classes can override these virtual methods to connect real hardware.
     */
    class Servo
    {
    public:
        Servo();
        virtual ~Servo();

        /// Enable or disable the servo.
        virtual MC_ServoErrorCode setPower(bool powerStatus, bool &isDone);
        /// Write the commanded position.
        virtual MC_ServoErrorCode setPos(int32_t pos);
        /// Write the commanded velocity.
        virtual MC_ServoErrorCode setVel(int32_t vel);
        /// Write the commanded torque.
        virtual MC_ServoErrorCode setTorque(double torque);
        /// Read the actual position.
        virtual int32_t pos(void);
        /// Read the actual velocity.
        virtual int32_t vel(void);
        /// Read the actual acceleration.
        virtual int32_t acc(void);
        /// Read the actual torque.
        virtual double torque(void);
        /// Report whether communication with the servo is healthy.
        virtual bool communicationReady(void);
        /// Report whether the servo is ready to be powered on.
        virtual bool readyForPowerOn(void);
        /// Report a non-fatal servo warning.
        virtual bool warning(void);
        /// Read a drive-latched position for an input channel when available.
        virtual bool readLatchedPosition(int index, double &position);
        virtual bool readVal(int index, double &value);
        virtual bool writeVal(int index, double value);
        virtual MC_ServoErrorCode resetError(bool &isDone);
        /// Advance the servo state by one cycle.
        virtual void runCycle(double freq);
        virtual void emergStop(void);

    private:
        class ServoImpl;
        ServoImpl *mImpl_;
    };

#pragma pack(pop)

}

#endif /** PLCOPEN_SERVO_HPP_ **/
