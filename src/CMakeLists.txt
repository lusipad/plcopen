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

ADD_LIBRARY(${PROJECT_NAME} SHARED ${URANUS_SOURCE})

SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

ADD_EXECUTABLE(axis_move demo/axis_move.cpp)
TARGET_LINK_LIBRARIES(axis_move ${PROJECT_NAME})

ADD_EXECUTABLE(axis_homing demo/axis_homing.cpp)
TARGET_LINK_LIBRARIES(axis_homing ${PROJECT_NAME})

INSTALL(TARGETS Uranus
    LIBRARY DESTINATION lib
)

INSTALL(FILES 
    fb/FunctionBlock.hpp 
    fb/FbPLCOpenBase.hpp 
    fb/FbSingleAxis.hpp
    fb/PLCTypes.hpp
    motion/Servo.hpp 
    motion/Global.hpp 
    motion/Scheduler.hpp
    DESTINATION include/Uranus
)
