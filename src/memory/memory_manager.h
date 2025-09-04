# PLC Runtime Core 构建说明

## 概述

本项目采用简化的构建方式，移除了复杂的 CMake 配置，直接使用编译器命令行进行编译。

## 系统要求

### Windows
- Windows 10/11
- Visual Studio 2019 或更高版本（或 Visual Studio Build Tools）
- PowerShell 5.1 或更高版本（可选，用于脚本）

### Linux
- GCC 7.0 或更高版本（支持 C++17）
- Make（可选）

## 使用方法

### Windows 构建

#### 基本编译
```cmd
# 编译测试程序
cl.exe /EHsc /std:c++17 /I"include" /I"src" test_simple.cpp /Fe:test_simple.exe

# 编译演示程序
cl.exe /EHsc /std:c++17 /I"include" /I"src" src\demo\demo_main.cpp /Fe:plc_runtime_demo.exe

# 编译CI基准测试
cl.exe /EHsc /std:c++17 /I"include" /I"src" tests\ci\ci-benchmark-gates.cpp /Fe:ci-benchmark-gates.exe
```

#### 使用构建脚本（如果存在）
```powershell
# 运行构建脚本
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

### Linux 构建

#### 基本编译
```bash
# 编译测试程序
g++ -std=c++17 -Iinclude -Isrc -O2 test_simple.cpp -o test_simple

# 编译演示程序
g++ -std=c++17 -Iinclude -Isrc -O2 src/demo/demo_main.cpp -o plc_runtime_demo

# 编译CI基准测试
g++ -std=c++17 -Iinclude -Isrc -O2 tests/ci/ci-benchmark-gates.cpp -o ci-benchmark-gates
```

#### 使用 Makefile（如果存在）
```bash
# 编译所有目标
make

# 清理构建产物
make clean
```

## 构建特性

### 当前构建方式的优势

- ✅ 简化的构建流程，无需复杂配置
- ✅ 直接使用编译器，减少依赖
- ✅ 快速编译和测试
- ✅ 易于调试和定制
- ✅ 跨平台兼容（Windows/Linux）
- ✅ 清洁的项目结构

## 输出文件

构建完成后，会在项目根目录生成以下可执行文件：

### Windows
- `test_simple.exe` - 基本功能测试程序
- `plc_runtime_demo.exe` - 演示程序
- `ci-benchmark-gates.exe` - CI 基准测试程序

### Linux
- `test_simple` - 基本功能测试程序
- `plc_runtime_demo` - 演示程序
- `ci-benchmark-gates` - CI 基准测试程序

## 项目结构

```
plc-runtime-core/
├── include/           # 头文件目录
├── src/              # 源代码目录
├── tests/            # 测试代码
├── examples/         # 示例程序
└── scripts/          # 构建脚本（可选）
```

## 故障排除

### 常见问题

1. **编译器未找到**
   
   **Windows:**
   ```cmd
   # 检查 Visual Studio 编译器是否可用
   where cl.exe
   
   # 如果未找到，需要运行 Visual Studio 开发者命令提示符
   # 或手动设置环境变量
   ```
   
   **Linux:**
   ```bash
   # 检查 GCC 是否安装
   gcc --version
   g++ --version
   
   # 如果未安装，使用包管理器安装
   sudo apt-get install build-essential  # Ubuntu/Debian
   sudo yum install gcc-c++              # CentOS/RHEL
   ```

2. **编译错误**
   - 检查 C++17 支持：确保编译器版本足够新
   - 检查头文件路径：确保 include 和 src 目录路径正确
   - 检查源文件依赖：确保所有必需的源文件都存在

3. **链接错误**
   - 确保所有依赖的库都已正确链接
   - 检查库文件路径和名称
   - 在 Windows 上可能需要添加系统库（如 kernel32.lib）

4. **运行时错误**
   - 检查可执行文件权限（Linux 上可能需要 sudo）
   - 确保所有依赖的动态库都可用
   - 检查工作目录和文件路径

### 详细构建步骤

如果需要更精细的控制，可以手动执行以下步骤：

#### Windows 详细步骤
```cmd
# 1. 打开Visual Studio开发者命令提示符
# 2. 导航到项目目录
cd /d "d:\Repos\plcopen"

# 3. 编译各个组件
cl.exe /EHsc /std:c++17 /I"include" /I"src" /c src\scheduler\*.cpp
cl.exe /EHsc /std:c++17 /I"include" /I"src" /c src\memory\*.cpp
cl.exe /EHsc /std:c++17 /I"include" /I"src" /c src\io\*.cpp

# 4. 链接生成可执行文件
cl.exe /EHsc /std:c++17 /I"include" /I"src" *.obj test_simple.cpp /Fe:test_simple.exe

# 5. 运行测试
.\test_simple.exe
```

#### Linux 详细步骤
```bash
# 1. 导航到项目目录
cd /path/to/plc-runtime-core

# 2. 编译各个组件
g++ -std=c++17 -Iinclude -Isrc -c src/scheduler/*.cpp
g++ -std=c++17 -Iinclude -Isrc -c src/memory/*.cpp
g++ -std=c++17 -Iinclude -Isrc -c src/io/*.cpp

# 3. 链接生成可执行文件
g++ -std=c++17 -Iinclude -Isrc *.o test_simple.cpp -o test_simple

# 4. 运行测试
./test_simple
```

## 开发说明

### 添加新的源文件

当添加新的源文件时，需要在编译命令中包含它们：

```cmd
# Windows
cl.exe /EHsc /std:c++17 /I"include" /I"src" test_simple.cpp src\new_module\new_file.cpp /Fe:test_simple.exe
```

```bash
# Linux
g++ -std=c++17 -Iinclude -Isrc test_simple.cpp src/new_module/new_file.cpp -o test_simple
```

### 编译选项说明

- `/EHsc` (Windows): 启用 C++ 异常处理
- `/std:c++17`: 使用 C++17 标准
- `/I"path"`: 添加头文件搜索路径
- `/Fe:name.exe`: 指定输出可执行文件名
- `-O2` (Linux): 启用优化
- `-g` (Linux): 包含调试信息

## 许可证

本项目遵循项目主许可证。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进构建脚本。

| 指标 | 目标值 | 典型值 |
|------|--------|--------|
| 调度抖动 | < 50μs | ~20μs |
| I/O 扫描周期 | < 100μs | ~80μs |
| 内存分配时间 | < 10μs | ~5μs |
| 任务切换时间 | < 5μs | ~2μs |
| ST 编译时间 | < 1s | ~500ms |
| 系统启动时间 | < 5s | ~3s |

## 项目状态

### 最近更新

本项目最近进行了大规模清理，移除了以下无效文件：
- CMake 构建文件和缓存
- Visual Studio 项目文件 (.vcxproj, .sln)
- 编译输出文件 (.obj, .pdb, .ilk, .exe, .dll)
- 临时测试目录和文件
- Python 缓存文件
- 其他构建产物

### 当前构建方式

项目现在采用更简洁的构建方式：
- 直接使用编译器命令行编译
- 移除了复杂的 CMake 配置
- 保持了核心源代码和头文件结构
- 更新了.gitignore 以防止未来的构建产物污染

详细的清理报告请参见 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。

## 开发指南

### 项目结构

```
plc-runtime-core/
├── .github/                # GitHub工作流和CI配置
│   └── workflows/         # CI/CD工作流文件
├── .kiro/                 # Kiro规范和追踪文件
│   └── specs/            # 项目规范文档
├── include/               # 公共头文件
│   ├── config/           # 配置管理头文件
│   ├── error/            # 错误处理头文件
│   ├── function_block/   # 功能块头文件
│   ├── io/               # I/O系统头文件
│   ├── lockfree/         # 无锁数据结构头文件
│   └── plc_runtime_core.h # 主头文件
├── src/                   # 源代码
│   ├── common/           # 通用工具和实用程序
│   ├── config/           # 配置管理实现
│   ├── demo/             # 演示程序
│   ├── error/            # 错误处理实现
│   ├── fb/               # 功能块实现
│   ├── function_block/   # 功能块引擎
│   ├── io/               # I/O系统实现
│   ├── lockfree/         # 无锁数据结构实现
│   ├── memory/           # 内存管理器
│   ├── misc/             # 杂项工具
│   ├── motion/           # 运动控制
│   ├── scheduler/        # 实时调度器
│   └── test/             # 内部测试文件
├── tests/                 # 测试代码
│   ├── ci/               # CI基准测试
│   ├── performance/      # 性能测试
│   └── unit/             # 单元测试
├── examples/              # 示例程序
├── scripts/               # 构建和配置脚本
├── docs/                  # 附加文档
└── doc/                   # 主要文档
    ├── design/           # 设计文档
    ├── reference/        # 参考文档
    └── user_guide/       # 用户指南
```

### 编码规范

- **C++ 标准**: C++17
- **命名约定**: snake_case (变量), PascalCase (类)
- **代码格式**: 使用 clang-format
- **注释**: Doxygen 格式

### 测试策略

- **单元测试**: 80% 代码覆盖率
- **集成测试**: 模块间接口测试
- **性能测试**: 实时性能基准
- **压力测试**: 长期稳定性验证

## 部署指南

### 生产环境部署

1. **系统配置**:
```bash
# 配置实时参数
echo "@realtime soft rtprio 99" >> /etc/security/limits.conf
echo "kernel.sched_rt_runtime_us = -1" >> /etc/sysctl.conf

# 禁用不必要的服务
systemctl disable bluetooth
systemctl disable cups
```

2. **性能调优**:
```bash
# CPU亲和性设置
echo 2-3 > /sys/devices/system/cpu/cpu0/cpuset.cpus  # 隔离CPU核心

# 中断亲和性
echo 1 > /proc/irq/0/smp_affinity  # 将中断绑定到特定CPU
```

3. **监控配置**:
```bash
# 启用性能监控
./rt_benchmark_suite > /var/log/plc-performance.log &
```

### Docker 部署

```bash
# 构建Docker镜像
docker build -t plc-runtime-core .

# 运行容器
docker run --privileged --network=host \
  -v /dev:/dev \
  plc-runtime-core
```

## 故障排除

### 常见问题

1. **调度延迟过高**:
   - 检查是否运行 RT 内核：`uname -r`
   - 验证实时权限：`ulimit -r`
   - 检查 CPU 负载：`top`

2. **内存分配失败**:
   - 检查内存预算配置
   - 查看内存使用统计
   - 检查是否有内存泄漏

3. **I/O 通信错误**:
   - 验证设备连接
   - 检查网络配置
   - 查看通信日志

### 调试工具

```bash
# 实时性能分析
perf record -g ./plc_runtime_demo
perf report

# 内存检查
valgrind --tool=memcheck ./plc_runtime_demo

# 系统调用跟踪
strace -f ./plc_runtime_demo
```

## 贡献指南

1. Fork 项目
2. 创建特性分支：`git checkout -b feature/new-feature`
3. 提交更改：`git commit -am 'Add new feature'`
4. 推送分支：`git push origin feature/new-feature`
5. 创建 Pull Request

### 代码审查清单

- [ ] 代码符合编码规范
- [ ] 添加了适当的测试
- [ ] 文档已更新
- [ ] 性能测试通过
- [ ] 静态分析无问题

## 许可证

本项目采用 MIT 许可证 - 详见[LICENSE](LICENSE)文件。

## 联系方式

- **项目主页**: <repository-url>
- **问题报告**: <repository-url>/issues
- **邮件**: plc-runtime-team@example.com

## 致谢

感谢以下开源项目的贡献：
- Linux RT-PREEMPT 项目
- ANTLR4 解析器生成器
- Google Test 测试框架
- gRPC 通信框架

---

**注意**: 本项目处于活跃开发阶段，API 可能会发生变化。生产环境使用前请充分测试。
"