# Uranus PLC System - 开放式PLC系统

![workflow](https://github.com/lusipad/plcopen/actions/workflows/cmake-multi-platform.yml/badge.svg)

基于IEC 61131-3标准的开源PLC运行时系统和开发环境。

## 项目概述

Uranus PLC System是一个完整的可编程逻辑控制器(PLC)系统，严格遵循IEC 61131-3国际标准。项目基于现有的PLCOpen运动控制框架，旨在提供一个高性能、可扩展、跨平台的工业自动化解决方案。

### 主要特性

- ✅ **符合IEC 61131-3标准** - 支持所有5种编程语言
- ✅ **PLCOpen运动控制** - 完整的运动控制功能块库 
- ✅ **高性能运行时** - 实时任务调度和执行
- ✅ **跨平台支持** - Windows、Linux、嵌入式系统
- ✅ **模块化架构** - 可扩展的功能块库
- 🚧 **工业通信** - Modbus、OPC UA、EtherNet/IP (开发中)
- 🚧 **集成开发环境** - 图形化编程和调试工具 (开发中)

### 支持的编程语言

| 语言 | 类型 | 状态 | 描述 |
|------|------|------|------|
| **ST** (Structured Text) | 文本 | 🚧 开发中 | 结构化文本语言 |
| **IL** (Instruction List) | 文本 | 📋 计划中 | 指令表语言 |
| **LD** (Ladder Diagram) | 图形 | 📋 计划中 | 梯形图语言 |
| **FBD** (Function Block Diagram) | 图形 | 📋 计划中 | 功能块图语言 |
| **SFC** (Sequential Function Chart) | 图形 | 📋 计划中 | 顺序功能图语言 |

## 快速开始

### 环境要求

- **操作系统**: Windows 10+, Ubuntu 18.04+, 或其他Linux发行版
- **编译器**: GCC 7+ 或 MSVC 2019+
- **CMake**: 3.15+

### 编译和安装

```bash
# 克隆仓库
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make -j4

# 运行示例
./axis_move           # 单轴运动示例
./axis_homing         # 回零示例
./test_basic          # 基础功能测试
```

### 基本使用

```cpp
#include "PLCRuntime.h"
#include "StandardFunctionBlocks.h"

int main() {
    // 获取PLC运行时实例
    auto& runtime = Uranus::PLCRuntime::getInstance();
    
    // 注册标准功能块
    Uranus::StandardFunctionBlockFactory::registerAllBlocks(runtime);
    
    // 创建任务
    runtime.createTask("MainTask", Uranus::TaskType::CYCLIC, 1);
    
    // 启动系统
    runtime.start();
    
    // 主循环
    while (true) {
        runtime.runCycle();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
```

## 项目结构

```
plcopen/
├── src/                    # 源代码
│   ├── motion/            # 运动控制模块
│   │   ├── axis/          # 轴控制
│   │   └── interpolation/ # 插补算法
│   ├── fb/                # 功能块库
│   ├── misc/              # 基础工具类
│   ├── plc/               # PLC核心模块 (新增)
│   ├── demo/              # 示例程序
│   └── test/              # 测试程序
├── doc/                   # 文档
│   ├── design/            # 设计文档
│   ├── reference/         # 参考资料
│   └── user_guide/        # 用户指南
├── CMakeLists.txt         # 构建配置
├── plan.md               # 开发计划
└── README.md             # 项目说明
```

## 开发计划

本项目采用分阶段开发模式，详细计划请参考 [plan.md](plan.md)：

### 🎯 第一阶段 (月份 1-3) - 核心运行时系统
- [x] 运行时框架设计
- [x] 标准功能块库框架
- [ ] I/O系统基础
- [ ] 任务调度器
- [ ] 内存管理

### 🎯 第二阶段 (月份 4-7) - 编程语言支持
- [ ] ST语言编译器
- [ ] IL虚拟机
- [ ] POU管理系统

### 🎯 第三阶段 (月份 8-11) - 图形化编程
- [ ] 梯形图编辑器
- [ ] 功能块图编辑器
- [ ] 顺序功能图编辑器

### 🎯 第四阶段 (月份 12-15) - 集成开发环境
- [ ] IDE核心框架
- [ ] 调试和监控功能
- [ ] 诊断工具

### 🎯 第五阶段 (月份 16-19) - 通信和网络
- [ ] Modbus TCP/RTU
- [ ] OPC UA支持
- [ ] 工业以太网协议

### 🎯 第六阶段 (月份 20-24) - 高级功能
- [ ] 冗余和安全功能
- [ ] Web HMI
- [ ] 性能优化

## PLCOpen运动控制功能

基于PLCOpen Motion Control标准（Part 1 & 2），提供完整的运动控制功能块库：

### 单轴管理功能块

| 功能块名称            | 描述                   | 支持情况 |
| :-------------------- | :--------------------- | -------- |
| MC_Power              | 控制轴的电源           | ✅        |
| MC_ReadStatus         | 读取轴的状态           | ✅        |
| MC_ReadAxisError      | 读取轴的错误代码       | ✅        |
| MC_ReadActualPosition | 读取轴的实际坐标       | ✅        |
| MC_ReadActualVelocity | 读取轴的实际速度       | ✅        |
| MC_Reset              | 复位                   | ✅        |
| MC_ReadParameter      | 读取轴的参数值         | 📋        |
| MC_SetPosition        | 设置坐标               | 📋        |
| MC_SetOverride        | 设置倍率               | 📋        |

### 单轴运动功能块

| 功能块名称                | 描述                                 | 支持情况 |
| :------------------------ | :----------------------------------- | -------- |
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