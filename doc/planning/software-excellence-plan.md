# 软件极致计划（Y/Z 系列，2026-07-06 立案）

> **活文档声明（2026-07-07 拷问后）**：本计划与难点账本是**参考资料**
> ——唯一的操作性活文档是 ROADMAP 里程碑表；批次收口只强制更新
> ROADMAP/STATUS/KB，本文只在复盘时修订（防计划表面积腐烂）。
>
> 性质：**候选批次清单**（复盘时定序），不是承诺——承诺看
> [ROADMAP](../../ROADMAP.md)。背景：v1.0.0-alpha 已发布、软件已立项
> 批次全部清零（KB-001~050）后，维护者定调"做到软件的极致"。
> 极致的定义按四条线各给可验证终点；每批仍走语义矩阵批准流程。

## 极致的可验证定义

| 线 | 终点标准 |
|----|---------|
| 算法质量（Y） | 声明的算法性妥协清零；与理论最优的差距**有度量**且收敛；关节/笛卡尔管线完全对称 |
| 标准完整（P） | Part 4 每个 FB 要么承接、要么显式声明不做——对照表零留白 |
| 工程证据（E） | ARM64 三平台 CI、clang-tidy 零 P0、变异分数进门禁、覆盖率 ≥90% |
| 采纳体验（Z） | `pip install pyplcopen` 五分钟数字孪生；文档站可搜；C++ 包管理器可装 |

## 优先序（2026-07-07 维护者定调：先把 PLCopen 做好做扎实，其他领域自然继续）

> 当前主轴 = **PLCopen 域扎实化**：Y7 正确性修复 → P 系列标准面清零
> （Part 4 剩余 + Part 5 回零）→ 对抗性探测轮（组/FB 语义面系统扫描，
> KB-051 同族猎杀）→ **Y0 oracle（先立标尺）→ Y2 完整 OTG →
> Y4 定时同步（评审三关键，提入本里程碑）** → P 系列 → E 系列 →
> 收口 → Y3 TOPP。权威合同：algorithm-contracts.md。
> H/F/T 三轨的**设计资产已全部完成**（难点 T13-T29、ADR-0005/0006、
> H1 十二决策修订稿、快路径/oracle 设计）——设计不过期，实现在
> PLCopen 扎实化里程碑收口后自然接续。

## 三轨设计资产（2026-07-06 完成，实现待命）

> "现在人形机器人是真的非常重要的时候，务必优先；EtherCAT 这条要
> 走通；数字孪生我们都需要——都是软件层面上真正要解决的问题。"
> 以下三轨优先于 Y/P/Z/E 的默认序（Y7 正确性修复仍插队最前）。
>
> **规划先行（2026-07-06 维护者定调"先规划设计，不急写代码"）**：
> 难点分析 long-term-plan T13-T22、设计总图
> [priority-tracks-design](../design/core/priority-tracks-design.md)、
> 架构裁决 ADR-0005/0006 均 Accepted（0006 的 GPL 分发口径待人核验）。实现在里程碑接续时启动。

### H 轨：人形机器人（确定性关节执行层的补全）

| # | 批次 | 内容 | 缺口来源 |
|---|------|------|---------|
| H1 | **同步关节流组** | 解除 KB-035"不承诺关节间时间同步"：同组关节的带时间戳目标**批量提交、同拍生效**，跨关节相位一致性有承诺有验收；断流看门狗组级联动（一个关节断流的组级策略：全组受控停 vs 独立，显式语义） | 人形步态/全身控制的硬需求 |
| H2 | **数值 IK 兜底** | 阻尼最小二乘（DLS）+ 关节限位 + 冗余零空间姿态目标：覆盖偏置腕 6R（UR 类）与 7 自由度臂；解析解优先、数值兜底的插件分层；RT 合同 = 有界迭代 + 显式不收敛错误 | 人形手臂全是 7DOF/偏置腕，解析 6R 接不住 |
| H3 | **动力学前馈（原 Y6 提级）** | RNEA 重力/惯量/科氏前馈扭矩，CST 流模式的扭矩通道；对刚体仿真 oracle 验证；周期预算硬门 | RL 策略输出扭矩/力控接口 |
| H4 | 关节容量与预算档 | 流组 32→48+ 关节容量与 @1kHz 预算重测；组 MaxAxes 评估 | 全身 + 双手关节数 |

### F 轨：EtherCAT 走通（软件部分全部前置）

| # | 批次 | 内容 |
|---|------|------|
| F1 | `plcopen-fieldbus` 仓库骨架 | 独立仓库（GPL 隔离边界——SOEM/IgH 均 GPL 系，动态边界设计为仓库存在理由）；Master 抽象 + SOEM 适配器编译面 + 我方 Servo/CiA402 接口对接层；许可证设计文档（ADR） |
| F2 | 虚拟从站 CI | 无硬件冒烟：模拟 CiA402 从站走完整状态机梯形 + PDO 映射往返；台架到位只做真机验证（S1 收窄为纯验证） |
| F3 | 首驱动品牌适配清单 | 国产驱动（汇川/雷赛/禾川类）对象字典差异矩阵草案——为台架采购与首适配定型 |

### T 轨：数字孪生三件套

| # | 批次 | 内容 |
|---|------|------|
| T1 | pip wheel（原 Z1） | pyplcopen 上 PyPI，cibuildwheel 三平台 |
| T2 | **MuJoCo 闭环孪生**（2026-07-06 升级） | 策略 → pyplcopen 流滤波 → MuJoCo 执行器（物理域，Menagerie 厂商参数模型用户侧下载不 vendor）→ 反馈回灌 → rerun 可视化；命令域由内核保真、物理域由 MuJoCo 承担、标定域声明真机专属。**独有主张：碰真机前把真实执行层放进仿真回路验证策略**（sim2real 鸿沟的执行层部分由此关闭） |
| T3 | 单位与配置层（原 Z2） | SI ↔ 每周期换算 + 机构参数配置结构 |
| Z0 | **冷用户测试（流程）** | 每次 T/Z 批收口后，干净环境模拟新用户从 README 走到跑通，失败即缺陷登记 |

## Y 系列：算法（核心线）

> **单主路径原则（2026-07-07 评审定调）**：默认算法永远一条主路径
> ——严格 OTG/TOPP + 显式声明的 fallback（如快路径失败落全解），
> **不做用户可选算法菜单**；任何"可配置算法"提案先过此门。

| # | 批次 | 内容 | 域/预算处理 |
|---|------|------|------------|
| Y0 | **最优控制 oracle** | Pontryagin bang-bang 数值参照解算器进测试层，量化每个 OTG 解与理论最优的差距（趋势线指标） | 测试层，零运行时成本；**一切 Y 批的标尺，先做** |
| Y1 | 传送带/转台跟踪 | `MC_TrackConveyorBelt/RotaryTable`——机器人拾取闭环的最后一块标准板 | 周期路径新增有界跟踪项，随批预算门 |
| Y2 | OTG 补完整任意状态 | **补完整 state-to-state 形态**（评审定界）：非零目标加速度 at≠0、v0=vt=vmax 钉边界量化深潜、t_i≥0 epsilon 政策成声明项（钳零阈值+复验，附注 #2）；Ruckig 黑盒对照（ADR-0003）。整周期量化与 KB-050 两处妥协的撤销移交 Y4 cycle-exact| 规划域；声明变更走回放全流程；STREAM_METRICS 门看住单解耗时 |
| Y3 | reachability TOPP（两层，评审定界） | **第一层加速度级 TOPP-RA**：沿路径离散点递推可达/可控速度集（每点小 LP），速度/加速度/曲率/关节/笛卡尔限速统一进路径参数化（arXiv:1707.07239）；**第二层 jerk-aware 扩展**：状态 v → (v,a)。**先影子后换主**（附注 #3）：首次落地 = 窗口版 oracle 量化 excess_cycles，数字定去留；前置工程 = geom 路径导数合同（q_s/q_ss/q_sss 逐段解析）。**不做 clothoid/min-snap/MPC**；五次 Bezier blending 保留 | 规划域（允许 ms 级，周期路径只采样）；`window_replan_us` 基准指标，矩阵先声明预算 |
| Y4 | **solve_fixed_time 一等原语**（评审三关键之一；拱顶石——同步/cycle-exact 量化/流追赶/接管汇入同一求解，附注 #1）+ 拐角剖面化 | `T_sync = max(T_min[i])` 后逐轴 `solve_fixed_time(axis, T_sync)`——最优结构插入合法巡航/等待段或直接固定时长可行剖面；验收 duration==T_sync、终态 p/v/a ≤1e-9、全程在限（难点 T43）。相位同步变体、拐角内曲率约束变速穿越随批 | 周期路径零新增（仍是采样）；无此能力"全身同时到达"不成立 |
| Y5 | 笛卡尔管线对称收官 | 位姿组窗口化、任意空间弧平面、全圆/CENTER/RADIUS、twist 回读 | 沿用 KB-044 机制与门 |
| **Y7** | **组接管连续性修复**（KB-051，正确性级，插队最前；**linear 范围已批**） | 公差管语义 v2.1（ṡ₀/a_s0 标量承接 + 横向 OTG 衰减 + β 限值分割 + R_tube）；circular/笛卡尔扩展待曲率链式项另批；验收 = 逐周期全向量 v/a/j ≤ 全额限值 | 规划域 |
| Y6 | 刚体动力学前馈 | 重力/惯量模型前馈扭矩（RNEA），对刚体仿真 oracle 验证 | 周期路径新增 ~1-3µs/6R，矩阵定硬门（≤10µs 提案） |

**性能纪律（写进每份 Y 矩阵验收表）**：平滑与最优性花规划域的钱；
周期路径每次花钱都过硬门（KB-044 先例）；6.6 反模式有效——不为
"先进"做无热点优化（现状：周期路径对 250µs 档占比 ~1-2%）。

## P 系列：PLCopen 标准面清零（2026-07-07 扩充为全 Part 对账）

- **Part 4 剩余 FB**：MC_GroupHome、MC_MoveDirectAbsolute/Relative、
  MC_GroupInterrupt/Continue、MC_GroupSetOverride、MC_PathSelect/
  MC_MovePath（路径表）、MC_SetKinTransform/MC_ReadCartesianTransform
  标准形态——逐块过矩阵；
- **Part 5 回零规程**（2026-07-07 维护者对账补入）：MC_StepAbsSwitch/
  MC_StepLimitSwitch/MC_StepRefPulse/MC_StepBlock 等标准回零步 FB
  ——现状仅 v0.x 承接的 home_direct 口径，标准规程面缺失；纯软件可
  验收（开关/脉冲信号经数字输入通道模拟）；
- **Part 3 用户指南**：非 FB 面——作为 Z3 文档站教程的场景组织参照；
- **显式门控**（进 VISION 解锁表）：Part 6 液压扩展（等行业信号）、
  PLCopen Safety FB 族（无认证的安全 FB 实现价值存疑，边界同商用级
  #8）、PLCopen XML/TC6（随层 4 编辑器）、SFC（随 ST/层 2）。

做不做都要显式（承接或声明/门控），终点是全 Part 对照零留白。

## Z 系列：采纳（门廊，不装修想象中的房间）

> Z1/Z3 起步细案见 [signal-channel-plan](signal-channel-plan.md)——拷问后拉进当前里程碑并行项 4b。

| # | 批次 | 内容 |
|---|------|------|
| Z1 | **pip wheel** | pyplcopen 上 PyPI（cibuildwheel 三平台）+ "5 分钟数字孪生" notebook——第一个外部用户信号的最短路径 |
| Z2 | 单位与配置层 | SI ↔ 每周期换算辅助 + 轴/组配置结构（拆掉"每周期单位"这堵新人墙，核心约定不变） |
| Z3 | 文档站 | 三条用户旅程 ×30 分钟教程、API 参考、算法白皮书（矩阵内容可读化） |
| Z4 | C++ 包管理 | vcpkg + conan 收录 |
| Z5 | 诊断体验 | ErrorCode → 文本+排查提示；trace 可视化 |

**克制条款**：Z 只做无论谁来都需要的门廊；更深的 API 重设计等第一批
真实用户反馈（与 VISION 解锁纪律同源）。G-code/ST 仍属 Phase D 门控，
不进本计划。

## L 系列：IEC 61131-3 语言层（2026-07-07 维护者定调完整级入列，自研）

> 定位：PLCopen 扎实化收口后的**下一里程碑主项**。裁决：自研（MatIEC
> 仅作黑盒 oracle，ADR-0003 模式）；设计见
> [st-runtime-design](../design/core/st-runtime-design.md)；难点
> T30-T39 已全量识别。IL 显式不做；在线变更显式远期。
>
> **两条拷问结论（2026-07-07）**：① L 系列推进的是 VISION 层 1-2 的
> 身份完整性，对商用级八项硬指标零贡献——账目如实；② "完整级别" =
> **61131-3 自己的特性表机制**：我们声明支持的 feature table + 对
> 声明的逐条一致性（连 CODESYS 都非全实现），否则 L 系列无收口。

| # | 批次 | 内容 | 难点映射 |
|---|------|------|---------|
| L0 | ST 子集 + VM 地基 | 表达式/控制流/基本类型/VAR/直调 FB 面；字节码 VM（零分配/看门狗）；确定性代码生成；**解析器自始容错增量（T40，LSP 前置）** | T30/T37/T40 |
| L1 | 类型系统全量 | 转换矩阵机读化、ANY 重载消解、溢出/IEEE 口径声明、字面量/子范围/数组/结构 | T32 |
| L2 | POU 与实例模型 | FUNCTION/FB/PROGRAM、IN_OUT 引用、EN/ENO、MC_* 绑定表 + AXIS_REF | T34 |
| L3 | 进程映像子系统 | %I/%Q/%M、扫描边界双缓冲、RETAIN 持久化合同、force | T33 |
| L4 | 标准函数/FB 库全量 | 机读矩阵驱动逐函数交付（含定长字符串 RT 语义） | T31 |
| L5 | 任务模型 v1 | 单任务 + 扫描看门狗 + 跨任务一致性口径声明（多任务随 executor） | T35 |
| L6 | SFC 执行语义 | 步自动机字节码、限定符矩阵、不安全网络编译诊断 | T36 |
| L7 | 调试监控面 | seqlock 变量监控、force、断点（调试构建） | T38 |
| L∀ | 一致性验证体系 | 逐条款机读矩阵 + MatIEC 黑盒 oracle + 解析器 fuzz——随 L0 起步贯穿全程 | T39 |

## D 系列：开发者工具面（2026-07-07 立案，骑在 L 系列与 trace 资产上）

| # | 批次 | 内容 | 前置 |
|---|------|------|------|
| D1 | LSP + VS Code 扩展 | Language Server（诊断/补全/跳转/悬停）= L 前端薄壳；扩展只做接线 | L0-L2（T40 容错前端为 L0 内置要求） |
| D2 | WASM Playground | 内核 + ST + 孪生可视化编译到浏览器——零安装试用/教学（采纳核弹：零依赖内核的独有能力） | L0 + T2；CI 先锁 wasm32 编译目标（T41） |
| D3 | 调试示波器 | trace 在线模式（触发窗口冻结）+ rerun 渲染——commissioning Scope | X4 资产（T42） |

**仍门控**（VISION 层 4/5 原判不变，LSP/Web 地基降低未来解锁成本）：
LD/FBD/SFC 图形画布、HMI、TC6-XML 工程交换、cam 表图形编辑器。

## E 系列：工程证据（快批，可随时插队；2026-07-07 细案）

| # | 项 | 方案 | 门 |
|---|-----|------|-----|
| E1 | ARM64 CI | ubuntu runner + aarch64-linux-gnu 交叉编译全量目标 + qemu-user 跑非基准测试子集（基准数字无意义不跑） | 编译零错 + 子集全绿 |
| E2 | clang-tidy | 配置 `.clang-tidy`（bugprone-*/clang-analyzer-*/performance-* 为错误级；readability 类仅警告）；CI 对 core/ 增量执行 | 错误级零违例 |
| E3 | 变异分数门 | 既有周任务管线加阈值：抽查模块变异分数 ≥70%（ai-collaboration §5 既定健康线）失败即红 | ≥70% |
| E4 | 覆盖率 90% | 现状 ≥85%；按模块补盲区（探测轮发现的格子优先），门槛升至 90% | ≥90% |

## 里程碑方案索引（2026-07-07 全部起草）

| 项 | 方案文档 | 状态 |
|----|---------|------|
| Y7 组接管修复 | [group-takeover-semantics.md](../compliance/group-takeover-semantics.md) | 草案待批 |
| P-Part5 回零 | [part5-homing-semantics.md](../compliance/part5-homing-semantics.md) | 草案待批 |
| P-Part4 管理组 | [part4-management-semantics.md](../compliance/part4-management-semantics.md) | 草案待批 |
| P-Part4 路径表/变换（第二批） | [part4-pathtable-semantics.md](../compliance/part4-pathtable-semantics.md) | 草案待批 |
| Z1/Z3 信号通道细案 | [signal-channel-plan.md](signal-channel-plan.md) | 已拉进当前里程碑并行项 4b |
| 对抗性探测轮 | `.claude/skills/plcopen-adversarial-probe` | 已固化为技能 |
| **算法合同集（权威）** | [algorithm-contracts.md](../design/core/algorithm-contracts.md) | **已批准**（2026-07-07 维护者裁决，六合同 + 落地顺序） |
| Y0 oracle | [otg-oracle-design.md](../design/core/otg-oracle-design.md) | 设计已备（测试层免批） |
| ST 运行时 | [st-runtime-design.md](../design/core/st-runtime-design.md) | 已裁决选项 A：L 系列为扎实化收口后的下一里程碑主项 |

## 推荐起手（待复盘拍板）

**（2026-07-07 重排）Y7 → P 系列（Part 4 剩余 + Part 5）→ 对抗性
探测轮 → **Y0（先立标尺）→ Y2 完整 OTG → Y4 定时同步** → P 系列 →
E 系列 → 【PLCopen 扎实化收口】→ Y3 TOPP（落地顺序 = 合同文档终节：
Y7→Y0→Y2→Y4→T24→Y3→H2；最关键三件 = Y2/Y4/T24 解析校验）；
与 L 系列的相对排序收口复盘时由维护者定**。

---

*创建：2026-07-06。复盘时定序后，当期批次进 ROADMAP 里程碑表；
本文件只增删候选，不承载现状（现状看 STATUS.md）。*
