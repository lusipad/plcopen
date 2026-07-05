# STATUS — 项目现状（单页）

> 本页是"现在在哪"的唯一入口，每个批次收口时更新。术语见
> [CONTEXT.md](CONTEXT.md)；边界细节见
> [已知边界注册表](doc/compliance/known-boundaries.md)。
> 最后更新：**2026-07-05**。

## 一句话

新核 `core/` 已完成 R0-R4 重写与 **Phase B 纯软件全部批次（BS1-BS6，
KB-034~041）**，33+ 项测试全绿、双平台 CI 绿；等待 v1.0.0-alpha 人工
发布项与硬件阶段触发。

## 能力面（新核，默认消费面 `plcopen::plcopen`）

| 层 | 能力 | 关键 KB |
|----|------|---------|
| L0/L1 rt·otg | 整型周期时间、定长容器、SPSC；时间最优 jerk-limited OTG（任意初速/非零初始加速度，鼓包域已修） | KB-026/034 |
| L2/L3 geom·plan | 直线/三点圆弧/Bezier/刚体帧（平移+绕Z+完整 RPY 原语）；路径缓冲、公差带 blending、前瞻窗口（jerk 精确可达扫描） | KB-030/031/039 |
| L4 exec | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表）、叠加 | KB-038 |
| L5 axis | 单轴全命令生命周期、组共享路径（2-8 轴）、前瞻窗口执行、坐标系栈（ACS/MCS/PCS + 工件帧/工具偏置）、kinematics 级联（龙门/SCARA；6R 预集成）、双空间限速、B9 流会话 | KB-035/036/037/041 |
| L6 fb | Part 1/2 全量 FB 面 + Part 4 线性/圆弧/blending 门面（CoordSystem 输入） | 矩阵 45/45 |
| L7 adapters | Servo 窄接口 + ServoSim + 桥接（ADR-0004）、CiA402 状态机、CSP/CSV/CST bumpless 骨架 | KB-040 |
| 工具面 | pyplcopen（单轴 + 流接口 demo）、16 份回放黄金语料、28 关节 @1kHz 预算基准 | — |

## 质量门禁现状

- 测试：33+ 目标全绿（oracle/fuzz/回放/分配卫兵/基准分层）；core 行覆盖 ≥85%
- 回放：16 语料逐周期比对；声明变更零例外流程运行中
- RT：静态扫描 20 文件 + 冻结窗口分配断言；DoD §5.3 对照 **PASS**（新核 = 旧线 9.3% 耗时）
- CI：Windows + Linux（gcc/clang）双绿

## 进行中 / 待办

| 项 | 状态 |
|----|------|
| 姿态批次（RPY + 6R 组接线） | 矩阵已批准；第一片（PoseKinematics + 刚体原语）已落库，组接线与测试待做 |
| T2 人工评审 | 2026-07-05 的 T2 提交待维护者逐行评审（各提交信息含证据链） |
| v1.0.0-alpha 发布 | 人工项（R4.8/R4.9），草案见 doc/planning/ |
| 硬件阶段（B5 真栈/B6 台架/B7 RT 报告） | 等台架或灯塔环境（触发条件见拆解文档） |

## 近期计划

见 [ROADMAP.md](ROADMAP.md)（当前里程碑）与
[doc/planning/phase-b-software-work-breakdown.md](doc/planning/phase-b-software-work-breakdown.md)。
