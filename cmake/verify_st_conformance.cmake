# L0 一致性矩阵锚点校验（st-l0-semantics 4.1，T39 起步件）。
# 逐条检查 doc/compliance/st-l0-conformance.yaml：
#   1. 测试源文件存在且包含锚点串；
#   2. 引用的 CTest 目标在 core/CMakeLists.txt 注册。
# 用法：cmake -P cmake/verify_st_conformance.cmake

cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(YAML_FILES
    "${PLCOPEN_ROOT}/doc/compliance/st-l0-conformance.yaml"
    "${PLCOPEN_ROOT}/doc/compliance/st-l1a-conformance.yaml")

set(LINES)
foreach(yaml IN LISTS YAML_FILES)
    if(NOT EXISTS "${yaml}")
        message(FATAL_ERROR "missing ${yaml}")
    endif()
    file(STRINGS "${yaml}" file_lines)
    list(APPEND LINES ${file_lines})
endforeach()

file(READ "${PLCOPEN_ROOT}/core/CMakeLists.txt" CMAKE_TEXT)

set(ENTRY_ID "")
set(ENTRY_FILE "")
set(ENTRY_ANCHOR "")
set(ENTRY_TEST "")
set(ENTRY_COUNT 0)
set(FAILURES)

function(check_entry id file anchor test cmake_text)
    if(id STREQUAL "" OR file STREQUAL "" OR anchor STREQUAL "" OR
       test STREQUAL "")
        list(APPEND FAILURES "${id}: incomplete entry")
        set(FAILURES "${FAILURES}" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(path "${root}/${file}")
    if(NOT EXISTS "${path}")
        list(APPEND FAILURES "${id}: missing file ${file}")
        set(FAILURES "${FAILURES}" PARENT_SCOPE)
        return()
    endif()
    file(READ "${path}" content)
    string(FIND "${content}" "${anchor}" found)
    if(found EQUAL -1)
        list(APPEND FAILURES "${id}: anchor '${anchor}' not found in ${file}")
    endif()
    string(FIND "${cmake_text}" "${test}" registered)
    if(registered EQUAL -1)
        list(APPEND FAILURES "${id}: test '${test}' not registered")
    endif()
    set(FAILURES "${FAILURES}" PARENT_SCOPE)
endfunction()

foreach(line IN LISTS LINES)
    if(line MATCHES "^  - id: (.+)$")
        if(NOT ENTRY_ID STREQUAL "")
            check_entry("${ENTRY_ID}" "${ENTRY_FILE}" "${ENTRY_ANCHOR}"
                        "${ENTRY_TEST}" "${CMAKE_TEXT}")
            math(EXPR ENTRY_COUNT "${ENTRY_COUNT} + 1")
        endif()
        set(ENTRY_ID "${CMAKE_MATCH_1}")
        set(ENTRY_FILE "")
        set(ENTRY_ANCHOR "")
        set(ENTRY_TEST "")
    elseif(line MATCHES "^    file: (.+)$")
        set(ENTRY_FILE "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^    anchor: (.+)$")
        set(ENTRY_ANCHOR "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^    test: (.+)$")
        set(ENTRY_TEST "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT ENTRY_ID STREQUAL "")
    check_entry("${ENTRY_ID}" "${ENTRY_FILE}" "${ENTRY_ANCHOR}"
                "${ENTRY_TEST}" "${CMAKE_TEXT}")
    math(EXPR ENTRY_COUNT "${ENTRY_COUNT} + 1")
endif()

if(FAILURES)
    string(REPLACE ";" "\n" text "${FAILURES}")
    message(FATAL_ERROR "st-l0 conformance check failed:\n${text}")
endif()

message(STATUS "st-l0 conformance verified (${ENTRY_COUNT} entries)")
