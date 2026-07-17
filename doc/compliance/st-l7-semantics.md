# ST 批次 L7 语义矩阵：监控、force 与调试器

> 状态：**已实现并复审通过（2026-07-17）**。L7 提供无锁变量
> 快照、L3 force 控制、断点/暂停/单步和确定性 trace；网络协议与 UI 不在
> 内核范围内。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| 监控 | 符号表驱动的 seqlock 快照，读者只见完整 scan 边界版本 |
| force | 复用 L3 掩码与边界时序；调试 API 不维护第二套写覆盖机制 |
| 断点 | 调试构建在字节码指令前检查定长 breakpoint bitmap；发布构建零检查成本 |
| 暂停 | 只暂停目标 ST task，不暂停运动 RT 或其他 task；未完成 scan 不提交 L3 输出 |
| 安全 | 调试控制由宿主鉴权；内核不开放 socket、不持有用户凭据 |

## 1. 监控与调试决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L7-D01 | 快照点 | task 成功提交后发布符号值、active POU/SFC 状态与版本；fault 同样发布 fault 快照但不发布 Q shadow |
| L7-D02 | 读一致性 | seqlock 读失败时重试有界次数，仍冲突则返回 `snapshot_busy`；不阻塞 writer |
| L7-D03 | 符号寻址 | stable symbol ID + 完整限定名；大小写不敏感名字仅用于查找，不作为持久 ID |
| L7-D04 | 断点命中 | 在对应源码语句第一条指令执行前暂停；同位置多断点去重；返回 task/POU/source/instruction |
| L7-D05 | 单步 | step-in 执行一条源语句；step-over 执行当前语句但跨过被调 POU；step-out 运行到当前 POU 返回 |
| L7-D06 | 暂停状态 | 暂停是宿主已知状态，不触发 L5 wallclock fault；继续后从尚未执行的指令恢复，预算余量保持 |
| L7-D07 | fault 调试 | fault 后可读快照/调用栈；必须先按 L5 reset/restart，再允许 continue |
| L7-D08 | trace | 定长环记录 scan/task/POU/SFC/FB/fault 事件；溢出丢最旧并递增 dropped 计数 |

## 2. force 控制

- 设置、更新、解除 force 只在扫描边界进入 L3 队列；API 返回将生效的目标
  task/version，不假装立即生效。
- 不允许通过普通监控写变量；所有写覆盖必须走 force，带类型、宽度、范围和
  权限校验。
- 暂停期间可排队 force，但只在任务恢复后的下一个扫描边界生效。

## 3. 拒绝与容量

| 形态 | 结果 |
|------|------|
| 发布构建设置断点/单步 | `debugging_disabled` |
| 无效 symbol/source/instruction、类型不符 force | 稳定诊断，不做近似匹配 |
| fault task 直接 continue | `task_faulted` |
| 在线代码替换、状态迁移、远程未鉴权连接 | 明确不支持 |

- breakpoint ≤4096、watch symbol ≤8192、trace 记录编译期定容、快照总字节
  受 L3 镜像/符号容量共同限制。
- 调试构建每指令 breakpoint 检查 O(1)；发布构建不得包含 bitmap 分支。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L7-A01 | seqlock 快照 | 并发压力下 0 torn read、TSAN 0 报告 |
| L7-A02 | 断点命中 | IF/循环/POU/SFC 源映射逐项准确 |
| L7-A03 | step-in/over/out | 调用栈与下一源位置 oracle 一致 |
| L7-A04 | 暂停隔离 | 目标 task 不提交半 scan 输出，其他 task/运动 RT 持续 |
| L7-A05 | force | 与 L3 同一队列、版本和拒绝测试；0 第二实现 |
| L7-A06 | fault 恢复 | read-only 调试、reset/restart、continue 顺序全绿 |
| L7-A07 | 发布零成本 | 发布反汇编/基准无 breakpoint 分支或显著回归 |
| L7-A08 | 容量/fuzz | N/N+1、随机控制序列 ≥100,000，0 crash/UBSan |

## 5. 不做

- 在线变更、热替换字节码、状态迁移、时间旅行调试不做。
- 调试 GUI、IDE 插件、网络协议、用户认证和审计存储由宿主/工具层实现。

---

*批准：2026-07-17；实现：completed。*
