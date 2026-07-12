# PLCopen Motion / Logic 补充资料审计

> 审计依据：PLCopen 官方 `Annex F Examples`、`Application Examples with
> PLCopen Motion Control`、`Application Examples for Motion Control in OOP
> v1.0`、配套 `OopMotionControlLibrary.xml`，以及 PackML 映射、程序结构化、
> 软件评价三份指南。原文只保存在 gitignore 的 `refs/plcopen-specs/`；本文仅
> 记录章节、仓库自述要求、判定和证据。

判定：✅ 支持；⚠️ 部分支持/仅有底层能力；❌ 缺失；➖ 指南或示例不构成
运行时合规要求。

## 1. 结论

这 7 个新增文件（4 个 Motion/Annex 文件 + 3 份 Logic 指南）已全文检查。
仓库的 C++ 运动内核能够承载传统 Motion 示例中的单轴、组运动、gear/cam、
队列和 blending，但没有交付官方示例工程；ST 当前也不能直接运行这些示例。
OOP Motion 的 `itfAxis`/`itfCommand` 接口族、方法返回 command object、属性和
继承合同均未实现，不能用 C++ 类名相似来声称 PLCopen OOP 合规。

Annex F 是 informative 示例集，不是新增强制 FB 标准。其 20 个示例单元中，
现有 ST 能完整表达的只有纯标量组合逻辑子集；用户 POU、ARRAY/STRUCT、SFC、
配置/任务及完整模拟量库仍缺失。

## 2. Annex F（46 页）

| 条目 | 示例主题 | 判定 | 仓库证据与边界 |
|------|----------|------|----------------|
| F.1 | `WEIGH` 函数、BCD 转换与标定 | ❌ | ST 无用户定义 FUNCTION；当前仅单 PROGRAM（`core/st/README.md`） |
| F.2 | `CMD_MONITOR` 命令监控、自动/手动模式、超时 | ⚠️ | BOOL/TIME、TON 与 CASE 可表达内部逻辑，但无用户 FB/多实例 POU |
| F.3 | `FWD_REV_MON` 双向互锁与报警 | ⚠️ | 标量逻辑/定时器可表达；没有可复用用户 FB 声明和 LD/FBD 执行面 |
| F.4 | `STACK_INT`，128 项堆栈与溢出/下溢 | ❌ | ST 尚无 ARRAY、用户 FB 和索引存储模型 |
| F.5 | `MIX_2_BRIX` 顺序混料 | ❌ | 示例核心是 SFC；仓库明确无 SFC（`core/st/README.md`） |
| F.6.1 | `LAG1` 一阶滤波 | ⚠️ | C++ 可实现数值式，ST 有 REAL/LREAL 运算；无标准化示例 FB |
| F.6.2 | `DELAY` N 样本延迟 | ❌ | 依赖 ARRAY/历史缓冲和用户 FB |
| F.6.3 | `AVERAGE` 滑动平均 | ❌ | 同上，缺数组及可复用实例 |
| F.6.4 | `INTEGRAL` 积分 | ⚠️ | 标量状态可在 PROGRAM 中手写；无示例 FB、采样周期封装 |
| F.6.5 | `DERIVATIVE` 微分 | ⚠️ | 同上，能手写但无库合同 |
| F.6.6 | `HYSTERESIS` | ✅/⚠️ | ST 标量逻辑可直接表达；未作为命名库 FB 交付 |
| F.6.7 | `LIMITS_ALARM` | ⚠️ | 标量比较可表达；无用户 FB |
| F.6.8 | `ANALOG_LIMITS` 结构 | ❌ | ST 尚无 STRUCT |
| F.6.9 | `ANALOG_MONITOR` | ❌ | 依赖结构和多个用户 FB 组合 |
| F.6.10 | `PID` 组合 | ❌ | 没有 PID/模拟量标准库，也无用户 FB 组合面 |
| F.6.11 | `DIFFEQ` 差分方程 | ❌ | 依赖系数/历史 ARRAY |
| F.6.12 | `RAMP` 时间斜坡 | ⚠️ | C++ 运动规划器有更强轨迹能力，但不是该 IEC 示例 FB；ST 无命名实现 |
| F.6.13 | `TRANSFER` 无扰切换 | ⚠️ | C++ 接管/同步路径有连续性合同，不能替代该过程控制 FB |
| F.7 | `GRAVEL` 配置、程序与 SFC | ❌ | 缺 CONFIGURATION/RESOURCE/TASK、多 PROGRAM、SFC |
| F.8 | `AGV` 程序示例 | ❌ | 缺示例所需完整 POU/配置和图形语言面 |

Annex F 只说明 IEC 61131-3 的组合表达方式；它不会把上述名称变成 PLCopen
Motion FB，也不能用 C++ 内部的滤波器、规划器或状态机替代语言级示例交付。

## 3. 传统 Motion 应用示例（4 页）

| 示例 | 要求/使用面 | 判定 | 证据 |
|------|-------------|------|------|
| 标签机 | FIFO、触发后相对运动、缓冲命令时序 | ⚠️ | `FbMoveRelative`、队列/BufferMode 可用；缺应用 FIFO、示例程序及 ST MC 完整调用面 |
| 仓储（Part 1） | X/Y/Z/Lifter 多轴顺序、Done/Busy 联锁 | ⚠️ | 单轴门面可组合；未交付该应用程序和时序回放 |
| 仓储（Part 4） | 组、线性路径、corner-distance blending | ⚠️ | `AxisGroup`、`FbMoveLinear*` 与部分 TransitionMode 已有；P4-B1 后 Part 4 为 40/68 同名门面 |
| 时间图 | 命令交接必须与 Busy/Active/Done 一致 | ⚠️ | 单项生命周期已有测试；没有针对官方两个示例的端到端 oracle |

## 4. OOP Motion 示例与库（46 页 + XML）

配套 XML 中可复算出 6 个接口：`itfAxis`、`itfCommand`、
`itfAxisCommand`、`itfContinuousAxisCommand`、
`itfSynchronizedAxisCommand`、`itfCamTable`；并定义 34 个 Method 节点。

| 合同族 | 官方资料要求 | 判定 | 仓库对照 |
|--------|--------------|------|----------|
| command object | 方法返回命令接口；命令暴露状态、引用号以及 Abort/Wait | ❌ | 现有 FB/Axis API 返回结果或命令 id，无 PLCopen `itfCommand` 对象模型 |
| command 继承 | Axis/continuous/synchronized command 逐层扩展 | ❌ | ST 无 INTERFACE/EXTENDS/动态绑定；C++ 内部继承不是该公开合同 |
| `itfAxis` 属性 | Actual 值、状态、错误和轴状态枚举 | ⚠️ | C++ read FB/AxisSnapshot 有数据；没有标准属性接口 |
| `itfAxis` Control | Power、Reset、Home、SetPosition、SetOverride 等方法 | ⚠️ | 多数经典 FB 门面存在，但不是 OOP Method 及返回 command object 形态 |
| 单轴 Motion | MoveAbsolute/Relative/Additive/Velocity/Stop/Halt/Profile 等 | ⚠️ | C++ 运行能力广泛存在；OOP 公共接口缺失，Part 1 的 D-01～D-20 仍适用 |
| 多轴同步 | Gear/Cam/Phasing/CombineAxes | ⚠️ | `core/fb/sync.h` 有经典门面；OOP command/property 合同缺失 |
| Cam table | Select 与 table interface | ⚠️ | `CamTableView`/`FbCamTableSelect` 可用；不是 `itfCamTable` |
| Part 4 OOP 扩展 | group command、position/velocity/acceleration/path/group 接口 | ❌/⚠️ | 有 AxisGroup 和部分经典 Part 4 门面；无文档所述 OOP 接口族，且 28/68 标准 FB 无同名入口 |
| 示例工程 | 仓储、标签、gear/cam 的 classic→OOP 对照 | ❌ | 未包含可构建/可运行的等价示例和端到端测试 |

## 5. Logic 指南

### 5.1 PackML 状态图映射（4 页）

| 要点 | 判定 | 说明 |
|------|------|------|
| PackML 状态、acting/wait/dual 分类与 State Complete | ❌ | 仓库没有 PackML 状态模型、命令集或 OEE 数据合同 |
| 正常、hold、suspend、stop、abort 序列 | ❌ | 运动轴状态机只服务 PLCopen Motion，不是机器级 PackML |
| 用 SFC 映射状态图 | ❌ | 当前无 SFC |
| 分散式错误处理与模块组合 | ⚠️ | C++/ST 有显式错误码和结构化控制流，但没有 PackML 模块接口 |

### 5.2 程序结构化七步法（4 页）

| 要点 | 判定 | 说明 |
|------|------|------|
| 外部接口、主信号、操作员交互先定义 | ⚠️ | C++ 公共 API 与 adapter 边界明确；没有 IEC 项目级 I/O 配置模型 |
| 自顶向下分区并定义 POU | ⚠️ | 仓库模块分层清晰；ST 仅单 PROGRAM，无用户 POU |
| 定义 scan 周期要求 | ✅/⚠️ | VM load 必须提供 `task_period_ns`，但无 IEC TASK 调度语法 |
| CONFIGURATION/RESOURCE/TASK 绑定 | ❌ | L5 未实现 |
| 纯符号编程、避免 jump、统一命名 | ✅/⚠️ | ST 无 GOTO/绝对地址，符号表与命名规则部分具备；多 POU 规则待 L2b |
| 用 SFC 分解主序列 | ❌ | L6 未实现 |

### 5.3 软件评价（2 页）

这是采购/供应商评价清单，不是运行时规格。仓库能够提供自动测试、文档、
版本与公开问题证据；但 PLCopen 认证、供应商财务/支持/培训、报价与许可证
承诺均不是代码可自证事项。官方认证及 compliance statement 仍必须由人和
PLCopen 认证流程完成。

## 6. 缺口登记

| ID | 缺口 | 影响 |
|----|------|------|
| M-01 | 未交付 PLCopen OOP Motion 的接口、属性、方法和 command object 模型 | 不得宣称 OOP Motion 合规 |
| M-02 | OOP XML 示例库未纳入构建/导入/执行测试 | 官方示例不能在仓库复现 |
| M-03 | 传统标签机/仓储示例没有端到端程序与时序 oracle | 底层能力不能证明应用示例行为 |
| L-01 | Annex F 所需用户 FUNCTION/FB、多 POU、ARRAY/STRUCT 尚未完成 | 多数示例无法由当前 ST 编译执行 |
| L-02 | SFC、CONFIGURATION/RESOURCE/TASK 缺失 | F.5/F.7/F.8、PackML 和七步法无法落地 |
| L-03 | 无 Annex F 模拟量 FB 库（滤波、PID、差分、transfer） | 过程控制示例仅能零散手写 |
| L-04 | 无 PackML 状态模型和 SFC 映射 | 机器级状态/OEE 互操作为空白 |
| L-05 | PLCopen 认证与供应商 compliance statement 未开展 | 只能陈述仓库证据，不能作官方认证声明 |

## 7. 范围裁决

- Annex F 和三份短指南为 informative/方法性资料，审计用于暴露语言与应用
  工程缺口，不自动扩大当前产品实现范围。
- OOP Motion 是明确的官方接口方案，但 ROADMAP 仍将 OOP 作为人专属待裁；
  在裁决和语义矩阵批准前只登记，不实现。
- 传统 Motion 示例可作为未来集成测试素材；在示例真正落地前，只能宣称
  对应底层能力存在，不能宣称示例兼容。

---

*创建：2026-07-12。审计依据固定于 `plcopen-specs-manifest.md` 的 SHA256。*
