# L1a 转换矩阵三方校验（st-l1a-semantics 7.1）：conv.h 实现的
# --dump-matrix 输出必须与 doc/compliance/st-l1a-conversions.yaml 逐行
# 一致（集合相等 + 计数相等）。作为 CTest 运行：
#   cmake -DEXE=<conversion_tests 可执行体> -P cmake/verify_st_l1a_conversions.cmake

cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(YAML "${PLCOPEN_ROOT}/doc/compliance/st-l1a-conversions.yaml")

if(NOT DEFINED EXE)
    message(FATAL_ERROR "pass -DEXE=<path to plcopen_core_st_l1a_conversion_tests>")
endif()
if(NOT EXISTS "${YAML}")
    message(FATAL_ERROR "missing ${YAML}")
endif()

# EMULATOR carries CMAKE_CROSSCOMPILING_EMULATOR (e.g. qemu-user for the
# ARM64 cross job); empty on native builds.
execute_process(
    COMMAND ${EMULATOR} "${EXE}" --dump-matrix
    OUTPUT_VARIABLE DUMP
    RESULT_VARIABLE RC)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "--dump-matrix failed (${RC})")
endif()

string(REPLACE "\r\n" "\n" DUMP "${DUMP}")
string(REGEX REPLACE "\n$" "" DUMP "${DUMP}")
string(REPLACE "\n" ";" DUMP_LINES "${DUMP}")

file(STRINGS "${YAML}" YAML_RAW)
set(YAML_LINES)
foreach(line IN LISTS YAML_RAW)
    if(line MATCHES "^- (.+)$")
        list(APPEND YAML_LINES "${CMAKE_MATCH_1}")
    endif()
endforeach()

list(LENGTH DUMP_LINES DUMP_COUNT)
list(LENGTH YAML_LINES YAML_COUNT)
if(NOT DUMP_COUNT EQUAL YAML_COUNT)
    message(FATAL_ERROR
        "cell count drifted: implementation ${DUMP_COUNT} vs yaml ${YAML_COUNT}")
endif()

set(FAILURES)
foreach(line IN LISTS DUMP_LINES)
    list(FIND YAML_LINES "${line}" found)
    if(found EQUAL -1)
        list(APPEND FAILURES "implementation cell not in yaml: ${line}")
    endif()
endforeach()
foreach(line IN LISTS YAML_LINES)
    list(FIND DUMP_LINES "${line}" found)
    if(found EQUAL -1)
        list(APPEND FAILURES "yaml cell not in implementation: ${line}")
    endif()
endforeach()

if(FAILURES)
    string(REPLACE ";" "\n" text "${FAILURES}")
    message(FATAL_ERROR "st-l1a conversion matrix drifted:\n${text}")
endif()

message(STATUS "st-l1a conversion matrix verified (${DUMP_COUNT} cells)")
