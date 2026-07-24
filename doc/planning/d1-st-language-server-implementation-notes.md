# 实施记录 — D1 ST Language Server

计划：[d1-st-language-server-plan.md](d1-st-language-server-plan.md)

## 摘要

语义矩阵已于 2026-07-24 获维护者批准。C++ `st::LanguageDocument`、
Python 标准库 stdio server、VS Code 薄客户端、许可证证据和本地 VSIX
均已完成；[PR #26](https://github.com/lusipad/plcopen/pull/26) 已合并，
合并后的五条主线工作流全绿，中英文用户指南已上线。Marketplace、PyPI、
tag 与 GitHub Release 均未授权、未执行。

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
- VS Code 扩展使用纯 JavaScript，唯一 production 直接依赖固定为
  `vscode-languageclient 10.1.0`；`@vscode/vsce 3.9.2` 只用于开发打包，
  不为单一入口再增加 TypeScript/测试框架依赖。
- `plcopenSt.pythonPath` 是 machine-scope 设置；扩展只把该解释器作为
  executable 直接启动，不寻找或采用工作区 `.venv`。untrusted workspace
  只保留语言/括号/注释配置，收到 trust grant 后才创建 client。
- production 许可证清单由 lockfile 与实际安装树生成；ISC/BlueOak 例外
  只能分别用于 `semver`/`minimatch`，不能被未来其他依赖复用。
- 独立 Language Tools workflow 在 Windows/Linux 同时编译 binding、运行
  原始 transcript、构建并干净安装 wheel，再用 Node 24 从固定 lockfile
  测试、审计、打包和检查 VSIX；两平台 artifact 保留 14 天。

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
- 当前 lockfile 将 production 传递项固定为 9 个包，其中
  `semver 7.8.5` 为 ISC、`minimatch 10.2.5` 为 BlueOak-1.0.0；其余均为
  MIT。版本、SPDX、integrity 与许可证全文已生成清单/notices 草案。
- `vsce package` 会提示扩展根目录没有独立 LICENSE，以及未 bundle 的
  393 文件性能建议。前者按人专属法律文件规则不由本批复制/新建，后者若
  处理将需要第三个直接构建依赖；两者均不阻止本地 VSIX 构建与安装。
- 公开 `pyplcopen==0.20.0` 不含尚未发布的 D1 server；扩展失败提示和双语
  指南因此都要求安装“当前 checkout 构建的 wheel”，不把公开旧包误写成
  可用修复。公共 PyPI/Marketplace 仍未获授权。

## Verification

- Python：`ruff check`、`ruff format --check` 和 15 项 LSP/bridge 单元测试
  通过，覆盖 UTF-16/CRLF/emoji、多 change 原子性、版本、容量、协议错误、
  四类查询、真实 pybind 与真实模块 stdio 入口。
- CTest：`pyplcopen_smoke` 与 `pyplcopen_language_tools` 通过。
- Wheel：本地 CPython 3.14 Windows wheel 构建通过，并核对同时包含
  `plcopen_lsp/{__init__,__main__,server}.py` 与 `pyplcopen` `.pyd`；
  安装到干净 target 后 5 项真实 bridge/stdio 测试通过；Language Tools CI
  的 Windows/Linux wheel 构建与干净 target 安装也均通过。
- VS Code：先确认 4 项 manifest/trust/startup 测试全红，再实现至全绿；
  `npm ci --ignore-scripts`、许可证清单 freshness、`npm audit --omit=dev`
  零漏洞与 `vsce package` 均通过。
- VSIX：生成 683.92 KiB / 393 文件的本地 artifact；归档检查证明 9 个
  production package 与 inventory 的名称/版本/许可证完全一致，且不含
  `@vscode/vsce`、任何其他 dev tree、测试、脚本、lockfile 或 `.bin`。
- Windows Debug：全新配置构建后 98/98 CTest 通过，包含 11 项 fuzz smoke
  与新增 `plcopen_core_st_language_tests`；RT safety scan 31 文件通过，
  replay fixture 18 文件/2409 样本零差异。
- 安装态 C++：安装 `st/language.h` 后，独立 `find_package` ST consumer
  成功实例化/复用 `LanguageDocument`，再完成既有 ST 编译、绑定和运动。
- 文档：中英文 MkDocs strict 与 i18n 26 对页面通过；新增同路径
  `guides/st-language-server/`，明确当前源码快照、trust 与未发布边界。

## 远端证据

- [PR #26](https://github.com/lusipad/plcopen/pull/26) 已合并到主线提交
  `b91ca3d6dc90509c1cc17f7178fd5b0696b9405c`。
- 合并后的
  [Windows](https://github.com/lusipad/plcopen/actions/runs/30104753037)、
  [Linux](https://github.com/lusipad/plcopen/actions/runs/30104753119)、
  [Language Tools](https://github.com/lusipad/plcopen/actions/runs/30104753368)、
  [Twin](https://github.com/lusipad/plcopen/actions/runs/30104753300) 与
  [Documentation](https://github.com/lusipad/plcopen/actions/runs/30104753259)
  工作流均为 `success`。
- [中文指南](https://lusipad.com/plcopen/guides/st-language-server/)、
  [英文指南](https://lusipad.com/plcopen/en/guides/st-language-server/)与两份
  sitemap 均已公网复验为 200。

## Questions for review

- 无。
