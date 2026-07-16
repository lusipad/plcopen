cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SOURCE_FILE "${PLCOPEN_ROOT}/doc/compliance/plcopen-motion-part5-io.yml")
set(OUTPUT_FILE "${PLCOPEN_ROOT}/doc/compliance/generated/plcopen-motion-part5-io.md")

if(NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Part 5 I/O source missing: ${SOURCE_FILE}")
endif()

file(READ "${SOURCE_FILE}" source)
string(REPLACE "\r\n" "\n" source "${source}")
string(REPLACE "\n" ";" lines "${source}")

set(basic_rows "")
set(derived_rows "")
set(overview_rows "")
set(detail_rows "")
set(current_fb "")
set(current_number "")
set(current_support "")
set(current_summary "")
set(current_pin_rows "")
set(seen_fbs ";")
set(seen_pins ";")
set(basic_count 0)
set(derived_count 0)
set(fb_count 0)
set(b_count 0)
set(e_count 0)
set(pin_yes_count 0)
set(pin_no_count 0)

macro(flush_fb)
    if(NOT "${current_fb}" STREQUAL "")
        if("${current_support}" STREQUAL "" OR "${current_summary}" STREQUAL "" OR
           "${current_pin_rows}" STREQUAL "")
            message(FATAL_ERROR "Incomplete Part 5 FB '${current_fb}'")
        endif()
        string(APPEND overview_rows
               "| ${current_number} | `${current_fb}` | ${current_support} | ${current_summary} |\n")
        string(APPEND detail_rows
               "## ${current_number} ${current_fb}\n\n"
               "| Function block | Level | Direction | Pin | Supported | Evidence or boundary |\n"
               "|---|---|---|---|---|---|\n"
               "${current_pin_rows}\n")
        math(EXPR fb_count "${fb_count} + 1")
    endif()
    set(current_fb "")
    set(current_number "")
    set(current_support "")
    set(current_summary "")
    set(current_pin_rows "")
    set(seen_pins ";")
endmacro()

foreach(line IN LISTS lines)
    if(line MATCHES "^  - \\{name:[ \t]*([A-Z][A-Z0-9_]*),[ \t]*support:[ \t]*(Yes|No),[ \t]*mapping:[ \t]*'([^']+)'\\}[ \t]*$")
        set(type_name "${CMAKE_MATCH_1}")
        set(type_support "${CMAKE_MATCH_2}")
        set(type_mapping "${CMAKE_MATCH_3}")
        string(APPEND basic_rows
               "| `${type_name}` | ${type_support} | ${type_mapping} |\n")
        math(EXPR basic_count "${basic_count} + 1")
    elseif(line MATCHES "^  - \\{name:[ \t]*([A-Z][A-Z0-9_]*),[ \t]*support:[ \t]*(Yes|No),[ \t]*mapping:[ \t]*'([^']+)',[ \t]*boundary:[ \t]*'([^']+)'\\}[ \t]*$")
        set(type_name "${CMAKE_MATCH_1}")
        set(type_support "${CMAKE_MATCH_2}")
        set(type_mapping "${CMAKE_MATCH_3}")
        set(type_boundary "${CMAKE_MATCH_4}")
        string(APPEND derived_rows
               "| `${type_name}` | ${type_support} | ${type_mapping} | ${type_boundary} |\n")
        math(EXPR derived_count "${derived_count} + 1")
    elseif(line MATCHES "^  - number:[ \t]*'(5\\.[0-9]+)'[ \t]*$")
        flush_fb()
        set(current_number "${CMAKE_MATCH_1}")
    elseif(NOT "${current_number}" STREQUAL "" AND
           "${current_fb}" STREQUAL "" AND
           line MATCHES "^    name:[ \t]*'([^']+)'[ \t]*$")
        set(current_fb "${CMAKE_MATCH_1}")
        if(seen_fbs MATCHES ";${current_fb};")
            message(FATAL_ERROR "Duplicate Part 5 FB '${current_fb}'")
        endif()
        string(APPEND seen_fbs "${current_fb};")
    elseif(NOT "${current_fb}" STREQUAL "" AND
           line MATCHES "^    support:[ \t]*(Yes|No)[ \t]*$")
        set(current_support "${CMAKE_MATCH_1}")
    elseif(NOT "${current_fb}" STREQUAL "" AND
           line MATCHES "^    summary:[ \t]*'([^']+)'[ \t]*$")
        set(current_summary "${CMAKE_MATCH_1}")
    elseif(NOT "${current_fb}" STREQUAL "" AND
           line MATCHES "^      - \\{direction:[ \t]*(in_out|input|output),[ \t]*class:[ \t]*([BE]),[ \t]*name:[ \t]*([A-Za-z][A-Za-z0-9_]*),[ \t]*support:[ \t]*(Yes|No),[ \t]*evidence:[ \t]*'([^']+)'\\}[ \t]*$")
        set(direction "${CMAKE_MATCH_1}")
        set(level "${CMAKE_MATCH_2}")
        set(pin "${CMAKE_MATCH_3}")
        set(pin_support "${CMAKE_MATCH_4}")
        set(evidence "${CMAKE_MATCH_5}")
        string(TOLOWER "${pin}" pin_key)
        if(seen_pins MATCHES ";${pin_key};")
            message(FATAL_ERROR "Duplicate pin '${pin}' in ${current_fb}")
        endif()
        string(APPEND seen_pins "${pin_key};")
        if(level STREQUAL "B")
            math(EXPR b_count "${b_count} + 1")
            if(NOT pin_support STREQUAL "Yes")
                message(FATAL_ERROR "Basic pin '${current_fb}.${pin}' must be supported")
            endif()
        else()
            math(EXPR e_count "${e_count} + 1")
        endif()
        if(pin_support STREQUAL "Yes")
            math(EXPR pin_yes_count "${pin_yes_count} + 1")
        else()
            math(EXPR pin_no_count "${pin_no_count} + 1")
        endif()
        string(APPEND current_pin_rows
               "| ${current_fb} | ${level} | ${direction} | `${pin}` | ${pin_support} | ${evidence} |\n")
    elseif(line MATCHES "^[ \t]*$" OR line MATCHES "^#[ \t].*$" OR
           line MATCHES "^schema:[ \t]*plcopen-motion-part5-io-v1[ \t]*$" OR
           line MATCHES "^source:[ \t]*.+$" OR
           line MATCHES "^(basic_types|derived_types|rows):[ \t]*$" OR
           line MATCHES "^    pins:[ \t]*$")
    else()
        message(FATAL_ERROR "Unrecognized line in ${SOURCE_FILE}: '${line}'")
    endif()
endforeach()
flush_fb()

if(NOT basic_count EQUAL 5 OR NOT derived_count EQUAL 5 OR
   NOT fb_count EQUAL 11 OR NOT b_count EQUAL 45 OR NOT e_count EQUAL 102)
    message(FATAL_ERROR
            "Part 5 totals mismatch: basic=${basic_count}/5 derived=${derived_count}/5 FB=${fb_count}/11 B=${b_count}/45 E=${e_count}/102")
endif()

set(generated "# PLCopen Motion Control Part 5 I/O Declaration Matrix\n\n")
string(APPEND generated
       "> Generated from `plcopen-motion-part5-io.yml`; do not edit by hand.\n"
       "> Normative names directions and B/E levels follow Part 5 v2.0 clauses 5.2 to 5.14.\n"
       "> This is unsigned engineering evidence and is not PLCopen approval or Logo authorization.\n\n"
       "## Basic data types\n\n"
       "| Standard type | Supported | C++ mapping or substitute |\n"
       "|---|---|---|\n"
       "${basic_rows}\n"
       "## Derived data types\n\n"
       "| Standard type | Supported | C++ mapping | Boundary |\n"
       "|---|---|---|---|\n"
       "${derived_rows}\n"
       "## Function-block overview\n\n"
       "| Clause | Function block | Supported | Boundary |\n"
       "|---|---|---|---|\n"
       "${overview_rows}\n"
       "${detail_rows}")
string(REGEX REPLACE "\n+$" "\n" generated "${generated}")

if(PLCOPEN_UPDATE_GENERATED)
    get_filename_component(output_dir "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")
    file(WRITE "${OUTPUT_FILE}" "${generated}")
else()
    if(NOT EXISTS "${OUTPUT_FILE}")
        message(FATAL_ERROR "Generated Part 5 I/O matrix missing: ${OUTPUT_FILE}")
    endif()
    file(READ "${OUTPUT_FILE}" existing)
    string(REPLACE "\r\n" "\n" existing "${existing}")
    if(NOT "${existing}" STREQUAL "${generated}")
        message(FATAL_ERROR
                "Generated Part 5 I/O matrix is stale. Run: cmake -DPLCOPEN_UPDATE_GENERATED=ON -P cmake/generate_part5_io_matrix.cmake")
    endif()
endif()

message(STATUS
        "Part 5 I/O matrix synchronized (${fb_count} FBs, ${b_count} B, ${e_count} E, pins Yes=${pin_yes_count} No=${pin_no_count})")
