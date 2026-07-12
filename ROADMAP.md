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
| 4 | Y2 完整 OTG（评审三关键） | state-to-state 任意目标状态（非零 at）+ 钉边界 + epsilon 政策声明化；Ruckig 黑盒对照 | **软件全交付**（KB-055 非零 at + 12 固定 + 50k fuzz；KB-057 epsilon 政策声明化；Y0 oracle 678 结构表 + 4 域基线）；Ruckig 对照待 ADR-0003 人工审批 |
| 4c | Y4 solve_fixed_time 一等原语（评审三关键，紧跟 Y2） | 同步 + cycle-exact 量化（删 KB-050 尾段补丁）+ 流追赶汇合同一求解；终态 ≤1e-9（T43） | **求解器 + KB-050 集成 + 流 rendezvous 已交付**（KB-056：8 候选族 + below_tmin multi-cubic；KB-050：匀速骑行/退避梯子删除；KB-035：流跟踪律 solve_fixed_time rendezvous 优先——38/38 pass）；组同步切换待下批 |
| 5a | P-Part4 剩余 FB | 管理组矩阵 + 路径表/变换第二批矩阵均已批准实现 | ⚠️ **口径修正（2026-07-12 原文审计）**：已交付项属实，但 **Part 4 v2.0 规格共 63 个 FB，我们 ~23 个（~35%）**——旧"零留白"声明基于自编 30 项清单，已作废。缺 44 项，见 P-Part4b |
| 5b | P-Part5 回零规程 | MC_Step* 标准回零步 FB 面（数字输入通道模拟验收） | ⚠️ **口径修正**：5 FB 已交付属实，但 **规格共 11 个**（5/11 = 45%）——缺 StepBlock/StepDistanceCoded/HomeAbsolute/飞越式×2/AbortPassiveHoming，见 P-Part5b |
| 4b | **信号通道并行项 + Z 系列全量**（拷问后拉入） | T1 pip wheel + Z3 文档站 + Z2 单位层 + Z4 包管理 + Z5 诊断 | **Z3 文档站已上线**（http://lusipad.com/plcopen/ ，Pages 2026-07-11 启用）；Wheels 三平台远端绿 + PyPI Trusted Publishing 作业已备——剩余人专属：PyPI publisher 注册 + 发布 tag |
| 6 | E 系列证据 | ARM64 CI + clang-tidy 零 P0 + 变异分数门 | **已复绿（2026-07-11 远端复验）**：Mutation 18/18、Coverage Gate（gcovr 90% 门）、Core Nightly 三作业全部通过 |
| 7 | 台架采购决策 | 下单或共建协议（S1 之门，与本里程碑并行） | **人工** |
| 8 | 72h soak 回写 | 保存完整结束日志并回写 DoD 表 | **周期等效口径关闭（2026-07-11）**：07-06 墙钟版证据链断裂如实登记；25.92 亿冻结周期（72h@1kHz ×10）Release 零分配 PASS 回写 DoD；墙钟 72h 归 B7。口径调整开放维护者复核 |
| 9 | Part 4 原文核对 | 坐标矩阵 3 项线下核实 | ✅ **已完成并升级为 🟢 AI 全自主**（2026-07-12）：规格 PDF 公开免费、`pdftotext` 可直读——**全面原文审计已出**（[plcopen-conformance-audit](doc/compliance/plcopen-conformance-audit.md)），推翻两条覆盖率声明 + 一条认证假设 |

**算法主线现状**：T24 quintic 快路径及 Y3 TOPP-RA 两层求解、executor/
oracle 已落库；主路径采用和任何语义变化仍需独立门禁。权威口径见
[algorithm-contracts](doc/design/core/algorithm-contracts.md)。

**明确不做**（本里程碑）：H/F/T 三轨实现（设计已备待命）、扭矩通道、
G-code、硬件验证（S1）。

## 下一里程碑：L 系列语言层（2026-07-11 维护者拍板启动）

维护者裁决 L 系列（IEC 61131-3 语言层，自研）优先于 H/F/T 三轨启动；
触发器挂账：**台架下单 → F（EtherCAT）插队；真机在手 → H1 修订稿送批**。
批次表见[软件极致计划](doc/planning/software-excellence-plan.md) L 系列节。
当前进度：**L0 与 L1a 均已交付**（KB-069/070，矩阵均 2026-07-11 批准
并同日实现收口——L0：容错前端 + 确定性 VM + 50 黄金程序 + 10 万 fuzz；
L1a：16 标量宇宙 + 无损加宽白名单 + 210 格转换矩阵机读化三方比对 +
双锚点哈希门）。下一批次候选：L1b（复合类型）矩阵待起草 / L2（POU 与
MC_* 绑定）。扎实化收口项全部关闭（AxisGroup 第 1 批、executor 双域
ADR-0007、Pages 上线、soak 周期等效），剩 PyPI 人专属动作。

## 商用门板穿插队列（2026-07-12 CEO 评审拍板，硬承诺非候选）

商用八项硬指标中四个纯软件可关切片，此前无挂号——本次评审发现并
排入 L 系列审批空窗，**顺序 P#8 → P#5 → P#2 → P#7，全部关闭前不得
后延出队**（死约束）：

| 批 | 内容 | 量级 | 关的门 |
|----|------|------|--------|
| P#8 | STO/SS1 集成边界文档（不做什么/边界线/集成商责任/CiA402·STO 交互口径） | 0.1 L0 | 指标 #8 全闭 |
| P#5 | 分支覆盖首测 → 补盲区 → workflow 加 85% 分支门（现只量行覆盖，指标写的是分支） | 0.2 L0 | 指标 #5 余项 |
| P#2 | 精度证据套件：样条/圆弧稳速波动 <0.1% + cam 相位 <1 周期 + blending 汇总断言，产出 commercial-gate-evidence.md 八项证据总账 | 0.5 L0 | 指标 #2 全闭（软件面） |
| P#7 | 文档四件套 + **运维手册小节**（fault 码→原因→处置）：全 FB 参考/实时集成/调优/TwinCAT·CODESYS 迁移指南（与 Z3′ 合流，L2a 后执行取 ST 素材） | 1 L0 | 指标 #7 主体 |
| G1 | 治理文档批：CONTRIBUTING + GOVERNANCE（决策/继任声明）+ SECURITY.md（含 ST 不可信输入面威胁模型与资源上限声明）；GitHub org 迁移列人专属 | 0.2 L0 | 采纳信任面（bus factor 声明化） |
| A1 | 浮点数值语义合同：编译旗标钉死（FMA 收缩/舍入）+ 跨平台运行值容差/禁止项 + 双平台运行值对照测试 | 0.3 L0 | "确定性 VM"从字节码级升为数值级诚实 |
| A2 | T30 WCET 度量工具：每指令成本表 + 静态无环路径长度 + 调用深度上界（compile 产物附带） | 0.2 L0 | T30 既有承诺兑现 |

**2026-07-12 生态调研追加裁决**：竞品 Intel RTmotion 确认存在（Apache
2.0、活跃、Part 1/2、零 Part 4）——VISION 竞争表已校准，"生态位无人"
作废；**CI 只测我们自己**（E5 趋势管线常驻，零外部依赖），**竞品对拍
（E6）为手动报告批不进 CI**，三条铁律见软件极致计划 E 系列表；**R 系列
（真硬件，锚 LeRobot/SO-ARM100）立案**，维护者已确认采购设备。

另两条 CEO 评审裁决（2026-07-12）：**L2b 收口设采纳信号检查点**
（pip 下载/stars/询盘/冷用户反馈，信号为零则 L3-L7 降优先级重议）；
**main 强制门禁（branch protection + required checks）挂 S1 入口条件**
（灯塔接触前上，现阶段保持直推效率）。

## 其他候选（复盘时定）

候选清单已立案为[软件极致计划](doc/planning/software-excellence-plan.md)
（Y 算法 / P 标准面 / Z 采纳 / E 证据四线 + **2026-07-12 计划补遗**：
Y7b 组接管扩展 / Y4b 组同步切换 / Y3′ 影子换主 / X5 executor IPC 形态 /
E5 基准趋势管线 / Z0′ 冷用户首轮 / Z3′ 文档站内容）；语言层批次的可
执行拆解见 [L 系列工作拆解](doc/planning/l-series-work-breakdown.md)
（含 pyplcopen ST 暴露、语言层探测轮等并行项）。已完成项与当前缺口
以本页表格为准。此外：

- **250µs 档启用**：预算证据已备（KB-044：占比 ~1-2%），随硬件阶段评估
- 硬件阶段任一触发项到位则优先于软件线

## 全景总图

剩余工作的完整账（含 AI 能做/不能做的分栏、依赖链、人侧杠杆排序）见
[**主计划**](doc/planning/master-plan.md)——复盘第一入口。**一句话**：
软件存货约 15-16 L0 当量（数月量级），F 轨 EtherCAT 是最后一块大软件
且是 #1/#4 的前置；**存货耗尽那天，项目速度 = 维护者的速度**——所以
人专属四件（台架决策 ★★★ / ADR-0006 许可证核验 ★★ / R 系列设备 /
P0 账号与申请）越早做，AI 的产出越不会堆在仓库里。

## 复盘节奏

每季度第 1 周固定四问：上季完成度与原因、用户反馈是否改变优先级、
VISION 解锁条件是否触发、路线是否仍指向 VISION。产出只更新本页与
STATUS.md，不另写大文档。

## 当前不在路线图上的能力

IL（永不，Ed3 已弃用）、LD/FBD 图形画布与 IDE、cam 表图形编辑器、
TC6-XML 工程交换、工业通信协议（Modbus/OPC UA/EtherNet-IP）、
Web HMI、SIL/冗余、云原生——何时启动见
[VISION.md 解锁条件表](VISION.md#解锁条件)。
（ST/SFC 编译器已上路线图 = L 系列；编辑器的文本路线 = D1 LSP/
VS Code 扩展与 D2 WASM Playground，挂在 L 系列拆解的触发表。）

---

*最后更新：2026-07-12（L0/L1a 交付 + executor 双域 + 计划补遗盘点）*
