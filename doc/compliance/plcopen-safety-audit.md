# PLCopen Safety 官方资料全文仓库对照审计

> **范围**：Safety Part 1 v2.10（194 页）、Part 2 v1.0（42 页）、Part 3 v1.0
>（61 页）、Part 4 v1.0（136 页）、Safe Motion v1.0（45 页）、Logic, Motion,
> and Safety（15 页），合计 **493 页**（以 `pdfinfo` 实测与规格 manifest 为准）。
>
> **口径**：仅按条款号和中文自述对照，不复制规格原句。`✅支持` 表示仓库有
> 对应安全能力及证据；`⚠️部分` 仅表示普通运动/适配原语可被外部安全系统使用，
> **不代表安全认证**；`❌缺失` 表示没有安全实现；`⛔门控` 表示项目明确禁止在
> 缺少认证语境时实现或宣称。
>
> **总判定**：仓库没有 `SF_*` 类型、safe data type、安全运行时、安全通信栈或
> 安全认证证据。VISION 明确把 PLCopen Safety FB 与 SIL 认证列为未解锁
>（`VISION.md:165,170`）。普通 `FbEmergencyStop`、CiA402 与运动限值均不是
> PLCopen Safety 实现。

## 1. Safety Part 1 — Concepts and Function Blocks

### 1.1 平台与通用模型

| 章节 | 要求（自述） | 判定 | 仓库证据 |
|------|-------------|------|----------|
| §1 | 安全库需要独立的目的、版本、认证声明与变更控制 | ⛔门控 | 无安全产品/认证主体；项目明确“无认证的安全 FB 不做”（`VISION.md:165`） |
| §2 | 安全能力需在适用安全标准、风险评估和术语边界内使用 | ⛔门控 | 无 ISO 13849/IEC 62061/IEC 61508 安全生命周期、SIL/PL 声明（`VISION.md:170`） |
| §3.1 | 安全应用、标准应用、设备与安全通信之间须有受控架构边界 | ❌缺失 | core 仅普通运动 L0-L7；adapter 明确保持语义桥接而非安全域（`core/adapters/README.md:11-16`） |
| §3.2 | 安全信号使用独立安全数据类型并限制隐式混用 | ❌缺失 | ST 仅 BOOL/INT/DINT/REAL/LREAL/TIME，无 SAFEBOOL 等安全类型（`doc/compliance/st-l0-semantics.md`） |
| §3.3 | 安全程序需满足确定性、初始化、复位和安全输出约束 | ❌缺失 | 普通确定性与 RT 门禁存在，但无安全故障假设、双通道诊断或安全输出证明（`doc/compliance/ci-gates.md`） |
| §4 | 开发环境必须限制语言、数据类型、函数、控制流和在线修改能力 | ❌缺失 | ST L0 是功能子集，但不是经认证的 safety profile，也无受控下载/变更环境（KB-069） |
| §5.1 | 所有 SF FB 共享 Activate、Reset、Ready、Error、DiagCode 等生命周期 | ❌缺失 | `rg SF_ core src` 零实现；普通 `MotionOutputs` 接口不同（`core/fb/motion.h:12-22`） |
| §5.2 | 诊断码需按状态、错误、复位请求和安全需求统一编码 | ❌缺失 | `rt::ErrorCode` 是普通运动错误枚举，不含 Safety DiagCode 状态协议（`core/rt/error.h`） |
| §5.3 | Diagnostic FB 应向非安全应用提供受控诊断视图 | ❌缺失 | 无 SF_Diagnostic 或安全/标准域网关 |
| §5.4–5.5 | SF FB 应遵循统一安全状态图及简化表示规则 | ❌缺失 | 无 Safety 状态机；AxisStatus 只描述普通运动状态（`core/axis/state.h:20-28`） |
| §5.6 | Reset 必须防止意外启动，并符合手动/自动复位约束 | ❌缺失 | `FbReset` 仅清普通轴 ErrorStop，不实现安全复位防重启（`core/fb/motion.h:140-162`） |

### 1.2 Part 1 安全 FB 清单（19/19）

下列每个 FB 的接口、功能状态、错误检测、错误行为和专用诊断码均已按其
§6.x.1–§6.x.6 核对；仓库没有同名或等价安全实现。

| 章节 / FB | 安全功能（自述） | 判定 | 证据/边界 |
|-----------|------------------|------|-----------|
| §6.1 SF_ResetButton | 监测复位按钮沿与持续粘连，生成合规复位脉冲 | ❌缺失 | 无安全复位按钮 FB |
| §6.2 SF_Equivalent | 双通道等价输入的一致性与差异时间监测 | ❌缺失 | 无双通道安全输入诊断 |
| §6.3 SF_Antivalent | 双通道反价输入的一致性与差异时间监测 | ❌缺失 | 同上 |
| §6.4 SF_ModeSelector | 多安全模式互斥选择、锁存与切换监测 | ❌缺失 | 无安全模式管理 |
| §6.5 SF_EmergencyStop | 急停输入、复位和安全输出状态机 | ❌缺失 | `FbEmergencyStop` 只调用普通 `trigger_error()`，无双通道/Reset/DiagCode，不能等同（`core/fb/probe.h:136-170`） |
| §6.6 SF_ESPE | 光电保护设备输入与启动/复位互锁 | ❌缺失 | 无安全传感器域 |
| §6.7 SF_PSE | 压敏保护设备输入与复位互锁 | ❌缺失 | 无对应 FB |
| §6.8 SF_TwoHandControlTypeII | II 型双手同步操作监测 | ❌缺失 | 无对应 FB |
| §6.9 SF_TwoHandControlTypeIII | III 型双手同步、释放与错误监测 | ❌缺失 | 无对应 FB |
| §6.10 SF_TestableSafetySensor | 带测试脉冲的安全传感器诊断 | ❌缺失 | 数字 IO 无安全测试/交叉短路诊断（`core/fb/io.h`） |
| §6.11 SF_MutingSeq | 四传感器顺序 muting | ❌缺失 | 无 muting 状态机 |
| §6.12 SF_MutingPar | 四传感器并行 muting | ❌缺失 | 无 muting 状态机 |
| §6.13 SF_MutingPar_2Sensor | 双传感器并行 muting | ❌缺失 | 无 muting 状态机 |
| §6.14 SF_EnableSwitch | 三位置使能开关含 panic 位置诊断 | ❌缺失 | 无安全使能开关 FB |
| §6.15 SF_EnableSwitch_2 | 不检测 panic 位置的使能开关变体 | ❌缺失 | 无对应 FB |
| §6.16 SF_Guard | 防护门监控与安全许可 | ❌缺失 | 无安全门域 |
| §6.17 SF_GuardLocking_2 | 防护门联锁、锁定与解锁序列 | ❌缺失 | 无对应 FB |
| §6.18 SF_GuardLockingSerial | 串联触点防护门锁监测 | ❌缺失 | 无对应 FB |
| §6.19 SF_Override | 受控 override 序列及超时/传感器监测 | ❌缺失 | `FbSetOverride` 是普通速度倍率，不是安全 override（`core/fb/motion.h:165-188`） |

### 1.3 Part 1 附录与合规

| 项目 | 要求（自述） | 判定 | 证据 |
|------|-------------|------|------|
| 附录 1 | 供应商声明开发环境限制、支持 FB 和安全数据类型 | ❌缺失 | 无 Safety supplier statement |
| 附录 2 | Safety 标志只能按授权规则用于合规产品 | ⛔门控 | 仓库无认证，不得使用 Safety logo 或作合规声明 |

## 2. Safety Part 2 — User Examples

| 章节 | 示例要求（自述） | 判定 | 仓库证据 |
|------|------------------|------|----------|
| §1 | 将安全计划、风险分析和验证置于应用实现之前 | ⛔门控 | 无安全项目生命周期或认证语境（`VISION.md:165,170`） |
| §2.1–2.4 | 建立安全计划、术语、生产线安全功能与 FB 责任边界 | ❌缺失 | 无 Safety 计划模板、SF 库或安全 I/O 模型 |
| §3.1 | SF FB 与安全外围设备需经安全输入/输出或认证设备接口连接 | ❌缺失 | Servo/CiA402 adapter 非安全通信（`core/adapters/servo.h:4-18`） |
| §3.2 | 应用图应明确安全域、标准域、输入设备和执行器边界 | ❌缺失 | 无 safety architecture 示例 |
| §3.3 | Safe drive 隐藏系统接口仍需由认证层完成安全功能映射 | ❌缺失 | 无 safe drive profile adapter |
| §4.1 | 标准应用应以 DiagCode/Ready/Error 诊断 SF 状态而不绕过安全逻辑 | ❌缺失 | 无 SF diagnostic surface |
| §4.2 | 隐藏接口的安全运动示例组合 SafeRequest、SafeStop、SLS | ❌缺失 | 三类 SF FB 均无；普通 Stop/SLS 限幅不构成安全监测 |
| §4.3 | Muting 示例需验证传感器顺序、时间窗和错误恢复 | ❌缺失 | 无 muting FB |
| §4.4 | I/O 接口的安全运动需显式请求、确认、超时和安全状态 | ❌缺失 | 无安全 I/O handshake |
| §4.5 | 双手控制示例需保持安全同步时间和释放约束 | ❌缺失 | 无 TwoHand SF FB |

## 3. Safety Part 3 — Library Implementation

| 章节 / FB | 实现要求（自述） | 判定 | 仓库证据 |
|-----------|------------------|------|----------|
| §1.1 | 新 FB 输出增加 SafetyDemand、ResetRequest 等统一诊断信号 | ❌缺失 | 无 SF 公共基类或输出结构 |
| §1.2 | 新诊断码编码应可由软件稳定分类错误、复位和安全需求 | ❌缺失 | `ErrorCode` 无该位域合同（`core/rt/error.h`） |
| §1.3 | 通用状态图需包含安全需求与复位请求路径 | ❌缺失 | 无 Safety 状态机 |
| §2.1 SF_GuardLocking_2 | 新版门锁状态机、解锁和诊断 | ❌缺失 | 无对应 FB |
| §2.2 SF_GuardLockingSerial | 串联触点门锁状态机 | ❌缺失 | 无对应 FB |
| §2.3 SF_PSE | 压敏设备安全状态机 | ❌缺失 | 无对应 FB |
| §2.4 SF_Diagnostic | 安全诊断到标准域的受控接口 | ❌缺失 | 无安全域隔离或网关 |
| §2.5 SF_Override | 安全 override 序列 | ❌缺失 | 普通倍率接口不等价 |
| §2.6 SF_EnableSwitch_2 | 双通道使能开关变体 | ❌缺失 | 无对应 FB |
| 附录 | 库供应商需声明受支持 FB 与开发环境约束 | ❌缺失 | 无 Safety 合规表或认证证据 |

## 4. Safety Part 4 — Extension for Presses

### 4.1 模型

| 章节 | 要求（自述） | 判定 | 仓库证据 |
|------|-------------|------|----------|
| §1–§2 | 压力机安全功能需结合机械风险、模式与适用标准 | ⛔门控 | 项目无压力机安全产品与认证语境 |
| §3.1–3.3 | 压力机 FB 使用统一安全接口、诊断与状态模型 | ❌缺失 | 无 SF 基础设施 |

### 4.2 压力机安全 FB（11/11）

| 章节 / FB | 安全功能（自述） | 判定 | 证据 |
|-----------|------------------|------|------|
| §4.1 SF_FootSwitch | 多位置脚踏开关与安全模式监测 | ❌缺失 | 无 SF 实现 |
| §4.2 SF_PressControl | 手动/设置/自动压力循环安全控制 | ❌缺失 | 无压力机状态机 |
| §4.3 SF_SingleValveMonitoring | 单阀 EDM 与切换超时监测 | ❌缺失 | 无安全阀 FB |
| §4.4 SF_SingleValveCycleMonitoring | 单阀跨周期状态监测 | ❌缺失 | 同上 |
| §4.5 SF_DoubleValveMonitoring | 双阀一致性、EDM 与超时监测 | ❌缺失 | 同上 |
| §4.6 SF_ValveGroupControl | 最多八阀登录、状态与输出控制 | ❌缺失 | 无安全阀组 |
| §4.7 SF_TwoHandMultiOperator | 多操作者双手控制仲裁 | ❌缺失 | 无对应 FB |
| §4.8 SF_CamshaftMonitor | 压力机凸轮轴启动/周期监测 | ❌缺失 | 普通 Cam 不是安全凸轮监测 |
| §4.9 SF_CycleControl | 安全启动、周期运行和停止控制 | ❌缺失 | 无对应 FB |
| §4.10 SF_CamMonitor | TDC/BDC 与方向凸轮信号监测 | ❌缺失 | 无对应 FB |
| §4.11 SF_TwoHandControlTypeIIIC | 压力机 IIIC 双手控制 | ❌缺失 | 无对应 FB |

| 附录 | 供应商支持表与 Safety logo 使用 | ⛔门控 | 未认证，不具备声明或标志使用条件 |

## 5. Safe Motion

### 5.1 安全运动功能与仓库边界

| 功能族 | 要求（自述） | 判定 | 仓库证据 |
|--------|-------------|------|----------|
| STO / SBC | 安全切断转矩与安全制动控制 | ❌缺失 | CiA402 skeleton 无安全通道；ROADMAP 仅计划 STO/SS1 边界文档（`ROADMAP.md:74`） |
| SS1 / SS2 | 受监控减速后进入 STO 或 SOS | ❌缺失 | `FbStop` 是普通规划器命令且已存在 Part 1 D-04，不是安全监控 |
| SOS | 安全保持静止并监测位置漂移 | ❌缺失 | 无独立安全传感器/监控通道 |
| SLS / SSM | 安全限速与安全速度监视 | ❌缺失 | 普通 `max_velocity`/override 不具安全完整性（`core/axis/state.h:370-389,486-533`） |
| SLP / SLA / SAR | 安全位置、加速度及范围监视 | ❌缺失 | 软限位/运动限值是功能控制，不是安全监视 |
| SDI / SLI | 安全方向与增量限制 | ❌缺失 | 无 SF 监控 |
| SLT / STR / SMT / SCA | 安全转矩、范围与凸轮相关监视 | ❌缺失 | 无认证扭矩反馈与安全逻辑 |

### 5.2 SafetyRequest 与网络映射

| 章节 | 要求（自述） | 判定 | 仓库证据 |
|------|-------------|------|----------|
| §4.1 | SF_SafetyRequest 统一发出安全功能请求并监测驱动确认、超时和错误 | ❌缺失 | 无 SF_SafetyRequest |
| §4.2–4.6 | 命名、单实例多轴、Safe Stop/SLS 与 PackAL 示例需保持请求/反馈语义 | ❌缺失 | 无 safe axis reference 或安全状态聚合 |
| §5.1 | 映射 OMAC 安全功能 | ❌缺失 | 无 PackAL Safety 层 |
| §5.2 | 映射 PROFIsafe profile | ❌缺失 | 无 PROFIsafe 栈 |
| §5.3 | 映射 FSoE | ❌缺失 | 无 Safety over EtherCAT 栈 |
| §5.4 | 映射 openSAFETY | ❌缺失 | 无 openSAFETY 栈 |
| §5.5 | 映射 CIP Safety over Sercos | ❌缺失 | 无相关栈 |
| §5.6 | 映射 CC-Link IE Safety | ❌缺失 | 无相关栈 |
| §5.7 | 映射 MECHATROLINK Safety | ❌缺失 | 无相关栈 |
| §6 | 对现有安全设备能力建立可追溯 SafetyRequest 映射表 | ❌缺失 | 无安全设备 capability model |

## 6. Logic, Motion, and Safety

| 章节 | 集成要求（自述） | 判定 | 仓库证据 |
|------|------------------|------|----------|
| §1–§2 | 标准运动状态与安全反应必须分域建模，通过清晰条件连接 | ⚠️部分 | 普通 AxisStatus/命令生命周期存在；无并行 Safety 状态机（`core/axis/state.h`） |
| §3 | 双独立轴示例由 EStop、Guard、Mode、EnableSwitch、SLS/SS1 控制普通运动 | ❌缺失 | 普通 Power/Stop/Profile 存在，但所有 SF 逻辑、safe feedback 与认证执行层缺失 |
| §4.1–4.3 | gear 主从示例需按最坏比率限制主轴，并保持同步关系下的安全反应 | ⚠️部分 | GearIn/ratio 更新存在；无安全限速 oracle、独立监控或安全停机证明（`core/fb/sync.h:111-164`） |
| §4.3 | 安全条件基于危险运动量，不应因控制拓扑是独立轴或同步轴而改变 | ❌缺失 | 当前只有功能所有权与组仲裁，无独立 hazard monitor（KB-068） |

## 7. 缺口登记

| 编号 | 缺口 | 影响资料 | 门控/修复方向 |
|------|------|----------|---------------|
| S-01 | 无安全认证语境、供应商声明、SIL/PL 生命周期与 logo 授权 | 全部 | **硬门控**：维持未解锁，需认证集成方与成本承担者 |
| S-02 | 无 SAFEBOOL 等安全数据类型、受限语言 profile 与认证开发环境 | Part 1 §3–4 | 不能把普通 ST 子集宣传为 Safety |
| S-03 | 无 SF 通用生命周期、状态图、DiagCode、SafetyDemand/ResetRequest | Part 1 §5、Part 3 §1 | 若解锁，先做独立安全架构/认证计划，不复用普通 MotionOutputs |
| S-04 | Part 1 的 19 个通用安全 FB 全缺失 | Part 1 §6 | 硬门控，不能以同名普通功能替代 |
| S-05 | 安全外围 I/O、双通道、EDM、测试脉冲与安全通信全缺失 | Part 1/2 | 需要认证硬件与通信栈边界 |
| S-06 | Part 3 的 6 个新增/修订 FB 与安全诊断网关全缺失 | Part 3 | 依赖 S-02/S-03 |
| S-07 | Part 4 的 11 个压力机安全 FB 全缺失 | Part 4 | 需压力机领域集成方与适用标准认证 |
| S-08 | STO/SS1/SS2/SOS/SLS 等 safe motion monitor/control 全缺失 | Safe Motion | ROADMAP 当前只允许先写 STO/SS1 集成边界 |
| S-09 | 七类安全网络/profile 映射全缺失 | Safe Motion §5 | 不得把普通 EtherCAT/CiA402 适配称为安全通信 |
| S-10 | 普通运动与安全状态的组合架构及两套机器示例未实现 | Logic+Motion+Safety | 可先形成“不做什么/责任边界”文档，不实现 SF 逻辑 |

## 8. 完成度统计

| 审计单元 | 数量 | 结果 |
|----------|------|------|
| 官方资料 | 6/6 | 全部目录与正文主题已覆盖 |
| Part 1 通用模型/规则组 | 11 | 0 支持，10 缺失，1 门控 |
| Part 1 安全 FB | 19/19 | 全缺失 |
| Part 2 章节/应用组 | 10 | 全缺失或门控 |
| Part 3 规则与 FB | 10 | 全缺失 |
| Part 4 模型组 | 2 | 缺失/门控 |
| Part 4 安全 FB | 11/11 | 全缺失 |
| Safe Motion 功能族 | 7 | 全缺失 |
| Safe Motion 映射章节 | 12 | 全缺失 |
| Logic+Motion+Safety 章节组 | 4 | 2 部分、2 缺失 |
| 已登记缺口 | S-01～S-10 | 10 项 |

---

*创建：2026-07-12。仅登记仓库事实与安全门控，不修改实现，不形成任何
PLCopen Safety、SIL 或 PL 合规声明。*
