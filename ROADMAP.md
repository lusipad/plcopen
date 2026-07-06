# ROADMAP

本文档只回答一件事：**当下承诺在做什么**。现状看 [STATUS.md](STATUS.md)，
长期方向看 [VISION.md](VISION.md)，战略与拆解看
[doc/planning/](doc/planning/README.md)，历史里程碑（v0.2 → v0.11 全部
sprint 记录）在 [doc/archive/roadmap-history.md](doc/archive/roadmap-history.md)。

## 当前里程碑：PLCopen 扎实化（2026-Q3）

2026-07-07 维护者定调"先把 plcopen 做好做扎实，其他领域自然继续"。
人形/EtherCAT/孪生三轨设计资产已完成并待命（见软件极致计划），实现
在本里程碑收口后接续。

| # | 任务 | DoD | 状态 |
|---|------|-----|------|
| 1 | Y7 组接管连续性修复 | KB-051 按矩阵修复，组接管成员速度步进 ≤ 包络 | 矩阵已起草待批 |
| 2 | P-Part4 剩余 FB | 管理组（GroupHome/MoveDirect/Override/Interrupt-Continue）矩阵已起草待批；路径表/KinTransform 第二批 | 矩阵已起草待批 |
| 3 | P-Part5 回零规程 | MC_Step* 标准回零步 FB 面（数字输入通道模拟验收） | 矩阵已起草待批 |
| 4 | 对抗性探测轮 | 组/FB 语义面系统扫描（跨域对照：单轴 vs 组、声明 vs 实测），发现即 KB/修复 | 方法已验证（KB-051） |
| 5 | E 系列证据 | ARM64 CI + clang-tidy 零 P0 + 变异分数门 | 快批可穿插 |
| 6 | Y0 最优性 oracle | 双 oracle 落地，excess_cycles 分域基线入趋势 | 设计已备 |
| 7 | 台架采购决策 | 下单或共建协议（S1 之门，与本里程碑并行） | **人工** |
| 8 | 72h soak 回写 | 2026-07-09 结果回写 DoD 表 | 定时 |
| 9 | Part 4 原文核对 | 坐标矩阵 3 项线下核实 | 人工 |

**明确不做**（本里程碑）：H/F/T 三轨实现（设计已备待命）、扭矩通道、
G-code、硬件验证（S1）。

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
