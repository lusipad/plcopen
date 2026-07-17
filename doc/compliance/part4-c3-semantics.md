# Part 4 C3 剩余门面语义矩阵

> 状态：**已批准（2026-07-16，维护者；范围为 C3a/C3b/C3c 已声明子集）**。
> 本文件是 Part 4 剩余 11 个同名 C++ 门面的 normative 验收规格。批准不等于
> PLCopen 认证，也不把表中 `unsupported` 的 E/O 级分支计为已实现。

## 定位与不变量

| 合同 | 保持 |
|---|---|
| 组成员所有权 | `AxisGroup` 仍是组运动期间唯一 setpoint 写者 |
| 周期路径 | 禁堆分配、阻塞、墙钟和浮点时间累加；Wait 的公开 Duration 是 ST TIME 纳秒，提交时按已配置 task period 向上换算为整数周期 |
| 坐标栈 | 继续使用 ACS/MCS/PCS 与固定 6D RPY；WCS/FCS/TCS 不静默映射 |
| 运动接管 | GroupHalt 沿原路径减速；新 Aborting 组运动可中止 Halt |
| 回放 | 未声明改变既有轨迹输出，18 份既有语料必须逐周期零差异 |

## 决策点

| 门面 | 已批准语义 |
|---|---|
| `MC_UngroupAllAxes` | Disabled/Standby/ErrorStop 可原子解绑全部成员，完成后 Disabled |
| `MC_GroupPower` | Enable 电平控制全部成员；任一成员失败时 Error 且组进入 ErrorStop |
| `MC_SetCartesianTransform` | 六个标量输入形成固定 6D RPY；CoordSystem=PCS/TCS 选择工件帧或工具帧，ExecutionMode=immediate/queued 进入统一组管理命令生命周期 |
| `MC_SetCoordinateTransform` | vendor ref 适配为固定 6D RPY；PCS/TCS 与 immediate/queued 使用同一组接口 |
| `MC_ReadKinTransform` | Enable 型以 `KinTransformRef` 明确 tag 回读平移或位姿 kinematics 插件非拥有引用；非法 tag 不进入 AxisGroup |
| `MC_ReadCoordinateTransform` | Enable 型按 CoordSystem 回读固定 6D PCS 或 TCS 引用；其他坐标系 `unsupported` |
| `MC_GroupSetPosition` | Standby 下原子设置 ACS/MCS/PCS，支持 absolute/relative |
| `MC_GroupReadError` | Enable 型回读组级 ErrorStop 锁存，不混入 FB 参数错误 |
| `MC_GroupHalt` | 独立 command id；沿原路径受控到零；新 Aborting 组运动可中止 |
| `MC_GroupWaitTime` | Duration 为正 ST TIME 纳秒；`set_task_cycle_period_ns` 明确 task/load 周期，提交时 `ceil(Duration/period)`，因此不早于请求时间；Aborting 零速后计时；Buffered 等前序完成 |
| `MC_GroupTransformPosition` | Enable 型 ACS/MCS/PCS 变换，报告逆解奇异位置 |

## 退化与拒绝规则

| 输入/状态 | 结果 |
|---|---|
| Set transform 的 queued mode | `unsupported` |
| 非 Cartesian `MC_COORD_REF`、WCS/FCS/TCS | `unsupported` |
| moving 中 GroupSetPosition 或 queued GroupSetPosition | `unsupported` |
| GroupPower 与逐轴 MC_Power 同时写 | 本批无命令源身份，登记边界，不伪造仲裁 |
| GroupWaitTime Duration <= 0 | `invalid_argument` |
| GroupWaitTime blending 模式 | `unsupported` |
| Buffered Wait 前已有普通组队列 | `unsupported`，避免伪造跨类型队列顺序 |
| 变换逆解失败 | 原子拒绝；输出不冒充有效结果 |

## 验收指标

| 维度 | 门槛 |
|---|---|
| 字段面 | 11 个同名门面均可从公开头构造并调用 |
| 原子性 | 解组、电源、组位置任一预检失败时不产生部分提交 |
| 生命周期 | Halt/Wait 的 Accepted/Busy/Active/Done/Aborted/Error 与 command id 可观察 |
| 变换 | ACS↔MCS↔PCS 往返误差 <= 1e-9；奇异标志可观察 |
| RT | Wait 只在提交时做整数纳秒到周期换算，周期域仅整数推进；分配守卫与 RT scan 通过 |
| 回归 | Windows 全测、回放、ARM64、clang-tidy、覆盖率既有地板全部通过 |

## 不做清单

- 不新增非 Cartesian 坐标变换插件系统；
- 不新增 MC_Power/GroupPower 命令源仲裁模型；
- 不实现 moving/queued 的 GroupSetPosition 轨迹重参考；
- 不声明 B/E/O 全覆盖、PLCopen 认证或 Beckhoff 黑盒性能等价。
