# Y7b1：组圆弧 aborting 接管连续性计划

> 日期：2026-07-21
> 上位计划：[software-excellence-plan.md](software-excellence-plan.md) Y7b
> 规范入口：[group-takeover-semantics.md](../compliance/group-takeover-semantics.md)
> 范围：plain joint-domain linear/circular 活动段之间的 aborting 接管；
> Cartesian、joint/Cartesian look-ahead window、blending chain 与动态 PCS 不在本批。
>
> 后续修订：Y7b2a 已证明 plain Cartesian LINE **来源侧**可直接复用真实成员
> 输出历史接到 joint LINE/circular，无需 differential kinematics；Cartesian 目标、
> Cartesian ARC/chain/window 来源和动态 PCS/tracking 的排除仍有效。

## 1. 最可能需要调整的决策

### 决策 1：Y7b 拆成可证明的 circular 与 differential-kinematics 两批

- **决定**：本批只关闭 `GroupPathKind::circular` 的成员轴连续性；Cartesian
  继续登记为 Y7b2，先补 kinematics / pose kinematics 的成员轴微分合同。
- **信心：高**。圆弧已有弧长参数化 `q_s/q_ss/q_sss`；Cartesian 只有逐周期 IK，
  没有 Jacobian、`dq/ds`、`d²q/ds²` 或 `d³q/ds³`。
- **什么会推翻它**：若现有 kinematics ABI 中发现可验证且跨插件统一的高阶微分入口，
  才重新评估合批；不得用 TCP 域导数冒充成员轴导数。

### 决策 2：连接器接收显式路径导数，不再把所有路径当直线

- **决定**：capture 按活动标量路径的实时 `q_s/q_ss` 重建成员速度/加速度；
  circular 的前两轴使用 `geom::path_derivative/path_second_derivative`，高轴使用
  `Δq/L` 与零曲率。新路径入口用
  `s_dot0 = dot(q_dot,q_s)/dot(q_s,q_s)` 与
  `s_ddot0 = dot(q_ddot-q_ss*s_dot0²,q_s)/dot(q_s,q_s)` 分解。
- **信心：高**。该写法同时覆盖圆弧弧长参数与 KB-054 的 linear 最长轴参数，
  不要求 `q_s` 是欧氏单位向量。
- **什么会推翻它**：若归一化修正改变非圆弧 replay，先把 linear 旧路径留在兼容包装；
  不顺带改变 KB-054 的路径长度语义。

### 决策 3：圆弧横向残差使用逐成员固定容量剖面

- **决定**：圆弧连接段把速度/加速度残差逐成员规划为从
  `{offset=0,v_lat,a_lat}` 回到 `{0,0,0}` 的 jerk-limited `Profile1D`；最多
  `MaxAxes` 个值对象，无 heap。实际输出为 `q_arc(s(t)) + offset(t)`，公差管半径为
  提交期逐周期求得的 `max ||offset||₂`。
- **信心：中高**。一条标量 lateral direction 无法在 N>2 时同时承接任意速度和
  加速度残差；逐成员剖面是最小的全向量表达。
- **什么会推翻它**：若 OTG 不能接受合法的非零入口加速度，缩回“速度连续、加速度
  有界”并在规范中显式降级；不得静默钳位后仍声称加速度连续。

### 决策 4：曲率链式项既参与限值构造，也做提交期后验检查

- **决定**：沿路标量包络按 beta=0.5 与圆弧最大 `|q_s|/|q_ss|/|q_sss|`
  保守收紧；随后逐周期按下式复验沿路与横向合成状态：

```text
q_dot  = q_s*s_dot + q_lat_dot
q_ddot = q_ss*s_dot^2 + q_s*s_ddot + q_lat_ddot
q_jerk = q_sss*s_dot^3 + 3*q_ss*s_dot*s_ddot + q_s*s_jerk + q_lat_jerk
```

  任一成员超过完整命令 v/a/d/j 包络时 connector 规划失败，沿用既有 rest-start
  回退；不把未经验证的剖面放进周期路径。
- **信心：高**。公式与 geom/Y3 导数合同一致；规划域允许有界逐周期检查，周期域只采样。
- **什么会推翻它**：若合法常规圆弧因离散差分容差被系统性误拒绝，优先修正检查口径；
  不删除链式项或放宽为只看标量剖面。

### 决策 5：负入口投影不允许把圆弧参数推进到段外

- **决定**：圆弧目标若投影 `s_dot0 < 0`，沿路入口取 0，负切向速度留在横向残差中
  受控衰减；不依赖 `geom::sample` 的段外延拓。
- **信心：高**。当前圆弧采样明确把弧长钳在 `[0,L]`，直接保留负 `s_dot0` 会制造
  “剖面在走、几何不动”的速度断崖。
- **什么会推翻它**：只有在规范批准圆弧段外延拓并补齐距离/公差定义后才允许改变。

## 2. 参考语义清单

| 参考行为/结构 | 处理 | 原因 |
|---------------|------|------|
| Y7 linear 公差管、即时 Busy/CommandAborted、可重入 | keep | 已批准的命令生命周期 |
| beta=0.5 沿路/横向预算与 rest-start 失败回退 | keep | 既有 KB-051/053 合同 |
| circular 前两轴弧长 + 高轴线性跟随 | keep | KB-030 公开路径语义 |
| `q_s/q_ss/q_sss` 解析导数与链式公式 | adapt | 从 Y3 影子工具转为 Y7b1 提交期正确性门 |
| 单一 lateral direction | keep for linear / adapt for circular | linear replay 优先稳定；圆弧需全向量残差 |
| linear 的 longest-axis 路径度量 | keep | KB-054 是独立声明变更，本批不修 |
| Cartesian/TCP 导数直接套成员轴 | drop | 缺 differential kinematics，证明域错误 |
| window/blending/dynamic PCS/tracking 接管 | drop | 状态与路径复合语义未进入本批矩阵 |
| callback、virtual、heap、运行期数值求解 | drop | 当前固定路径形态不需要新协议或分配 |

## 3. 假设

- `geom::ArcSegment` 以平面弧长参数化，`q_s/q_ss/q_sss` 已由独立 oracle 覆盖。
  **信心：高；来源：KB-059/061 与 geom derivative tests。**
- 普通 linear/circular 活动段由一个 `Profile1D` 驱动，提交时可完整取得
  `s/s_dot/s_ddot`。**信心：高；来源：AxisGroup start/cycle。**
- `Profile1D` 规划与提交期验证允许 O(duration * MaxAxes)，周期路径仍为 O(MaxAxes)
  固定采样且零分配。**信心：高；来源：现有 connector 的 R_tube 扫描。**
- 当前四项基线 `y7_group_takeover/a3_circular/cartesian/state_transition_matrix` 为 4/4。
  **信心：高；来源：2026-07-21 本地运行。**

## 4. 偏离策略

- 每个行为变化先由能在旧实现上失败的回归测试锁定，再改连接器。
- 最小边界是 plain linear/circular；若实现需要读取 Cartesian owner、window owner、
  tracking/Jog 或 kinematics ABI，立即停止并拆批。
- 以下情况必须停止：新增 public API/错误码；改变 KB-054 路径度量；新增 heap、依赖、
  callback 或 virtual；周期路径出现非固定迭代/IK；既有非接管 replay 无声明差异；
  第三次非机械偏离。

## 5. 机械工作（低评审价值）

1. 在 Y7 测试中加入 linear->circular、circular->linear、非对齐公差管、三轴跟随、
   重入/停止的最小回归。
2. 为 connector 增加 circular capture/plan 与固定容量 vector residual 状态。
3. `submit_circular(aborting)` 在几何完整预验证后、abort 前捕获 plain 活动路径状态。
4. `start()` 按目标 kind 选择 linear 或 circular planner；`cycle()` 在圆弧基点上叠加
   vector residual。
5. 更新规范、KB、STATUS/ROADMAP/计划索引；Cartesian 缺口保持显式。

## 6. 验证

- 新测试必须先在旧实现上复现 circular rest-start 速度断崖，再在新实现上通过。
- focused：Y7、A3 circular、Cartesian、state-transition、geom derivatives、TOPP executor。
- 全量 Windows Debug CTest；RT-safety scan；18/2409 replay；安装态 find_package 与
  FetchContent consumer；`mkdocs build --strict`；`git diff --check`。
- 逐周期验收：接管边界成员速度步进不超过加速度包络容差；合成 v/a/j 不超完整包络；
  `||offset||₂ <= R_tube`，connector 结束后回到圆弧且终点精确。
- 独立审查重点：链式项符号、非单位 `q_s` 投影、负投影、N>2 残差、RT 固定容量、
  Cartesian 未越界声称。

## 实施交接

实施期间维护
[y7b1-circular-takeover-implementation-notes.md](y7b1-circular-takeover-implementation-notes.md)。
第三次偏离或任一数学前提被推翻时停止实现并重新运行 kickoff。
