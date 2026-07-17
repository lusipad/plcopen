# E3 mutation score gate: apply N mutations one at a time, verify existing tests
# catch each one. A mutation is "killed" if the build fails or the test fails.
# Gate threshold: >= 70% kill rate.
#
# Usage: cmake -P cmake/mutation_score_gate.cmake
# Prerequisite: none (configures its own build directory)

cmake_minimum_required(VERSION 3.21)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED PLCOPEN_MUTATION_BUILD_DIR)
    set(BUILD "${ROOT}/build/mutation-score")
else()
    set(BUILD "${PLCOPEN_MUTATION_BUILD_DIR}")
endif()

set(THRESHOLD 70)

# --- Configure once ---
message(STATUS "mutation-score: configuring build...")

set(configure_cmd "${CMAKE_COMMAND}" -S "${ROOT}" -B "${BUILD}"
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPLCOPEN_BUILD_TESTS=ON)

if(DEFINED PLCOPEN_MUTATION_GENERATOR)
    list(APPEND configure_cmd -G "${PLCOPEN_MUTATION_GENERATOR}")
endif()

execute_process(COMMAND ${configure_cmd} RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "mutation-score: configure failed")
endif()

# Build all test targets once (so incremental rebuilds are fast per-mutation)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --parallel
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "mutation-score: initial build failed")
endif()

# --- Mutation registry ---
# Each mutation: FILE, ORIG, REPL, TEST (target name), LABEL (human-readable tag)
set(MUT_COUNT 20)

# -- core/fb/basic.h (PLCopen standard FBs) --
set(MUT_1_FILE  "core/fb/basic.h")
set(MUT_1_ORIG  "q = clk && !memory;")
set(MUT_1_REPL  "q = clk || !memory;")
set(MUT_1_TEST  "plcopen_core_r3_tests")
set(MUT_1_LABEL "rtrig-and-to-or")

set(MUT_2_FILE  "core/fb/basic.h")
set(MUT_2_ORIG  "q = !clk && memory;")
set(MUT_2_REPL  "q = clk && memory;")
set(MUT_2_TEST  "plcopen_core_r3_tests")
set(MUT_2_LABEL "ftrig-remove-not")

set(MUT_3_FILE  "core/fb/basic.h")
set(MUT_3_ORIG  "q = set || (q && !reset);")
set(MUT_3_REPL  "q = set && (q && !reset);")
set(MUT_3_TEST  "plcopen_core_r3_tests")
set(MUT_3_LABEL "sr-or-to-and")

set(MUT_4_FILE  "core/fb/basic.h")
set(MUT_4_ORIG  "q = (q || set) && !reset;")
set(MUT_4_REPL  "q = (q || set) || !reset;")
set(MUT_4_TEST  "plcopen_core_r3_tests")
set(MUT_4_LABEL "rs-and-to-or")

set(MUT_5_FILE  "core/fb/basic.h")
set(MUT_5_ORIG  "q = et >= pt;")
set(MUT_5_REPL  "q = et > pt;")
set(MUT_5_TEST  "plcopen_core_r3_tests")
set(MUT_5_LABEL "ton-gte-to-gt")

set(MUT_6_FILE  "core/fb/basic.h")
set(MUT_6_ORIG  "q = et < pt;")
set(MUT_6_REPL  "q = et <= pt;")
set(MUT_6_TEST  "plcopen_core_r3_tests")
set(MUT_6_LABEL "tof-lt-to-lte")

set(MUT_7_FILE  "core/fb/basic.h")
set(MUT_7_ORIG  "qd = cv <= 0;")
set(MUT_7_REPL  "qd = cv < 0;")
set(MUT_7_TEST  "plcopen_core_r3_tests")
set(MUT_7_LABEL "ctud-lte-to-lt")

# -- core/rt/error.h (Result type) --
set(MUT_8_FILE  "core/rt/error.h")
set(MUT_8_ORIG  "return Result(true, ErrorCode::ok, value);")
set(MUT_8_REPL  "return Result(false, ErrorCode::ok, value);")
set(MUT_8_TEST  "plcopen_core_rt_tests")
set(MUT_8_LABEL "result-success-flip")

set(MUT_9_FILE  "core/rt/error.h")
set(MUT_9_ORIG  "return Result(false, error, T{});")
set(MUT_9_REPL  "return Result(true, error, T{});")
set(MUT_9_TEST  "plcopen_core_rt_tests")
set(MUT_9_LABEL "result-failure-flip")

# -- core/otg/profile1d.h (OTG planner internals) --
set(MUT_10_FILE  "core/otg/profile1d.h")
set(MUT_10_ORIG  "duration_cycles_ += segment.duration_cycles;")
set(MUT_10_REPL  "duration_cycles_ -= segment.duration_cycles;")
set(MUT_10_TEST  "plcopen_core_otg_time_optimal_tests")
set(MUT_10_LABEL "duration-add-to-sub")

set(MUT_11_FILE  "core/otg/profile1d.h")
set(MUT_11_ORIG  "segment.c1 + 2.0 * segment.c2 * x")
set(MUT_11_REPL  "segment.c1 + 3.0 * segment.c2 * x")
set(MUT_11_TEST  "plcopen_core_otg_time_optimal_tests")
set(MUT_11_LABEL "velocity-coeff-2-to-3")

set(MUT_12_FILE  "core/otg/profile1d.h")
set(MUT_12_ORIG  "remaining -= current.duration_cycles;")
set(MUT_12_REPL  "remaining += current.duration_cycles;")
set(MUT_12_TEST  "plcopen_core_otg_time_optimal_tests")
set(MUT_12_LABEL "sample-remaining-add-to-sub")

set(MUT_13_FILE  "core/otg/time_optimal.h")
set(MUT_13_ORIG  "0.5 * (va + vb) * duration")
set(MUT_13_REPL  "0.5 * (va - vb) * duration")
set(MUT_13_TEST  "plcopen_core_otg_time_optimal_tests")
set(MUT_13_LABEL "ramp-distance-add-to-sub")

# -- core/geom/geometry.h (vector math) --
set(MUT_14_FILE  "core/geom/geometry.h")
set(MUT_14_ORIG  "return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;")
set(MUT_14_REPL  "return lhs.x * rhs.x - lhs.y * rhs.y + lhs.z * rhs.z;")
set(MUT_14_TEST  "plcopen_core_r2_tests")
set(MUT_14_LABEL "dot-add-to-sub")

set(MUT_15_FILE  "core/geom/geometry.h")
set(MUT_15_ORIG  "return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};")
set(MUT_15_REPL  "return {lhs.x + rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};")
set(MUT_15_TEST  "plcopen_core_r2_tests")
set(MUT_15_LABEL "vec3-sub-x-to-add")

set(MUT_16_FILE  "core/geom/geometry.h")
set(MUT_16_ORIG  "while(sweep < 0.0) {")
set(MUT_16_REPL  "while(sweep > 0.0) {")
set(MUT_16_TEST  "plcopen_core_r2_tests")
set(MUT_16_LABEL "arc-sweep-lt-to-gt")

# -- core/rt/static_vector.h (container) --
set(MUT_17_FILE  "core/rt/static_vector.h")
set(MUT_17_ORIG  "return size_ == 0;")
set(MUT_17_REPL  "return size_ != 0;")
set(MUT_17_TEST  "plcopen_core_rt_tests")
set(MUT_17_LABEL "empty-eq-to-neq")

set(MUT_18_FILE  "core/rt/static_vector.h")
set(MUT_18_ORIG  "return size_ == Capacity;")
set(MUT_18_REPL  "return size_ != Capacity;")
set(MUT_18_TEST  "plcopen_core_rt_tests")
set(MUT_18_LABEL "full-eq-to-neq")

# -- core/st (compiler success contract and VM instruction budget) --
set(MUT_19_FILE  "core/st/compile.h")
set(MUT_19_ORIG  "result.ok = true;")
set(MUT_19_REPL  "result.ok = false;")
set(MUT_19_TEST  "plcopen_core_st_l0_compiler_tests")
set(MUT_19_LABEL "st-compile-success-flip")

set(MUT_20_FILE  "core/st/vm.h")
set(MUT_20_ORIG  "if(remaining <= 0) {")
set(MUT_20_REPL  "if(remaining < 0) {")
set(MUT_20_TEST  "plcopen_core_st_l0_runtime_tests")
set(MUT_20_LABEL "st-budget-boundary-off-by-one")

# --- Run mutations ---
message(STATUS "mutation-score: running ${MUT_COUNT} mutations...")

set(KILLED 0)
set(SURVIVED 0)
set(SKIPPED 0)
set(SURVIVED_LIST "")

foreach(i RANGE 1 ${MUT_COUNT})
    set(target_path "${ROOT}/${MUT_${i}_FILE}")

    # Read original
    file(READ "${target_path}" original_content)
    string(FIND "${original_content}" "${MUT_${i}_ORIG}" found_pos)

    if(found_pos LESS 0)
        message(STATUS "  [${i}/${MUT_COUNT}] SKIP  ${MUT_${i}_LABEL} (target not found)")
        math(EXPR SKIPPED "${SKIPPED} + 1")
        continue()
    endif()

    # Apply mutation
    string(REPLACE "${MUT_${i}_ORIG}" "${MUT_${i}_REPL}" mutated_content "${original_content}")
    file(WRITE "${target_path}" "${mutated_content}")

    # Incremental build of the specific test target
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --target ${MUT_${i}_TEST} --parallel
        RESULT_VARIABLE build_rc
        OUTPUT_QUIET ERROR_QUIET)

    if(NOT build_rc EQUAL 0)
        message(STATUS "  [${i}/${MUT_COUNT}] KILLED(build) ${MUT_${i}_LABEL}")
        math(EXPR KILLED "${KILLED} + 1")
        file(WRITE "${target_path}" "${original_content}")
        continue()
    endif()

    # Run the test (timeout guards against mutant infinite loops)
    execute_process(
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD}" -R "^${MUT_${i}_TEST}$"
                --output-on-failure --timeout 60
        RESULT_VARIABLE test_rc
        OUTPUT_QUIET ERROR_QUIET
        TIMEOUT 120)

    # Restore original immediately
    file(WRITE "${target_path}" "${original_content}")

    if(NOT test_rc EQUAL 0)
        message(STATUS "  [${i}/${MUT_COUNT}] KILLED(test)  ${MUT_${i}_LABEL}")
        math(EXPR KILLED "${KILLED} + 1")
    else()
        message(STATUS "  [${i}/${MUT_COUNT}] SURVIVED     ${MUT_${i}_LABEL}")
        math(EXPR SURVIVED "${SURVIVED} + 1")
        string(APPEND SURVIVED_LIST "\n    - ${MUT_${i}_LABEL} (${MUT_${i}_FILE})")
    endif()
endforeach()

# --- Report ---
math(EXPR TESTED "${KILLED} + ${SURVIVED}")

if(TESTED EQUAL 0)
    message(FATAL_ERROR "mutation-score: no mutations were tested (all skipped)")
endif()

math(EXPR SCORE "${KILLED} * 100 / ${TESTED}")

message(STATUS "")
message(STATUS "mutation-score: ${KILLED}/${TESTED} killed, ${SURVIVED} survived, ${SKIPPED} skipped (${SCORE}%)")

if(NOT "${SURVIVED_LIST}" STREQUAL "")
    message(STATUS "  survivors:${SURVIVED_LIST}")
endif()

if(SCORE LESS ${THRESHOLD})
    message(FATAL_ERROR "mutation-score: ${SCORE}% < ${THRESHOLD}% threshold — add tests to kill survivors")
endif()

message(STATUS "mutation-score: PASSED (${SCORE}% >= ${THRESHOLD}%)")
