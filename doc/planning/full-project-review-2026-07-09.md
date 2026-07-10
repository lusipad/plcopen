# 全项目 Review 报告（2026-07-09）

## 结论

本轮 Review 没有发现 P0 阻断项。`core/` 新核的本地质量门禁和最新主线 Windows/Linux CI 均为绿色，RT 扫描、回放语料校验、语义矩阵生成和 Debug CTest 全部通过。

需要优先处理的风险有三类：

1. `STATUS.md` 与真实项目状态已经漂移，尤其是测试数量、RT 扫描文件数、KB-051 状态和 CI 门禁口径。
2. `AxisGroup` / `AxisModel` 是带所有权关系的公共状态类型，但当前未显式禁止 copy/move，默认复制会复制成员指针和 owner 指针，API 合同不安全。
3. `core/axis/group.h` 已膨胀到 4306 行，组生命周期、路径执行、窗口、笛卡尔、kinematics、接管连接段等状态集中在一个 header 内，后续缺陷定位和变更审查成本过高。

## 审查范围

- 规范与状态入口：`CLAUDE.md`、`CONTEXT.md`、`STATUS.md`、`doc/compliance/known-boundaries.md`
- 构建与发布入口：根 `CMakeLists.txt`、`core/CMakeLists.txt`、`.github/workflows/*.yml`
- 新核主要层级：`core/rt`、`core/otg`、`core/geom`、`core/plan`、`core/exec`、`core/axis`、`core/fb`、`core/adapters`
- 旧线 `src/`：只做冻结边界和显著风险抽查，不按新核 RT 合同审计
- 本轮产物：Review 报告与修复队列，不修改业务代码

## 验证矩阵

| 检查 | 结果 | 备注 |
|------|------|------|
| `cmake -P cmake/rt_safety_scan.cmake` | 通过 | 输出为 `RT-safety scan passed (24 files)` |
| `cmake -P cmake/verify_replay_fixtures.cmake` | 通过 | 18 files，2409 samples |
| `cmake -P cmake/generate_part1_part2_matrix.cmake` | 通过 | 49 rows |
| `.\build.ps1 -Configuration Debug -Test` | 通过 | CTest 发现并执行 48 项测试，全部通过 |
| `ctest --test-dir build -N` | 通过 | 当前测试清单为 48 项 |
| `git diff --check` | 通过 | 报告写入后复验通过；新增文件另做内容检查 |
| `gh run list --limit 10` | 通过 | 最新 `Windows CI` 与 `Linux CI` 均 success，触发时间 2026-07-08 22:34 UTC |

未在本轮重跑的重型/周期门禁：coverage gate、mutation score gate、core nightly、wheels。原因是这些 workflow 主要为定时、手动或 tag 触发，且本轮目标是 Review 报告，不是发布签核。

## Findings

### P1：`STATUS.md` 已经不是可信单页状态源

证据：

- `STATUS.md:11` 写“36 项测试全绿”，但当前 `ctest --test-dir build -N` 显示 48 项测试。
- `STATUS.md:38` 写“36 目标全绿”，与当前 CTest 48 项不一致。
- `STATUS.md:40` 写 RT 静态扫描 21 文件，但本地脚本输出为 24 files。
- `STATUS.md:51` 仍把 **KB-051 组接管速度断崖** 写成“已登记；修复批次 Y7 插队最前”，而 `doc/compliance/known-boundaries.md:66` 已登记为 **已修复（Y7，2026-07-08）**，且 `core/CMakeLists.txt:146-149` 已有 `plcopen_core_y7_group_takeover_tests`。
- `STATUS.md:41` 把 Windows/Linux、ARM64、clang-tidy、变异分数、覆盖率、wheels、文档站写在同一条 CI 能力里；实际 workflow 触发条件不同：coverage/mutation/core-nightly 为定时或手动，wheels 为 tag 或手动，docs 只在 `main` 的 docs/mkdocs 路径变更或手动触发。

风险：

- `STATUS.md` 自称“现在在哪”的唯一入口，一旦它滞后，后续 agent 会把已修复问题当待办，把周期门禁误认为 PR 必跑门禁。
- KB-051 属于高风险运动语义缺陷，状态漂移会直接误导发布说明、用户规避建议和后续测试安排。

建议修复：

- 立刻更新 `STATUS.md`：测试数改为 48，RT 扫描改为 24 files，KB-051 改为已修复并保留 circular/笛卡尔适用范围边界。
- 把 CI 口径拆成“push/PR 必跑”“定时/手动质量门”“tag 发布门”“docs 部署门”四行。
- 后续让 `STATUS.md` 中的测试数量和回放数量来自 CMake/脚本输出，避免人工同步。

### P1：`AxisGroup` / `AxisModel` 缺少显式 copy/move 合同

证据：

- `core/axis/group.h:172-181` 定义 `class AxisGroup` 和构造函数，但没有 `AxisGroup(const AxisGroup&) = delete`、move 删除或安全 move 实现。
- `core/axis/group.h:213-229` 的 `add_axis` 会把成员轴加入 `axes_`，并调用 `axis.set_group_owner(this)`。
- `core/axis/group.h:4269` 保存 `rt::StaticVector<AxisModel *, MaxAxes> axes_`，默认复制会复制裸指针。
- `core/axis/state.h:242-250` 定义 `class AxisModel` 和构造函数，同样没有显式 copy/move 合同。
- `core/axis/state.h:272-280` 暴露 `group_owner()` / `set_group_owner(void*)`，默认复制 `AxisModel` 会复制 owner 指针。
- `core/rt/static_vector.h:11-83` 的 `StaticVector` 没有删除复制，底层 `std::array` 使其默认可复制。

风险：

- 复制 `AxisGroup` 后，新组会持有同一批 `AxisModel*`，但轴的 `group_owner_` 仍指向原组；两个组对象的成员视图和轴 owner 合同会分裂。
- 复制 `AxisModel` 后，新轴可能携带旧 owner 指针、快照、队列和运动状态，表现为“看似已入组或运动中”的伪状态。
- 当前项目内部未发现主动复制 `AxisGroup` 的用法，所以这不是已观测运行时回归；但它是公共类型 API 的潜在误用点，应在用户代码可见前收紧。

建议修复：

- 对 `AxisGroup` 和 `AxisModel` 显式删除 copy/move，除非能定义并验证 rebind-safe move。
- 增加编译期测试：`static_assert(!std::is_copy_constructible_v<AxisGroup>)`、`!std::is_move_constructible_v<AxisGroup>`，`AxisModel` 同理。
- 如果后续确实需要 move，把 owner rebinding、成员轴状态和 disable/standby 前置条件写入语义矩阵。

### P1：`core/axis/group.h` 成为新核最大架构热点

证据：

- `core/axis/group.h` 当前 4306 行，约 186 KB，是 `core/` 下最大文件。
- `AxisGroup` 从 `core/axis/group.h:172` 开始，公开面覆盖成员管理、group status、窗口深度、坐标系、kinematics、pose、Cartesian、direct/path 等多类职责。
- `core/axis/group.h:4230-4275` 的成员状态同时包含 pose/kinematics、cartesian window、joint window、connector、active command、override、interrupt、frame/tool 变换和成员轴指针。

风险：

- Y7 接管修复、笛卡尔窗口、Part 4 管理/路径表等高风险逻辑集中在一个 header，后续任意小变更都难以局部审查。
- 状态字段彼此距离远，语义变更容易漏掉 reset、abort、stop、re-entry、replay 和 readback 路径。
- 当前测试覆盖强，但文件结构放大了未来改动的回归面。

建议修复：

- 不改变 public API 的前提下，先拆内部 helpers：`group_window`、`group_cartesian`、`group_takeover_connector`、`group_frames_pose`、`group_path_table`。
- 每次只迁移一个行为簇，迁移前后跑对应目标和 replay；不要在拆分批次里重写算法。
- 把状态字段按行为簇收拢，减少“一个 reset 分散改十处”的维护模式。

### P2：主线门禁与周期门禁的边界需要机器可读化

证据：

- `linux-ci.yml` / `windows-ci.yml` 在 push、pull_request、workflow_dispatch 上运行，是主线反馈面。
- `coverage.yml`、`mutation-score.yml`、`core-nightly.yml` 是 schedule + workflow_dispatch。
- `wheels.yml` 是 tag `v*` + workflow_dispatch。
- `docs.yml` 只在 `main` 且 `docs/**` / `mkdocs.yml` 变更时部署，或手动触发。

风险：

- 文档把“能力存在”和“每 PR 必跑”混在一起，容易造成发布签核误判。
- 后续添加门禁时，如果没有集中矩阵，`STATUS.md`、README、发布草案和 workflow 会继续漂移。

建议修复：

- 新增或生成一份 `doc/compliance/ci-gates.md`，列出 gate、触发条件、证明对象、是否 release-blocking。
- `STATUS.md` 只引用该矩阵，不手写长句。
- 发布草案只引用 release-blocking gates，避免把 nightly/手动 gate 当作每次发布必跑项。

### P2：旧线 `src/` 的冻结边界需要在消费文档中反复显式化

证据：

- `src/` 旧线仍存在 `new/delete`、`std::vector`、`std::map`、`std::function` 等动态分配和非 RT 友好结构。
- 项目规范已声明旧线冻结为回放/迁移基线，新消费面为 `core/` 与 `plcopen::plcopen`。

风险：

- 新用户或新 agent 如果从 `src/demo`、旧 FB 或旧 motion 代码入手，可能把旧线模式误搬回 `core/`。
- 这不是当前 `core/` RT 合同缺陷，但属于 onboarding 和采纳风险。

建议修复：

- 在用户入口文档和 demo 索引中明确标注：`src/` 是 legacy baseline，不代表新核 RT 编码风格。
- 新示例优先指向 `core/demo` 与 `pyplcopen`，旧示例只作为迁移对照。

## 正向观察

- 层级 include 抽查没有发现明显反向依赖：`core/adapters` 向内依赖 `axis` 属 L7 适配层语境，`core/fb` 依赖 `axis` 属 L6 语境。
- RT 静态扫描通过，且 `core/test/a2_alloc_guard_tests.cpp` 对冻结窗口分配有运行期断言。
- KB-051/052/053 已有明确 known-boundaries 登记和 `plcopen_core_y7_group_takeover_tests` 验收目标。
- 回放 fixture 校验和 Part 1/2 语义矩阵生成脚本可作为文档漂移的机器护栏基础。

## 修复队列

| 优先级 | 任务 | 验收 |
|--------|------|------|
| P1 | 更新 `STATUS.md` 的测试数、RT 扫描文件数、KB-051 状态和 CI 口径 | `ctest -N`、RT scan、workflow 触发条件与文档一致 |
| P1 | 删除或定义 `AxisGroup` / `AxisModel` copy/move 语义 | 编译期 static_assert 覆盖，核心测试全绿 |
| P1 | 为 `core/axis/group.h` 制定拆分计划，先迁移 connector 或 window 一个行为簇 | 单行为簇 diff，可对应测试和 replay 证明行为不变 |
| P2 | 建立 CI gate 矩阵并由 `STATUS.md` 引用 | workflow 触发条件与矩阵一致 |
| P2 | 强化 legacy `src/` 与新核 `core/` 的消费边界 | 新用户入口不再把旧线作为默认 RT 示例 |

## 残余风险

- 本轮没有运行 coverage、mutation、nightly 和 wheel 构建；它们应在发布签核或周期质量复盘中单独确认。
- 本轮没有对每个算法族做逐行形式化审计，重点是架构边界、状态漂移、公共 API 合同和门禁可信度。
- `AxisGroup` 拆分本身有回归风险，必须按行为簇小批次推进，不能与语义变更混合。
