# Part 4 几何 Blending 语义矩阵

> 状态：**已批准（2026-07-05，维护者）；v1 已实现（KB-031）**。本文件是
> Phase A4（公差带几何 blending v1）的验收规格（normative），实现与验收
> 测试以本矩阵为准。曲线选型依据 long-term-plan 6.3-#4（五次 Bézier，C2
> 加速度连续）。验收证据：`plcopen_core_a4_blending_tests` + 回放场景
> `core-group-blend`。实现形态：KB-031 的整链单剖面执行已被
> A5 look-ahead 窗口取代（KB-032，见
> [part4-lookahead-semantics.md](part4-lookahead-semantics.md)）：分段剖面
> 按扫描结点速度衔接，直线段不再被拐角限速拖慢；公差承诺、显式降级与
> 构造性节拍门槛语义保持。

## 提案范围（v1）

| 决策点 | 提案 | 理由 |
|---|---|---|
| 过渡曲线 | 五次 Bézier（C2） | 6.3-#4 推荐基线；闭式求值，成本约圆角圆弧 3 倍；clothoid 留 Phase D 重估 |
| 公差模型 | `mcCornerDeviation`：过渡曲线到原拐角的最大偏差 ≤ TransitionParameter[0] | 几何维承诺，替代旧 30%/70% 速度阈值时间近似 |
| 适用段对 | linear→linear（批准圆弧规格后扩展 linear↔circular） | 与已实现路径段对齐 |
| L2/L3 承载 | 复用 R2 已交付的 `decide_blend` 公差带二次 Bézier 决策，升级为五次 | 已有原语与测试 |

## TransitionMode 组合语义矩阵

未列出组合 = 显式 `unsupported`，不静默降级。

| TransitionMode | TransitionParameter | BufferMode | 语义 |
|---|---|---|---|
| `mcTMNone` | 全零 | Aborting/Buffered | 现状（零过渡，完全停止或换段），保持 implemented |
| `mcTMMaxCornerDeviation` | [0] = 公差 > 0，有限 | `blending_low` / `blending_high` | 插入五次 Bézier 过渡：路径偏差 ≤ 公差；拐角通过速度由几何限速（曲率 × 轴加速度上限）与前瞻共同决定 |
| `mcTMMaxCornerDeviation` | [0] ≤ 0 或非有限 | 任意 | `invalid_argument` |
| `mcTMMaxCornerDeviation` | 公差 > 0 | Aborting | `invalid_argument`（aborting 语义与预混合队列互斥，显式报错） |
| 其余模式（StartVelocity/ConstantVelocity/CornerDistance） | — | — | `unsupported`（后续按需立项） |

## 退化与降级规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| 公差 ≥ 任一相邻段长的一半 | 过渡区截断到段长一半，实际偏差仍 ≤ 公差（不放大） |
| 相邻段共线（无拐角） | 无过渡曲线，直通；`blending` 请求按 buffered 连接执行（速度不降零），非错误 |
| 拐角反折（夹角 ≈ 180°） | 降级为 `BUFFERED`（完全停止），降级事实通过命令状态可查，不静默 |
| 队列中后继命令被 abort/error | 已承诺的过渡段执行完毕后停止（承诺轨迹不回撤，与单写者模型一致） |
| 过渡区内新 aborting 命令 | 允许，从当前采样状态接管（KB-026 接管语义） |

## 验收指标（6.5 对齐）

- 路径偏差 ≤ 公差且公差利用率 > 80%（不过度保守）；
- 过渡区加速度连续（setpoint 二阶差分无阶跃）；
- 拐角不停车：过渡区最低路径速度 > 0，循环时间效率对比零过渡基线有可测改善；
- 兼容模式：旧 30%/70% 速度阈值语义不迁入新核（v0.x 专属，`KB-001` 保留为旧线口径）。

---

*草案创建：2026-07-05；批准：2026-07-05。圆弧规格见
[plcopen-motion-part4-circular-matrix.md](plcopen-motion-part4-circular-matrix.md)。*
