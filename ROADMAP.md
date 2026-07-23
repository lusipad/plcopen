# ROADMAP

本文档只回答一件事：**当下承诺在做什么**。现状看 [STATUS.md](STATUS.md)，
长期方向看 [VISION.md](VISION.md)，战略与拆解看
[doc/planning/](doc/planning/README.md)，历史里程碑（v0.2 → v0.11 全部
sprint 记录）在 [doc/archive/roadmap-history.md](doc/archive/roadmap-history.md)。

## 当前承诺（2026-07-23，唯一优先级声明）

优先级只在本节声明一次，下列各节只承载明细，不再各自宣称"当前/最高"：

1. **P0 v0.20.0 正式发布（已完成）**：版本信号已校准回 pre-1.0，
   [GitHub Release](https://github.com/lusipad/plcopen/releases/tag/v0.20.0)
   与 [PyPI `pyplcopen==0.20.0`](https://pypi.org/project/pyplcopen/0.20.0/)
   已于 2026-07-20 上线；tag 固定到 `7788a85`。候选门禁：
   [Windows](https://github.com/lusipad/plcopen/actions/runs/29692190516)、
   [Linux](https://github.com/lusipad/plcopen/actions/runs/29692190521)、
   [Wheels](https://github.com/lusipad/plcopen/actions/runs/29692204896)、
   [Nightly](https://github.com/lusipad/plcopen/actions/runs/29692206177)、
   [Coverage](https://github.com/lusipad/plcopen/actions/runs/29692207269)、
   [Mutation](https://github.com/lusipad/plcopen/actions/runs/29692208327) 与
   [Docs](https://github.com/lusipad/plcopen/actions/runs/29692209416) 全绿。
   交付形态为 GitHub source/header-only Release + PyPI 三平台 wheels/sdist；
   [tag 发布工作流](https://github.com/lusipad/plcopen/actions/runs/29709944703)
   5/5 job 全绿，发布了 20 个 wheels 与 1 个 sdist。
2. **P0 主线复绿（已完成）**：`17932de` 已修复测试辅助函数无谓按值复制
   `st::Program`；[Windows CI](https://github.com/lusipad/plcopen/actions/runs/29680604789)
   与 [Linux CI](https://github.com/lusipad/plcopen/actions/runs/29680604785) 均通过，
   包括 Linux/Clang 81 文件 `clang-tidy`、GCC 与 ARM64。
3. **P1 CI 时长恢复（已完成）**：`0195739` 已把 11 项 fuzz CTest 统一移到原生
   Nightly，A2/X5 新增专项后日常门当前跑 82 项非 fuzz 测试；[Linux CI](https://github.com/lusipad/plcopen/actions/runs/29682295508)
   中 ARM/QEMU 测试由 28 分 40 秒降至 4 分 57 秒，[Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29682299843)
   11/11 fuzz 仅用 5.30 秒并全绿。`7336379` 在不减少 81 个 TU 或检查项的
   前提下把全量 `clang-tidy` 改为 8 路并行；[远端复验](https://github.com/lusipad/plcopen/actions/runs/29687354905)
   E2 由 22 分 17 秒降至 10 分 31 秒，Linux 总时长由 27 分 59 秒降至
   16 分 46 秒；v0.20.0 候选复验 Linux 为 16 分 29 秒。PR 分支现仅由
   `pull_request` 触发，不再与 feature-branch `push` 重复运行主门禁。
4. **A1 浮点数值语义合同（已完成）**：`805a363` 已向 header-only consumer
   传递严格浮点选项，并以[数值语义合同](doc/design/core/floating-point-semantics.md)
   和跨平台运行值测试钉死 FMA、舍入、特殊值与超越函数容差；
   [Windows CI](https://github.com/lusipad/plcopen/actions/runs/29688920738)、
   [Linux CI](https://github.com/lusipad/plcopen/actions/runs/29688920705) 与
   [三平台 Wheels](https://github.com/lusipad/plcopen/actions/runs/29688932738) 复验通过。
5. **无外部前置的软件队列**：v0.20.0、A2 T30 WCET、G1 治理与 X5
   executor IPC 软件形态均已收口。X5 保留 ADR-0007 进程内承诺环，只在
   `Servo` 边界新增固定 ABI SPSC + 状态双缓冲；Windows/Linux 两进程与
   Linux TSan 已在 [Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29873304328)
   远端复验，整轮 9/9 job 全绿。E5 基准趋势管线也已完成——指标由 Linux/GCC
   Release 同机 base/head 成对比较，结果保留 90 天且不回灌门禁，CI 只测
   本仓库、零外部 benchmark 依赖；[主干 Linux CI](https://github.com/lusipad/plcopen/actions/runs/29873287478)
   已完成首次 `record` bootstrap，随后 [PR #9 真实 base/head 比较](https://github.com/lusipad/plcopen/actions/runs/29874743112/job/88782685662)
   以 `mode=compare`、`verdict=pass` 闭环。AxisGroup
   第 1-5 批行为簇拆分已全部完成，`group.h` 7744→4487 行；共享调度边界按设计
   保留，不再作为该架构债挂账。H2 数值 IK 兜底已于 2026-07-23 交付
   （KB-089）：`SerialChain` 覆盖 1～8 轴 DH/modified-DH、严格失败分流与
   7DOF 偏好，Debug 实测低于 30 µs；L5 六轴 seam 不扩。下一无外部前置批次
   为 T2 MuJoCo/rerun 闭环孪生。
6. **F 轨 EtherCAT**：仍是最大的剩余软件块和商用指标 #1/#4 的上游；
   ADR-0006 许可证人工核验与台架决策完成后立即插队，按 F1→F2→F3 推进。
7. **完成面守护**：PLCopen/Beckhoff C0→C6 与 ST L0→L7、L∀ 已完成；只
   维护能力矩阵、特性表和限制注册表不漂移，不扩张到 IDE、Safety、CNC。
   商用门板已关闭的软件面保持不回退，台架、部署、认证和日历证据不以
   软件模拟冒充。

真机在手时 H1 修订稿送批并插队。这里的“对等”限于核心可编程运动能力，
不含 IDE、Safety、EtherCAT 产品化、CNC/G-code 与品牌私有扩展。

## 已完成里程碑：PLCopen 扎实化（2026-Q3，2026-07-12 归档）

收口项全部关闭（AxisGroup 第 1 批、executor 双域 ADR-0007、Pages 上线、
soak 周期等效）；`v0.20.0` tag / GitHub Release / PyPI 已于 2026-07-20
完成，后续版本仍需维护者明确授权，台架采购决策仍为人专属。下表保留为
交付记录。

2026-07-07 维护者定调"先把 plcopen 做好做扎实，其他领域自然继续"。
人形/EtherCAT/孪生三轨设计资产已完成并待命（见软件极致计划），实现
在本里程碑收口后接续。

| # | 任务 | DoD | 状态 |
|---|------|-----|------|
| 1 | Y7/Y7b1/Y7b2a 组接管修复 | 公差管语义 v2.4：linear + plain joint-domain circular 的解析承接，并允许 plain Cartesian LINE 来源以真实成员输出历史接到 joint LINE/circular；Cartesian 目标等其余形态留后续 Y7b2b | **已交付**（KB-051/086/087，30 个顶层测试函数） |
| 2 | 对抗性探测轮 | 组/FB 语义面系统扫描，发现即 KB/修复 | **已交付**（KB-052/053/054） |
| 3 | Y0 最优性 oracle（评审定序提前：先立标尺再修算法） | 切换结构枚举表（第一交付物）+ 双 oracle + Ruckig 黑盒对照，excess_cycles 分域基线入趋势 | **主 oracle 已交付**：678 结构表 + Newton 打靶 + 4 域基线；副 oracle / Ruckig 对照随 Y2 |
| 4 | Y2 完整 OTG（评审三关键） | state-to-state 任意目标状态（非零 at）+ 钉边界 + epsilon 政策声明化；Ruckig 黑盒对照 | **软件全交付**（KB-055 非零 at + 12 固定 + 50k fuzz；KB-057 epsilon 政策声明化；Y0 oracle 678 结构表 + 4 域基线）；Ruckig 对照待 ADR-0003 人工审批 |
| 4c | Y4 solve_fixed_time 一等原语（评审三关键，紧跟 Y2） | 同步 + cycle-exact 量化（删 KB-050 尾段补丁）+ 流追赶汇合同一求解；终态 ≤1e-9（T43） | **求解器 + KB-050 集成 + 流 rendezvous + Y4b 组同步切换已交付**（KB-056：8 候选族 + below_tmin multi-cubic；KB-050：匀速骑行/退避梯子删除；KB-035：流跟踪律 solve_fixed_time rendezvous 优先——38/38 pass；KB-088：[Y4b 组同步切换矩阵](doc/compliance/group-synchronized-switch-semantics.md)） |
| 5a | P-Part4 剩余 FB | 管理组矩阵 + 路径表/变换 + P4-B2/P4-B3/C3 已实现 | ✅ Part 4 v2.0 **68/68 同名门面齐全**（KB-078）；旧 Position 回读包装已在 C6 删除，接口/语义边界逐项登记，见 [完整条款审计](doc/compliance/plcopen-part4-clause-audit.md) |
| 5b | P-Part5 回零规程 | MC_Step* 标准回零步 FB 面（数字输入通道模拟验收） | ✅ **C5 软件合同关闭**（KB-080）：11/11 标准 FB、45 B + 102 E 机读声明及软件语义闭合；硬件/认证边界不冒充 |
| 4b | **信号通道并行项 + Z 系列全量**（拷问后拉入） | T1/Z1 wheel+notebook、Z2 SI 配置、Z3/Z3′ 三旅程+API、Z4 包管理、Z5 诊断；Z0 持续冷用户流程 | **仓库交付完成（2026-07-22）**：`v0.20.0` wheels/sdist 已发布；notebook、轴/组 SI 配置、Python/C++/ST 可执行旅程、Doxygen API、Conan 2/vcpkg overlay 消费门、ErrorCode 提示和 trace HTML 已闭环。Z4 中央 registry 仅提供经验证的提交资产，外部账号/PR/审核仍由维护者与上游完成；Z0 由 `Cold User` workflow 持续执行 |
| 6 | E 系列证据 | ARM64 CI + clang-tidy 零 P0 + 变异分数门 | **已复绿（2026-07-19 v0.20.0 候选复验）**：Mutation 20/20、Coverage 全核 line 95.5% / 运动栈 branch 85.0% / ST branch 85.1%、Core Nightly 7/7 job 通过 |
| 7 | 台架采购决策 | 下单或共建协议（S1 之门，与本里程碑并行） | **人工** |
| 8 | 72h soak 回写 | 保存完整结束日志并回写 DoD 表 | **周期等效口径关闭（2026-07-11）**：07-06 墙钟版证据链断裂如实登记；25.92 亿冻结周期（72h@1kHz ×10）Release 零分配 PASS 回写 DoD；墙钟 72h 归 B7。口径调整开放维护者复核 |
| 9 | Part 4 原文核对 | 坐标矩阵 3 项线下核实 | ✅ **已完成并升级为 🟢 AI 全自主**（2026-07-12）：规格 PDF 公开免费、`pdftotext` 可直读——**全面原文审计已出**（[plcopen-conformance-audit](doc/compliance/plcopen-conformance-audit.md)），推翻两条覆盖率声明 + 一条认证假设 |

**算法主线现状**：T24 quintic 快路径及 Y3 TOPP-RA 两层求解、executor/
oracle 已落库；主路径采用和任何语义变化仍需独立门禁。权威口径见
[algorithm-contracts](doc/design/core/algorithm-contracts.md)。

**明确不做**（本里程碑）：H/F/T 三轨实现（设计已备待命）、扭矩通道、
G-code、硬件验证（S1）。

## 已完成里程碑：L 系列语言层（2026-07-18 收口）

维护者于 2026-07-11 拍板启动自研 IEC 61131-3 语言层。ST-L0/L1a/L1b、
L2b/L2c、L3/L4/L5/L6/L7 与 L∀ 均已实现并通过完成态门禁；basic 10 +
Part 1/2 45 + Part 4 68 + Part 5 11 共 134 个 FB、1476 个 pin 已全部接入
native adapter，feature-set `pending=0`。批次证据与范围外项见
[L 系列工作拆解](doc/planning/l-series-work-breakdown.md)，后续只按已批准
语义维护，不把 IDE、完整 IEC 平台或 PLCopen 官方认证倒推为已完成。

## PLCopen 合规补齐（2026-07-12 原文审计后重构）

原文审计推翻旧覆盖率声明并发现**认证不是黑箱**（自声明填表 + 提交 +
批准，无测试套件；"一个或多个 FB 即可合规"，关键是 **B 级 I/O 齐备**）。
详见 [合规补齐计划](doc/planning/plcopen-conformance-plan.md)。

| 批 | 内容 | 量级 | 出口 |
|----|------|------|------|
| **P1-A** | Part 1 首批结构性缺口 4 项（MoveAbsolute.Direction / SetOverride 电平化 / Phasing 主从双轴 / DigitalCamSwitch 完整化） | 0.5 L0 | **完成**；其余 D 项已由 C4/KB-079 清零 |
| **L2a**（矩阵重写） | 引脚表改以规格附录 B3 的 B/E 表为准——**L2a 就是合规面本身**；机读引脚表 → 自动生成合规声明 | 0.5 L0 | **首批绑定完成（KB-071）**；完整 ST MC 面与正式提交材料仍后续维护 |
| P5-B | Part 5 缺 6 项（StepBlock/DistanceCoded/HomeAbsolute/飞越式×2/AbortPassiveHoming） | 0.5 L0 | **完成（KB-072）**；后续 C5/KB-080 已关闭 11/11 软件合同 |
| **P4-B1** | Part 4 第一波缺口（组参数/动态/SW限位/回读/运动学信息，精确 19 项） | 1 L0 | **完成（KB-073）**：40/68 有同名门面，28 项仍缺失 |
| P-GUIDE / P-OFFICIAL | 对照全部 35 个已取回 PLCopen 官方技术文件：指南、OOP/Annex F、Safety、OPC UA、XML/TC6 | 0.3 L0 | ✅ **2047 页全文审计完成**；G/M/L/S/U/X 缺口已登记，门控领域未进入实现 |
| **P4-B2** | 工具/载荷 8 项 + 点动 2 项 | 0.8 L0 | **完成（KB-076）**：工具真实接入 TCP，载荷库与 ACS/MCS/PCS Jog 已交付；50/68 同名门面 |
| **P4-B3** | 同步/刚体动力学/跟踪 | 0.7 L0 | **完成（KB-077）**：axis↔group 双向同步、刚体动态与动态 PCS 跟踪已交付；57/68 同名门面 |
| **C3 / P4-B4** | 最后 11 个管理/变换/位置/Halt/Wait 门面 | 0.8 L0 | **完成（KB-078）**：68/68 同名门面；power-owner、queued transform/moving set-position 与非 Cartesian ref 仍为显式边界 |
| **C4 / P1-P2** | D-01～D-20 生命周期、状态、错误与持续控制语义清零 | 1 L0 | **完成（KB-079）**：20/20 关闭；正式 B/E/V 供应商声明与 PLCopen 认证仍未完成 |
| **C5 / Part 5** | 标准名称、派生类型、I/O 与可软件验证回零语义 | 1 L0 | **完成（KB-080）**：11/11、45 B + 102 E |
| **C6 / 对等验收** | Beckhoff 核心能力矩阵、迁移与限制一致性 | 0.3 L0 | **完成（KB-081）**：implemented/partial/excluded 总账；无兼容层 |

**人专属待裁**：OOP（排期/门控/永不）· OPC UA 触发条件（现为死锁条件）
· 向 PLCopen 确认提交是否需会员资格 · 合规声明提交动作。

## 商用门板穿插队列（2026-07-12 CEO 评审拍板，硬承诺非候选）

商用八项硬指标中四个纯软件可关切片均已关闭；下表保留交付记录，并登记
不会直接关闭硬指标、但仍需兑现的软件合同。当前执行顺序只以“当前承诺”
节为准：

| 批 | 内容 | 量级 | 关的门 |
|----|------|------|--------|
| **P#8** | STO/SS1 集成边界文档（不做什么/边界线/集成商责任/CiA402·STO 交互口径） | 0.1 L0 | **完成（2026-07-13）**：指标 #8 的诚实集成边界全闭；STO/SS1 实现与认证仍硬门控 |
| **P#5** | 分支覆盖首测 → 公开合同补盲 → workflow 加 85% 分支门 | **完成（2026-07-14）**：固定生产运动栈 73.4% → 85.0%（6004/7062，精确 85.006%），同提交启用 `--fail-under-branch 85` | 指标 #5 软件门关闭 |
| **P#2** | 精度证据套件：Bezier/圆弧稳速波动 <0.1% + cam 相位 <1 周期 + blending 汇总断言 + 八项证据总账 | **完成（2026-07-15）**：0.0775% / 约 7.6e-12% / 0 拍 / 公差利用率 100%；KB-075 声明升级 65 点弧长表 | 指标 #2 软件面关闭；总账见商用证据页 |
| **P#7** | 文档四件套 + 运维手册（全 FB 参考/实时集成/调优/TwinCAT·CODESYS 迁移/故障处置） | **完成（2026-07-15）**：5 页指南接入文档站，严格构建通过 | 指标 #7 软件文档主体关闭；现场/认证材料仍按总账管理 |
| G1 | 治理文档批：CONTRIBUTING + GOVERNANCE（决策/继任声明）+ SECURITY.md（含 ST 不可信输入面威胁模型与资源上限声明）；GitHub org 迁移列人专属 | 0.2 L0 | **完成（2026-07-21）**：三份根文档、公开入口和 issue 对齐点同步；bus factor=1、无指定继任者、无 SLA/LTS，私密漏洞报告/org 迁移保留为人侧动作 |
| A1 | 浮点数值语义合同：编译旗标钉死（FMA 收缩/舍入）+ 跨平台运行值容差/禁止项 + 双平台运行值对照测试 | 0.3 L0 | **完成（2026-07-19，`805a363`）**：严格选项传递到 consumer，Windows/Linux/ARM64 数值测试与三平台 Wheels 通过；[合同](doc/design/core/floating-point-semantics.md) |
| A2 | T30 WCET 度量工具：每指令成本表 + 静态无环路径长度 + 调用深度上界（compile 产物附带） | 0.2 L0 | **完成（2026-07-20）**：87-opcode 预算/时间分类、无分配机读报告、对象输入 exact-budget 修复与 Release 环境化 `observed_*` 校准；不改变 VM budget，不声称 certified WCET；[合同](doc/compliance/st-wcet-semantics.md) |
| X5 | executor ↔ fieldbus IPC 软件形态：固定 ABI Servo setpoint/feedback SPSC + 状态双缓冲；默认纯内存门、显式两进程门 | 0.2 L0 | **完成（2026-07-22）**：owner 映射期不可转让、NaN/Inf 整帧拒绝、Windows/Linux 父子进程与 Linux TSan 本地及 [Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29873304328) 远端均通过；[合同](doc/compliance/executor-ipc-semantics.md) |

**2026-07-12 生态调研追加裁决**：竞品 Intel RTmotion 确认存在（Apache
2.0、活跃、Part 1/2、零 Part 4）——VISION 竞争表已校准，"生态位无人"
作废；**CI 只测我们自己**（E5 趋势管线常驻，零外部依赖），**竞品对拍
（E6）为手动报告批不进 CI**，三条铁律见软件极致计划 E 系列表；**R 系列
（真硬件，锚 LeRobot/SO-ARM100）立案**，维护者已确认采购设备；
**Feetech STS adapter**（SO-ARM100 舵机总线）语义矩阵已批准
（2026-07-12），S2 实现已排期。

另两条 CEO 评审裁决（2026-07-12）：L2b 的采纳信号检查点现转为 L 系列
闭合后的独立观察项，不反向改写 L3-L7 已完成事实；信号不足时调整后续
D/H/F 候选优先级。**main 强制门禁（branch protection + required checks）挂 S1 入口条件**
（灯塔接触前上，现阶段保持直推效率）。

## 其他候选（复盘时定）

候选清单已立案为[软件极致计划](doc/planning/software-excellence-plan.md)
（Y 算法 / P 标准面 / Z 采纳 / E 证据四线 + **2026-07-12 计划补遗**：
Y7b1 circular 接管（已完成）/ Y7b2a Cartesian LINE 来源桥接（joint LINE/circular，已完成）/
Y7b2b Cartesian 目标接管（固定 fraction A 与 B v0 `K=6` 已 NO-GO；`K=40` 仅留候选压缩/holdout/WCET planning spike）/ Y4b 组同步切换（已完成）/
Y3′ 影子换主 / X5 executor IPC 形态（已完成）/
E5 基准趋势管线 / Z0′ 冷用户首轮（已完成）/ Z3′ 文档站内容）；语言层批次的可
执行拆解见 [L 系列工作拆解](doc/planning/l-series-work-breakdown.md)
（含 pyplcopen ST 暴露、语言层探测轮等并行项）。已完成项与当前缺口
以本页表格为准。此外：

- **250µs 档启用**：预算证据已备（KB-044：占比 ~1-2%），随硬件阶段评估
- 硬件阶段任一触发项到位则优先于软件线

## 全景总图

剩余工作的完整账（含 AI 能做/不能做的分栏、依赖链、人侧杠杆排序）见
[**主计划**](doc/planning/master-plan.md)——复盘第一入口。**一句话**：
L 系列、四项商用软件门与 X5 关闭后，剩余软件存货重估约 8 L0；F 轨
EtherCAT 仍是最后一块大软件且是 #1/#4 的前置；**存货耗尽那天，项目速度
= 维护者的速度**——所以
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

*最后更新：2026-07-22（Y4b 与 Z0′ 完成证据同步）*
