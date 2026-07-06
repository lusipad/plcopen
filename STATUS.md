# STATUS — 项目现状（单页）

> 本页是"现在在哪"的唯一入口，每个批次收口时更新。术语见
> [CONTEXT.md](CONTEXT.md)；边界细节见
> [已知边界注册表](doc/compliance/known-boundaries.md)。
> 最后更新：**2026-07-06**。

## 一句话

新核 `core/` 软件可写面 100%（R0-R4 重写 + Phase B 纯软件 + 位姿闭环 +
软件收尾批，KB-034~049），36 项测试全绿、双平台 CI 绿；**v1.0.0-alpha
已发布（2026-07-06）**，剩余工作全部依赖硬件/用户/日历时间。

## 历史刻度（处于哪一步）

| 阶段 | 窗口 | 落点 |
|------|------|------|
| v0.x 旧线（fork 自 i5cnc） | 2026-04 → 07 | Part 1/2 FB 面 45/45 收口于 v0.11.0，冻结为回放/迁移基线 |
| R0-R4 新核重写 | 2026-07 | `core/` L0-L7 全层落地，与旧线 DoD 对照 PASS |
| Phase B 纯软件 | 2026-07 | 坐标系/kinematics/轨迹流/cam/前瞻 v2/adapters（KB-034~041） |
| **← 现在** | 2026-07-07 | v1.0.0-alpha 已发布（KB-001~051）；规划体系收口；当前里程碑 = **PLCopen 扎实化**（Y7/P 系列/探测轮/证据），三轨设计资产待命 |

## 能力面（新核，默认消费面 `plcopen::plcopen`）

| 层 | 能力 | 关键 KB |
|----|------|---------|
| L0/L1 rt·otg | 整型周期时间、定长容器、SPSC；时间最优 jerk-limited OTG（任意初速/非零初始加速度，鼓包域已修） | KB-026/034 |
| L2/L3 geom·plan | 直线/三点圆弧/Bezier/刚体帧（平移+绕Z+完整 RPY 原语）；路径缓冲、公差带 blending、前瞻窗口（jerk 精确可达扫描） | KB-030/031/039 |
| L4 exec | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表、经典规律生成器）、叠加 | KB-038/046 |
| L5 axis | 单轴全命令生命周期、组共享路径（2-8 轴）、前瞻窗口执行、坐标系栈（ACS/MCS/PCS + 工件帧/工具偏置）、kinematics 级联（龙门/SCARA）、位姿管线（RPY + 6R，TCP 工具变换）、笛卡尔/位姿回读（含 RPY 反演万向节约定）、段内笛卡尔插补（直线/圆弧/blending + 前瞻窗口，逐周期逆解 + 测地姿态，opt-in；腕奇异可穿越）、窗口深度可配、双空间限速、B9 流会话 | KB-035/036/037/041~050 |
| L6 fb | Part 1/2 全量 FB 面 + Part 4 线性/圆弧/blending 门面（CoordSystem 输入） | 矩阵 45/45 |
| L7 adapters | Servo 窄接口 + ServoSim + 桥接（ADR-0004）、CiA402 状态机、CSP/CSV/CST bumpless 骨架 | KB-040 |
| 工具面 | pyplcopen（单轴/流/PoseArmSim）、18 份回放黄金语料、28 关节 @1kHz 预算基准 + 笛卡尔 IK 预算门、参考 executor demo（seqlock 双线程 + ServoSim）、周期级 trace 工具 | — |

## 质量门禁现状

- 测试：36 目标全绿（oracle/fuzz/回放/分配卫兵/基准分层）；core 行覆盖 ≥85%
- 回放：18 语料逐周期比对；声明变更零例外流程运行中
- RT：静态扫描 21 文件 + 冻结窗口分配断言；DoD §5.3 对照 **PASS**（新核 = 旧线 9.3% 耗时）
- CI：Windows + Linux（gcc/clang）双绿

## 进行中 / 待办

| 项 | 状态 |
|----|------|
| 抽查评审 | 2026-07-05 批次核心提交（OTG/流/kin/adapters）开放抽查，证据链在各提交信息；非合入门槛 |
| v0.x EOL 窗口 | v1.0.0-alpha 已发布（2026-07-06）；旧线 90 天 P0-only 窗口至 2026-10-04 |
| 硬件阶段（B5 真栈/B6 台架/B7 RT 报告） | 等台架或灯塔环境；参考 executor 软件形态已备（插上真机即测量） |
| 72h 分配断言 soak | 运行中（2026-07-06 13:02 起，07-09 出结果回写 DoD 表） |
| **KB-051 组接管速度断崖** | 对抗性探测发现的未声明缺陷，已登记；修复批次 Y7 插队最前（临时规避：运动中方向变更走 blending/窗口或先 GroupStop） |

## 近期计划

见 [ROADMAP.md](ROADMAP.md)（当前里程碑）、
[软件极致计划](doc/planning/software-excellence-plan.md)（Y/P/Z/E 候选）与
[长期规划第 7 稿](doc/planning/long-term-plan.md)（S0-S3 商业化关键路径）。
