/*
 * PLCTypes.h
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

#ifndef _URANUS_PLCTYPES_HPP_
#define _URANUS_PLCTYPES_HPP_

#include <cstdint>
#include <memory>
#include "Global.h"

namespace plcopen
{

#pragma pack(push)
#pragma pack(4)

    /**
     * @brief IEC 61131-3 style base type aliases.
     */
    typedef bool BOOL;
    typedef uint8_t BYTE;
    typedef uint8_t USINT;
    typedef int8_t SINT;
    typedef uint16_t WORD;
    typedef uint16_t UINT;
    typedef int16_t INT;
    typedef uint32_t DWORD;  // Fixed: DWORD should be 32-bi
    typedef uint32_t UDINT;
    typedef int32_t DINT;
    typedef uint64_t LWORD;
    typedef uint64_t ULINT;
    typedef int64_t LINT;
    typedef float REAL;
    typedef double LREAL;
    typedef char *STRING;

    typedef char CHAR;        // Single-byte character (8-bit)
    typedef uint16_t WCHAR;   // Double-byte character (16-bit, Unicode support)

    typedef uint32_t TIME;           // Time interval in milliseconds (32-bit)
    typedef uint64_t LTIME;          // Long time interval in nanoseconds (64-bit)
    typedef uint32_t DATE;           // Date as days since 1970-01-01 (32-bit)
    typedef uint64_t LDATE;          // Long date as nanoseconds since 1970-01-01, day multiples only (64-bit)
    typedef uint32_t TIME_OF_DAY;    // Time of day as milliseconds since midnight (32-bit)
    typedef uint32_t TOD;            // Abbreviation for TIME_OF_DAY
    typedef uint64_t LTIME_OF_DAY;   // Long time of day as nanoseconds since midnight (64-bit)
    typedef uint64_t LTOD;           // Abbreviation for LTIME_OF_DAY
    typedef uint64_t DATE_AND_TIME;  // Date and time as milliseconds since 1970-01-01 00:00:00 (64-bit)
    typedef uint64_t DT;             // Abbreviation for DATE_AND_TIME
    typedef uint64_t LDATE_AND_TIME; // Long date and time as nanoseconds since 1970-01-01 00:00:00 (64-bit)
    typedef uint64_t LDT;            // Abbreviation for LDATE_AND_TIME

    // Added WSTRING type (wide string, Unicode support)
    typedef uint16_t *WSTRING;       // Double-byte string (simplified implementation)

    typedef MC_BufferMode MC_BUFFER_MODE;
    typedef MC_TransitionMode MC_TRANSITION_MODE;
    typedef MC_CoordSystem MC_COORD_SYSTEM;
    typedef MC_CircMode MC_CIRC_MODE;
    typedef MC_CircPath MC_CIRC_PATHCHOICE;
    typedef MC_Direction MC_DIRECTION;
    typedef MC_Source MC_SOURCE;
    typedef MC_ErrorCode MC_ERRORCODE;
    typedef MC_ServoErrorCode MC_SERVOERRORCODE;

    /// Single-axis reference type.
    class Axis;
    typedef Axis *AXIS_REF;

    /// Multi-axis group reference type.
    class AxesGroup;
    typedef AxesGroup *AXES_GROUP_REF;

    /// Cam-table reference type.
    class CamTable;
    typedef std::shared_ptr<CamTable> MC_CAM_REF;

#pragma pack(pop)

}

#endif /** _URANUS_PLCTYPES_HPP_ **/
