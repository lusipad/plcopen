# Windows开发环境设置指南

本文档介绍如何在Windows环境下设置PLC运行时核心系统的开发和测试环境。

## 系统要求

- Windows 10 版本 1903 或更高版本
- Windows 11（推荐）
- 至少 8GB RAM
- 至少 10GB 可用磁盘空间

## 开发工具安装

### 方案一：Visual Studio（推荐）

1. **安装 Visual Studio 2022 Community**
   - 下载地址：https://visualstudio.microsoft.com/zh-hans/downloads/
   - 选择"使用C++的桌面开发"工作负载
   - 确保包含以下组件：
     - MSVC v143编译器工具集
     - Windows 10/11 SDK
     - CMake工具
     - Git for Windows

2. **配置vcpkg包管理器**
   ```cmd
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```

3. **安装依赖包**
   ```cmd
   .\vcpkg install gtest:x64-windows
   .\vcpkg install benchmark:x64-windows
   ```

### 方案二：MinGW-w64

1. **安装MSYS2**
   - 下载地址：https://www.msys2.org/
   - 按照官网指南完成安装

2. **安装开发工具**
   ```bash
   # 在MSYS2终端中执行
   pacman -S mingw-w64-x86_64-gcc
   pacman -S mingw-w64-x86_64-cmake
   pacman -S mingw-w64-x86_64-ninja
   pacman -S mingw-w64-x86_64-gtest
   ```

3. **设置环境变量**
   - 将 `C:\msys64\mingw64\bin` 添加到系统PATH

## 项目构建

### 使用Visual Studio

1. **克隆项目**
   ```cmd
   git clone <repository-url>
   cd plc-runtime-core
   ```

2. **配置CMake**
   ```cmd
   mkdir build
   cd build
   cmake -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
         -DCMAKE_BUILD_TYPE=Release ^
         -DENABLE_TESTING=ON ^
         -DENABLE_BENCHMARKS=ON ^
         ..
   ```

3. **构建项目**
   ```cmd
   cmake --build . --config Release --parallel
   ```

### 使用MinGW

1. **配置CMake**
   ```cmd
   mkdir build
   cd build
   cmake -G "MinGW Makefiles" ^
         -DCMAKE_BUILD_TYPE=Release ^
         -DENABLE_TESTING=ON ^
         -DENABLE_BENCHMARKS=ON ^
         ..
   ```

2. **构建项目**
   ```cmd
   mingw32-make -j%NUMBER_OF_PROCESSORS%
   ```

## 运行测试

### 基准测试

```cmd
# 使用PowerShell脚本（推荐）
powershell -ExecutionPolicy Bypass -File scripts\run-ci-benchmarks.ps1

# 或直接运行
cd build
.\tests\ci\Release\ci-benchmark-gates.exe
```

### 单元测试

```cmd
cd build
ctest -C Release --verbose
```

## 性能优化

### Windows性能设置

1. **电源计划设置**
   ```cmd
   # 设置为高性能模式
   powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
   ```

2. **禁用Windows Defender实时保护**（测试期间）
   - 打开Windows安全中心
   - 转到"病毒和威胁防护"
   - 临时关闭"实时保护"

3. **关闭不必要的后台应用**
   - 打开任务管理器
   - 在"启动"选项卡中禁用不必要的程序

### 进程优先级设置

```powershell
# 在PowerShell中设置高优先级
Get-Process ci-benchmark-gates | Set-Process -Priority High

# 或在任务管理器中手动设置为"高"或"实时"优先级
```

## 开发工具配置

### Visual Studio Code

1. **安装扩展**
   - C/C++ Extension Pack
   - CMake Tools
   - GitLens

2. **配置settings.json**
   ```json
   {
       "cmake.configureArgs": [
           "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
           "-DENABLE_TESTING=ON",
           "-DENABLE_BENCHMARKS=ON"
       ],
       "cmake.buildDirectory": "${workspaceFolder}/build",
       "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"
   }
   ```

### Visual Studio

1. **打开CMake项目**
   - 文件 → 打开 → CMake...
   - 选择项目根目录的CMakeLists.txt

2. **配置CMake设置**
   - 项目 → CMake设置
   - 添加vcpkg工具链文件路径

## 常见问题解决

### 编译错误

1. **找不到头文件**
   ```
   解决方案：确保vcpkg正确安装并集成到Visual Studio
   ```

2. **链接错误**
   ```
   解决方案：检查库文件路径，确保使用正确的架构（x64/x86）
   ```

### 运行时错误

1. **找不到DLL**
   ```
   解决方案：将依赖的DLL复制到可执行文件目录，或设置PATH环境变量
   ```

2. **权限不足**
   ```
   解决方案：以管理员身份运行Visual Studio或命令提示符
   ```

### 性能问题

1. **基准测试结果不稳定**
   ```
   解决方案：
   - 关闭其他应用程序
   - 设置高性能电源计划
   - 使用管理员权限运行
   - 临时禁用杀毒软件
   ```

2. **编译速度慢**
   ```
   解决方案：
   - 使用并行编译 (-j 参数)
   - 启用预编译头
   - 使用SSD存储
   - 增加虚拟内存
   ```

## 调试配置

### Visual Studio调试

1. **设置启动项目**
   - 右键点击目标项目
   - 选择"设为启动项目"

2. **配置调试参数**
   - 项目属性 → 调试
   - 设置命令参数和工作目录

### 性能分析

1. **使用Visual Studio诊断工具**
   - 调试 → 性能探查器
   - 选择"CPU使用率"和"内存使用率"

2. **使用Intel VTune**（可选）
   - 下载Intel VTune Profiler
   - 分析热点函数和性能瓶颈

## 持续集成

### GitHub Actions

项目包含Windows CI配置，会自动：
- 构建项目（Release和Debug）
- 运行所有测试
- 生成基准测试报告
- 检测性能回归

### 本地CI模拟

```cmd
# 模拟CI环境
scripts\run-ci-benchmarks.ps1 -BuildType Release
scripts\run-ci-benchmarks.ps1 -BuildType Debug
```

## 部署准备

### 发布构建

```cmd
cmake --build build --config Release --target install
```

### 打包

```cmd
cd build
cpack -C Release
```

这将生成Windows安装包（MSI或NSIS）。

## 技术支持

如果遇到问题，请：

1. 检查本文档的常见问题部分
2. 查看项目的GitHub Issues
3. 确保使用最新版本的工具和依赖
4. 提供详细的错误信息和环境配置

---

**注意**: 本指南假设使用x64架构。如需x86支持，请相应调整vcpkg和CMake配置。