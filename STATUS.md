# STATUS — 项目现状（单页）

> 本页是"现在在哪"的唯一入口，每个批次收口时更新。术语见
> [CONTEXT.md](CONTEXT.md)；边界细节见
> [已知边界注册表](doc/compliance/known-boundaries.md)。
> 最后更新：**2026-07-06**。

## 一句话

新核 `core/` 已完成 R0-R4 重写、**Phase B 纯软件全部批次与姿态/回读/
笛卡尔插补批次（KB-034~044）**，36 项测试全绿、双平台 CI 绿；等待
v1.0.0-alpha 人工发布项与硬件阶段触发。

## 历史刻度（处于哪一步）

| 阶段 | 窗口 | 落点 |
|------|------|------|
| v0.x 旧线（fork 自 i5cnc） | 2026-04 → 07 | Part 1/2 FB 面 45/45 收口于 v0.11.0，冻结为回放/迁移基线 |
| R0-R4 新核重写 | 2026-07 | `core/` L0-L7 全层落地，与旧线 DoD 对照 PASS |
| Phase B 纯软件 | 2026-07 | 坐标系/kinematics/轨迹流/cam/前瞻 v2/adapters（KB-034~041） |
| **← 现在** | 2026-07-06 | 姿态/回读/笛卡尔插补批次已收口（KB-042~044，6R 逐周期逆解实测 2.4µs）；等 v1.0.0-alpha 人工发布与硬件阶段触发 |

## 能力面（新核，默认消费面 `plcopen::plcopen`）

| 层 | 能力 | 关键 KB |
|----|------|---------|
| L0/L1 rt·otg | 整型周期时间、定长容器、SPSC；时间最优 jerk-limited OTG（任意初速/非零初始加速度，鼓包域已修） | KB-026/034 |
| L2/L3 geom·plan | 直线/三点圆弧/Bezier/刚体帧（平移+绕Z+完整 RPY 原语）；路径缓冲、公差带 blending、前瞻窗口（jerk 精确可达扫描） | KB-030/031/039 |
| L4 exec | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表）、叠加 | KB-038 |
| L5 axis | 单轴全命令生命周期、组共享路径（2-8 轴）、前瞻窗口执行、坐标系栈（ACS/MCS/PCS + 工件帧/工具偏置）、kinematics 级联（龙门/SCARA）、位姿管线（RPY + 6R，TCP 工具变换）、笛卡尔/位姿回读（含 RPY 反演万向节约定）、段内笛卡尔插补（逐周期逆解 + 测地姿态，opt-in）、双空间限速、B9 流会话 | KB-035/036/037/041~044 |
| L6 fb | Part 1/2 全量 FB 面 + Part 4 线性/圆弧/blending 门面（CoordSystem 输入） | 矩阵 45/45 |
| L7 adapters | Servo 窄接口 + ServoSim + 桥接（ADR-0004）、CiA402 状态机、CSP/CSV/CST bumpless 骨架 | KB-040 |
| 工具面 | pyplcopen（单轴 + 流接口 demo）、17 份回放黄金语料、28 关节 @1kHz 预算基准 + 笛卡尔 IK 预算门 | — |

## 质量门禁现状

- 测试：36 目标全绿（oracle/fuzz/回放/分配卫兵/基准分层）；core 行覆盖 ≥85%
- 回放：17 语料逐周期比对；声明变更零例外流程运行中
- RT：静态扫描 21 文件 + 冻结窗口分配断言；DoD §5.3 对照 **PASS**（新核 = 旧线 9.3% 耗时）
- CI：Windows + Linux（gcc/clang）双绿

## 进行中 / 待办

| 项 | 状态 |
|----|------|
| 抽查评审 | 2026-07-05 批次核心提交（OTG/流/kin/adapters）开放抽查，证据链在各提交信息；非合入门槛 |
| v1.0.0-alpha 发布 | 人工项（R4.8/R4.9），草案见 doc/planning/ |
| 硬件阶段（B5 真栈/B6 台架/B7 RT 报告） | 等台架或灯塔环境（触发条件见拆解文档） |

## 近期计划

见 [ROADMAP.md](ROADMAP.md)（当前里程碑）与
[doc/planning/phase-b-software-work-breakdown.md](doc/planning/phase-b-software-work-breakdown.md)。
