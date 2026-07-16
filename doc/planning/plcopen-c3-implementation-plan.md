# PLCopen C3 — Part 4 剩余 11 个门面实施计划

> 状态：**已完成（2026-07-16，KB-078）**。实现证据：
> `plcopen_core_part4_c3_{tests,fuzz}`、Windows/Linux 69/69、ARM64/QEMU
> 62/62、18 回放零差异、clang-tidy 0 error-level、覆盖率硬门通过。
>
> 计划日期：2026-07-16。上游收束计划见
> [plcopen-beckhoff-parity-closure.md](plcopen-beckhoff-parity-closure.md)。
> 事实源为 PLCopen Motion Control Part 4 v2.0 Published（2026-05）与
> [Part 4 条款审计](../compliance/plcopen-part4-clause-audit.md)。

## 1. 最可能调整的决策

### 决策 1：按风险分三批，不按条款编号机械实现

| 批次 | 同名门面 | 出口条件 |
|---|---|---|
| C3a 管理/回读 | `FbUngroupAllAxes`、`FbGroupPower`、`FbReadKinTransform`、`FbReadCoordinateTransform`、`FbGroupReadError`、`FbGroupTransformPosition` | 只读或原子管理路径闭合；不引入运动队列新状态 |
| C3b 坐标写入 | `FbSetCartesianTransform`、`FbSetCoordinateTransform`、`FbGroupSetPosition` | 坐标引用、相对/绝对、非法状态和原子性闭合 |
| C3c 运动生命周期 | `FbGroupHalt`、`FbGroupWaitTime` | 独立命令 ID、排队/接管、Done/Busy/Active/Aborted/Error 闭合 |

**Confidence: high。** 这与现有 `AxisGroup` 的管理、配置、运动三类所有权边界一致。

**What would flip it:** 如果 C3a 的回读需要先引入新的运行期 transform 注册表，
则把对应 Set/Read 成对移动到 C3b，但不把 Halt/Wait 提前混入。

### 决策 2：vendor-specific 引用适配现有 C++ 类型，不虚构通用引擎

- `MC_KIN_REF` 适配为现有 `kin::Kinematics` / `kin::PoseKinematics` 只读引用；
- `MC_COORD_REF` 在本批适配为现有固定 6D RPY 变换数据；
- `MC_SetCoordinateTransform` 与 Cartesian 路径共用真实 PCS/MCS 帧消费链，
  非 Cartesian（如 cylindrical）返回 `unsupported`；
- `MC_GroupTransformPosition` 只承诺 ACS/MCS/PCS，WCS/FCS/TCS 和配置分支
  返回显式错误。

**Confidence: high。** PLCopen 明确把两种 REF 的实际数据类型留给 vendor；仓库已有
稳定的插件与 6D 刚体变换事实源。

**What would flip it:** 只有项目决定在默认 core 中加入可注册的非 Cartesian
坐标变换插件时，才扩展 `MC_COORD_REF`，不在 C3 为单一入口预建抽象层。

### 决策 3：门面齐备不等于语义冒充

- `GroupPower` 采用 Enable 电平控制所有真实成员，任一成员失败则 Error；现有
  `FbPower` 没有命令源所有权，因此“禁止 MC_Power 与 GroupPower 同时驱动”登记为
  部分覆盖，不伪造检测结果；
- `GroupSetPosition` 先交付 Standby 下的原子 ACS/MCS/PCS absolute/relative；
  moving 中不改变轨迹的重参考与 `mcQueued` 留作显式 `unsupported`；
- `SetCartesianTransform` 支持 Standby immediate；queued execution 与动态 PCS
  分别由后续队列扩展和已有 tracking FB 承接；
- `ReadError` 返回真实组级故障锁存，而不是把 FB 自身参数错误混作 GroupErrorID。

**Confidence: medium。** 这是当前架构下最小且诚实的 68/68 门面路径。

**What would flip it:** 若验收口径改为“每个 E 级分支也必须与 Beckhoff 对等”，
则 C3 不能以 68/68 为出口，必须先增加 execution-mode 队列和 power-owner 模型。

### 决策 4：Halt 与 Wait 是真实命令，不复用 Stop 或睡眠

- `GroupHalt` 使用独立 command ID 和可被新运动 Aborting 接管的受控减速生命周期；
  它保持原路径，零速后回 Standby（动态 PCS 时保留对应运动状态语义）；
- `GroupWaitTime` 使用 `std::int64_t duration_cycles`，进入固定容量组命令队列；
  Aborting 先受控到零再计时，Buffered 等待前序完成，blending 模式显式拒绝；
- 周期路径不使用 wall clock、阻塞、线程或堆分配。

**Confidence: high。** 官方 Notes 明确区分 Halt 与 Stop，并规定 Wait 的计时起点和
不支持 blending。

**What would flip it:** 如果项目公共时间基准从 cycle domain 迁移为显式周期时长，
Duration 的外部类型可以调整，但内部仍以整数周期执行。

## 2. 参考语义 keep / adapt / drop

| PLCopen v2.0 行为 | 计划 | 理由 |
|---|---|---|
| UngroupAll 仅在 Disabled/Standby/ErrorStop，完成后 Disabled | keep | 与现有成员管理和组状态可直接一致 |
| GroupPower 为 Enable 型持续控制，掉电转 ErrorStop | keep | Beckhoff 级基本组电源能力 |
| MC_Power 与 GroupPower 同时使用需仲裁 | adapt | 当前 core 无命令源身份；先如实登记部分覆盖 |
| Kin/Coord Transform 引用类型 vendor-specific | adapt | 复用现有插件引用和固定 6D RPY 数据 |
| SetCoordinate 支持任意非 Cartesian 变换 | drop（本批） | 默认 core 没有该类变换引擎，静默当 Cartesian 会误导 |
| GroupSetPosition 不影响运动 | adapt | 本批只在 Standby 原子重参考；moving/queued 显式不支持 |
| Halt 保持原路径且可被新运动中止 | keep | 不能用不可接管的 GroupStop 冒充 |
| Wait 的 Aborting 在速度到零后计时，Buffered 排队 | keep | 是 Wait 的核心可观察语义 |
| Wait blending in/out | drop | 官方明确不支持 |
| TransformPosition 持续 Enable 计算并报告 singularity | keep | 复用现有正逆解和 gimbal/singularity 信号 |

## 3. 假设

- **高置信，代码事实：** `AxisGroup` 是组成员 setpoint 的唯一写者；新增路径不开放
  第二个公共 setpoint 写入口。
- **高置信，代码事实：** 固定容量 `GroupPosition`、插件引用、workpiece frame、
  command ID 和队列可承载 C3，不需要新依赖。
- **高置信，官方事实：** Part 4 v2.0 当前共有 68 个 FB；C3 前为 57/68。
- **中置信，项目决策：** C3 的“68/68”表示同名 C++ 门面存在并有已验证子集，
  不表示 B/E/O 全支持、PLCopen 认证或 Beckhoff 黑盒性能相等。

## 4. 偏差政策

遇到边界时选择可逆、最小影响、显式报错的方案，并立即记录到
[plcopen-c3-implementation-notes.md](plcopen-c3-implementation-notes.md)：

- 非 Cartesian、未定义 ExecutionMode、缺少配置分支 → `unsupported`；
- 多成员写入先全量校验，失败时保持原状态；
- 周期执行需要新增容量时，先使用现有固定上限，不引入堆分配；
- 同一批出现第三个偏差，或发现现有命令 ID/队列无法表达 Halt/Wait，停止补丁并
  重新执行 kickoff。

仅以下情况停止询问：需要破坏公开 ABI、改变既有轨迹回放、引入新依赖，或必须
扩展到 EtherCAT/真机安全语义。普通接口子集和显式错误按上述政策继续。

## 5. 机械工作（低评审价值）

- 在现有 `core/fb/group.h`、`management.h`、`path_table.h` 按职责放置门面；
- 在 `AxisGroup` 增加最小管理、变换、Halt/Wait 状态和查询；
- CMake 注册 C3 tests/fuzz；更新 README、STATUS、ROADMAP、CHANGELOG、68 项审计；
- 修正条款审计 §6 的旧“18 项”标题和已关闭行，不改历史 KB 数字。

## 6. 验证

每批至少证明：

1. 默认构造和 B 级字段面存在；Enable/Execute 下降沿清理正确；
2. 正常、非法参数、前置状态、接管/中止四类合同；
3. 多成员电源/位置/解组原子性，变换往返与 singularity 可观察；
4. Halt 在原路径受控到零且可被新运动中止；Wait 的 Aborting/Buffered 起点和
   CommandInfo 剩余周期正确；
5. 适合随机化的变换、Duration、状态组合做确定性 fuzz；
6. 分配守卫、18 份回放、Windows 全测、Linux GCC/Clang、ARM64/QEMU、
   clang-tidy、RT scan、coverage 和矩阵门全部通过；
7. 出口口径更新为 68/68 同名门面，同时逐项保留 partial/unsupported 边界。

## Handoff

按 C3a → C3b → C3c 顺序执行。实现过程中逐条更新 implementation notes 的
Decisions / Deviations / Surprises / Questions for review；不得在收尾时一次性补写。
