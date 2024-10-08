# 
# Copyright 2020 (C) SYMG(Shanghai) Intelligence System Co.,Ltd
# 
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
# 

PROJECT(Uranus)

INCLUDE(Uranus.cmake)

file(GLOB_RECURSE URANUS_HEADER  ${CMAKE_CURRENT_SOURCE_DIR}/*.h )
source_group("Header Files" FILES ${URANUS_HEADER}) 

ADD_LIBRARY(${PROJECT_NAME} SHARED ${URANUS_SOURCE} ${URANUS_HEADER})

SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

ADD_EXECUTABLE(axis_move demo/axis_move.cpp)
TARGET_LINK_LIBRARIES(axis_move ${PROJECT_NAME})

ADD_EXECUTABLE(axis_homing demo/axis_homing.cpp)
TARGET_LINK_LIBRARIES(axis_homing ${PROJECT_NAME})

ADD_EXECUTABLE(axis_move_oscilloscope demo/axis_move_oscilloscope.cpp)
TARGET_LINK_LIBRARIES(axis_move_oscilloscope ${PROJECT_NAME})

INSTALL(TARGETS Uranus
    LIBRARY DESTINATION lib
)

INSTALL(FILES 
    fb/FunctionBlock.h 
    fb/FbPLCOpenBase.h 
    fb/FbSingleAxis.h
    fb/PLCTypes.h
    motion/Servo.h 
    motion/Global.h 
    motion/Scheduler.h
    DESTINATION include/Uranus
)
