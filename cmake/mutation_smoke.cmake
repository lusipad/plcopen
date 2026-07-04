cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED PLCOPEN_MUTATION_BUILD_DIR)
    set(PLCOPEN_MUTATION_BUILD_DIR "${PLCOPEN_ROOT}/build/mutation-smoke")
endif()

set(target_file "${PLCOPEN_ROOT}/core/fb/basic.h")
set(original_text "q = clk && !memory;")
set(mutated_text "q = clk && memory;")

file(READ "${target_file}" original)
string(FIND "${original}" "${original_text}" mutation_pos)

if(mutation_pos LESS 0)
    message(FATAL_ERROR "Mutation target not found in ${target_file}")
endif()

string(REPLACE "${original_text}" "${mutated_text}" mutated "${original}")
file(WRITE "${target_file}" "${mutated}")

set(failure "")
set(build_result 1)
set(test_result 1)

set(configure_command
    "${CMAKE_COMMAND}" -S "${PLCOPEN_ROOT}" -B "${PLCOPEN_MUTATION_BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=ON
    -DPLCOPEN_BUILD_TESTS=ON)

if(DEFINED PLCOPEN_MUTATION_GENERATOR)
    list(APPEND configure_command -G "${PLCOPEN_MUTATION_GENERATOR}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result)

if(configure_result EQUAL 0)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${PLCOPEN_MUTATION_BUILD_DIR}" --target plcopen_core_r3_tests --parallel
        RESULT_VARIABLE build_result)
else()
    set(failure "Mutation configure failed")
endif()

if(configure_result EQUAL 0 AND build_result EQUAL 0)
    execute_process(
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PLCOPEN_MUTATION_BUILD_DIR}" --output-on-failure -R plcopen_core_r3_tests
        RESULT_VARIABLE test_result)

    if(test_result EQUAL 0)
        set(failure "Mutation survived: R_TRIG test still passed")
    endif()
elseif(configure_result EQUAL 0)
    set(failure "Mutation build failed before tests could run")
endif()

file(WRITE "${target_file}" "${original}")

if(NOT "${failure}" STREQUAL "")
    message(FATAL_ERROR "${failure}")
endif()

message(STATUS "Mutation smoke passed: plcopen_core_r3_tests killed the edge-detection mutant")
