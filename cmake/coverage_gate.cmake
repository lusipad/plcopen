# E4 coverage gate: build with gcov, gate full-core line coverage, and report
# production motion stack and ST branch coverage at 85%.
#
# Usage: cmake -P cmake/coverage_gate.cmake
# Requires: g++ with gcov support, gcovr (pip install gcovr)

cmake_minimum_required(VERSION 3.21)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BUILD "${ROOT}/build/coverage")
set(REPORT_DIR "${ROOT}/out/coverage-linux")
set(LINE_THRESHOLD 90)
set(BRANCH_TARGET 85)

find_program(GCOVR gcovr)
if(NOT GCOVR)
    message(FATAL_ERROR "coverage-gate: gcovr not found (pip install gcovr)")
endif()

# Start from a clean instrumentation build so stale .gcda files cannot skew the gate.
file(REMOVE_RECURSE "${BUILD}")
file(REMOVE_RECURSE "${REPORT_DIR}")
file(MAKE_DIRECTORY "${REPORT_DIR}")

# Configure with coverage flags
message(STATUS "coverage-gate: configuring...")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${ROOT}" -B "${BUILD}"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_CXX_COMPILER=g++
        -DCMAKE_CXX_FLAGS=--coverage
        -DPLCOPEN_BUILD_TESTS=ON
        -DPLCOPEN_BUILD_DEMOS=OFF
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr)
if(NOT rc EQUAL 0)
    message(STATUS "${command_stdout}")
    message(FATAL_ERROR "coverage-gate: configure failed\n${command_stderr}")
endif()

# Build
message(STATUS "coverage-gate: building...")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --parallel
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr)
if(NOT rc EQUAL 0)
    message(STATUS "${command_stdout}")
    message(FATAL_ERROR "coverage-gate: build failed\n${command_stderr}")
endif()

# Run tests
message(STATUS "coverage-gate: running tests...")
execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD}" --output-on-failure
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr)
if(NOT rc EQUAL 0)
    message(STATUS "${command_stdout}")
    message(FATAL_ERROR "coverage-gate: tests failed\n${command_stderr}")
endif()

# Keep the all-core line gate and branch trend transparent, including ST and adapters.
message(STATUS "coverage-gate: computing full-core coverage...")
execute_process(
    COMMAND ${GCOVR}
        --root "${ROOT}"
        "${BUILD}"
        --filter "core/"
        --exclude "core/test/"
        --exclude "core/bench/"
        --exclude "core/demo/"
        --json-summary "${REPORT_DIR}/core-summary.json"
        --fail-under-line ${LINE_THRESHOLD}
        --print-summary
    WORKING_DIRECTORY "${ROOT}"
    RESULT_VARIABLE line_rc)

if(NOT line_rc EQUAL 0)
    message(FATAL_ERROR "coverage-gate: full-core line coverage < ${LINE_THRESHOLD}%")
endif()

# This fixed scope is the production motion stack (L0-L6 plus kin/stream).
# ST and adapters are outer sink consumers and remain visible in the all-core report above.
message(STATUS "coverage-gate: computing production-motion-stack branch coverage...")
execute_process(
    COMMAND ${GCOVR}
        --root "${ROOT}"
        "${BUILD}"
        --filter "core/(rt|otg|geom|plan|exec|axis|fb|kin|stream)/"
        --exclude "core/test/"
        --exclude "core/bench/"
        --exclude "core/demo/"
        --exclude-unreachable-branches
        --exclude-throw-branches
        --json-summary "${REPORT_DIR}/motion-stack-summary.json"
        --fail-under-branch ${BRANCH_TARGET}
        --print-summary
    WORKING_DIRECTORY "${ROOT}"
    RESULT_VARIABLE branch_rc)

# Keep the language implementation independently accountable instead of
# allowing unrelated core tests to hide weak ST branch coverage.
message(STATUS "coverage-gate: computing ST branch coverage...")
execute_process(
    COMMAND ${GCOVR}
        --root "${ROOT}"
        "${BUILD}"
        --filter "core/st/"
        --exclude "core/test/"
        --exclude-unreachable-branches
        --exclude-throw-branches
        --json-summary "${REPORT_DIR}/st-summary.json"
        --fail-under-branch ${BRANCH_TARGET}
        --print-summary
    WORKING_DIRECTORY "${ROOT}"
    RESULT_VARIABLE st_branch_rc)

set(BRANCH_FAILURES)
if(NOT branch_rc EQUAL 0)
    list(APPEND BRANCH_FAILURES
         "production motion stack branch coverage < ${BRANCH_TARGET}%")
endif()
if(NOT st_branch_rc EQUAL 0)
    list(APPEND BRANCH_FAILURES "ST branch coverage < ${BRANCH_TARGET}%")
endif()
if(BRANCH_FAILURES)
    string(REPLACE ";" "\n  - " branch_failure_text "${BRANCH_FAILURES}")
    message(FATAL_ERROR "coverage-gate failed:\n  - ${branch_failure_text}")
endif()

message(STATUS
    "coverage-gate: PASSED (full-core line >= ${LINE_THRESHOLD}%; production motion stack and ST branch >= ${BRANCH_TARGET}%)")
