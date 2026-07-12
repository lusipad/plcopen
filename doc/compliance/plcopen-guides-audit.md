# PLCopen 指南与语言方法审计

> **范围**：`guide_compliant_fb`、`guide_coding`、
> `guide_quality_metrics`、`guide_quality_automation`、`guide_oop`、
> `annex_a_e` 六份资料。版本与 SHA256 见
> [规格清单](plcopen-specs-manifest.md)。
>
> **出处纪律**：仅记录章节/规则族与中文自述，不复制原文。判定针对当前
> `core/`、ST 子集与仓库工程流程；指南中的示例代码不是本仓库实现来源。

## 判定口径

| 符号 | 含义 |
|------|------|
| ✅ | 已有可执行规则或直接证据 |
| ⚠️ | 部分覆盖、厂商选择未声明或仅靠人工约定 |
| 🔴 | 指南强制规则/规范性 Annex 与当前行为冲突 |
| ❌ | 整项能力或度量缺失 |
| ➖ | 对当前 C++ 库/已声明 ST 子集不适用 |

---

## 1. Creating PLCopen Compliant Function Block Libraries v1.0

| 章节 | 要求（自述） | 判定 | 仓库证据 |
|------|-------------|------|----------|
| §1 | 明确区分 Execute 边沿型与 Enable 电平型；Execute 配 Done，Enable 配 Valid | ✅ | P1-A2 已将 `MC_SetOverride` 改为 Enable/Enabled 电平型；其他 FB 按各自类别保持 |
| §1 | 边沿型异步动作必须在后续周期保持可观察终态 | 🔴 | Execute 低后停止观察并清跟踪，见 Part 1 D-01；`core/fb/motion.h:35-105` |
| §2 | 基础边沿模型应具 Dormant/Executing/Done/Error/Resetting 生命周期 | ⚠️ | `AxisExecuteFb` 具有 busy/done/error，但没有显式 resetting 状态；`core/fb/motion.h:35-105` |
| §3 | 可中止模型应区分 Aborting 与 Aborted，并保证中止终态 | ⚠️ | 对外只有 `CommandAborted`，内部 takeover 无通用 aborting 状态；轴/组实现分散处理（`core/axis/state.h:707-721`; `core/axis/group.h`） |
| §4 | Timeout 与单周期 TimeLimit 是不同合同；长动作应可跨周期分片 | ❌ | 公共运动 FB 没有统一 timeout/time-limit 输入或状态；仅 ST VM 有指令预算 watchdog（`core/st/vm.h:96-97`） |
| §5 | 电平型 FB 在 Enable 期间持续刷新 Valid/输出，错误恢复不依赖新边沿 | ⚠️ | Read/Power 类基本符合（`core/fb/parameter.h:24-91`; `core/fb/motion.h:123-137`），但错误恢复/Busy 规则未形成公共基类 |
| §6 | 可复用 FB 应提供接口表、状态图、时序图及错误表 | ⚠️ | 合规矩阵和测试存在，但公开 API 尚无逐 FB 状态图/时序图/错误表 |
| §7 | 经典调用与 OOP 包装可并存 | ❌ | ST 语言层不支持 CLASS/INTERFACE/METHOD；`core/st/lexer.h:128-152` |

### 已确认问题 G-01

**G-01：FB 生命周期模型未统一。** Part 1 D-01/D-12 是直接违规；其余 FB
仍缺统一的 resetting、timeout、time-limit 与 level-controlled 错误恢复合同。

---

## 2. PLCopen Coding Guidelines v1.0

正文共识别 **64 条正式 Guideline**（文本另有 1 条规则格式示例，不计入）。
§2.1–2.4 先给规则族摘要，§2.5 再按原文 Rule ID 逐条判定；图形语言规则
在当前库无编辑器/程序模型时标为不适用，而不是判定符合。

### 2.1 命名规则（§3）

| 规则族 | 判定 | 证据/说明 |
|--------|------|----------|
| 禁止偏移访问成员、禁止硬编码物理地址 | ✅ | C++ 通过成员名与抽象引用访问；数字 I/O 使用逻辑通道（`core/axis/state.h:1116-1157`） |
| 前缀/大小写/字符集必须统一并文档化 | ⚠️ | C++ 命名由 `.clang-format`/现有风格约束，但无 PLCopen 命名规则文档或 lint；ST lexer 限定标识符字符（`core/st/lexer.h`） |
| 禁止关键字、标准类型与标准库对象名冲突 | ✅(ST) | lexer 关键字表先于 identifier 分类（`core/st/lexer.h:62-181`） |
| 不同 POU/类型/变量不得重名或遮蔽 | ⚠️ | ST 单 PROGRAM 子集有符号表检查，但尚无 namespace/多 POU，因此未覆盖完整规则（`core/st/sema.h`） |
| 标识符长度与 namespace 约定 | ❌ | 未声明最小/最大标识符长度；ST 不支持 namespace |

### 2.2 注释规则（§4）

| 规则族 | 判定 | 证据/说明 |
|--------|------|----------|
| 公共元素及意图应有清晰注释 | ⚠️ | 模块 README 与关键算法注释存在，但没有文档覆盖率门；公开头并非每个成员都有说明 |
| 禁止嵌套注释、注释掉的代码 | ⚠️ | 无专用静态门；clang-tidy 不证明该规则（`cmake/clang_tidy_gate.cmake`） |
| 统一单行注释形式 | ⚠️ | C++ 主体使用 `//`，ST 输入仍接受 IEC 注释语法；未形成发布门 |
| 注释统一使用一种团队语言 | ⚠️偏差 | 项目文档/提交以中文为主，代码注释以英文为主（`CLAUDE.md`）；这是项目选择但未登记为 Coding Guidelines 偏差 |

### 2.3 通用编码实践（§5）

| 规则族 | 判定 | 证据/说明 |
|--------|------|----------|
| 禁止死代码与未使用声明 | ⚠️ | clang-tidy gate 覆盖部分 C++ 问题（`cmake/clang_tidy_gate.cmake`），未覆盖 ST dead-code/unused 全集 |
| 所有变量先初始化、地址不重叠 | ✅ | C++ 聚合默认初始化；ST 有类型默认值与定长实例存储（`core/st/types.h`; `core/st/vm.h`） |
| 模块化、封装、限制外部/全局变量 | ✅(C++) / ⚠️(语言层) | `core/` L0-L7 依赖向内；ST 尚无 VAR_EXTERNAL/GLOBAL 完整模型 |
| 返回错误必须检查 | ⚠️ | `rt::Result/ErrorCode` 广泛使用，但无静态门保证所有返回值被消费 |
| 浮点/时间阈值避免等值比较 | ⚠️ | 算法中仍存在合理的精确零/状态比较；没有按用途区分的 lint，不能声明全面符合 |
| POU 复杂度应测量并设上限 | ❌ | 无 McCabe/Halstead/POU 复杂度门；见 G-02 |
| 多任务共享数据单写者并做快照 | ✅(runtime) | executor 双域与 SPSC/单写者合同（`doc/compliance/runtime-thread-ownership-semantics.md`; KB-068） |
| 物理输出每周期单点写入 | ⚠️ | AxisModel 集中数字输出 bank，但缺全局“每周期一次”断言（`core/axis/state.h:1142-1148`） |
| 禁止递归 | ✅(ST) / ⚠️(C++) | ST 无递归 POU；RT scan 不扫描全部非周期 C++ 递归 |
| 任务只调用 PROGRAM；跨任务值先本地快照 | ➖/⚠️ | ST 当前仅单 PROGRAM、无任务配置；运行时快照合同已实现 |
| 参数模式与读写用途一致 | ⚠️ | C++ 不直接映射 VAR_INPUT/OUTPUT/IN_OUT；Part 1 矩阵已登记接口偏差 |
| FB 实例每周期最多调用一次 | ⚠️ | 调用方责任，库无运行时检测；`MC_Power` 多实例问题见 Part 1 3.1-n4 |
| 临时变量、数据类型、接口针脚数、显式转换 | ⚠️ | ST 转换矩阵显式且机读（`st-l1a-conversions.yaml`），但无 VAR_TEMP；大量 FB 参数超过建议值属于领域接口要求 |
| 禁止废弃构造 | ⚠️ | ST unsupported 构造有诊断分批，但未维护 PLCopen 废弃清单 |

### 2.4 语言专用规则（§6/§7）

| 语言 | 规则族 | 判定 | 证据/说明 |
|------|--------|------|----------|
| FBD | 网络中间赋值、每网最多约 32 元素 | ➖ | 当前无 FBD 编译器/模型 |
| LD | 单线圈写入、每 rung 最多约 32 元素 | ➖ | 当前无 LD 编译器/模型 |
| SFC | 分支配对、action 语言限制、最大 step 数 | ➖ | 当前无 SFC 编译器/模型 |
| ST | 避免无结构跳转、限制行长、禁止改 FOR 控制变量 | ⚠️ | 无 GOTO/JUMP；FOR 控制变量写入报错（KB-069），但无 80 字符输入门 |
| ST | 控制变量作用域、循环必须可终止 | ⚠️ | 指令预算阻止无限执行（`core/st/vm.h:96-97`），但允许 WHILE/REPEAT 动态不终止直到 watchdog |
| ST | 表达式显式括号、IF 应有 ELSE、禁 tab | ❌ | parser 允许省略 ELSE，也无 tab/括号风格 lint（`core/st/parser.h`） |
| 全部 | 禁止动态分配 | ✅(周期路径) / ⚠️(加载域) | RT scan 禁周期分配（`cmake/rt_safety_scan.cmake`）；ST 编译/加载域可用宿主容器 |
| ST/IL | 指针仅限数组访问且只可等值比较 | ➖ | ST L0/L1a 无指针；IL 未实现 |

### 2.5 64 条正式规则逐条判定

> ID 按正文保留；原文将 FOR 控制变量规则编号为 `L22`，不存在 `L12`，
> 这里不擅自顺排。规则模板示例不计入分母。

| ID | 要求（自述） | 判定 | 证据/适用性 |
|----|-------------|------|---------------|
| N1 | 程序不直接使用物理地址 | ✅ | C++ FB 使用轴/I/O 抽象与逻辑通道（`core/axis/state.h:1116-1157`） |
| N2 | 使用类型前缀时记录约定 | ⚠️ | C++ 风格一致，但无 PLCopen 前缀规范或 lint |
| N3 | 明确并避开保留名称 | ⚠️ | ST 关键字受 lexer 保护，标准类型/库对象冲突未形成完整表（`core/st/lexer.h`） |
| N4 | 明确大小写规则 | ⚠️ | 实现有稳定风格，未形成独立规则门 |
| N5 | 局部名称不得遮蔽全局名称 | ⚠️ | 单 PROGRAM 符号表覆盖有限，尚无完整 GLOBAL/多 POU 模型（`core/st/sema.h`） |
| N6 | 声明可接受的名称长度 | ❌ | 未公开标识符长度上限 |
| N7 | 声明 namespace 命名规则 | ➖/❌ | ST 无 namespace；C++ 无 PLCopen 专项规则 |
| N8 | 声明可接受字符集 | ⚠️ | lexer 实际限定字符，公开规范未完整登记（`core/st/lexer.h`） |
| N9 | 不同元素类型不得复用同名 | ⚠️ | 任务/函数/用户 FB 尚未实现，无法完整验证 |
| N10 | 用户定义类型前缀须有约定 | ➖/❌ | ST 尚无用户定义派生类型 |
| C1 | 代码意图应由注释解释 | ⚠️ | README/算法注释存在，无覆盖门 |
| C2 | 所有程序元素应有清晰注释 | ⚠️ | 公开头并非逐元素文档化 |
| C3 | 发布代码不得有嵌套注释 | ⚠️ | 无专用静态检查 |
| C4 | 发布代码不得保留注释掉的代码 | ⚠️ | 无专用静态检查 |
| C5 | 统一采用单行注释形式 | ⚠️ | C++ 主体用 `//`，ST 接受 IEC 注释；未声明边界 |
| C6 | 注释使用团队指定语言 | ⚠️偏差 | 代码注释主要英文、工程文档主要中文，无正式语言政策 |
| CP1 | 成员通过名称而非偏移访问 | ✅ | C++ 类型化成员访问，无裸偏移式 PLC 数据访问 |
| CP2 | 应用代码均应可达且被使用 | ⚠️ | clang-tidy 覆盖部分；ST 未做完整可达性分析 |
| CP3 | 变量使用前初始化 | ✅ | C++ 默认初始化；ST 类型默认值与定长实例存储（`core/st/types.h`; `core/st/vm.h`） |
| CP4 | 直接地址不得重叠 | ➖ | ST 不支持 `%I/%Q/%M` 直接变量 |
| CP5 | 应用应模块化并保持清晰设计 | ✅原则 | `core/` L0-L7 单向依赖与模块 README |
| CP6 | 函数/FB 内避免外部变量 | ⚠️ | C++ 依赖显式传入；ST 无完整 VAR_EXTERNAL |
| CP7 | 必须检查错误信息 | ⚠️ | `rt::Result/ErrorCode` 广泛使用，无全量消费门 |
| CP8 | 浮点比较不依赖精确相等 | ⚠️ | 无按用途区分的检查门 |
| CP28 | 时间和物理量比较不依赖精确相等 | ⚠️ | 有定长时间与容差合同，无全局 lint |
| CP9 | 限制 POU 复杂度 | ❌ | 无 McCabe/POU 复杂度阈值 |
| CP10 | 避免多任务多写同一变量 | ✅(runtime) | executor 单写者合同（`runtime-thread-ownership-semantics.md`; KB-068） |
| CP11 | 多任务访问须同步 | ✅(runtime) | SPSC/快照边界已定义；ST 无任务模型 |
| CP12 | 物理输出每周期只写一次 | ⚠️ | 数字输出集中写入，无每周期一次断言（`core/axis/state.h:1142-1148`） |
| CP13 | POU 不得递归 | ✅(ST) / ⚠️(C++) | ST 无递归用户 POU；C++ 无全库递归门 |
| CP14 | POU 应有单一退出点 | ⚠️ | C++ 与 ST 允许早返回，无规则门 |
| CP15 | 跨任务写入值每周期只快照一次 | ✅(runtime) | executor 快照合同；ST 无任务配置 |
| CP16 | Task 只调用 PROGRAM | ➖ | ST 无 TASK/CONFIGURATION |
| CP17 | 参数用途符合声明模式 | ⚠️ | Part 1 已发现标准 I/O 偏差；ST 用户 POU 未实现 |
| CP18 | 限制全局变量 | ⚠️ | C++ 模块化；ST 无完整 VAR_GLOBAL |
| CP19 | 避免 jump 与 return | ⚠️ | 无 GOTO/JUMP，但支持 RETURN，未限制使用 |
| CP20 | FB 实例每周期最多调用一次 | ⚠️ | 调用方责任，库无检测 |
| CP21 | 临时值使用 VAR_TEMP | ➖/❌ | ST 尚无 VAR_TEMP |
| CP22 | 选择恰当数据类型 | ⚠️ | 16 标量可用，无类型选择 lint |
| CP23 | 声明 POU 针脚数量上限 | ❌ | 未公开统一上限 |
| CP24 | 不声明未使用变量 | ⚠️ | C++ 工具覆盖部分；ST 无完整门 |
| CP25 | 数据类型转换显式 | ✅(ST) | 210 格转换矩阵与显式转换函数（`st-l1a-conversions.yaml`） |
| CP26 | 全局变量只能由一个 PROGRAM 写 | ➖/⚠️ | ST 无多 PROGRAM/VAR_GLOBAL；runtime 有单写者原则 |
| CP27 | 避免废弃特性 | ⚠️ | 未维护 PLCopen 废弃构造清单 |
| L1 | 声明统一缩进 | ⚠️ | C++ 有 clang-format；ST 无格式门 |
| L2 | FBD 不在网络内写中间结果 | ➖ | 无 FBD |
| L3 | 声明 FBD 网络复杂度上限 | ➖ | 无 FBD |
| L5 | LD 线圈后不得继续串接触点 | ➖ | 无 LD |
| L6 | 声明 LD rung 复杂度上限 | ➖ | 无 LD |
| L7 | SFC 分支正确闭合 | ➖ | 无 SFC |
| L8 | 不用 SFC 编写 SFC action | ➖ | 无 SFC |
| L9 | 声明 SFC 图复杂度上限 | ➖ | 无 SFC |
| L4 | 声明 ST 通用格式规则 | ❌ | 无 ST formatter/lint |
| L10 | 避免 CONTINUE/EXIT | ⚠️ | 两者可用且无使用限制 |
| L11 | 声明 ST 最大行长 | ❌ | 无输入行长门 |
| L22 | FOR 内不得修改控制变量 | ✅ | sema 拒绝修改（KB-069） |
| L13 | FOR 控制变量不在循环外使用 | ⚠️ | 无独立规则证据 |
| L14 | 参数传递清晰可辨 | ⚠️ | 内建 binder 有参数表；用户 POU 未实现 |
| L15 | 用括号显式表达优先级 | ❌ | parser 支持优先级，无风格强制 |
| L16 | 声明 tab 使用政策 | ❌ | 无 ST tab 检查 |
| L17 | 每个 IF 有 ELSE | ❌ | parser 允许省略 ELSE（`core/st/parser.h`） |
| E1 | 不使用动态内存分配 | ✅(周期路径) / ⚠️(加载域) | RT scan 禁周期分配；加载域使用宿主容器 |
| E2 | 不使用指针算术 | ✅(ST) | ST 无指针；C++ 无全库专项门 |
| E3 | 指针比较仅限允许的等值判断 | ➖ | ST 无指针 |

逐条合计：N 10 + C 6 + CP 28 + L 17 + E 3 = **64**。

### 已确认问题 G-02

**G-02：64 条 Coding Guidelines 未转为项目机读门。** 当前格式、clang-tidy、
RT-safety 只覆盖其中一部分；命名、注释、复杂度、FB 单调用、ST 风格等仍靠人工，
图形语言则整体不在产品范围。

---

## 3. Guideline Software Quality Metrics v1.0

### 3.1 工作流要求

| 章节 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| §3 | 指标需绑定质量属性、粒度、测量方法与解释范围 | ⚠️ | coverage/mutation/benchmark 有明确阈值，其他指标未建数据字典（`doc/compliance/ci-gates.md:12-15`） |
| §4 | 开发中持续检查、版本前后比较、交付后审计 | ⚠️ | CI/周门覆盖持续检查；无统一历史趋势存储与交付后 metric report |
| §5 | 分 basic/advanced/expert 成熟度逐步引入 | ⚠️ | 已有 coverage/mutation/clang-tidy，但未显式映射成熟度级别 |
| §6 | 阈值应由本项目基线和风险校准，不应盲套通用数值 | ✅原则 | 当前 coverage ≥90%、mutation 阈值和 benchmark 预算均为项目门（`cmake/coverage_gate.cmake`; `cmake/mutation_score_gate.cmake`） |

### 3.2 指标覆盖矩阵

| 指标族 | 判定 | 当前证据/缺口 |
|--------|------|---------------|
| LOC/文件尺寸/单位尺寸 | ⚠️ | 可由仓库统计，但未设门；`core/axis/group.h` 超大问题仅靠人工评审发现 |
| McCabe 圈复杂度 | ❌ | 无采集工具、基线、阈值或趋势 |
| Halstead 体积/难度/长度 | ❌ | 无采集 |
| 注释行与注释比 | ❌ | 无采集 |
| Fan-in/Fan-out、依赖耦合 | ⚠️ | L0-L7 依赖方向有架构约束，但无数值采集 |
| CBO/RFC/DIT/NOC/LCOM 等 OOP 指标 | ➖/❌ | ST 无 OOP；C++ 也未采集这些指标 |
| 测试覆盖率 | ✅ | core line coverage ≥90% 周门（`cmake/coverage_gate.cmake`） |
| 变异分数 | ✅ | 周门执行登记 mutation 并设阈值（`cmake/mutation_score_gate.cmake`） |
| 静态分析问题数 | ✅(错误级) | Linux clang-tidy gate（`cmake/clang_tidy_gate.cmake`） |
| 性能/执行时间 | ✅ | benchmark、jitter、RT executor smoke 进入 CTest/CI |
| 成熟度/变更频率/缺陷修复时长 | ❌ | 无模块成熟度和趋势数据集 |
| 重用率/克隆率 | ⚠️ | 有发布/包消费者测试，无 clone/reuse metric |

### 已确认问题 G-03

**G-03：质量度量只覆盖测试与运行质量，缺结构质量仪表盘。** McCabe、
Halstead、LOC 分布、耦合/内聚、注释比、成熟度和趋势均未采集，因此不能按
该指南声称建立了完整 metric-based quality assessment。

---

## 4. Metrics for Quality Assessment of PLC Software（提案）

| 页/主题 | 要求（自述） | 判定 | 证据/说明 |
|---------|-------------|------|----------|
| pp.2-3 | 质量度量应低成本嵌入日常开发，并按利益相关者提供报告 | ⚠️ | CI 自动化程度高，但没有角色化报告或单一质量看板 |
| pp.3-4 | 先盘点平台工具能力，再形成适用本组织的指南 | ⚠️ | 已有工具门清单（`ci-gates.md`），尚未形成 PLCopen metric 选型记录 |
| pp.4-5 | 用变更历史计算库模块成熟度，辅助重用决策和测试投入 | ❌ | 无模块成熟度指标、change-based score 或版本趋势 |

该文件是工作组提案而非最终规范；不产生“违规”，但暴露 G-03 的流程缺口。

---

## 5. PLCopen OOP Guidelines v1.0

| 章节 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| §2 | IEC 第三版 OOP 包括 class、method、interface、继承、动态绑定 | ❌ | ST lexer 将 L2 接口/POU 构造分类为 unsupported，当前无 OOP AST/VM（`core/st/lexer.h:128-152`） |
| §3 | 经典 FB 与 OOP 接口可共存、渐进迁移 | ❌ | `core/st` 只能绑定经典 basic FB；无 interface/method/property adapter（`core/st/bind.h`） |
| §3 | 模块与命令应通过统一 interface 解耦 | ⚠️(C++) / ❌(ST) | C++ 使用抽象/模板与小接口，但没有 PLCopen OOP 语言表面 |
| §4 | 状态机、行为模型、错误处理可由组合或继承复用 | ⚠️ | C++ 有基类复用，却已出现 D-01/D-02 基类级错误；ST 无继承/组合语义 |
| §4 | 优先在合适场景使用组合，继承必须控制深度与封装泄漏 | ⚠️ | 无 DIT/CBO/LCOM 度量；C++ 多层 FB 继承用于复用接口（如 `FbMoveRelative : FbMoveAbsolute`）并暴露了多余输入 |
| §4 | ABSTRACT/FINAL/可见性等关键字需有明确设计纪律 | ❌ | ST 不支持这些关键字；无语义矩阵 |
| §4 | 动态分配必须结合确定性与控制周期风险裁决 | ✅原则/❌语言能力 | RT 路径明确禁分配（CLAUDE.md；RT scan），ST 也无动态分配；尚无 OOP 对象生命周期模型 |
| §5 | 性能、初始化成本、接口调用开销需要测量 | ❌ | 无 OOP 能力，因此也无对应 benchmark |

### 已确认问题 G-04

**G-04：L 系列没有 PLCopen OOP 能力。** CLASS/METHOD/INTERFACE、继承、
动态绑定、可见性、property 和对象初始化均缺失；这不是小偏差，而是明确的
语言层未覆盖面。若 OOP 继续门控，应在 VISION/ROADMAP 中保持显式“不支持”。

---

## 6. Annex A-E：文本语言规范方法

### 6.1 Annex A：语法与语义描述方法

| 条款族 | 判定 | 证据/说明 |
|--------|------|----------|
| A.1 BNF 风格的 terminal/non-terminal、optional/repetition | ⚠️ | parser/lexer 有代码语法，但没有独立、可生成的正式 grammar；`core/st/parser.h` 是事实源 |
| A.2 语义应区分静态约束、动态行为与错误 | ✅部分 | `st-l0/l1a-semantics.md` + conformance YAML + `DiagCode/ScanError` 已分层 |

### 6.2 Annex B：语言与程序模型

| 条款族 | 判定 | 证据/说明 |
|--------|------|----------|
| B.0 配置/资源/任务/程序模型 | ❌ | 当前仅单 PROGRAM，无 CONFIGURATION/RESOURCE/TASK（`st-l0-semantics.md`） |
| B.1.1 标识符/字面量 | ✅部分 | lexer 支持子集并给 unsupported 诊断（`core/st/lexer.h`） |
| B.1.2 数字、字符串、日期时间字面量 | ⚠️ | 数值与 TIME 子集已支持；STRING、TOD/DATE/DT 未实现 |
| B.1.3 基本/泛型/派生类型 | ⚠️ | 16 标量与转换已实现；ARRAY/STRUCT/ENUM/SUBRANGE/STRING 等未实现 |
| B.1.4 直接变量、多元素变量、初始化 | ❌/⚠️ | 无 `%I/%Q/%M`、ARRAY/STRUCT；标量声明与 CONSTANT 已支持 |
| B.1.5 Function/FB/Program | ⚠️ | 单 PROGRAM + 内建 basic FB；用户 FUNCTION/FB、多实例声明尚未实现 |
| B.1.6 SFC | ❌ | 无 SFC |
| B.1.7 Configuration | ❌ | 无配置元素 |
| B.2 IL | ➖ | IL 已被产品范围排除，lexer 对相关构造报 unsupported |
| B.3 ST 表达式/语句 | ✅部分 | IF/CASE/FOR/WHILE/REPEAT/EXIT/CONTINUE/RETURN 与标量表达式已覆盖；见 `core/st/README.md` |

### 6.3 Annex C：分隔符与关键字

| 条款族 | 判定 | 证据/说明 |
|--------|------|----------|
| C.1 分隔符 | ⚠️ | ST 子集支持算术/比较/赋值/范围等所需符号；字符串、直接地址、图形连接符不支持 |
| C.2 关键字 | ⚠️ | 已实现关键字进入 `TokenKind`；未实现关键字由 unsupported 表分类（`core/st/token.h`; `core/st/lexer.h`） |

### 6.4 Annex D：实现依赖参数

| 条款 | 实现依赖参数（自述） | 判定 | 登记/证据 |
|------|----------------------|------|-----------|
| 1.5.1 | 错误处置程序 | ⚠️ | `DiagCode/ScanError` 已分层，未形成覆盖全部 Annex E 的统一处置表 |
| 2.1.1 | 国家字符及 `#/$/|/!` 解释 | ⚠️ | lexer 使用固定 ASCII 词法，未公开登记这些字符选择 |
| 2.1.2 | 标识符最大长度 | ❌ | 未声明 |
| 2.1.5 | 注释最大长度 | ❌ | 未声明 |
| 2.2.3.1 | duration 值域 | ✅部分 | TIME 值域随 64 位表示，见 KB-069；duration 全类型未实现 |
| 2.3.1 | TIME 值域；TOD/DT 秒精度 | ⚠️ | TIME 已定义；TOD/DATE/DT 未实现 |
| 2.3.3 | 数组下标/尺寸、结构元素/尺寸、每声明变量数上限 | ➖/❌ | ARRAY/STRUCT 未实现；未发布统一 unsupported 限制表 |
| 2.3.3.1 | 枚举值最大数量 | ➖ | ENUM 未实现 |
| 2.3.3.2 | STRING 默认及最大长度 | ➖ | STRING 未实现 |
| 2.4.1.1 | 层级最大深度、逻辑/物理映射 | ➖ | CONFIGURATION/RESOURCE 层级未实现 |
| 2.4.1.2 | 下标数/范围、结构层级上限 | ➖ | ARRAY/STRUCT 未实现 |
| 2.4.2 | 系统输入初始化 | ➖ | 无配置/进程映像输入模型 |
| 2.4.3 | 每声明变量最大数量 | ❌ | 标量声明存在但未公开上限 |
| 2.5 | POU 执行时间信息 | ⚠️ | 有 VM 指令预算和运行基准，无逐 POU WCET 声明 |
| 2.5.1.1 | 函数以名称或符号表示 | ✅ | ST 使用名称形式调用内建函数/FB |
| 2.5.1.3 | 函数规格最大数量 | ❌ | 未公开 |
| 2.5.1.5 | 可扩展函数最大输入数 | ➖/❌ | 无用户可扩展函数模型 |
| 2.5.1.5.1 | 类型转换对精度的影响 | ✅ | `st-l1a-conversions.yaml` 与 KB-070 登记 |
| 2.5.1.5.2 | 单变量函数精度、算术函数实现 | ⚠️ | 基础算术有语义矩阵；数学函数精度/实现未统一登记 |
| 2.5.2 | FB 规格及实例最大数量 | ⚠️ | 内建 FB 与实例存储有容量，公开矩阵未列全 |
| 2.5.2.3.3 | 计数器上下限 | ⚠️ | basic counter 使用标量类型边界，未以该参数公开声明 |
| 2.5.2.3.4 | 定时期间修改 PT 的效果 | ⚠️ | basic timer 有实现行为，未在合规矩阵逐项登记 |
| 2.5.3 | 程序大小限制 | ❌ | 未公开 |
| 2.6 | 执行控制元素的时序/可移植性影响 | ⚠️ | scan/预算合同存在，未覆盖完整 IEC 控制元素 |
| 2.6.2 | step elapsed 精度、每 SFC 最大 step 数 | ➖ | 无 SFC |
| 2.6.3 | 每 SFC/step 最大 transition 数 | ➖ | 无 SFC |
| 2.6.4 | action 控制机制 | ➖ | 无 SFC |
| 2.6.4.2 | 每 step 最大 action block 数 | ➖ | 无 SFC |
| 2.6.5 | step 状态显示、transition 清除时间、分合支宽度 | ➖ | 无 SFC/图形面 |
| 2.7.1 | RESOURCE 库内容 | ➖ | 无 RESOURCE |
| 2.7.2 | task 数量、周期分辨率、调度策略 | ➖ | ST VM 无 task scheduler，宿主逐 scan 调用 |
| 3.3.1 | 表达式最大长度、布尔短路/全求值选择 | ⚠️ | KB-069 声明全求值；最大长度未声明 |
| 3.3.2 | 语句最大长度 | ❌ | 未声明 |
| 3.3.2.3 | CASE selection 最大数量 | ❌ | CASE 已实现，未声明上限 |
| 3.3.2.4 | FOR 结束时控制变量值 | ✅ | KB-069 登记循环行为 |
| 4.1.1 | 图形/半图形表示及网络拓扑限制 | ➖ | 无图形语言 |
| 4.1.3 | feedback loop 求值顺序 | ➖ | 无 FBD/LD 网络 |
| 4.3.3 | 网络执行顺序指定方式 | ➖ | 无图形网络 |

### 6.5 Annex E：错误条件

| 条款 | 错误条件（自述） | 判定 | 处置/证据 |
|------|------------------|------|-----------|
| 2.3.3.1 | 值超出 subrange | ➖ | SUBRANGE 未实现 |
| 2.4.2 | 数组初始化项数不匹配 | ➖ | ARRAY 未实现 |
| 2.5.1 | 函数误用直接变量或外部变量 | ➖ | 用户 FUNCTION 与直接/外部变量未实现 |
| 2.5.1.5.1 | 类型转换错误 | ✅ | conversion invalid 诊断与测试（KB-070） |
| 2.5.1.5.2 | 数值越界；除零 | ✅ | `DiagCode/ScanError` 与算术测试覆盖 |
| 2.5.1.5.4 | selection 输入类型混用；MUX selector 越界 | ➖ | MUX/可扩展 selection 未实现 |
| 2.5.1.5.5 | 字符位置非法；结果超长；CONCAT 超长 | ➖ | STRING 未实现 |
| 2.5.1.5.6 | 结果超出数据类型值域 | ✅部分 | 标量算术/转换有诊断；未实现函数族不适用 |
| 2.5.2.2 | FB 实例作输入时缺参数；VAR_IN_OUT 缺参数 | ⚠️ | 内建 binder 校验参数；用户 FB/完整 IN_OUT 未实现 |
| 2.6.2 | SFC 初始 step 数错误；程序修改 step 状态/时间 | ➖ | 无 SFC |
| 2.6.2.5 | 选择分支存在多个未定优先级真 transition | ➖ | 无 SFC |
| 2.6.3 | transition 条件求值有副作用 | ➖ | 无 SFC |
| 2.6.4.5 | action 控制冲突 | ➖ | 无 SFC |
| 2.6.5 | SFC 不安全或不可达 | ➖ | 无 SFC |
| 2.7.1 | VAR_ACCESS 数据类型冲突 | ➖ | 无 VAR_ACCESS/RESOURCE |
| 2.7.2 | task 资源过量、截止期未满足、调度冲突 | ➖ | 无 task 模型 |
| 3.2.2 | 数值越界；当前结果与操作数类型不一致 | ✅部分 | VM 标量算术有类型检查/故障；IL/current-result 模型未实现 |
| 3.3.1 | 除零；操作数据类型非法 | ✅ | sema/VM 诊断与测试覆盖 |
| 3.3.2.1 | 函数返回时未赋返回值 | ➖ | 用户 FUNCTION 未实现 |
| 3.3.2.4 | 迭代不终止 | ✅部分 | 指令预算 watchdog 确定性终止 scan，不做静态终止性证明（`core/st/vm.h:96-97`） |
| 4.1.1 | connector label 与元素同名 | ➖ | 无图形语言 |
| 4.1.3 | feedback 变量未初始化 | ➖ | 无 FBD/LD feedback 网络 |

### 已确认问题 G-05

**G-05：ST 语义矩阵未完整登记 Annex D 实现依赖参数。** 已支持的算术/转换
语义较完整，但长度、容量、程序大小、FB 数量等限制散落在代码中；未实现的
配置/任务/SFC/派生类型也缺统一的 Annex B-E 覆盖表。

---

## 7. 汇总

| 编号 | 结论 | 严重度 |
|------|------|--------|
| G-01 | FB 生命周期缺统一 level/edge/abort/timer 合同 | 🔴 |
| G-02 | Coding Guidelines 64 条未形成完整机读门 | 🔴流程缺口 |
| G-03 | 缺结构质量与成熟度度量 | 🔴流程缺口 |
| G-04 | ST/OOP 语言能力整体缺失 | ❌能力缺失 |
| G-05 | Annex B-E 覆盖与实现依赖参数登记不完整 | 🔴合规缺口 |

这些结论只登记问题，不授权实现。任何语言能力、公开 FB 行为或门禁变更仍需
先走语义矩阵批准流程。
