# Structural check for the FB-level half of the ST-L2c binding authority.
# Pin completeness is a separate gate: this verifier deliberately refuses to
# treat an FB row as evidence that any pin dispatcher exists.

cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CATALOG "${PLCOPEN_ROOT}/doc/compliance/st-binding-fb-catalog.yml")
if(NOT EXISTS "${CATALOG}")
    message(FATAL_ERROR "missing ${CATALOG}")
endif()

file(STRINGS "${CATALOG}" LINES)
file(READ "${CATALOG}" CATALOG_TEXT)
string(FIND "${CATALOG_TEXT}"
       "schema: plcopen-st-binding-fb-catalog-v1" SCHEMA_AT)
if(SCHEMA_AT EQUAL -1)
    message(FATAL_ERROR "unexpected ST binding FB catalog schema")
endif()

file(GLOB FB_HEADERS "${PLCOPEN_ROOT}/core/fb/*.h")
set(FB_TEXT "")
foreach(header IN LISTS FB_HEADERS)
    file(READ "${header}" text)
    string(APPEND FB_TEXT "\n${text}")
endforeach()

set(REQUIRED_SETS iec_basic plcopen_part1_part2 plcopen_part4 plcopen_part5)
set(EXPECTED_iec_basic 10)
set(EXPECTED_plcopen_part1_part2 45)
set(EXPECTED_plcopen_part4 68)
set(EXPECTED_plcopen_part5 11)
foreach(set_name IN LISTS REQUIRED_SETS)
    set(COUNT_${set_name} 0)
endforeach()
set(SEEN_NAMES)
set(FAILURES)
set(TOTAL 0)

foreach(line IN LISTS LINES)
    if(line MATCHES
       "^  - \\{set: ([a-z0-9_]+), name: ([A-Za-z0-9_]+), native: fb::([A-Za-z0-9_]+)\\}$")
        set(set_name "${CMAKE_MATCH_1}")
        set(fb_name "${CMAKE_MATCH_2}")
        set(native_name "${CMAKE_MATCH_3}")
        if(NOT set_name IN_LIST REQUIRED_SETS)
            list(APPEND FAILURES "${fb_name}: unknown set ${set_name}")
            continue()
        endif()
        string(TOLOWER "${fb_name}" lower_name)
        if(lower_name IN_LIST SEEN_NAMES)
            list(APPEND FAILURES "duplicate FB name ${fb_name}")
        else()
            list(APPEND SEEN_NAMES "${lower_name}")
        endif()
        math(EXPR COUNT_${set_name} "${COUNT_${set_name}} + 1")
        math(EXPR TOTAL "${TOTAL} + 1")
        string(FIND "${FB_TEXT}" "${native_name}" native_at)
        if(native_at EQUAL -1)
            list(APPEND FAILURES
                 "${fb_name}: native facade fb::${native_name} not found")
        endif()

        if(set_name STREQUAL "iec_basic")
            set(authority "${PLCOPEN_ROOT}/core/fb/basic.h")
            set(authority_token "${native_name}")
        elseif(set_name STREQUAL "plcopen_part1_part2")
            set(authority
                "${PLCOPEN_ROOT}/doc/compliance/plcopen-motion-part1-io.yml")
            set(authority_token "${fb_name}")
        elseif(set_name STREQUAL "plcopen_part4")
            set(authority
                "${PLCOPEN_ROOT}/doc/compliance/plcopen-part4-clause-audit.md")
            set(authority_token "${fb_name}")
        else()
            set(authority
                "${PLCOPEN_ROOT}/doc/compliance/plcopen-motion-part5-io.yml")
            set(authority_token "${fb_name}")
        endif()
        file(READ "${authority}" authority_text)
        string(FIND "${authority_text}" "${authority_token}" authority_at)
        if(authority_at EQUAL -1)
            list(APPEND FAILURES "${fb_name}: missing from ${authority}")
        endif()
    endif()
endforeach()

foreach(set_name IN LISTS REQUIRED_SETS)
    if(NOT COUNT_${set_name} EQUAL EXPECTED_${set_name})
        list(APPEND FAILURES
             "${set_name}: expected ${EXPECTED_${set_name}}, got ${COUNT_${set_name}}")
    endif()
endforeach()
if(NOT TOTAL EQUAL 134)
    list(APPEND FAILURES "expected 134 FB rows, got ${TOTAL}")
endif()

foreach(extension IN ITEMS
        MC_ReadCommandPosition MC_ReadCommandVelocity MC_EmergencyStop)
    string(FIND "${CATALOG_TEXT}" "name: ${extension}," extension_at)
    if(extension_at EQUAL -1)
        list(APPEND FAILURES
             "missing explicit extension boundary for ${extension}")
    endif()
endforeach()

if(FAILURES)
    string(REPLACE ";" "\n" text "${FAILURES}")
    message(FATAL_ERROR "ST binding FB catalog check failed:\n${text}")
endif()
message(STATUS
        "ST binding FB catalog verified: 10 basic + 45 Part1/2 + 68 Part4 + 11 Part5 = ${TOTAL}")

set(PIN_VERIFY_ARGUMENTS)
if(PLCOPEN_ALLOW_UNRESOLVED)
    list(APPEND PIN_VERIFY_ARGUMENTS -DPLCOPEN_ALLOW_UNRESOLVED=ON)
endif()
list(APPEND PIN_VERIFY_ARGUMENTS
     -P "${PLCOPEN_ROOT}/cmake/generate_st_binding_pin_catalog.cmake")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${PIN_VERIFY_ARGUMENTS}
    WORKING_DIRECTORY "${PLCOPEN_ROOT}"
    RESULT_VARIABLE pin_result
    OUTPUT_VARIABLE pin_output
    ERROR_VARIABLE pin_error)
if(NOT pin_result EQUAL 0)
    message(FATAL_ERROR
            "ST binding pin authority is incomplete or stale:\n${pin_output}${pin_error}")
endif()
message(STATUS "${pin_output}")
