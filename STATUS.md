# STATUS — 项目现状（单页）

> 本页是"现在在哪"的唯一入口，每个批次收口时更新。术语见
> [CONTEXT.md](CONTEXT.md)；边界细节见
> [已知边界注册表](doc/compliance/known-boundaries.md)。
> 最后更新：**2026-07-24**。

## 一句话

新核 `core/` 主体能力已落地（R0-R4 重写 + Phase B 纯软件 + 位姿闭环 +
软件收尾批，KB-034~081）。`v1.0.0-alpha`（2026-07-06）保留为历史实验性
预览，当前版本线已校准回 pre-1.0，**v0.20.0 已于 2026-07-20 正式发布**：
[GitHub Release](https://github.com/lusipad/plcopen/releases/tag/v0.20.0) 与
[PyPI](https://pypi.org/project/pyplcopen/0.20.0/) 均已上线。Part 4 管理/路径表/变换 + Part 5
回零 FB 已交付。2026-07-19 候选门禁 Windows/Linux/Wheels/Nightly/
Coverage/Mutation/Docs 全通过。**中英双语文档站已上线**，本批
[Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30035093781)
全绿
（https://lusipad.com/plcopen/ ，Pages 2026-07-11 启用）。架构、合规、
边界、贡献、治理、安全与变更历史已进一步整合为
[中文项目文档](https://lusipad.com/plcopen/project/) 与
[英文项目文档](https://lusipad.com/plcopen/en/project/)；
[本批 Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30054317413)
全绿并通过 21 对页面校验。ST 运行时、标准证据与 ADR 已继续整合为 3 对
双语技术参考页，五个既有英文 URL 的中文正文已修正，可见语言选择器保持
当前深层路径；[Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30060185662)
全绿并通过 24 对页面、Doxygen 拼装与 Pages 发布，公开参考页及两份 sitemap
均返回 200。**executor
双域已落地**（ADR-0007：规划域产帧、RT 域仅消费承诺轨迹，TSAN 零
报告）。S0 纯软件发布准备与发布执行均已收口；tag `v0.20.0` 固定到
`7788a85`，tag 工作流已通过 Trusted Publishing 发布 20 个 wheels 与 1 个
sdist。
S1-S3 另依赖硬件、用户和日历时间。**L 系列语言层已闭合（2026-07-18）**，
批次 ST-L0（KB-069）、ST-L1a（标量宇宙 + 转换矩阵机读化，KB-070）与
ST-L2a-Bind 首批十个单轴 MC 块（KB-071）均已交付；Part 5 P5-B 六个缺失
门面（KB-072）已补齐；Part 4 P4-B1 十九项管理/回读门面（KB-073）与
Feetech STS S2 纯软件协议层（KB-074）已交付。Part 4 P4-B2 工具/载荷库
与 ACS/MCS/PCS 点动（KB-076）已交付；P4-B3 的 axis↔group 同步、刚体动力学
与动态 PCS 跟踪（KB-077）已交付；C3 最后 11 个管理、变换、位置与
Halt/Wait 门面（KB-078）已交付，达到 68/68 有同名门面；C4 Part 1/2
语义清零（KB-079）已交付，D-01～D-20 全部关闭；C5 Part 5 标准合同
（KB-080）已交付，11/11 标准 FB 与 45 B + 102 E 软件声明闭合；C6
能力对等验收（KB-081）已完成，核心能力逐项登记且不保留旧软件兼容层。
ST 语言层随后完成 L1b1/L1b2/L1b3、L2b/L2c、L3/L4/L5/L6/L7 与 L∀ 闭合：
`GROUP_REF`、basic 10 + Part 1/2 45 + Part 4 68 + Part 5 11 共 134 个 FB、
1476 个 pin 已由显式 authority 生成并全部接入 native adapter，未解析项为 0。
Feetech 4.8 未核，不进真机。
商用门板 P#8 的 STO/SS1 集成责任与宣传边界已锁定，但安全实现/认证未解锁。
P#5 分支覆盖门、P#2 轨迹精度软件证据和 P#7 文档主体也已关闭；八项硬指标
当前关闭 4/8，统一证据见[商用级八项证据总账](doc/compliance/commercial-gate-evidence.md)。
**Z 系列仓库内收口已完成（2026-07-22）**：冷用户持续门、可执行 notebook、
轴/组 SI 配置、Python/C++/ST 三条主旅程、Conan/vcpkg 消费者资产、结构化
错误诊断与单文件 trace 可视化均已落地；ConanCenter/vcpkg 中央仓库收录仍是
外部维护者流程，不作为本仓完成状态冒充。
**H2 数值 IK 已交付（2026-07-23，KB-089）**：`kin::SerialChain` 提供
1～8 轴 standard/modified DH 正解、确定性自适应 DLS 逆解、严格失败分流与
7DOF 零空间偏好；解析 6R 仍优先，L5 `AxisGroup` 六轴位姿 seam 本批不扩。
**T2a 闭环孪生已完成（2026-07-24，KB-090）**：可选
`pyplcopen[twin]` 把既有流 setpoint 接入固定 1 kHz MuJoCo 物理步，反馈
回灌 actual/readback，并以 Rerun 记录 headless `.rrd`；默认 wheel/core
依赖不变。CI 只用本仓 primitive 双关节模型，真机保真、安全、标定与
sim2real 均不在声明范围；[PR #16](https://github.com/lusipad/plcopen/pull/16)
的 Windows/Linux Twin integration 与主门禁均已远端复验。

## 历史刻度（处于哪一步）

| 阶段 | 窗口 | 落点 |
|------|------|------|
| v0.x 旧线（fork 自 i5cnc） | 2026-04 → 07 | Part 1/2 FB 面 45/45 收口于 v0.11.0，冻结为回放/迁移基线 |
| R0-R4 新核重写 | 2026-07 | `core/` L0-L7 阶梯 + kin/stream 支撑库全部落地，与旧线 DoD 对照 PASS |
| Phase B 纯软件 | 2026-07 | 坐标系/kinematics/轨迹流/cam/前瞻 v2/adapters（KB-034~041） |
| **← 现在** | 2026-07-20 | v0.20.0 已正式发布；PLCopen / Beckhoff 软件收束 C0～C6 与 ST L0～L7、L∀ 已完成；134/134 FB、1476/1476 pins、feature-set `pending=0`，逐项对等状态见 [能力矩阵](doc/compliance/plcopen-beckhoff-parity-matrix.md) |

## 能力面（新核，默认消费面 `plcopen::plcopen`）

结构三段：**阶梯 L0-L7**（依赖只向下）、**支撑库 kin/stream**（阶梯旁、
依赖只向内、被 L5 消费）、**外圈消费面 st / L7 adapters**（两条平行的纯
sink 门面，生产层无反向引用）。分层健康度见
[2026-07 架构审查](doc/design/architecture-review-2026-07.md)。

| 层 | 能力 | 关键 KB |
|----|------|---------|
| L0/L1 rt·otg | 整型周期时间、定长容器、SPSC；时间最优 jerk-limited OTG（任意初速/非零初始加速度，鼓包域已修） | KB-026/034 |
| L2/L3 geom·plan | 直线/三点圆弧/Bezier/刚体帧（平移+绕Z+完整 RPY 原语）；路径缓冲、公差带 blending、前瞻窗口（jerk 精确可达扫描） | KB-030/031/039 |
| L4 exec | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表、经典规律生成器）、叠加 | KB-038/046 |
| L5 axis | 单轴全命令生命周期、组共享路径（2-8 轴）、前瞻窗口执行、坐标系栈（ACS/MCS/PCS + 工件帧/工具偏置）、kinematics 级联（消费支撑库 kin：龙门/SCARA）、位姿管线（RPY + 6R，TCP 工具变换）、笛卡尔/位姿回读（含 RPY 反演万向节约定）、段内笛卡尔插补（直线/圆弧/blending + 前瞻窗口，逐周期逆解 + 测地姿态，opt-in；腕奇异可穿越）、窗口深度可配、双空间限速、B9 流会话（消费支撑库 stream） | KB-035/036/037/041~050 |
| L6 fb | **Part 1 v2.0：43/43 门面，C4（KB-079）关闭 D-01～D-20；正式 B/E/V 供应商声明仍未闭合。Part 4 v2.0：68/68 同名门面；统一使用 v2 `FbGroupReadPosition(Source)`，不保留旧 Position 回读包装；power-owner、queued transform/moving set-position、非 Cartesian ref、buffered dynamic PCS 与部分 E/O 字段仍显式受限。Part 5 v2.0：C5（KB-080）完成 11/11 标准 FB、45 B + 102 E 机读声明与可软件验证语义。**这些事实不等于 PLCopen 官方合规或真机性能证明；C6 逐项状态见 [能力对等矩阵](doc/compliance/plcopen-beckhoff-parity-matrix.md)。Part 6 的 5 个 FB 全部门控未实现。 | 全文审计见 [总账](doc/compliance/plcopen-conformance-audit.md) 与各专项矩阵 |
| L7 adapters | **外圈消费面之一**（绕过 L6，只消费 axis/state.h + rt/error.h）：Servo 窄接口 + ServoSim + 桥接（ADR-0004）、CiA402 状态机、CSP/CSV/CST bumpless 骨架、Feetech STS 协议 0 固定容量总线/Servo/Sim（无 IO；动态单位与 Status 位未核，不进真机） | KB-040/074 |
| 支撑库 kin | 阶梯旁支撑库（依赖 geom/rt，被 L5 消费）：kinematics 插件 ABI + 合规 harness、龙门/SCARA 解析解、球腕 6R（Pieper + 8 分支 seed 选支、奇异 margin）；H2 固定容量 1～8 轴 DH/modified-DH `SerialChain`（数值雅可比 + 自适应 DLS + 7DOF 偏好，严格/best-effort 分流） | KB-037/041/089 |
| 支撑库 stream | 阶梯旁支撑库（依赖 otg/rt，被 L5 消费）：B9 轨迹流滤波（OTG 在线重解、断流看门狗、solve_fixed_time rendezvous 跟踪律）、多关节聚合 | KB-035 |
| st 语言层（ST L0-L7、L∀） | **外圈消费面之一**（与 L7 adapters 平行、居 L6 之上，纯 sink）。IEC 61131-3 ST：容错前端、确定性字节码 VM、标量/枚举/子范围/聚合/字符串日期类型、用户 POU 与 134 个标准 FB 完整绑定；49 个公开绑定类型、AXIS/GROUP/序列/对象 typed registry 均只向 ST 暴露 1-based handle。L3-L7 已完成并按 L 系列总账闭合；仍不宣称完整 IEC 平台或 PLCopen 官方认证 | KB-069/070/071；[L 系列总账](doc/planning/l-series-work-breakdown.md) |
| 工具面 | pyplcopen（单轴/流/PoseArmSim，`0.20.0` 的 Windows/Linux/macOS wheels + sdist 已发布到 PyPI，CycleConfig 与轴/组 SI 配置；当前源码另有可选 `twin` MuJoCo/Rerun 闭环）、可执行 notebook、Python/C++/ST 三条 30 分钟旅程、18 份回放黄金语料、28 关节 @1kHz 预算基准 + 笛卡尔 IK 预算门、参考 executor demo（canonical `planning → committed trajectory → RT` 双域，ADR-0007，TSAN 零报告）、X5 固定 ABI Servo IPC（默认纯内存合同 + 显式 Windows/Linux 两进程 harness）、周期级 trace + 单文件 HTML/SVG、**[中文默认](https://lusipad.com/plcopen/) + [`/en/` 英文](https://lusipad.com/plcopen/en/)的双语文档站**、已通过消费者预检的 Conan recipe 与 vcpkg overlay port（中央 registry 尚未收录）、结构化 ErrorCode 诊断 | — |

## 质量门禁现状

- 测试：Z 系列分支当前 Windows Debug 配置登记 98 项 CTest，其中日常门运行 87 项非 fuzz 测试；2026-07-22 本地全量 98/98（含 11 fuzz）通过，新增 notebook、Python SI/诊断、ST 文档同源旅程与 trace HTML 端到端门。Y4b 的 Y7 定向门含 31 个顶层场景。X5 [executor IPC 合同](doc/compliance/executor-ipc-semantics.md)的 Windows 显式进程门 2/2、Linux/GCC 2/2 与 Linux TSan 纯内存并发门均通过；默认配置确认不注册进程测试，[Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29873304328) 又完成 Windows/Linux 两进程与 Linux TSan 远端复验，整轮 9/9 job 全绿。A2 [WCET 软件度量合同](doc/compliance/st-wcet-semantics.md)新增 87-opcode 成本/时间分类、无分配机读报告与 Release 环境化观测，且不形成 certified WCET 声明。E5 [基准趋势管线](doc/design/benchmark-trend-pipeline.md)已完成四类机读指标、严格零依赖比较器和 Linux 同机 base/head report-only 比较；Linux 自对拍 9/9 对 PASS，[主干 Linux CI](https://github.com/lusipad/plcopen/actions/runs/29873287478) 首次 `record` bootstrap 成功，[PR #9 真实比较](https://github.com/lusipad/plcopen/actions/runs/29874743112/job/88782685662) artifact 确认 `mode=compare`、`verdict=pass`、base=`78d287c`。2026-07-19 候选 Coverage Gate 实测全 `core/` line **95.5%（43925/45983）**、固定生产运动栈 branch **85.0%（9184/10811）**、`core/st` branch **85.1%（14187/16663）**，均达到硬门。A1 [浮点数值语义合同](doc/design/core/floating-point-semantics.md)已在 Windows、Linux GCC/Clang 与 ARM64/QEMU 复验。P#2 聚合门实测 Bezier 稳速波动 0.0775%、圆弧约 7.6e-12%、cam 相位 0 拍、blending 公差利用率 100%。P#7 五页指南与运维手册已接入文档站，`mkdocs build --strict` 通过。精度总账见[商用证据](doc/compliance/commercial-gate-evidence.md)，覆盖口径见[分支覆盖基线](doc/compliance/branch-coverage-baseline.md)
- 回放：18 语料逐周期比对；声明变更零例外流程运行中
- 分层：2026-07-12 include 图审计 **0 违规**（L0-L4 零 PLCopen 语义引用、无循环依赖、L4 不引 L3），见 [架构审查报告](doc/design/architecture-review-2026-07.md)
- RT：静态扫描 31 文件（含 st vm/bind、Feetech adapter、X5 OS-free IPC 原语、Z 系列 ST 旅程与 H2 SerialChain）+ 冻结窗口分配断言；DoD §5.3 的周期耗时对比通过（新核 = 旧线 9.3%）；分配 soak 以周期等效口径关闭（25.92 亿周期零分配，2026-07-11），墙钟 72h/抖动证据归 B7 真机报告
- CI（v0.20.0）：候选 `5a5cf81` 的 [Windows CI](https://github.com/lusipad/plcopen/actions/runs/29692190516)、[Linux CI](https://github.com/lusipad/plcopen/actions/runs/29692190521)、[三平台 Wheels + sdist](https://github.com/lusipad/plcopen/actions/runs/29692204896)、[Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29692206177)、[Coverage Gate](https://github.com/lusipad/plcopen/actions/runs/29692207269)、[Mutation Score Gate](https://github.com/lusipad/plcopen/actions/runs/29692208327) 与 [Documentation](https://github.com/lusipad/plcopen/actions/runs/29692209416) 全部通过；最终 tag 提交 `7788a85` 的 Windows/Linux/Documentation 主线门禁 0 annotations，[tag Wheels/PyPI run](https://github.com/lusipad/plcopen/actions/runs/29709944703) 5/5 job 全绿。合并提交 `78d287c` 的 [Windows CI](https://github.com/lusipad/plcopen/actions/runs/29873287504)、[Linux CI](https://github.com/lusipad/plcopen/actions/runs/29873287478)、[Documentation](https://github.com/lusipad/plcopen/actions/runs/29873287492) 与新版 [Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29873304328) 也全部通过；Nightly 9/9 job 全绿。11 项 fuzz 只进入 Nightly；A2/X5 新增专项后，普通 PR 当前跑 82 项非 fuzz 测试。PR 分支不再同时触发 push 与 pull_request 两套 Windows/Linux 主门禁。`main` 仍无 branch protection/ruleset，这些是 workflow 证据而非技术强制的 required checks。触发边界见 [CI gate 矩阵](doc/compliance/ci-gates.md)

## 进行中 / 待办

| 项 | 状态 |
|----|------|
| **v0.20.0** | **已发布（2026-07-20）**：annotated tag 固定到 `7788a85`；GitHub Release 为 Latest、非 prerelease；PyPI 已上线 20 个 wheels 与 1 个 sdist，公开 wheel 下载校验通过 |
| **CI 反馈时长** | **P1 已完成，候选复验稳定**：普通 PR 不跑 fuzz 且不重复触发两套主门禁；Windows 13:45，Linux 16:29，重型 sanitizer/fuzz 留在 20:56 的 Nightly |
| 抽查评审 | 2026-07-05 批次核心提交（OTG/流/kin/adapters）开放抽查，证据链在各提交信息；非合入门槛 |
| 旧 `src/` v0.11 线 EOL 窗口 | v1.0.0-alpha 实验预览已发布（2026-07-06）；旧 `src/` 线 90 天 P0-only 窗口至 2026-10-04，不影响新核采用 v0.20.0 版本号 |
| 硬件阶段（B5 真栈/B6 台架/B7 RT 报告） | 等台架或灯塔环境；参考 executor 双域已落地（ADR-0007，TSAN 零报告），真机测量链待硬件 |
| 72h 分配断言 soak | **周期等效口径关闭（2026-07-11）**：07-06 墙钟版证据链断裂（无结束日志，如实登记）；改交付 2,592,000,000 冻结周期（72h@1kHz ×10）Release 零分配 PASS；真实 72h 墙钟连续运行归 B7 真机 RT 报告。口径调整开放维护者复核 |
| **KB-051/086/087/088 组接管** | **Y7/Y7b1/Y7b2a/Y4b 已修复（2026-07-22）**：plain ACS joint-domain linear/circular 的 aborting 接管均按实时成员状态进入有界公差管；plain Cartesian LINE 来源也可用统一 odometer 的真实成员输出历史连续接到 plain joint-domain LINE/circular 目标，不扩 kinematics ABI。vector connector 的全部非零 residual 现以共同 `T_sync=max(T_min[i])` fixed-time 重解，同拍汇入共享标量路径；GroupStop 仍独立制动。Y7b2b 固定 fraction A 与 B v0 `K=6` 均已 NO-GO；`K=40` 因成本、覆盖与 holdout 缺口只保留 planning/WCET spike。Cartesian 目标默认继续 rest-start，未获生产实现授权；Cartesian ARC/chain/window 来源和动态 PCS/tracking 仍不消费该 bridge |
| Part 4 管理/路径表/变换 FB | **已交付**（GroupHome/MoveDirect/GroupSetOverride/GroupInterrupt·Continue + PathSelect/MovePath/SetKinTransform/ReadCartesianTransform，验收测试已接入 CTest） |
| Part 5 回零 FB | **C5 软件合同已关闭（KB-080）**：11/11 标准 FB、45 B + 102 E 机读声明与软件语义均有测试；真机堵转、编码器、多圈、时间戳、正式批准仍是独立边界 |
| A1 浮点数值语义合同 | **已完成（`805a363`）**：严格编译选项传递、运行环境禁止项、跨平台基本运算与超越函数容差均已合同化并远端复验 |
| A2 T30 WCET 度量 | **已完成（2026-07-20）**：87-opcode 版本锚定成本表、静态无环工作量与调用/实例深度、无分配 `WcetReport`、原生 FB/线性字节校准要求和 Release `observed_*` 基准均已落地；`fb_store_object` 低报已修，VM budget/字节码不变，真机 certified WCET 明确不声明 |
| G1 治理文档 | **已完成（2026-07-21）**：[贡献流程](CONTRIBUTING.md)、[单维护者决策/继任声明](GOVERNANCE.md)与[安全政策](SECURITY.md)已落地；ST 不可信输入面和默认资源上限公开，bus factor=1、无指定继任者、无 SLA/LTS 如实声明；私密漏洞报告与 GitHub org 迁移仍为人侧动作 |
| E5 基准趋势管线 | **已完成（2026-07-22）**：OTG 完整样本账 + ST 混合指令成本 + 笛卡尔 IK + 64 段窗口重规划统一入 schema v1 JSON；同 runner 9 对 AB/BA、二次确认、90 天 artifact 与只读 report-only PR 边界已接入 Linux workflow。本地工具 15/15、Windows/Linux record、Linux 自对拍通过，[主干首次 bootstrap](https://github.com/lusipad/plcopen/actions/runs/29873287478) 与 [PR #9 真实 base/head 比较](https://github.com/lusipad/plcopen/actions/runs/29874743112/job/88782685662) 均成功，artifact 为 `mode=compare`、`verdict=pass` |
| X5 executor IPC | **已完成（2026-07-22）**：固定 ABI Servo setpoint/feedback SPSC + 状态双缓冲、一次性 owner claim、整帧 NaN/Inf 拒绝与完整 lifecycle 已落地；默认纯内存门、Windows/Linux 两进程和 Linux TSan 本地通过，[Core Nightly](https://github.com/lusipad/plcopen/actions/runs/29873304328) 9/9 job 全绿并形成两平台进程/TSan 远端证据；不声称恶意 peer 隔离、DC/真栈/硬实时或许可证结论 |
| Z0′ 冷用户首轮 | **已完成（2026-07-22）**：隔离环境不使用本地源码或 `PYTHONPATH`；Linux CPython 3.13 强制 binary wheel 安装、Windows CPython 3.14 sdist 构建与 README stream 旅程通过，Python 指南 single-axis/SI/stream/pose/cam 五组示例全部跑通。文档已说清 3.14+ 本地构建工具链，SI 示例不再用停稳后零速冒充配置速度；见 [实施记录](doc/planning/z0-cold-user-test-implementation-notes.md) |
| **Z1～Z5 软件极致收口** | **已完成（2026-07-22）**：可执行 notebook；轴/组 SI 配置及 Python 绑定；Python/C++/ST 三条同源旅程与 Doxygen API；Conan/vcpkg 双消费者资产；全量 ErrorCode 结构化诊断和 trace HTML/SVG。Windows Debug 98/98、Conan 两消费者、vcpkg 严格 port、MkDocs strict、Doxygen/Graphviz、RT scan 与 18/2409 replay 均通过；PR #12/#13 全门绿色，主干 Pages build/deploy 通过，[Cold User 公开入口复验](https://github.com/lusipad/plcopen/actions/runs/29920696958) 29 秒全绿。外部中央 registry 收录仍按上游维护者流程跟踪 |
| **H2 数值 IK 兜底** | **已完成（2026-07-23，KB-089）**：1～8 轴转动关节 `SerialChain`、standard/modified DH、数值雅可比 + 自适应 DLS、32 次总上限、三类失败码、显式 best-effort 与 7DOF 零空间偏好均已落地；6DOF/非退化 7DOF 各 2 万例回环、固定 FK/SO(3) oracle、带偏好冻结窗口零分配，preference-enabled Debug hot-seed 通过 30 µs 专项门。解析 6R 仍优先，L5 位姿 seam 仍限六轴 |
| **T2a MuJoCo/Rerun 闭环孪生** | **已完成（2026-07-24，KB-090）**：2,000 tick/2.0 s、双轴末误差 ≤0.02 rad、command 确定性、feedback 单位换算、断流反例、错误模型/覆盖保护与 `.rrd` footer 均有可执行测试；默认依赖不变，外部模型只接本地路径。[Twin integration](https://github.com/lusipad/plcopen/actions/runs/30024648555) 的 Windows/Linux 与 [Windows](https://github.com/lusipad/plcopen/actions/runs/30024648191) / [Linux](https://github.com/lusipad/plcopen/actions/runs/30024648125) 主门禁均通过 |
| AxisGroup 架构债 | **第 1-5 批全部完成（2026-07-21）**：connector、joint look-ahead、Cartesian path/window、frame/pose/kinematics 与 MoveDirect lifecycle/path 各自形成明确 owner；第 2-5 批累计 75 个 `AxisGroup` 方法原样迁出，`group.h` 7744→4487 行；93/93、RT scan、18/2409 replay 与 find_package/FetchContent consumer 全绿。共享 queue/status/error 与 management 调度按设计保留在 `AxisGroup` |
| Y2 epsilon 政策 | **已声明化**（KB-057：段时长钳零 + 复验，整数量化天然覆盖） |
| Y2 Ruckig 对照 | **人工门控**（ADR-0003：需先审查上游许可证，不进 R1） |

## 近期计划

见 [ROADMAP.md](ROADMAP.md)（当前承诺）、
[L 系列工作拆解](doc/planning/l-series-work-breakdown.md)（语言层批次
主线与并行项）、[软件极致计划](doc/planning/software-excellence-plan.md)
（Y/P/Z/E 候选 + 2026-07-12 计划补遗）与
[长期规划第 7 稿](doc/planning/long-term-plan.md)（S0-S3 商业化关键路径）。
