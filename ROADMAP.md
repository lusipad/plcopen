# ROADMAP

本文档只回答一件事：**当下承诺在做什么**。现状看 [STATUS.md](STATUS.md)，
长期方向看 [VISION.md](VISION.md)，战略与拆解看
[doc/planning/](doc/planning/README.md)，历史里程碑（v0.2 → v0.11 全部
sprint 记录）在 [doc/archive/roadmap-history.md](doc/archive/roadmap-history.md)。

## 当前里程碑：三大优先轨 S0 第一批（2026-Q3）

上一里程碑（v1.0.0-alpha 收口 + 姿态批次 + 文档重建）已全部完成并
发布（KB-042~050，tag v1.0.0-alpha）。本里程碑为 S0 软件封顶期第一批：

| # | 任务 | DoD | 状态 |
|---|------|-----|------|
| 1 | Y7 组接管连续性修复 | KB-051 缺陷按矩阵修复，组接管成员速度步进 ≤ 包络（探测转固定测试） | 待矩阵起草 |
| 2 | T24 快路径设计 + H1 矩阵修订 | 流式增量重解方案成文，T23/T24/T25 补入流矩阵并获批 | **下一步** |
| 3 | H1 同步混合指令帧实现 | 修订矩阵全部验收项 + 48 关节帧口径预算门 | 待 #2 |
| 4 | H2 数值 IK（SerialChain）实现 | 已批矩阵验收项（UR 类/7DOF fuzz + 预算门） | 可并行 |
| 5 | F1 fieldbus 仓库 + 骨架 | 独立仓建立、双形态构建、许可证 README、CI 起步 | 可并行 |
| 6 | F2 虚拟从站 CI | CiA402 全梯形 + PDO 往返无硬件冒烟 | 待 #5 |
| 7 | T1 pip wheel | cibuildwheel 三平台出包（PyPI 上传属人） | 可并行 |
| 8 | T2 rerun 孪生 demo | 参考人形构型 H30 可视化 notebook 五分钟跑通 | 待 #3/#7 |
| 9 | 台架采购决策 | 下单或共建协议（S0→S1 唯一物理门） | **人工** |
| 10 | 72h soak 回写 | 2026-07-09 结果回写 DoD 表 | 定时 |
| 11 | Part 4 原文核对 | 坐标矩阵 3 项线下核实 | 人工 |

**明确不做**（本里程碑）：扭矩通道实现（T18 安全矩阵先行）、G-code、
硬件相关验证（S1）。

## 下一里程碑候选（复盘时定）

候选清单已立案为[软件极致计划](doc/planning/software-excellence-plan.md)
（Y 算法 / P 标准面 / Z 采纳 / E 证据四线，推荐起手 Z1 pip wheel →
Y0 最优性 oracle）。此外：

- **250µs 档启用**：预算证据已备（KB-044：占比 ~1-2%），随硬件阶段评估
- 硬件阶段任一触发项到位则优先于软件线

## 复盘节奏

每季度第 1 周固定四问：上季完成度与原因、用户反馈是否改变优先级、
VISION 解锁条件是否触发、路线是否仍指向 VISION。产出只更新本页与
STATUS.md，不另写大文档。

## 当前不在路线图上的能力

ST/IL/LD 编译器与 IDE、工业通信协议（Modbus/OPC UA/EtherNet-IP）、
Web HMI、SIL/冗余、云原生——何时启动见
[VISION.md 解锁条件表](VISION.md#解锁条件)。

---

*最后更新：2026-07-05（文档体系重建批③：历史迁入 archive，本页重置为当前里程碑）*
