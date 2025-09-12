# CI基准门禁测试

# 查找依赖
find_package(Threads REQUIRED)

# 多种方式查找GTest（兼容不同Linux发行版）
find_package(GTest QUIET)
find_package(PkgConfig QUIET)

if(NOT GTest_FOUND AND NOT GTEST_FOUND AND NOT TARGET GTest::gtest)
    # 尝试pkg-config查找
    if(PkgConfig_FOUND)
        pkg_check_modules(GTEST gtest)
        pkg_check_modules(GTEST_MAIN gtest_main)
    endif()
    
    # 如果还是没找到，尝试系统路径
    if(NOT GTEST_FOUND)
        find_path(GTEST_INCLUDE_DIR gtest/gtest.h
            PATHS /usr/include /usr/local/include)
        find_library(GTEST_LIBRARY gtest
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
        find_library(GTEST_MAIN_LIBRARY gtest_main
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
            
        if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY AND GTEST_MAIN_LIBRARY)
            set(GTEST_FOUND TRUE)
        endif()
    endif()
endif()

find_package(benchmark QUIET)

# CI基准门禁测试可执行文件
add_executable(ci-benchmark-gates
    ci-benchmark-gates.cpp
)

# 链接库 - 兼容多种GTest安装方式
if(TARGET GTest::gtest)
    # 现代CMake方式
    target_link_libraries(ci-benchmark-gates
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
elseif(TARGET gtest)
    # 旧版CMake方式
    target_link_libraries(ci-benchmark-gates
        gtest
        gtest_main
        Threads::Threads
    )
elseif(GTEST_FOUND AND GTEST_LIBRARY AND GTEST_MAIN_LIBRARY)
    # 手动查找方式
    target_include_directories(ci-benchmark-gates PRIVATE ${GTEST_INCLUDE_DIR})
    target_link_libraries(ci-benchmark-gates
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        Threads::Threads
    )
else()
    message(FATAL_ERROR "GTest not found! Please install libgtest-dev package.")
endif()

# Linux特定的库
if(UNIX AND NOT APPLE)
    target_link_libraries(ci-benchmark-gates rt)
endif()

# 如果找到benchmark库，也链接它
if(benchmark_FOUND)
    target_link_libraries(ci-benchmark-gates benchmark::benchmark)
    target_compile_definitions(ci-benchmark-gates PRIVATE HAVE_BENCHMARK)
endif()

# 设置编译选项 - 彻底解决MSVC标志冲突
if(MSVC)
    # 彻底清除所有继承的编译标志，重新设置
    set_target_properties(ci-benchmark-gates PROPERTIES
        COMPILE_FLAGS ""
        COMPILE_OPTIONS ""
    )
    
    # 明确禁用运行时检查并设置适当的标志
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ci-benchmark-gates PRIVATE
            /O2          # 优化
            /DNDEBUG     # 禁用调试宏
            /GS-         # 禁用安全检查（避免与优化冲突）
        )
    else()
        target_compile_options(ci-benchmark-gates PRIVATE
            /Od          # 禁用优化
            /Zi          # 调试信息
            /RTC1        # Debug模式才启用运行时检查
        )
    endif()
    
    # 设置运行时库类型避免冲突
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(ci-benchmark-gates PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
        )
    else()
        set_target_properties(ci-benchmark-gates PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
        )
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang处理
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ci-benchmark-gates PRIVATE
            -O3
            -march=native
            -mtune=native
            -DNDEBUG
        )
    else()
        target_compile_options(ci-benchmark-gates PRIVATE
            -g
            -O1
        )
    endif()
endif()

# 添加测试
add_test(
    NAME CIBenchmarkGates
    COMMAND ci-benchmark-gates
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 设置测试属性 - Debug/Sanitizer模式需要更长时间
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(TEST_TIMEOUT 900)  # Debug模式: 15分钟超时
else()
    set(TEST_TIMEOUT 300)  # Release模式: 5分钟超时
endif()

set_tests_properties(CIBenchmarkGates PROPERTIES
    TIMEOUT ${TEST_TIMEOUT}
    LABELS "benchmark;ci;performance"
)

# 创建实时基准测试目标
add_custom_target(ci-benchmark-rt
    COMMAND sudo chrt -f 99 nice -n -20 ./ci-benchmark-gates
    DEPENDS ci-benchmark-gates
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行实时优先级CI基准测试"
)

# 创建基准报告目标
add_custom_target(ci-benchmark-report
    COMMAND ./ci-benchmark-gates --gtest_output=xml:ci-benchmark-results.xml
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/generate-benchmark-report.py ci-benchmark-report.json
    DEPENDS ci-benchmark-gates
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "生成CI基准测试报告"
)

# 安装CI基准测试
install(TARGETS ci-benchmark-gates
    RUNTIME DESTINATION bin
    COMPONENT testing
)