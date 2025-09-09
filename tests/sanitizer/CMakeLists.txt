# Sanitizer测试构建配置
cmake_minimum_required(VERSION 3.15)

# 检查编译器是否支持Sanitizer
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(WARNING "Sanitizer tests require Clang or GCC compiler")
    return()
endif()

# 检查平台支持
if(WIN32 AND NOT MINGW)
    message(WARNING "Sanitizer tests are not fully supported on Windows MSVC")
    return()
endif()

# Sanitizer编译选项
set(SANITIZER_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

# AddressSanitizer配置
if(ENABLE_ASAN OR CMAKE_BUILD_TYPE STREQUAL "Sanitizer")
    list(APPEND SANITIZER_FLAGS "-fsanitize=address")
    list(APPEND SANITIZER_FLAGS "-fno-omit-frame-pointer")
    list(APPEND SANITIZER_FLAGS "-g")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=address")
    message(STATUS "Enabled AddressSanitizer")
endif()

# UndefinedBehaviorSanitizer配置
if(ENABLE_UBSAN OR CMAKE_BUILD_TYPE STREQUAL "Sanitizer")
    list(APPEND SANITIZER_FLAGS "-fsanitize=undefined")
    list(APPEND SANITIZER_FLAGS "-fsanitize=nullability")
    list(APPEND SANITIZER_FLAGS "-fno-sanitize-recover=all")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=undefined")
    message(STATUS "Enabled UndefinedBehaviorSanitizer")
endif()

# ThreadSanitizer配置（与其他sanitizer互斥）
if(ENABLE_TSAN AND NOT (ENABLE_ASAN OR ENABLE_MSAN))
    list(APPEND SANITIZER_FLAGS "-fsanitize=thread")
    list(APPEND SANITIZER_FLAGS "-fno-omit-frame-pointer")
    list(APPEND SANITIZER_FLAGS "-g")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=thread")
    message(STATUS "Enabled ThreadSanitizer")
endif()

# MemorySanitizer配置（仅Clang支持，与其他sanitizer互斥）
if(ENABLE_MSAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT (ENABLE_ASAN OR ENABLE_TSAN))
    list(APPEND SANITIZER_FLAGS "-fsanitize=memory")
    list(APPEND SANITIZER_FLAGS "-fsanitize-memory-track-origins")
    list(APPEND SANITIZER_FLAGS "-fno-omit-frame-pointer")
    list(APPEND SANITIZER_FLAGS "-g")
    list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=memory")
    message(STATUS "Enabled MemorySanitizer")
endif()

# 添加调试信息和优化设置
list(APPEND SANITIZER_FLAGS "-O1")  # 轻度优化保持调试性能
list(APPEND SANITIZER_FLAGS "-g3")  # 详细调试信息
list(APPEND SANITIZER_FLAGS "-fno-optimize-sibling-calls")  # 保持调用栈清晰

# 检查是否有GTest
find_package(GTest QUIET)
if(NOT GTest_FOUND AND NOT GTEST_FOUND AND NOT TARGET gtest)
    message(WARNING "GTest not found, skipping Sanitizer tests")
    return()
endif()

# ST VM 深度递归测试
add_executable(test_st_vm_deep_recursion 
    test_st_vm_deep_recursion.cpp
    # 添加必要的源文件
    ${CMAKE_SOURCE_DIR}/src/error/error_codes.cpp
    ${CMAKE_SOURCE_DIR}/src/error/standard_error_category.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/structured_logger.cpp
)

# 设置包含目录
target_include_directories(test_st_vm_deep_recursion PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# 应用Sanitizer标志
if(SANITIZER_FLAGS)
    target_compile_options(test_st_vm_deep_recursion PRIVATE ${SANITIZER_FLAGS})
    target_link_options(test_st_vm_deep_recursion PRIVATE ${SANITIZER_LINK_FLAGS})
    
    # 设置运行时环境变量
    set_target_properties(test_st_vm_deep_recursion PROPERTIES
        # AddressSanitizer选项
        ENVIRONMENT "ASAN_OPTIONS=check_initialization_order=1:strict_init_order=1:detect_odr_violation=1:detect_leaks=1"
        # UndefinedBehaviorSanitizer选项  
        ENVIRONMENT "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
        # ThreadSanitizer选项
        ENVIRONMENT "TSAN_OPTIONS=history_size=7:halt_on_error=1"
        # MemorySanitizer选项
        ENVIRONMENT "MSAN_OPTIONS=print_stats=1:halt_on_error=1"
    )
endif()

# 链接必要的库
if(TARGET gtest)
    target_link_libraries(test_st_vm_deep_recursion gtest gtest_main)
elseif(TARGET GTest::gtest)
    target_link_libraries(test_st_vm_deep_recursion GTest::gtest GTest::Main)
elseif(GTEST_FOUND)
    target_include_directories(test_st_vm_deep_recursion PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_libraries(test_st_vm_deep_recursion ${GTEST_LIBRARIES})
endif()

# 系统依赖
if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(test_st_vm_deep_recursion ${CMAKE_THREAD_LIBS_INIT})
endif()

if(UNIX)
    target_link_libraries(test_st_vm_deep_recursion pthread)
endif()

# 添加文件系统支持（C++17）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(test_st_vm_deep_recursion stdc++fs)
endif()

# 创建Sanitizer测试目标
add_custom_target(sanitizer-tests)
add_dependencies(sanitizer-tests test_st_vm_deep_recursion)

# 创建运行脚本
if(SANITIZER_FLAGS)
    # 创建运行脚本来设置环境变量
    set(RUN_SCRIPT_CONTENT "#!/bin/bash\n")
    string(APPEND RUN_SCRIPT_CONTENT "set -e\n\n")
    
    string(APPEND RUN_SCRIPT_CONTENT "echo \"Running ST VM Deep Recursion Tests with Sanitizers\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "echo \"================================================\"\n")
    
    if(ENABLE_ASAN OR CMAKE_BUILD_TYPE STREQUAL "Sanitizer")
        string(APPEND RUN_SCRIPT_CONTENT "export ASAN_OPTIONS=\"check_initialization_order=1:strict_init_order=1:detect_odr_violation=1:detect_leaks=1:fast_unwind_on_malloc=0\"\n")
        string(APPEND RUN_SCRIPT_CONTENT "echo \"AddressSanitizer enabled with options: $ASAN_OPTIONS\"\n")
    endif()
    
    if(ENABLE_UBSAN OR CMAKE_BUILD_TYPE STREQUAL "Sanitizer")
        string(APPEND RUN_SCRIPT_CONTENT "export UBSAN_OPTIONS=\"print_stacktrace=1:halt_on_error=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/ubsan_suppressions.txt\"\n")
        string(APPEND RUN_SCRIPT_CONTENT "echo \"UndefinedBehaviorSanitizer enabled with options: $UBSAN_OPTIONS\"\n")
    endif()
    
    if(ENABLE_TSAN)
        string(APPEND RUN_SCRIPT_CONTENT "export TSAN_OPTIONS=\"history_size=7:halt_on_error=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan_suppressions.txt\"\n")
        string(APPEND RUN_SCRIPT_CONTENT "echo \"ThreadSanitizer enabled with options: $TSAN_OPTIONS\"\n")
    endif()
    
    if(ENABLE_MSAN)
        string(APPEND RUN_SCRIPT_CONTENT "export MSAN_OPTIONS=\"print_stats=1:halt_on_error=1\"\n")
        string(APPEND RUN_SCRIPT_CONTENT "echo \"MemorySanitizer enabled with options: $MSAN_OPTIONS\"\n")
    endif()
    
    string(APPEND RUN_SCRIPT_CONTENT "\necho \"\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "./test_st_vm_deep_recursion --gtest_color=yes\n")
    string(APPEND RUN_SCRIPT_CONTENT "TEST_RESULT=$?\n\n")
    
    string(APPEND RUN_SCRIPT_CONTENT "if [ $TEST_RESULT -eq 0 ]; then\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"✅ All Sanitizer tests passed!\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"No memory safety issues detected.\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "else\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"❌ Sanitizer tests failed or detected issues!\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "    echo \"Please review the output above for details.\"\n")
    string(APPEND RUN_SCRIPT_CONTENT "fi\n\n")
    
    string(APPEND RUN_SCRIPT_CONTENT "exit $TEST_RESULT\n")
    
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/run_sanitizer_tests.sh" "${RUN_SCRIPT_CONTENT}")
    
    # 创建Windows批处理脚本
    set(BAT_SCRIPT_CONTENT "@echo off\n")
    string(APPEND BAT_SCRIPT_CONTENT "echo Running ST VM Deep Recursion Tests with Sanitizers\n")
    string(APPEND BAT_SCRIPT_CONTENT "echo ================================================\n")
    string(APPEND BAT_SCRIPT_CONTENT "echo Note: Full Sanitizer support requires MinGW/Clang on Windows\n")
    string(APPEND BAT_SCRIPT_CONTENT "echo.\n")
    string(APPEND BAT_SCRIPT_CONTENT "test_st_vm_deep_recursion.exe --gtest_color=yes\n")
    string(APPEND BAT_SCRIPT_CONTENT "if %ERRORLEVEL% equ 0 (\n")
    string(APPEND BAT_SCRIPT_CONTENT "    echo.\n")
    string(APPEND BAT_SCRIPT_CONTENT "    echo ✅ All tests passed!\n")
    string(APPEND BAT_SCRIPT_CONTENT ") else (\n")
    string(APPEND BAT_SCRIPT_CONTENT "    echo.\n")
    string(APPEND BAT_SCRIPT_CONTENT "    echo ❌ Tests failed or detected issues!\n")
    string(APPEND BAT_SCRIPT_CONTENT ")\n")
    string(APPEND BAT_SCRIPT_CONTENT "pause\n")
    
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/run_sanitizer_tests.bat" "${BAT_SCRIPT_CONTENT}")
    
    # 设置脚本权限
    if(UNIX)
        file(COPY "${CMAKE_CURRENT_BINARY_DIR}/run_sanitizer_tests.sh" 
             DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endif()

# 创建抑制文件示例
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/ubsan_suppressions.txt" 
"# UndefinedBehaviorSanitizer抑制文件
# 用于抑制已知的无害未定义行为

# 示例抑制规则：
# function:problem_function
# src:problematic_file.cpp
# vptr:ClassName
")

file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/tsan_suppressions.txt"
"# ThreadSanitizer抑制文件  
# 用于抑制已知的无害数据竞争

# 示例抑制规则：
# race:function_name
# deadlock:mutex_name
# thread:thread_function
")

# 添加测试到CTest
if(ENABLE_TESTING)
    add_test(NAME st_vm_sanitizer_tests COMMAND test_st_vm_deep_recursion)
    
    # 设置测试属性
    set_tests_properties(st_vm_sanitizer_tests PROPERTIES
        TIMEOUT 300  # 5分钟超时
        LABELS "sanitizer;vm;recursion"
    )
    
    # 如果启用了Sanitizer，设置环境变量
    if(SANITIZER_FLAGS)
        set_tests_properties(st_vm_sanitizer_tests PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=check_initialization_order=1:detect_leaks=1;UBSAN_OPTIONS=print_stacktrace=1"
        )
    endif()
endif()

# 输出配置摘要
message(STATUS "Sanitizer test configuration:")
message(STATUS "  Sanitizer flags: ${SANITIZER_FLAGS}")
message(STATUS "  Link flags: ${SANITIZER_LINK_FLAGS}")
message(STATUS "  Test executable: test_st_vm_deep_recursion")

if(UNIX)
    message(STATUS "  Run script: ${CMAKE_CURRENT_BINARY_DIR}/run_sanitizer_tests.sh")
else()
    message(STATUS "  Run script: ${CMAKE_CURRENT_BINARY_DIR}/run_sanitizer_tests.bat")
endif()