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

    add_executable(plcopen_core_otg_optimality_oracle test/otg_optimality_oracle.cpp)
    target_link_libraries(plcopen_core_otg_optimality_oracle PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_otg_optimality_oracle)
    add_test(NAME plcopen_core_otg_optimality_oracle
             COMMAND plcopen_core_otg_optimality_oracle --iterations 500)

    add_executable(plcopen_core_otg_time_optimal_tests test/otg_time_optimal_tests.cpp)
    target_link_libraries(plcopen_core_otg_time_optimal_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_otg_time_optimal_tests)
    add_test(NAME plcopen_core_otg_time_optimal_tests
             COMMAND plcopen_core_otg_time_optimal_tests --iterations 5000)

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

    add_executable(plcopen_core_r3_io_tests test/r3_io_tests.cpp)
    target_link_libraries(plcopen_core_r3_io_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_r3_io_tests)
    add_test(NAME plcopen_core_r3_io_tests COMMAND plcopen_core_r3_io_tests)

    add_executable(plcopen_core_a3_circular_tests test/a3_circular_tests.cpp)
    target_link_libraries(plcopen_core_a3_circular_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_a3_circular_tests)
    add_test(NAME plcopen_core_a3_circular_tests COMMAND plcopen_core_a3_circular_tests)

    add_executable(plcopen_core_a4_blending_tests test/a4_blending_tests.cpp)
    target_link_libraries(plcopen_core_a4_blending_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_a4_blending_tests)
    add_test(NAME plcopen_core_a4_blending_tests COMMAND plcopen_core_a4_blending_tests)

    add_executable(plcopen_core_a5_lookahead_tests test/a5_lookahead_tests.cpp)
    target_link_libraries(plcopen_core_a5_lookahead_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_a5_lookahead_tests)
    add_test(NAME plcopen_core_a5_lookahead_tests COMMAND plcopen_core_a5_lookahead_tests)

    add_executable(plcopen_core_y7_group_takeover_tests test/y7_group_takeover_tests.cpp)
    target_link_libraries(plcopen_core_y7_group_takeover_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_y7_group_takeover_tests)
    add_test(NAME plcopen_core_y7_group_takeover_tests COMMAND plcopen_core_y7_group_takeover_tests)

    add_executable(plcopen_core_stream_tests test/stream_filter_tests.cpp)
    target_link_libraries(plcopen_core_stream_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_stream_tests)
    add_test(NAME plcopen_core_stream_tests COMMAND plcopen_core_stream_tests)

    add_executable(plcopen_core_wrist6r_tests test/wrist6r_tests.cpp)
    target_link_libraries(plcopen_core_wrist6r_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_wrist6r_tests)
    add_test(NAME plcopen_core_wrist6r_tests COMMAND plcopen_core_wrist6r_tests)

    add_executable(plcopen_core_pose_tests test/pose_tests.cpp)
    target_link_libraries(plcopen_core_pose_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_pose_tests)
    add_test(NAME plcopen_core_pose_tests COMMAND plcopen_core_pose_tests)

    add_executable(plcopen_core_readback_tests test/readback_tests.cpp)
    target_link_libraries(plcopen_core_readback_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_readback_tests)
    add_test(NAME plcopen_core_readback_tests COMMAND plcopen_core_readback_tests)

    add_executable(plcopen_core_cartesian_tests test/cartesian_tests.cpp)
    target_link_libraries(plcopen_core_cartesian_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_cartesian_tests)
    add_test(NAME plcopen_core_cartesian_tests COMMAND plcopen_core_cartesian_tests)

    add_executable(plcopen_core_adapters_tests test/adapters_tests.cpp)
    target_link_libraries(plcopen_core_adapters_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_adapters_tests)
    add_test(NAME plcopen_core_adapters_tests COMMAND plcopen_core_adapters_tests)

    add_executable(plcopen_core_cam_tests test/cam_tests.cpp)
    target_link_libraries(plcopen_core_cam_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_cam_tests)
    add_test(NAME plcopen_core_cam_tests COMMAND plcopen_core_cam_tests)

    add_executable(plcopen_core_kinematics_tests test/kinematics_tests.cpp)
    target_link_libraries(plcopen_core_kinematics_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_kinematics_tests)
    add_test(NAME plcopen_core_kinematics_tests COMMAND plcopen_core_kinematics_tests)

    add_executable(plcopen_core_coordinate_tests test/coordinate_tests.cpp)
    target_link_libraries(plcopen_core_coordinate_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_coordinate_tests)
    add_test(NAME plcopen_core_coordinate_tests COMMAND plcopen_core_coordinate_tests)

    add_executable(plcopen_core_stream_session_tests test/stream_session_tests.cpp)
    target_link_libraries(plcopen_core_stream_session_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_stream_session_tests)
    add_test(NAME plcopen_core_stream_session_tests COMMAND plcopen_core_stream_session_tests)

    add_executable(plcopen_core_a2_alloc_guard test/a2_alloc_guard_tests.cpp)
    target_link_libraries(plcopen_core_a2_alloc_guard PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_a2_alloc_guard)
    add_test(NAME plcopen_core_a2_alloc_guard COMMAND plcopen_core_a2_alloc_guard)

    add_executable(plcopen_core_part5_homing_tests test/part5_homing_tests.cpp)
    target_link_libraries(plcopen_core_part5_homing_tests PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_part5_homing_tests)
    add_test(NAME plcopen_core_part5_homing_tests COMMAND plcopen_core_part5_homing_tests)

    add_executable(plcopen_core_replay_regression test/replay_regression.cpp)
    target_link_libraries(plcopen_core_replay_regression PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_replay_regression)
    add_test(NAME plcopen_core_replay_regression
             COMMAND plcopen_core_replay_regression ${CMAKE_CURRENT_SOURCE_DIR}/../testdata/replay)

    add_executable(plcopen_core_jitter_harness bench/jitter_harness.cpp)
    target_link_libraries(plcopen_core_jitter_harness PRIVATE plcopen::core)
    plcopen_core_rt_compile_options(plcopen_core_jitter_harness)
    add_test(NAME plcopen_core_jitter_harness_smoke
             COMMAND plcopen_core_jitter_harness --seconds 2 --no-rt)
    set_tests_properties(plcopen_core_jitter_harness_smoke PROPERTIES LABELS benchmark)

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

    add_executable(plcopen_core_rt_executor_demo demo/rt_executor_demo.cpp)
    target_link_libraries(plcopen_core_rt_executor_demo PRIVATE plcopen::core)
    if(UNIX AND NOT APPLE)
      target_link_libraries(plcopen_core_rt_executor_demo PRIVATE pthread)
    endif()

    add_executable(plcopen_core_group_path_motion demo/group_path_motion.cpp)
    target_link_libraries(plcopen_core_group_path_motion PRIVATE plcopen::plcopen)

    if(BUILD_TESTING)
        add_test(NAME plcopen_core_basic_fb_cycle_smoke COMMAND plcopen_core_basic_fb_cycle)
        add_test(NAME plcopen_core_group_linear_move_smoke COMMAND plcopen_core_group_linear_move)
        add_test(NAME plcopen_core_trajectory_stream_smoke COMMAND plcopen_core_trajectory_stream)
        add_test(NAME plcopen_core_group_path_motion_smoke COMMAND plcopen_core_group_path_motion)
        add_test(NAME plcopen_core_rt_executor_smoke
                 COMMAND plcopen_core_rt_executor_demo --cycles 2000 --trace
                         ${CMAKE_CURRENT_BINARY_DIR}/rt_executor_trace_smoke.bin)
    endif()
endif()

# DoD §5.3 evidence tool: legacy-vs-core cycle cost on an equivalent workload.
# Only meaningful (and only built) when the frozen v0.x line is enabled.
if(PLCOPEN_BUILD_LEGACY)
    add_executable(plcopen_core_legacy_compare bench/legacy_compare.cpp)
    target_link_libraries(plcopen_core_legacy_compare PRIVATE plcopen::core plcopen)
endif()

install(TARGETS plcopen_core
    EXPORT plcopenTargets)

install(DIRECTORY adapters axis exec fb geom kin otg plan rt stream
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/plcopen
    FILES_MATCHING PATTERN "*.h")
