# ST 批次 L2a 语义矩阵：PLCopen 引脚事实源 + MC_* 绑定 + AXIS_REF

> 状态：**已预批准**（2026-07-12，维护者批准合规计划后续全部调整）。
> 2026-07-12 原文审计修订：引脚表以 Part 1 v2.0 附录 B3 为唯一规范源，
> 不再从 C++ 字段反推 PLCopen 接口。L2a 分为同一矩阵下的两个原子阶段：
> **L2a-Spec** 先交付 43 FB / 236 B / 302 E 的机读事实源与声明生成门禁；
> **L2a-Bind** 再由同一事实源生成/校验 ST 绑定，禁止第二套手写引脚表。
>
> 本文件是 L 系列批次 L2a 的验收规格
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
| L2a 内部交付 | **L2a-Spec** = 官方 B3 机读事实源 + 声明表；**L2a-Bind** = AXIS_REF + ST 绑定实现 | 先锁定规范输入，再实现消费者；两阶段都可独立门禁和回滚 |
| GROUP_REF/组 FB | 显式推后（L2c 或随需）：组 FB 需要 GROUP_REF 与组生命周期语义映射 | 单轴面先闭环；组门面 FB 的 ST 绑定另批 |

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| L0/L1a 全部已批语义（KB-069/070） | 既有程序字节码逐位不变（新 FB 类型与指令仅追加）；两个锚点哈希保持 |
| C++ MC FB 门面（`fb/motion.h` 等） | L2a-Spec 不改生产代码；L2a-Bind 只做字段适配。D-01～D-20 未关闭项必须在声明表标为语义未通过，不能因引脚存在而宣称合规 |
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

## 3. 决策点：PLCopen 引脚事实源与绑定面

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 3.1 | 规范 FB 集合 | Part 1 v2.0 附录 B3 的全部 43 个 FB；规范事实源必须精确包含 236 个 B 引脚和 302 个 E 引脚 | 合规声明按供应商选择的 FB 子集提交，但规范库不能只记录首批实现子集 |
| 3.1a | 首批绑定集合 | L2a-Bind 首批仍为单轴核心 10 块：`MC_Power`、`MC_MoveAbsolute`、`MC_MoveRelative`、`MC_MoveAdditive`、`MC_MoveVelocity`、`MC_Halt`、`MC_Stop`、`MC_Reset`、`MC_Home`、`MC_SetOverride` | 绑定实现保持可验收切片；未绑定的 33 项在声明表明确为 unavailable，不从规范源删除 |
| 3.1b | 单一事实源 | `plcopen-motion-part1-io.yml` 保存 FB、引脚名、方向、B/E 等级；生成附录 B3 风格 Markdown 声明表，ST PinTable 及其一致性测试只消费/核对该源 | 规范、声明和语言接口不再漂移 |
| 3.2 | 引脚类型映射 | L2a-Spec 首阶段按附录 B3 保存名字、方向与 B/E 等级；类型由正文 FB 表交叉核对后加入，不能从 C++ 字段猜测。ST 映射使用项目类型策略（如规范 REAL→LREAL、WORD ErrorID→DINT）并单列 `st_type`，不得覆盖规范原始类型 | 区分官方合同与项目承载类型，保留审计证据 |
| 3.3 | BufferMode 引脚 | 以 **INT 编码**绑定：0=aborting、1=buffered、2=blending_low、3=blending_previous、4=blending_next、5=blending_high、6=blending_cnc（映射 axis::BufferMode，超域 = FB Error invalid_argument）；`MC_BUFFER_MODE` 枚举语法糖随 L1b 枚举落地后补 | 枚举类型属 L1b；先给显式编码不堵 buffered/blending 用法 |
| 3.4 | 调用形态 | 与 basic.h 同：命名形参 + 非正式全覆盖两种；`Axis :=` 实参只接受 AXIS_REF 变量（表达式/字面量 = 编译错误）；输出经 `inst.Done` 等只读 | 机制复用，零新文法 |
| 3.5 | 未赋输入保持 | 沿 KB-069 口径：未赋 input 保持上次值（含 Axis——绑定一次后续调用可省略） | IEC FB 调用语义连续 |
| 3.6 | Direction/Home 输入 | `MC_MoveAbsolute.Direction` 与 `MC_MoveVelocity.Direction` 使用 INT 编码（0=current、1=positive、-1=negative、2=shortest_way；MoveVelocity 不接受 shortest_way 时走 FB Error）；`MC_Home.Position` = LREAL | 对齐已批准 P1-A1 Direction 矩阵；枚举语法糖仍随 L1b |

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
| 6.2 | L2a-Spec 事实源计数与官方附录 B3 一致 | 43 FB、236 B、302 E；每项含方向且名称唯一 |
| 6.2a | 由事实源生成附录 B3 风格声明表；生成物陈旧时 CI 失败 | 逐字节同步 |
| 6.2b | L2a-Bind 首批 10 FB 全引脚（名/类型/方向/等级）由同一事实源核对，锚点通过 | 全表；禁止手写第二事实源 |
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
| 首批 10 项之外的 33 个 Part 1 FB 绑定 | 后续 L2a-Bind 扩表批；规范引脚已在 L2a-Spec 全量记录 |
| ST 内创建/销毁轴对象 | 永不（轴生命周期归宿主） |

---

*草案创建并按官方 B3 重写、预批准：2026-07-12。*
