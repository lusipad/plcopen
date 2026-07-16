# TwinCAT / CODESYS 能力迁移指南

plcopen 是全新 C++ 运动控制内核，**不是兼容层**。TwinCAT/CODESYS 的源码、
库二进制、工程文件、设备树、任务配置和品牌私有参数都不能直接导入；迁移
单位是“运动能力与周期合同”。

权威范围见
[PLCopen / Beckhoff 核心能力对等矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)。

## 概念映射

| TwinCAT / CODESYS 概念 | plcopen 入口 | 迁移要求 |
|---|---|---|
| AXIS_REF / 轴对象 | `axis::AxisModel` + 宿主 Servo 绑定 | 宿主持有生命周期并逐周期桥接反馈/设定值 |
| NC/PTP 单轴 | `FbMove*`、`FbHalt`、`FbStop` | 参数使用每周期单位；非法组合显式失败 |
| Homing procedures | `core/fb/homing.h` 的 11 个标准 FB | 物理限位、参考信号、编码器和 TorqueLimit 由宿主接入 |
| Gear / Cam / Phasing | `core/fb/sync.h` | 主从轴必须满足 group/owner 前置条件 |
| Axis group | `axis::AxisGroup` | 固定容量非拥有成员集合；先配置再 enable |
| Coordinated motion | `FbMoveLinear*`、`FbMoveCircular*`、`FbMovePath` | 明确选择 ACS/MCS/PCS、buffer 与 transition |
| Kinematic transform | `kin::Kinematics` / `PoseKinematics` | 直接实现插件接口；不导入 TwinCAT 机构文件 |
| Tool / payload | group Tool/Payload/RigidBodyDynamic API | 用标准单位重新录入并由测试验证 TCP 结果 |
| Conveyor / rotary tracking | `FbTrackConveyorBelt` / `FbTrackRotaryTable` | 由宿主逐周期提供动态坐标帧 |
| PLC task scan | 每个 FB 的 `call()` + `AxisModel/AxisGroup::cycle()` | 固定调用顺序和周期；时间以整数周期计数 |
| NC task / RT executor | ADR-0007 committed trajectory ring | 规划线程产帧，RT 线程每拍只消费一帧 |
| EtherCAT / drive task | `adapters::Servo` 窄接口 | 总线、DC、PDO、驱动安全由宿主或独立组件实现 |

## 推荐迁移顺序

1. 用 `find_package(plcopen)` 或 `FetchContent` 接入
   `plcopen::plcopen`，先在 `ServoSim` 上建立相同单位和周期。
2. 为每根轴建立 `AxisModel`，把驱动反馈映射到 snapshot 输入，把 setpoint
   映射到 Servo adapter；不要复制品牌私有轴对象。
3. 按功能族迁移：Power/Reset → PTP/Stop → Homing → Gear/Cam → Group
   lifecycle → coordinated path → coordinates/kinematics/tracking。
4. 把 TwinCAT/CODESYS 的隐式任务推进改成显式 `call()/cycle()` 顺序；
   生产环境使用规划域/RT 域隔离。
5. 为每个机器场景保存输入、状态和 setpoint 黄金回放；接管、断电、限位、
   非法参数至少各有一条验收。
6. 最后接真实硬件，并单独验证单位、极性、限位、编码器、多圈、TorqueLimit、
   通信丢失和安全链路。

## 需要重新设计的地方

- 不保留 `mAxis`、`mExecute` 或旧 C++ 类名包装；使用当前标准入口与字段。
- 不迁移 TwinCAT/CODESYS 工程文件、设备树、Task 配置或在线调试状态。
- 不复制厂商错误码；统一映射到 `rt::ErrorCode`，同时保留宿主诊断详情。
- 不把未支持的 BufferMode、坐标系或数据引用静默改成最近似模式。
- `TIME` 使用整数扫描周期，`REAL` 使用 C++ `double`；所有物理单位由宿主
  统一后再进入内核。
- PLCopen 正式合规、Beckhoff 黑盒性能、Safety 和 EtherCAT 产品能力必须
  分别验证，不能由软件单元测试替代。

## 当前边界

- Part 1：43/43 公共门面，D-01～D-20 已关闭；正式 B/E/V 供应商声明仍未提交。
- Part 4：68/68 同名门面；部分 E/O 字段、power-owner、queued transform、
  moving set-position、非 Cartesian vendor ref 与 buffered dynamic PCS 有明确限制。
- Part 5：11/11 标准 FB，45 B + 102 E 逐项机读声明与软件语义已收口；
  真机堵转、绝对编码器、多圈和硬件时间戳仍由集成商验证。
- 完整逐项状态以
  [能力对等矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)
  和
  [Known Boundaries](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md)
  为准。
