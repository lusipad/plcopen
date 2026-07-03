cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(REPLAY_DIR "${PLCOPEN_ROOT}/testdata/replay")
set(MANIFEST "${REPLAY_DIR}/manifest.json")

if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "Replay manifest missing: ${MANIFEST}")
endif()

file(READ "${MANIFEST}" manifest)
foreach(required IN ITEMS "schema" "fixtures" "format-smoke-single-axis" "format-smoke-two-axis" "format-smoke-stop")
    if(NOT manifest MATCHES "\"${required}\"")
        message(FATAL_ERROR "Replay manifest does not mention '${required}'")
    endif()
endforeach()

file(GLOB replay_files "${REPLAY_DIR}/*.jsonl")
list(LENGTH replay_files replay_count)

if(replay_count LESS 3)
    message(FATAL_ERROR "Expected at least 3 replay fixture files, got ${replay_count}")
endif()

set(line_count 0)

foreach(file IN LISTS replay_files)
    file(READ "${file}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")

    set(file_line_count 0)
    foreach(line IN LISTS lines)
        if(line STREQUAL "")
            continue()
        endif()

        math(EXPR file_line_count "${file_line_count} + 1")
        math(EXPR line_count "${line_count} + 1")

        foreach(field IN ITEMS tick axis position velocity acceleration source)
            if(NOT line MATCHES "\"${field}\"[ \t]*:")
                file(RELATIVE_PATH relative "${PLCOPEN_ROOT}" "${file}")
                message(FATAL_ERROR "${relative} line ${file_line_count} is missing '${field}'")
            endif()
        endforeach()
    endforeach()

    if(file_line_count EQUAL 0)
        file(RELATIVE_PATH relative "${PLCOPEN_ROOT}" "${file}")
        message(FATAL_ERROR "${relative} has no replay samples")
    endif()
endforeach()

message(STATUS "Replay fixtures verified (${replay_count} files, ${line_count} samples)")
