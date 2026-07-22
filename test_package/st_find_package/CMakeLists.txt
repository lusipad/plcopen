cmake_minimum_required(VERSION 3.21)

project(plcopen_st_guide_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plcopen REQUIRED CONFIG)

add_executable(plcopen_st_guide_smoke main.cpp)
target_compile_features(plcopen_st_guide_smoke PRIVATE cxx_std_17)
target_link_libraries(plcopen_st_guide_smoke PRIVATE plcopen::plcopen)
