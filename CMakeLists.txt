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
