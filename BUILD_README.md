# 构建指南（Windows / 跨平台）

新核 `plcopen::plcopen` 是 **header-only INTERFACE 目标**（`core/` 头文件
树），构建产物是测试/demo/绑定，不产出库二进制。Linux 细节见
[BUILD_LINUX.md](BUILD_LINUX.md)；提交前门禁见 `.claude/skills/plcopen-gates`。

## 环境要求

- **Windows**：Windows 10/11、Visual Studio 2022+（VS 2026 已验证）、
  CMake 3.21+、PowerShell 5.1+
- **Linux**：GCC 9+ / Clang 10+、CMake 3.21+
- C++17；核心测试目标以 `-fno-exceptions -fno-rtti`（MSVC `/EHs-c- /GR-`）编译

## 一键构建（Windows）

```powershell
.\build.ps1              # Release 构建
.\build.ps1 -Test        # 构建 + 全量 CTest
.\build.ps1 -Clean       # 清理重建
.\build.ps1 -Install     # 安装到 out/
```

## CMake 直接构建（跨平台）

```bash
cmake -S . -B build -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

### 构建选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `PLCOPEN_BUILD_TESTS` | ON | 全部测试（含 `PLCOPEN_BUILD_CORE_TESTS` 新核套件） |
| `PLCOPEN_BUILD_DEMOS` | ON | `core/demo/`（单轴 FB、组直线、轨迹流、组路径） |
| `PLCOPEN_BUILD_PYTHON_BINDINGS` | OFF | pyplcopen（pybind11 FetchContent；smoke：`ctest -R pyplcopen_smoke`） |
| `PLCOPEN_BUILD_DOCS` | OFF | Doxygen `docs` target（未装 Doxygen 时优雅降级） |
| `PLCOPEN_BUILD_LEGACY` | OFF | 冻结的 v0.x `src/` 线（回放/迁移基线；DoD §5.3 对照工具需要） |

## 覆盖率（Windows）

```powershell
.\coverage.ps1                                   # 默认目录
.\coverage.ps1 -BuildDir build-sync -Configuration Debug
```

## 下游消费

**安装后 `find_package`：**

```bash
cmake --install build --prefix <prefix>
```

```cmake
find_package(plcopen CONFIG REQUIRED)
target_link_libraries(app PRIVATE plcopen::plcopen)
```

**源码树 `FetchContent`：**

```cmake
include(FetchContent)
FetchContent_Declare(plcopen GIT_REPOSITORY https://github.com/lusipad/plcopen.git)
FetchContent_MakeAvailable(plcopen)
target_link_libraries(app PRIVATE plcopen::plcopen)
```

```cpp
#include "axis/state.h"
#include "fb/motion.h"   // 头文件按 core/ 相对路径引用
```

两种消费方式均有 CI 级 smoke（`test_package/`）。

## 常用验证入口

```bash
cmake -P cmake/rt_safety_scan.cmake          # RT 路径静态扫描
cmake -P cmake/verify_replay_fixtures.cmake  # 回放语料格式校验
ctest --test-dir build -R replay             # 黄金回放回归
ctest --test-dir build -L benchmark          # 基准层（含 STREAM_METRICS）
```

## 输出位置

- 构建产物：`build/core/{Debug,Release}/`（测试/demo 可执行文件）
- 安装树：`<prefix>/include/plcopen/`（头文件）+ CMake package config
- 旧线（仅 `PLCOPEN_BUILD_LEGACY=ON`）：`build/src/…/plcopen.dll|.so`
