cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(RT_DIRS
    core/adapters
    core/rt
    core/otg
    core/geom
    core/dyn
    core/exec
    core/kin
    core/stream)

set(CODE_GLOBS
    "*.h"
    "*.hh"
    "*.hpp"
    "*.c"
    "*.cc"
    "*.cpp"
    "*.cxx"
    "*.ixx")

set(RT_FILES)

foreach(dir IN LISTS RT_DIRS)
    if(EXISTS "${PLCOPEN_ROOT}/${dir}")
        foreach(glob IN LISTS CODE_GLOBS)
            file(GLOB_RECURSE matches "${PLCOPEN_ROOT}/${dir}/${glob}")
            list(APPEND RT_FILES ${matches})
        endforeach()
    endif()
endforeach()

foreach(glob IN LISTS CODE_GLOBS)
    file(GLOB_RECURSE core_matches "${PLCOPEN_ROOT}/core/${glob}")
    foreach(file IN LISTS core_matches)
        file(READ "${file}" content)
        if(content MATCHES "RT-SAFE")
            list(APPEND RT_FILES "${file}")
        endif()
    endforeach()
endforeach()

if(RT_FILES)
    list(REMOVE_DUPLICATES RT_FILES)
endif()

set(VIOLATIONS)

set(HEAP_REGEX
    "(^|[^A-Za-z0-9_:])(new|delete)([^A-Za-z0-9_]|$)|std::(make_unique|make_shared|unique_ptr|shared_ptr|allocator|vector|deque|list|map|unordered_map|string)")

function(normalize_heap_scan_content content output)
    # Placement construction starts an object lifetime in caller-owned memory;
    # it does not allocate. Keep the exemption deliberately narrow so ordinary
    # new expressions and allocation helpers remain violations.
    string(REGEX REPLACE "#include[ \t]*<new>[^\n]*" ""
        normalized "${content}")
    string(REGEX REPLACE
        "::new[ \t\r\n]*\\([ \t\r\n]*static_cast<void[ \t]*\\*>\\([^)]*\\)[ \t\r\n]*\\)[ \t\r\n]*[A-Za-z_][A-Za-z0-9_:<>]*[ \t\r\n]*\\{[ \t\r\n]*\\}"
        "placement_construct" normalized "${normalized}")
    # A deleted special member (`= delete;`) is a compile-time declaration,
    # not a heap deallocation expression.
    string(REGEX REPLACE "=[ \t\r\n]*delete[ \t\r\n]*;" "= default;"
        normalized "${normalized}")
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

# Scanner fixtures: the precise placement-new form is allowed, while normal
# allocation, deletion, and owning heap wrappers must continue to be caught.
normalize_heap_scan_content(
    "#include <new>\n::new(static_cast<void *>(cursor)) Storage{};"
    placement_fixture)
if(placement_fixture MATCHES "${HEAP_REGEX}")
    message(FATAL_ERROR "RT-safety scanner rejected placement-new fixture")
endif()
normalize_heap_scan_content("auto *raw = new Item" new_fixture)
if(NOT new_fixture MATCHES "${HEAP_REGEX}")
    message(FATAL_ERROR "RT-safety scanner missed ordinary new fixture")
endif()
normalize_heap_scan_content("delete raw" delete_fixture)
if(NOT delete_fixture MATCHES "${HEAP_REGEX}")
    message(FATAL_ERROR "RT-safety scanner missed ordinary delete fixture")
endif()
normalize_heap_scan_content("std::unique_ptr<Item> owned" unique_ptr_fixture)
if(NOT unique_ptr_fixture MATCHES "${HEAP_REGEX}")
    message(FATAL_ERROR "RT-safety scanner missed unique_ptr fixture")
endif()

function(check_rule file content name regex)
    string(REGEX MATCH "${regex}" match "${content}")
    if(match)
        file(RELATIVE_PATH relative "${PLCOPEN_ROOT}" "${file}")
        string(REPLACE "\n" " " snippet "${match}")
        list(APPEND VIOLATIONS "${relative}: ${name}: ${snippet}")
        set(VIOLATIONS "${VIOLATIONS}" PARENT_SCOPE)
    endif()
endfunction()

foreach(file IN LISTS RT_FILES)
    file(READ "${file}" content)

    normalize_heap_scan_content("${content}" heap_content)
    check_rule("${file}" "${heap_content}" "heap allocation" "${HEAP_REGEX}")
    check_rule("${file}" "${content}" "blocking synchronization"
        "std::(mutex|recursive_mutex|timed_mutex|shared_mutex|lock_guard|unique_lock|scoped_lock|condition_variable)")
    check_rule("${file}" "${content}" "exceptions"
        "(^|[^A-Za-z0-9_:])(throw|try|catch)([^A-Za-z0-9_]|$)|std::exception")
    check_rule("${file}" "${content}" "threads or OS I/O"
        "#include[ \t]*<thread>|#include[ \t]*<filesystem>|#include[ \t]*<fstream>|#include[ \t]*<iostream>|std::(thread|jthread|this_thread|async|future|promise)|(^|[^A-Za-z0-9_])system[ \t]*\\(")
    check_rule("${file}" "${content}" "wall-clock time"
        "#include[ \t]*<chrono>|std::chrono")
    check_rule("${file}" "${content}" "floating time state"
        "(double|float)[ \t\r\n]+[A-Za-z0-9_, \t*]*(dt|Dt|time|Time|elapsed|Elapsed)")
endforeach()

list(LENGTH VIOLATIONS violation_count)
list(LENGTH RT_FILES scanned_count)

if(violation_count GREATER 0)
    string(REPLACE ";" "\n" violation_text "${VIOLATIONS}")
    message(FATAL_ERROR "RT-safety scan failed:\n${violation_text}")
endif()

message(STATUS "RT-safety scan passed (${scanned_count} files)")
