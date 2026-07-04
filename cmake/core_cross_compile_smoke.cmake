cmake_minimum_required(VERSION 3.21)

get_filename_component(PLCOPEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BUILD_DIR "${PLCOPEN_ROOT}/build/core-cross-smoke")
file(MAKE_DIRECTORY "${BUILD_DIR}")

set(SMOKE_SOURCE "${BUILD_DIR}/core_cross_smoke.cpp")
file(WRITE "${SMOKE_SOURCE}" [=[
#include "exec/sampler.h"
#include "geom/geometry.h"
#include "rt/spsc_queue.h"

int main()
{
    using namespace plcopen::core;
    rt::SpscQueue<int, 2> queue;
    queue.push(1);
    int value = 0;
    queue.pop(value);
    const auto line = geom::make_line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    if(!line.ok()) {
        return 1;
    }
    exec::CommittedPath<1> path;
    path.push(geom::as_path_segment(line.value()));
    const auto profile = otg::plan({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 1.0, 1.0, 1.0});
    if(!profile.ok()) {
        return 1;
    }
    const geom::Vec3 finish = exec::sample_profiled_path(
        path, profile.value(), rt::CycleTick::from_cycles(profile.value().duration_cycles()));
    return value == 1 && finish.x > 0.999 ? 0 : 1;
}
]=])

function(run_compile label compiler)
    if(NOT compiler)
        message(STATUS "${label}: compiler not found; skipping")
        return()
    endif()

    set(object "${BUILD_DIR}/${label}.o")
    execute_process(
        COMMAND "${compiler}" -std=c++17 -I "${PLCOPEN_ROOT}/core" -fno-exceptions -fno-rtti
                -c "${SMOKE_SOURCE}" -o "${object}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_out
        ERROR_VARIABLE compile_err)

    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR "${label}: compile failed\n${compile_out}\n${compile_err}")
    endif()

    find_program(SIZE_TOOL NAMES ${label}-size size)
    if(SIZE_TOOL)
        execute_process(COMMAND "${SIZE_TOOL}" "${object}" OUTPUT_VARIABLE size_out ERROR_QUIET)
        string(STRIP "${size_out}" size_out)
        message(STATUS "${label}: compile passed; footprint\n${size_out}")
    else()
        message(STATUS "${label}: compile passed; size tool not found")
    endif()
endfunction()

find_program(AARCH64_CXX NAMES aarch64-linux-gnu-g++ aarch64-none-linux-gnu-g++)
find_program(ARM_NONE_EABI_CXX NAMES arm-none-eabi-g++)

run_compile("aarch64-linux-gnu" "${AARCH64_CXX}")
run_compile("arm-none-eabi" "${ARM_NONE_EABI_CXX}")
