# AxisGroup 第 3 批：Cartesian path/window 拆分计划

> 日期：2026-07-21
> 上位计划：[axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md)
> 范围：只拆 Cartesian path/window；frame/tool/pose/kinematics 状态和 Jog 保持原位。

## 1. 最可能需要调整的决策

### 决策 1：沿用“状态 owner + AxisGroup 成员实现头”

- **决定**：`core/axis/group_cartesian.h` 定义 `CartesianSegment` 和
  `GroupCartesianState<MaxAxes>`；`core/axis/group_cartesian_impl.h` 承载既有
  `AxisGroup` Cartesian 提交、窗口、预校验和周期执行方法。
- **信心：高**。
- **什么会推翻它**：若迁移需要新增 callback/context interface、动态多态或改变
  public API，则退回到只迁方法的保守方案。

### 决策 2：owner 只接收纯 Cartesian 运行时状态

- **决定**：迁入 `CartPiece`、窗口固定容量存储、`active_cart_`、IK seed、halt
  profile、窗口 tick/index/entry dynamics/error/id/active 状态。
- **保留在 AxisGroup**：`cart_tail_joints_` 与 `jog_cartesian_start_` 共用 union，
  不拆 overlay；`cartesian_velocity_limit_`、frame/tool/pose/kinematics plugin 与
  margin 属于下一批上下文。
- **信心：高**。
- **什么会推翻它**：如果 owner 无法在不复制 frame/Jog 状态的前提下独立构造，
  停止并重校准边界，不扩大本批。

### 决策 3：只迁 Cartesian 方法，不迁 frame 转换算法

- **决定**：迁出 `submit_cartesian_window()` 到 `cartesian_cycle()` 的 Cartesian
  path/window、prevalidation/execution 方法；`cartesian_part()`、
  `select_orientation_interpolation()`、`apply_coordinate_frame()` 和
  `solve_cartesian_target()` 暂留 `AxisGroup`，由第 4 批处理 frame/pose 边界。
- **信心：中高**。
- **什么会推翻它**：仅当编译可见性证明某个小型纯函数必须随调用者迁移，才允许
  做单点调整，并记录实施偏离。

### 决策 4：结构迁移不补造新语义

- **决定**：不新增 Cartesian window continue/override 支持；当前合同仍是
  `interrupt()` 对 Cartesian window 返回 `unsupported`，stop 保持受控减速。
- **信心：高**。
- **什么会推翻它**：迁移前测试已经复现语义缺陷；该缺陷必须另开语义批。

## 2. 参考语义清单

| 参考行为/结构 | 处理 | 原因 |
|---------------|------|------|
| 第 2 批的 state owner + out-of-class inline 实现 | keep | 不引入新协议即可削减热点 |
| `CartesianSegment` 的字段、默认值和 `GroupCommand::cart` 形状 | keep | 属于既有 public 编译合同 |
| Cartesian line/arc/blend/window/stop/error/replay 输出 | keep | 本批未声明语义变更 |
| frame/tool/pose/kinematics 只读访问 | adapt | 方法仍是 `AxisGroup` 成员，状态留给第 4 批 |
| `cart_tail_joints_` 与 Jog scratch overlay | keep in AxisGroup | 避免扩大对象或把 Jog 偷渡进本批 |
| standalone helper callbacks/context object | drop | 会把结构迁移变成新接口设计 |

## 3. 假设

- `AxisGroup` public API、错误码、逐周期 setpoint 与 replay 是本批行为合同。
  **信心：高；来源：代码与上位计划。**
- 迁移前 9 项基线已覆盖 Cartesian line/arc/blend/window/stop/fault、pose/readback/
  kinematics、Part 4 路径表与 replay。**信心：高；来源：测试审计和 9/9 运行。**
- 现有测试未给 Cartesian window 定义 continue/override 正向行为；不得借本批补齐。
  **信心：高；来源：`cartesian_tests.cpp` 与 A5 测试对照。**
- 新头仍由 `group.h` 传递包含并随 `core/axis/*.h` 安装。**信心：高；来源：
  `core/CMakeLists.txt` 的目录安装规则。**

## 4. 偏离策略

- 边界问题优先选可逆、最小 diff、最接近“成员归属改名 + 方法原样搬运”的方案，
  当场记入实施记录。
- 发现共享状态时留在 `AxisGroup`，不为追求 owner 字段数破坏 overlay 或提前做第 4 批。
- 以下情况必须停止：public API/错误码/状态机/replay 需要改变；需要新 callback、动态
  多态或依赖；需要迁入 Jog 或 frame/pose/kinematics 所有权；计划前提被推翻。

## 5. 机械工作（低评审价值）

1. 新建 Cartesian 类型/状态 owner 和实现头。
2. 将纯 Cartesian 状态改为 `cartesian_.*` 单一归属，保留共享 scratch/context。
3. 将既有方法体原样迁出，只增加 `inline AxisGroup::` 与成员归属改名。
4. 删除本批造成的孤立 `HeapStaticVector`，不清理任何其它既有代码。

## 6. 验证

- 迁移前后同一组 9 项 focused tests 全绿。
- 对迁出方法做规范化逐行比较，撤销 `cartesian_.*` 和 out-of-class 语法后 0 差异。
- Windows Debug 完整构建与全量 CTest 全绿。
- RT-safety scan、18/2409 replay fixture verifier、`mkdocs build --strict`、
  `git diff --check` 通过。
- 安装产物包含 `group_cartesian.h`、`group_cartesian_impl.h`；find_package consumer
  可编译运行。
- 可观察结果：`group.h` 继续缩小；Cartesian 运行时状态只有一个 owner；frame/Jog
  边界未移动；所有公开行为不变。

## 实施交接

实施期间维护
[axis-group-batch3-implementation-notes.md](axis-group-batch3-implementation-notes.md)：
决策、偏离、意外和待评审问题随发生随记录；第三次偏离或任何前提被推翻时停止搬运，
重新运行 kickoff。
