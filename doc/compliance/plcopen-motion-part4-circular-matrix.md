# PLCopen Motion Control Part 4 圆弧运动矩阵

> 状态：**已批准（2026-07-05，维护者）；v1 与 14-pin 原生扩展已实现**。本文件是 Phase A3
> （`MC_MoveCircularAbsolute` / `MC_MoveCircularRelative`）的验收规格
> （normative），实现与验收测试以本矩阵为准。验收证据：
> `plcopen_core_a3_circular_tests` + 回放场景 `core-group-circular`。

## 规范来源

- 官方条目：`PLCopen Motion Control Part 4_version 2.0.pdf`（同 linear 矩阵，
  SHA-256 见 [plcopen-motion-part4-linear-matrix.md](plcopen-motion-part4-linear-matrix.md)）
- 圆弧 FB 与 `MC_CIRC_MODE` / `MC_CIRC_PATHCHOICE` 的具体条目号**待人工对照 PDF 核实后填入**
  （本草案不杜撰条目号）。

## 提案范围（v1 基线）

对齐新核 L2 已有能力（三点平面圆弧 + 线性第三轴插值）与 A3 的 DoD
（退化几何显式错误语义），提案第一版边界：

| 决策点 | 提案 | 理由 |
|---|---|---|
| 圆弧定义模式 | 仅 `mcBorder`（起点-辅助点-终点三点过弧）；`mcCenter` / `mcRadius` 显式 `unsupported` | L2 `make_arc` 即三点构造；CENTER 的圆心一致性校验与 RADIUS 的双解选择留到 v2 |
| 坐标系 | 仅 ACS（沿用 `KB-012` 口径），前两轴张成圆弧平面，其余轴线性跟随路径参数 | 与 linear 基线一致；MCS/PCS 属 Phase B1 |
| 轴数 | 2-8 轴（≥2；第 3 轴起线性跟随） | 与 linear 的组语义一致 |
| BufferMode | Aborting / Buffered；tangent line→arc 支持 `blending_low/high` | 非切向连接显式降级并可查询 |
| Transition | `mcTMNone` 或 blending 下 `mcTMMaxCornerDeviation`；`TransitionVelocity=0` 自动，正值限制连接结点速度 | 几何上界与速度上界独立进入窗口规划器 |
| Orientation | `joint_space`、`shortest_path`、`constant` | 后两者要求 6D pose 插件及 MCS/PCS；分别走最短测地和保持段起点 TCP 姿态 |
| Tolerance | `0` 关闭额外验收；正有限值检查 aux 在圆弧参数上的高维线性跟随残差 | 独立几何输入，不复用 `TransitionParameter` |
| PathChoice | `mcCW` / `mcCCW` 输入公开；BORDER 模式下由三点唯一确定弧向，与输入不一致时显式报错 | 未定义组合不静默猜测 |

## 退化几何显式语义（A3 DoD / T8）

| 输入形态 | 语义 | 错误码 |
|---|---|---|
| 三点共线（含辅助点在弦上/外延） | 拒绝，不退化为直线 | `invalid_argument` |
| 任意两点重合（零弧长/零半径） | 拒绝 | `invalid_argument` |
| 三点几乎共线（数值病态，曲率半径超过阈值 R_max = 弦长 × 1e6 提案值） | 拒绝，阈值写入验收测试 | `invalid_argument` |
| 非有限坐标 / 维数不匹配 | 拒绝 | `invalid_argument` |
| 整圆（起点=终点且辅助点有效） | v1 拒绝（终点重合视为零弧长）；整圆/多圈留 v2 | `invalid_argument` |

## 功能块矩阵（提案）

| 功能块 | 必需输入 | 必需输出 | v1 支持边界 |
|---|---|---|---|
| `MC_MoveCircularAbsolute` | AxesGroup, Execute, CircMode, AuxPoint, EndPoint, PathChoice, Velocity, Acceleration, Deceleration, Jerk, CoordSystem, BufferMode, Tolerance, Transition*, OrientationMode | 与 linear FB 相同合同（Done/Busy/Active/CommandAccepted/CommandAborted/Error/ErrorID/CommandID） | BORDER；joint-space 2-8 轴；pose 模式 6 轴 MCS/PCS；显式组合矩阵 |
| `MC_MoveCircularRelative` | 同上，AuxPoint/EndPoint 为相对分量 | 同上 | 相对分量按命令触发时的承诺终点解析（与 linear relative 一致） |

## 跨切语义（提案）

| 语义 | 提案验收 |
|---|---|
| 共享路径 | 弧长参数化的单一路径参数驱动全部成员（L2 弧长表 + 逆映射，速度波动率断言 < 0.1% 进验收测试） |
| 圆弧平面 | 前两轴平面内圆弧逐周期半径误差 ≤ 1e-9（相对） |
| 端点 | 终点精确到达（与 linear 同容差口径） |
| 组/轴状态、命令生命周期、错误传播 | 全部沿用 linear 矩阵已验收合同 |

## 实现记录（2026-07-05）

`AxisGroup::submit_circular` + `FbMoveCircularAbsolute/Relative`（`core/axis/group.h`、
`core/fb/motion.h`）。组 cycle 路径按 path kind 采样：circular 以平面弧长为路径参数，
linear 分支保持不变（既有回放语料字节级一致）。

14-pin 原生扩展（2026-07-17）：`Tolerance`、`TransitionMode`、
`TransitionVelocity`、`TransitionParameter`、`OrientationMode` 全部进入
`GroupCommand`。正 `Tolerance` 以 aux 扫角占总扫角的比例为参数，独立
验收第 3 维及更高成员（笛卡尔圆弧验收 z）的线性跟随残差；负值或非有限
值拒绝。非法姿态枚举拒绝，合法但缺 pose 插件、坐标系或圆弧 blending
组合未实现时显式 `unsupported`。

---

*草案创建：2026-07-05；批准：2026-07-05。*
