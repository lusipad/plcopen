# PLCopen Part 3 User Guidelines v2.0 全文审计

> **性质**：Part 3 是应用指南与用户派生 FB 示例，不是 Part 1/4 的认证条款。
> 本审计按目录逐节对照仓库能力，判定“可直接组合 / 部分支持 / 缺失”，不把
> 示例接口误当成强制 B 级接口。出处纪律：仅使用章节号与中文自述。
>
> **范围**：§1.1–1.4、§2.1–2.18、§3.1–3.3，全部正文 **25/25 节**。
> **判定口径**：`✅支持` / `⚠️部分支持` / `❌缺失` / `➖指南性`。

## §1 总则

| 条款 | 指南要点（自述） | 判定 | 证据/说明 |
|------|------------------|------|----------|
| 1.1 | 用标准运动 FB 组合可复用应用功能，并给出行业示例 | ➖指南性 | 仓库定位为可嵌入运动内核；应用 UDFB 应位于上层（`README.md`; `core/fb/README.md`） |
| 1.2 | 用户可由标准 FB 组合派生复合 FB，派生接口不属于基础规范 | ⚠️部分支持 | C++ FB 可组合，但仓库未提供 Part 3 的 UDFB 库；ST 的 MC 调用属于 L2 后续范围（`doc/compliance/st-l0-semantics.md`; KB-069） |
| 1.3 | 图形和文本表示可表达同一控制逻辑 | ⚠️部分支持 | 目前只有 C++ 与 ST 文本路径，无 LD/FBD/SFC 图形编辑或执行面（`core/st/README.md`） |
| 1.4 | 历史章节仅说明版本演进 | ➖指南性 | 不产生实现要求 |

## §2 应用示例

| 条款 | 应用/模式（自述） | 判定 | 证据/说明 |
|------|------------------|------|----------|
| 2.1 | 基础轴上电、回零、运动、停止与错误复位顺序 | ⚠️部分支持 | Power/Home/Move/Halt/Stop/Reset 均有 FB，C4 已关闭 Stop 与 Execute 生命周期缺口；Home 仍仅普通轨迹置 homed |
| 2.2 | 标签机以虚拟主轴、gear/cam 与触发协调送料和工艺轴 | ⚠️部分支持 | Gear/Cam/TouchProbe/DigitalCamSwitch 基元存在；无成品标签机 UDFB，CamTable 为直接 view 且 DCS 仅单轨（KB-016；Part 1 D-13） |
| 2.3 | 仓储搬运用绝对/相对及协调直线运动组织多轴路径 | ✅支持 | 单轴 MoveAbsolute/Relative 与组 MoveLinearAbsolute/Relative 均实现并有验收（`core/fb/motion.h:191-242,713-774`; `core/test/r3_group_fb_tests.cpp`） |
| 2.4 | Jog 由正/反方向电平持续运动，释放后受控停止并处理互斥输入 | ❌缺失 | 无 `FbJog`/`MC_Jog`；可用 MoveVelocity+Halt 组合但没有指南接口与方向互斥生命周期（`core/fb/motion.h:256-309`） |
| 2.5 | Inch 在触发沿执行固定距离，允许规定的中止/重触发行为 | ❌缺失 | 无 `FbInch`；MoveRelative 是低层基元，不等于示例 UDFB（`core/fb/motion.h:228-239`） |
| 2.6 | JogToPosition 在目标前按制动距离切换速度/定位并输出完成状态 | ❌缺失 | 无 `FbJogToPosition`；现有连续与绝对运动未封装该复合状态机（`core/fb/motion.h:256-421`） |
| 2.7 | 机械联动双轴持续比较位置差与报警，输出互锁状态 | ❌缺失 | 无 `FbAxesInterlock`，也无跨轴容差监控 UDFB；只提供单轴读回基元（`core/fb/parameter.h:239-342`） |
| 2.8 | 虚拟 MasterEngine 统一连续运行、定位停车、inch 与从轴同步 | ❌缺失 | 无 `FbMasterEngine`；基础 MoveVelocity/Continuous/Cam/Gear 可组合，但未实现该 UDFB |
| 2.9 | CamTableSelect 支持准备/选择曲线并供 CamIn/CamOut 使用 | ⚠️部分支持 | 三个 FB 均存在；CamTableSelect 使用调用方持有的 `CamTableView`，无控制器表仓库、ID 切换准备链（`core/fb/sync.h:248-341`; KB-016） |
| 2.10 | 三段 cam 将启动、循环、停止曲线连续缓冲连接 | ⚠️部分支持 | Cam 基元与 buffered 同步入口存在，但无三段 cam UDFB，且表切换/全部 blending 模式未覆盖（`core/fb/sync.h:288-341`; Part 1 附录 A） |
| 2.11 | 定长切割把送料、夹持、切割与转台按位置同步组织 | ⚠️部分支持 | 单轴/组路径与 cam 基元可支撑运动部分；无该应用 UDFB、工艺 IO 编排或示例验收 |
| 2.12 | 注册纠偏以 TouchProbe 捕获标记并用 Phasing 调整同步相位 | ⚠️部分支持 | TouchProbe 与 PhasingAbsolute/Relative 已实现；探针为软件采样固定四通道，Phasing 缺显式 Master 与完整动力学（`core/fb/probe.h`; `core/fb/sync.h:402-494`; KB-022；Part 1 D-15） |
| 2.13 | 旋盖过程组合速度、扭矩回读、扭矩控制和角度/时间判定 | ⚠️部分支持 | MoveVelocity/ReadActualTorque/TorqueControl 已具持续 owner/InTorque；仍未提供旋盖 UDFB，InTorque 也不代表真实驱动反馈（KB-079） |
| 2.14 | FlyingShear 按主轴窗口接近、同步、切割、返回并循环 | ❌缺失 | 无 `FbFlyingShear`/`FbCatchUp`；GearInPos/Cam 可作为基元，但完整序列、窗口输出与复位状态机不存在（`core/fb/sync.h:167-192,288-341`） |
| 2.15 | 用 SFC 协调四轴同步、变速与水平定位序列 | ❌缺失 | ST L0 支持 IF/CASE/FOR/WHILE/REPEAT，不支持 SFC，也没有该四轴示例（`doc/compliance/st-l0-semantics.md`） |
| 2.16 | 定长类型专用 Shift Register 支持 put/get、移位与旋转 | ❌缺失 | 运动内核与 ST 基本 FB 中均无 ShiftRegister UDFB（`core/st/basic.h`） |
| 2.17 | 以外部数组和索引管理通用 ShiftRegister，避免非标准指针 | ❌缺失 | 无该 FB；ST L0 也不含数组/指针型复合接口（`doc/compliance/st-l0-semantics.md`） |
| 2.18 | 固定深度 FIFO 用于探针事件到工艺站之间的数据排队 | ❌缺失 | 内部命令队列不可作为应用 LREAL FIFO；无 `FbLrealFifo32`（`core/axis/state.h:738-742`; `core/st/basic.h`） |

## §3 OMAC PackAL 映射

| 条款 | 应用/模式（自述） | 判定 | 证据/说明 |
|------|------------------|------|----------|
| 3.1 | 卷绕/放卷按半径、转矩或张力反馈维持表面速度/张力 | ❌缺失 | 无 winder/unwinder、卷径估算或张力控制模块；只有通用速度/扭矩基元 |
| 3.2 | Dancer Control 用传感器与 PID 动态修正主从 gear 比 | ⚠️部分支持 | GearIn 支持 ContinuousUpdate 更新比率，但无 PID、DANCER_REF 或 PackAL FB（`core/fb/sync.h:111-164`; `core/test/r3_sync_tests.cpp:846-930`） |
| 3.3 | CSV 卷绕轴由卷径换算表面速度并处理半径边界、方向和停机 | ❌缺失 | MoveVelocity 已接受有符号速度；仍无 `PS_Wind_csv`、卷径换算和卷绕边界状态机 |

## 结论与缺口登记

Part 3 的基础运动原语多数可由 Part 1/4 实现支撑，但“应用指南已处理”不能等同于
“示例 UDFB 已交付”。当前 25 节中：3 节为纯指南/历史，2.3 可直接支持；
其余应用面以部分支持或缺失为主。

| 编号 | 缺口 | 影响范围 | 建议归属 |
|------|------|----------|----------|
| P3-01 | 缺 Jog/Inch/JogToPosition/AxesInterlock/MasterEngine 等通用 UDFB | §2.4–2.8 | 应用层 UDFB 包，不应塞入轴内核 |
| P3-02 | Cam 表仓库、三段曲线与应用切换流程不完整 | §2.9–2.11 | Cam 应用层 + KB-016 后续 |
| P3-03 | 注册、旋盖、FlyingShear 只有低层基元，缺完整序列 | §2.12–2.14 | 独立示例/应用库与验收场景 |
| P3-04 | 无 SFC、ShiftRegister、应用 FIFO | §2.15–2.18 | PLC 语言后续层或通用 IEC 库 |
| P3-05 | 无卷绕、Dancer PID、CSV PackAL FB | §3.1–3.3 | 包装行业扩展库 |

---

*创建：2026-07-12。依据 `refs/plcopen-specs/mc_part3.txt` 全目录审计；
只记录差距，不修改实现。*
