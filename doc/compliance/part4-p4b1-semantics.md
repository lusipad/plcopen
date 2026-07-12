# Part 4 P4-B1 十九项管理与回读语义矩阵

> 状态：**已批准**（2026-07-13，维护者批准全部条款）。
> 依据：PLCopen Motion Control Part 4 v2.0 §9.4-9.5、§9.11-9.12、
> §9.14-9.24、§9.32-9.33；条目级缺口见
> `plcopen-part4-clause-audit.md`。实现按五个纵向切片推进。
>
> 计数纠错：旧计划写“薄门面 21 项”，但其命名分组按标准逐项展开只能
> 得到 **19 项**：配置/运动学信息 4 + 回读 5 + 参数/动态 8 + SW 限位 2。
> `MC_GroupSetPosition` 会改变坐标，`MC_GroupReadError` 需要错误记录模型，
> 均不默认为本批内容；它们留在后续批次单独定语义。

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| 批次范围 | 只补下表 19 个 C++ 同名语义门面；不把底层字段存在等同于标准支持，不改 Part 4 68 项分母 |
| 单写者 | Read FB 只观察快照；Write FB 只在批准状态写配置域，不从读 FB 或周期线程反向修改运动 |
| RT | 配置、元数据和输出均固定容量（最多 8 轴）；call/cycle 无分配、无锁、无异常，不用有限差分伪造速度/加速度 |
| 兼容 | `FbGroupReadActualPosition`/`FbGroupReadCommandPosition` 保留为兼容包装；新 v2 `FbGroupReadPosition` 以 Source 统一承接 |
| 运动输出 | 默认 absolute/end-point 配置下既有规划器、轨迹与回放 setpoint 逐位不变；显式启用 percentage/default/SWLimits 后只影响批准后提交的新命令，活动承诺不追改 |
| 合规口径 | 19 项有门面后只减少“无同名入口”数量；逐 I/O、派生类型和其余 Part 4 缺口未闭合前不得声明合规 |

## 2. 精确范围

| 族 | 数量 | 标准名称 |
|---|---:|---|
| 配置/运动学信息 | 4 | `MC_GroupReadConfiguration`、`MC_ReadAxisGroupInfo`、`MC_ReadDHParameters`、`MC_ReadJointInfo` |
| 组运动回读 | 5 | `MC_GroupReadPosition`、`MC_GroupReadVelocity`、`MC_GroupReadAcceleration`、`MC_GroupReadMotionState`、`MC_GroupReadCommandInfo` |
| 参数与动态 | 8 | `MC_GroupReadParameter`、`MC_GroupWriteParameter`、`MC_GroupWriteReferenceDynamics`、`MC_GroupReadReferenceDynamics`、`MC_GroupWriteDefaultDynamics`、`MC_GroupReadDefaultDynamics`、`MC_GroupWriteJoggingDynamics`、`MC_GroupReadJoggingDynamics` |
| 软件限位 | 2 | `MC_GroupReadSWLimits`、`MC_GroupWriteSWLimits` |

## 3. 决策点

| # | 决策 | 提案 | 理由 |
|---|---|---|---|
| 3.1 | 通用生命周期 | Read 类为 Enable 电平：Enable=false 清 Valid/Error/Busy 与值输出；有效引用同拍 `Valid=true, Busy=false`。Write 类为 Execute 上升沿：同拍 Done 或 Error，终态保持到 Execute=false | 当前操作均为内存域 O(8)，不虚构异步 Busy |
| 3.2 | IdentInGroup | 首批强类型 `IdentInGroup{index}`，0-based，稳定性仅保证组 Disabled 期间成员表不变；越界 `out_of_range` | 与 AxisGroup 固定插入槽一致；不引入不存在的持久 UUID |
| 3.3 | GroupReadConfiguration | ACS 返回槽位的真实 `AxisModel*`；MCS/PCS/WCS/FCS/TCS 不伪造 virtual AXIS_REF，首批显式 `unsupported`；AxisID 仅作为同拍槽号诊断 | 核没有虚拟路径轴对象，不能返回悬空或合成引用 |
| 3.4 | ReadAxisGroupInfo | 由 `AxisModel::group_owner()` + owner 内槽位反查；未入组 `precondition_failed`；只认真实 `AxisGroup` owner | 已有非 owning owner 合同，读取不改所有权 |
| 3.5 | 运动学元数据 | 新增宿主绑定的固定容量 `GroupKinematicsInfo`：serial 标志、每 link 四个 DH 值、每 joint ZeroPosition/DirectionClockwise；仅 Disabled 且无成员运动时绑定，首次成功 Enable 后冻结；插件不自动推导这些值 | `Kinematics`/`PoseKinematics` ABI 没有 DH 与方向信息；显式事实源优于猜测 |
| 3.6 | DH/Joint 拒绝 | 未绑定、非 serial、数量与成员/plugin joint_count 不一致、非有限值均 Error；无局部输出 | 标准限定 serial kinematics，失败必须原子 |
| 3.7 | Source | 强枚举 `GroupValueSource::{commanded, actual, set}`；commanded 映射 command snapshot，actual 映射 actual snapshot；`set` 首批 `unsupported` | AxisSnapshot 没有独立 set-value 域，不能把 commanded 重命名冒充 |
| 3.8 | Position | ACS 逐成员读取；MCS/PCS 沿现有 `read_cartesian` 与同一 Source；其余坐标系沿 KB-036 `unsupported`；旧两个 Position FB 调新实现 | 复用已批准位置转换链并消除 v1 双门面漂移 |
| 3.9 | Velocity/Acceleration | 首批只支持 ACS，逐成员读取 snapshot 对应阶次；PathVelocity/PathAcceleration 只在活动共享路径有真实标量状态时给出，否则为 0；MCS/PCS 及其他坐标显式 `unsupported` | 核没有 Jacobian/Jdot，位置有限差分既不确定也非同拍物理量 |
| 3.10 | MotionState | 精确输出 Tracking/InSync/InPosition/Standstill/ConstantVelocity/Accelerating/Decelerating/ActiveCommandID；未实现动态坐标 tracking 时固定 `Tracking=false, InSync=true`；相位从活动 `Profile1D` 同拍样本派生，无活动命令时 Standstill/InPosition=true、ID=0 | 对齐 §9.17 字段，不用 GroupStatus 私有位替代标准运动态 |
| 3.11 | CommandInfo | 以 CommandID 查询固定队列/窗口/活动命令；标准 `CommandState` 只含 accepted/active，另输出 elapsed/remaining cycles、remaining distance、progress，InfoID/WarningID=0。命令终止或未知后 `out_of_range` | 原文没有 Done/Aborted command state；只为当前可查询命令建有界观察面 |
| 3.12 | 参数注册表 | `GroupParameter` 精确承接标准两项：`dynamics_mode`（absolute/percentage）与 `transition_reference_point`；两项均可读写。非标准调优旋钮不塞进标准枚举 | 规格 §9.19.1 是唯一事实源，vendor 扩展以后另列 |
| 3.13 | DynamicsMode | absolute 沿现有物理单位；percentage 将新 Move 的四阶输入解释为 Reference Dynamics 百分比 `[0,100]`。模式切换只允许 Disabled/Standby 且无 pending，且不追改活动承诺 | Reference Dynamics 必须有真实消费语义，不能只存不生效 |
| 3.14 | TransitionReferencePoint | 强枚举精确为 `start_point/end_point`；首批只支持 `end_point`（当前窗口用后继 Transition 输入修饰前段终点），`start_point` 写入 `unsupported`，默认 end-point | 对齐 §11.4 两值枚举，不把 v1 start-point 语义静默映射到现有窗口 |
| 3.15 | Dynamics 数据 | Reference/Default 各为路径 Velocity/Acceleration/Deceleration/Jerk 四值；Jogging 另含路径四值 + 最多 8 轴四阶数组。三槽独立，所有被更新值有限且 >0，Jogging size 等于成员数 | 精确匹配 §9.21-9.24 与 §9.46-9.47，不给 Reference/Default 虚构逐轴字段 |
| 3.16 | Dynamics 生效 | Reference 为 percentage 模式缩放基准；Default 仅在命令显式选择 `use_default_dynamics` 时替代四阶输入；Jogging 只供后续 GroupJog 批次消费。写入只影响之后提交的新命令 | C++ 没有 IEC“输入未连接”反射，显式选择比用 0/NaN 猜 unset 更诚实 |
| 3.17 | 部分更新 | Reference/Default 按标准以负值表示保持、0 的厂商口径定为保持、正值更新；Jogging 同样逐字段/逐轴处理。非有限值始终拒绝，不能借 NaN 表示 ignore | 跟随原文 `>=0 change` 与“zero vendor specific”，本项目选择 zero=保持 |
| 3.18 | GroupSWLimits | 固定容量每轴 min/max + min/max enable；写入仅 Disabled/Standby 且无 pending/Direct/同步成员运动；全表预检 min<=max、有限、size 匹配后原子提交 | 组限位是配置事务，不允许半组生效 |
| 3.19 | SWLimits 生效 | 写成功同步到各 AxisModel 的软件限位配置；仅约束之后提交的新绝对目标，现有 active/queued 轨迹不追改；相对命令归一化后检查 | 复用轴级唯一限位事实源，避免组/轴两套运行时判定 |
| 3.20 | 外部轴级改限位 | 轴属于已配置组时，直接 `AxisModel::configure_limits` 拒绝；必须经组事务写入，移出组后恢复轴级入口 | 保证组读回与真实轴限位不漂移 |

## 4. 退化与拒绝规则

| 形态 | 结果 |
|---|---|
| null group/axis、Ident 越界、数组 size 不匹配 | Error；输出清零；配置原子不变 |
| Read FB Enable 保持 | 每拍重读最新快照；Valid 保持，不锁存旧值 |
| Write FB Execute 保持 | 只在上升沿写一次；输入变化不在线更新，需下降后重触发 |
| `GroupValueSource::set` | `unsupported`，不退化为 commanded |
| 非 ACS Velocity/Acceleration | `unsupported`，不做有限差分或伪 Jacobian |
| DH/Joint 未绑定或非 serial | `precondition_failed`；不从插件类型名猜模型 |
| 未知 GroupParameter | `unsupported`；已知但只读参数写入同样 `unsupported` |
| Dynamics/SWLimits 活动态写入 | `precondition_failed`；活动/排队命令、坐标与旧配置逐位不变 |
| Dynamics/SWLimits 任一字段非法 | `invalid_argument`；整表原子拒绝 |
| 查询未知/淘汰 CommandID | `out_of_range`，不返回当前命令资料代替 |
| 组成员从外部直接修改组托管 SWLimits | `precondition_failed`，不制造读回漂移 |

## 5. 验收指标

| # | 指标 | 门槛 |
|---|---|---|
| 5.1 | 19 个公开类名与 B 级字段编译锚点 | 19/19；旧 Position 类仍可编译 |
| 5.2 | Read 生命周期 | 每类覆盖 disabled/null/valid/Enable 下降；值输出无旧值泄漏 |
| 5.3 | 配置与 owner 反查 | 1/8 轴、移除后槽位变化、未入组、越界、非 ACS virtual-axis 拒绝 |
| 5.4 | DH/Joint | 2/3/6/8 轴定长回读；未绑定、非 serial、数量/有限性拒绝；绑定后冻结 |
| 5.5 | Position/Velocity/Acceleration | command/actual 各 ≥2 轴逐值；Position MCS/PCS 沿现有 oracle；set 与非 ACS 高阶回读拒绝 |
| 5.6 | MotionState/CommandInfo | linear/circular/direct/window/stop/interrupt 的标准运动位、ID、accepted/active、进度/距离/周期逐周期锚点；终止后查询拒绝 |
| 5.7 | 参数 | dynamics mode 与 transition reference point 逐值往返；未知/未承载枚举拒绝；活动态原子性；percentage 与 absolute 新命令轨迹按比例 oracle |
| 5.8 | 三类 Dynamics | 独立槽、负/零/正更新规则、1/8 轴 Jogging、非法字段/size/活动态原子拒绝；Reference percentage 与 Default 显式选择真实影响新提交 |
| 5.9 | SWLimits | 1/8 轴事务读写、之后绝对/相对目标拒绝、外部轴写拒绝、移组后恢复；活动轨迹不追改 |
| 5.10 | RT/质量 | 新 FB call 10 万输入 fuzz 零 crash；冻结窗口 10 万周期零分配；RT scan 通过 |
| 5.11 | 回放与文档 | 18 份回放逐位不变；Part 4 口径从 21/68 更新为 40/68 有同名门面、28 项缺失，但仍逐项标部分/缺失且不宣称合规 |

## 6. 不做

| 项 | 后续归属 |
|---|---|
| `MC_GroupSetPosition` | 坐标重置会改变运动域，另立语义矩阵 |
| `MC_GroupReadError` | 先定义固定容量错误记录、Axis/Record 选择与淘汰合同 |
| MCS/PCS 速度与加速度 | 需要 kinematics Jacobian/Jdot 或等价同拍导数接口 |
| virtual AXIS_REF / AxisID 全局注册表 | 宿主引用与身份层，不在 AxisGroup 内伪造 |
| `mcSetValue` | 需要独立 set-value 状态域 |
| 在线 Dynamics/SWLimits 追改活动命令 | 属声明行为变更，需单独矩阵与回放 |
| Jog、工具/载荷、刚体、同步/跟踪 | P4-B2/B3 或后续专批 |
| ST 引脚与正式供应商声明 | 先完成 C++ 结构能力，再由 Part 4 机读 I/O 批次承接 |

## 7. 实现记录（KB-073）

- 19 个标准名称均新增 C++ 门面；旧 Position 双门面保留兼容并复用 v2
  Source 型实现。配置、DH/Joint 元数据、Dynamics 与 SWLimits 全为最多 8 轴
  的固定容量状态，读块同拍 Valid，写块上升沿同拍 Done/Error。
- MotionState/CommandInfo 对普通、Direct 与 joint-window 命令提供真实同拍
  状态；Cartesian window 没有逐段 CommandID 存储，查询显式 unsupported。
  `mcSetValue`、非 ACS 速度/加速度、virtual AXIS_REF 与 start-point 仍按矩阵
  显式拒绝。
- percentage mode 消费 Reference Dynamics；显式 Default 选择直接替代四阶
  输入，不做 percentage 二次缩放。SWLimits 整表预检后写入成员轴唯一限位
  状态，所有 joint/Cartesian 提交在接管前检查最终 ACS 目标。
- `plcopen_core_part4_p4b1_tests` 覆盖 1/2/3/6/8 轴、生命周期、状态、
  部分更新与原子拒绝；10 万输入 fuzz 零崩溃，10 万冻结周期零分配。
  默认配置下 18 份回放逐位不变。
- 出口口径为 40/68 有同名门面、28 项无同名入口；本批门面仍有 E 级和
  分支边界，不构成 Part 4 合规声明。

---

*草案创建：2026-07-13。维护者批准后按“基础数据与配置域 → 回读 → 参数/
Dynamics → SWLimits → 文档门禁”五个纵向切片测试先行实现。*
