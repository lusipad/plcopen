# 运维手册

## 故障处置总则

先停止提交新的命令，保留最后一个错误码和轴/组快照，再判断是否允许
`FbReset`，或是否需要由宿主安全系统接管。`FbReset` 只清除可复位错误，不替代
STO/SS1，也不证明驱动器已经安全。

## 错误码表

| `ErrorCode` | 常见原因 | 处置 |
|-------------|----------|------|
| `ok` | 调用成功，无需恢复动作 | 继续执行下一条命令；无需复位 |
| `invalid_argument` | 空引用、NaN/Inf、零或负动力学参数、无效组合 | 修正输入；确认 Execute 上升沿只提交一次 |
| `out_of_range` | 超出固定数组、软限位或表点范围 | 检查维数、目标和固定容量；不要扩大 RT 容器 |
| `capacity_exceeded` | 队列、窗口或固定表已满 | 降低连续段数/窗口深度，等待已承诺段完成 |
| `infeasible` | 在动力学约束下没有可行的 OTG/TOPP 解 | 增加可用周期或放宽合法动力学限值，保留原轨迹 |
| `not_converged` | 有界数值求解在 32 次迭代内仍无法同时满足位置/姿态门 | 换用更接近目标的 seed；仅在诊断场景显式调用 best-effort |
| `singular_region` | 数值 IK 在病态区域把阻尼升到上限后残差仍无法下降 | 改变目标或 seed 以离开奇异构型，不要反复提交同一输入 |
| `limit_infeasible` | 硬关节限位投影阻止数值 IK 到达目标 | 检查关节限位与目标可达性；成功前保留原承诺轨迹 |
| `precondition_failed` | 轴未上电、组非 standby、所有权/同步状态不满足 | 读取状态快照，按生命周期解除 owner 或先停止 |
| `unsupported` | 当前矩阵明确未实现，例如 CENTER/RADIUS 圆弧、EtherCAT | 不要重试同一输入；切换到已声明支持的路径或宿主实现 |
| `bytecode_version_mismatch` | ST 工件字节码版本与当前运行时不匹配 | 用当前工具链重新编译 ST 程序，或成对升级编译器与运行时 |

## 典型现象

| 现象 | 首查项 | 不要做 |
|------|--------|----------|
| 命令一直 `busy` | 是否位于 buffered 队列、组是否已被窗口承诺 | 不要在 RT 线程强行改目标 |
| `command_aborted` | 是否有 aborting 接管、GroupStop 或掉电 | 不要把它当成规划失败 |
| blending 退化为 buffered | `last_blend_degraded_command()` 和提交时机 | 不要盲目增大公差来掩盖反折/过迟提交 |
| cam 换表被拒绝 | 当前 master 相位、新旧 slave 位置差与 tolerance | 不要忽略位置连续性门 |
| stream 进入 stopped | timeout/extrapolation 配置、生产者心跳 | 不要在断流时直接写零目标 |
| 组进入 errorstop | `last_cartesian_error()`、成员掉电/错误快照 | 不要只重置组而不检查驱动器 |

## 事故记录

每次现场事件至少记录：版本/tag、周期配置、轴/组配置、输入命令、错误码
文本（`rt::to_string`）、最后 100 个快照、是否触发安全系统，以及回放
语料 ID。修复后先在 ServoSim 和黄金回放中复现，再安排真机验证。

## Trace 可视化

参考执行器的 `PLCT v1` 二进制 trace 可以直接导出为 CSV 或单文件 HTML：

```bash
python tools/plcopen_trace.py rt_executor_trace.bin --csv rt_executor_trace.csv --html rt_executor_trace.html
```

HTML 输出是自包含的 SVG 时序图：每个轴一条 lane，横轴为 tick，绿线表示命令
位置，并附带最终位置、最大速度/加速度和最大单周期位置步长摘要。先用它判断
问题出在命令序列、动力学约束还是宿主桥接，再决定是否需要更深的 replay /
真机复现。

## 安全边界

普通运动错误处理不能宣称 STO、SS1、SIL 或 PL。安全相关动作必须走已验证
的外部安全系统；项目责任矩阵见
[STO/SS1 integration boundary](https://github.com/lusipad/plcopen/blob/main/doc/compliance/sto-ss1-integration-boundary.md)。
