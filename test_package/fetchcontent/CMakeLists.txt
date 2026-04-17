cmake_minimum_required(VERSION 3.21)

project(plcopen_fetchcontent_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(NOT DEFINED PLCOPEN_SOURCE_DIR)
  message(FATAL_ERROR "Set PLCOPEN_SOURCE_DIR to a local plcopen checkout.")
endif()

FetchContent_Declare(
  plcopen
  SOURCE_DIR "${PLCOPEN_SOURCE_DIR}")

FetchContent_MakeAvailable(plcopen)

add_executable(plcopen_fetchcontent_smoke main.cpp)
target_link_libraries(plcopen_fetchcontent_smoke PRIVATE plcopen::plcopen)
