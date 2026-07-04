add_library(plcopen_core INTERFACE)
add_library(plcopen::core ALIAS plcopen_core)

target_include_directories(
    plcopen_core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(plcopen_core INTERFACE cxx_std_17)

function(plcopen_core_rt_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GR- /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
    endif()
endfunction()

set(PLCOPEN_DEFAULT_BUILD_CORE_TESTS ${PLCOPEN_BUILD_TESTS})
option(PLCOPEN_BUILD_CORE_TESTS "Build plcopen rewrite core tests" ${PLCOPEN_DEFAULT_BUILD_CORE_TESTS})
unset(PLCOPEN_DEFAULT_BUILD_CORE_TESTS)

if(BUILD_TESTING AND PLCOPEN_BUILD_CORE_TESTS)
    add_executable(plcopen_core_rt_tests test/core_rt_tests.cpp)
    target_link_libraries(plcopen_core_rt_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_rt_tests)
    add_test(NAME plcopen_core_rt_tests COMMAND plcopen_core_rt_tests)

    add_executable(plcopen_core_otg_fuzz test/otg_fuzz.cpp)
    target_link_libraries(plcopen_core_otg_fuzz PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_otg_fuzz)
    add_test(NAME plcopen_core_otg_fuzz_smoke COMMAND plcopen_core_otg_fuzz --iterations 2000)

    add_executable(plcopen_core_otg_oracle test/otg_oracle.cpp)
    target_link_libraries(plcopen_core_otg_oracle PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_otg_oracle)
    add_test(NAME plcopen_core_otg_oracle COMMAND plcopen_core_otg_oracle)

    add_executable(plcopen_core_r2_tests test/r2_motion_tests.cpp)
    target_link_libraries(plcopen_core_r2_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r2_tests)
    add_test(NAME plcopen_core_r2_tests COMMAND plcopen_core_r2_tests)

    add_executable(plcopen_core_r3_tests test/r3_semantics_tests.cpp)
    target_link_libraries(plcopen_core_r3_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_tests)
    add_test(NAME plcopen_core_r3_tests COMMAND plcopen_core_r3_tests)

    add_executable(plcopen_core_benchmark bench/core_benchmark.cpp)
    target_link_libraries(plcopen_core_benchmark PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_benchmark)
    add_test(NAME plcopen_core_benchmark COMMAND plcopen_core_benchmark)
    set_tests_properties(plcopen_core_benchmark PROPERTIES LABELS benchmark)
endif()
