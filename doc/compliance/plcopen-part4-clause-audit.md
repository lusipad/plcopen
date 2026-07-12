# PLCopen Motion Control Part 4 条款审计

> 审计基线：Part 4 v2.0（2026-05，Published）与 Part 4 v1.0
> （2008-12-03）。本文件只记录原文条款号与中文审计结论，不转载规格英文整句。
>
> 审计对象：`core/axis/group*`、`core/axis/group_takeover_connector.h`、
> `core/fb/{group,motion,management,path_table}.h`、Part 4 语义矩阵、测试和
> `known-boundaries.md`。状态表示当前公开 FB 门面与可验证语义，不以底层能力
> 或可组合替代标准 FB。
>
> **结论：不合规，且现有“63 个 FB / 约 35%”基线本身有误。** v2 §1.3
> 与附录 1 的短表均列出 **68 个** FB；仓库现有审计漏计 5 个。当前可找到
> 同名公开门面的为 21 个，另有 2 个旧名/自定义回读门面，不能据此声明 Part 4 合规。

## 1. 审计方法与状态口径

| 状态 | 判定 |
|---|---|
| ✅ 已承接 | 同名公开门面存在，核心生命周期有自动化证据；不代表每个可选数据类型均支持 |
| ⚠️ 部分/偏差 | 有近似能力或旧版门面，但接口、状态、Notes 或范围与对应版本不一致 |
| ❌ 缺失 | 无对应公开 FB；底层字段、组合调用或其他 FB 输出不算实现 |

证据主线为 `core/test/r3_group_fb_tests.cpp`、`part4_management_tests.cpp`、
`part4_pathtable_tests.cpp`、`part4_{coordinate,linear,circular,blending,lookahead}_tests.cpp`
及 `y7_group_takeover_tests.cpp`。这些测试能证明已实现子集，但没有覆盖 68 项
标准接口，因此不能用全套 CTest 通过反推 Part 4 合规。

## 2. 规格基线与版本迁移

### 2.1 清单数量纠错

| 来源 | 原文清单数 | 审计结论 |
|---|---:|---|
| v1 §1.3、附录 1.5 | 39 | 两处一致 |
| v2 §1.3、附录 1 短表 | 68 | 两处一致；正文 §9/§10 定义同一集合 |
| 仓库 `part4-coverage.md` | 30 | 已自标失效 |
| 仓库 `plcopen-conformance-audit.md` | 63 | **D4-01：误计数**，漏掉 5 项，不能作为合规分母 |

v2 §2.1 将 v1 扩展为机器人动力学、工具/载荷、组参数、软件限位、点动、
等待与更丰富回读；§2.2 同步 Part 1 v2 的接口约定。v1 的三项
`MC_GroupReadActual{Position,Velocity,Acceleration}` 在 v2 改为带 Source 选择的
`MC_GroupRead{Position,Velocity,Acceleration}`。当前两个 Position 门面仍采用
Actual/Command 分拆旧形态，Velocity/Acceleration 均缺失。

### 2.2 v1 到 v2 的迁移账

| 迁移类别 | 条款 | 当前情况 |
|---|---|---|
| 保留的 v1 FB | v2 §2.1、§9、§10 | 当前共 21 个同名门面；其余仍缺或只有非标准近似入口 |
| 三个回读 FB 改型 | v2 §2.1、§9.14-9.16 | ⚠️ Position 用两个旧名门面近似；Velocity/Acceleration 缺失 |
| v2 新增管理/诊断 | v2 §9.5、§9.11-9.12、§9.17-9.24、§9.32-9.33 | ❌ 全部或绝大多数缺失 |
| v2 新增机器人数据 | v2 §7-§8、§9.48-9.58 | ❌ 刚体、工具、载荷 FB 全缺 |
| v2 新增运动 | v2 §9.9、§9.40、§9.44-9.47 | ❌ GroupPower/Wait/Jog/JogVector/点动动态缺 |

## 3. 正文概念与跨 FB 合同

| 条款 | 要求主题 | 当前证据与判定 |
|---|---|---|
| v2 §1.1-1.4；v1 §1 | 范围、命名、术语、行政/运动分类 | ⚠️ 项目 C++ `Fb*` 门面是适配层，但公开字段并非完整 PLCopen 形态；旧矩阵清单错误 |
| v2 §3.1；v1 §2.1 | ACS/MCS/PCS 等坐标系与正逆运动学 | ⚠️ ACS、MCS、PCS 和插件路径有测试；WCS/FCS/TCS 明确不支持（KB-012/036），变换 FB 面不完整 |
| v2 §3.2；v1 §2.2 | 动态坐标系中的命令行为 | ❌ `MC_SetDynCoordTransform` 与两种 Tracking FB 缺失，无法满足标准入口 |
| v2 §3.3；v1 §2.3 | 协调、直接、相对/绝对运动 | ⚠️ 六个基本 Move 门面存在；Direct 是独立各轴 PTP，边界见 KB-068 |
| v2 §3.4；v1 §2.4、§7 | BufferMode、Blending、命令队列 | ⚠️ Aborting/Buffered 和部分 blending 有测试；TransitionMode 只承接 None/MaxCornerDeviation 子集（KB-031/039） |
| v2 §4 | 位置、Configuration、Turn、姿态表示 | ⚠️ `GroupPosition` 是最多 8 个标量，缺标准 MC_POS_REF 的 Configuration/Turn 完整合同；姿态/奇异边界见 KB-044 |
| v2 §5；v1 §3 | 组状态图及组轴状态关系 | ⚠️ Disabled/Standby/Moving/Stopping/ErrorStop 可观察；项目新增 Interrupted，且 FB 生命周期并非逐项按 Notes 验证 |
| v1 §3.3 | Input Execution Mode | ⚠️ 统一 rising-edge/Enable 基类覆盖部分约定；同步/跟踪 FB 缺失，无法完整验证 |
| v2 §6；v1 §4 | 建组、成员标识、组使用约束 | ⚠️ Add/Remove 可用，但无 IdentInGroup 输入、ReadConfiguration 和 UngroupAllAxes；成员采用插入槽位 |
| v2 §7 | 刚体动力学与载荷 | ❌ 无标准数据引用与读写/选择 FB |
| v2 §8 | 工具管理 | ⚠️ 底层工具变换存在；标准 ToolData/Select/Read FB 全缺 |
| v2 §11.1-11.10；v1 §7 | Buffer/Transition、CommandID、圆弧选择、软件限位、TCS | ⚠️ 有内部枚举和命令 ID；没有完整标准数据类型/输出面，TCS 不支持，软件限位组 FB 缺失 |

## 4. v2 全部 68 个 FB 审计

### 4.1 组管理、配置与回读（1-33）

| # | 条款 / FB | 状态 | 当前证据或缺口 |
|---:|---|---|---|
| 1 | §9.1 `MC_AddAxisToGroup` | ⚠️ | `FbAddAxisToGroup`；缺标准 `IdentInGroup`，按插入槽位分配 |
| 2 | §9.2 `MC_RemoveAxisFromGroup` | ⚠️ | `FbRemoveAxisFromGroup` 以 AxisRef 删除，不是标准 IdentInGroup 接口 |
| 3 | §9.3 `MC_UngroupAllAxes` | ❌ | 无门面 |
| 4 | §9.4 `MC_GroupReadConfiguration` | ❌ | 无门面 |
| 5 | §9.5 `MC_ReadAxisGroupInfo` | ❌ | 无门面 |
| 6 | §9.6 `MC_GroupEnable` | ✅ | `FbGroupEnable`；组启用测试覆盖基本路径 |
| 7 | §9.7 `MC_GroupDisable` | ✅ | `FbGroupDisable`；取消/禁用路径有管理测试 |
| 8 | §9.8 `MC_GroupHome` | 🔴 | `FbGroupHome` 缺标准 Position，且启动成员回零后立即 Done；见 D4-13 |
| 9 | §9.9 `MC_GroupPower` | ❌ | 无门面 |
| 10 | §9.10.1 `MC_SetKinTransform` | ⚠️ | `FbSetKinTransform` 接收 C++ 插件指针，不是标准 KinTransform 引用接口 |
| 11 | §9.10.2 `MC_SetCartesianTransform` | ❌ | 有底层 frame setter，无同名 FB |
| 12 | §9.10.3 `MC_SetCoordinateTransform` | ❌ | 无同名 FB |
| 13 | §9.10.4 `MC_ReadKinTransform` | ❌ | 无门面 |
| 14 | §9.10.5 `MC_ReadCartesianTransform` | ⚠️ | `FbReadCartesianTransform` 合并回读 workpiece/tool RPY，接口与标准引用形态不同 |
| 15 | §9.10.6 `MC_ReadCoordinateTransform` | ❌ | 无门面 |
| 16 | §9.11 `MC_ReadDHParameters` | ❌ | 无门面 |
| 17 | §9.12 `MC_ReadJointInfo` | ❌ | 无门面 |
| 18 | §9.13 `MC_GroupSetPosition` | ❌ | 无门面 |
| 19 | §9.14 `MC_GroupReadPosition` | ⚠️ | 旧式 `FbGroupReadActualPosition`/`CommandPosition` 分拆；无 v2 Source 接口 |
| 20 | §9.15 `MC_GroupReadVelocity` | ❌ | 无门面 |
| 21 | §9.16 `MC_GroupReadAcceleration` | ❌ | 无门面 |
| 22 | §9.17 `MC_GroupReadMotionState` | ❌ | 无门面 |
| 23 | §9.18 `MC_GroupReadCommandInfo` | ❌ | 内部 command ID 不等于标准回读 FB |
| 24 | §9.19 `MC_GroupReadParameter` | ❌ | 无门面 |
| 25 | §9.20 `MC_GroupWriteParameter` | ❌ | 规格目录有 Paramater 拼写差异；标准 FB 门面不存在 |
| 26 | §9.21 `MC_GroupWriteReferenceDynamics` | ❌ | 无门面 |
| 27 | §9.22 `MC_GroupReadReferenceDynamics` | ❌ | 无门面 |
| 28 | §9.23 `MC_GroupWriteDefaultDynamics` | ❌ | 无门面 |
| 29 | §9.24 `MC_GroupReadDefaultDynamics` | ❌ | 无门面；规格概览的 Deynamics 为原文拼写误差 |
| 30 | §9.29 `MC_GroupReadStatus` | ⚠️ | 字段账确认缺 Busy、增加 Interrupted；Enable 真时每周期重算状态位，假时清除 Valid/Error |
| 31 | §9.30 `MC_GroupReadError` | ❌ | 不能以 ReadStatus 内 error/error_id 替代独立标准 FB |
| 32 | §9.31 `MC_GroupReset` | ✅ | `FbGroupReset`，错误复位路径有测试 |
| 33 | §9.32 `MC_GroupReadSWLimits` | ❌ | 无门面 |

### 4.2 运动、路径与点动（34-51）

| # | 条款 / FB | 状态 | 当前证据或缺口 |
|---:|---|---|---|
| 34 | §9.33 `MC_GroupWriteSWLimits` | ❌ | 无门面 |
| 35 | §9.25 `MC_GroupStop` | ✅ | `FbGroupStop`；受控停止和接管测试覆盖 |
| 36 | §9.26 `MC_GroupHalt` | ❌ | 无门面；Stop+Reset 组合不算实现 |
| 37 | §9.27 `MC_GroupInterrupt` | ⚠️ | 字段账确认缺 `BufferMode`/标准中断参数面；实现增加 Interrupted 状态，Done 在到达该状态时置位 |
| 38 | §9.28 `MC_GroupContinue` | ⚠️ | `FbGroupContinue` 与项目暂停点模型成对，基本恢复路径有测试 |
| 39 | §9.34 `MC_MoveLinearAbsolute` | ⚠️ | 同名门面；位置类型、路径速度（KB-054）、坐标系与 Transition 子集存在偏差 |
| 40 | §9.35 `MC_MoveLinearRelative` | ⚠️ | 同上，Relative 几何有测试 |
| 41 | §9.36 `MC_MoveCircularAbsolute` | ⚠️ | 同名门面；仅 BORDER 三点圆弧，CENTER/RADIUS 缺失（KB-030） |
| 42 | §9.37 `MC_MoveCircularRelative` | ⚠️ | 同上 |
| 43 | §9.38 `MC_MoveDirectAbsolute` | ⚠️ | 字段账确认缺 CoordSystem；成员独立 PTP，全成员完成才 Done，Stop/Disable 报 CommandAborted（KB-068） |
| 44 | §9.39 `MC_MoveDirectRelative` | ⚠️ | 同上 |
| 45 | §9.40 `MC_GroupWaitTime` | ❌ | 无门面 |
| 46 | §9.41 `MC_PathSelect` | ⚠️ | `FbPathSelect` 使用自定义最多 32 点表和进程内 handle，非标准 PathData 接口 |
| 47 | §9.42 `MC_MovePath` | ⚠️ | 字段账确认缺标准 PathData/CoordSystem/BufferMode 面；实现仅执行自定义线性 waypoint 窗口 |
| 48 | §9.43 `MC_GroupSetOverride` | ⚠️ | 同名门面仅 `VelFactor`；AccFactor 等不做，零因子语义见 KB-058/067 |
| 49 | §9.44 `MC_GroupJog` | ❌ | 无门面 |
| 50 | §9.45 `MC_GroupJogVector` | ❌ | 无门面 |
| 51 | §9.46 `MC_GroupWriteJoggingDynamics` | ❌ | 无门面；目录 Dyamics 拼写不影响标准名称判定 |

### 4.3 动力学、工具、载荷与变换（52-63）

| # | 条款 / FB | 状态 | 当前证据或缺口 |
|---:|---|---|---|
| 52 | §9.47 `MC_GroupReadJoggingDynamics` | ❌ | 无门面 |
| 53 | §9.48 `MC_GroupReadRigidBodyDynamic` | ❌ | 无门面 |
| 54 | §9.49 `MC_GroupWriteRigidBodyDynamic` | ❌ | 无门面 |
| 55 | §9.50 `MC_GroupWriteToolData` | ❌ | 底层工具变换不等于 ToolData 标准 FB |
| 56 | §9.51 `MC_GroupReadToolData` | ❌ | 无门面 |
| 57 | §9.52 `MC_GroupSelectTool` | ❌ | 无门面 |
| 58 | §9.53 `MC_GroupReadTool` | ❌ | 无门面 |
| 59 | §9.54 `MC_GroupWritePayloadData` | ❌ | 无门面 |
| 60 | §9.55 `MC_GroupReadPayloadData` | ❌ | 无门面 |
| 61 | §9.56 `MC_GroupSelectPayload` | ❌ | 无门面 |
| 62 | §9.57 `MC_GroupReadPayload` | ❌ | 无门面 |
| 63 | §9.58 `MC_GroupTransformPosition` | ❌ | 有内部坐标换算，无标准 FB 门面 |

### 4.4 同步、动态坐标与跟踪（64-68）

| # | 条款 / FB | 状态 | 当前证据或缺口 |
|---:|---|---|---|
| 64 | §10.3 `MC_SyncAxisToGroup` | ❌ | 无门面 |
| 65 | §10.4 `MC_SyncGroupToAxis` | ❌ | 无门面 |
| 66 | §10.5 `MC_SetDynCoordTransform` | ❌ | 无门面 |
| 67 | §10.6 `MC_TrackConveyorBelt` | ❌ | 无门面 |
| 68 | §10.7 `MC_TrackRotaryTable` | ❌ | 无门面 |

## 5. 已有 21 个同名门面的逐 FB 五问

“I/O”只列影响兼容性的 B 级必需接口族；“Notes”记录核心行为，不复制原句。
输出时序按 Execute 型的 Done/Busy/CommandAborted/Error 或 Enable 型的
Valid/Busy/Error 核对。扩展一栏回答项目是否增加、删减或改变标准可观察面。

### 5.1 逐字段 I/O 底稿

属性：`B` 为合规基础字段，`E` 为扩展/可选字段。`—` 表示实现无对应字段；
`outputs.*` 指 `MotionOutputs`。数组/引用数据类型按一个字段列出，其内部结构在
§3/§4 和缺失族表另判。

| FB | VAR_IN_OUT（规格→实现） | VAR_INPUT B（规格→实现） | VAR_INPUT E（规格→实现） | VAR_OUTPUT B/E（规格→实现） | 字段判定 |
|---|---|---|---|---|---|
| AddAxisToGroup | AxesGroup→`group_ref`; Axis→`axis_ref` | Execute→`execute` | IdentInGroup→— | Done/Busy/Error/ErrorID→`outputs.done/busy/error/error_id` | ⚠️ 1 个 E 输入缺失；Busy 永不置位 |
| RemoveAxisFromGroup | AxesGroup→`group_ref` | Execute→`execute` | IdentInGroup→— | Done/Busy/Error/ErrorID→通用输出 | ⚠️ 规格无 AxisRef 输入，当前删除键不兼容 |
| GroupEnable | AxesGroup→`group_ref` | Execute→`execute` | — | Done/Busy/Error/ErrorID→通用输出 | ✅ 字段可映射；Busy 为同周期操作恒假 |
| GroupDisable | AxesGroup→`group_ref` | Execute→`execute` | — | Done/Busy/Error/ErrorID→通用输出 | ✅ 字段可映射；Busy 为同周期操作恒假 |
| GroupHome | AxesGroup→`group_ref` | Execute→`execute`; Position→— | BufferMode→— | Done/Busy/Active/CommandAborted/Error/ErrorID→通用输出 | ❌ 缺 Position；实现不跟踪回零命令生命周期 |
| SetKinTransform | AxesGroup→`group_ref` | Execute→`execute`; KinTransform→`pose_plugin/kinematics_plugin` | — | Done/Busy/Error/ErrorID→通用输出 | ⚠️ 引用数据被两个 ABI 指针及 margin/step 扩展替代 |
| ReadCartesianTransform | AxesGroup→`group_ref` | Enable→`enable`; CoordSystem/TransformRef→— | — | Valid/Busy/Error/ErrorID/Transform→`valid`/—/`error`/`error_id`/两个 `[6]` | ❌ 缺 Busy 与标准选择输入；输出被合并扩展 |
| GroupReadStatus | AxesGroup→`group_ref` | Enable→`enable` | — | Valid/Busy/Error/ErrorID + 状态位→`valid`/—/`error`/`error_id` + 六状态位 | ⚠️ 缺 Busy；增加 Interrupted，状态集合不同 |
| GroupStop | AxesGroup→`group_ref` | Execute→`execute`; Deceleration/Jerk→同名 | BufferMode→— | Done/Busy/Active/CommandAborted/Error/ErrorID→通用输出 | ⚠️ 缺 BufferMode；输出字段齐但 aborted 路径未按命令账本产生 |
| GroupInterrupt | AxesGroup→`group_ref` | Execute→`execute`; Deceleration/Jerk→同名 | BufferMode/中断引用→— | Done/Busy/Active/CommandAborted/Error/ErrorID→通用输出 | ⚠️ 输入子集；增加 Interrupted 状态 |
| GroupContinue | AxesGroup→`group_ref` | Execute→`execute` | ContinueMode/引用→— | Done/Busy/Active/CommandAborted/Error/ErrorID→通用输出 | ⚠️ 扩展输入缺失；仅恢复单一保存上下文 |
| GroupReset | AxesGroup→`group_ref` | Execute→`execute` | — | Done/Busy/Error/ErrorID→通用输出 | ✅ 字段可映射；Busy 为同周期操作恒假 |
| MoveLinearAbsolute | AxesGroup→`group_ref` | Execute/Position/Velocity/Acceleration/Deceleration/Jerk→同义字段 | CoordSystem/BufferMode/TransitionMode/TransitionParameter→同义字段 | Done/Busy/Active/CommandAborted/Error/ErrorID→通用输出 | ⚠️ 字段族齐；Position 数据结构、模式值域与路径速度偏差 |
| MoveLinearRelative | AxesGroup→`group_ref` | Execute→`execute`; Distance→继承 `position`; 动态→同名 | CoordSystem/Buffer/Transition→同名 | 六通用输出→通用输出 | ⚠️ Distance 命名不兼容，其余同 Absolute |
| MoveCircularAbsolute | AxesGroup→`group_ref` | Execute/AuxPoint/EndPoint/PathChoice/动态→同义字段 | CircMode/CoordSystem/Buffer/Transition→`circ_mode/coord_system/buffer_mode`/— | 六通用输出→通用输出 | ❌ 缺 Transition 字段；CircMode 只承接 BORDER |
| MoveCircularRelative | AxesGroup→`group_ref` | Execute/AuxPoint/EndPoint/PathChoice/动态→同义字段 | CircMode/CoordSystem/Buffer/Transition→同上 | 六通用输出→通用输出 | ❌ 同 Absolute，且 Relative 数据仍名 EndPoint |
| MoveDirectAbsolute | AxesGroup→`group_ref` | Execute/Position/Velocity/Acceleration/Deceleration/Jerk→同名 | CoordSystem→— | 六通用输出→通用输出 | ⚠️ 缺 CoordSystem；输出由独立 direct command ID 裁决 |
| MoveDirectRelative | AxesGroup→`group_ref` | Execute/Distance/动态→`execute/distance/同名` | CoordSystem→— | 六通用输出→通用输出 | ⚠️ 缺 CoordSystem |
| PathSelect | AxesGroup→`group_ref`; PathData→`table` | Execute→`execute` | CoordSystem→— | Done/Busy/Error/ErrorID/PathRef→`outputs.done/busy/error/error_id`/`table.handle` | ⚠️ 缺 CoordSystem；PathRef 通过可变对象返回而非输出字段 |
| MovePath | AxesGroup→`group_ref`; PathData→`table` | Execute→`execute` | CoordSystem/BufferMode→— | 六通用输出→通用输出 | ❌ 两个模式输入缺失；只支持线性 waypoint |
| GroupSetOverride | AxesGroup→`group_ref` | Execute→`execute`; VelFactor→`vel_factor` | AccFactor/JerkFactor→— | Done/Busy/Error/ErrorID→通用输出 | ⚠️ 两个动态因子缺失；实现增加 factor=0 暂停 |

### 5.2 Notes、状态机、输出时序逐项判定

| FB / 条款 | B 级 I/O 对照 | Notes 核心行为 | 组状态影响 | 输出时序 | 扩展/偏差结论 |
|---|---|---|---|---|---|
| `MC_AddAxisToGroup` §9.1 | 缺 `IdentInGroup`；有 Group/Axis/Execute 与通用输出 | 仅 Disabled 可改组、标识唯一 | 成功仍 Disabled | 同周期 Done；下降沿清除 | ⚠️ 自动槽位是非标准替代 |
| `MC_RemoveAxisFromGroup` §9.2 | 标准按 Ident 删除；当前按 Axis 指针 | 仅 Disabled、成员存在才删除 | 成功仍 Disabled | 同周期 Done；下降沿清除 | ⚠️ 接口不兼容 |
| `MC_GroupEnable` §9.6 | Group/Execute 与通用输出可映射 | 空组或成员前置条件由 `AxisGroup::enable` 拒绝 | Disabled→Standby | 成功同周期 Done，失败同周期 Error | ✅ 同周期管理操作，字段与状态一致 |
| `MC_GroupDisable` §9.7 | 基本 B 级族可映射 | 立即取消组活动并禁用 | 任意态→Disabled | 同周期 Done；Direct 活动 FB 观察为 CommandAborted | ⚠️ 其他运动 FB 无逐命令 aborted 账本，可能被 Disabled 观察为非 Done/非 Error 终态 |
| `MC_GroupHome` §9.8 | 缺标准 Position；只有 Group/Execute | 所有成员完成才完成；异常传播 | Standby→Moving→Standby/ErrorStop | 当前提交后立即 Done | ❌ 时序偏差：实现调用只启动并行回零便 Done |
| `MC_SetKinTransform` §9.10.1 | 标准 Transform 引用被两个 C++ 插件指针替代 | 配置运动学变换，活动组不得无定义换型 | 保持 Standby | 同周期 Done | ⚠️ 厂商插件扩展，接口不兼容 |
| `MC_ReadCartesianTransform` §9.10.5 | Enable 型；标准输入/结果引用与当前两个 `double[6]` 不同 | 有效期间持续回读 | 不改变 | Enable 真即 Valid，假清零 | ⚠️ 合并 workpiece/tool 是项目扩展 |
| `MC_GroupReadStatus` §9.29 | Enable 与状态位输出可映射 | 状态位应互斥并持续反映组状态 | 不改变 | Enable 真即 Valid，假清除 | ⚠️ 增加 Interrupted；成员同步时折叠为 Moving |
| `MC_GroupStop` §9.25 | Group/Execute/Deceleration/Jerk 可映射 | 受控停止；实现缺标准 BufferMode | Moving→Stopping→Standby | Busy/Active 至 Standby 后 Done；下降沿清除 | ⚠️ 标准“Execute 保持时锁定停止态”未实现，停稳即 Standby/Done |
| `MC_GroupInterrupt` §9.27 | 基本输入族可映射 | 受控中断并保留续行信息 | Moving→Stopping→项目 `Interrupted` | 到 Interrupted 置 Done | ⚠️ `Interrupted` 是项目可观察扩展 |
| `MC_GroupContinue` §9.28 | Group/Execute 可映射 | 只允许从中断上下文恢复 | Interrupted→Moving→Standby | Busy/Active 至 Standby 后 Done | ⚠️ 依赖项目扩展状态 |
| `MC_GroupReset` §9.31 | Group/Execute 与通用输出可映射 | `AxisGroup::reset` 仅在组 ErrorStop 执行并检查成员状态 | ErrorStop→Standby，非法态 Error | 同周期 Done 或 Error；下降沿清除 | ⚠️ 无 Busy 周期；成员仍 ErrorStop 时返回失败而非掩盖 |
| `MC_MoveLinearAbsolute` §9.34 | Position/CoordSystem/动态/Buffer/Transition 基本存在 | 协调直线路径，路径动态受限 | Standby→Moving→Standby | Busy/Active→Done；abort 账本不完整 | ⚠️ 路径速度 KB-054、数据类型与 Transition 子集 |
| `MC_MoveLinearRelative` §9.35 | Distance 以继承的 `position` 表达 | 相对起点建立直线路径 | 同上 | 同上 | ⚠️ 字段命名、路径速度与坐标子集 |
| `MC_MoveCircularAbsolute` §9.36 | Aux/End/PathChoice/动态存在；圆弧模式不全 | 依 CircMode 构造圆弧 | Standby→Moving→Standby | Busy/Active→Done | ⚠️ 只支持 BORDER；CENTER/RADIUS 缺失 |
| `MC_MoveCircularRelative` §9.37 | 与绝对型同族，目标按相对解释 | 相对圆弧 | 同上 | 同上 | ⚠️ 同上 |
| `MC_MoveDirectAbsolute` §9.38 | Position/动态存在；无 BufferMode 符合该类形态 | 各成员直接 PTP，协调性不保证 | Standby/Moving→Moving→Standby | 全成员到位才 Done；停止/禁用 aborted | ⚠️ 项目独立命令 ID 域，见 KB-068 |
| `MC_MoveDirectRelative` §9.39 | Distance/动态存在 | 相对直接 PTP | 同上 | 同上 | ⚠️ 同上 |
| `MC_PathSelect` §9.41 | 标准 PathData/CoordSystem 被自定义 `PathTable*` 替代 | 校验路径并产生选择结果 | 不改变 | 当前同周期 Done | ⚠️ 32 点上限、进程内 handle 为项目扩展 |
| `MC_MovePath` §9.42 | 标准 PathData/BufferMode 面不完整，仅 table 指针 | 执行已选择路径 | Standby→Moving→Standby | Busy/Active→Done | ⚠️ 只提交线性 waypoint，非完整路径元素 |
| `MC_GroupSetOverride` §9.43 | 只有 VelFactor；缺 AccFactor 等 v2 面 | 修改组动态比例，零值暂停 | Moving 保持 Moving；零值驻留 | 当前同周期 Done | ⚠️ 子集；零值恢复修复见 KB-058/067 |

## 6. 47 个无同名门面的 B 级缺失证据

下表按接口/状态族压缩，但逐项列名；“无同名”由 `core/fb/*.h` 类声明检索
确认。没有类就不存在可核对的 B 级 I/O、输出时序或 Notes 状态机，底层方法不抵扣。

| 接口与状态族 | 缺失 FB（逐项） | B 级接口/状态影响 |
|---|---|---|
| 建组/配置 Execute、Done/Busy/Error | `MC_UngroupAllAxes`、`MC_GroupReadConfiguration`、`MC_ReadAxisGroupInfo` | 无批量解组、配置数组和轴组信息输出；Disabled 配置流程不完整 |
| 电源/回零 Execute 型 | `MC_GroupPower` | 无 Enable/EnablePositive/EnableNegative 等组电源面；Disabled/Standby 前置链不可标准化 |
| 变换 Set/Read 引用型 | `MC_SetCartesianTransform`、`MC_SetCoordinateTransform`、`MC_ReadKinTransform`、`MC_ReadCoordinateTransform`、`MC_ReadDHParameters`、`MC_ReadJointInfo` | 无标准 Transform/DH/Joint 引用 I/O；不能核对配置时状态限制与 Valid 时序 |
| 组位置/运动回读 Enable 型 | `MC_GroupSetPosition`、`MC_GroupReadPosition`、`MC_GroupReadVelocity`、`MC_GroupReadAcceleration`、`MC_GroupReadMotionState`、`MC_GroupReadCommandInfo` | v2 Position 无同名门面（只有两个旧/自定义近似）；其余缺 Position/Source/运动位/CommandID 输出；诊断面断裂 |
| 参数/动力学 Execute 或 Enable 型 | `MC_GroupReadParameter`、`MC_GroupWriteParameter`、`MC_GroupWriteReferenceDynamics`、`MC_GroupReadReferenceDynamics`、`MC_GroupWriteDefaultDynamics`、`MC_GroupReadDefaultDynamics` | 缺 ParameterNumber、Value、Dynamics 引用和 Done/Valid 时序；动态配置 Notes 无入口 |
| 独立错误回读 Enable 型 | `MC_GroupReadError` | 缺 ErrorID/Axis/记录选择等标准输出；ReadStatus 合并字段不能替代 |
| 组软件限位读写 | `MC_GroupReadSWLimits`、`MC_GroupWriteSWLimits` | 缺 MC_GROUP_SWLIMITS 数据 I/O；无法验证写入允许态和读回 Valid |
| 运动控制 Execute 型 | `MC_GroupHalt`、`MC_GroupWaitTime` | 缺 Halt 的 Moving→Stopping→Standby 行为及 Wait 的缓冲队列行为 |
| 点动及动态 | `MC_GroupJog`、`MC_GroupJogVector`、`MC_GroupWriteJoggingDynamics`、`MC_GroupReadJoggingDynamics` | 缺方向/向量/坐标/动态输入；无 Jog 状态与 Enabled/Busy 生命周期 |
| 刚体动力学 | `MC_GroupReadRigidBodyDynamic`、`MC_GroupWriteRigidBodyDynamic` | 缺 RigidBody 引用和读写时序；无载荷动力学配置状态约束 |
| 工具管理 | `MC_GroupWriteToolData`、`MC_GroupReadToolData`、`MC_GroupSelectTool`、`MC_GroupReadTool` | 缺 ToolID/ToolData/选中工具输出；底层单一 tool transform 不支持工具库 Notes |
| 载荷管理 | `MC_GroupWritePayloadData`、`MC_GroupReadPayloadData`、`MC_GroupSelectPayload`、`MC_GroupReadPayload` | 缺 PayloadID/PayloadData/选中载荷输出；无选择状态与输出时序 |
| 坐标位置变换 | `MC_GroupTransformPosition` | 缺 Source/Target CoordSystem 与 Position 输入输出；内部 submit 换算不可独立调用 |
| 同步/动态坐标/跟踪 | `MC_SyncAxisToGroup`、`MC_SyncGroupToAxis`、`MC_SetDynCoordTransform`、`MC_TrackConveyorBelt`、`MC_TrackRotaryTable` | 缺主从引用、同步参数和跟踪坐标输入；Synchronized/Tracking 状态及 InSync/Busy/Active 输出均无证据 |

## 7. v1.0 全 39 项逐项迁移

| # | v1 FB | v2 迁移 | 当前承接 |
|---:|---|---|---|
| 1 | `MC_AddAxisToGroup` | 保留 | ⚠️ 同名，缺 IdentInGroup |
| 2 | `MC_RemoveAxisFromGroup` | 保留 | ⚠️ 同名，按 AxisRef 删除 |
| 3 | `MC_UngroupAllAxes` | 保留 | ❌ |
| 4 | `MC_GroupReadConfiguration` | 保留 | ❌ |
| 5 | `MC_GroupEnable` | 保留 | ✅ |
| 6 | `MC_GroupDisable` | 保留 | ✅ |
| 7 | `MC_GroupHome` | 保留 | ⚠️ 启动即 Done |
| 8 | `MC_SetKinTransform` | 保留 | ⚠️ 插件接口 |
| 9 | `MC_SetCartesianTransform` | 保留 | ❌ |
| 10 | `MC_SetCoordinateTransform` | 保留 | ❌ |
| 11 | `MC_ReadKinTransform` | 保留 | ❌ |
| 12 | `MC_ReadCartesianTransform` | 保留 | ⚠️ 自定义合并回读 |
| 13 | `MC_ReadCoordinateTransform` | 保留 | ❌ |
| 14 | `MC_GroupSetPosition` | 保留 | ❌ |
| 15 | `MC_GroupReadActualPosition` | 改名/改型为 v2 `MC_GroupReadPosition(Source)` | ⚠️ 旧名门面存在 |
| 16 | `MC_GroupReadActualVelocity` | 改名/改型为 v2 `MC_GroupReadVelocity(Source)` | ❌ |
| 17 | `MC_GroupReadActualAcceleration` | 改名/改型为 v2 `MC_GroupReadAcceleration(Source)` | ❌ |
| 18 | `MC_GroupStop` | 保留 | ✅ |
| 19 | `MC_GroupHalt` | 保留 | ❌ |
| 20 | `MC_GroupInterrupt` | 保留 | ⚠️ 项目扩展状态 |
| 21 | `MC_GroupContinue` | 保留 | ⚠️ |
| 22 | `MC_GroupReadStatus` | 保留 | ⚠️ 同名 Enable 门面，增加 Interrupted |
| 23 | `MC_GroupReadError` | 保留 | ❌ |
| 24 | `MC_GroupReset` | 保留 | ✅ |
| 25 | `MC_MoveLinearAbsolute` | 保留 | ⚠️ |
| 26 | `MC_MoveLinearRelative` | 保留 | ⚠️ |
| 27 | `MC_MoveCircularAbsolute` | 保留 | ⚠️ |
| 28 | `MC_MoveCircularRelative` | 保留 | ⚠️ |
| 29 | `MC_MoveDirectAbsolute` | 保留 | ⚠️ |
| 30 | `MC_MoveDirectRelative` | 保留 | ⚠️ |
| 31 | `MC_PathSelect` | 保留 | ⚠️ 自定义 PathTable |
| 32 | `MC_MovePath` | 保留 | ⚠️ 线性 waypoint 子集 |
| 33 | `MC_GroupSetOverride` | 保留并在 v2 扩展 | ⚠️ 仅 VelFactor |
| 34 | `MC_SyncAxisToGroup` | 保留 | ❌ |
| 35 | `MC_SyncGroupToAxis` | 保留 | ❌ |
| 36 | `MC_SetDynCoordTransform` | 保留 | ❌ |
| 37 | `MC_TrackConveyorBelt` | 保留 | ❌ |
| 38 | `MC_TrackRotaryTable` | 保留 | ❌ |
| 39 | `MC_GroupJog` | v1 附录列出；v2 保留并扩展 JogVector/动态读写 | ❌ |

> 纠错说明：v1 §1.3 正文概览为 38 项，附录 1.5 另列 `MC_GroupJog`，故
> 合规程序口径为 39；v2 没有删除 v1 FB，三个 Actual 回读是改名/改型。

## 8. 附录合规程序逐项核对

| 程序项 | v1 / v2 要求 | 当前证据 | 判定 |
|---|---|---|---|
| Supplier Statement | 供应商、产品、版本等声明 | 仓库无 Part 4 填妥声明 | ❌ |
| Supported data types | 对各引用/枚举/结构类型声明支持 | 仅 C++ 内部类型，无官方表映射 | ❌ |
| Supported Buffer Modes | 逐模式 Yes/No | 语义文档只声明子集，未填官方表 | ⚠️ |
| Supported Transition Modes | 逐模式 Yes/No | 仅 None/MaxCornerDeviation 子集，未填官方表 | ⚠️ |
| Short FB overview | 每个 FB Yes/No 与短注 | 本文 68 行可作审计底稿，尚非供应商签署表 | ⚠️ |
| Per-FB interface table | B/E/O 等接口逐字段选择与支持 | 本文 §5.1 已形成 21 项字段底稿，§6 对 47 项以无门面证明；官方签署表未生成 | ⚠️ 审计完成，行政表缺失 |
| 标识使用 | 满足程序后才可用合规标识 | 当前不满足 | ❌ 禁止声明/使用 |
| v2 SRCI 关系附录 | 信息性映射 | 项目无 SRCI 层 | N/A，不计合规 |

## 9. v1.0 回读改型补充

v1 的 39 项均包含在上表的继承或改型关系中。除三个回读 FB 外，名称沿用至
v2；因此上表对缺失门面的判定同时适用于 v1。三个 v1 专项结论如下：

| v1 条款 / FB | 当前判定 |
|---|---|
| §5.10 `MC_GroupReadActualPosition` | ⚠️ `FbGroupReadActualPosition` 同名语义近似，但 C++ 输出类型/生命周期仍非合规声明 |
| §5.11 `MC_GroupReadActualVelocity` | ❌ 缺失 |
| §5.12 `MC_GroupReadActualAcceleration` | ❌ 缺失 |

v1 §3 状态图、§3.3 Input Execution Mode、§7 blending/buffering 不是因为 v2
新增功能就可跳过；当前证据只锁定已实现运动子集。附录 1 的供应商声明、支持数据
类型、BufferMode、TransitionMode 和 39 项逐 FB 表均未形成可发布的合规清单，故
**v1 也不能宣称合规**。

## 10. 附录内容补充

| 条款 | 审计结果 |
|---|---|
| v2 附录 1；v1 附录 1 | ❌ 未提供填妥的供应商声明、支持数据类型、BufferMode、TransitionMode 和逐 FB Yes/No 合规表 |
| v2 附录 1 各 FB 接口表；v1 附录 A | ⚠️ 本文 §5.1 已完成现有 21 项逐字段 B/E 映射，§6 完成 47 项无门面证明；官方供应商签署表未生成 |
| v2 附录 2；v1 附录 1.6 | 不适用当前实现审计；未获完整合规前不得使用 PLCopen 合规标识 |
| v2 附录 3（PLCopen 与 SRCI） | 信息性关系已审阅；项目没有 SRCI 通讯层，不作为当前 Part 4 FB 合规证据 |

## 11. 发现登记

| ID | 严重度 | 发现 | 影响 / 修复方向 |
|---|---|---|---|
| D4-01 | 高 | v2 FB 总数被记为 63，原文两处均为 68 | 修正所有矩阵、manifest 与计划的分母；以 68 项机读源生成表 |
| D4-02 | 高 | 现有 `part4-coverage.md` 仅 30 项且已失效 | 不得再作完成证据；由本审计或后续机读矩阵取代 |
| D4-03 | 高 | 68 项中 47 项无同名公开门面；另有已实现项仍属接口/语义子集 | 建立 68 项唯一机读清单，逐项实现或正式声明不支持 |
| D4-04 | 高 | v2 三个 Source 型回读被旧名/自创 CommandPosition 形态替代或缺失 | 实现 v2 `ReadPosition/Velocity/Acceleration`，保留旧门面仅作兼容层 |
| D4-05 | 高 | Add/Remove 缺标准 IdentInGroup 接口 | 明确成员标识模型并补配置回读/UngroupAllAxes |
| D4-06 | 高 | 变换、同步、动态坐标、跟踪的标准门面大面积缺失 | 底层能力不得替代标准 FB；按 §9.10/§10 独立验收 |
| D4-07 | 高 | 工具、载荷、刚体动力学整族缺失 | 先定义数据引用/所有权/更新时机，再实现 §9.48-9.57 |
| D4-08 | 中 | 线性 Velocity 是最长轴限制而非标准路径速度（KB-054） | 属声明行为变更，需语义批准与回放重录 |
| D4-09 | 中 | 圆弧仅 BORDER；坐标系与 TransitionMode 均为子集 | 在接口和合规表中逐项声明，不得笼统写“已承接” |
| D4-10 | 中 | 字段审计确认：GroupHome 缺 Position，Circular 缺 Transition，Direct/Path 缺 CoordSystem，MovePath 缺 BufferMode，Override 缺 Acc/JerkFactor，多项读 FB 缺 Busy；泛型输出不能消除这些缺口 | 后续机读化本文 §5.1，并按具体缺字段补接口合同测试 |
| D4-11 | 中 | 没有填妥的 v1/v2 官方合规程序表 | 完整实现前至少发布诚实的 Supported Yes/No 表；禁止合规标识声明 |
| D4-12 | 中 | Notes 审计确认的测试缺口集中在：Disable 对非 Direct 运动的 aborted 终态、Stop 的 Execute 保持锁态、GroupHome 完成时序、各模式拒绝后的输出保持；现有测试未锁定这些标准可观察行为 | 按四类具体行为补状态机与输出时序锚点 |
| D4-13 | 高 | `MC_GroupHome` 提交成员回零后立即 Done，没有等待全部成员完成 | 建立组回零命令账本，全部成员成功才 Done，任一失败传播 Error/ErrorStop |

## 12. 可核验结论

1. v2 权威分母是 **68**，不是 63；v1 分母是 **39**。
2. 当前同名公开门面 21 个；其中不少仅为子集或接口偏差，另有两个 v1 风格
   Position 回读门面。不能把底层 API、组合调用或另一个 FB 的输出计作实现。
3. 当前最强证据集中在组基本管理、六种 Move、Stop、Interrupt/Continue、
   PathSelect/MovePath、SetKinTransform 与部分坐标回读；参数/诊断、同步/跟踪、
   Jog、工具/载荷/刚体整族仍是明确缺口。
4. 本轮只登记，不修改任何运行时代码；所有行为变更仍须语义矩阵先行。
