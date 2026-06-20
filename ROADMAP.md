# ROADMAP

本文档描述 plcopen 项目未来 6-12 个月的**可执行路线**。

## 本文档的性质

| 是什么 | 不是什么 |
|--------|----------|
| 近期可信承诺 | 长期愿景（见 `VISION.md`） |
| 每季度复盘一次 | 永久不变的时间表 |
| 具体可交付内容 | 模糊的未来想法 |
| 基于真实产能估算 | 基于理想情况的假设 |

**约束**：本文档列出的任务应当都有明确的 DoD（Definition of Done），并且总工作量不超过**单人兼职 4 小时/晚 × 3-4 晚/周**的产能。

---

## 里程碑基线：v0.2.0（已发布）

**时间窗口**：2026-04 → 2026-07（约 3 个月）

**核心目标**：把 v0.1 的"工程化整理"收尾，让项目从**文档堆**转成**可被别人使用的库**。

**版本定位**：第一个对外可宣布的、可被引用的 release。

**当前状态（2026-04-21 复核）**：`v0.2.0` 作为历史基线已经发布；以下 Sprint 表格主要保留任务拆解与 DoD 记录。

### Sprint 0：文档与定位（2026-04-17 → 2026-05-01，约 2 周）

| # | 任务 | DoD（完成的定义） | 状态 |
|---|------|------------------|------|
| 0.1 | 归档 `.kiro/specs/` / `plan.md` / `MILESTONES.md` | 文件移到 `doc/vision/`，`git log --follow` 可追溯 | ✅ 完成 |
| 0.2 | 写 `VISION.md` | 包含愿景层次、解锁条件、原则、边界 | ✅ 完成 |
| 0.3 | 写 `ROADMAP.md`（本文） | 明确 v0.2.0 具体任务和 DoD | ✅ 完成 |
| 0.4 | 写 `doc/vision/README.md` 归档前言 | 说明归档文档的性质，避免被当路线图 | ✅ 完成 |
| 0.5 | 重写根目录 `README.md` | 首屏有一句话定位 + 对比表 + 快速开始 | ✅ 完成 |
| 0.6 | 命名统一（`plcopen` 为主） | 仓库名、CMake project、package target、public namespace、内部 include guard / legacy macro 统一到 `plcopen` / `PLCOPEN_*` | ✅ 完成 |
| 0.7 | Commit Sprint 0 所有变更 | 一次清晰的 commit，message 说清搬家意图 | ✅ 完成 |

### Sprint 1：测试与 CI（2026-05 → 2026-06，约 4 周）

| # | 任务 | DoD |
|---|------|-----|
| 1.1 | 接入 Catch2 或 GoogleTest | CMakeLists 集成，替换手写的 `SimpleTest` 类 |
| 1.2 | 写 `AxisBase` 状态机测试 | 覆盖 PLCopen 8 个状态的合法/非法转换 |
| 1.3 | 写 `ProfilePlanner` 数值边界测试 | 覆盖：0 速度、极短距离、超速、超加速 |
| 1.4 | 写 `FbSingleAxis` 功能块集成测试 | MoveAbsolute / Relative / Velocity / Stop / Halt |
| 1.5 | 测试覆盖率工具（gcov / llvm-cov） | 生成报告，目标 >50% |
| 1.6 | GitHub Actions CI（Windows） | push/PR 自动构建 + 跑测试 |
| 1.7 | GitHub Actions CI（Linux） | gcc + clang 两种编译器 |
| 1.8 | README 加 CI badge | 显示构建状态 |

**诚实的覆盖率目标**：v0.2.0 追求 >50%。**不追求 90%**（那是空话）。重点覆盖状态机和算法边界，demo 和工具代码暂不覆盖。

### Sprint 2：对外可用（2026-06 → 2026-07，约 4 周）

| # | 任务 | DoD |
|---|------|-----|
| 2.1 | Linux 构建脚本（`build.sh`） | Ubuntu 22.04 一键构建通过 |
| 2.2 | Linux 构建文档（`BUILD_LINUX.md`） | apt 依赖、gcc 版本要求 |
| 2.3 | CMake `install(EXPORT)` 配置 | `make install` 产出 `plcopenConfig.cmake` |
| 2.4 | `find_package(plcopen)` 可用 | 独立测试工程能引用 |
| 2.5 | `FetchContent` 兼容 | 在另一个仓库用 `FetchContent_Declare` 能拉 |
| 2.6 | 独立 `plcopen-examples` 仓库 | 至少 2 个 example：单轴点到点、单轴速度 |
| 2.7 | `CHANGELOG.md` | 记录 v0.1 → v0.2 的变化 |
| 2.8 | v0.2.0 tag + GitHub Release | Release notes + 二进制（可选） |
| 2.9 | 发社区（r/PLC / 论坛） | 至少 1 个发帖 |

---

## 已完成工作集：v0.3.0（已在 v0.3.30 落地）

**预估窗口**：2026-Q4（v0.2.0 发布后 3-6 个月）

**2026-04-18 更新**：其中仓库内可执行的 `P0/P1/P2` 条目已在当前 `v0.3.30` 工作集完成并通过验证；`用户反馈修复` 继续按 issue 驱动。

**候选目标**（按优先级；已完成项保留作记录）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| MC_Home 当前支持模式收口 | 已在 `v0.3.30` 工作集补齐并通过回归验证 | 保持回归覆盖 | P0（已完成） |
| Buffer mode 当前实现边界测试 | 已在 `v0.3.30` 工作集补齐回归测试 | 细粒度 blending 另立项 | P0（已完成） |
| 用户反馈修复 | v0.2 后收集的 issue | 有 issue 即做 | P0 |
| S 曲线（Jerk 受限）规划 | 已在单轴 `AxisMove` 路径落地，并补齐回归验证 | 扩展到 homing/look-ahead 另立项 | P1（已完成） |
| API 文档（Doxygen） | 已提供可选 `docs` target，缺少 Doxygen 时优雅降级 | 持续补公开头文件注释 | P1（已完成） |
| 更多 example（凸轮/齿轮/同步） | 已补概念级双轴 demo，用于展示同步思路 | 正式 PLCopen 多轴 FB 另立项 | P2（已完成） |
| Python 绑定（pybind11） | 已提供单轴仿真 facade 与 smoke test | 扩展 API 面另立项 | P2（已完成） |

**硬承诺**：P0 项目。
**软承诺**：P1 项目（视 v0.2 发布后的反馈）。
**机会主义**：P2 项目（有额外精力时做）。

---

## 已完成工作集：v0.4.0（仓库内可执行项已完成）

**预估窗口**：2026-Q2 → 2026-Q3

**2026-04-18 更新**：除继续按 issue 驱动的 `用户反馈修复` 外，当前仓库内可执行的 `v0.4.0` 条目已完成并通过本地验证闭环。

**候选目标**（按优先级）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| 用户反馈修复 | 继续响应 `v0.3.x` 使用反馈 | 有 issue 即做 | P0（按 issue 驱动，当前无仓库内证据） |
| `MC_Home` jerk-aware 规划 | 已让 homing 对 `mHomingJerk` 配置产生真实行为，并补齐回归 | `ProfilePlanner` jerk 路径已落地 | P0（已完成） |
| API 文档补注释 | 已补齐 `docs` 入口公开头文件的关键注释 | Doxygen 入口已存在 | P1（已完成） |
| Buffer mode / blending 边界固化 | 已显式收口为“`ABORTING` 打断，其余已定义值排队；未定义值报错” | 细粒度 blending 另立项 | P1（已完成） |
| `pyplcopen` 表面积扩展 | 已补单轴速度运动、halt/stop 与加速度读取等最小有用 API | 有真实使用场景或 issue | P2（已完成） |

**原则**：`v0.4.0` 继续优先做 motion core 的真实能力与边界收紧，不把正式多轴 FB、ST/IDE、通信协议等长期愿景项塞进当前里程碑。

**当前执行状态（2026-04-21 复核）**：截至目前，`ROADMAP.md` 中仓库内已定义且可直接落地的里程碑工作已经完成；在下一次季度复盘前，默认只继续 issue-driven 修复与文档/测试补充，不预写 `v0.5.0` 范围。

---

## 已完成工作集：v0.5.0（基础 IEC 功能块首批已落地）

**预估窗口**：2026-Q3

**2026-04-21 更新**：在保持“可嵌入库”定位不变的前提下，项目完成了 Layer 1 的首批基础 IEC 功能块交付，补齐了边沿检测、双稳态、定时器与计数器的最小闭环。

**候选目标**（按优先级；已完成项保留作记录）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| 基础 IEC 边沿检测 | 为计数器、时序控制和更高层逻辑提供最小基础块 | `FunctionBlock`/`PLCTypes` 已具备 | P0（已完成） |
| 基础 IEC 双稳态 | 为状态保持和互锁逻辑提供标准块 | 与边沿检测一起落地 | P0（已完成） |
| 基础 IEC 定时器 | 让库具备非 motion 场景下最基础的时序控制能力 | 明确采用显式周期推进 | P0（已完成） |
| 基础 IEC 计数器 | 让库具备 scan-cycle 下的基本计数控制能力 | 边沿语义先落稳 | P0（已完成） |
| 基础块安装导出与示例 | 让下游能够实际使用这批基础块，而不是只停在仓库内 | 公开头文件与 demo 一并落地 | P1（已完成） |

**原则**：`v0.5.0` 只补 Layer 1 的第一批核心块，不顺手扩到完整运算块库、ST 运行时或正式多轴 FB。

**当前执行状态（2026-04-21 复核）**：截至目前，仓库内已定义且可直接落地的 `v0.5.0` 工作已完成；后续默认继续 issue-driven 修复、Layer 1 扩面候选评估与下一次路线复盘。

---

## 已完成工作集：v0.6.0（当前公开 FunctionBlock 面已补齐）

**预估窗口**：2026-Q3 → 2026-Q4

**2026-04-22 更新**：在不引入 ST/IDE/通信层的前提下，仓库把当前公开支持面中剩余的 FunctionBlock 补齐到了“代码、测试、文档口径一致”的状态。

**候选目标**（按优先级；已完成项保留作记录）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| 单轴管理块收口 | 补齐 `ReadParameter/SetPosition/SetOverride` 等仍缺的公开块 | 复用现有 AxisBase 原语 | P0（已完成） |
| 单轴高级运动块收口 | 补齐 `MoveSuperimposed/TorqueControl` 的当前仓库语义 | 对现有单轴栈做显式边界定义 | P0（已完成） |
| 多轴同步块收口 | 把 `Gear/Cam` 从 demo 概念推进到正式 FunctionBlock 入口 | 新增最小同步 runtime | P1（已完成） |
| 公开支持表闭环 | 让 README/CHANGELOG 与实际代码一致 | 全量回归通过 | P1（已完成） |

**原则**：`v0.6.0` 追求的是“补齐当前公开 FunctionBlock 面”，不是引入完整 PLC 运行时、真实 AxesGroup 框架或工业通信层。

**当前执行状态（2026-04-22 复核）**：截至目前，当前仓库公开面中的 FunctionBlock 已全部落地；后续若继续扩展，应进入新的里程碑定义，而不是在当前版本上继续隐式加范围。

---

## 已完成工作集：v0.7.0（IEC 61131-3 标准 FunctionBlock 口径补齐）

**预估窗口**：2026-Q4

**2026-04-22 更新**：在不把标准函数库、ST/IDE 或通信层拉入当前范围的前提下，项目补齐了当前采用口径下剩余的 IEC 61131-3 标准 FunctionBlock，并继续保持“显式 scan-cycle 推进”的执行契约。

**候选目标**（按优先级；已完成项保留作记录）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| `RTC` 标准块补齐 | 收口当前基础 IEC 标准 FunctionBlock 集 | 复用 `PLCTypes` 中已有 `DT` 类型别名 | P0（已完成） |
| 时间相关边界测试 | 防止启停、重启与时间上界行为回归 | 沿用 Catch2 基础块测试组织方式 | P0（已完成） |
| 公开支持表闭环 | 让 README/CHANGELOG 与实现口径一致 | 全量回归通过 | P1（已完成） |

**原则**：`v0.7.0` 只补当前采用口径下的 IEC 61131-3 标准 FunctionBlock，不把选择/比较/算术等标准函数库扩进当前里程碑。

**当前执行状态（2026-04-22 复核）**：截至目前，当前仓库采用口径下的 IEC 61131-3 标准 FunctionBlock 已补齐；若继续扩展 IEC 61131-3，下一步应显式定义“标准函数库”里程碑，而不是混入本版本。

---

## 已完成工作集：v0.8.0（AxesGroup Foundation）

**预估窗口**：2026-Q2

**2026-05-01 更新**：`AxesGroup Foundation` 已作为当前里程碑检查点提交。这一阶段对应 PLCopen Part 4 coordinated motion 的基础层，不追求完整的 coordinated motion、kinematics 或线性插补。

**当前范围**：

- `AxesGroup` runtime
- `MC_AddAxisToGroup`
- `MC_RemoveAxisFromGroup`
- `MC_GroupEnable`
- `MC_GroupDisable`
- `MC_GroupReadStatus`
- `MC_GroupReadActualPosition`
- `MC_GroupReadCommandPosition`
- `MC_GroupStop`
- `MC_GroupReset`
- `MC_CombineAxes`
- `MC_Gear* / MC_Cam*` 的 group-aware 前置条件
- README / CHANGELOG / 安装导出闭环

**明确延后**：

- `MC_GroupHome`
- `MC_MoveLinear*`
- kinematics / coordinate transforms
- multi-master / multi-slave coordinated runtime

**原则**：`v0.8.0` 只把 group 做成真实存在、可测试、可公开引用的运行时边界，不把 Part 4 的完整路径规划和坐标变换偷渡进来。

---

## 已完成工作集：v0.9.0（Part 1/2 Completion）

**预估窗口**：2026-Q2 → 2026-Q3

**2026-05-03 更新**：`v0.9.0` 已作为 Part 1/2 Completion 检查点收口。详细执行计划见 [v0.9.0 Part 1/2 Completion Plan](doc/compliance/part1-part2-completion-plan.md)。

**当前基线**：

- Part 1/2 标准 FB：45 个 tracked rows。
- 当前 FB 状态：45 个 `implemented`，0 个 `partial`，0 个 `missing`；剩余限制均转入明确的 runtime boundary / out-of-scope 说明。
- Profile FB 已补链式多段、timed segment、scale/offset 归一化和 active update 回归；外部 profile-table import/parser 仍是未来 runtime goal。
- 跨切语义：Execute/Done/Busy/Error、Enable/Valid/Error、当前公开错误码覆盖已归为 implemented；BufferMode 与当前公开 ContinuousUpdate 合同已归为 implemented，未来几何 blending、外部 profile-table import/parser、未建模同步 planner 变体另列新 runtime feature。

**候选目标**（按优先级）：

| 候选 | 动机 | 解锁条件 | 优先级 |
|------|------|----------|--------|
| Compliance matrix 正规化 | 把每个 `partial` 拆成可执行缺口，而不是口头承诺“全支持” | 当前矩阵已经列出全部 partial 原因 | P0（已完成） |
| 跨切 FB contract 收口 | Execute/Enable 生命周期、错误码和清除语义会影响所有后续实现 | 代表性 FB 回归已锁住当前公开行为 | P0（已完成） |
| 管理类单轴 FB 完成 | 参数、IO、AxisInfo、TouchProbe 等是 Part 1/2 完整口径的基础面 | 保持硬件相关能力诚实建模 | P0（已完成/边界明确） |
| 单轴 motion partial 收口 | Home、BufferMode、profile、superimposed、torque 是当前最大缺口 | shared planner/contract 行为已覆盖，剩余为显式范围边界 | P1（已完成/边界明确） |
| 多轴同步 FB 收口 | Cam/Gear/Phasing/CombineAxes 需要在 Part 1/2 与 Part 4 边界上写清楚 | 不把 coordinated path planning 偷渡进来 | P1（已完成/边界明确） |
| 文档与发布门禁 | 对外 claim 必须可审计 | 全量 build/test/consumer/docs smoke 通过 | P1（已完成） |

**原则**：`v0.9.0` 的目标是“Part 1/2 partial 清零或显式重分类”，不是“扩展到完整 Part 4 coordinated motion”。硬件 latch 等无法由当前 runtime 验证的能力，要新增窄抽象或诚实列为范围外，不能用模拟假设冒充已实现。

---

## 当前发布里程碑：v0.10.0（发布与采用收口）

**时间窗口**：2026-Q2

**发布日期**：2026-06-20

**核心目标**：不扩展运行时能力，把 `v0.9.0` tag 之后已经完成的 Part 1/2 语义收口和发布门禁修正，整理成可重复验证、可被下游消费的正式版本。

**完成定义**：

- Windows 与 Linux GCC/Clang 从全新 checkout 完成构建和全量 CTest。
- Windows 覆盖率报告可重复生成并保持至少 50% 的门槛。
- CI 实际运行安装后的 `find_package` consumer 与源码树 `FetchContent` consumer。
- Linux GCC 通道实际运行 Python binding smoke，并用 Doxygen 生成 API 文档。
- CMake、`.version`、README、CHANGELOG、ROADMAP、VISION、tag 和 GitHub Release 的版本口径一致。
- 只发布源码与 Release Notes；`v1.0` 前不承诺平台二进制 ABI。

**发布后节奏**：进入 30 天反馈观察期，优先处理真实集成问题。下一功能里程碑由 Issues、Discussions 和实际项目需求决定；没有新证据时不启动 Part 4、IEC 或平台化扩面。

---

## 每季度复盘（每季度第 1 周）

**固定 4 个问题**：

1. 上季度计划的任务完成了多少？未完成的原因？
2. 有没有用户反馈或 issue 改变了优先级？
3. 解锁条件表（VISION.md）里有没有能力应该启动？
4. 现在的路线图还指向 VISION 吗？还是漂了？

**复盘产出**：更新本 ROADMAP 的"当前里程碑"和"下一里程碑"段落。

**不做的事**：
- 每季度写全新的大规划文档（会变成纸上富贵）
- 把 VISION 的内容复制到 ROADMAP（职责分开）

---

## 当前**不在**路线图上的能力

以下能力**不会**因为 `v0.10.0` 发布而自动进入下一里程碑，除非解锁条件满足：

- ST / IL / LD / FBD / SFC 编译器 & 编辑器
- Electron IDE
- Modbus / OPC UA / EtherNet/IP 通信
- Web HMI
- 冗余 / SIL 安全
- RT-PREEMPT 实时调度
- 云原生 / Kubernetes / 分布式 PLC

**为什么明确写出来**：避免让贡献者或潜在用户误以为这些"在计划中"。它们在 VISION 里，不在 ROADMAP 里。这两个位置有本质区别。

---

## 贡献者如何参与

- **要做当前路线图中的 issue-driven 修复或文档/测试补充**：先看 [ROADMAP.md](ROADMAP.md) 当前状态，再发 issue / PR 说明范围
- **要做下一阶段候选**：先开 issue 讨论，确认优先级后再动手
- **要做"不在路线图"的能力**：先看 VISION.md 解锁条件，满足后开 issue 讨论
- **文档改进 / 测试补充**：任何时候都欢迎，不需要预先讨论

---

## 时间估算的说明

本 ROADMAP 的时间窗口基于以下假设：

- 单人兼职投入：**约 10-15 小时/周**
- 包含：代码、测试、文档、code review、issue 回复
- 不包含：调研新方向、学习新技术、个人事务

**如果投入时间显著变化**（比如转为全职投入，或完全没空），下次复盘时调整窗口。

**如果任务实际耗时超过估计 50%**：不加班，而是延后到下一个里程碑。**加班赶路线图是反模式**。

---

*本文档最后更新：2026-05-03*
*下次复盘：2026-07-01（确定下一里程碑范围）*
