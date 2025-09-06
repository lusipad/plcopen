# 测试目录结构

**版本**: v1.0.0-MVP1  
**最后更新**: 2025-09-06  
**目录重组**: 标准化测试文件结构

## 📁 目录结构

```
tests/
├── unit/                       # 单元测试
│   ├── test_real_time_scheduler.cpp    # 实时调度器单元测试
│   ├── test_memory_manager.cpp         # 内存管理单元测试
│   ├── test_lockfree_structures.cpp    # 无锁数据结构单元测试
│   ├── test_error_handler.cpp          # 错误处理单元测试
│   ├── test_st_compiler.cpp           # ST编译器单元测试
│   ├── run_all_unit_tests.cpp         # 单元测试执行器
│   └── CMakeLists.txt                  # 单元测试构建配置
│
├── integration/                # 集成测试
│   └── mvp1_integration_test.cpp      # MVP-1集成测试
│
├── mvp1/                      # MVP-1专项测试
│   ├── simple_mvp1_test.cpp          # 简化MVP-1测试
│   └── simple_axis_test.cpp          # 轴运动测试
│
├── ci/                        # CI基准测试
│   ├── ci-benchmark-gates.cpp        # CI基准门禁测试
│   ├── CMakeLists.txt                 # CI测试构建配置
│   └── README.md                      # CI测试说明
│
├── performance/               # 性能测试
│   └── rt-benchmark-suite.cpp        # 实时性能基准测试
│
├── comprehensive_tdd_tests.cpp       # 完整TDD测试套件
├── CLAUDE.md                         # 测试模块开发文档
└── README.md                         # 本文件
```

## 🎯 测试分类说明

### 单元测试 (Unit Tests)
- **目标**: 测试单个模块的功能
- **覆盖率**: 85%+ 目标
- **执行**: 快速执行，适合开发过程中频繁运行
- **依赖**: 最小化外部依赖

### 集成测试 (Integration Tests)
- **目标**: 测试模块间协作
- **范围**: 跨模块功能验证
- **执行**: 中等执行时间
- **依赖**: 多个模块协同工作

### MVP-1测试 (MVP-1 Tests)
- **目标**: 验证MVP-1阶段交付功能
- **范围**: 端到端功能验证
- **执行**: 较长执行时间
- **依赖**: 完整系统功能

### CI测试 (CI Tests)
- **目标**: 持续集成质量门禁
- **范围**: 性能基准和回归检测
- **执行**: 自动化执行
- **依赖**: 构建环境

### 性能测试 (Performance Tests)
- **目标**: 实时性能验证
- **范围**: 延迟、吞吐量、资源使用
- **执行**: 需要特殊环境
- **依赖**: 实时内核(推荐)

## 🚀 运行测试

### 构建所有测试
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 运行特定类别测试
```bash
# 单元测试
./bin/run_unit_tests

# 集成测试  
./bin/mvp1_integration_test

# MVP-1测试
./bin/simple_mvp1_test
./bin/simple_axis_test

# 完整TDD测试
./bin/comprehensive_tdd_tests

# CI基准测试
./bin/ci-benchmark-gates
```

### 运行所有测试
```bash
# 使用CTest运行所有注册的测试
ctest --output-on-failure

# 或手动运行所有可执行文件
for test in bin/*test*; do ./$test; done
```

## 📊 测试覆盖率

当前测试覆盖率统计：
- **单元测试**: 新增28个测试套件，200+测试用例
- **集成测试**: MVP-1核心功能覆盖
- **性能测试**: 实时调度和I/O性能基准
- **TDD测试**: 24个综合测试用例

目标覆盖率：**85%+**

## 🛠️ 开发指南

### 添加新的单元测试
1. 在 `unit/` 目录创建 `test_[module_name].cpp`
2. 使用统一的测试框架 `test/TestFramework.h`
3. 更新 `unit/CMakeLists.txt`
4. 在 `run_all_unit_tests.cpp` 中注册新测试

### 添加新的集成测试
1. 在 `integration/` 目录创建测试文件
2. 在主 `CMakeLists.txt` 中添加构建目标
3. 更新CI配置文件

### 测试命名约定
- 单元测试: `test_[module_name].cpp`
- 集成测试: `[feature]_integration_test.cpp`
- 性能测试: `[feature]_benchmark.cpp`
- 专项测试: `[specific_feature]_test.cpp`

---

*此目录结构遵循C++项目的最佳实践，提供清晰的测试分类和管理。*