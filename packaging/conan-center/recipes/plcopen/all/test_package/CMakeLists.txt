cmake_minimum_required(VERSION 3.21)
project(plcopen_test_package LANGUAGES CXX)

find_package(plcopen CONFIG REQUIRED)

add_executable(plcopen_test_package test_package.cpp)
target_compile_features(plcopen_test_package PRIVATE cxx_std_17)
target_link_libraries(plcopen_test_package PRIVATE plcopen::plcopen)
