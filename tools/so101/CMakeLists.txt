if(BUILD_TESTING AND PLCOPEN_BUILD_TESTS)
  add_executable(plcopen_so101_read_only_probe_tests
                 read_only_probe_tests.cpp)
  target_link_libraries(plcopen_so101_read_only_probe_tests
                        PRIVATE plcopen::core)
  target_include_directories(plcopen_so101_read_only_probe_tests
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME plcopen_so101_read_only_probe_tests
           COMMAND plcopen_so101_read_only_probe_tests)

  add_executable(plcopen_so101_comparison_tests
                 comparison_tests.cpp
                 comparison.cpp)
  target_link_libraries(plcopen_so101_comparison_tests
                        PRIVATE plcopen::core)
  target_include_directories(plcopen_so101_comparison_tests
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME plcopen_so101_comparison_tests
           COMMAND plcopen_so101_comparison_tests)
endif()

if(PLCOPEN_BUILD_SO101_TOOLS)
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR
            "PLCOPEN_BUILD_SO101_TOOLS currently requires Linux")
  endif()

  add_executable(plcopen_so101_discover
                 discover.cpp
                 posix_serial.cpp)
  target_link_libraries(plcopen_so101_discover PRIVATE plcopen::core)
  target_include_directories(plcopen_so101_discover
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(plcopen_so101_compare
                 compare.cpp
                 comparison.cpp)
  target_link_libraries(plcopen_so101_compare PRIVATE plcopen::core)
  target_include_directories(plcopen_so101_compare
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
