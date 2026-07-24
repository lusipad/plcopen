# CI gate 矩阵

本页记录 GitHub Actions gate 的触发条件和证明对象，避免把"能力存在"
误写成"每个 PR 必跑"。`STATUS.md` 只引用本矩阵，不重复展开长句。

> 远端证据截至 2026-07-19：v0.20.0 候选 `5a5cf81` 的 Windows、Linux、
> Wheels、Coverage、Mutation、Core Nightly 与 Documentation 全部通过；
> Pages 已启用且站点 200（https://lusipad.com/plcopen/ ）。
>
> 2026-07-24 双语站点
> [Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30035093781)
> 全绿；中文根站、英文 `/en/`、Doxygen API 与两份 sitemap 公网复验均为
> 200。
>
> 2026-07-24 项目文档站内整合
> [Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30054317413)
> 全绿，21 对 Markdown、Doxygen 拼装与 Pages 推送通过；中文 `/project/`、
> 英文 `/en/project/`、架构/合规/安全深层页及两份 sitemap 公网复验均为
> 200。
>
> 2026-07-24 技术参考页站内整合
> [Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30060185662)
> 全绿，24 对 Markdown、英文正文语言合同、Doxygen 拼装与 Pages 推送通过；
> ST 运行时、标准证据、ADR、五个修正后的英文页及两份 sitemap 公网复验均为
> 200，深层页可见语言选择器保持对应路径。
>
> 2026-07-24 D3 在线 Scope 由
> [PR #23](https://github.com/lusipad/plcopen/pull/23) 合并；合并后的
> [Windows](https://github.com/lusipad/plcopen/actions/runs/30071051259)、
> [Linux](https://github.com/lusipad/plcopen/actions/runs/30071051258)、
> [Twin/Scope](https://github.com/lusipad/plcopen/actions/runs/30071051277) 与
> [Documentation](https://github.com/lusipad/plcopen/actions/runs/30071051246)
> 全绿，25 对双语 Markdown 通过；中文 `/guides/online-scope/`、英文
> `/en/guides/online-scope/` 与两份 sitemap 公网复验均为 200。
>
> 2026-07-24 D1 ST Language Server 由
> [PR #26](https://github.com/lusipad/plcopen/pull/26) 合并；合并后的
> [Windows](https://github.com/lusipad/plcopen/actions/runs/30104753037)、
> [Linux](https://github.com/lusipad/plcopen/actions/runs/30104753119)、
> [Language Tools](https://github.com/lusipad/plcopen/actions/runs/30104753368)、
> [Twin](https://github.com/lusipad/plcopen/actions/runs/30104753300) 与
> [Documentation](https://github.com/lusipad/plcopen/actions/runs/30104753259)
> 全绿；Language Tools 在 Windows/Linux 生成并验证 wheel 与 VSIX artifact，
> 26 对双语 Markdown 通过；中文 `/guides/st-language-server/`、英文
> `/en/guides/st-language-server/` 与两份 sitemap 公网复验均为 200。

| Gate | Workflow | 触发 | 证明对象 | Release blocking |
|------|----------|------|----------|------------------|
| Windows 主线 | `.github/workflows/windows-ci.yml` | `main` push、`pull_request`、`workflow_dispatch` | RT scan、Part 1/2 与 Part 5 I/O matrix、replay fixture、87 项非 fuzz CTest（含 D1 LanguageDocument、Part 5 C5、ST conformance 锚点、转换矩阵三方比对、[A1 数值语义合同](../design/core/floating-point-semantics.md)、[A2 WCET 软件度量](st-wcet-semantics.md)与 [X5 纯内存 IPC 合同](executor-ipc-semantics.md)）、排除 fuzz 可执行文件的 Windows coverage artifact（延迟门基准豁免插桩重跑，预算门仍在 CTest 强制）、installed/fetchcontent consumer | 是 |
| Linux 主线 | `.github/workflows/linux-ci.yml` | `main` push、`pull_request`、`workflow_dispatch` | gcc/clang Release 构建、RT scan、Part 1/2 matrix、replay fixture、87 项非 fuzz CTest（含 D1 LanguageDocument、ST conformance 锚点、两个字节码锚点哈希、转换矩阵、A1 数值语义、A2 WCET 与 X5 纯内存 IPC 报告复验）、带平台/编译器身份的 benchmark 输出、83 TU 全量 clang-tidy gate（8 worker 并行，不减少检查项）、ARM64/QEMU 非 fuzz 测试（matrix_check 经 CMAKE_CROSSCOMPILING_EMULATOR 执行）、Python binding、consumer smoke、API docs 生成 | 是 |
| Twin / Scope integration | `.github/workflows/twin-ci.yml` | `main` push、`pull_request`、`workflow_dispatch` | Windows/Linux Python 3.12 从 `.[twin,scope]` 构建安装；本仓 primitive MJCF Gate 0；默认 installed binding smoke；2,000 tick headless MuJoCo 闭环、反馈单位换算、确定性/失败路径、Twin 与 D3 Scope Rerun `.rrd` footer 验证 | 是（涉及 twin/scope/binding/optional metadata 的变更） |
| Language Tools | `.github/workflows/language-tools.yml` | `main` push、`pull_request`、`workflow_dispatch` | Windows/Linux Python 3.12 编译 pybind、原始 LSP transcript、wheel 构建与干净 target 安装；Node 24 从固定 lockfile 执行 manifest/trust 测试、production license/integrity freshness、`npm audit --omit=dev`、本地 VSIX 打包与 dev-tree 归档拒绝；两平台 VSIX artifact 保留 14 天 | 是（涉及 ST 工具域、Python binding/server 或 VS Code 扩展的变更） |
| E5 基准趋势观测 | `.github/workflows/linux-ci.yml` 的 `benchmark-trend` job | `main` push、`pull_request`、`workflow_dispatch` | 同一 Linux/GCC Release runner 重新构建 base/head；OTG excess 采用确定性零退化比较，ST 混合负载每指令成本、笛卡尔 IK 与 64 段窗口重规划采用 9 对 AB/BA + 二次确认；schema v1 JSON artifact 保留 90 天。首次无 base 工具时只 record bootstrap，不冒充比较；非零比较结果写入 job summary 但不阻断 CI | 否（报告型证据） |
| Coverage Gate | `.github/workflows/coverage.yml` | 每周一 `03:47 UTC`、`workflow_dispatch` | `cmake/coverage_gate.cmake` + gcovr 8.6；全 `core/` line >= 90%、生产运动栈 `rt/otg/geom/plan/exec/axis/fb/kin/stream` branch >= 85%、`core/st` 独立 branch >= 85% 均为硬门；上传三份 JSON summary，口径与首测见 `branch-coverage-baseline.md` | 周期质量门；发布前按需手动确认 |
| Mutation Score Gate | `.github/workflows/mutation-score.yml` | 每周一 `03:17 UTC`、`workflow_dispatch` | `cmake/mutation_score_gate.cmake`，20 项登记 mutation（含 ST 编译成功契约与 VM 指令预算边界），总 kill score >= 70% | 周期质量门；发布前按需手动确认 |
| Core Nightly | `.github/workflows/core-nightly.yml` | 每日 `02:23 UTC`、`workflow_dispatch` | **九个独立 job**：11 项统一 fuzz smoke、1,000,000 轮 deterministic OTG fuzz、A2 allocation 50,000,000-cycle soak、1,000,000 轮 time-optimal OTG fuzz、executor/ST L3/L5/X5 TSAN、X5 Linux/Windows 两进程共享内存对拍、ST 五模式各 100,000 输入 ASan/UBSan、ST L7 独立 TSAN；单项失败不跳过其余项 | 周期质量门；不替代主线 CI |
| Wheels | `.github/workflows/wheels.yml` | tag `v*`、`workflow_dispatch` | cibuildwheel 三平台 wheel、sdist artifact、`ci/smoke_test.py`；tag 推送额外触发 `publish_pypi`（Trusted Publishing，PyPI 侧 publisher 注册完成前该作业失败属预期） | 发布包门 |
| Documentation | `.github/workflows/docs.yml` | `main` 分支 `docs/**`、`docs.zh/**`、`overrides/**` 或 `mkdocs*.yml` 变更、`workflow_dispatch` | 26 对中英文 Markdown 路径一一对应，英文正文拒绝中文叙述；中文根站与 `/en/` 英文站分别通过 MkDocs strict，项目/技术导读页回链权威仓库来源，深层页语言选择器保持对应路径，和 Doxygen API 拼装后一次推送 `gh-pages`；Pages 已启用（2026-07-11），站点 https://lusipad.com/plcopen/ | 文档发布门 |

## 使用规则

- PR 合入判断默认看 Windows 主线和 Linux 主线；11 项 `fuzz` 标签 CTest
  只由 Core Nightly 执行，不进入日常 CTest 或 Windows coverage。
- X5 的默认 `plcopen_core_ipc_transport_tests` 只使用进程内内存和线程；
  `plcopen_core_ipc_executor_process` 只有显式打开
  `PLCOPEN_ENABLE_PROCESS_INTEGRATION_TESTS` 才注册，并仅在 Nightly 运行。
- feature branch 的 `push` 不再触发 Windows/Linux 主线；同一 PR 只保留
  `pull_request` 运行，避免重复占用 runner。`main` push 和手动触发不变。
- E5 只消费当前 job 自行构建的 base/head，不下载历史 artifact；历史 JSON
  只供审计和画趋势，不能作为高权限 workflow 的可执行或基线输入。比较器、
  策略和完整合同见[基准趋势管线](../design/benchmark-trend-pipeline.md)。
- `main` 当前无 branch protection/ruleset；上表记录的是 workflow 证据与项目政策，不代表 GitHub 已强制 required checks。
- 发布签核需要按发布内容确认 Wheels、Documentation，以及必要的周期质量门最近一次结果。
- `STATUS.md` 中的测试数量、RT scan 文件数、回放数量应来自本地命令或 CI 输出，不手写推断值。
