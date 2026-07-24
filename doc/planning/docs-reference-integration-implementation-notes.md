# 实施记录 — 技术参考页站内整合

计划：[docs-reference-integration-plan.md](docs-reference-integration-plan.md)

## 摘要

三对技术参考页、五个既有英文页翻译、正文语言合同和深层同页语言切换已
完成并上线。24 对页面、strict 构建、全站链接、浏览器交互、远端主线门禁
与 Pages 公网复验均通过。

## Decisions

- 2026-07-24：下一批只新增 ST 运行时、标准与证据、架构决策三个用户入口；
  规范原文与完整矩阵继续以仓库文件为唯一事实源。
- 2026-07-24：现有公开页优先链接站内导读，但新导读页必须回链全部承重原文。

## Deviations

- 2026-07-24：原计划只新增三个参考入口；核对正文后发现五个既有英文页实际
  仍是中文，因此在同一批内补齐翻译并新增语言合同。未扩大公开主题或 URL。
- 2026-07-24：浏览器实际点击显示语言选择器把深层页送回首页；改为由 Material
  header partial 使用 `page.url` 生成同页链接，并把验证器的可见选择器合同改为
  逐页路径。head 中站点级 alternate 保持根路径，否则 Material 会在每个深层
  URL 下请求 `sitemap.xml` 并产生 404。

## Surprises

- 2026-07-24：上一批 i18n 验证只证明路径、HTML、canonical 与语言选择器成对，
  没有证明英文 Markdown 的正文语言；`docs/` 中实时集成、调参、迁移、运维和
  功能块参考五页仍含完整中文正文。
- 2026-07-24：上一批 alternate 合同固定期待 `/plcopen/` 与 `/plcopen/en/`，
  因而把“能切语言”和“保持当前页面”混为一谈；深层页交互测试才暴露该缺口。

## Questions for review

- 无。

## Verification

- 新页面/导航合同实现前失败：缺少 3×2 个来源和六个导航项。
- 英文正文合同实现前失败：准确定位五个含中文叙述的英文页面。
- `python -m mkdocs build --strict -f mkdocs.yml`：通过。
- `python -m mkdocs build --strict -f mkdocs.en.yml`：通过。
- `python ci/verify_docs_i18n.py --site-dir site`：24 对页面通过。
- 生成站点链接检查：50 个 HTML、3041 个站内目标与片段通过。
- paired fenced-code 检查：24 对 Markdown 页面通过。
- `python -m ruff check ci/verify_docs_i18n.py`、`python -m pip check`、
  `git diff --check`：通过。
- 本地浏览器 QA：三个新参考入口均返回 200；五个英文化页面 `lang=en`
  且正文无汉字；中文 ST 深层页经可见语言选择器准确到达对应英文深层页，
  两份 sitemap 全部 200，控制台零错误。
- 本机未安装 Doxygen；CMake `docs` target 只输出安装提示。远端
  [Documentation build/deploy](https://github.com/lusipad/plcopen/actions/runs/30060185662)
  已完成 Doxygen/API 拼装、24 对页面合同与 Pages 原子推送。
- [PR #21](https://github.com/lusipad/plcopen/pull/21) 的 Documentation、
  Windows、Linux 与 Twin integration 全部通过后合入 `main`；合并提交
  `e944982` 的 [Windows](https://github.com/lusipad/plcopen/actions/runs/30060185658)、
  [Linux](https://github.com/lusipad/plcopen/actions/runs/30060185669)、
  [Twin integration](https://github.com/lusipad/plcopen/actions/runs/30060185653)
  与 Documentation 主线运行全部成功。
- 公网复验：中英文 ST 运行时、标准证据、ADR、修正后的英文实时集成与运维页
  及两份 sitemap 均返回 200；中文 ST 深层页的可见语言选择器准确指向
  `/plcopen/en/references/st-runtime/`，英文页可准确返回中文同页。
