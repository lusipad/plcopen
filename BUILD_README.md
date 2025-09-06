# PLC Runtime Core 构建说明

**版本**: v1.0.0-MVP1  
**最后更新**: 2025-09-06  
**适用系统**: Windows 10/11, Linux (Ubuntu 22.04+)

## 概述

本项目采用标准的CMake构建系统，支持跨平台编译和完整的项目依赖管理。构建系统包含多个静态库和可执行文件，提供完整的PLC运行时功能。

## 系统要求

### Windows
- Windows 10/11
- Visual Studio 2019 或更高版本（或 Visual Studio Build Tools）
- CMake 3.15 或更高版本
- PowerShell 5.1 或更高版本（可选，用于脚本）

### Linux
- GCC 7.0 或更高版本（支持C++17）
- CMake 3.15 或更高版本
- Make 或 Ninja 构建工具

## 标准构建流程

### Windows 构建

#### 使用Visual Studio
```cmd
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake .. -G "Visual Studio 16 2019" -A x64

# 编译项目
cmake --build . --config Release

# 运行测试
ctest --config Release
```

#### 使用命令行工具
```cmd
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译项目
cmake --build . --config Release
```

### Linux 构建

#### 标准构建
```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译项目
make -j$(nproc)

# 运行测试
ctest
```

#### 使用Ninja构建（推荐，更快）
```bash
# 安装Ninja（如果未安装）
sudo apt-get install ninja-build  # Ubuntu/Debian

# 创建构建目录
mkdir build && cd build

# 使用Ninja生成器配置
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# 编译项目
ninja
```

## 构建目标

项目包含以下构建目标：

### 静态库
- `st_compiler` - ST语言编译器库
- `io_system` - I/O子系统库  
- `fb_system` - 功能块系统库
- `scheduler` - 实时调度器库

### 可执行文件
- `mvp1_integration_test` - MVP-1集成测试
- `simple_mvp1_test` - 简化MVP-1测试
- `simple_axis_test` - 轴运动测试
- `comprehensive_tdd_tests` - 完整TDD测试套件
- `test_st_compiler` - ST编译器单元测试
- `mvp1_showcase` - MVP-1功能展示程序（如果存在源文件）
- `ci-benchmark-gates` - CI基准测试

### 输出目录结构

构建完成后，输出文件位于：
```
build/
├── bin/           # 可执行文件
│   ├── mvp1_integration_test(.exe)
│   ├── simple_mvp1_test(.exe)
│   ├── simple_axis_test(.exe)
│   ├── comprehensive_tdd_tests(.exe)
│   ├── test_st_compiler(.exe)
│   └── ci-benchmark-gates(.exe)
└── lib/           # 静态库文件
    ├── libst_compiler.a/.lib
    ├── libio_system.a/.lib
    ├── libfb_system.a/.lib
    └── libscheduler.a/.lib
```

## 快速开始

### 1. 克隆并构建项目
```bash
# 克隆项目
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# 创建构建目录并构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)  # Linux
# 或
cmake --build . --config Release  # Windows
```

### 2. 运行测试程序
```bash
# 运行MVP-1集成测试
./bin/mvp1_integration_test

# 运行完整TDD测试套件
./bin/comprehensive_tdd_tests

# 运行CI基准测试
./bin/ci-benchmark-gates
```

## 高级构建选项

### 构建类型选择
```bash
# Debug构建（包含调试信息）
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release构建（优化性能）
cmake .. -DCMAKE_BUILD_TYPE=Release

# RelWithDebInfo（优化+调试信息）
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### 安装项目
```bash
# 构建后安装到系统目录
cmake --build . --target install

# 或指定安装前缀
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/plc-runtime
cmake --build . --target install
```

## 故障排除

### 常见问题

1. **CMake版本过低**
   ```bash
   # 检查CMake版本
   cmake --version
   
   # 升级CMake（Ubuntu/Debian）
   sudo apt-get update
   sudo apt-get install cmake
   
   # 或下载最新版本
   wget https://cmake.org/files/v3.20/cmake-3.20.0-Linux-x86_64.sh
   chmod +x cmake-3.20.0-Linux-x86_64.sh
   sudo ./cmake-3.20.0-Linux-x86_64.sh --prefix=/usr/local --skip-license
   ```

2. **编译器不支持C++17**
   ```bash
   # 检查GCC版本
   gcc --version
   g++ --version
   
   # Ubuntu升级到GCC 7+
   sudo apt-get install gcc-7 g++-7
   sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 100
   sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 100
   ```

3. **构建目录权限问题**
   ```bash
   # 清理构建目录
   rm -rf build
   mkdir build
   cd build
   
   # 确保有写权限
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

4. **链接错误**
   - 在Linux上可能需要安装pthread库：
     ```bash
     sudo apt-get install libpthread-stubs0-dev
     ```
   - 对于实时调度功能，可能需要：
     ```bash
     sudo apt-get install librt-dev
     ```

5. **测试运行失败**
   ```bash
   # 检查可执行文件是否存在
   ls -la build/bin/
   
   # 设置执行权限（Linux）
   chmod +x build/bin/*
   
   # 使用详细输出运行测试
   ./build/bin/comprehensive_tdd_tests --verbose
   ```

### 开发模式构建

对于开发者，推荐使用Debug模式和额外的警告选项：
```bash
mkdir build-debug && cd build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror -g"
make -j$(nproc)
```

### 项目结构说明

```
plcopen/
├── CMakeLists.txt     # 主构建配置
├── include/           # 头文件目录
│   ├── error/         # 错误处理模块
│   ├── fb/            # 功能块模块
│   ├── io/            # I/O系统模块
│   ├── lockfree/      # 无锁数据结构
│   ├── memory/        # 内存管理模块
│   ├── scheduler/     # 调度器模块
│   └── st_compiler/   # ST编译器模块
├── src/               # 源代码目录
│   ├── fb/            # 功能块实现
│   ├── io/            # I/O系统实现
│   ├── scheduler/     # 调度器实现
│   └── st_compiler/   # ST编译器实现
├── tests/             # 测试代码
│   ├── ci/            # CI基准测试
│   ├── unit/          # 单元测试
│   └── comprehensive_tdd_tests.cpp
└── build/             # 构建输出目录（生成）
    ├── bin/           # 可执行文件
    └── lib/           # 静态库
```

## 许可证

本项目遵循项目主许可证。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进构建脚本。
