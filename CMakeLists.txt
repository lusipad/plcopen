cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

project(
  plcopen
  VERSION 0.2.0
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
set(PLCOPEN_DEFAULT_BUILD_TESTS OFF)
if(PLCOPEN_IS_TOP_LEVEL)
  set(PLCOPEN_DEFAULT_BUILD_TESTS ${BUILD_TESTING})
endif()
option(PLCOPEN_BUILD_TESTS "Build plcopen tests" ${PLCOPEN_DEFAULT_BUILD_TESTS})
unset(PLCOPEN_DEFAULT_BUILD_TESTS)

add_subdirectory(src)

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
