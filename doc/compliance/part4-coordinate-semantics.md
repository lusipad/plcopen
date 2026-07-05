# Part 4 坐标系语义矩阵 v1

> 状态：**已批准（2026-07-05，维护者，按 v1 范围；原文核对项由维护者
> 线下核实，有出入再修订矩阵）**。本文件是 Phase B1
> （坐标系栈）第一片的验收规格（normative 候选）。任务拆解见
> [phase-b-software-work-breakdown.md](../planning/phase-b-software-work-breakdown.md) BS2。
> ⚠️ **原文核对项**（批准前需人工对照 PLCopen Part 4 文本）：
> `MC_SetCartesianTransform` / `MC_SetCoordinateTransform` 的参数形态与
> 旋转角定义顺序；`MC_COORD_SYSTEM` 值域（v0.x 公开枚举为
> ACS/MCS/WCS/PCS/FCS/TCS）；PCS 与 WCS 的术语边界。本草案只引用条目
> 名，不转载原文。

## 定位与不变量

B1 v1 = 让组命令的目标点可以在 **MCS/PCS 笛卡尔帧**中表达，经帧栈换算
到 ACS 后走既有路径规划——帧换算发生在 **submit 时刻**（规划域），
周期路径零新增计算。**不改变以下已验收合同**：

| 合同 | 保持 |
|---|---|
| ACS 路径规划全家（KB-027/030/031/032/033） | 换算后的 ACS 几何走既有直线/圆弧/blending/前瞻，回放基线逐位不变 |
| 组运行时单写者 | 帧栈是组的配置状态，不新增写者 |
| RT 禁令 | 帧换算 = 常数次矩阵乘，且只在 submit（非 RT）发生 |
| 未定义显式报错 | 所有未列组合 `unsupported`/`invalid_argument` |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 坐标系集合 | v1 支持 `ACS`（既有默认）、`MCS`、`PCS`；`WCS/FCS/TCS` 显式 `unsupported` | B1 范围（long-term-plan §3.2）；三帧覆盖真实需求主体 |
| 2 | ACS↔MCS 映射 | v1 **恒等映射**（笛卡尔正交轴组：轴 i = 笛卡尔坐标 i），显式声明；kinematics 插件（BS3/B2）落地后成为可配变换 | 不把 kinematics 偷渡进 B1；恒等声明让 MCS 语义先行可测 |
| 3 | 笛卡尔维度 | 组的前 3 维参与笛卡尔帧语义；4-8 维（辅助轴）恒按 ACS 直通，不参与旋转 | Part 4 路径轴 + 辅助轴的通行分工；避免高维旋转的未定义语义 |
| 4 | PCS 定义 | PCS = MCS 上的刚体工件帧：平移 (x,y,z) + **绕 Z 单轴旋转 θ**（v1）；完整 RPY 三角旋转留 v2（与 6R kinematics 同批） | 2.5D 工件摆放覆盖绝大多数产线场景；单角无万向节歧义 |
| 5 | 工具偏置 | TCP 平移偏置 (x,y,z)（作用于目标点：命令终点 = 目标 − 工具偏置在该帧的投影）；工具旋转留 v2 | 平移工具（笔尖/点胶针/夹爪中心）是 v1 真实需求 |
| 6 | 帧设置接口 | 组级 `set_workpiece_frame(transform)` / `set_tool_offset(vec)`；仅组 `standby` 且队列空时可设置，运动中显式 `invalid_argument`（运动中换帧无定义语义） | 语义最小且安全；在线换帧（如视觉修正）需要 blending 语义配套，另立规格 |
| 7 | 命令语义 | `GroupCommand` 增 `coord_system` 字段（默认 ACS，全向后兼容）：MCS/PCS 下 `target`/`aux` 解释为该帧笛卡尔点，submit 时经帧栈换算为 ACS 再进规划；Relative 的 `distance` 按该帧**方向向量**变换（只旋转不平移） | 换算前置 = 周期路径零改动、blending/前瞻窗口天然混帧安全（窗口内全是 ACS 几何） |
| 8 | 圆弧 | MCS/PCS 三点圆弧照常支持：刚体变换保圆，aux/target 换算后走 KB-030 构造；旋转帧下圆弧平面 = 前两轴平面的原合同**不变**（v1 旋转仅绕 Z，平面不倾斜） | 绕 Z 旋转下平面圆弧封闭；倾斜平面圆弧留待完整 RPY |
| 9 | 回读 | v1 回读仍仅 ACS（既有 FbGroupRead*）；按帧回读（CoordSystem 输入）列 v2 | 诚实最小；回读换帧是纯便利功能 |
| 10 | 变换合法性 | 仅刚体变换：旋转正交、无缩放/剪切；非有限输入 `invalid_argument` | 数值病态在源头拒绝（T8） |

## 退化与拒绝规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| `WCS/FCS/TCS` | `unsupported` |
| MCS/PCS 命令但组维度 < 2 | `invalid_argument`（笛卡尔帧至少需要 XY） |
| 运动中/队列非空时设置帧或工具偏置 | `invalid_argument` |
| 帧参数非有限 | `invalid_argument` |
| PCS 命令但从未设置工件帧 | 按恒等帧执行（= MCS）——显式声明，不报错 |
| 未设置工具偏置 | 零偏置（恒等），不报错 |
| 4-8 维目标含于 MCS/PCS 命令 | 高维分量按 ACS 直通（声明），不报错 |

## 验收指标（全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| 几何等价 oracle | PCS/MCS 命令的逐周期 setpoint ≡ 手工预变换后等价 ACS 命令 | 逐周期差 ≤ 1e-9 |
| 旋转帧圆弧 | PCS（θ≠0）三点圆弧逐周期半径误差 | ≤ 1e-9（KB-030 同口径） |
| 工具偏置 | 带偏置命令终点 = 目标 − 偏置（帧内） | ≤ 1e-9 |
| 拒绝矩阵 | 上表全部形态 | 显式错误码，零静默 |
| 回放 | 既有全部 golden 场景（纯 ACS） | 逐位不变 |
| 混帧窗口 | ACS/MCS/PCS 段混合进同一 blending 窗口 | 几何正确（换算前置的构造性保证）+ 回放新场景 |

## 不做（v1 显式范围外）

- kinematics 正逆解（BS3/B2：恒等映射之外的 ACS↔MCS）；
- 完整 RPY 旋转、倾斜平面圆弧、工具旋转（v2，与 6R 同批）；
- 运动中在线换帧/帧插值（视觉伺服修正类需求另立规格）；
- 按帧回读、`MC_ReadCartesianTransform` 类回读面（v2）；
- 路径长度/速度在 PCS 与 ACS 间的重解释——路径参数语义维持既有合同
  （linear 按最长成员行程、circular 按平面弧长），换算发生在几何点上。

---

## 实现记录（2026-07-05，BS2.2-BS2.5，KB-036）

- L2 帧原语 `geom::RigidFrame`（`core/geom/frame.h`）：平移 + 绕 Z 旋转，
  cos/sin 缓存，刚体性构造性成立（无任意矩阵输入需校验正交）。
- L5 集成：`GroupCommand.coord_system`（默认 ACS 全兼容）、
  `AxisGroup::set_workpiece_frame/set_tool_offset`（standby + 空队列守卫）、
  `apply_coordinate_frame` 在 submit 入口把 MCS/PCS 目标/aux 换算为 ACS
  （绝对点过帧减工具偏置；相对距离仅旋转——平移与工具偏置在两个 TCP
  位置间相消）。FB 面 `FbMoveLinear*/FbMoveCircular*` 增 `coord_system`。
- 验收：`plcopen_core_coordinate_tests` 7 场景——MCS 恒等、PCS 直线/
  相对/圆弧几何等价 oracle（双组逐周期 1e-9）、混帧 blending 窗口、
  拒绝矩阵（WCS/FCS/TCS、非有限、运动中换帧）、PCS 未设帧=恒等声明、
  FB 面。回放黄金场景 `core-group-pcs`（ACS 首段 + PCS blending 后继
  混帧窗口 + 工具偏置，166 样本）；既有全部 golden 逐位不变。

*草案创建：2026-07-05；批准：2026-07-05。*
