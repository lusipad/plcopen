# STATUS — 项目现状（单页）

> 本页是"现在在哪"的唯一入口，每个批次收口时更新。术语见
> [CONTEXT.md](CONTEXT.md)；边界细节见
> [已知边界注册表](doc/compliance/known-boundaries.md)。
> 最后更新：**2026-07-10**。

## 一句话

新核 `core/` 主体能力已落地（R0-R4 重写 + Phase B 纯软件 + 位姿闭环 +
软件收尾批，KB-034~068），**v1.0.0-alpha 已发布（2026-07-06）**，Part 4
管理/路径表/变换 + Part 5 回零 FB 已交付。周期质量门已复绿（2026-07-11
远端复验：Nightly/Coverage/Wheels 全通过）。S0 仍有纯软件闭环：PyPI/Pages
外部发布动作、参考 executor 的规划域/RT 域拆分与 TSAN 证据；S1-S3 另
依赖硬件、用户和日历时间。**L 系列语言层已启动（2026-07-11 维护者拍板）**，
L0 语义矩阵草案送批中。

## 历史刻度（处于哪一步）

| 阶段 | 窗口 | 落点 |
|------|------|------|
| v0.x 旧线（fork 自 i5cnc） | 2026-04 → 07 | Part 1/2 FB 面 45/45 收口于 v0.11.0，冻结为回放/迁移基线 |
| R0-R4 新核重写 | 2026-07 | `core/` L0-L7 全层落地，与旧线 DoD 对照 PASS |
| Phase B 纯软件 | 2026-07 | 坐标系/kinematics/轨迹流/cam/前瞻 v2/adapters（KB-034~041） |
| **← 现在** | 2026-07-10 | v1.0.0-alpha 已发布；当前里程碑 = **PLCopen 扎实化**——主体功能已交付，正在关闭深度周期门、发布渠道和运行时双域证据 |

## 能力面（新核，默认消费面 `plcopen::plcopen`）

| 层 | 能力 | 关键 KB |
|----|------|---------|
| L0/L1 rt·otg | 整型周期时间、定长容器、SPSC；时间最优 jerk-limited OTG（任意初速/非零初始加速度，鼓包域已修） | KB-026/034 |
| L2/L3 geom·plan | 直线/三点圆弧/Bezier/刚体帧（平移+绕Z+完整 RPY 原语）；路径缓冲、公差带 blending、前瞻窗口（jerk 精确可达扫描） | KB-030/031/039 |
| L4 exec | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表、经典规律生成器）、叠加 | KB-038/046 |
| L5 axis | 单轴全命令生命周期、组共享路径（2-8 轴）、前瞻窗口执行、坐标系栈（ACS/MCS/PCS + 工件帧/工具偏置）、kinematics 级联（龙门/SCARA）、位姿管线（RPY + 6R，TCP 工具变换）、笛卡尔/位姿回读（含 RPY 反演万向节约定）、段内笛卡尔插补（直线/圆弧/blending + 前瞻窗口，逐周期逆解 + 测地姿态，opt-in；腕奇异可穿越）、窗口深度可配、双空间限速、B9 流会话 | KB-035/036/037/041~050 |
| L6 fb | Part 1/2 全量 FB 面 + Part 4 线性/圆弧/blending 门面（CoordSystem 输入）+ Part 4 管理 FB（GroupHome/MoveDirect/GroupSetOverride/GroupInterrupt·Continue）+ Part 4 路径表/变换 FB（PathSelect/MovePath/SetKinTransform/ReadCartesianTransform）+ Part 5 回零 FB（StepAbsSwitch/StepLimitSwitch/StepRefPulse/StepDirect/FinishHoming） | 矩阵 45/45 + Part 4/5 |
| L7 adapters | Servo 窄接口 + ServoSim + 桥接（ADR-0004）、CiA402 状态机、CSP/CSV/CST bumpless 骨架 | KB-040 |
| 工具面 | pyplcopen（单轴/流/PoseArmSim，wheel 构建链已具备但 PyPI 尚未发布，CycleConfig SI 换算）、18 份回放黄金语料、28 关节 @1kHz 预算基准 + 笛卡尔 IK 预算门、参考 executor demo（双向 SPSC 单写者基础；canonical `planning → committed trajectory → RT` 双域尚未完成）、周期级 trace、MkDocs 源码与部署 workflow（Pages 尚未启用）、vcpkg/Conan recipe、ErrorCode 诊断文本 | — |

## 质量门禁现状

- 测试：48 项 CTest；当前提交在 WSL 复现的 gcovr 8.6/Linux CI 口径为 90.1%（8868/9840，达到 90% 门槛），远端 workflow 结果仍待复验；Windows `coverage.ps1` 的独立 Debug 全模块口径为 87.88%（21958/24985，通过其 50% 门），两者不混用
- 回放：18 语料逐周期比对；声明变更零例外流程运行中
- RT：静态扫描 24 文件 + 冻结窗口分配断言；DoD §5.3 的周期耗时对比通过（新核 = 旧线 9.3%），72h 分配/抖动证据未关闭
- CI（远端基线 `23fd571`，截至 2026-07-11）：Windows/Linux 主线通过，Mutation 18/18 通过；KB-068 修复后远端复验完成——Core Nightly（07-11 定时：OTG 1M fuzz、50M-cycle allocation soak、time-optimal 1M fuzz 三作业拆分后全绿）、Coverage Gate 与 Wheels（07-11 手动重触发）全部通过。`main` 仍无 branch protection/ruleset，这些是 workflow 证据而非技术强制的 required checks。触发边界见 [CI gate 矩阵](doc/compliance/ci-gates.md)

## 进行中 / 待办

| 项 | 状态 |
|----|------|
| 抽查评审 | 2026-07-05 批次核心提交（OTG/流/kin/adapters）开放抽查，证据链在各提交信息；非合入门槛 |
| v0.x EOL 窗口 | v1.0.0-alpha 已发布（2026-07-06）；旧线 90 天 P0-only 窗口至 2026-10-04 |
| 硬件阶段（B5 真栈/B6 台架/B7 RT 报告） | 等台架或灯塔环境；参考 executor 的单写者/SPSC 基础已备，双域与真机测量链仍待完成 |
| 72h 分配断言 soak | **未完成**：仅有 2026-07-06 启动记录，未见结束日志或 DoD 回写；Nightly 的 50M-cycle tier 不等同于 72h 证据 |
| **KB-051 组接管速度断崖** | **已修复（Y7，2026-07-08）**：linear 组级 aborting 接管速度连续；circular/笛卡尔接管扩展仍按 KB-051 适用范围声明另批 |
| Part 4 管理/路径表/变换 FB | **已交付**（GroupHome/MoveDirect/GroupSetOverride/GroupInterrupt·Continue + PathSelect/MovePath/SetKinTransform/ReadCartesianTransform，验收测试已接入 CTest） |
| Part 5 回零 FB | **已交付**（StepAbsSwitch/StepLimitSwitch/StepRefPulse/StepDirect/FinishHoming，验收测试已接入 CTest） |
| Y2 epsilon 政策 | **已声明化**（KB-057：段时长钳零 + 复验，整数量化天然覆盖） |
| Y2 Ruckig 对照 | **人工门控**（ADR-0003：需先审查上游许可证，不进 R1） |

## 近期计划

见 [ROADMAP.md](ROADMAP.md)（当前里程碑）、
[软件极致计划](doc/planning/software-excellence-plan.md)（Y/P/Z/E 候选）与
[长期规划第 7 稿](doc/planning/long-term-plan.md)（S0-S3 商业化关键路径）。
