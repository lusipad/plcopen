# AxisGroup 第 2 批：joint look-ahead window 拆分计划

> 日期：2026-07-21
> 上位计划：[axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md)
> 范围：只拆 joint look-ahead window；不同时迁移 Cartesian、frame/pose/kinematics 或 Part 4 direct 生命周期。

## 1. 最可能需要调整的决策

### 决策 1：第 2 批必须独立过门，不连续搬完 2-5

- **决定**：本批只处理 joint window，以及已经不可分割的 direct-line bridge、
  stop/interrupt/continue/override 生命周期；第 3-5 批仍留在 `AxisGroup`。
- **信心：高**。
- **什么会推翻它**：只有证明第 2 批无法在不改 public API、错误码和周期输出的
  前提下独立编译，才允许重新合并批次边界。

### 决策 2：状态所有权与代码落点分开，避免引入回调框架

- **决定**：`core/axis/group_window.h` 定义 `GroupLookaheadWindow`，独占
  `WindowNode` / `WindowSegment` / 固定容量窗口及全部 `window_*` 状态；
  `core/axis/group_window_impl.h` 承载原样迁出的 `AxisGroup` 私有 window 方法。
  `AxisGroup` 继续拥有统一 `queue_`、group status/error 和 direct completion 入口。
- **信心：中高**。
- **什么会推翻它**：若编译期可见性迫使新增跨层回调、动态多态或改变头文件消费
  合同，则退回到只迁移状态和纯算法，不为“少几行”制造新协议。

### 决策 3：本批是结构迁移，不顺手修语义

- **决定**：不改 blending、arc、jerk reachability、override、interrupt 或 direct
  的任何算法和状态转换；实现代码逐段原样搬运，只做成员归属的机械改名。
- **信心：高**。
- **什么会推翻它**：focused test 在迁移前已复现缺陷；该缺陷必须另开语义修复批，
  不混入本拆分。

## 2. 假设

- 当前 public 合同由 `AxisGroup` 的现有方法签名、错误码、周期 setpoint 和 replay
  输出共同定义。**信心：高；来源：代码与上位计划。**
- `plcopen_core_a5_lookahead_tests`、`plcopen_core_part4_pathtable_tests` 和
  `plcopen_core_replay_regression` 已覆盖本批最危险的 window、arc、stop、
  interrupt/continue、override 和 path-table 复用路径。**信心：高；来源：测试源码与
  迁移前 3/3 绿色基线。**
- window 的 fixed-capacity 存储允许在 `AxisGroup` 构造时分配一次，但周期路径不得
  分配。**信心：高；来源：当前 `HeapStaticVector` 实现和 RT 合同。**
- 第 3/4 批共享 Cartesian 执行上下文，应紧邻推进；第 5 批应缩窄为 direct
  lifecycle/path materialization。**信心：中高；来源：当前代码依赖图。**

## 3. 偏离策略

- 边界问题优先选择：可逆、最小 diff、最接近“状态集中 + 原样搬运”的方案，并立即
  记录在实施记录中。
- 若出现第 3/4/5 批才能解决的耦合，只保留显式桥接，不提前迁移相邻行为簇。
- 以下情况必须停止本批：需要改变 public API；需要改变错误码、状态机、算法或 replay
  声明；需要新增动态多态/依赖；发现现有计划的行为前提错误。

## 4. 机械工作（低评审价值）

1. 新建 window 状态 owner 与实现头。
2. 将 `AxisGroup` 内 window 类型、状态和私有实现迁出，保留薄声明和统一调度点。
3. 只做必要的成员归属改名，更新拆分计划与实施记录。

## 5. 验证

- 迁移前后 focused tests：A5 look-ahead、Part 4 path table、replay 全绿。
- Windows Debug 构建和全量 CTest 全绿。
- `cmake -P cmake/rt_safety_scan.cmake` 通过，证明周期路径未引入禁用构造。
- `cmake -P cmake/verify_replay_fixtures.cmake` 继续报告 18 fixtures / 2409 samples
  逐位一致。
- `git diff --check` 通过；diff 仅包含本批代码、测试/计划记录，不触碰其它脏工作树。
- 可观察结果：`group.h` 缩小，window 的类型和状态只在新 owner 中定义；所有既有
  `AxisGroup` public API 与运行输出不变。

## 实施交接

实施期间维护
[axis-group-batch2-implementation-notes.md](axis-group-batch2-implementation-notes.md)：
决策、偏离、意外和待评审问题随发生随记录；第三次偏离或任何前提被推翻时，停止搬运
并重新校准计划。
