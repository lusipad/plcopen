# Uranus PLC 构建脚本使用说明

## 概述

本项目提供了一个PowerShell构建脚本，用于一键编译和测试Uranus PLC项目：

**`build.ps1`** - 完整功能构建脚本

## 系统要求

- Windows 10/11
- PowerShell 5.1 或更高版本
- Visual Studio 2022 或更高版本
- CMake 3.21 或更高版本

## 使用方法

### 基本构建

```powershell
# 默认Release配置构建
.\build.ps1

# 指定Debug配置构建
.\build.ps1 -Configuration Debug
```

### 清理构建

```powershell
# 清理后重新构建
.\build.ps1 -Clean
```

### 执行测试

```powershell
# 构建并执行测试
.\build.ps1 -Test

# 清理、构建、测试
.\build.ps1 -Clean -Test
```

### 安装到输出目录

```powershell
# 构建并安装
.\build.ps1 -Install

# 完整流程：清理、构建、测试、安装
.\build.ps1 -Clean -Test -Install
```

## 脚本功能

### build.ps1

- ✅ 环境检查（PowerShell版本、CMake、操作系统）
- ✅ 多配置构建（Debug/Release）
- ✅ 自动CMake配置和构建
- ✅ 单元测试执行
- ✅ 自动安装到输出目录
- ✅ 彩色输出和进度显示
- ✅ 错误处理和报告

## 输出目录

- **构建目录**: `build/`
- **输出目录**: `out/`
- **可执行文件**: `build/src/Release/` 或 `build/src/Debug/`

## 生成的文件

构建完成后，会生成以下文件：

- `Uranus.dll` - 主库文件
- `Uranus.lib` - 导入库
- `test_basic.exe` - 基本测试程序
- `axis_move.exe` - 轴运动演示程序
- `axis_homing.exe` - 轴回零演示程序
- `axis_move_oscilloscope.exe` - 轴振荡运动演示程序

## 故障排除

### 常见问题

1. **PowerShell执行策略错误**
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```

2. **CMake未找到**
   - 确保CMake已安装并添加到PATH
   - 或使用完整路径：`C:\Program Files\CMake\bin\cmake.exe`

3. **Visual Studio未找到**
   - 确保已安装Visual Studio 2022
   - 确保安装了C++开发工具

4. **构建失败**
   - 检查是否有编译错误
   - 查看CMake输出日志
   - 尝试清理后重新构建：`.\build.ps1 -Clean`

### 手动构建步骤

如果脚本有问题，可以手动执行以下步骤：

```powershell
# 1. 创建构建目录
mkdir build
cd build

# 2. 配置CMake
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..

# 3. 构建项目
cmake --build . --config Release --parallel

# 4. 运行测试
.\src\Release\test_basic.exe
```

## 开发说明

### 添加新的构建配置

在`build.ps1`中，可以修改`$BuildConfigs`哈希表来添加新的构建配置：

```powershell
$BuildConfigs = @{
    "Debug" = @{
        CMAKE_BUILD_TYPE = "Debug"
        URANUS_ENABLE_ASSERTS = "ON"
        URANUS_ENABLE_LOGGING = "ON"
    }
    "Release" = @{
        CMAKE_BUILD_TYPE = "Release"
        URANUS_ENABLE_ASSERTS = "OFF"
        URANUS_ENABLE_LOGGING = "OFF"
    }
    # 添加新配置...
}
```

### 自定义CMake选项

在`Invoke-CMakeConfigure`函数中，可以添加更多CMake选项：

```powershell
$CMakeVars = @(
    "-G", "Visual Studio 17 2022",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DURANUS_ENABLE_TESTS=ON",
    "-DURANUS_ENABLE_BENCHMARKS=ON",
    # 添加更多选项...
)
```

## 许可证

本项目遵循项目主许可证。

## 贡献

欢迎提交Issue和Pull Request来改进构建脚本。
