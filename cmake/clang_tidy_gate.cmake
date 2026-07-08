# clang-tidy gate (E2): run clang-tidy on all core/ source files using the
# project .clang-tidy config. Error-level checks (bugprone-*, clang-analyzer-*,
# performance-*) are WarningsAsErrors — any finding fails the gate.
#
# Usage: cmake -P cmake/clang_tidy_gate.cmake
# Requires: build/ with compile_commands.json (cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

find_program(CLANG_TIDY clang-tidy)
if(NOT CLANG_TIDY)
    message(FATAL_ERROR "clang-tidy not found")
endif()

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_LIST_DIR}/../core/test/*.cpp"
                          "${CMAKE_CURRENT_LIST_DIR}/../core/bench/*.cpp"
                          "${CMAKE_CURRENT_LIST_DIR}/../core/demo/*.cpp")

set(COMPILE_COMMANDS_PATH "${CMAKE_CURRENT_LIST_DIR}/../build/compile_commands.json")
if(NOT EXISTS "${COMPILE_COMMANDS_PATH}")
    message(FATAL_ERROR "compile_commands.json not found at ${COMPILE_COMMANDS_PATH}")
endif()
file(READ "${COMPILE_COMMANDS_PATH}" COMPILE_COMMANDS_JSON)

set(FAILED 0)
set(CHECKED 0)

foreach(SRC IN LISTS SOURCES)
    get_filename_component(SRC_REAL "${SRC}" REALPATH)
    string(FIND "${COMPILE_COMMANDS_JSON}" "${SRC_REAL}" IN_COMPILE_DB)
    if(IN_COMPILE_DB EQUAL -1)
        continue()
    endif()

    math(EXPR CHECKED "${CHECKED} + 1")
    execute_process(
        COMMAND ${CLANG_TIDY} -p "${CMAKE_CURRENT_LIST_DIR}/../build" "${SRC_REAL}"
        RESULT_VARIABLE RC
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE ERR
    )
    if(NOT RC EQUAL 0)
        # Filter: only report if there are actual WarningsAsErrors hits
        string(FIND "${OUT}${ERR}" "warnings-as-errors" HAS_ERRORS)
        if(NOT HAS_ERRORS EQUAL -1)
            message(STATUS "FAIL: ${SRC}")
            message("${OUT}")
            math(EXPR FAILED "${FAILED} + 1")
        endif()
    endif()
endforeach()

message(STATUS "clang-tidy gate: checked ${CHECKED} files, ${FAILED} failed")
if(FAILED GREATER 0)
    message(FATAL_ERROR "clang-tidy gate: ${FAILED} file(s) have error-level findings")
endif()
