# Part 4 全量 FB 对照表（P 系列收口文档）

> 🔴 **本表已失效（2026-07-12 原文审计）**：本表跟踪的 30 个 FB 是
> **自编清单**，而 Part 4 **v2.0 规格实际定义 68 个 FB**——本表漏了
> 38 个，并在残缺清单上错误宣布了"零留白"。
>
> **当前同名门面：68/68，名称面齐全但条款不合规**；C6 已删除两个旧名
> Position 回读包装，统一使用 v2 `MC_GroupReadPosition(Source)`。完整清单见
> **[plcopen-part4-clause-audit.md](plcopen-part4-clause-audit.md)**。
> 本表保留为历史记录（其对 30 个 FB 的实现映射仍准确），**不得再
> 用于任何覆盖率声明**；重建工作见 P-Part4b 批次。
>
> 原终点标准（仍有效，但基准换为规格原文）：每个 Part 4 标准 FB 要么
> 承接、要么显式声明不做——零留白。
> 最后更新：2026-07-17（C6/KB-081 移除旧回读包装；30 项分母仍失效）。

## 对照表

### 组管理 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 1 | MC_AddAxisToGroup | **已承接** | `FbAddAxisToGroup` (`core/fb/group.h`) | |
| 2 | MC_RemoveAxisFromGroup | **已承接** | `FbRemoveAxisFromGroup` (`core/fb/group.h`) | |
| 3 | MC_UngroupAllAxes | **已承接** | `FbUngroupAllAxes` (`core/fb/group.h`) | Disabled/Standby/ErrorStop 原子解绑 |
| 4 | MC_GroupEnable | **已承接** | `FbGroupEnable` (`core/fb/motion.h`) | |
| 5 | MC_GroupDisable | **已承接** | `FbGroupDisable` (`core/fb/motion.h`) | |
| 6 | MC_GroupReset | **已承接** | `FbGroupReset` (`core/fb/group.h`) | |
| 7 | MC_GroupReadStatus | **已承接** | `FbGroupReadStatus` (`core/fb/group.h`) | |
| 8 | MC_GroupReadActualPosition | **由 v2 入口承接** | `FbGroupReadPosition(Source=actual)` (`core/fb/group.h`) | ACS/MCS/PCS 三坐标系 |
| 9 | MC_GroupReadCommandPosition | **由 v2 入口承接** | `FbGroupReadPosition(Source=command)` (`core/fb/group.h`) | 同上 |
| 10 | MC_GroupReadError | **已承接** | `FbGroupReadError` (`core/fb/group.h`) | 独立回读组 ErrorStop 锁存；记录细分未承载 |
| 11 | MC_GroupStop | **已承接** | `FbGroupStop` (`core/fb/motion.h`) | |
| 12 | MC_GroupHome | **已承接** | `FbGroupHome` (`core/fb/management.h`) | 并行回零全成员 |

### 组控制 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 13 | MC_GroupSetOverride | **已承接** | `FbGroupSetOverride` (`core/fb/management.h`) | VelFactor ∈ [0,1]，factor=0 驻留 |
| 14 | MC_GroupInterrupt | **已承接** | `FbGroupInterrupt` (`core/fb/management.h`) | 保留暂停点 |
| 15 | MC_GroupContinue | **已承接** | `FbGroupContinue` (`core/fb/management.h`) | 从暂停点重启 |
| 16 | MC_GroupHalt | **已承接** | `FbGroupHalt` (`core/fb/management.h`) | 原路径受控到零回 standby，新 Aborting 运动可接管 |

### 协调运动 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 17 | MC_MoveLinearAbsolute | **已承接** | `FbMoveLinearAbsolute` (`core/fb/motion.h`) | CoordSystem 输入 |
| 18 | MC_MoveLinearRelative | **已承接** | `FbMoveLinearRelative` (`core/fb/motion.h`) | |
| 19 | MC_MoveCircularAbsolute | **已承接** | `FbMoveCircularAbsolute` (`core/fb/motion.h`) | BORDER 模式 |
| 20 | MC_MoveCircularRelative | **已承接** | `FbMoveCircularRelative` (`core/fb/motion.h`) | |
| 21 | MC_MoveDirectAbsolute | **已承接** | `FbMoveDirectAbsolute` (`core/fb/management.h`) | 非协调 PTP |
| 22 | MC_MoveDirectRelative | **已承接** | `FbMoveDirectRelative` (`core/fb/management.h`) | |

### 路径 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 23 | MC_PathSelect | **已承接** | `FbPathSelect` (`core/fb/path_table.h`) | 校验 + 句柄 |
| 24 | MC_MovePath | **已承接** | `FbMovePath` (`core/fb/path_table.h`) | 窗口机器逐点提交 |

### 变换 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 25 | MC_SetKinTransform | **已承接** | `FbSetKinTransform` (`core/fb/path_table.h`) | 运动学插件安装 |
| 26 | MC_ReadKinTransform | **已承接** | `FbReadKinTransform` (`core/fb/path_table.h`) | vendor ref 适配为插件非拥有引用 |
| 27 | MC_SetCartesianTransform | **已承接** | `FbSetCartesianTransform` (`core/fb/path_table.h`) | 固定 6D RPY immediate 子集 |
| 28 | MC_ReadCartesianTransform | **已承接** | `FbReadCartesianTransform` (`core/fb/path_table.h`) | 帧回读 |

### 跟踪 FB

| # | 标准 FB | 状态 | 实现 | 说明 |
|---|---------|------|------|------|
| 29 | MC_TrackConveyorBelt | **已承接** | `FbTrackConveyorBelt` (`core/fb/tracking.h`) | 动态 PCS 与持续保持 |
| 30 | MC_TrackRotaryTable | **已承接** | `FbTrackRotaryTable` (`core/fb/tracking.h`) | 绕转台 Z 轴动态 PCS |

## 汇总

| 状态 | 数量 | 占比（**基于失效的 30 项自编清单**） |
|------|------|------|
| 已承接 | 23 | 77% |
| 已覆盖（合并入其他 FB） | 1 | 3% |
| v2 计划（底层已备或可组合达到） | 4 | 13% |
| Y1 计划（显式远期） | 2 | 7% |
| ~~留白~~ | ~~0~~ | ~~0%~~ |

🔴 **上表分母错误**。以 Part 4 v2.0 原文为准的当前真实账：
**规格 68 个 FB，68 个有同名门面；接口和语义仍逐项部分覆盖**——
见 [plcopen-part4-clause-audit.md](plcopen-part4-clause-audit.md)。

## 已知实现边界（与对照表交叉引用）

- 线性运动：ACS/MCS/PCS 支持；WCS/FCS/TCS 显式不支持（KB-012/036）
- 圆弧运动：BORDER 模式；CENTER/RADIUS 显式不支持（KB-030），Y5 计划
- Blending：MaxCornerDeviation + 前瞻窗口（KB-031/032/039）
- 运动学：龙门/SCARA/SphericalWrist6R 参考实现（KB-037/044）
- 组接管：linear 接管速度连续（KB-051）与加速度连续（KB-052）已修复；circular/笛卡尔扩展另批
- Override：AccFactor 显式 v2（part4-management-semantics §不做）
