# CI gate 矩阵

本页记录 GitHub Actions gate 的触发条件和证明对象，避免把"能力存在"
误写成"每个 PR 必跑"。`STATUS.md` 只引用本矩阵，不重复展开长句。

> 远端证据截至 2026-07-12（基线 `f29d07d`）：Windows/Linux 主线、
> Coverage、Mutation、Core Nightly（五作业）、Wheels 全部通过；
> Pages 已启用且站点 200（http://lusipad.com/plcopen/ ）。

| Gate | Workflow | 触发 | 证明对象 | Release blocking |
|------|----------|------|----------|------------------|
| Windows 主线 | `.github/workflows/windows-ci.yml` | `push`、`pull_request`、`workflow_dispatch` | RT scan、Part 1/2 matrix、replay fixture、完整 CTest（56 项，含 st 语言层与转换矩阵三方比对）、Windows coverage artifact（延迟门基准豁免插桩重跑，预算门仍在 CTest 强制）、installed/fetchcontent consumer | 是 |
| Linux 主线 | `.github/workflows/linux-ci.yml` | `push`、`pull_request`、`workflow_dispatch` | gcc/clang Release 构建、RT scan、Part 1/2 matrix、replay fixture、完整 CTest（含两个 st 字节码锚点哈希的跨平台复验）、benchmark baseline、clang-tidy gate、ARM64/QEMU（matrix_check 经 CMAKE_CROSSCOMPILING_EMULATOR 执行）、Python binding、consumer smoke、API docs 生成 | 是 |
| Coverage Gate | `.github/workflows/coverage.yml` | 每周一 `03:47 UTC`、`workflow_dispatch` | `cmake/coverage_gate.cmake` + gcovr 8.6；全 `core/` line >= 90% 为硬门；生产运动栈 `rt/otg/geom/plan/exec/axis/fb/kin/stream` branch 固定范围报告，85% 达标后启用硬门；上传两份 JSON summary，口径与首测见 `branch-coverage-baseline.md` | 周期质量门；发布前按需手动确认 |
| Mutation Score Gate | `.github/workflows/mutation-score.yml` | 每周一 `03:17 UTC`、`workflow_dispatch` | `cmake/mutation_score_gate.cmake`，登记 mutation score 阈值（st 模块入抽查清单为计划补遗项） | 周期质量门；发布前按需手动确认 |
| Core Nightly | `.github/workflows/core-nightly.yml` | 每日 `02:23 UTC`、`workflow_dispatch` | **五个独立 job**：1,000,000 轮 deterministic OTG fuzz、A2 allocation 50,000,000-cycle soak、1,000,000 轮 time-optimal OTG fuzz、executor 双域 TSAN（20,000 周期 @4kHz，ADR-0007）、ST 前端 fuzz 100,000 输入（ASan/UBSan）；单项失败不跳过其余项 | 周期质量门；不替代主线 CI |
| Wheels | `.github/workflows/wheels.yml` | tag `v*`、`workflow_dispatch` | cibuildwheel 三平台 wheel、sdist artifact、`ci/smoke_test.py`；tag 推送额外触发 `publish_pypi`（Trusted Publishing，PyPI 侧 publisher 注册完成前该作业失败属预期） | 发布包门 |
| Documentation | `.github/workflows/docs.yml` | `main` 分支 `docs/**` 或 `mkdocs.yml` 变更、`workflow_dispatch` | MkDocs 构建并推 `gh-pages`；Pages 已启用（2026-07-11），站点 http://lusipad.com/plcopen/ | 文档发布门 |

## 使用规则

- PR 合入判断默认看 Windows 主线和 Linux 主线。
- `main` 当前无 branch protection/ruleset；上表记录的是 workflow 证据与项目政策，不代表 GitHub 已强制 required checks。
- 发布签核需要按发布内容确认 Wheels、Documentation，以及必要的周期质量门最近一次结果。
- `STATUS.md` 中的测试数量、RT scan 文件数、回放数量应来自本地命令或 CI 输出，不手写推断值。
