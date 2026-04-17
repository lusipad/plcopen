cmake_minimum_required(VERSION 3.21)

project(plcopen_find_package_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plcopen REQUIRED CONFIG)

add_executable(plcopen_find_package_smoke main.cpp)
target_link_libraries(plcopen_find_package_smoke PRIVATE plcopen::plcopen)
