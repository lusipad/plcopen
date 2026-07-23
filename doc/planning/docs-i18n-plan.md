# 文档站中英双语计划

> 状态：2026-07-24 已批准实施。用户确认继续采用“中文默认 + English
> 切换”的方向。

## 1. 最可能调整的决定

### URL 与语言入口

```text
https://lusipad.com/plcopen/      中文默认站
https://lusipad.com/plcopen/en/   英文站
```

- **置信度：高**
- 两套站点保持相同页面路径；Material 原生语言选择器在对应页面之间切换。
- 既有根路径继续有效并改为对应中文内容，不制造 404；英文内容迁到 `/en/`。
- **会推翻此决定的证据**：维护者要求英文根路径保持内容不变，或要求按浏览器
  语言自动跳转。

### 构建与发布

- 每种语言使用独立 MkDocs 配置和独立源目录，先严格构建到同一 `site/`
  树，再一次性发布。
- 不引入 i18n 插件；使用 Material 已有的 `theme.language` 与
  `extra.alternate`。
- **置信度：高**
- **会推翻此决定的证据**：现有 MkDocs/Material 版本无法在相同页面路径间
  保持语言切换，或 `ghp-import` 无法保持当前 Pages 发布契约。

## 2. 假设

- 用户上一轮确认“继续”，表示批准中文默认、English 切换的建议。置信度高，
  来源：对话。
- 所有当前公开 Markdown 页面都需要中文对应页，而不是只翻译首页。置信度中，
  来源：完整中文支持的通常含义。
- 代码、命令、API 名称、错误文本和路径保持原文；解释性文字翻译为简体中文。
  置信度高，来源：技术文档惯例。
- 英文源继续位于 `docs/`，中文源位于 `docs.zh/`，减少对既有英文内容的扰动。
  置信度高，来源：仓库现状与外科手术式修改要求。

## 3. 偏离策略

- 页面翻译遇到歧义时，选择可逆、逐句对齐、保留技术名词原文的方案，并记录
  到实施记录后继续。
- 部署拼装遇到边界时，优先保持当前 `gh-pages`、自定义域名和一次发布语义。
- 只有出现破坏现有 URL、需要新增第三方依赖、改变 Pages 权限或无法保持中英
  页面一一对应时才停止并重新确认。

## 4. 机械工作（低评审价值）

- 新增 `docs.zh/`，镜像并翻译当前公开 Markdown 页面。
- 新增中文与英文 MkDocs 配置，配置 `zh`/`en` 语言和相互切换链接。
- 调整文档 workflow 的路径触发、双站构建、API 拼装和 Pages 发布命令。
- 增加构建产物断言，验证两种 `lang`、关键页面和 API 文档均存在。

## 5. 验证

- `mkdocs build --strict` 分别构建中文根站和英文 `/en/` 站。
- 产物中根页面为 `lang="zh"`，英文页面为 `lang="en"`。
- 两种站点的每个导航页面都有对应产物，语言切换链接存在。
- 中文页面没有失效的本地 Markdown 链接；代码块与命令保持不变。
- workflow YAML 可解析，`git diff --check` 通过。
- PR Documentation workflow 完整通过；合并后 Pages 部署终态成功。

## 参考语义

Material for MkDocs 官方文档说明每个项目只能设置一种 canonical language，
多语言站点应按语言独立构建，并通过 `extra.alternate` 互联：
<https://squidfunk.github.io/mkdocs-material/setup/changing-the-language/>。
