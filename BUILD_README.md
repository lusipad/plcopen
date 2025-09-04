# PLC Runtime Core 构建说明

## 概述

本项目采用简化的构建方式，移除了复杂的CMake配置，直接使用编译器命令行进行编译。

## 系统要求

### Windows
- Windows 10/11
- Visual Studio 2019 或更高版本（或 Visual Studio Build Tools）
- PowerShell 5.1 或更高版本（可选，用于脚本）

### Linux
- GCC 7.0 或更高版本（支持C++17）
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

#### 使用Makefile（如果存在）
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
- `ci-benchmark-gates.exe` - CI基准测试程序

### Linux
- `test_simple` - 基本功能测试程序
- `plc_runtime_demo` - 演示程序
- `ci-benchmark-gates` - CI基准测试程序

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
   # 检查Visual Studio编译器是否可用
   where cl.exe
   
   # 如果未找到，需要运行Visual Studio开发者命令提示符
   # 或手动设置环境变量
   ```
   
   **Linux:**
   ```bash
   # 检查GCC是否安装
   gcc --version
   g++ --version
   
   # 如果未安装，使用包管理器安装
   sudo apt-get install build-essential  # Ubuntu/Debian
   sudo yum install gcc-c++              # CentOS/RHEL
   ```

2. **编译错误**
   - 检查C++17支持：确保编译器版本足够新
   - 检查头文件路径：确保include和src目录路径正确
   - 检查源文件依赖：确保所有必需的源文件都存在

3. **链接错误**
   - 确保所有依赖的库都已正确链接
   - 检查库文件路径和名称
   - 在Windows上可能需要添加系统库（如kernel32.lib）

4. **运行时错误**
   - 检查可执行文件权限（Linux上可能需要sudo）
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

- `/EHsc` (Windows): 启用C++异常处理
- `/std:c++17`: 使用C++17标准
- `/I"path"`: 添加头文件搜索路径
- `/Fe:name.exe`: 指定输出可执行文件名
- `-O2` (Linux): 启用优化
- `-g` (Linux): 包含调试信息

## 许可证

本项目遵循项目主许可证。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进构建脚本。
