# ST 批次 L2a 语义矩阵：MC_* 绑定 + AXIS_REF（草案）

> 状态：**草案，待维护者批准**。本文件是 L 系列批次 L2a 的验收规格
> （normative）。依据：T34（POU 与实例模型——本批次只取其 MC 绑定
> 半边）、已批准的 [st-l0-semantics.md](st-l0-semantics.md) 与
> [st-l1a-semantics.md](st-l1a-semantics.md)（全部合同为不变量）。
> 这是语言层的**身份闭环件**：`MC_MoveAbsolute(Axis := AxisX,
> Execute := TRUE, Position := 100.0)` 让 plcopen 从"合规的库"变成
> "PLC 的心智模型"（st-runtime-design §1 的原始论据）。

## 0. 批次拆分（本矩阵的第一个决策）

| 决策点 | 提案 | 理由 |
|---|---|---|
| L2 拆分 | **L2a**（本矩阵）= AXIS_REF 句柄 + 单轴 MC_* 绑定表 + 宿主绑定 API；**L2b**（另立矩阵）= 用户定义 FUNCTION/FUNCTION_BLOCK/PROGRAM、VAR_IN_OUT 别名、EN/ENO、递归禁止 | MC 绑定骑在既有 FB 调用机制上（L0 已验证 CALL/实例内存合同），用户 POU 是编译器结构工程——两者无耦合 |
| GROUP_REF/组 FB | 显式推后（L2c 或随需）：组 FB 需要 GROUP_REF 与组生命周期语义映射 | 单轴面先闭环；组门面 FB 的 ST 绑定另批 |

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| L0/L1a 全部已批语义（KB-069/070） | 既有程序字节码逐位不变（新 FB 类型与指令仅追加）；两个锚点哈希保持 |
| C++ MC FB 门面（`fb/motion.h` 等） | 一行不改；ST 绑定是其字段面的纯消费者，Done/Busy/Aborted 生命周期语义（含"Done 在 Execute 保持期锁存"）原样透传 |
| 单写者合同（KB-068/ADR-0007） | 含 MC 绑定的 `Instance::scan()` 必须运行在轴所有者上下文（参考 executor 的规划线程）——ST 程序是规划域的命令源，不是 RT 采样路径 |
| basic.h 绑定（KB-069） | 机制共用（同一 CALL 指令与实例区），引脚表并列扩展 |

## 2. 决策点：AXIS_REF

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 2.1 | 类型形态 | `AXIS_REF` 为不透明句柄类型：只能声明变量、只能作 MC FB 的 `Axis :=` 实参；赋值/运算/比较/转换一律编译错误（专用稳定码） | 未定义组合显式报错；句柄语义不给用户任何算术面 |
| 2.2 | 绑定时机 | 宿主在 load 后、首个 scan 前经 `Instance::bind_axis(name, AxisModel*)` 绑定（大小写不敏感名字查找）；绑定为加载域 API | 轴对象生命周期归 executor，ST 只持句柄（T34"AXIS_REF 解析为轴对象句柄"） |
| 2.3 | 未绑定语义 | 未绑定的 AXIS_REF 参与 MC 调用不阻止 load/scan：FB 走既有 `axis_ref == nullptr` 错误路径（Error + ErrorID=invalid_argument） | 与 C++ 门面语义完全一致，零新语义 |
| 2.4 | 重绑定 | 仅 fault 态或首个 scan 前允许 `bind_axis` 覆盖；运行中重绑 = `precondition_failed` | 防运行中偷换轴对象 |
| 2.5 | 生命周期（宿主合同） | 轴对象必须比绑定长寿：宿主销毁 AxisModel 前必须先停 scan 并 reload/解除绑定——悬空绑定属宿主合同违约（与 Program 生命周期合同同族，文档显式声明+集成指南强调）；名字复用（rebind 同名到新对象）走 2.4 重绑定规则，无静默切换 | Codex 外部评审输入：悬空/销毁/复用在真实部署是非确定性故障源，边界必须显式 |

## 3. 决策点：MC FB 绑定面（L2a 集合）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 3.1 | FB 集合 | 单轴核心 10 块：`MC_Power`、`MC_MoveAbsolute`、`MC_MoveRelative`、`MC_MoveAdditive`、`MC_MoveVelocity`、`MC_Halt`、`MC_Stop`、`MC_Reset`、`MC_Home`、`MC_SetOverride`——全部为 `fb/motion.h` 既有门面的直绑 | 覆盖"上电-运动-停-复位"完整生命周期；其余单轴面（叠加/触探/相位/剖面/读写参数）随后续批按同机制扩表 |
| 3.2 | 引脚类型映射 | Execute/Enable/Done/Busy/Active/CommandAccepted/CommandAborted/Error/Status/Valid ↔ BOOL；Position/Distance/Velocity/Acceleration/Deceleration/Jerk/OverrideFactor ↔ LREAL；ErrorID ↔ DINT（rt::ErrorCode 数值）；CommandID ↔ UDINT；机读引脚表延续 pins.h 模式并进一致性矩阵 | LREAL=双精度 double 直通；错误码数值与 ErrorCode 诊断文本表对齐 |
| 3.3 | BufferMode 引脚 | 以 **INT 编码**绑定：0=aborting、1=buffered、2=blending_low、3=blending_previous、4=blending_next、5=blending_high、6=blending_cnc（映射 axis::BufferMode，超域 = FB Error invalid_argument）；`MC_BUFFER_MODE` 枚举语法糖随 L1b 枚举落地后补 | 枚举类型属 L1b；先给显式编码不堵 buffered/blending 用法 |
| 3.4 | 调用形态 | 与 basic.h 同：命名形参 + 非正式全覆盖两种；`Axis :=` 实参只接受 AXIS_REF 变量（表达式/字面量 = 编译错误）；输出经 `inst.Done` 等只读 | 机制复用，零新文法 |
| 3.5 | 未赋输入保持 | 沿 KB-069 口径：未赋 input 保持上次值（含 Axis——绑定一次后续调用可省略） | IEC FB 调用语义连续 |
| 3.6 | Direction/Home 输入 | `MC_MoveVelocity.Direction` 以 INT 编码（1=positive、-1=negative、0=current，超域 FB Error）；`MC_Home.Position` = LREAL | 同 3.3 编码先行原则 |

## 4. 决策点：执行域与实例模型

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 4.1 | 域声明 | 含 MC 绑定的 scan() 归**规划域**（与 C++ 侧 FB `call()`/axis submit 同域）：在参考 executor 形态下运行于规划线程，坐在承诺轨迹环之前；**不是** RT 采样路径 | ADR-0007 三线程形态的自然位置：ST 程序 = 规划域命令源 |
| 4.2 | 零分配维持 | scan() 含 MC 调用仍零堆分配（axis submit 本就 RT 纪律代码）；冻结窗口断言扩展覆盖 MC 调用路径 | 纪律不因域放松而放松 |
| 4.3 | 实例存储 | MC FB 实例同 basic.h 走加载期静态区（门面类无虚函数、可平凡拷贝，静态断言看住）；AXIS_REF 槽存于变量区（8 字节句柄，符号表标记类型不可读值） | L0 实例内存合同直接外延 |
| 4.4 | 指令预算口径 | MC CALL 计 1 条指令但真实成本为规划域级（含 OTG 求解）——声明：指令预算是确定性看门狗不是时间预算，MC 场景的时间预算归 executor 周期设计（与 C++ 用户调 submit 同责任面） | 诚实声明，不给"预算=微秒"的错觉 |

## 5. 退化与拒绝规则（进验收测试）

| 形态 | 语义 |
|---|---|
| AXIS_REF 赋值/运算/比较/转换/作非 Axis 引脚实参 | 编译错误 `sema_operand_type_invalid` |
| `Axis :=` 传非 AXIS_REF 表达式 | 编译错误 `sema_type_mismatch` |
| 未绑定句柄参与 MC 调用 | 运行期 FB Error + ErrorID=invalid_argument（scan 不 fault） |
| 运行中 `bind_axis` | `precondition_failed`，原绑定不变 |
| BufferMode/Direction 编码超域 | FB Error + invalid_argument（门面既有路径） |
| 读 MC 输入引脚 / 写输出引脚 | 沿 KB-069：`sema_pin_not_output` / `sema_pin_not_input` |
| 组 FB 名（MC_GroupEnable 等） | 专用 unsupported 诊断（归属 L2c） |

## 6. 验收指标

| # | 指标 | 门槛 |
|---|------|------|
| 6.1 | 端到端黄金场景：ST 程序（Power→MoveAbsolute→Done→MoveRelative buffered→Halt→Stop→Reset）驱动 AxisModel+ServoSim，逐扫描断言 Done/Busy/Aborted 生命周期与位置终点 | ≥6 场景全绿，含 aborting 接管与 buffered 排队 |
| 6.2 | 引脚表机读一致性：10 FB 全引脚（名/类型/方向）进 st-l2a-conformance，锚点校验通过 | 全表 |
| 6.3 | AXIS_REF 拒绝面 5.x 逐条 + 未绑定/重绑定语义 | 全绿 |
| 6.4 | 与 C++ 门面等价：同一命令序列 ST 路径与 C++ 直调路径的轴 setpoint 逐周期逐位一致 | 逐位 |
| 6.5 | 既有字节码/锚点哈希/回放 | 逐位不变 |
| 6.6 | scan() 含 MC 调用零分配 + fuzz 语料扩展（MC 名/AXIS_REF 进生成器） | 断言 0 / Nightly 维持 |
| 6.7 | 参考 executor 集成冒烟：规划线程跑 ST scan 产生运动命令，经承诺轨迹环到 RT 线程（ADR-0007 形态闭环） | demo PASS |

## 7. 不做清单（显式拒绝，带归属）

| 构造 | 归属 |
|------|------|
| 用户定义 FUNCTION/FUNCTION_BLOCK/PROGRAM、VAR_IN_OUT、EN/ENO、递归检测 | L2b |
| GROUP_REF 与组 FB（MC_GroupEnable/MoveLinear* 等）、CAM/GEAR 的表句柄类型 | L2c |
| MC_BUFFER_MODE/MC_DIRECTION 枚举语法糖 | L1b 枚举落地后补 |
| 叠加/触探/相位/剖面/读写参数/读状态 FB 绑定 | 后续扩表批（机制同 L2a） |
| ST 内创建/销毁轴对象 | 永不（轴生命周期归宿主） |

---

*草案创建：2026-07-12。批准后本节改为批准记录 + 实现记录。*
