# Part 1/2 C4 语义清零矩阵

> 状态：**已实现并验收（2026-07-16，KB-079）**。维护者指令
> “把这些全部都做完”批准全范围；`plcopen_core_part1_c4_tests` 提供
> D-01～D-20 的公共 API 证据。
> 本文件是 D-01～D-20 中剩余 16 项的 normative 验收规格。批准覆盖必要的
> 周期输出声明变更与黄金回放升级；不覆盖 modulo 轴、真实扭矩闭环、
> Safety 或 EtherCAT 产品语义。

## 定位与不变量

| 合同 | 保持 |
|---|---|
| 分层 | PLCopen 生命周期只在 L5 axis/L6 fb；L0-L4 不引入标准语义 |
| 所有权 | AxisModel 仍是单轴 setpoint 唯一写者 |
| RT | 周期路径禁分配、锁、异常、墙钟；循环和容量均有硬上限 |
| 队列 | 沿用固定 QueueCapacity 与 command ID；拒绝不产生部分提交 |
| 错误 | 轴故障、FB 自身错误、普通接管三类终态必须可区分 |
| 回放 | 有意输出变化走 KB 声明与量化升级；其他语料逐位不变 |

## 决策点

| ID | 已批准语义 |
|---|---|
| D-01 | Execute 下降不丢弃已接受命令；Done/Aborted/Error 至少置位一周期，下一上升沿可重用 |
| D-02 | InVelocity/InEndVelocity/InGear/InTorque/InSync 是持续 setpoint 状态，Execute 低仍更新，owner 失效即复位 |
| D-03 | 正常 Enable=false 进入 Disabled；运行中外部 power feedback 丢失触发 ErrorStop 并锁存轴错误 |
| D-04 | MC_Stop 零速后 Done；Execute 高时保持 Stopping 并拒绝运动，下降后回 Standstill |
| D-06 | Relative 以执行时 set position；Additive 仅在 DiscreteMotion 使用最近终点，其余状态使用执行时 set position |
| D-07 | MoveVelocity 接受有符号 Velocity，与 Direction 符号相乘；零速度拒绝，shortest_way 对线性速度拒绝 |
| D-08 | MoveContinuous* 接受有符号 EndVelocity，零值保留为显式不支持 |
| D-09 | TorqueControl 建立持续 CST command owner；TorqueRamp 按任务周期换算，运动限制进入 servo setpoint；普通运动接管清 torque owner，InTorque 持续比较 commanded torque |
| D-10 | AccelerationProfile 按时间-加速度段积分，完成后保持积分终速 |
| D-11 | SetPosition、WriteParameter/WriteBoolParameter、WriteDigitalOutput 的 ExecutionMode 进入统一轴管理命令生命周期：immediate 同周期执行；queued 固定容量排队，在既有单轴运动清空后逐周期执行；排队/执行期间 Busy=TRUE，完成或拒绝后 Busy=FALSE；SetPosition Relative 以实际执行时的 actual position 为基准 |
| D-14 | GearOut/CamOut 从脱同步瞬间速度进入 ContinuousMotion；无后继时保持最后速度 |
| D-16 | Axis ErrorStop 使相关 active/buffered FB 报 Error/ErrorID，不报 CommandAborted |
| D-17 | active FB 运行期自身错误记录 Error；首个 buffered 后继立即成为 active |
| D-18 | Standstill 下 MoveSuperimposed 在会话期进入 DiscreteMotion，完成后释放 |
| D-19 | Enable 型 FB 有 Busy；不可恢复错误锁存到 Enable 新上升沿，可恢复等待态保持 Busy/Valid=false |
| D-20 | ContinuousUpdate 许可只在 Execute 上升沿锁存，本命令中后改 TRUE 不生效 |

## 退化与拒绝规则

| 输入/状态 | 结果 |
|---|---|
| MoveVelocity Velocity == 0 | `invalid_argument` |
| MoveVelocity Direction == shortest_way | `unsupported` |
| Continuous EndVelocity == 0 | `unsupported` |
| 未建模 modulo 方向组合 | 线性轴按符号规则；不声明多圈最短路 |
| TorqueControl 无真实驱动反馈 | InTorque 比较 commanded torque；Velocity/Acceleration/Deceleration/Jerk/Direction 透传到 servo contract，不宣称实际电流/扭矩到达 |
| TorqueControl BufferMode != aborting | `unsupported`；持续 CST owner 没有可定义的 buffered 完成点 |
| TorqueControl Direction == shortest_way | `unsupported`；扭矩方向只接受 current/positive/negative |
| AccelerationProfile 非零阶保持插值 | `unsupported` |
| SetPosition 非有限输入或重标定后任一坐标/限位无效 | 原子 `invalid_argument`/`out_of_range` |
| 未定义 ExecutionMode | 上升沿以 `unsupported` 拒绝，不入队 |
| queued 参数、通道或 SetPosition 在实际执行时非法 | 先保持 Busy；执行周期原子失败，随后 Error/ErrorID，不产生部分写入 |
| 轴管理队列已满 | 上升沿以 `capacity_exceeded` 拒绝，既有命令顺序不变 |
| 运行期 FB 错误无 buffered 后继 | Error 终结并停在当前安全状态 |
| Enable 保持高的不可恢复参数/引用错误 | Error 锁存；条件恢复不自动重启 |

## 验收指标

| 维度 | 门槛 |
|---|---|
| 条款 | 16 个开放 D 项逐项有测试和实现证据；D-05/12/13/15 保持关闭 |
| 生命周期 | Axis、Profile、Probe、Sync、Phasing 各至少一个 Execute 单拍终态测试 |
| 持续状态 | Velocity/Continuous/Gear/Cam/Combine/Torque 的 Inxxx 与接管复位全覆盖 |
| 状态机 | power loss、Stop lock、superimposed、GearOut/CamOut 状态转换矩阵 |
| 数值 | signed 输入、Relative/Additive 基准、SetPosition 偏置、AccelerationProfile 手算 oracle |
| 错误 | axis error、FB runtime error、buffered successor 三类可区分 |
| RT/质量 | 分配守卫、RT scan、fuzz、覆盖率、跨平台与回放门禁全部通过 |

## 不做清单

- 不新增 modulo/multi-turn 轴模型；
- 不新增驱动电流环、扭矩传感器或 Safety torque 语义；
- 不新增外部 Profile 文件解析和任意插值器；
- 不把 PLCopen 自声明、会员资格或 Beckhoff 黑盒性能写成已完成。

## 实现结果

- D-01～D-20 全部关闭，原四个 P1-A 项保持关闭；
- 新增运动命令错误固定账本、轴管理命令固定队列/结果账本、Stop 锁、
  Torque owner、加速度 profile 与统一 Enable 回读生命周期，均保持固定容量周期路径；
- Axis/Profile/Probe/Sync/Phasing 的 Execute 单拍终态、持续 Inxxx、
  signed velocity、SetPosition 重标定和错误接续由专项测试覆盖；
- 既有 18 份回放逐周期零差异；正式 B/E/V 供应商声明和 PLCopen 认证仍
  是 C4 之外的独立工作。
