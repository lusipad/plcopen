# CI基准门禁测试

# 查找Google Test和Benchmark库
find_package(GTest REQUIRED)
find_package(benchmark QUIET)

# CI基准门禁测试可执行文件
add_executable(ci-benchmark-gates
    ci-benchmark-gates.cpp
)

# 链接库
target_link_libraries(ci-benchmark-gates
    plc_runtime_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Linux特定的库
if(UNIX AND NOT APPLE)
    target_link_libraries(ci-benchmark-gates rt)
endif()

# 如果找到benchmark库，也链接它
if(benchmark_FOUND)
    target_link_libraries(ci-benchmark-gates benchmark::benchmark)
    target_compile_definitions(ci-benchmark-gates PRIVATE HAVE_BENCHMARK)
endif()

# 设置编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ci-benchmark-gates PRIVATE
        -O3
        -march=native
        -mtune=native
        -DNDEBUG
    )
elseif(MSVC)
    target_compile_options(ci-benchmark-gates PRIVATE
        /O2
        /DNDEBUG
    )
endif()

# 添加测试
add_test(
    NAME CIBenchmarkGates
    COMMAND ci-benchmark-gates
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 设置测试属性
set_tests_properties(CIBenchmarkGates PROPERTIES
    TIMEOUT 300  # 5分钟超时
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