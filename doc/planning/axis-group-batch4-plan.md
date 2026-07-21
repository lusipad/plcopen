# AxisGroup 第 4 批：frame/pose/kinematics 拆分计划

> 日期：2026-07-21
> 上位计划：[axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md)
> 范围：只拆 frame/tool/pose/kinematics 状态与转换实现；tracking/Jog、编号工具、
> management queue 和 direct lifecycle 保持原位。

## 1. 最可能需要调整的决策

### 决策 1：使用非模板、无堆分配的状态 owner

- **决定**：`core/axis/group_pose_frames.h` 定义非模板
  `GroupPoseFramesState`，集中 plugin/margin、Cartesian 速度上限、frame/tool 配置
  回显，以及 configured/active/Jog/dynamic tracking 的刚体变换快照，共 20 个字段。
- **信心：高**。
- **什么会推翻它**：若候选字段实际依赖 `MaxAxes`、需要动态存储或独立生命周期，
  则缩回 owner 边界；不为形式统一引入无用模板或 heap wrapper。

### 决策 2：沿用 AxisGroup out-of-class inline 实现

- **决定**：`core/axis/group_pose_frames_impl.h` 承载 28 个既有配置、读回、验证、
  frame collapse 与运动学求解方法；方法仍是 `AxisGroup` 成员，不新增 callback、
  context、virtual interface 或依赖。
- **信心：高**。
- **什么会推翻它**：若迁移要求改变 public API、错误码或 `GroupCommand`，退回只迁
  状态的保守方案。

### 决策 3：共享状态可入 owner，共享状态机不得入本批

- **决定**：`tracking_hold_pose_`、dynamic reference frame 与 active/Jog tool 快照
  是纯 frame 数据，迁入 owner；tracking/Jog 指针、布尔状态、命令 id、求解周期方法
  仍留 `AxisGroup`。
- **信心：中高**。
- **什么会推翻它**：若 nested owner 会迫使 tracking/Jog 生命周期一起迁移，则只保留
  configured/active frame 状态，记录偏离。

### 决策 4：management 与第 5 批边界不动

- **决定**：`submit_kinematics()`、`submit_coordinate_transform()`、
  `apply_selected_tool()`、tracking/Jog 执行、`start()` 和 direct lifecycle 不迁；
  `GroupKinematicsInfo` 继续作为 Part 4 元数据留在 `AxisGroup`。
- **信心：高**。
- **什么会推翻它**：无；需要跨越这些边界时停止本批并重做计划。

## 2. 参考语义清单

| 参考行为/结构 | 处理 | 原因 |
|---------------|------|------|
| 第 2/3 批 state owner + `AxisGroup` inline impl | keep | 已验证能降低热点且不引入协议 |
| configured/active/Jog tool 与 workpiece/dynamic frame 默认值 | keep | 属于既有运行时合同 |
| plugin 指针由 caller 持有、pose/translational 互斥 | keep | public 生命周期和错误码不得变化 |
| `read_cartesian()` / `transform_position()` 的 ACS/MCS/PCS 语义 | keep | 第 4 批核心可观察行为 |
| Cartesian 第 3 批直接读取 frame/plugin 状态 | adapt | 只增加 `pose_frames_.` owner 前缀 |
| management enqueue、tracking/Jog 状态机、编号工具表 | drop | 属于共享调度或其它行为簇 |
| callback/context、模板参数、heap storage | drop | 当前状态不需要这些抽象 |

## 3. 假设

- `AxisGroup` public API、错误码、状态机、逐周期 setpoint 与 replay 是行为合同。
  **信心：高；来源：上位计划与 CLAUDE.md。**
- 迁移前 13 项 focused tests 已覆盖 pose/readback/kinematics/coordinate、Cartesian、
  Part 4 management/path 与 replay。**信心：高；来源：测试审计与 13/13 运行。**
- `RigidTransform{}` 与 `Vec3{}` 默认值可原样放进聚合 owner，不需要构造逻辑。
  **信心：高；来源：`geom/frame.h` / `geom/geometry.h`。**
- 新头由 `group.h` 传递包含并随 `core/axis/*.h` 安装。
  **信心：高；来源：现有 batch2/3 安装验证。**

## 4. 偏离策略

- 边界问题优先选可逆、最小 diff、最接近“状态 owner 前缀 + 方法原样搬运”的方案，
  当场写入实施记录。
- 共享 frame 数据可以留在 owner；共享状态机、queue/lifecycle 和 Part 4 元数据必须
  留在 `AxisGroup`。
- 以下情况必须停止：需要改 public API/错误码/replay；需要新增协议、依赖或 heap；
  需要迁移 tracking/Jog/direct/management 生命周期；需要改 batch3 算法本体。

## 5. 机械工作（低评审价值）

1. 新建 frame/pose 状态 owner 和实现头。
2. 将 20 个纯状态字段改为 `pose_frames_.*` 单一归属。
3. 将 28 个既有方法体原样迁出，只增加 `inline AxisGroup::` 与 owner 前缀。
4. 删除本批造成的孤立 include/字段，不清理其它既有代码。

## 6. 验证

- 迁移前后同一组 13 项 focused tests 全绿。
- 迁出方法撤销 `pose_frames_.*` 和 out-of-class 语法后，规范化逐行比较 0 差异。
- Windows Debug 完整构建与全量 CTest 全绿。
- RT-safety scan、18/2409 replay fixture verifier、`mkdocs build --strict`、
  `git diff --check` 通过。
- 安装产物包含新头；find_package 与 FetchContent consumer 可编译运行。
- 可观察结果：`group.h` 继续缩小；frame/tool/plugin 状态只有一个 owner；第 3 批
  Cartesian 行为与第 5 批 lifecycle 边界均不变。

## 实施交接

实施期间维护
[axis-group-batch4-implementation-notes.md](axis-group-batch4-implementation-notes.md)：
决策、偏离、意外和待评审问题随发生随记录；第三次偏离或任何前提被推翻时停止搬运，
重新运行 kickoff。
