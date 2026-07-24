# 实施记录 — D1 ST Language Server

计划：[d1-st-language-server-plan.md](d1-st-language-server-plan.md)

## 摘要

语义矩阵已于 2026-07-24 获维护者批准。C++ `st::LanguageDocument`
及其定向测试已实现；Python LSP、VS Code client、许可证证据、CI 和用户
文档仍待完成。

## Decisions

- 维护者在紧接明确审批文本后回复“继续”，批准
  [D1 v1 语义矩阵](../compliance/st-language-server-semantics.md)全部范围。
- 同时批准 `semver`（ISC）与 `minimatch`（BlueOak-1.0.0）作为 VSIX
  production 传递依赖；仍不授权 Marketplace、PyPI、Tag 或 GitHub
  Release 发布。
- `LanguageDocument` 持有当前全文、权威编译诊断和工具索引；LSP 文档版本
  与多 change 原子应用留在 Python 协议层，避免 C++ API 反向依赖 LSP。
- POU 复用先用“种类 + 小写名称 + FNV-1a + 原文相等”确认；全文词法扫描
  只负责重定位 fragment，未变化 POU 的声明索引不重新解析。

## Deviations

- 无。

## Surprises

- `BindingPinDesc::lower_name` 当前实际保存生成清单的规范拼写（例如
  `Execute`），不是小写文本；工具查询因此使用 ST 大小写不敏感比较，
  展示仍取生成清单拼写。
- 本机 Visual Studio 安装可用 MSVC，但未安装 ClangCL platform toolset；
  C++ 层已用 MSVC 和 8 项定向/回归测试验证，Clang/GCC 留给后续 Linux CI。
## Questions for review

- 无。
