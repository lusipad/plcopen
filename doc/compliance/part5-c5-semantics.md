# Part 5 C5 标准合同收口语义矩阵

> 状态：**已批准并实现**（2026-07-17）。维护者明确本项目是全新软件，
> 不保留历史 API 兼容层。依据 PLCopen Motion Control Part 5 v2.0
> §3.1～3.11 与
> 附录 5.2～5.14；差距基线见 `plcopen-part5-part6-audit.md`。

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| C5 出口 | 11/11 FB 均提供规范名称、派生类型、逐 I/O 支持状态和可软件验证语义；未支持 E 级项必须显式拒绝或在声明表标为 No |
| 公共面 | 只保留规范名称；旧缩写类名与旧字段直接删除，本项目不承担历史 API 兼容 |
| 生命周期 | Execute 型终态遵循 C4：提前下降不丢失已接受命令的 Done/CommandAborted/Error；排队期间 Busy=TRUE、Active=FALSE |
| 单写者 | 主动步骤只经 `AxisModel` 命令队列；被动 Flying 只观察并平移坐标，不替换活动运动 |
| RT | call/cycle 路径固定容量、无堆分配、无锁、无异常、无系统调用 |
| 诚实边界 | 真机械堵转安全、编码器时间戳/多圈真实性、PLCopen 审批和 Logo 不由软件模拟证明 |

## 2. 决策点

| # | 决策 | 已批准语义 | 理由 |
|---|---|---|---|
| C5-01 | 规范名称 | 公共入口仅为 `FbStepAbsoluteSwitch`、`FbStepLimitSwitch`、`FbStepBlock`、`FbStepReferencePulse`、`FbStepDistanceCoded`、`FbHomeDirect`、`FbHomeAbsolute`、`FbFinishHoming`、两项 Flying 与 `FbAbortPassiveHoming` | 全新软件无需维护历史 API，单一标准面更可审计 |
| C5-02 | 派生类型 | `HomeDirection` 覆盖 positive/negative/switch-positive/switch-negative；`SwitchMode` 覆盖六模式；新增固定通道 `ReferenceSignalRef`；统一使用 `BufferMode` | 对齐附录 5.2，裸整数不进入 C++ 核 |
| C5-03 | 搜索方向 | fixed direction 忽略初始电平；switch-positive/switch-negative 在 Execute 上升沿按初始电平选择首方向；LimitSwitch 初态已满足时先反向脱离，再按原方向回找 | 对齐 §3.1/3.2 |
| C5-04 | 开关条件 | on/off 为电平条件；rising/falling 为真实后续边沿；edge-positive/negative 按实际运动方向选择固定物理边沿 | 对齐 §3.1/3.9 |
| C5-05 | SetPosition | C++ 以 `set_position_enabled` 表示规范图中的“已连接/未连接”；未连接不改坐标，已连接在条件满足并停稳后置位 | 避免默认值 0 被误当成强制置零 |
| C5-06 | TorqueLimit | 主动搜索命令把非负 TorqueLimit 作为 servo setpoint 上限透传；0 表示不限制。软件测试验证透传与独立 actual torque，不宣称驱动已执行保护 | 把控制责任交给 adapter/drive，核不伪造力矩闭环 |
| C5-07 | BufferMode | 主动运动步骤支持 Aborting/Buffered；排队时不提前 arm probe 或开始计时。无运动 HomeDirect/HomeAbsolute/Flying 只支持 Aborting，其他模式显式 `unsupported` | 无运动操作没有可诚实排队的轴命令对象 |
| C5-08 | HomeDirect | 无运动设位成功后独立置 homed 并恢复软限位监督，不再要求 FinishHoming 二次收口 | 对齐 §3.6 的 finalizing FB 身份 |
| C5-09 | HomeAbsolute | 快照独立绝对位置源，无运动设位并独立置 homed；源协议与多圈由宿主负责 | 对齐 §3.7 且保持硬件边界 |
| C5-10 | FinishHoming | `Distance` 是相对位移；0 只完成 homed，非 0 提交 `move_relative`，动力学与 BufferMode 逐项校验 | 修正旧 absolute ParkPosition 偏差 |
| C5-11 | 逐 I/O 声明 | 从 Part 5 原文表生成 11 FB 支持矩阵：B 项全部 Yes；E 项按实现标 Yes/No，并为 No 写明替代/边界 | 形成可审签材料，但不代替人工提交 |
| C5-12 | AbsoluteSwitch 限位恢复 | 搜索中任一物理限位触发时反向越过绝对开关，再按原方向重新搜索；时间/距离限值覆盖恢复段 | 对齐 §3.1 的错误方向恢复合同 |
| C5-13 | Flying 目标语义 | 在线重标定只改变当前坐标；活动与排队绝对目标的数值保持不变，活动剖面从新坐标连续重规划 | 对齐 §3.9/3.10 的 absolute targets 不变 |
| C5-14 | 静态/收口接管 | HomeAbsolute 的 Aborting 可接管活动单轴运动；FinishHoming 的零 Distance 可终止活动 Homing 并转 Standstill | 对齐静态回零与 Finish 的上升沿身份 |

## 3. 退化与拒绝规则

| 形态 | 语义 |
|---|---|
| 派生枚举超域、ReferenceSignal 通道越界 | `invalid_argument`，不提交命令 |
| 主动步骤使用 blending BufferMode | `unsupported`，现有运动/队列不变 |
| 被动或静态回零使用非 Aborting | `unsupported`，不创建 owner、不改坐标 |
| Buffered 步骤尚未成为 active | Busy=TRUE、Active=FALSE，不 arm probe、不累计时间/距离 |
| TorqueLimit 非有限或小于 0 | `invalid_argument` |
| torque limit 无 adapter/drive 承载 | 软件仅证明 setpoint 透传；供应商声明备注“集成方必须映射并验证” |
| switch-positive/switch-negative 用于不接受该模式的 FB | `invalid_argument` |
| FinishHoming 的 Distance/动力学非法 | 原子拒绝，不先置 homed |
| HomeDirect/HomeAbsolute 前置条件失败 | 坐标、homed、软限位状态不变 |

## 4. 验收指标

| # | 指标 | 门槛 |
|---|---|---|
| C5-T01 | 11 个规范类名与 Part 5 派生类型编译锚点 | 全部通过；生产代码内无旧三缩写类名引用 |
| C5-T02 | 四种 HomeDirection、六种 SwitchMode | 正常、初态、反向脱离、非法组合逐项覆盖 |
| C5-T03 | Aborting/Buffered | 主动三类搜索步骤各覆盖排队 Busy/Active、激活后捕获和前序接管 |
| C5-T04 | TorqueLimit | setpoint 透传、0 不限制、负值/NaN 原子拒绝 |
| C5-T05 | HomeDirect/HomeAbsolute | 零运动、置位、homed、软限位恢复、非 Aborting 拒绝 |
| C5-T06 | FinishHoming | Distance 正/负/零、相对目标、Buffered、非法动力学原子拒绝 |
| C5-T07 | 逐 I/O 支持表 | 11/11、45 B + 102 E 均有 Yes/No 与证据/边界，无空白 |
| C5-T08 | 质量门 | Windows/Linux GCC/Clang/ARM64、clang-tidy、RT scan、回放、覆盖率全绿 |

## 5. 不做

| 项 | 边界 |
|---|---|
| 真机械堵转安全证明 | 需要真实机构、驱动 torque limit 和风险评估 |
| 绝对编码器协议与掉电多圈 | adapter/宿主职责 |
| 硬件时间戳/差分捕获精度声明 | `ReferenceSignalRef` 首批只承载固定数字通道 |
| PLCopen 提交、签署、Logo | 人专属动作；仓库只生成未签署模板 |
| Part 6 液压 FB | VISION 未解锁，保持明确不支持 |
