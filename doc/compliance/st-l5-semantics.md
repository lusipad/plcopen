# ST 批次 L5 语义矩阵：任务、资源与故障恢复

> 状态：**已实现并复审通过（2026-07-17）**。本矩阵覆盖
> CONFIGURATION/RESOURCE、周期与事件任务、多 PROGRAM 映射、看门狗和
> 跨任务一致性；调度由 executor 驱动，VM 不读取墙钟。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| 模型 | 一个 CONFIGURATION 可含多个 RESOURCE；每资源拥有周期/事件 TASK 与 PROGRAM 实例映射 |
| 调度 | 每 RESOURCE 为确定性协作调度器，不并行执行两个 ST scan；运动 RT 域独立 |
| 时间 | 周期与相位量化到 executor 基准 tick；VM 只接收整数 release 序号 |
| 一致性 | 每任务开始读取 L3 快照，成功结束提交事务；同 release 冲突按调度顺序后写胜出并计数 |
| fault | 单任务 fault 隔离，不阻塞其他任务或运动 RT；恢复只能由宿主扫描边界 API 发起 |

## 1. 任务与调度决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L5-D01 | 周期任务 | `INTERVAL` 必须为基准 tick 的正整数倍；`PHASE` 范围 `[0, INTERVAL)` |
| L5-D02 | 事件任务 | 事件在边界采样并锁存一次 release；连续高电平不重复，下一次上升沿再释放 |
| L5-D03 | 优先级 | 0 为最高；同 tick 先优先级、再 TASK 声明序；同 TASK 的 PROGRAM 按映射声明序 |
| L5-D04 | 过期 release | 任务尚未结束时到达的新 release 不排队、不追赶，增加 `missed_release_count` |
| L5-D05 | 程序实例 | 每个 PROGRAM 映射是独立实例；同一实例不能映射到多个任务 |
| L5-D06 | 共享数据 | 仅 GVL/进程映像可共享；任务局部与 PROGRAM 状态不共享；任务提交之间不存在半写可见 |
| L5-D07 | 运动隔离 | ST 任务超时、暂停或 fault 不能阻塞运动周期线程；已有承诺轨迹如何处置由宿主安全策略决定 |

## 2. 看门狗、fault 与恢复

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L5-D08 | 指令看门狗 | 每任务配置静态 instruction budget；超限立即锁存 `task_budget_exceeded`，本次 L3 输出不提交 |
| L5-D09 | 墙钟看门狗 | executor 可报告 `task_wallclock_exceeded`；VM 不测时，收到报告后在边界锁存同级 task fault |
| L5-D10 | fault 隔离 | fault 任务不再接收 release；其他任务继续；fault 原因、POU、指令位置和计数保持可读 |
| L5-D11 | reset_task | 仅宿主可调用；在边界清 fault、清 missed 计数并重新对齐下一 release，变量/FB 状态保留 |
| L5-D12 | restart_task | 显式重新初始化 PROGRAM 实例后再启用；与 reset 分开，不能隐式清状态 |
| L5-D13 | 资源 fault | 配置/映像提交自身失败可锁存 resource fault 并停止该资源全部任务；不跨资源传播 |

## 3. 拒绝与容量

| 形态 | 结果 |
|------|------|
| 周期非 tick 整倍数、非法相位/优先级 | 稳定加载期诊断 |
| PROGRAM 重复映射、无任务映射 | 重复为错误；未映射允许但不执行并给 warning |
| task 内自 reset、动态建删任务 | 明确不支持 |
| 隐式线程并发、抢占式 VM、wallclock syscall | 明确不支持 |

- 每 configuration RESOURCE ≤16、每 RESOURCE TASK ≤64、每 TASK PROGRAM
  ≤64；事件锁存与诊断全部定长。
- 编译产物报告每 release 最坏指令预算和映像复制字节数；容量 N/N+1 必须
  可独立测试。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L5-A01 | 周期/相位/事件 release | 整数调度 oracle ≥10,000 tick 逐项一致 |
| L5-A02 | 优先级与声明序 | 随机化输入下执行序列确定 |
| L5-A03 | 多 PROGRAM/多任务快照 | 无半写可见；冲突计数与后写胜出一致 |
| L5-A04 | missed release | 不追赶、不堆积，计数精确 |
| L5-A05 | budget/wallclock/resource fault | 输出不提交、隔离范围和诊断全绿 |
| L5-A06 | reset/restart | 保留状态与重新初始化两条恢复路径严格分离 |
| L5-A07 | 运动隔离 | 故障 ST 任务下 RT executor 轨迹消费持续且 TSAN 0 报告 |
| L5-A08 | fuzz/容量 | 配置图 ≥100,000，0 crash/UBSan；N/N+1 边界全绿 |

## 5. 实现与验证证据

- 生产入口：`core/st/tasking.h`、`core/st/configuration_runtime.h`、
  `core/st/process_image.h` 与 `core/st/vm.h`；资源级唯一事务 owner 保证协作式
  非抢占，一个 TASK 的全部 PROGRAM 映射成功后才统一发布。
- 资源映像复用 L3 schema：跨 PROGRAM 的本地 Q/M 同址拒绝；只有同一
  GVL/VAR_EXTERNAL 逻辑对象可合并；I 区允许完全相同及 X 与包含它的
  B/W/D/L 别名。
- `core/test/st_l5_frontend_tests.cpp` 覆盖 grammar、artifact 与 N/N+1；
  `core/test/st_l5_tests.cpp` 覆盖 L5-A01 至 A07，包括随机优先级/声明序 oracle、
  owner 跨边界/reset/restart/resource fault、事务回滚和 RT executor 轨迹消费。
- `core/test/st_fuzz.cpp --l5` 的 Release 固定种子配置图 100,000/100,000 通过；
  Nightly 同一路径在 sanitizer 下运行。WSL Ubuntu TSan 执行 L5 测试为
  `st l5 tests passed`，0 报告。
- Windows Debug L0-L5 联合 CTest 20/20 通过；第三轮独立语义复审 APPROVE。

## 6. 不做

- VM 内抢占、多核并行 ST、OS 线程创建、实时调度策略设置和系统时钟读取不做。
- 安全任务认证、冗余控制器和分布式 resource 不做。

---

*批准：2026-07-17；实现：2026-07-17；复审：APPROVE。*
