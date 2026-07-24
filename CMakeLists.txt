cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

project(
  plcopen
  VERSION 0.20.0
  DESCRIPTION "a c++ library for plcopen"
  HOMEPAGE_URL "http://github.com/lusipad/plcopen"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

set(PLCOPEN_IS_TOP_LEVEL OFF)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(PLCOPEN_IS_TOP_LEVEL ON)
endif()

option(PLCOPEN_BUILD_DEMOS "Build plcopen demo executables" ${PLCOPEN_IS_TOP_LEVEL})
option(PLCOPEN_BUILD_CORE "Build plcopen rewrite core targets" ON)
option(PLCOPEN_BUILD_LEGACY "Build legacy v0.x src targets" OFF)
option(PLCOPEN_BUILD_DOCS "Build plcopen API docs with Doxygen" OFF)
option(PLCOPEN_BUILD_PYTHON_BINDINGS "Build pybind11-based Python bindings" OFF)
option(PLCOPEN_ENABLE_PROCESS_INTEGRATION_TESTS
       "Enable cooperative local tests that create named shared memory and child processes" OFF)
set(PLCOPEN_DEFAULT_BUILD_TESTS OFF)
if(PLCOPEN_IS_TOP_LEVEL)
  set(PLCOPEN_DEFAULT_BUILD_TESTS ${BUILD_TESTING})
endif()
option(PLCOPEN_BUILD_TESTS "Build plcopen tests" ${PLCOPEN_DEFAULT_BUILD_TESTS})
unset(PLCOPEN_DEFAULT_BUILD_TESTS)

if(PLCOPEN_BUILD_CORE)
  add_subdirectory(core)
endif()

if(PLCOPEN_BUILD_LEGACY)
  add_subdirectory(src)
endif()

if(PLCOPEN_IS_TOP_LEVEL AND BUILD_TESTING)
  add_test(
    NAME replay_fixture_format
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_replay_fixtures.cmake)
endif()

if(PLCOPEN_BUILD_DOCS)
  find_package(Doxygen QUIET)

  if(Doxygen_FOUND)
    set(PLCOPEN_DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs/api)
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in
      ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      @ONLY)

    add_custom_target(
      docs
      COMMAND ${CMAKE_COMMAND} -E make_directory ${PLCOPEN_DOXYGEN_OUTPUT_DIR}
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating plcopen API documentation with Doxygen"
      VERBATIM)
  else()
    message(STATUS "Doxygen not found; 'docs' target will print installation guidance.")
    add_custom_target(
      docs
      COMMAND ${CMAKE_COMMAND} -E echo "Doxygen not found. Install Doxygen and reconfigure with -DPLCOPEN_BUILD_DOCS=ON to generate API docs."
      VERBATIM)
  endif()
endif()

if(PLCOPEN_BUILD_PYTHON_BINDINGS)
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

  set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
  find_package(pybind11 CONFIG QUIET)
  if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG v2.13.6)
    if(DEFINED CMAKE_WARN_DEPRECATED)
      set(_PLCOPEN_WARN_DEPRECATED_DEFINED TRUE)
      set(_PLCOPEN_WARN_DEPRECATED_VALUE "${CMAKE_WARN_DEPRECATED}")
    else()
      set(_PLCOPEN_WARN_DEPRECATED_DEFINED FALSE)
    endif()
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(pybind11)
    if(_PLCOPEN_WARN_DEPRECATED_DEFINED)
      set(CMAKE_WARN_DEPRECATED "${_PLCOPEN_WARN_DEPRECATED_VALUE}" CACHE BOOL "" FORCE)
    else()
      unset(CMAKE_WARN_DEPRECATED CACHE)
    endif()
    unset(_PLCOPEN_WARN_DEPRECATED_DEFINED)
    unset(_PLCOPEN_WARN_DEPRECATED_VALUE)
  endif()

  pybind11_add_module(pyplcopen python/pyplcopen.cpp)
  target_link_libraries(pyplcopen PRIVATE plcopen::plcopen)
  install(TARGETS pyplcopen LIBRARY DESTINATION .)

  if(BUILD_TESTING)
    file(GENERATE
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_smoke_$<CONFIG>.py
      CONTENT [=[
import sys
sys.path.insert(0, r"$<TARGET_FILE_DIR:pyplcopen>")
import pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()
axis.move_absolute(2.0, 1.0, 1.0, 1.0)
assert abs(axis.command_position() - 2.0) < 1e-8
assert axis.command_acceleration() == 0.0
assert axis.actual_acceleration() == 0.0
axis.move_relative(-0.5, 1.0, 1.0, 1.0)
assert abs(axis.command_position() - 1.5) < 1e-8
axis.move_velocity(1.0)
assert axis.command_velocity() > 0.0
axis.halt()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL
axis.move_velocity(1.0)
axis.stop()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL
axis.home_direct(0.25)
assert abs(axis.home_position() - 0.25) < 1e-12
assert abs(axis.actual_position() - 0.25) < 1e-12
command_before_feedback = (
    axis.command_position(),
    axis.command_velocity(),
    axis.command_acceleration(),
)
axis.set_actual_feedback(0.2, -0.03, 0.004, 0.5)
assert abs(axis.actual_position() - 0.2) < 1e-12
assert abs(axis.actual_velocity() + 0.03) < 1e-12
assert abs(axis.actual_acceleration() - 0.004) < 1e-12
assert abs(axis.actual_torque() - 0.5) < 1e-12
assert command_before_feedback == (
    axis.command_position(),
    axis.command_velocity(),
    axis.command_acceleration(),
)
try:
    axis.set_actual_feedback(float("nan"), 0.0)
    raise AssertionError("non-finite actual feedback was accepted")
except RuntimeError as error:
    assert "invalid_argument" in str(error)

# Z2 SI configuration objects apply one load-domain conversion and round-trip
# through the same public boundary.
cycle = pyplcopen.CycleConfig.at_1khz()
si = pyplcopen.AxisSiConfig()
si.max_velocity = 200.0
si.max_acceleration = 1000.0
si.max_deceleration = 1000.0
si.max_jerk = 50000.0
configured_axis = pyplcopen.AxisSim()
configured_axis.configure_si(cycle, si)
si_read = configured_axis.si_config(cycle)
assert abs(si_read.max_velocity - 200.0) < 1e-9
assert abs(si_read.max_jerk - 50000.0) < 1e-3

diagnostic = pyplcopen.diagnose(pyplcopen.ErrorCode.INVALID_ARGUMENT)
assert diagnostic.code == pyplcopen.ErrorCode.INVALID_ARGUMENT
assert diagnostic.name == "invalid_argument"
assert "NaN/Inf" in diagnostic.hint
assert pyplcopen.error_text(pyplcopen.ErrorCode.INVALID_ARGUMENT).startswith("invalid_argument:")
assert pyplcopen.diagnose(pyplcopen.ErrorCode.NOT_CONVERGED).name == "not_converged"
assert pyplcopen.diagnose(pyplcopen.ErrorCode.SINGULAR_REGION).name == "singular_region"
assert pyplcopen.diagnose(pyplcopen.ErrorCode.LIMIT_INFEASIBLE).name == "limit_infeasible"

# B9 stream session demo (KB-035): a 100 Hz sine target stream upsampled to
# the cycle rate; the stream stalls at the end and the filter comes to a
# jerk-limited controlled stop; a standard exit follows at rest.
import math
axis.home_direct(0.0)
axis.stream_engage(0.5, 0.05, 0.01, timeout_cycles=30, extrapolation_cycles=40)
assert axis.status() == pyplcopen.AxisStatus.SYNCHRONIZED_MOTION
target = 0.0
for k in range(200):
    target = 0.3 * math.sin(0.02 * k)
    axis.stream_push(target, axis.stream_now() + 1)
    axis.cycle(10)
    assert abs(axis.command_velocity()) <= 0.5 + 1e-9
assert axis.stream_mode() == "tracking"
assert abs(axis.command_position() - target) < 0.01
axis.cycle(400)
assert axis.stream_mode() == "stopped"
assert axis.stream_dropouts() == 1
assert abs(axis.command_velocity()) < 1e-9
axis.stream_disengage()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL

# Pose surface (KB-042/043/044): TCP pose command over a Cartesian line,
# then read back in the same frame.
arm = pyplcopen.PoseArmSim()
group_si = pyplcopen.GroupSiConfig()
group_si.cycle = cycle
for index in range(6):
    group_si.set_axis(index, si)
arm.configure_si(group_si)
assert arm.si_config(cycle).count == 6
arm.move_joints([0.3, 0.6, 1.0, -0.4, 0.9, 0.2], 0.05, 0.004, 0.004, 0.004)
arm.move_pose(0.35, 0.15, 0.55, 0.3, -0.5, 1.2, 0.01, 0.002, 0.002, 0.002,
              cartesian=True)
pose, gimbal = arm.read_pose()
assert not gimbal
for got, want in zip(pose, [0.35, 0.15, 0.55, 0.3, -0.5, 1.2]):
    assert abs(got - want) < 1e-6

# Cam law generator (KB-046) and the CSV convention round trip.
table = pyplcopen.generate_cam_law("modified_sine", 6.2832, 0.5, 33)
assert len(table) == 33
assert abs(table[-1][1] - 0.5) < 1e-9
import os, tempfile
cam_path = os.path.join(tempfile.gettempdir(), "pyplcopen_cam_smoke.csv")
with open(cam_path, "w") as handle:
    handle.write("# master,slave" + chr(10))
    for master, slave in table:
        handle.write(repr(master) + "," + repr(slave) + chr(10))
loaded = pyplcopen.load_cam_table_csv(cam_path)
assert len(loaded) == 33
assert abs(loaded[-1][1] - 0.5) < 1e-9
]=])
    add_test(NAME pyplcopen_smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_smoke_$<CONFIG>.py)
    file(GENERATE
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_language_tools_$<CONFIG>.py
      CONTENT
"import sys
import unittest

sys.path.insert(0, r\"$<TARGET_FILE_DIR:pyplcopen>\")
sys.path.insert(0, r\"${CMAKE_CURRENT_SOURCE_DIR}/python\")
suite = unittest.defaultTestLoader.discover(
    r\"${CMAKE_CURRENT_SOURCE_DIR}/python/tests\"
)
result = unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
")
    add_test(
      NAME pyplcopen_language_tools
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_language_tools_$<CONFIG>.py)
    add_test(
      NAME pyplcopen_smoke_docs
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/ci/docs_python_smoke.py
              ${CMAKE_CURRENT_SOURCE_DIR}/docs/getting-started/python.md
              $<TARGET_FILE_DIR:pyplcopen>)
    add_test(
      NAME pyplcopen_smoke_notebook
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/ci/notebook_smoke.py
              ${CMAKE_CURRENT_SOURCE_DIR}/docs/notebooks/five-minute-digital-twin.ipynb
              $<TARGET_FILE_DIR:pyplcopen>)
  endif()
endif()

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plcopenConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/plcopenConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcopen)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/plcopenConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

install(
  EXPORT plcopenTargets
  FILE plcopenTargets.cmake
  NAMESPACE plcopen::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcopen)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/plcopenConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/plcopenConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcopen)
