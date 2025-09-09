# ST VM深度递归与Sanitizer测试指南

本指南介绍如何使用Sanitizer工具来验证ST虚拟机在深度递归和内存安全方面的表现。

## 📋 概述

ST VM深度递归测试是专门设计用来验证PLCOpen ST虚拟机在极限条件下的稳定性和安全性。通过结合各种Sanitizer工具，我们可以检测：

- 栈溢出和内存访问越界
- 未定义行为和数据竞争
- 内存泄漏和未初始化内存访问
- 线程安全性问题

## 🛠️ 支持的Sanitizer

### AddressSanitizer (ASan)
**用途**: 检测内存访问错误、缓冲区溢出、使用后释放等问题

**编译标志**: `-fsanitize=address`

**环境变量**:
```bash
export ASAN_OPTIONS="check_initialization_order=1:strict_init_order=1:detect_odr_violation=1:detect_leaks=1:fast_unwind_on_malloc=0"
```

**检测内容**:
- 缓冲区溢出（栈和堆）
- 使用后释放 (use-after-free)
- 双重释放 (double-free)
- 内存泄漏
- 初始化顺序问题

### UndefinedBehaviorSanitizer (UBSan)
**用途**: 检测C++未定义行为

**编译标志**: `-fsanitize=undefined -fsanitize=nullability`

**环境变量**:
```bash
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
```

**检测内容**:
- 整数溢出
- 空指针解引用
- 数组越界访问
- 类型转换错误
- 浮点异常

### ThreadSanitizer (TSan)
**用途**: 检测数据竞争和死锁

**编译标志**: `-fsanitize=thread`

**环境变量**:
```bash
export TSAN_OPTIONS="history_size=7:halt_on_error=1"
```

**检测内容**:
- 数据竞争 (data races)
- 死锁 (deadlocks)
- 线程泄漏
- 原子操作错误

**注意**: TSan与其他Sanitizer互斥，需要单独构建

### MemorySanitizer (MSan)
**用途**: 检测未初始化内存访问

**编译标志**: `-fsanitize=memory -fsanitize-memory-track-origins`

**环境变量**:
```bash
export MSAN_OPTIONS="print_stats=1:halt_on_error=1"
```

**检测内容**:
- 未初始化内存读取
- 内存来源跟踪
- 变量初始化验证

**注意**: 仅Clang编译器支持，且与其他Sanitizer互斥

## 🚀 构建和运行

### 使用CMake构建

#### 1. 基本Sanitizer构建 (ASan + UBSan)
```bash
mkdir build-sanitizer && cd build-sanitizer
cmake -DCMAKE_BUILD_TYPE=Sanitizer -DENABLE_ASAN=ON -DENABLE_UBSAN=ON ..
make -j$(nproc)
cd tests/sanitizer
./run_sanitizer_tests.sh
```

#### 2. ThreadSanitizer构建
```bash
mkdir build-tsan && cd build-tsan  
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON ..
make -j$(nproc)
cd tests/sanitizer
./run_sanitizer_tests.sh
```

#### 3. MemorySanitizer构建 (仅Clang)
```bash
mkdir build-msan && cd build-msan
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_MSAN=ON ..
make -j$(nproc)  
cd tests/sanitizer
./run_sanitizer_tests.sh
```

### 手动编译和运行

#### AddressSanitizer示例
```bash
g++ -fsanitize=address -fno-omit-frame-pointer -g -O1 \
    -I../../include \
    test_st_vm_deep_recursion.cpp \
    ../../src/error/*.cpp \
    ../../src/logging/*.cpp \
    -lgtest -lgtest_main -pthread \
    -o test_asan

export ASAN_OPTIONS="check_initialization_order=1:detect_leaks=1"
./test_asan --gtest_color=yes
```

#### UndefinedBehaviorSanitizer示例
```bash
clang++ -fsanitize=undefined -fsanitize=nullability -fno-sanitize-recover=all \
    -g -O1 -I../../include \
    test_st_vm_deep_recursion.cpp \
    ../../src/error/*.cpp \
    ../../src/logging/*.cpp \
    -lgtest -lgtest_main -pthread \
    -o test_ubsan

export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
./test_ubsan --gtest_color=yes
```

## 🧪 测试用例说明

### 1. SimpleRecursiveFunctionStackOverflow
**目标**: 验证栈溢出检测机制

**测试内容**:
- 深度递归的阶乘计算
- 栈空间耗尽检测
- 优雅错误恢复

**预期结果**: 
- ASan: 检测到栈溢出
- VM: 返回STACK_OVERFLOW错误码
- 系统: 不崩溃，可恢复

### 2. NestedLoopRecursionStressTest  
**目标**: 压力测试嵌套递归调用

**测试内容**:
- 斐波那契递归嵌套
- 循环中的递归调用
- 复杂调用栈管理

**预期结果**:
- 执行时间合理
- 内存使用稳定
- 错误处理正确

### 3. MemoryBoundsCheckingWithRecursion
**目标**: 验证数组边界检查

**测试内容**:
- 递归中的数组访问
- 边界值测试
- 越界访问检测

**预期结果**:
- ASan: 检测越界访问
- UBSan: 检测数组索引错误
- VM: 边界检查生效

### 4. ConcurrentRecursionThreadSafety
**目标**: 验证多线程递归安全性

**测试内容**:
- 多线程并发递归
- 共享状态访问
- 线程安全验证

**预期结果**:
- TSan: 无数据竞争报告
- 多线程执行成功
- 结果一致性保证

### 5. UninitializedMemoryAccessDetection
**目标**: 检测未初始化内存访问

**测试内容**:
- 未初始化变量使用
- 条件初始化场景
- 内存访问模式

**预期结果**:
- MSan: 检测未初始化访问
- UBSan: 检测相关未定义行为
- VM: 变量初始化检查

### 6. StackOverflowRecoveryMechanism
**目标**: 验证栈溢出恢复机制

**测试内容**:
- 可控深度递归
- 不同深度测试
- 恢复机制验证

**预期结果**:
- 栈溢出优雅处理
- VM状态可恢复
- 后续操作正常

### 7. ComprehensiveSanitizerStressTest
**目标**: 综合Sanitizer压力测试

**测试内容**:
- 多种操作类型
- 大内存访问模式
- 复杂控制流程

**预期结果**:
- 所有Sanitizer无报错
- 性能指标合理
- 内存使用稳定

## 📊 结果解读

### 成功标志
- ✅ 所有测试用例通过
- ✅ 无Sanitizer错误报告
- ✅ VM状态保持稳定
- ✅ 内存使用在预期范围内

### 常见问题及解决

#### AddressSanitizer错误
```
ERROR: AddressSanitizer: stack-overflow
```
**原因**: 栈空间不足或无限递归
**解决**: 检查递归终止条件，增加栈大小配置

#### UndefinedBehaviorSanitizer错误
```
runtime error: signed integer overflow
```
**原因**: 整数运算溢出
**解决**: 添加溢出检查或使用更大的数据类型

#### ThreadSanitizer错误
```
WARNING: ThreadSanitizer: data race
```
**原因**: 多线程访问共享数据无同步
**解决**: 添加适当的同步机制（mutex、原子操作）

#### MemorySanitizer错误
```
use-of-uninitialized-value
```
**原因**: 使用了未初始化的变量
**解决**: 确保变量在使用前正确初始化

## 🔧 高级配置

### Sanitizer选项调优

#### AddressSanitizer详细配置
```bash
export ASAN_OPTIONS="\
check_initialization_order=1:\
strict_init_order=1:\
detect_odr_violation=1:\
detect_leaks=1:\
fast_unwind_on_malloc=0:\
malloc_context_size=30:\
print_stats=1:\
print_module_map=2"
```

#### UndefinedBehaviorSanitizer详细配置
```bash
export UBSAN_OPTIONS="\
print_stacktrace=1:\
halt_on_error=1:\
print_summary=1:\
suppressions=ubsan_suppressions.txt"
```

#### ThreadSanitizer详细配置
```bash
export TSAN_OPTIONS="\
history_size=7:\
halt_on_error=1:\
print_full_thread_history=1:\
suppressions=tsan_suppressions.txt"
```

### 抑制文件示例

#### ubsan_suppressions.txt
```
# 抑制已知的无害未定义行为
function:harmless_overflow_function
src:legacy_code.cpp:*
vptr:KnownPolymorphicIssue
```

#### tsan_suppressions.txt
```
# 抑制已知的无害数据竞争
race:benign_race_function
deadlock:known_false_positive
thread:test_helper_thread
```

## 📈 性能基准

### 预期性能指标

| 测试用例 | 无Sanitizer | ASan | UBSan | TSan | MSan |
|---------|-------------|------|-------|------|-------|
| 简单递归 | 1ms | 5ms | 2ms | 10ms | 15ms |
| 嵌套递归 | 10ms | 50ms | 20ms | 100ms | 150ms |
| 内存边界 | 2ms | 10ms | 5ms | 20ms | 30ms |
| 并发测试 | 50ms | 200ms | 100ms | 500ms | - |
| 压力测试 | 100ms | 500ms | 200ms | 1000ms | 1500ms |

### 内存使用基准

| 测试场景 | 基础内存 | ASan倍数 | TSan倍数 | MSan倍数 |
|---------|----------|----------|----------|----------|
| 轻度递归 | 1MB | 3x | 10x | 3x |
| 深度递归 | 10MB | 3x | 10x | 3x |
| 并发测试 | 5MB | 3x | 15x | 3x |

## 🚨 注意事项

### 编译器兼容性
- **GCC**: 支持ASan、UBSan、TSan
- **Clang**: 全支持所有Sanitizer
- **MSVC**: 仅支持ASan（Windows 10+）

### 平台兼容性
- **Linux**: 全支持
- **macOS**: 支持ASan、UBSan、TSan
- **Windows**: 有限支持，推荐MinGW或Clang

### 性能影响
- **AddressSanitizer**: 2-5x性能开销
- **UndefinedBehaviorSanitizer**: 1-2x性能开销  
- **ThreadSanitizer**: 5-15x性能开销，10x内存开销
- **MemorySanitizer**: 2-3x性能开销

### 使用建议
1. **开发阶段**: 使用ASan + UBSan组合
2. **并发测试**: 单独使用TSan
3. **内存问题**: 使用MSan排查
4. **CI集成**: 添加到自动化测试流程
5. **发布前**: 运行完整Sanitizer测试套件

## 📚 参考资源

- [AddressSanitizer官方文档](https://clang.llvm.org/docs/AddressSanitizer.html)
- [UndefinedBehaviorSanitizer文档](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [ThreadSanitizer文档](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [MemorySanitizer文档](https://clang.llvm.org/docs/MemorySanitizer.html)
- [Sanitizers Wiki](https://github.com/google/sanitizers/wiki)

---

*本指南持续更新中，如有问题请参考项目文档或提交Issue。*