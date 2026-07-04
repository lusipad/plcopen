add_library(plcopen_core INTERFACE)
add_library(plcopen::core ALIAS plcopen_core)
if(NOT TARGET plcopen::plcopen)
    add_library(plcopen::plcopen ALIAS plcopen_core)
endif()

target_include_directories(
    plcopen_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/plcopen>)
target_compile_features(plcopen_core INTERFACE cxx_std_17)
set_target_properties(plcopen_core PROPERTIES EXPORT_NAME plcopen)

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

    add_executable(plcopen_core_r3_sync_tests test/r3_sync_tests.cpp)
    target_link_libraries(plcopen_core_r3_sync_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_sync_tests)
    add_test(NAME plcopen_core_r3_sync_tests COMMAND plcopen_core_r3_sync_tests)

    add_executable(plcopen_core_r3_motion_family_tests test/r3_motion_family_tests.cpp)
    target_link_libraries(plcopen_core_r3_motion_family_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_motion_family_tests)
    add_test(NAME plcopen_core_r3_motion_family_tests COMMAND plcopen_core_r3_motion_family_tests)

    add_executable(plcopen_core_r3_parameter_tests test/r3_parameter_tests.cpp)
    target_link_libraries(plcopen_core_r3_parameter_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_parameter_tests)
    add_test(NAME plcopen_core_r3_parameter_tests COMMAND plcopen_core_r3_parameter_tests)

    add_executable(plcopen_core_r3_profile_tests test/r3_profile_tests.cpp)
    target_link_libraries(plcopen_core_r3_profile_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_profile_tests)
    add_test(NAME plcopen_core_r3_profile_tests COMMAND plcopen_core_r3_profile_tests)

    add_executable(plcopen_core_r3_probe_tests test/r3_probe_tests.cpp)
    target_link_libraries(plcopen_core_r3_probe_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_probe_tests)
    add_test(NAME plcopen_core_r3_probe_tests COMMAND plcopen_core_r3_probe_tests)

    add_executable(plcopen_core_r3_group_fb_tests test/r3_group_fb_tests.cpp)
    target_link_libraries(plcopen_core_r3_group_fb_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_group_fb_tests)
    add_test(NAME plcopen_core_r3_group_fb_tests COMMAND plcopen_core_r3_group_fb_tests)

    add_executable(plcopen_core_benchmark bench/core_benchmark.cpp)
    target_link_libraries(plcopen_core_benchmark PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_benchmark)
    add_test(NAME plcopen_core_benchmark COMMAND plcopen_core_benchmark)
    set_tests_properties(plcopen_core_benchmark PROPERTIES LABELS benchmark)
endif()

if(PLCOPEN_BUILD_DEMOS)
    add_executable(plcopen_core_basic_fb_cycle demo/basic_fb_cycle.cpp)
    target_link_libraries(plcopen_core_basic_fb_cycle PRIVATE plcopen::plcopen)

    add_executable(plcopen_core_group_linear_move demo/group_linear_move.cpp)
    target_link_libraries(plcopen_core_group_linear_move PRIVATE plcopen::plcopen)

    add_executable(plcopen_core_trajectory_stream demo/trajectory_stream.cpp)
    target_link_libraries(plcopen_core_trajectory_stream PRIVATE plcopen::plcopen)

    if(BUILD_TESTING)
        add_test(NAME plcopen_core_basic_fb_cycle_smoke COMMAND plcopen_core_basic_fb_cycle)
        add_test(NAME plcopen_core_group_linear_move_smoke COMMAND plcopen_core_group_linear_move)
        add_test(NAME plcopen_core_trajectory_stream_smoke COMMAND plcopen_core_trajectory_stream)
    endif()
endif()

install(TARGETS plcopen_core
    EXPORT plcopenTargets)

install(DIRECTORY axis exec fb geom otg plan rt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plcopen
    FILES_MATCHING PATTERN "*.h")
