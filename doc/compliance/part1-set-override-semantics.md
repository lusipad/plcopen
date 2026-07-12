# MC_SetOverride 语义矩阵（P1-A2 草案）

> 状态：**已预批准**（2026-07-12，维护者批准本计划后续全部调整）。本矩阵关闭 Part 1 审计 D-12，将
> `MC_SetOverride` 从非标准 Execute/Done 百分比接口改为标准
> Enable/Enabled 与 `VelFactor ∈ [0,1]`。这是公开接口和活动轨迹行为的
> 声明变更；实现前必须批准，回放若发生变化不得静默重录。

## 1. 不变量与变更面

| 项 | 裁决 |
|----|------|
| 标准 FB 公共面 | `FbSetOverride` 不再继承 `AxisExecuteFb`；公开 `axis_ref`、`enable`、`vel_factor`、`enabled`、`error`、`error_id` |
| 电平语义 | 每次 `call()` 都按 Enable 处理，不使用上升沿；Enable 高时持续验证并应用当前 VelFactor |
| Disable | Enable 低时清 `Enabled/Error/ErrorID`，不恢复 1.0，轴保留最后成功倍率 |
| AxisModel 内部量纲 | 单轴 override 从 percent 改为 factor `[0,1]`，所有计算删除 `/100.0` |
| 既有能力 | KB-003/020 的新命令缩放、活动离散/homing/continuous/profile 重规划范围保持 |
| 同步从轴 | Gear/Cam/Combine 继续由 master value 驱动，不受本地 override 重规划 |
| 组所有权 | 活动组成员调用单轴 override 继续原子拒绝 |

## 2. 接口矩阵

| I/O | 类型 | 方向 | 语义 |
|-----|------|------|------|
| Axis | `AxisModel*` | B 输入 | 轴引用；空引用报 Error |
| Enable | bool | B 输入 | TRUE 时每周期处理；FALSE 时仅清 FB 输出 |
| VelFactor | double | B 输入 | 闭区间 `[0,1]`；1=原速，0=受控暂停 |
| Enabled | bool | B 输出 | 当前周期输入合法且倍率已成功应用时 TRUE |
| Error | bool | B 输出 | 当前周期处理失败时 TRUE |
| ErrorID | ErrorCode | E 输出 | 仓库诊断扩展；成功/Disable 时为 ok |

删除标准 FB 上的 `execute/percent/Done/Busy/Active/CommandAccepted/
CommandAborted/CommandID`。不提供同类中的 deprecated alias，避免一个对象同时
存在互相冲突的边沿/电平语义。

## 3. VelFactor 组合矩阵

| Axis 状态/命令 | Factor 1..0 | Factor 0 | 从 0 恢复 >0 |
|---------------|-------------|----------|---------------|
| Disabled | 保存倍率，Enabled=TRUE；不改变状态 | 同左 | 同左 |
| Standstill | 保存倍率，Enabled=TRUE；不改变状态 | 保存 0；保持 Standstill | 保存新倍率 |
| DiscreteMotion/Home/Profile | 从当前 p/v/a 按新速度上限重规划至原目标 | 按原 Deceleration/Jerk 受控减速到零；保持原 AxisStatus 和 command ownership，位置驻留 | 从暂停位置按新上限重规划至原目标 |
| MoveVelocity | 下一周期目标速度乘 factor | 按 Deceleration/Jerk 受控降到零；保持 ContinuousMotion 和命令 active | 从零恢复有符号目标速度 |
| Continuous target/hold | 活动段重规划，hold 速度同步缩放 | 受控降到零并保持原命令/状态 | 恢复并继续原目标/保持速度 |
| Halt/Stop | 不追改停止剖面 | 不追改 | 不追改 |
| Gear/Cam/Combine | 保存倍率但不重规划同步节点 | 同左 | 同左 |
| 活动组成员 | `invalid_argument`，不改倍率/轨迹/FB 以外状态 | 同左 | 同左 |

## 4. Factor=0 暂停状态机

单轴新增 `override_paused_`。0 因子不是瞬时冻结：

1. 从当前命令 p/v/a 使用活动命令的 Deceleration/Jerk 规划到安全停止点；
2. 减速过程中 AxisStatus 保持原命令状态，command id/FB ownership 不变；
3. 停稳后位置逐周期保持，命令不 Done，轴不进入 Standstill；
4. factor>0 时从暂停位置重新规划到原 target；MoveVelocity 恢复持续速度；
5. 外部 aborting/Halt/Stop/掉电/Error/组接管按既有优先级取消暂停和原命令；
6. 零因子期间 buffered 后继仍保持队列，不提前启动。

该语义与已批准并验证的 GroupSetOverride factor=0（KB-058）同族，但单轴
必须复用单轴 command ownership 和完成账本，不能借用组状态。

## 5. 错误与原子性

| 输入/状态 | 结果 |
|-----------|------|
| Enable=FALSE | 清 FB 输出；轴倍率与轨迹不变 |
| Axis=null | Enabled=FALSE，Error=TRUE，ErrorID=invalid_argument |
| VelFactor NaN/Inf/<0/>1 | Error；轴倍率、profile、tick、target、active id 与队列不变 |
| 活动重规划失败 | Error；恢复旧倍率、旧 profile、tick、暂停标志和 hold velocity |
| 同一合法 factor 重复调用 | 幂等，Enabled=TRUE，不重复重规划 |
| Error 消失且 Enable 保持 TRUE | 当周期重试；成功后 Error 清、Enabled=TRUE |

错误优先级：空 Axis → factor 域 → 组所有权/轴前置条件 → 重规划结果。

## 6. 兼容策略

底层 `AxisModel::set_override(double)` 改为标准 factor 量纲，不保留含糊的
percent 同名入口。若 C++ 迁移确有需要，只允许另名
`set_override_percent_legacy(double)`，标记 deprecated，内部除以 100 后调用
标准入口；**标准 FB 不暴露该入口**。本批默认不增加 legacy API，编译失败
用于迫使调用方迁移，迁移文档列出 `25.0 → 0.25`。

## 7. 验收

| # | 验收 | 门槛 |
|---|------|------|
| 1 | Enable/Enabled/Error 电平表 | 全组合逐周期通过；Disable 保留最后倍率 |
| 2 | 0、边界 1、典型 0.25/0.5、NaN/Inf/越界 | 合法成功；非法逐字段原子不变 |
| 3 | Discrete/Home/三 Profile/MoveVelocity/Continuous | 降倍率不越新包络，最终目标与状态正确 |
| 4 | factor=0 | 受控减速、位置稳定、状态与 active id 保持；恢复到目标 |
| 5 | buffered/aborting/掉电/Error/组成员 | ownership、队列与终态符合矩阵 |
| 6 | 旧 percent 调用点 | 全部迁移为 factor；仓库无 `percent`/`/100.0` override 残留 |
| 7 | 门禁 | Debug 全构建、CTest、RT-safety、回放、生成矩阵、文档严格构建全绿 |

## 8. 文档出口

实现后关闭 D-12、更新 §2.4.1-p/§3.18、KB-003/020、L2a 引脚表、迁移
指南与 P1-A 计划。若回放变化，先登记声明变更并说明原因；本矩阵批准本项
语义变化，但不自动批准任何无关基线变化。

---

*创建并预批准：2026-07-12。*
