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

include(plcopen.cmake)

file(GLOB_RECURSE PLCOPEN_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
source_group("Header Files" FILES ${PLCOPEN_HEADER})

add_library(${PROJECT_NAME} SHARED ${PLCOPEN_SOURCE} ${PLCOPEN_HEADER})

if(NOT TARGET plcopen::plcopen)
    add_library(plcopen::plcopen ALIAS ${PROJECT_NAME})
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fb>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/motion>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/motion/axis>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/motion/interpolation>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/misc>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/plcopen>
)

set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

if(PLCOPEN_BUILD_DEMOS)
    add_executable(axis_move demo/axis_move.cpp)
    target_link_libraries(axis_move ${PROJECT_NAME})

    add_executable(axis_homing demo/axis_homing.cpp)
    target_link_libraries(axis_homing ${PROJECT_NAME})

    add_executable(axis_move_oscilloscope demo/axis_move_oscilloscope.cpp)
    target_link_libraries(axis_move_oscilloscope ${PROJECT_NAME})

    add_executable(axis_sync demo/axis_sync.cpp)
    target_link_libraries(axis_sync ${PROJECT_NAME})

    add_executable(axis_gear demo/axis_gear.cpp)
    target_link_libraries(axis_gear ${PROJECT_NAME})

    add_executable(axis_cam demo/axis_cam.cpp)
    target_link_libraries(axis_cam ${PROJECT_NAME})

    add_executable(axes_group_lifecycle demo/axes_group_lifecycle.cpp)
    target_link_libraries(axes_group_lifecycle ${PROJECT_NAME})

    add_executable(basic_fb_cycle demo/basic_fb_cycle.cpp)
    target_link_libraries(basic_fb_cycle ${PROJECT_NAME})
endif()

if(PLCOPEN_BUILD_PYTHON_BINDINGS)
    include(FetchContent)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

    set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(pyplcopen python/pyplcopen.cpp)
    target_link_libraries(pyplcopen PRIVATE ${PROJECT_NAME})

    if(BUILD_TESTING)
        file(GENERATE
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_smoke_$<CONFIG>.py
            CONTENT [=[
import sys
sys.path.insert(0, r"$<TARGET_FILE_DIR:pyplcopen>")
import pyplcopen

axis = pyplcopen.AxisSim(100.0)
axis.power_on()
axis.move_absolute(1.0, 2.0, 4.0, 4.0, 32.0)
assert abs(axis.actual_position() - 1.0) < 1e-2
axis.move_velocity(1.0, 4.0, 4.0, 32.0)
assert axis.command_velocity() > 0.0
assert axis.command_acceleration() >= 0.0
axis.halt(4.0, 32.0)
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL
]=])
        add_test(NAME pyplcopen_smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_smoke_$<CONFIG>.py)
    endif()
endif()

if(PLCOPEN_BUILD_TESTS AND BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.11.0
    )
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    set(PLCOPEN_TEST_SOURCES
        test/test_basic.cpp
        test/test_axis_status.cpp
        test/test_fb_basic.cpp
        test/test_fb_multi_axis.cpp
        test/test_profile_planner.cpp
        test/test_fb_single_axis.cpp
        test/test_axes_group.cpp
    )

    add_executable(test_basic ${PLCOPEN_TEST_SOURCES})
    target_link_libraries(test_basic PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain)
    catch_discover_tests(test_basic DISCOVERY_MODE PRE_TEST)

    if(PLCOPEN_BUILD_DEMOS)
        add_test(NAME axis_sync_smoke COMMAND axis_sync --no-sleep)
        add_test(NAME axis_gear_smoke COMMAND axis_gear --no-sleep)
        add_test(NAME axis_cam_smoke COMMAND axis_cam --no-sleep)
        add_test(NAME axes_group_lifecycle_smoke COMMAND axes_group_lifecycle --no-sleep)
        add_test(NAME basic_fb_cycle_smoke COMMAND basic_fb_cycle --no-sleep)
    endif()
endif()

install(TARGETS ${PROJECT_NAME}
    EXPORT plcopenTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    fb/FunctionBlock.h 
    fb/FbBasic.h
    fb/FbMultiAxis.h
    fb/FbPLCOpenBase.h 
    fb/FbSingleAxis.h
    fb/PLCTypes.h
    misc/Event.h
    misc/ExeclQueue.h
    misc/LinkList.h
    misc/Queue.h
    motion/AxesGroup.h
    motion/CamTable.h
    motion/Servo.h 
    motion/Global.h 
    motion/Scheduler.h
    motion/axis/Axis.h
    motion/axis/AxisBase.h
    motion/axis/AxisHoming.h
    motion/axis/AxisMotion.h
    motion/axis/AxisMotionBase.h
    motion/axis/AxisMove.h
    motion/axis/AxisStatus.h
    motion/axis/AxisSync.h
    motion/interpolation/MathUtils.h
    motion/interpolation/ProfilePlanner.h
    motion/interpolation/ProfilesPlanner.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plcopen
)
