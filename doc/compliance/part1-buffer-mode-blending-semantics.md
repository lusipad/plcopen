# Part 1 单轴 BufferMode Blending 交接语义

> 状态：**已实现口径的追认规格（normative）**，描述新核 `core/axis/state.h`
> 中 `BLENDING_LOW` / `BLENDING_HIGH` 的单轴交接语义。口径来源：
> `KB-001`（v0.x 旧线的 30%/70% 速度阈值记录）与 `KB-029`（新核承接口径，
> 本文件为其完整规格）。本文件只覆盖**单轴**（`AxisModel` 命令队列）；
> 组几何 blending 是另一套语义，见
> [part4-blending-semantics.md](part4-blending-semantics.md)（`KB-031`）与
> [part4-lookahead-semantics.md](part4-lookahead-semantics.md)（`KB-032`）。
> 验收证据：`core/test/r3_motion_family_tests.cpp:525-609`
> (`check_velocity_threshold_blending`)。

## 适用范围

| 维度 | 口径 |
|---|---|
| 生效对象 | 单轴 `AxisModel` 的活动命令 + 队列头后继 |
| 支持的 BufferMode | `aborting` / `buffered` / `blending_low` / `blending_high` 共 4 个枚举（`core/axis/state.h:41-47`） |
| 前驱命令种类 | 仅 `CommandKind::move_absolute`（`move_relative` / `move_additive` 在 `start()` 归一化为 `move_absolute`，因此排队的相对运动同样可作前驱） |
| 不参与 blending | homing、`halt` / `stop`、`move_velocity` 与连续保持（`move_continuous_*`）、torque |
| 交接判据 | 前驱**实时 setpoint 速度**相对其标称速度的阈值穿越，非几何公差 |
| 不覆盖 | 组/笛卡尔路径 blending、`TransitionMode` 公差带、前瞻窗口 |

## 交接判据（normative）

**资格（eligibility）**——每周期在剖面采样后判定
（`core/axis/state.h:3535-3538`）：活动命令为 `move_absolute`
**且**队列非空**且**队列头 `buffer_mode` 为 `blending_low` 或
`blending_high`。三者任一不满足即视为不合格，并清除已武装状态
（`core/axis/state.h:3555-3558`）。

**阈值（threshold）**——`core/axis/state.h:3541-3543`：

```
nominal   = active_command_.velocity * override_        // 活动命令标称速度 × 实时 override
threshold = 0.3 * nominal   (BLENDING_LOW)
threshold = 0.7 * nominal   (BLENDING_HIGH)
```

`override_` 取**当前周期的实时值**，因此运行期 `MC_SetOverride` 会同步
改变交接门槛（声明：阈值不是提交时冻结的常量）。

**武装-回落迟滞（arm-then-fall）**——`core/axis/state.h:3544-3553`：

- 当 `|state.velocity| > threshold` 时置 `blend_armed_ = true`（只武装，不交接）；
- 在**其后某个周期**首次出现 `|state.velocity| <= threshold` 且 `blend_armed_`
  为真时，立刻调用 `blend_into_next()` 交接并结束本周期。

即：必须**先超过再跌破**。起步阶段速度尚未越过阈值的那些周期不构成交接
条件，这保证交接只发生在减速段而不是加速段。

`blend_armed_` 的清零点共四处：`start()`（新命令接管，
`core/axis/state.h:3328`）、资格失效分支（`3557`）、错误路径
（`2411`）、交接自身（`3671`）。

## 交接时的命令语义

交接由 `blend_into_next()` 执行（`core/axis/state.h:3668-3673`）：

1. 前驱以 `snapshot_.last_completed_command_id = snapshot_.active_command_id`
   记账，即前驱在交接点报 **`Done`**，**不是 `CommandAborted`**
   （与 `Aborting` 接管的本质区别；验收测试显式断言前驱不得置
   `command_aborted`）。
2. 清 `blend_armed_`，调用 `start_next_queued()`
   （`core/axis/state.h:3675-3690`）弹出队列头并 `start()`。
3. 前驱**剩余距离被丢弃**：位置以增量方式施加
   （`core/axis/state.h:3520-3523`，`command_position += step`），交接后
   前驱剖面不再被采样，其未走完的行程不会被补偿。绝对位置的正确性由后继
   自身的绝对目标保证。

## 交接速度：无公式，属涌现量（声明）

**本实现不存在交接速度公式。** PLCopen Part 1 §2.4.2-d 的文字会让读者
预期 "Low/High 取前后两命令速度的较低/较高者" 这类 **min/max 规则——
新核明确不是这样**。

`start()` 用 `otg::plan_time_optimal` 从**实时 setpoint**
`{snapshot_.command_position, snapshot_.command_velocity,
snapshot_.command_acceleration}` 规划到后继目标
（`core/axis/state.h:3401-3404`）。因此结点速度 = 交接那一周期前驱剖面
恰好采到的速度，是时间最优剖面与阈值迟滞共同作用的**涌现结果**，只满足
一个可证边界：

```
0 < |v_junction| <= threshold      // threshold = 0.3 或 0.7 × nominal
```

它既不等于两命令速度的 min，也不等于 max，且与 `override_`、加减速/jerk
限值、行程长度相关。任何依赖精确结点速度的上层逻辑属未定义用法。

## 退化与降级规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| 前驱全程未武装（短行程/低速，峰值速度从未超过阈值） | 不交接，退化为 `BUFFERED`：前驱走到自己的终点、`finish_active()` 后再启动后继 |
| 队列头不是 `blending_low/high`（含 `buffered`） | 退化为 `BUFFERED`（队列语义不变） |
| 队列为空 | 无后继可交接；前驱正常完成 |
| 前驱为 homing / halt / stop / move_velocity / 连续保持 | 不参与 blending，按各自完成语义处理 |
| 交接前发生运行期错误 | 走错误路径，`blend_armed_` 清零（`core/axis/state.h:2411`），不交接 |
| 交接前收到 `Aborting` 命令 | 按接管语义即时接管（`KB-026`），前驱报 `CommandAborted` |
| `BufferMode` 取值不在 4 个枚举内（含把 PLCopen 的 `BLENDING_PREVIOUS`/`BLENDING_NEXT`/`BLENDING_CNC` 强转进来） | 提交即 `invalid_argument`（`is_valid_buffer_mode()`，`core/axis/state.h:397-401`；校验点 `1259`、`1302`），不静默降级 |

## 与 Part 1 §2.4.2-d 的偏差

规格正文要求 `BLENDING_LOW`/`BLENDING_HIGH` 的交接速度分别取前后两命令
速度的较低/较高者。新核采用前驱标称速度的 30%/70% **阈值触发**，交接
速度为涌现量（见上节），**不实现 min/max 规则**——这是已声明偏差，
登记于 `KB-029`（其历史来源为 `KB-001` 的 v0.x 旧线口径）。同时
`BLENDING_PREVIOUS`/`BLENDING_NEXT`/`BLENDING_CNC` 三个标准枚举缺失
（§2.4.2-c，同样在 `KB-029` 范围内）。条款级判定见
[plcopen-part1-clause-matrix.md](plcopen-part1-clause-matrix.md) §2.4.2。

## 与组几何 blending 的边界

本文件的速度阈值语义**只作用于单轴命令队列**。组路径
（`MC_MoveLinear*` 等）的 `blending_low/high` 与 `mcTMMaxCornerDeviation`
组合走的是**几何公差带**语义：五次 Bézier 拐角过渡、曲率限速、前瞻窗口
结点速度（`KB-031` / `KB-032`），与 30%/70% 阈值无关，两套口径不互相
迁移、不互相回退。笛卡尔链见 `KB-049`。

## 验收证据

- `core/test/r3_motion_family_tests.cpp:525-609`
  `check_velocity_threshold_blending`：
  - `blending_low` 链：后继在前驱终点**之前**接管
    （`first_done_position < 6.0 - 0.1`），前驱报 `Done` 且**全程不得**
    置 `command_aborted`，链终点收敛到后继目标 `12.0`（1e-6）；
  - `blending_high` 短行程链：前驱（0.001 行程）从未武装，实际经过自身
    终点后才启动后继，即退化为 `BUFFERED`，链终点 `1.0`（1e-6）。

---

*本规格为已实现行为的追认（doc-only），不改变任何周期路径输出；
KB 登记见 `KB-029`（新核）与 `KB-001`（v0.x 旧线记录）。*
