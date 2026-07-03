# PLCopen Motion Control Part 4 v2.0 Linear Motion Matrix

> 状态：`v0.11.0` implemented scope。本文只跟踪 Linear Motion Foundation，不表示完整 Part 4 合规。

## 规范来源

- 官方条目：`PLCopen Motion Control Part 4_version 2.0.pdf`
- 发布状态：Part 4 – Coordinated Motion, Version 2.0, Published, May 2026
- PLCopen 下载页发布日期：2026-06-01
- 官方下载：[PLCopen Part 4 Version 2.0](https://www.plcopen.org/download_file/force/938d04f2-0c6e-44c1-bb24-d36ba0e12e9d/342/)
- 2026-06-21 获取文件 SHA-256：`A0B1E688E7C6DF17BAC1EE907568D6DFA5739B7A9A1AD188F039EA2D709A5947`

规范定位：

- §2.2.2：`CommandAccepted`
- §2.2.3：`CommandID`
- §3.1：coordinate systems
- §4：`MC_POS_REF`
- §5：group state diagram
- §9.34：`MC_MoveLinearAbsolute`
- §9.35：`MC_MoveLinearRelative`
- §11.1：Buffer Modes / Transition Velocity
- §11.2：TransitionMode
- §11.7：OrientationMode

## 状态含义

| 状态 | 含义 |
|---|---|
| `implemented` | 代码、自动化测试和文档证据齐全 |
| `partial` | 已有相关基础，但不满足本里程碑合同 |
| `missing` | 尚无实现 |
| `out-of-scope` | 本里程碑明确拒绝，不计入完成项 |

## 数据类型与公共合同

| 合同 | Part 4 v2.0 要求 | v0.11.0 决策 | 当前状态 | 预计证据 |
|---|---|---|---|---|
| `MC_POS_REF` | vendor-specific axes-group position type；兼容实现可使用 `ARRAY[1..N] OF REAL` | 固定容量 8，携带有效维数，按 group member slot 映射 | implemented | `PLCTypes.h` + type tests |
| `MC_DISTANCE_REF` | Relative FB 图示使用 distance reference；正文表格写 `MC_POS_REF` | 与 `MC_POS_REF` 使用同一存储表示，语义为相对分量 | implemented | `PLCTypes.h` + relative tests |
| `MC_COORD_SYSTEM` | ACS/MCS/WCS/PCS/FCS/TCS | 公开完整枚举；v0.11.0 仅接受 ACS | implemented | `Global.h` + all non-ACS rejection tests |
| `MC_COMMAND_ID` | vendor-specific unique queued command identifier；0 表示尚未接受 | 使用无符号整数，group 内单调分配，0 保留 | implemented | buffered/aborting lifecycle tests |
| `MC_TRANSITION_VELOCITY` | 与 BufferMode 独立，包含 Zero/Low/Previous/Next/High | 公开枚举；仅接受 `mcTVZero` | implemented | enum + all nonzero rejection tests |
| `MC_TRANSITION_MODE` | None/StartVelocity/ConstantVelocity/CornerDistance/MaxCornerDeviation | 复用现有枚举；仅接受 `NONE` | implemented | validation tests |
| `MC_TRANSITION_PARAMETER` | transition mode 的附加参数 | v0.11.0 只接受全零参数 | implemented | type + nonzero rejection tests |
| `MC_ORIENTATION_MODE` | Linear/JointInterpolated/Fixed/PathBased | 公开枚举；ACS 线性子集只接受 `mcLinear` | implemented | enum + all non-linear rejection tests |

## 功能块矩阵

| 功能块 | 必需输入 | 必需输出 | v0.11.0 支持边界 | 当前状态 |
|---|---|---|---|---|
| `MC_MoveLinearAbsolute` | AxesGroup, Execute, Position, Velocity, Acceleration, Deceleration, Jerk, CoordSystem, BufferMode, TransitionVelocity, TransitionMode, TransitionParameter, OrientationMode | Done, Busy, Active, CommandAccepted, CommandAborted, Error, ErrorID, CommandID | ACS、2-8 轴、absolute dynamics、Aborting/Buffered、零过渡（`KB-012`） | implemented |
| `MC_MoveLinearRelative` | AxesGroup, Execute, Distance, Velocity, Acceleration, Deceleration, Jerk, CoordSystem, BufferMode, TransitionVelocity, TransitionMode, TransitionParameter, OrientationMode | Done, Busy, Active, CommandAccepted, CommandAborted, Error, ErrorID, CommandID | ACS、2-8 轴、absolute dynamics、Aborting/Buffered、零过渡（`KB-012`） | implemented |

## 跨切语义矩阵

| 语义 | Part 4 v2.0 要求 | v0.11.0 验收 | 当前状态 |
|---|---|---|---|
| 共享路径 | coordinated path 使用一个路径速度；Linear FB 产生插补直线 | 所有成员由同一个 scalar path sample 映射，逐周期共线 | implemented |
| group/axis state | group command 为 GroupMoving；成员轴为 SynchronizedMotion | active command 全周期状态一致，完成后回 GroupStandby/Standstill | implemented |
| command acceptance | 命令进入 buffer 后置 `CommandAccepted`；Busy 结束或新 Execute 时清除 | 本地原子提交当周期置位并分配非零 CommandID | implemented |
| Aborting | 立即中止当前 motion 并启动新命令 | 原命令 `CommandAborted`，其后 queued commands 一并中止 | implemented |
| Buffered | 前一 motion Done 后启动 | FIFO，前一命令 error/abort 时后继按规范传播 | implemented |
| 原子提交 | 规范以 axes-group command 为整体 | 任一参数/成员/限制失败时不产生部分运动或队列残片 | implemented |
| 成员限制 | path dynamics 必须保持成员可执行 | 提交前计算共同约束；严格成员限制约束整组 | implemented |
| group stop | `MC_GroupStop` 沿原路径受控减速；速度为零后置 `Done`，且 Execute 为 TRUE 时保持 GroupStopping | 支持 Deceleration/Jerk；Execute 下降且已停止后回 GroupStandby；GroupDisable 或成员掉电会置 `CommandAborted` 并禁用 group | implemented |
| 错误传播 | 任一轴 ErrorStop 使 group 进入 GroupErrorStop | 其他成员不得继续完成原路径；reset 后才恢复 | implemented |
| 非 ACS | 规范支持多个 coordinate systems | MCS/WCS/PCS/FCS/TCS 返回明确 unsupported error（`KB-012`） | out-of-scope |
| transition geometry | 规范定义多种 TransitionVelocity/Mode | 非零 transition 与 blending 显式拒绝（`KB-012`） | out-of-scope |
| orientation interpolation | 规范定义四种 orientation mode | 非 `mcLinear` 显式拒绝（`KB-012`） | out-of-scope |

## 必需测试证据

- 2/3/8 轴 absolute 与 relative：正、负、混合方向及静止分量。
- 每周期共线性、同周期启动、完成偏差不超过一个 tick、终点容差。
- 零距离、维数不匹配、NaN/Inf、非正 dynamics、disabled/unpowered/busy/error member。
- 跨 scheduler 成员拒绝与 validate-then-commit 原子性。
- Aborting、Buffered、CommandAccepted、CommandID、CommandAborted、同一 FB 在 CommandAccepted 后复用和 falling-edge 清理。
- GroupStop 原路径减速、Execute 保持/提前下降、Disable/掉电中止、运行中成员 error、GroupErrorStop/reset。
- 非 ACS、非零 transition、非默认 orientation 的明确拒绝。
- Windows/Linux、coverage、find_package、FetchContent 与 demo smoke。

## 完成计数

当前里程碑跟踪：

| 类别 | Total | Implemented | Partial | Missing | Out of scope |
|---|---:|---:|---:|---:|---:|
| 数据类型与公共合同 | 8 | 8 | 0 | 0 | 0 |
| 功能块 | 2 | 2 | 0 | 0 | 0 |
| 跨切语义 | 12 | 9 | 0 | 0 | 3 |
| **总计** | **22** | **19** | **0** | **0** | **3** |

`v0.11.0` 完成门槛：除明确的 `out-of-scope` 行外，所有行必须变为 `implemented`，并链接到代码与自动化测试证据。
