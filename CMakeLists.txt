cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

project(
  plcopen
  VERSION 0.11.0
  DESCRIPTION "a c++ library for plcopen"
  HOMEPAGE_URL "http://github.com/lusipad/plcopen"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

set(PLCOPEN_IS_TOP_LEVEL OFF)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(PLCOPEN_IS_TOP_LEVEL ON)
endif()

option(PLCOPEN_BUILD_DEMOS "Build plcopen demo executables" ${PLCOPEN_IS_TOP_LEVEL})
option(PLCOPEN_BUILD_CORE "Build plcopen rewrite core targets" ON)
option(PLCOPEN_BUILD_LEGACY "Build legacy v0.x src targets" OFF)
option(PLCOPEN_BUILD_DOCS "Build plcopen API docs with Doxygen" OFF)
option(PLCOPEN_BUILD_PYTHON_BINDINGS "Build pybind11-based Python bindings" OFF)
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
  include(FetchContent)
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

  set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
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

  pybind11_add_module(pyplcopen python/pyplcopen.cpp)
  target_link_libraries(pyplcopen PRIVATE plcopen::plcopen)

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
axis.move_relative(-0.5, 1.0, 1.0, 1.0)
assert abs(axis.command_position() - 1.5) < 1e-8
axis.move_velocity(1.0)
assert axis.command_velocity() > 0.0
axis.halt()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL
]=])
    add_test(NAME pyplcopen_smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/pyplcopen_smoke_$<CONFIG>.py)
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
