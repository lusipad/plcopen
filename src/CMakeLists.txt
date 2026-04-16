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

project(plcopen)

include(plcopen.cmake)

file(GLOB_RECURSE PLCOPEN_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
source_group("Header Files" FILES ${PLCOPEN_HEADER})

add_library(${PROJECT_NAME} SHARED ${PLCOPEN_SOURCE} ${PLCOPEN_HEADER})

set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

add_executable(axis_move demo/axis_move.cpp)
target_link_libraries(axis_move ${PROJECT_NAME})

add_executable(axis_homing demo/axis_homing.cpp)
target_link_libraries(axis_homing ${PROJECT_NAME})

add_executable(axis_move_oscilloscope demo/axis_move_oscilloscope.cpp)
target_link_libraries(axis_move_oscilloscope ${PROJECT_NAME})

# 添加测试可执行文件
add_executable(test_basic test/test_basic.cpp)
target_link_libraries(test_basic ${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
)

install(FILES
    fb/FunctionBlock.h 
    fb/FbPLCOpenBase.h 
    fb/FbSingleAxis.h
    fb/PLCTypes.h
    motion/Servo.h 
    motion/Global.h 
    motion/Scheduler.h
    DESTINATION include/plcopen
)
