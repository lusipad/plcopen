cmake_minimum_required(VERSION 3.21)

project(plcopen_find_package_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(PLCOPEN_SMOKE_REQUIRE_SERIAL_CHAIN
       "Require the unreleased H2 SerialChain install surface" ON)
option(PLCOPEN_SMOKE_REQUIRE_H1
       "Require the unreleased H1 synchronized joint stream install surface" ON)
option(PLCOPEN_SMOKE_REQUIRE_H3
       "Require the unreleased H3 fixed-base dynamics install surface" ON)

find_package(plcopen REQUIRED CONFIG)

add_executable(plcopen_find_package_smoke main.cpp)
target_link_libraries(plcopen_find_package_smoke PRIVATE plcopen::plcopen)
if(PLCOPEN_SMOKE_REQUIRE_SERIAL_CHAIN)
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_SERIAL_CHAIN=1)
else()
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_SERIAL_CHAIN=0)
endif()
if(PLCOPEN_SMOKE_REQUIRE_H1)
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_H1=1)
else()
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_H1=0)
endif()
if(PLCOPEN_SMOKE_REQUIRE_H3)
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_H3=1)
else()
    target_compile_definitions(plcopen_find_package_smoke
                               PRIVATE PLCOPEN_SMOKE_REQUIRE_H3=0)
endif()
