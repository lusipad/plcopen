# E4 coverage gate: build with gcov, run tests, gate on ≥90% line coverage.
#
# Usage: cmake -P cmake/coverage_gate.cmake
# Requires: g++ with gcov support, gcovr (pip install gcovr)

cmake_minimum_required(VERSION 3.21)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BUILD "${ROOT}/build/coverage")
set(THRESHOLD 90)

find_program(GCOVR gcovr)
if(NOT GCOVR)
    message(FATAL_ERROR "coverage-gate: gcovr not found (pip install gcovr)")
endif()

# Configure with coverage flags
message(STATUS "coverage-gate: configuring...")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${ROOT}" -B "${BUILD}"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_CXX_COMPILER=g++
        -DCMAKE_CXX_FLAGS=--coverage
        -DPLCOPEN_BUILD_TESTS=ON
        -DPLCOPEN_BUILD_DEMOS=OFF
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "coverage-gate: configure failed")
endif()

# Build
message(STATUS "coverage-gate: building...")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --parallel
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "coverage-gate: build failed")
endif()

# Run tests
message(STATUS "coverage-gate: running tests...")
execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD}" --output-on-failure
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "coverage-gate: tests failed")
endif()

# Run gcovr with fail-under threshold
message(STATUS "coverage-gate: computing coverage...")
execute_process(
    COMMAND ${GCOVR}
        --root "${ROOT}"
        "${BUILD}"
        --filter "core/"
        --exclude "core/test/"
        --exclude "core/bench/"
        --exclude "core/demo/"
        --fail-under-line ${THRESHOLD}
        --print-summary
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "coverage-gate: line coverage < ${THRESHOLD}%")
endif()

message(STATUS "coverage-gate: PASSED (>= ${THRESHOLD}% line coverage)")
