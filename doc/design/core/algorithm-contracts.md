# 算法合同（维护者裁决，2026-07-07——算法线权威口径）

> 性质：**已批准的合同集**。六份合同 + 落地顺序由维护者逐条给定；
> 各实现批次的语义矩阵不得与本文冲突（冲突即缺陷）。单主路径原则
> 前置：严格 OTG/TOPP + 显式 fallback，无用户算法菜单。

## 1. 单轴 OTG（完整三阶 S-curve）

```text
x = (p, v, a)；u = jerk；|v| ≤ v_max，|a| ≤ a_max，|u| ≤ j_max
目标: (p0,v0,a0) → (p1,v1,a1)        # 任意目标状态，不再限 a1=0

主求解器：
  enumerate jerk words in {+j, 0, −j}，段数 ≤ 7
  每结构：解段时长 t_i ≥ 0 → 精确积分 → 越 v/a 界即弃
  取最小 T
```

测试 oracle 三层：A 切换结构打靶 / B 粗网格值迭代 / C Ruckig 黑盒
时长对照（ADR-0003，不进 runtime）。runtime 只用自研求解器。
→ 落地：Y2 **已交付**（`time_optimal.h` A9 v2，任意目标加速度；
Y0 oracle 亦已落地，[otg-oracle-design](otg-oracle-design.md) 为标尺）。

## 2. 多轴同步（solve_fixed_time——必须补的核心能力）

```text
T_min[i] = solve_min_time(axis_i)      ∀i
T_sync   = max(T_min)
profile[i] = solve_fixed_time(axis_i, T_sync)   ∀i
```

`solve_fixed_time`：在最优结构中插入合法巡航/等待段，或直接求固定
总时长可行 jerk-limited 剖面。**验收**：

```text
profile[i].duration == T_sync
终态 p/v/a 精确 ≤ 1e-9
全程 v/a/j 在限
```

没有它，"全身同时到达"不成立。→ 落地：Y4 **已交付**（难点 T43）。

## 3. 组路径运动 / TOPP（标量路径律）

```text
第一层: TOPP-RA on s，状态 = ṡ，约束 = 关节速度/加速度/笛卡尔限
第二层: jerk-aware，状态 = (ṡ, s̈)
输出:   s(t), ṡ(t), s̈(t)；q(t) = path(s(t))
```

linear / circular / 笛卡尔**全部走标量路径律**——组运动（路径律）
与单轴 OTG（状态到状态）分工清晰。不再堆局部扫描。→ 落地：Y3
**影子中**（`core/plan/topp.h` + `topp_jerk.h` + `topp_executor.h`
已在库中，按附注 #3 以影子 oracle 对照现行扫描，数字定去留）。

## 4. 流式快路径（仅此一种）

```text
quintic Hermite: 当前 (p,v,a) → 瞄准 (p,v,0)，时长 h
解析求 v/a/j 极值（三次/二次/一次根 + 端点）
全在限 → 接受；否则 → 落全解 OTG
```

**禁止**：8 点采样、0.95 边距、平均命中率作安全证明。采样只做测试
（对照 fuzz），不做合同。慢解每周期限流（最坏拍合同）。
→ 落地：T24 **已交付**（KB-064，`core/stream/quintic_fast_path.h`，
配置开关默认关；[stream-fastpath-design](stream-fastpath-design.md)）。

## 5. aborting 接管（先批 linear 组）

```text
t̂ = 新路径切向
ṡ₀ = ⟨q̇, t̂⟩；a_s0 = ⟨q̈, t̂⟩
along   = OTG_1D(s=0, v=ṡ₀, a=a_s0 → 新路径目标)     # β·限值
lateral = OTG_decay(q̇ − ṡ₀·t̂, a_lat → 0,0)          # (1−β)·限值
q_out   = path(along.s) + lateral.offset
断言：每周期全向量 v/a/j ≤ 全额限值
```

**circular / 笛卡尔暂不批准**——扩展前置条件是曲率链式项显式进
矩阵作限值检查：

```text
q̇  = q_s·ṡ
q̈  = q_ss·ṡ² + q_s·s̈
q⃛  = q_sss·ṡ³ + 3·q_ss·ṡ·s̈ + q_s·s⃛
```

→ 落地：Y7 **已交付**（[group-takeover-semantics](../../compliance/group-takeover-semantics.md)，linear 范围已批已实现；circular/笛卡尔扩展开放）。

## 6. IK（解析优先，DLS 只做兜底）

```text
for k in 0..N_max:
  e = SE3_log(target⁻¹ · current)          # log map，禁 RPY 差值
  if |dp| ≤ ε_p and |dθ| ≤ ε_R: success
  J  = jacobian(q)
  dq = Jᵀ·(J·Jᵀ + λ²I)⁻¹·e
  q  = project_joint_limits(q + dq)
失败返回失败码，不返回近似解:
  not_converged / singular_region / limit_infeasible
```

→ 落地：H2 **待实现**（kinematics 矩阵 v2.1 已批准；
`core/kin/serial_chain.h` 尚不存在，仍是规划项）。

## 落地顺序（最小正确序）

```text
Y7 linear 接管 → Y0 oracle → Y2 完整 OTG → Y4 定时同步
→ T24 解析快路径 → Y3 TOPP-RA/jerk-aware → H2 IK v2.1
```

落地状态（2026-07-12）：**Y7/Y0/Y2/Y4/T24 已交付**（T24 = KB-064，
默认关）；**Y3 影子中**（TOPP 三头已在库，影子 oracle 对照）；
**H2 待实现**（矩阵 v2.1 已批，`serial_chain` 未动工）。

**最关键三件**：完整 OTG（Y2）、定时同步（Y4）、解析快路径校验
（T24）——已全部交付。

## 附注：内核实现观点（AI 提案，维护者批准 2026-07-07）

1. **fixed-time 是拱顶石，不是同步子功能**——四个问题同一求解：
   多轴同步；**整周期量化**（cycle-exact = solve_fixed_time 定在
   ceil(T*/Δt)·Δt 上精确求解——收口后删除 KB-050 匀速骑行/退避
   梯子两处尾段补丁）；流追赶 rendezvous（到未来 T 时刻路径状态）；
   接管汇入时间对齐。Y4 为一等原语，紧跟 Y2 落地。
2. **"不漏解"的真正战场在结构边界数值域**：t_i ≥ 0 的 epsilon
   政策是**声明项**（负阈值内钳零 + 钳零后边界条件复验），不是
   实现细节；oracle fuzz 在结构切换流形附近定向加密（两结构最优
   时长近相等的状态族）——漏解几乎只发生在那里，均匀采样撞不到。
3. **TOPP 先影子后换主**：TOPP-RA 首次落地 = 窗口版 oracle（对
   窗口场景量化现行扫描的 excess_cycles，与 Y0 同构），数字定
   去留；**前置工程 = geom 路径导数合同**（逐段解析 q_s/q_ss/
   q_sss：直线平凡、圆弧解析、五次 Bezier 三阶导有界可求）。
