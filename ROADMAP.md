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
| 1 | Y7 组接管修复 | 公差管语义 v2.1：标量承接 + 横向衰减 + β 分割；circular/笛卡尔待曲率项另批 | **已交付**（KB-051） |
| 2 | 对抗性探测轮 | 组/FB 语义面系统扫描，发现即 KB/修复 | **已交付**（KB-052/053/054） |
| 3 | Y0 最优性 oracle（评审定序提前：先立标尺再修算法） | 切换结构枚举表（第一交付物）+ 双 oracle + Ruckig 黑盒对照，excess_cycles 分域基线入趋势 | **主 oracle 已交付**：678 结构表 + Newton 打靶 + 4 域基线；副 oracle / Ruckig 对照随 Y2 |
| 4 | Y2 完整 OTG（评审三关键） | state-to-state 任意目标状态（非零 at）+ 钉边界 + epsilon 政策声明化；Ruckig 黑盒对照 | **核心已交付**（KB-055：非零 at + 钉边界 12 用例 + epsilon 声明）；Ruckig 对照待 ADR-0003 人工审批 |
| 4c | Y4 solve_fixed_time 一等原语（评审三关键，紧跟 Y2） | 同步 + cycle-exact 量化（删 KB-050 尾段补丁）+ 流追赶汇合同一求解；终态 ≤1e-9（T43） | **求解器 + KB-050 集成 + 流 rendezvous 已交付**（KB-056：8 候选族 + below_tmin multi-cubic；KB-050：匀速骑行/退避梯子删除；KB-035：流跟踪律 solve_fixed_time rendezvous 优先——38/38 pass）；组同步切换待下批 |
| 5a | P-Part4 剩余 FB | 管理组矩阵 + 路径表/变换第二批矩阵均已起草待批 | 两份矩阵待批 |
| 5b | P-Part5 回零规程 | MC_Step* 标准回零步 FB 面（数字输入通道模拟验收） | 矩阵已起草待批 |
| 4b | **信号通道并行项**（拷问后拉入：学习不等建造） | T1 pip wheel + 最小文档站起步——细案见 [signal-channel-plan](doc/planning/signal-channel-plan.md) | 方案已备，可并行 |
| 6 | E 系列证据 | ARM64 CI + clang-tidy 零 P0 + 变异分数门 | 快批可穿插 |
| 7 | 台架采购决策 | 下单或共建协议（S1 之门，与本里程碑并行） | **人工** |
| 8 | 72h soak 回写 | 2026-07-09 结果回写 DoD 表 | 定时 |
| 9 | Part 4 原文核对 | 坐标矩阵 3 项线下核实 | 人工 |

**收口后算法主线接续**：Y3 reachability TOPP（Y2/Y4 已提入本里程
碑）；T24 解析快路径随 H1 解冻；权威口径
[algorithm-contracts](doc/design/core/algorithm-contracts.md)。

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
