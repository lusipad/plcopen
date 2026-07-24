# 实施记录 — 项目文档站内整合

计划：[docs-site-integration-plan.md](docs-site-integration-plan.md)

## 摘要

九对双语项目导读页、分层导航和权威源契约已完成；内部计划、归档和完整规范
矩阵继续留在仓库，不进入普通用户路径。本地与远端 strict 构建、21 对页面
校验、全站链接检查、浏览器导航 QA、独立审查和 Pages 部署均通过；中英文
项目导览与深层主题页已上线。

## Decisions

- 2026-07-24：以九对站内导读页整合公开项目主题，不复制内部计划、归档或完整
  规范矩阵。
- 2026-07-24：每个导读页必须回链权威仓库文档；时效性和规范性事实继续以
  仓库原文为准。
- 2026-07-24：将原有“参考”大杂烩拆成使用指南、参考、项目文档和版本四组，
  不改变既有用户指南 URL。
- 2026-07-24：项目聚合页必须直接包含状态、架构、合规、边界、贡献、治理、
  安全和 Changelog 八个权威入口；验证器逐个锁定，避免聚合页只剩部分来源
  仍误通过。

## Deviations

- 无。

## Surprises

- `ci/verify_docs_i18n.py` 原先只特殊处理根 `index.md`；新增
  `project/index.md` 后暴露嵌套 index 的产物与 canonical 计算缺口，已改为
  对任意 `*/index.md` 使用目录 URL。
- `omx explore` 仍因 Windows 环境缺少 allowlist wrapper 所需 host bash
  无法启动；已按仓库约定回退到 `rg`。

## Questions for review

- 独立审查首轮发现聚合页原先只强制 `STATUS.md` 回链，无法证明其他权威入口
  仍存在；已扩为八入口合同，复核结论 APPROVE（0 个剩余问题）。

## Verification

- 新契约实现前失败：缺少 9×2 个项目页及双语导航项。
- `python -m mkdocs build --strict -f mkdocs.yml`：通过。
- `python -m mkdocs build --strict -f mkdocs.en.yml`：通过。
- `python ci/verify_docs_i18n.py --site-dir site`：21 对页面通过。
- 生成站点链接检查：44 个 HTML 页面、2646 个站内目标与片段通过。
- fenced-code 保真：21 对 Markdown 页面通过。
- `python -m ruff check ci/verify_docs_i18n.py`、`python -m pip check`、
  `git diff --check`：通过。
- 本地浏览器 QA：中文导航显示九个项目入口；中英文聚合页 `lang`、
  canonical 与深层路径正确；英文聚合页八个权威来源齐全。
- 独立代码审查：APPROVE，0 个剩余问题。
- PR #19 的 Documentation `30053381705`：strict build 通过；PR 事件按契约
  跳过 deploy。
- 合并提交 `ecf9638` 的
  [Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30054317413)：
  build 与 deploy 均通过，包含 21 对页面、Doxygen API 拼装和 `gh-pages`
  推送。
- 公网 smoke：中文 `/project/`、英文 `/en/project/`、中文架构、英文合规、
  中文安全和两份 sitemap 均返回 200，语言标记与新路径正确。
- 合并提交 `ecf9638` 的 Linux、Windows、Twin 主分支工作流全部通过。
