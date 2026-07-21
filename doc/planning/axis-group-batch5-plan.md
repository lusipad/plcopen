# AxisGroup 第 5 批：direct lifecycle/path 拆分计划

> 日期：2026-07-21
> 上位计划：[axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md)
> 范围：只拆 MoveDirect 专属状态与入口、查询、生命周期实现；共享 queue/status/error、
> 通用 active path、look-ahead window、management、tracking 和 Jog 保持原位。

## 1. 最可能需要调整的决策

### 决策 1：把“path materialization”收窄为 direct 专属生命周期

- **决定**：本批不迁移通用 `active_*` path/profile 状态，也不迁移第 2 批窗口段与
  采样算法；只集中 `direct_command_id_`、完成/中止 id 与 active/stopping 标志。
- **信心：高**。
- **什么会推翻它**：若 direct 专属字段不足以形成单一 owner，则退回只迁方法实现；
  不把共享 path、queue 或 window 状态拖入本批。

### 决策 2：使用非模板、无堆分配的状态 owner

- **决定**：`core/axis/group_direct_path.h` 定义非模板 `GroupDirectPathState`，仅含
  3 个 command id 和 2 个布尔状态，共 5 个字段。
- **信心：高**。
- **什么会推翻它**：若候选状态实际依赖 `MaxAxes` 或独立存储，则缩回 owner 边界；
  不为形式一致引入模板、动态分配或 storage wrapper。

### 决策 3：沿用 AxisGroup out-of-class inline 实现

- **决定**：`core/axis/group_direct_path_impl.h` 承载 13 个既有 direct 方法；方法仍是
  `AxisGroup` 成员，可以直接访问成员轴、queue、status 与窗口，不新增 callback、
  context、virtual interface 或依赖。
- **信心：高**。
- **什么会推翻它**：若迁移要求改变 public API、错误码、`GroupCommand` 或调度顺序，
  退回只迁状态的保守方案。

### 决策 4：完成/中止触发点保持在既有 owner

- **决定**：通用 `cycle()`、`finish_active()`、`abort_motion()` 与第 2 批
  `window_cycle()` 继续决定何时调用 `complete_direct()` / `abort_direct()`；本批只迁
  helper 定义，不搬触发逻辑。
- **信心：高**。
- **什么会推翻它**：无；需要改变触发点、窗口完成语义或 queue 顺序时停止本批。

## 2. 参考语义清单

| 参考行为/结构 | 处理 | 原因 |
|---------------|------|------|
| 第 2/3/4 批 state owner + `AxisGroup` inline impl | keep | 已验证能降低热点且不引入协议 |
| MoveDirect Aborting/Buffered 的成员轴 PTP 语义 | keep | public 行为合同 |
| blending MoveDirect 转为窗口 `direct_line` | keep | 第 2 批窗口与完成 id 合同 |
| direct command done/aborted/active/busy 查询 | keep | FB 生命周期可观察合同 |
| generic `cycle` / `finish_active` / `abort_motion` 调度 | keep | direct 与通用路径、窗口、queue 的唯一协调点 |
| 5 个 direct 专属字段 | adapt | 只增加 `direct_path_.` owner 前缀 |
| 通用 active path/profile、queue/status/error、management | drop | 是共享调度状态，不属于 direct owner |
| window rebuild/sample、tracking、Jog、编号工具 | drop | 已有 owner 或独立行为簇 |
| callback/context、模板参数、heap storage | drop | 当前状态与类外实现不需要这些抽象 |

## 3. 假设

- `AxisGroup` public API、错误码、状态机、逐周期 setpoint 与 replay 是行为合同。
  **信心：高；来源：上位计划与 CLAUDE.md。**
- 迁移前 11 项 focused tests 已覆盖 direct preflight、启动、排队、停止、完成/中止 id、
  FB lifecycle、blending window、management 边界与 replay。**信心：高；来源：测试审计与
  11/11 运行。**
- 5 个候选字段只服务 direct 生命周期，不需要 `MaxAxes`、构造逻辑或动态存储。
  **信心：高；来源：字段读写扫描。**
- 新头由 `group.h` 传递包含并随 `core/axis/*.h` 安装。
  **信心：高；来源：现有第 2/3/4 批安装验证。**

## 4. 偏离策略

- 边界问题优先选可逆、最小 diff、最接近“状态 owner 前缀 + 方法原样搬运”的方案，
  当场写入实施记录。
- direct helper 可以继续访问共享调度状态，但共享状态的所有权和触发逻辑不得迁移。
- 以下情况必须停止：需要改 public API/错误码/replay；需要新增协议、依赖或 heap；
  需要迁移通用 path/profile、queue/status/error、window 算法或 management/tracking/Jog；
  需要改变 direct 完成/中止时机。

## 5. 机械工作（低评审价值）

1. 新建 direct 状态 owner 和实现头。
2. 将 5 个专属字段改为 `direct_path_.*` 单一归属。
3. 将 13 个既有方法体原样迁出，只增加 `inline AxisGroup::` 与 owner 前缀。
4. 保持 `cycle`、window、queue 与通用 path 实现原位，仅改专属状态访问前缀。
5. 删除本批造成的孤立字段，不清理其它既有代码。

## 6. 验证

- 迁移前后同一组 11 项 focused tests 全绿。
- 13 个迁出方法撤销 `direct_path_.*` 和 out-of-class 语法后，规范化逐行比较 0 差异；
  `group_window_impl.h` 除必要调用适配外不得发生算法差异。
- Windows Debug 完整构建与全量 CTest 全绿。
- RT-safety scan、18/2409 replay fixture verifier、`mkdocs build --strict`、
  `git diff --check` 通过。
- 安装产物包含新头；find_package 与 FetchContent consumer 可编译运行。
- 可观察结果：`group.h` 继续缩小；direct 专属状态只有一个 owner；第 2 批窗口、
  通用 path 与管理调度语义不变。

## 实施交接

实施期间维护
[axis-group-batch5-implementation-notes.md](axis-group-batch5-implementation-notes.md)：
决策、偏离、意外和待评审问题随发生随记录；第三次偏离或任何前提被推翻时停止搬运，
重新运行 kickoff。
