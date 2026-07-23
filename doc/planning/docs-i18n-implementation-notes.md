# 实施记录 — 文档站中英双语

计划：[docs-i18n-plan.md](docs-i18n-plan.md)

## 摘要

中文根站与 `/en/` 英文站已完成同路径内容、原生语言切换和双重 strict
构建；Doxygen API 保持在语言目录之外，由 workflow 在两站构建后拼装。
本地构建、逐页 i18n 校验、actionlint 与独立代码审查均通过；
[远端 Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30035093781)
全绿，中文根站与 `/en/` 英文站已上线。

## Decisions

- 2026-07-24：保留 `docs/` 作为英文单一事实源，新增 `docs.zh/` 作为中文对应
  内容，避免移动既有英文文件。
- 2026-07-24：采用 Material 原生多项目语言切换，不新增 i18n 插件。
- 2026-07-24：Doxygen API 保持唯一的 `/api/` 入口，不复制到两个语言目录；
  Notebook 继续使用英文原件，中文页明确标注并链接到 `/en/notebooks/`。
- 2026-07-24：两站先分别构建到 `site/` 与 `site/en/`，再用 MkDocs 已依赖
  的 `ghp-import` 一次推送完整树，避免两次发布出现短暂缺页。
- 2026-07-24：Material 的同页切换保留根 `alternate`，由运行时读取两份
  sitemap 选择对应路径；`ci/verify_docs_i18n.py` 逐页锁定该数据合同。

## Deviations

- 浏览器安全策略不允许直接打开本地 `file://` 构建产物；没有绕过该限制，
  改用标准库 HTML 解析器检查语言标记、alternate 链接、页面一一对应和深层
  锚点；Pages 部署后再以 HTTPS 完成可见页面 smoke。

## Surprises

- `omx explore` 在当前 Windows 环境找不到 allowlist wrapper 所需的 host bash；
  已回退到 `rg` 做只读关系检查，没有影响任务范围。
- 默认 Markdown slug 会把纯中文标题压成 `_1`、`_2`，导致保留下来的跨页
  片段链接失效；已按 Material 官方建议使用 `pymdownx.slugs.slugify`，并把
  三处中文跨页链接改到可读 Unicode 锚点。

## Questions for review

- 无。独立审查最终结论为 APPROVE（0 个剩余问题）。

## Verification

- `python -m mkdocs build --strict -f mkdocs.yml`：通过。
- `python -m mkdocs build --strict -f mkdocs.en.yml`：通过。
- `python ci/verify_docs_i18n.py --site-dir site`：12 对页面通过。
- 翻译保真检查：12 份 Markdown 的 fenced code 完全一致，英文 inline code
  无丢失。
- 生成产物站内链接检查：26 个 HTML 页面无缺失目标或片段。
- actionlint v1.7.12：`.github/workflows/docs.yml` 通过。
- `git diff --check`、`python -m pip check`：通过。
- 远端 Documentation `30035093781`：build 与 deploy 均通过，包含 Doxygen
  API 拼装、双站 strict、12 对页面校验与 `gh-pages` 推送。
- 公网 smoke：中文根站、英文 `/en/`、两种 Python 深层页、Doxygen API 与
  两份 sitemap 均返回 200；HTML `lang` 分别为 `zh` / `en`。
