# 回读批次语义矩阵 v1

> 状态：**已批准**（2026-07-05 起草，同日维护者批准"按 v1 范围实现"）。
> 本文件是回读批次的验收规格（normative）。补齐姿态批次（[姿态矩阵](orientation-semantics.md)，
> KB-042）的另一半：位姿能下命令后，上位机要能**按帧读回**组的笛卡尔
> 位置/位姿——包括姿态批次刻意回避的"矩阵→RPY"反向换算的显式语义。

## 定位与不变量

回读批次 = 组的笛卡尔/位姿回读：任意时刻按帧（ACS/MCS/PCS）读组的
命令位与实际位；位姿组（KB-042）输出完整位姿（位置 + RPY）。回读是
**纯查询**（const，不写任何状态）。**不改变以下合同**：

| 合同 | 保持 |
|---|---|
| 周期路径 / 回放 | 回读在调用方上下文按需执行，周期路径零新增代码；全部既有回放语料逐位不变 |
| KB-012 承接口径 | 既有 `FbGroupReadActual/CommandPosition` 的成员槽位（ACS）直读语义逐字节不变；笛卡尔回读是并列扩展（缺省 ACS 全兼容） |
| KB-036/037/042 管线 | 回读复用同一帧栈/工具/正解合同做**反向复合**，不引入第二套换算 |
| RT 禁令 | 回读 = 一次正解 + 常数次矩阵乘 + 常数次 atan2，无分配无异常有界时间 |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 核心 API | `AxisGroup::read_cartesian(CoordSystem cs, PositionSource src, GroupPosition &out) const`（`src ∈ {command, actual}`）；组 enabled 即可读（任意运动状态——回读不是配置，无 standby 守卫） | 单入口，镜像提交侧的坐标语义 |
| 2 | 平移管线输出 | MCS：前 N 维 = forward(关节)（无插件 = 恒等）**+ 工具偏置**（TCP = 轴系位置 + tool_offset，镜像提交时的减法）；PCS：再左乘工件帧逆；高维 ACS 直通——布局与提交侧逐槽位镜像 | 读回来的就是能重新提交的目标 |
| 3 | 位姿管线输出 | 法兰 = `PoseKinematics::forward(关节)`；TCP = 法兰 ∘ tool；MCS 输出 TCP 位姿（[0..2] 位置、[3..5] RPY）；PCS 输出 工件帧⁻¹ ∘ TCP | 与 KB-042 提交链严格互逆 |
| 4 | 矩阵→RPY 反演 | 约定 R = Rz(yaw)·Ry(pitch)·Rx(roll) 的反向：`pitch = atan2(-r20, hypot(r00, r10)) ∈ [-π/2, π/2]`，`roll = atan2(r21, r22)`，`yaw = atan2(r10, r00)`，roll/yaw ∈ (-π, π]；**万向节带**（`hypot(r00, r10) < 1e-9`）：约定 `roll = 0`、`yaw = atan2(-r01, r11)`（绕轴自由度全部折入 yaw），并置 gimbal 标志 | 反演约定完备（任意正交 R 都有唯一输出且重建矩阵精确）；歧义显式化为约定 + 标志，不静默也不报错 |
| 5 | 连续性 | **声明边界**：回读是瞬时表示，不承诺跨周期 RPY 连续或解卷绕（gimbal 邻域穿越时 roll/yaw 可跳变）；需要连续姿态跟踪的上层自行解卷绕或用矩阵域 | 解卷绕是有状态语义，与纯查询矛盾 |
| 6 | FB 门面 | 既有两个组读 FB 增加 `coord_system` 输入（缺省 ACS = 槽位直读，逐字节兼容）+ `gimbal_lock` 输出位；MCS/PCS 时输出按决策 #2/#3 布局 | 最小 FB 面，不新增块 |
| 7 | 帧配置回读 | 工件帧/工具的 setter 缓存**原始 RPY 六元组**，getter 原值回显（`workpiece_frame_rpy()` / `tool_transform_rpy()` / `tool_offset()`）——不从矩阵反算配置值 | 配置回读不应经过 gimbal 反演；所设即所读 |
| 8 | actual 源 | `actual` 读 `AxisSnapshot.actual_position` 经同一换算链；仿真（ServoSim）下与 command 同构 | 真机反馈路径为硬件阶段留同一入口 |

## 退化与拒绝规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| `WCS/FCS/TCS` 回读 | `unsupported` |
| 组未 enable / 成员为空 | `invalid_argument` |
| 万向节带内的位姿回读 | 按决策 #4 约定输出 + gimbal 标志，**不报错** |
| PCS 回读且未设工件帧 | 恒等（与 KB-036 提交侧声明一致） |
| 平移组（含无插件）回读 | 决策 #2 布局，永不输出 RPY 槽位语义 |
| FB `coord_system` 缺省 | 与既有成员槽位读逐字节一致（回归护栏） |

## 验收指标（全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| RPY 反演 oracle | 10 万例随机 RPY（含 gimbal 邻域定向采样）：make_rpy → 反演 → 重建，矩阵逐元素 | 常规域 ≤1e-12；gimbal 带 ≤1e-9；值域断言全过 |
| 位姿组往返 | KB-042 命令 settle 后 `read_cartesian(MCS/PCS)` vs 命令目标 | 位置 ≤1e-8；RPY 重建矩阵逐元素 ≤1e-8 |
| 平移组等价 | 恒等 / SCARA 组 vs 手工反向复合 oracle | ≤1e-9 |
| 配置回读 | set 后 get 原值回显 | 逐位 |
| FB 兼容 | `coord_system` 缺省 vs 既有槽位读 | 逐字节一致 |
| 回放 | 全部既有语料 | 逐位不变 |

## 不做（v1 显式范围外）

- 笛卡尔速度/加速度回读（twist）——随笛卡尔插补批次一起评估；
- RPY 跨周期连续性 / 解卷绕（决策 #5 声明边界）;
- 四元数输出形态；
- 独立 `MC_ReadCartesianTransform` FB（v1 为编程接口 getter，FB 面按需另立）；
- 力/扭矩的笛卡尔映射。

---

*草案创建：2026-07-05；批准：2026-07-05（v1 范围）。*

## 实现记录（2026-07-06，KB-043）

`geom::extract_rpy`（反演唯一入口）+ `AxisGroup::read_cartesian` / 配置
getter（原值回显）+ 组读 FB `coord_system`/`gimbal_lock` 均已落库，
测试先行（缺失 API 红 → 接线绿）。验收 `plcopen_core_readback_tests`
全绿（35/35 CTest），周期路径零改动，既有回放逐位不变。
