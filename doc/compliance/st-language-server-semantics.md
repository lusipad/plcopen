# D1 ST Language Server 语义矩阵 v1

> 状态：**维护者已批准（2026-07-24）**。
>
> 批准范围：本矩阵全部 16 项 v1 决策，以及
> `vscode-languageclient 10.1.0` production tree 中 `semver`（ISC）与
> `minimatch`（BlueOak-1.0.0）的许可证例外。批准不包含 Marketplace、
> PyPI、Tag 或 GitHub Release 发布。
>
> 设计依据：[long-term-plan T40](../planning/long-term-plan.md)、
> [软件极致计划 D1](../planning/software-excellence-plan.md)、
> [ST L0 编译器合同](st-l0-semantics.md)、
> [LSP 3.18](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.18/specification/)
> 与
> [VS Code Language Server 指南](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)。
> 本矩阵定义加载/工具域，不改变字节码、VM、运动输出或 RT 周期路径。

## 定位与不变量

D1 v1 把既有容错 ST 前端和权威类型/FB 清单接成一个可复用 Language
Server，再用薄 VS Code 扩展启动并连接它。首批只承诺一个打开文档内的多
POU 工程，不把尚未存在的跨文件工程模型藏进实现。

| 既有合同 | 保持 |
|---|---|
| ST 语言权威 | 文法、类型、标准函数、134 个 FB 与引脚继续来自 `core/st` 及生成清单；扩展和 Python server 不复制第二套语义 |
| 编译与运行 | `st::compile()` 产物、稳定 `DiagCode`、字节码版本、VM budget 与 `scan()` 行为不变 |
| T40 容错 | 半成品源码仍产生多条诊断；索引失败不得使 server 崩溃或退回旧源码冒充当前结果 |
| T40 增量 | 编辑器按 LSP 增量变更同步；分析索引以 POU 为缓存单位，只重建变化 POU；跨 POU 语义检查允许重跑整个当前文档 |
| 大小写 | 标识符按 ST 规则大小写不敏感；展示保留声明拼写 |
| RT 边界 | Language Server、Python、JSON-RPC、文件/进程与所有索引分配只在加载/工具域 |
| 依赖边界 | core、默认 CMake consumer 与 `pyplcopen` 运行依赖不增加第三方库 |
| 发布边界 | 本批可构建本地 VSIX/CI artifact；不自动发布 Marketplace、tag、GitHub Release 或 PyPI |

## 决策点

| # | 决策 | v1 语义 | 理由 |
|---|---|---|---|
| 1 | 结构 | 新增加载域 `st::LanguageDocument`，复用编译器、lexer、类型表、标准函数与 binding manifest；pybind 只暴露 server 所需的私有桥；`plcopen_lsp` 用 Python 标准库实现 JSON-RPC/LSP；VS Code 扩展只启动 client | 保持 C++ 前端为唯一语言权威，同时避免在 core 引入 JSON 依赖 |
| 2 | 文档范围 | 每个打开 URI 独立分析；一个文档可包含 TYPE/GVL/多个 FUNCTION、FUNCTION_BLOCK、PROGRAM 与 SFC；定义只返回同一 URI | 当前编译合同已有单文本多 POU，尚无跨文件工程/导入语义 |
| 3 | 协议 | stdio 上的 LSP 3.18 子集：`initialize/initialized/shutdown/exit`、`didOpen/didChange/didClose`、`publishDiagnostics`、`completion`、`definition`、`hover`；未知 request 返回 `MethodNotFound` | 精确覆盖 D1 四项能力，不自创 RPC |
| 4 | 同步与位置 | advertise `TextDocumentSyncKind.Incremental`；URI 视为 opaque，打开后只信客户端文本；版本必须单调增加；位置固定协商 `utf-16`，LSP range 为零基、尾端排他 | VS Code/LSP 的必支持基线；不从磁盘覆盖未保存内容 |
| 5 | 变更原子性 | 一个 `didChange` 的 changes 按序作用，省略 range 表示替换全文；任一 range 越界、切开 UTF-16 surrogate 或版本陈旧时，整批不提交，保留旧文档并发 `window/logMessage` | notification 无响应通道，保守做法是拒绝损坏而非部分应用 |
| 6 | 诊断 | open/change 后对当前未保存文本运行权威编译诊断并 push 当前 version；`source=plcopen-st`，`code=to_string(DiagCode)`；`warning_program_unmapped` 为 Warning，其余为 Error；close 发布空数组 | 稳定码可供机器消费，版本可防旧结果覆盖新编辑 |
| 7 | 诊断 range | C++ 一基行/UTF-8 字节列转换为 LSP UTF-16；已知 token 覆盖完整 token，只有点位置时覆盖一个 Unicode scalar，行尾用空 range；缺失/越界位置钳到文档起点或行尾 | 现有诊断只有点位置，不能伪造更宽的错误区间 |
| 8 | POU 增量索引 | fragment cache 键为“单位种类 + 小写名称 + 内容哈希”；range 相对 fragment 保存，重排/前方插入后重新投影绝对位置；更新报告公开 `reparsed_pous/reused_pous` 供门禁验证 | 真正证明编辑一个 POU 不重建其余 POU 的工具索引 |
| 9 | 补全 | 普通位置返回当前 POU 可见变量/参数、GVL、用户 POU/类型、内置类型/关键字、标准函数、注册转换函数与已声明 FB；`instance.` 只返回解析到的用户/标准 FB 引脚；大小写去重，当前作用域优先；最多 8192 项，截断时 `isIncomplete=true` | 提供有语义的基本补全，同时让客户端负责前缀过滤 |
| 10 | 定义 | 对当前 token 按“当前 POU 局部/参数 → GVL → 用户 POU/类型”解析；用户 FB 引脚可跳到其声明；同优先级多定义或无源码的内置项返回 `null` | 不在无效工程中猜测目标，不为内置表伪造虚拟文件 |
| 11 | 悬停 | 用户符号显示声明种类、保留拼写与规范化类型/POU 签名；内置类型、标准函数和标准 FB/引脚显示权威清单可证明的名称、方向与类型；未知/歧义返回 `null` | 首批只展示结构化事实，不写一套可能漂移的教程文案 |
| 12 | 容错查询 | 即使权威编译失败，索引仍从可辨识 fragment/token 提供补全、定义和悬停；当前不完整 fragment 不复用其旧符号冒充现状 | 编辑态可用，但不隐藏已删除或改坏的声明 |
| 13 | 容量 | header 最多 8 KiB、单条 JSON-RPC body 最多 32 MiB、最多同时保留 64 个打开文档、单文档 UTF-8 最多 4 MiB；文档超限只发布 `capacity_document_bytes` / `capacity_open_documents` 诊断，查询返回空 | server 内存和响应规模必须有界；32 MiB 可容纳 4 MiB 文本的最坏 JSON 转义 |
| 14 | Python/VS Code 接线 | wheel 增加零第三方 Python 依赖的 `plcopen_lsp` 包；扩展用设置指定 Python，运行 `python -m plcopen_lsp`，缺模块时显示可操作错误；不自动采用 workspace 内解释器 | 避免在不可信仓库中静默执行 `.venv` |
| 15 | 扩展依赖与许可证门 | 提案固定 `vscode-languageclient 10.1.0`（runtime）与 `@vscode/vsce 3.9.2`（dev/package）两个直接依赖并提交 lockfile。两项直接依赖为 MIT；当前 production tree 另含 `semver`（ISC）与 `minimatch`（BlueOak-1.0.0），超出 `PROVENANCE.md` 现行 MIT/BSD/Apache 自动允许清单，**必须由维护者显式批准 ISC/BlueOak 后才可实施**。不再增加其他直接 npm/Python/C++ 依赖 | 官方 client 避免自写易错 LSP 适配，但传递许可证不能被顶层 MIT 标签掩盖 |
| 16 | Workspace Trust | untrusted workspace 只注册 `.st` 语言配置，不启动 Python 进程；server 不访问网络、不执行 ST 或 workspace 命令、不读取未打开 URI | 工具不把打开陌生仓库变成代码执行入口 |

## 退化、拒绝与错误规则

| 条件 | 行为 |
|---|---|
| `initialize` 前收到文档请求 | JSON-RPC `ServerNotInitialized` |
| `shutdown` 后收到非 `exit` request | JSON-RPC `InvalidRequest` |
| header 缺失/非法、非 UTF-8 body、坏 JSON 或 batch | 按 LSP/JSON-RPC 返回 Parse/Invalid Request；进程保持可服务 |
| header 超过 8 KiB 或 body 超过 32 MiB | 记录协议错误并终止连接，不分配声明大小的 body |
| 文档版本陈旧或重复 | 忽略整批变更并写 Error log；不发布带错误版本的诊断 |
| 增量 range 越界或切开 surrogate pair | 忽略整批变更并写 Error log；旧文本、版本、索引保持原子不变 |
| 文档或打开数超限 | 发布稳定 capacity 诊断；不截断源码、不驱逐其他文档 |
| 编译失败 | 发布全部有界诊断；容错索引查询仍基于当前文本 |
| 定义/悬停无法唯一解析 | 返回 `null`，不取“第一个看起来像”的声明 |
| 补全超过 8192 项 | 按当前作用域、用户符号、权威内置项的稳定顺序截断，并置 `isIncomplete=true` |
| Python/`plcopen_lsp` 不可启动 | 扩展输出通道和错误通知给出解释器/安装命令；不退回扩展内置伪解析 |
| ISC 或 BlueOak-1.0.0 未获维护者明确批准 | 不安装/提交 npm lockfile，不开始扩展实现；可改送旧 client 或无 VS Code client 的新矩阵，但不得静默放宽出处清单 |
| 不可信 workspace | 不 spawn server；语言标识和基础括号/注释配置仍可用 |
| 文档关闭 | 删除内存文本/索引并发布空 diagnostics |

## 验收指标

| 指标 | 门槛与证明 |
|---|---|
| 协议生命周期 | 原始 Content-Length transcript 覆盖 initialize→open/change→四项能力→close→shutdown/exit，响应 id/错误码/能力精确 |
| 增量同步 | 含 emoji、CRLF 与多 change batch 的 UTF-16 worked examples 精确还原文本；陈旧/坏 range 整批原子拒绝 |
| POU 缓存 | 三 POU 文档只改中间 POU 时 `reparsed_pous=1`、`reused_pous=2`；在前方增删文本后复用 POU 的 definition range 仍准确 |
| 诊断 | 半成品源码发布当前 version、多条稳定 `DiagCode` 与合法 UTF-16 range；修复后清空；close 再清空 |
| 补全 | 变量/GVL/用户 POU/类型、51 个标准函数、注册转换函数、已声明 FB 与 `instance.` 引脚各有正例；大小写重复为零；越界走显式 incomplete |
| 定义 | 局部遮蔽 GVL、跨 POU 调用、用户 FB pin 和前方文本位移均命中精确 selection range；歧义/内置返回 null |
| 悬停 | 用户变量/POU、标准函数、标准 FB pin 的 kind/type/direction 与权威清单一致；坏源码仍可查询可辨识项 |
| 容量与安全 | 8 KiB header、32 MiB body、4 MiB 文档、64 文档均有 N/N+1；server 不读未打开 URI、不联网、不执行 ST；untrusted workspace 不 spawn |
| 扩展 | 维护者许可证批准有记录；production tree 生成完整 package/version/license/integrity 清单和第三方 notices 草案；`npm ci`、Node 单测/manifest 检查、`npm audit --omit=dev` 与 `vsce package` 生成可安装 VSIX，并证明 VSIX 不含 `@vscode/vsce` dev tree；file/untitled `.st` 均连接同一 client |
| 平台 | Windows/Linux Python binding + LSP transcript + VS Code package workflow 全绿 |
| 回归 | 全量 CTest、ST fuzz smoke、RT scan、18 份 replay 零差异、双语 MkDocs strict/i18n 通过 |

## 不做

- 不做跨文件/跨 workspace 符号、文件 watcher、import/library 路径或多 root；
- 不做 references、rename、formatting、signature help、semantic tokens、代码操作、
  snippets、调试适配器或在线 PLC 连接；
- 不做 TextMate/ST 语法高亮复制；扩展首批只登记语言、基础括号/注释与 LSP；
- 不把 `LanguageDocument`、Python server 或 JSON-RPC 放入 RT 周期；
- 不宣称完整 IEC IDE、CODESYS/TwinCAT 工程兼容或 PLCopen 官方认证；
- 不发布 Marketplace、Open VSX、PyPI、tag 或 GitHub Release。
