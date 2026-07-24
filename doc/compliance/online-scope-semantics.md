# D3 在线调试示波器语义矩阵 v1

> 状态：**已批准（2026-07-24，维护者按
> `D3 Scope → D1 ST LSP → H1 → T2b → H3` 顺序授权开发；本矩阵限 v1
> executor `PLCT` 通道）**。
>
> 设计依据：[long-term-plan T42](../planning/long-term-plan.md) 与
> [软件极致计划 D3](../planning/software-excellence-plan.md)。本矩阵只定义
> commissioning 工具面，不改变运动内核或 ST 调试器语义。

## 定位与不变量

D3 v1 把既有 executor 周期 trace 从“运行结束后一次性落盘”升级为
“运行中由非 RT 消费者持续落盘、工具侧实时跟随并按触发条件冻结窗口”。

| 既有合同 | 保持 |
|---|---|
| executor 双域（ADR-0007） | RT 线程仍只消费承诺帧、驱动 Servo、向固定容量 SPSC 发布记录 |
| RT 路径纪律 | RT 线程零文件 I/O、零阻塞锁、零堆分配；队列满只计数并丢当前 trace 记录 |
| `PLCT v1` | 魔数、版本、记录大小和记录字段逐位兼容；已有静态文件继续可读 |
| 运动输出与回放 | D3 只观察，不修改 setpoint、feedback、规划时长或周期顺序 |
| ST L7 调试器 | `DebugTraceRecord` 保持宿主内部接口，不冻结为 D3 跨工具文件格式 |
| 默认依赖 | core 和默认 wheel 不新增依赖；Rerun 仍是显式可选工具依赖 |

## 决策点

| # | 决策 | v1 语义 | 理由 |
|---|---|---|---|
| 1 | 在线落盘 seam | RT 线程把完整 `TraceRecord` 推入单生产者/单消费者环；非 RT writer 写 header、持续追加完整记录并刷新 | 文件 I/O 与调度抖动不进入周期路径 |
| 2 | 兼容性 | `--trace PATH` 参数与 `PLCT v1` 格式不变；差异仅是文件在运行中增长 | 已有工具、CI 和用户脚本无需迁移 |
| 3 | 队列耗尽 | writer 落后导致队列满时，RT 线程丢当前 trace 记录并递增 `trace_dropped`；运动执行继续 | 示波器不得反压或阻塞控制 |
| 4 | 静态读取 | header、版本、记录大小或尾部完整性不合法时拒绝整个文件，不返回伪完整数据 | 截断数据不能冒充有效证据 |
| 5 | 触发条件 | 一个轴的 position/velocity/acceleration，支持 rising/falling 阈值穿越；首个样本只建立比较基线，不自行触发 | v1 接口小且行为可精确测试 |
| 6 | 窗口 | `pre_cycles` 表示触发拍之前保留的完整周期数，另含触发拍；`post_cycles` 表示触发后完整周期数；输出包含所有轴记录并按原顺序保存 | “前 N + 触发 + 后 M”没有 off-by-one 歧义 |
| 7 | follow 完成 | 捕获到触发后第 `post_cycles` 拍的全部记录后冻结；`--timeout` 从 follow 开始计总时限，等待 header、触发与 post 窗口均计入，持续来数据也不延长 | 在线命令必须有界，不无限等待 |
| 8 | 输出 | 冻结窗口可写兼容 `PLCT v1`、CSV、单文件 HTML；Rerun 可实时记录所有跟随样本并写显式 `.rrd`，Viewer 仅显式 `--spawn` | 保留无依赖路径，同时提供 commissioning 曲线 |
| 9 | 配置域 | 轴号非负，周期数非负，阈值有限，超时为正；不合法组合在打开输出前拒绝 | 无部分文件、无静默修正 |

## 退化、拒绝与错误规则

| 条件 | 行为 |
|---|---|
| trace 路径不可创建 | executor 在进入周期循环前失败，返回非零 |
| writer 在运行中写失败 | writer 锁存失败；executor 完成线程回收后返回非零，不把部分文件报告为 PASS |
| trace 环满 | 控制继续；`trace_dropped` 递增；executor 健康门失败 |
| 静态文件尾部不足一个完整记录 | 拒绝并报告 truncated record |
| follow 暂时读到半条记录 | 保留残片并继续等待，不输出半条记录 |
| trigger 字段、轴号、阈值、窗口或超时无效 | CLI 参数错误，未创建 window/CSV/HTML/RRD |
| 阈值始终未穿越 | 总时限到达后失败，不生成冻结窗口 |
| Rerun 未安装但请求 `.rrd`/`--spawn` | 明确依赖错误；PLCT/CSV/HTML 路径不受影响 |
| `--spawn` 未同时指定 `.rrd` | 参数错误，避免只有易失 Viewer 而无证据文件 |

## 验收指标

| 指标 | 门槛与证明 |
|---|---|
| 在线可见 | executor 尚未退出时，文件已含合法 header 和至少一条完整记录 |
| 完整落盘 | 无压力场景 `trace_records == cycles × axes` 且 `trace_dropped == 0` |
| RT 隔离 | RT 循环只调用 SPSC `push` 与 lock-free 计数；文件 open/write/flush/close 只在非 RT writer |
| 窗口精度 | rising/falling worked examples 均精确输出 `pre + 1 + post` 个周期的全部轴记录 |
| 截断安全 | 静态尾部截断拒绝；在线半条记录等待补齐后只发布一次 |
| 格式兼容 | 既有 PLCT fixture 与 executor 端到端文件继续通过摘要、CSV、HTML 测试 |
| Rerun | 有可选依赖时 `.rrd` footer 验证通过，实体至少覆盖每轴 position/velocity/acceleration |
| 平台 | Windows 与 Linux executor/tool 端到端门通过 |
| 回归 | 全量 CTest、RT scan、18 份 replay 零差异 |

## 不做

- 不把本工具宣称为认证测量仪器、实时网络协议或远程运维服务；
- 不修改 `PLCT v1` 记录字段，不混入 actual/error/ST watch 等新通道；
- 不把 ST `DebugTraceRecord`、breakpoint 或 force 语义并入 v1；
- 不做复合布尔表达式、多触发器、hysteresis、自动缩放策略或无限历史；
- 不在 RT 线程执行阈值判断、Rerun 调用、文件 I/O 或动态内存操作。
