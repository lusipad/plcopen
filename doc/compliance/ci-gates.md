# CI gate 矩阵

本页记录 GitHub Actions gate 的触发条件和证明对象，避免把“能力存在”误写成“每个 PR 必跑”。`STATUS.md` 只引用本矩阵，不重复展开长句。

> 远端证据截至 2026-07-10：Windows/Linux 主线与 Mutation 通过；Coverage、
> Core Nightly、Wheels 最近一次失败。三 job Nightly、gcovr 8.6 固定与 Linux
> wheel 修复仍是未提交工作树内容，不能当作远端已生效证据。Documentation
> workflow 只证明生成并推送 `gh-pages`；Pages API 与站点仍为 404。

| Gate | Workflow | 触发 | 证明对象 | Release blocking |
|------|----------|------|----------|------------------|
| Windows 主线 | `.github/workflows/windows-ci.yml` | `push`、`pull_request`、`workflow_dispatch` | RT scan、Part 1/2 matrix、replay fixture、完整 CTest、Windows coverage artifact、installed/fetchcontent consumer | 是 |
| Linux 主线 | `.github/workflows/linux-ci.yml` | `push`、`pull_request`、`workflow_dispatch` | gcc/clang Release 构建、RT scan、Part 1/2 matrix、replay fixture、完整 CTest、benchmark baseline、clang-tidy gate、ARM64/QEMU、Python binding、consumer smoke、API docs 生成 | 是 |
| Coverage Gate | `.github/workflows/coverage.yml` | 每周一 `03:47 UTC`、`workflow_dispatch` | `cmake/coverage_gate.cmake` + gcovr 8.6，core line coverage >= 90% | 周期质量门；发布前按需手动确认 |
| Mutation Score Gate | `.github/workflows/mutation-score.yml` | 每周一 `03:17 UTC`、`workflow_dispatch` | `cmake/mutation_score_gate.cmake`，登记 mutation score 阈值 | 周期质量门；发布前按需手动确认 |
| Core Nightly | `.github/workflows/core-nightly.yml` | 每日 `02:23 UTC`、`workflow_dispatch` | 三个独立 job 分别执行 1,000,000 轮 deterministic OTG fuzz、A2 allocation 50,000,000-cycle soak、1,000,000 轮 time-optimal OTG fuzz；单项失败不跳过其余项 | 周期质量门；不替代主线 CI |
| Wheels | `.github/workflows/wheels.yml` | tag `v*`、`workflow_dispatch` | cibuildwheel 三平台 wheel、sdist artifact、`ci/smoke_test.py` | 发布包门 |
| Documentation | `.github/workflows/docs.yml` | `main` 分支 `docs/**` 或 `mkdocs.yml` 变更、`workflow_dispatch` | MkDocs GitHub Pages 部署 | 文档发布门 |

## 使用规则

- PR 合入判断默认看 Windows 主线和 Linux 主线。
- `main` 当前无 branch protection/ruleset；上表记录的是 workflow 证据与项目政策，不代表 GitHub 已强制 required checks。
- 发布签核需要按发布内容确认 Wheels、Documentation，以及必要的周期质量门最近一次结果。
- `STATUS.md` 中的测试数量、RT scan 文件数、回放数量应来自本地命令或 CI 输出，不手写推断值。
