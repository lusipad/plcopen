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
→ 落地：Y2（[otg-oracle-design](otg-oracle-design.md) 为标尺）。

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

没有它，"全身同时到达"不成立。→ 落地：Y4（难点 T43）。

## 3. 组路径运动 / TOPP（标量路径律）

```text
第一层: TOPP-RA on s，状态 = ṡ，约束 = 关节速度/加速度/笛卡尔限
第二层: jerk-aware，状态 = (ṡ, s̈)
输出:   s(t), ṡ(t), s̈(t)；q(t) = path(s(t))
```

linear / circular / 笛卡尔**全部走标量路径律**——组运动（路径律）
与单轴 OTG（状态到状态）分工清晰。不再堆局部扫描。→ 落地：Y3。

## 4. 流式快路径（仅此一种）

```text
quintic Hermite: 当前 (p,v,a) → 瞄准 (p,v,0)，时长 h
解析求 v/a/j 极值（三次/二次/一次根 + 端点）
全在限 → 接受；否则 → 落全解 OTG
```

**禁止**：8 点采样、0.95 边距、平均命中率作安全证明。采样只做测试
（对照 fuzz），不做合同。慢解每周期限流（最坏拍合同）。
→ 落地：T24（随 H1，[stream-fastpath-design](stream-fastpath-design.md)）。

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

→ 落地：Y7（[group-takeover-semantics](../../compliance/group-takeover-semantics.md)，linear 范围已批）。

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

→ 落地：H2（kinematics 矩阵 v2.1，已批准）。

## 落地顺序（最小正确序）

```text
Y7 linear 接管 → Y0 oracle → Y2 完整 OTG → Y4 定时同步
→ T24 解析快路径 → Y3 TOPP-RA/jerk-aware → H2 IK v2.1
```

**最关键三件**：完整 OTG（Y2）、定时同步（Y4）、解析快路径校验
（T24）——其余可等。
