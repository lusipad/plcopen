# ST 批次 L6 语义矩阵：SFC 文本执行语义

> 状态：**已实现并复审通过（2026-07-17）**。本矩阵定义文本 SFC
> 的步、转换、分支/汇合和全部 N/S/R/L/D/P/SD/DS/SL 动作限定符；图形
> 编辑器不在范围内。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| 网络 | 初始步、普通步、转换、选择分支/汇合、同时分支/汇合、动作块 |
| 执行 | 每 scan 用旧 active-step 快照求值转换，构造新 active set，再更新限定符并执行动作 |
| 时间 | 每动作定时器使用 task period 整数累加，精度为任务周期，无壁钟 |
| 动作 | 动作体调用 L2b POU 语句；同一动作每 scan 至多执行一次 |
| RT | 步/边/动作/定时器全部加载期定容，周期路径 O(静态网络大小)、零分配 |

## 1. 网络求值决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L6-D01 | 初始步 | 每个 SFC 网络恰好一个初始步；加载/显式 restart 时激活 |
| L6-D02 | 转换条件 | 只允许无副作用 BOOL 表达式与纯 FUNCTION；不允许 FB 调用或赋值 |
| L6-D03 | 求值快照 | 仅从 scan 开始的 active set 判断 enabled；本 scan 新激活步不能连跳第二次 |
| L6-D04 | 选择分支 | 多个条件为 TRUE 时仅源码声明最前者触发；优先级固定且入诊断/trace |
| L6-D05 | 同时分支 | divergence 激活全部后继；convergence 仅在全部前驱 active 且公共条件 TRUE 时触发 |
| L6-D06 | 步更新 | 先统一计算所有 firing transitions，再原子更新 active set；同一步同时激活/退出为编译错误网络 |
| L6-D07 | 动作顺序 | 按动作声明序执行；多步引用同一动作时先合并限定符控制，动作体仍只执行一次 |

## 2. 动作限定符逐项矩阵

| 限定符 | 已批准语义 |
|--------|------------|
| N | 步 active 时每 scan 执行；步退出当 scan 停止 |
| S | 步激活时置 stored latch；持续执行直到 R |
| R | 步 active 时清同名动作的 pending/stored latch；同 scan 与其他限定符冲突时 R 优先 |
| L | 步 active 后立即执行，最多持续指定 TIME；步先退出则立即停止 |
| D | 步连续 active 达指定 TIME 后执行；步退出立即停止并清延迟 |
| P | 步由 inactive→active 的首个 scan 执行一次 |
| SD | 步激活时存储一个延迟请求；到时置 stored latch，即使原步已退出；R 可取消 pending/active |
| DS | 仅在步连续 active 达指定 TIME 后置 stored latch；到时前退出则取消；置位后持续到 R |
| SL | 步激活时立即置 stored latch，并在指定 TIME 后自动清除；R 可提前清除 |

所有 TIME=0 的延迟在当前限定符更新阶段到期；仍不让新激活动作在同 scan
重复执行。一个动作同 scan 的控制优先级为 `R > 到期清除 > stored 置位 >`
`N/L/D/P`，避免依赖声明遍历偶然性。

## 3. 不安全网络与容量

| 形态 | 结果 |
|------|------|
| 无/多初始步、不可达步、无出口非终止分支 | 稳定编译诊断（显式终止步除外） |
| 同时汇合缺前驱、分支/汇合不配对、同一步冲突更新 | `sema_unsafe_sfc_network` |
| 转换条件有副作用、动作控制冲突未被矩阵定义 | 稳定编译诊断 |
| 负 TIME、容量超限 | `sema_range_violation` / `capacity_exceeded` |

- 每网络 step ≤1024、transition ≤4096、action ≤1024、每 step 动作块 ≤64、
  分合支宽度 ≤64；所有位集和计时器静态分配。
- 编译产物报告可达图、最大并行 active step 数和每 scan 最坏求值/动作预算。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L6-A01 | 顺序、选择、同时网络 | 手工 step-set oracle 逐 scan 一致 |
| L6-A02 | 九种限定符 | 每种 0/1/N 周期、早退、R 冲突与重新激活全绿 |
| L6-A03 | 同 scan 快照 | 无连跳、原子 active-set 更新用例全绿 |
| L6-A04 | 不安全网络 | 每类稳定诊断且恢复可继续报后续错误 |
| L6-A05 | POU/任务集成 | action fault 进入所属 task fault；其他 task/运动域隔离 |
| L6-A06 | trace | step/transition/action/qualifier 事件确定且有源位置 |
| L6-A07 | RT/容量 | scan 零分配；精确预算 N 成功、N-1 事务性失败 |
| L6-A08 | fuzz | 结构化网络 ≥100,000，0 crash/UBSan |

## 5. 实现与复审证据

- Windows Debug L0-L6 定向 CTest 17/17、feature-set 与 conformance 2/2
  通过；base/L2b/L5/L6 fuzz smoke 各 3000 轮通过。
- Release 固定种子 L6 结构化网络 fuzz 100,000 轮通过；WSL Clang
  ASan/UBSan 最新重建后的 L6 专项与 L6 fuzz smoke 3000 轮均为 0 报告。
- runner 覆盖逐步 resume、scan 内 staged 状态不可见、fault/abort 回滚、精确
  WCET N/N-1、嵌套同时分支峰值和 caller-owned trace 容量边界。
- 第三轮独立语义复审结论：APPROVE，无 remaining semantic blocker。

## 6. 不做

- SFC 图形编辑器、布局/连线 round-trip、LD/FBD action、macro step 和在线
  网络修改不做。
- IL 不做；PackML 状态模型是基于 SFC 的独立应用库，不属于 L6 本体。

---

*批准：2026-07-17；实现：2026-07-17；复审：APPROVE。*
