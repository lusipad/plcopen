# 实施记录 — D1 ST Language Server

计划：[d1-st-language-server-plan.md](d1-st-language-server-plan.md)

## 摘要

语义矩阵已于 2026-07-24 获维护者批准。C++ `st::LanguageDocument`、
Python 标准库 stdio server、私有 pybind bridge 及其定向测试已实现；
VS Code client、许可证证据、CI 和用户文档仍待完成。

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
- Python 层独占 URI、单调版本和多 change 原子应用；C++ bridge 只收最终
  全文并返回 Python 内建类型，避免 pybind 暴露 JSON-RPC/LSP 对象。
- stdio framing 固定 8 KiB header、32 MiB body；单文档 UTF-8 固定
  4 MiB、最多 64 个打开文档。URI 保持不透明，不读工作区文件、不访问
  网络，也不执行 ST。
- Python wheel 显式打包 `plcopen_lsp` 顶层包；同一 wheel 同时携带
  `pyplcopen` 扩展和 `python -m plcopen_lsp` 入口。

## Deviations

- 无。

## Surprises

- `BindingPinDesc::lower_name` 当前实际保存生成清单的规范拼写（例如
  `Execute`），不是小写文本；工具查询因此使用 ST 大小写不敏感比较，
  展示仍取生成清单拼写。
- 本机 Visual Studio 安装可用 MSVC，但未安装 ClangCL platform toolset；
  C++ 层已用 MSVC 和 8 项定向/回归测试验证，Clang/GCC 留给后续 Linux CI。
- CMake `file(GENERATE)` 的 bracket argument 不展开源码目录变量；Python
  测试 runner 改用 quoted `CONTENT` 后，源码包路径与配置态扩展路径均可
  正确注入。

## Verification

- Python：`ruff check`、`ruff format --check` 和 15 项 LSP/bridge 单元测试
  通过，覆盖 UTF-16/CRLF/emoji、多 change 原子性、版本、容量、协议错误、
  四类查询、真实 pybind 与真实模块 stdio 入口。
- CTest：`pyplcopen_smoke` 与 `pyplcopen_language_tools` 通过。
- Wheel：CPython 3.14 Windows wheel 构建通过，并核对同时包含
  `plcopen_lsp/{__init__,__main__,server}.py` 与 `pyplcopen` `.pyd`；
  安装到干净 target 后 5 项真实 bridge/stdio 测试通过；Linux wheel 留给
  后续 CI。
## Questions for review

- 无。
