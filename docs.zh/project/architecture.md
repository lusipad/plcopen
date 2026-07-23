<title>架构</title>

# 架构

plcopen 是可嵌入的 C++17 运动控制内核。生产内核按依赖只向内的阶梯组织，
阶梯之外还有支撑库和两张外圈消费面。

## 分层地图

| 层 | 职责 |
|---|---|
| L0 `rt` | 有界实时原语、队列、时间与错误 |
| L1 `otg` | jerk-limited 在线轨迹生成 |
| L2 `geom` | 几何、路径、样条与坐标框架 |
| L3 `plan` | look-ahead 与 blending |
| L4 `exec` | 周期采样与执行 |
| L5 `axis` | 轴与组状态机 |
| L6 `fb` | PLCopen 风格功能块门面 |
| L7 `adapters` | 窄硬件与协议适配器 |

`kin` 与 `stream` 是由 L5 消费的支撑库。IEC 61131-3 `st` 编译器和 VM
位于外圈，与 L7 构成并行消费面。

## 承重不变量

- 依赖只向内，生产依赖图是 DAG。
- L0～L4 以及 `kin`、`stream` 不包含 PLCopen 语义。
- 规划域持有规划状态并提交轨迹 frame。
- 实时域每个 tick 只消费一个已提交 frame。
- 硬件访问保持在窄 `Servo` 边界之后。

这些规则比层号本身更重要。层号并不表示每一层都必须 include 紧邻的下一层。

## 权威设计记录

完整图集、实现现状对照和不变量表维护在
[权威架构文档](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md)。
架构理由单独记录在
[ADR](https://github.com/lusipad/plcopen/tree/main/doc/design/decisions) 中。
