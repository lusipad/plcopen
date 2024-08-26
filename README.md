# PLCOpen Libray
![workflow](https://github.com/lusipad/plcopen/actions/workflows/cmake-multi-platform.yml/badge.svg)

一个 C++ 库，实现了 PLCopen 运动控制标准（part 1 & 2）中的功能块。



## 概述

PLCOpen  运动控制标准定义了一套用于运动控制的标准函数库，旨在提供独立于底层架构的标准命令集和结构。

PLCOpen 运动控制包括以下几个部分：

1. **Part 1 - PLCopen Function Blocks for Motion Control**：提供了运动控制的基础函数块。
2. **Part 2 - PLCopen Motion Control - Extensions**：在 2.0 版本中与 Part 1 合并，提供额外的功能。
3. **Part 3 - PLCopen Motion Control - User Guidelines**：用户指南和示例。
4. **Part 4 - PLCopen Motion Control - Coordinated Motion**：协调运动控制。
5. **Part 5 - PLCopen Motion Control - Homing Procedures**：回原点程序。
6. **Part 6 - PLCopen Motion Control - Fluid Power Extensions**：流体动力扩展

本项目主要实现了 Part 1 & 2 部分，提供一个基础的、完整的 PLC 运动控制基础库，用户可以在此基础上，实现更加复杂的功能。

本项目不提供梯形图编辑、ST 语言解析等能力，仅提供 C++ 接口的调用方式。

这个仓库 fork 自 `i5cnc`，但是之前问过沈阳机床的人，他们也不知道自己还有这么一个开源的仓库（黑人问号），整个仓库也处于一个无人维护的状态。所以我 fork 了一份，闲暇之余按照自己的想法调整调整。

## 快速开始

### 编译

``` bash
mkdir build
cd build
cmake ..
make
```

支持 windows / linux / macOs 下使用。（windows kernel 开发中）

### 示例

在 demo 中提供了简单的回原点、单轴运动的示例。

## 功能说明

### 单轴管理功能块

| 功能块名称            | 描述                   | 支持情况 |
| :-------------------- | :--------------------- | -------- |
| MC_Power              | 控制轴的电源。         | O        |
| MC_ReadStatus         | 读取轴的状态。         | O        |
| MC_ReadAxisError      | 读取轴的错误代码。     | O        |
| MC_ReadParameter      | 读取轴的参数值。       |          |
| MC_ReadBoolParameter  | 读取轴的布尔型参数值。 |          |
| MC_ReadDigitalInput   | 读取轴的数字型输入。   |          |
| MC_ReadDigitalOutput  | 读取轴的数字型输出。   |          |
| MC_ReadActualPosition | 读取轴的实际坐标。     |          |
| MC_ReadActualVelocity | 读取轴的实际速度。     |          |
| MC_ReadActualTorque   | 读取轴的实际扭矩。     |          |
| MC_ReadAxisInfo       | 读取轴的信息。         |          |
| MC_ReadMotionState    | 读取运动状态。         |          |
| MC_SetPosition        | 设置坐标。             |          |
| MC_SetOverride        | 设置倍率。             |          |
| MC_TouchProbe         | 设置扭矩。             |          |
| MC_DigitalCamSwitch   | 设置电子凸轮。         |          |
| MC_Reset              | 复位。                 |          |
| MC_AbortTrigger       | 终止触发器。           |          |
| MC_HaltSuperimposed   | 停止额外运动。         |          |

### 多轴管理功能块

| 功能块名称        | 描述             | 支持情况 |
| :---------------- | :--------------- | -------- |
| MC_CamTableSelect | 选择一个凸轮表。 | O        |

### 单轴运动功能块

| 功能块名称                | 描述                                 | 支持情况 |
| :------------------------ | :----------------------------------- | -------- |
| MC_Home                   | 回零。                               |          |
| MC_Stop                   | 停止轴的运动。                       |          |
| MC_Halt                   | 立即停止轴的运动。                   | O        |
| MC_MoveAbsolute           | 将轴移动到绝对位置。                 | O        |
| MC_MoveRelative           | 将轴从当前位置移动相对距离。         | O        |
| MC_MoveAdditive           | 向轴的当前运动添加一个偏移量。       | O        |
| MC_MoveSuperimposed       | 在轴的当前运动上叠加一个额外的运动。 |          |
| MC_MoveVelocity           | 启动连续运动。                       | O        |
| MC_MoveContinuousAbsolute | 启动连续运动且以相对值终止。         |          |
| MC_MoveContinuousRelative | 启动连续运动且以绝对值终止。         |          |
| MC_TorqueControl          | 控制轴的扭矩。                       |          |
| MC_PositionProfile        | 设置轴的位置轮廓。                   |          |
| MC_VelocityProfile        | 设置轴的速度轮廓。                   |          |
| MC_AccelerationProfile    | 设置轴的加速度轮廓。                 |          |

### 多轴运动功能块

| 功能块名称         | 描述               | 支持情况 |
| :----------------- | :----------------- | -------- |
| MC_CamIn           | 启动凸轮输入。     |          |
| MC_CamOut          | 停止凸轮输入。     |          |
| MC_GearIn          | 启动齿轮输入。     |          |
| MC_GearOut         | 停止齿轮输入。     |          |
| MC_GearInPos       | 设置齿轮输入位置。 |          |
| MC_PhasingAbsolute | 设置绝对相位。     |          |
| MC_PhasingRelative | 设置相对相位。     |          |
| MC_CombineAxis     | 结合轴。           |          |

## 其他

1. 目前算法仅支持了加速度减速度运动规划 (加速度直线型)，不支持 Jerk 运动规划。

## 参考资料

1. PLCOpen Motion Control 相关资料，可以在 [PLCOpen 官网](https://plcopen.org/technical-activities/motion-control) 下载。
2. 由于 PLCOpen 的资料都是英文的，所以丢到 AI 智能体做了一个[知识库](https://chatglm.cn/agentShare?id=66c8b6c8b3232fbf83b14ecb)，可以通过大模型进行交互。