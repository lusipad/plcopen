# Uranus PLC 构建脚本使用说明

## 概述

本项目提供了一个 PowerShell 构建脚本，用于一键编译和测试 Uranus PLC 项目：

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

- ✅ 环境检查（PowerShell 版本、CMake、操作系统）
- ✅ 多配置构建（Debug/Release）
- ✅ 自动 CMake 配置和构建
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

1. **PowerShell 执行策略错误**
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```

2. **CMake 未找到**
   - 确保 CMake 已安装并添加到 PATH
   - 或使用完整路径：`C:\Program Files\CMake\bin\cmake.exe`

3. **Visual Studio 未找到**
   - 确保已安装 Visual Studio 2022
   - 确保安装了 C++ 开发工具

4. **构建失败**
   - 检查是否有编译错误
   - 查看 CMake 输出日志
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

### 自定义 CMake 选项

在`Invoke-CMakeConfigure`函数中，可以添加更多 CMake 选项：

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

欢迎提交 Issue 和 Pull Request 来改进构建脚本。
| MC_MoveAbsolute           | 将轴移动到绝对位置                   | ✅        |
| MC_MoveRelative           | 将轴从当前位置移动相对距离           | ✅        |
| MC_MoveAdditive           | 向轴的当前运动添加一个偏移量         | ✅        |
| MC_MoveVelocity           | 启动连续运动                         | ✅        |
| MC_Stop                   | 停止轴的运动                         | ✅        |
| MC_Halt                   | 立即停止轴的运动                     | ✅        |
| MC_Home                   | 回零                                 | 🚧        |
| MC_MoveSuperimposed       | 在轴的当前运动上叠加一个额外的运动   | 📋        |
| MC_TorqueControl          | 控制轴的扭矩                         | 📋        |

### 多轴运动功能块

| 功能块名称         | 描述               | 支持情况 |
| :----------------- | :----------------- | -------- |
| MC_CamTableSelect  | 选择一个凸轮表     | ✅        |
| MC_CamIn           | 启动凸轮输入       | 📋        |
| MC_CamOut          | 停止凸轮输入       | 📋        |
| MC_GearIn          | 启动齿轮输入       | 📋        |
| MC_GearOut         | 停止齿轮输入       | 📋        |

## IEC 61131-3标准功能块

系统提供完整的IEC 61131-3标准功能块库：

### 定时器
- **TON** - 通电延时定时器
- **TOF** - 断电延时定时器  
- **TP** - 脉冲定时器

### 计数器
- **CTU** - 增计数器
- **CTD** - 减计数器
- **CTUD** - 增减计数器

### 双稳态
- **SR** - 置位复位锁存器
- **RS** - 复位置位锁存器

### 边沿检测
- **R_TRIG** - 上升沿触发
- **F_TRIG** - 下降沿触发

### 数学运算
- **ADD/SUB/MUL/DIV** - 基本数学运算
- **GT/GE/EQ/LE/LT/NE** - 比较运算

## 技术特点

### 当前实现
- ✅ **运动规划**: 支持加速度/减速度运动规划（加速度直线型）
- ✅ **实时调度**: 基于优先级的任务调度
- ✅ **内核兼容**: 支持用户态和内核态部署
- ✅ **零依赖**: 无外部依赖的自包含设计

### 限制和改进计划
- ❌ **Jerk运动规划**: 计划在v0.2版本添加
- ❌ **ContinuousUpdate**: 计划在v0.3版本添加
- ❌ **图形化编程**: 计划在第三阶段实现

## 版本规划

### v0.1 - 项目工程化整理 ✅
- ✅ CMake跨平台支持
- ✅ 代码格式化调整
- 🚧 文档补充
- 🚧 增加测试覆盖

### v0.2 - PLC核心功能 (当前开发)
- 🚧 PLC运行时系统
- 🚧 标准功能块库
- 🚧 ST语言支持
- 🚧 调整状态机实现

### v0.3 - 扩展功能
- 📋 Jog/Inc功能块
- 📋 Home功能块改进
- 📋 Jerk运动规划
- 📋 多轴协调运动

### v0.4 - 图形化编程
- 📋 梯形图编辑器
- 📋 功能块图编辑器
- 📋 多语言支持

### v0.5 - 集成开发环境
- 📋 完整IDE
- 📋 调试功能
- 📋 GUI界面

## 贡献指南

我们欢迎所有形式的贡献！

### 如何贡献

1. **Fork** 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开 **Pull Request**

### 开发规范

- 遵循现有的代码风格
- 添加适当的单元测试
- 更新相关文档
- 确保CI/CD流水线通过

### 报告问题

使用 [GitHub Issues](https://github.com/lusipad/plcopen/issues) 报告：
- 🐛 Bug报告
- 💡 功能请求  
- 📖 文档改进
- ❓ 使用问题

## 项目历史

这个项目最初fork自i5cnc，但原仓库已处于无人维护状态。我们在原有PLCOpen运动控制功能的基础上，扩展为完整的PLC系统，旨在为工业自动化提供开源解决方案。

## 许可证

本项目采用 Apache License 2.0 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 参考资料

1. [IEC 61131-3 国际标准](https://webstore.iec.ch/publication/4552)
2. [PLCOpen Motion Control 资料](https://plcopen.org/technical-activities/motion-control) (仓库doc目录下有Part 1&2资料)
3. [PLCOpen AI知识库](https://chatglm.cn/agentShare?id=66c8b6c8b3232fbf83b14ecb) - 通过AI交互学习PLCOpen标准

## 联系我们

- **项目主页**: [GitHub](https://github.com/lusipad/plcopen)
- **文档**: [在线文档](https://lusipad.github.io/plcopen)  
- **讨论区**: [GitHub Discussions](https://github.com/lusipad/plcopen/discussions)

## 致谢

感谢以下项目和标准的启发：
- IEC 61131-3 International Standard
- PLCOpen组织的技术规范
- i5cnc原始项目
- 工业自动化开源社区

---

**注意**: 本项目目前处于早期开发阶段，API可能会发生变化。生产环境使用请谨慎评估。