# Implementation notes — PLCopen C4 / Part 1/2 语义清零

Plan: [plcopen-c4-implementation-plan.md](plcopen-c4-implementation-plan.md)

## Decisions

- 维护者“把这些全部都做完”作为 C4 全范围和声明变更批准记录；实施仍按
  C4a～C4d 可独立验证切片推进，最终不保留开放 D 项。
- 所有复核测试只走公开 FB/AxisModel seam；不读取私有队列或注入内部状态。

## Deviations

- D-18 在进入本批前已由 `cycle_superimposed()` 自然满足，本批只补公共
  API 回归证据，没有为“关闭编号”重写生产路径。
- D-03 使用独立 `set_power_feedback()` 表达外部功率反馈；正常
  `set_power(false)` 仍是无故障 Disable，避免把用户停机误报为轴故障。
- D-10 复用 AxisModel 既有固定队列槽保存加速度段，没有引入第二份段数组
  或动态容器；周期积分保持 O(1)。
- D-04 的停止锁只由 `FbStop` 提交的 `lock_stopping` 命令启用；内部原始
  Stop/Halt 路径保持原合同，避免无 FB Execute 电平时形成不可释放锁。
- 本批改变 FB 门面的终态与持续输出，但 18 份既有回放场景没有覆盖这些
  门面差异，逐周期黄金数据保持零差异，无需重录。

## Surprises

- 初始代码复核发现 D-18 的 Standstill → DiscreteMotion → Standstill 已在
  `AxisModel::cycle_superimposed()` 实现；专项回归确认后只更新条款证据。
- 首次全量 CTest 中分配守卫出现一次非稳定崩溃；相同二进制在
  16000～20100 周期逐点复测、单测复测和重新生成后的全量测试均通过，
  未形成可重复缺陷或代码修补。
- 最终代码复核发现 Continuous 门面仍以 `Done && Busy` 兼容旧行为，违反
  C4 的持续状态分离目标；已改为只使用 `InEndVelocity`，并补互斥回归。
- override 边缘复核发现 InVelocity/InEndVelocity 应比较“已接受命令经当前
  override 缩放后的轴内 setpoint”，已收回 AxisModel 统一判定，避免输入
  后改和 override 共同造成假阴性。
- moving SetPosition 对无位置目标的 velocity/torque/acceleration owner 不再
  校验陈旧 `active_target_`，只平移公开坐标并保持物理速度。
- 远端 Windows runner 升级到 VS Coverage 18.7 后，动态插桩会令零分配
  守卫进程返回非零；该守卫与延迟基准同属插桩会改变被测量对象的预算门，
  已从 coverage 重跑排除，仍由前序未插桩 CTest 强制执行。

## Verification

- Windows Debug：70/70 CTest；
- Linux GCC/Clang Release：各 70/70 CTest；
- ARM64 交叉编译 + QEMU：63/63（排除 benchmark/jitter）；
- Windows line coverage：89.36%（36270/40588，阈值 50%）；
- clang-tidy error-level gate：全量通过；
- RT scan 27 files、Part 1/2 49 rows、Part 1 I/O 43 FB/236 B/302 E；
- 18 份 replay fixtures / 2409 samples 与 replay regression 零差异；
- `mkdocs build --strict`、`git diff --check` 通过。

## Questions for review

- 无。C4 的 modulo、多插值 Profile、真实 torque feedback、BufferMode
  3/4/6 与官方认证边界均保持显式，不作为 D 项关闭的一部分。
