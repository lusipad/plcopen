# R0-R4 详细工作拆解（第 1 年重写冲刺）

## 本文档的性质

| 是什么 | 不是什么 |
|--------|----------|
| `rewrite-plan.md` 3.1b 的可执行工作拆解 | 新的战略、路线图或架构决策 |
| issue / PR 级任务池的来源 | 大设计文档 |
| R0-R4 的 DoD、前置条件和验证入口 | Phase B/C 的展开计划 |

依据：`long-term-plan.md` 第 1 年 = R0-R4 重写冲刺，`rewrite-plan.md` R0-R4 迁移策略，`ai-collaboration.md` 风险分级与门禁策略。

当前事实基线（2026-07-04 本地核对）：

- 源码版本面已到 `0.11.0`，`CMakeLists.txt` / `.version` / `CHANGELOG.md` 一致。
- 本地 tag 与 GitHub Release 仍只到 `v0.10.0`；`v0.11.0` 需要单独发布收口。
- `doc/design/core/architecture.md` 已作为新核架构图单一事实源建立。
- replay fixture 格式校验、mutation smoke、机读 Part 1/2 矩阵、KB 编号化已经有首批脚手架。

## 拆分规则

一个任务只有满足这些条件才进入本文件：

- 能由一个 issue 或一个小 PR 承载。
- 有明确前置条件和 DoD。
- 有最小验证命令或人工验收证据。
- 不把 Phase B/C 的硬件、EtherCAT、kinematics、认证工作提前塞进 R0-R4。

风险等级沿用 `ai-collaboration.md`：

- `T0`：文档、demo、新增测试、格式和 CI 脚本。
- `T1`：非 RT 路径、工具链、pyplcopen、构建配置。
- `T2`：RT 周期路径、OTG / 规划器算法、状态机语义、公开 API。
- `T3`：LICENSE / NOTICE / PROVENANCE、release tag、外部安全或商用承诺。

## 阶段出口

| 阶段 | 出口判据 |
|------|----------|
| R0 | `v0.11.0` 发布状态明确；旧线维护边界明确；golden replay 语料可校验；PROVENANCE / 许可证决策有记录 |
| R1 | L0/L1 可独立构建测试；OTG oracle + fuzz 门禁可运行；RT-safety 扫描与基准框架上线；Tier 1/2 编译证据建立 |
| R2 | L2-L4 新内核路径执行链可跑线段、圆弧、前瞻、blending 与周期采样；效果指标进入 CI 趋势 |
| R3 | L5/L6 语义迁移完成；全量现有 Catch2 行为由新核承接；replay diff 区分等价与有意变更 |
| R4 | demo、consumer、pyplcopen、文档和迁移指南收口；旧 `src/` 删除或归档；`v1.0.0-alpha` 证据包完整 |

## R0：冻结与基线（M0-M0.5）

目标：让旧线成为可信基线，而不是一边重写一边继续漂移。

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R0.1 | `v0.11.0` 发布证据刷新 | T0 | `main` 最新 CI 完成 | 记录 Windows / Linux / coverage / docs / consumer 状态，明确是否满足发布条件 | `gh run list --limit 10`，必要时 `gh run view <id>` |
| R0.2 | `v0.11.0` tag / Release 草案 | T3 | R0.1 通过 | release notes、tag commit、source-only 口径和不承诺 ABI 的说明准备好 | 人工执行 tag / Release；AI 只起草 |
| R0.3 | 旧线维护政策落文档 | T0 | R0.2 决策明确 | README / ROADMAP 指向 v0.x P0-only 维护窗口；不新增旧线功能 | `rg -n "v0\\.x|P0-only|维护" README.md ROADMAP.md doc` |
| R0.4 | replay fixture 覆盖清单 | T1 | 现有 fixture 格式校验存在 | 列出全量测试场景到 fixture 的覆盖矩阵；先覆盖代表性 motion / FB / group 场景 | `cmake -P cmake/verify_replay_fixtures.cmake` |
| R0.5 | golden replay 录制器最小入口 | T1 | R0.4 清单确定 | 能从现有测试或 demo 录制 setpoint JSONL；格式稳定、可审查 | 新增 CTest 或脚本 smoke |
| R0.6 | PROVENANCE 初稿 | T3 | 许可证方向初定 | 每个新核模块的语义来源、禁止来源和旧核回避清单可查 | 人工审查；不靠自动通过 |
| R0.7 | 许可证决策记录 | T3 | D-LIC 拍板 | ADR 记录 Apache / MIT / dual-license 取舍；不直接改 license 除非明确授权 | 人工审签 |
| R0.8 | R0 证据包 | T0 | R0.1-R0.7 | `doc/planning/` 有 R0 完成摘要，列出未决人工项 | `git diff --check` |

R0 不做：新核实现、旧线功能、Phase B 硬件计划展开。

## M0 文档整合（与 R0 并行）

目标：让公开文档不误导 AI 或贡献者。

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| M0.1 | ROADMAP 当前里程碑改口径 | T0 | R0.1 状态明确 | 当前里程碑指向 R0-R4 重写冲刺，历史里程碑保留 | `rg -n "R0|R4|重写" ROADMAP.md` |
| M0.2 | VISION 层次表补第 0.5 层 | T0 | long-term-plan 评审方向不变 | VISION 与长期规划互链，不复制大段计划 | `rg -n "0\\.5|路径级" VISION.md` |
| M0.3 | README 顶部状态横幅 | T0 | R0.2 口径明确 | README 说明 v0.x 当前状态、重写进行中、新架构入口 | `rg -n "重写|v0\\.x|doc/planning" README.md` |
| M0.4 | v0.x 设计文档头注 | T0 | `doc/design/core/architecture.md` 已存在 | 旧设计文档标注为 v0.x 基线参考；合规矩阵标注为新核验收规格 | `rg -n "v0\\.x|新核验收规格" doc/design doc/compliance` |
| M0.5 | 文档索引收口 | T0 | M0.1-M0.4 | `doc/planning/README.md` 指向本拆解、长期计划、重写计划、AI 协作计划 | `git diff --check` |

## R1：L0/L1 地基（M1-M3）

目标：先交付最小、可独立验证的实时地基和 OTG 求解器。R1 是 AI 吞吐的放大器，不是功能展示。

**软件闭环状态（2026-07-04）**：R1A/R1B/R1C 的仓库内自动化部分已接入 `core/`、CTest、RT-safety scan、fuzz smoke、oracle、benchmark、nightly fuzz 入口和交叉编译 smoke 脚本；72h PREEMPT_RT 报告仍是模板，需真实硬件环境填写。

### R1A：构建骨架与 RT 基础

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R1A.1 | 新核 CMake 目标骨架 | T1 | M0 文档入口存在 | 新增 `core/` 目标，默认不替换旧 `src/`；可单独构建测试 | `cmake --build <build> --target <new-core-test>` |
| R1A.2 | L0 模块 README | T0 | R1A.1 | 写清职责、不变量、禁用事项、依赖规则 | `rg -n "零分配|零锁|无异常" core` |
| R1A.3 | 整型周期时间域 | T2 | R1A.1 | `CycleTick` / cycle duration 只在边界换算，RT 路径不做 `t += dt` | 单元测试覆盖长时间累计 |
| R1A.4 | 定长容器最小集 | T2 | R1A.1 | 只实现当前 R1 必需容器，不做通用 STL 替代库 | 容量边界测试，禁止堆分配测试 |
| R1A.5 | SPSC 命令队列最小实现 | T2 | R1A.4 | 单生产者 / 单消费者，无锁、容量固定、满队列语义明确 | 单元测试覆盖空、满、wrap-around |
| R1A.6 | 错误码与 `Result` 约定 | T2 | R1A.1 | RT 路径无异常；错误可携带最小诊断码 | 编译选项尝试 `-fno-exceptions` 可过核心目标 |

### R1B：OTG 求解器与验证

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R1B.1 | OTG 输入/输出合同文档 | T0 | R1A.2 | 明确状态、目标、限制、边界和数值容差 | ADR 或 L1 README |
| R1B.2 | `Profile1D` 定长表示 | T2 | R1B.1 | 最多 7 段闭式分段，RT sample O(1)，无堆分配 | 单元测试 + allocation guard |
| R1B.3 | 最小同向/静止状态求解 | T2 | R1B.2 | 先覆盖静止到目标、同向速度接管，不碰全部 case | oracle 对比 |
| R1B.4 | 任意初始 `(p,v,a)` case 扩展 | T2 | R1B.3 | 反向、非零加速度、短距离、不可达限制均有语义 | fuzz smoke + edge tests |
| R1B.5 | 数值积分 oracle | T1 | R1B.2 | oracle 与实现分离，避免同错同源 | CTest smoke |
| R1B.6 | fuzz smoke / nightly 分层 | T1 | R1B.5 | PR 内秒级，夜间百万级；失败可复现 seed | CI workflow |
| R1B.7 | 可选外部 oracle 调研 | T0 | R1B.4 | 只记录 Ruckig 社区版是否适合交叉验证；不直接新增依赖 | ADR，必要时后续任务拆出 |

### R1C：门禁与基准

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R1C.1 | RT-safety 静态扫描 v1 | T1 | R1A.1 | 扫描 RT 路径禁分配、锁、异常、系统调用、浮点时间累加 | CI 失败用例 |
| R1C.2 | allocation guard | T2 | R1A.4 | `freeze()` 后 RT 路径分配断言可触发 | 单元测试 |
| R1C.3 | 微基准 harness | T1 | R1B.2 | 记录 `sample()`、queue、container 的周期耗时趋势 | CI 上传简表或日志 |
| R1C.4 | 72h RT 报告模板 | T0 | R1C.3 | 模板定义硬件、内核、调优、p99.999 报告字段 | 文档检查 |
| R1C.5 | ARM64 交叉编译 | T1 | R1A.1 | Tier 1 build-only 或 QEMU smoke 进 CI | Linux CI 新 job |
| R1C.6 | arm-none-eabi / footprint 门禁 | T1 | R1A.6 | Tier 2 build-only，报告 flash / RAM 估算；失败不影响旧线 | CI job 或手动脚本 |

R1 不做：路径缓冲、圆弧、FB 迁移、pyplcopen 迁移。

## R2：L2-L4 运动内核（M4-M7）

目标：让“路径”成为一等公民，但仍不接 PLCopen FB 语义。

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R2.1 | L2 几何合同文档 | T0 | R1 通过 | line / arc / spline 的输入、退化语义、容差写清 | L2 README |
| R2.2 | 直线段几何 | T2 | R2.1 | 弧长、采样、切向连续性明确 | 单元测试 |
| R2.3 | 圆弧/螺旋几何 v1 | T2 | R2.2 | 共线三点、零半径、病态半径显式错误 | 几何边界测试 |
| R2.4 | 弧长表与逆映射 | T2 | R2.3 | 规划期构建，RT 采样 O(1) 或有界；速度波动可测 | oracle + benchmark |
| R2.5 | 路径段缓冲 | T2 | R2.2 | 固定容量、单调消费、满缓冲语义明确 | wrap / underflow tests |
| R2.6 | look-ahead v1 | T2 | R2.5 | 双向扫描、窗口默认值、软降级语义明确 | 循环时间效率指标 |
| R2.7 | blending v1 | T2 | R2.6 | 五次 Bezier 或当前决策曲线；公差带不超限 | 路径偏差测试 |
| R2.8 | L4 周期采样器 | T2 | R2.5 | 每周期 O(1)，只读承诺轨迹，状态可快照 | allocation guard + benchmark |
| R2.9 | gear / cam 同步内核映射 | T2 | R2.8 | 迁移现有语义所需的最小同步 primitive | 当前 KB 测试映射清单 |
| R2.10 | 效果指标进 CI | T1 | R2.6-R2.8 | 速度波动、路径误差、循环时间效率有趋势输出 | CI log / artifact |

R2 降级线：若时间不足，look-ahead 深度和 blending 品质可保守，但 R2.5 / R2.8 / R2.10 不能砍。

## R3：L5/L6 语义迁移（M8-M10）

目标：把 v0.x 的可执行规格迁到新核上。测试先行，语义默认不变，故意变更必须写迁移说明。

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R3.1 | 迁移批次矩阵 | T0 | R2 通过 | 按 compliance matrix 分批：base、basic IEC、single-axis、multi-axis、Part 4 linear | 矩阵文档 |
| R3.2 | L5 axis/group 状态机 | T2 | R3.1 | Axis / group 生命周期映射到新数据流；单写者规则不破 | state-machine tests |
| R3.3 | FB 基类合同迁移 | T2 | R3.2 | Execute/Enable 生命周期、清错、Done/Busy 持有语义一致 | base tests |
| R3.4 | 基础 IEC FB 迁移 | T1 | R3.3 | 边沿、定时器、计数器、RTC 当前语义一致 | `test_fb_basic` |
| R3.5 | 单轴管理 FB 迁移 | T2 | R3.3 | 参数、IO、AxisInfo、TouchProbe 当前边界一致 | generated matrix anchors |
| R3.6 | 单轴 motion FB 迁移 | T2 | R3.5 | Move/Home/Halt/Profile/Torque 当前 KB 语义一致 | replay diff + Catch2 |
| R3.7 | 多轴同步 FB 迁移 | T2 | R3.6 | Gear/Cam/CombineAxes 当前公开边界一致 | multi-axis tests |
| R3.8 | Part 4 linear FB 迁移 | T2 | R3.7 | ACS 2-8 轴、Aborting/Buffered、CommandID、GroupStop 语义一致 | Part 4 matrix tests |
| R3.9 | replay diff 仲裁 | T1 | 每个迁移批次 | 未声明变更零差异；声明变更有迁移说明 | diff report |
| R3.10 | 迁移说明草案 | T0 | R3.9 | 旧 API、行为变化、保留边界列清楚 | docs-sync |

R3 不做：新功能、坐标系、EtherCAT、kinematics。任何“顺手增强”进 Phase B backlog。

## R4：切换收口（M11-M12）

目标：让新核能被用户实际消费，并把旧线有秩序地退到维护状态。

| ID | 任务 | 等级 | 前置 | DoD | 验证 |
|----|------|------|------|-----|------|
| R4.1 | CMake install/export 迁移 | T1 | R3 通过 | `find_package(plcopen)` 使用新核目标 | installed consumer smoke |
| R4.2 | FetchContent consumer 迁移 | T1 | R4.1 | source-tree consumer 使用新核 | FetchContent smoke |
| R4.3 | demos 迁移 | T0 | R3 通过 | 只保留能展示当前承诺的 demo | demo smoke |
| R4.4 | pyplcopen smoke 迁移 | T1 | R4.1 | Python facade 当前最小公开面可用 | Python smoke |
| R4.5 | README 重写为新核口径 | T0 | R4.1-R4.4 | README 不再把 v0.x 架构当当前架构 | docs grep |
| R4.6 | 旧设计文档归档 | T0 | R4.5 | v0.x 设计文档移入或标注归档区；新核设计为当前入口 | link check |
| R4.7 | 删除或隔离旧 `src/` | T2 | R4.1-R4.4 | 主构建不再编译旧核；回放基线仍可追溯 tag/branch | full CI |
| R4.8 | v0.x EOL 公告 | T3 | R4.7 | P0-only 窗口、结束日期、迁移路径明确 | 人工审签 |
| R4.9 | `v1.0.0-alpha` 证据包 | T3 | R4.1-R4.8 | release notes、DoD、已知限制、未声明商用级 | 人工发布 |

## 跨阶段固定门禁

这些门禁一旦上线，不为赶进度关闭：

| 门禁 | 最晚上线阶段 | 失败处理 |
|------|--------------|----------|
| `git diff --check` | R0 | 修正格式 |
| full CTest | R0 | 不合并 |
| replay fixture format | R0 | 不合并 |
| docs-sync | R1 | 补文档或拆 PR |
| RT-safety scan | R1 | 修正或证明不在 RT 路径 |
| fuzz smoke | R1 | 固定 seed，先修再合 |
| allocation guard | R1 | 修正，不能豁免 RT 路径 |
| benchmark trend | R1 | 记录 delta；T2 变更需解释 |
| replay diff | R3 | 未声明变更必须零差异 |

## 首批建议 issue

按当前仓库状态，先开这些最小工单：

1. R0.1：刷新 `v0.11.0` 发布证据，确认 tag / Release 缺口。
2. R0.4：把现有 replay fixture 格式校验扩成覆盖清单。
3. R0.5：实现最小 setpoint JSONL 录制入口。
4. M0.1-M0.3：ROADMAP / VISION / README 状态口径同步。
5. R1A.1-R1A.2：建立新核 CMake 骨架和 L0 README。
6. R1A.3：实现整型周期时间域，先用最小测试锁住长时间累计。
7. R1B.1：写 OTG 输入/输出合同，不先写求解器。
8. R1C.1：RT-safety 扫描 v1，先扫分配和锁。

## 明确不排入第 1 年

- EtherCAT 真机闭环和 `plcopen-fieldbus` 产品化。
- kinematics 插件 ABI。
- PLCopen 认证。
- ST / G-code 前端。
- 250us 周期档位。
- HMI、IDE、云原生、分布式 PLC。

这些不是不重要，而是 R0-R4 的成功条件是“少做、做硬、可验证”。

---

*本文档最后更新：2026-07-04（初稿：把 R0-R4 拆成 issue / PR 级工作包）*
