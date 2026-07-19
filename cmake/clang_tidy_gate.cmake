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
find_program(RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy-18)
if(NOT RUN_CLANG_TIDY)
    message(FATAL_ERROR "run-clang-tidy not found")
endif()

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_LIST_DIR}/../core/test/*.cpp"
                          "${CMAKE_CURRENT_LIST_DIR}/../core/bench/*.cpp"
                          "${CMAKE_CURRENT_LIST_DIR}/../core/demo/*.cpp")

if(NOT DEFINED CLANG_TIDY_BUILD_DIR)
    set(CLANG_TIDY_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/../build")
endif()

set(COMPILE_COMMANDS_PATH "${CLANG_TIDY_BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${COMPILE_COMMANDS_PATH}")
    message(FATAL_ERROR "compile_commands.json not found at ${COMPILE_COMMANDS_PATH}")
endif()
file(READ "${COMPILE_COMMANDS_PATH}" COMPILE_COMMANDS_JSON)

set(CHECKED 0)

foreach(SRC IN LISTS SOURCES)
    get_filename_component(SRC_REAL "${SRC}" REALPATH)
    string(FIND "${COMPILE_COMMANDS_JSON}" "${SRC_REAL}" IN_COMPILE_DB)
    if(IN_COMPILE_DB EQUAL -1)
        continue()
    endif()

    math(EXPR CHECKED "${CHECKED} + 1")
endforeach()

if(CHECKED EQUAL 0)
    message(FATAL_ERROR "clang-tidy gate: compile database matched zero files")
endif()

if(NOT DEFINED CLANG_TIDY_JOBS)
    set(CLANG_TIDY_JOBS 8)
endif()

set(CLANG_TIDY_LOG "${CLANG_TIDY_BUILD_DIR}/clang-tidy-gate.log")
file(REMOVE "${CLANG_TIDY_LOG}")
execute_process(
    COMMAND ${RUN_CLANG_TIDY}
            -quiet
            -j ${CLANG_TIDY_JOBS}
            -clang-tidy-binary ${CLANG_TIDY}
            -p "${CLANG_TIDY_BUILD_DIR}"
            ".*/core/(test|bench|demo)/.*[.]cpp$"
    RESULT_VARIABLE RC
    OUTPUT_FILE "${CLANG_TIDY_LOG}"
    ERROR_FILE "${CLANG_TIDY_LOG}"
)

message(STATUS "clang-tidy gate: checked ${CHECKED} files with ${CLANG_TIDY_JOBS} workers")
if(NOT RC EQUAL 0)
    file(READ "${CLANG_TIDY_LOG}" OUTPUT)
    string(REGEX MATCHALL "[^\r\n]*warnings-as-errors[^\r\n]*" ERROR_LINES
           "${OUTPUT}")
    if(ERROR_LINES)
        foreach(ERROR_LINE IN LISTS ERROR_LINES)
            message("${ERROR_LINE}")
        endforeach()
    else()
        message("${OUTPUT}")
    endif()
    file(REMOVE "${CLANG_TIDY_LOG}")
    message(FATAL_ERROR "clang-tidy gate: parallel analysis failed")
endif()
file(REMOVE "${CLANG_TIDY_LOG}")
