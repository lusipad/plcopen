# D1 ST Language Server 实施计划

规格：[st-language-server-semantics.md](../compliance/st-language-server-semantics.md)

## 1. 最可能调整的决策

### 1.1 v1 是单文档多 POU，不是跨文件工程

```text
URI A ──► LanguageDocument A ──► diagnostics/completion/definition/hover
URI B ──► LanguageDocument B ──► 独立结果，不与 A 合并
```

置信度：高。仓库当前的事实源是“一个 source string 编译一个多 POU
project”，没有 import、库路径或跨文件诊断归属合同。只有维护者要求 D1
首批即支持真实工程目录时才翻转；那会先需要独立工程模型矩阵。

### 1.2 语言权威留在 C++，协议薄壳用 Python 标准库

```text
VS Code
  └─ vscode-languageclient ──stdio/LSP 3.18──► plcopen_lsp (Python)
                                                  └─ pybind
                                                     └─ st::LanguageDocument
                                                        ├─ st::compile
                                                        ├─ Lexer/type tables
                                                        └─ binding manifest
```

置信度：高。它不在 core 引入 JSON/Node 依赖，也不在扩展重写 ST。只有
wheel 无法可靠携带纯 Python server 或 pybind 往返成为已测瓶颈时才改成
独立原生 server；先不承担自写 C++ JSON-RPC 的复杂度。

### 1.3 “增量”分成编辑同步、POU 索引和全局语义三层

- LSP 文本按 UTF-16 range 增量同步；
- 只有内容变化的 POU 重建索引，缓存范围相对 fragment 保存；
- 为保证跨 POU 正确性，`st::compile()` 的全局语义检查 v1 可重跑当前文档；
  不宣称增量字节码编译。

置信度：高。这是 T40 “POU 粒度够用且简单”的最小诚实实现。只有 Release
实测证明全局语义检查阻塞交互，才把 compiler AST/sema 缓存列为后续优化，
不能先用无数据的大重构替代 D1。

### 1.4 四项能力采用保守解析

| 能力 | keep/adapt/drop |
|---|---|
| 当前未保存文本的 push diagnostics | **keep**，直接映射稳定 `DiagCode` 并带 version |
| 客户端前缀过滤的基本 completion | **keep**，返回当前可见用户符号、标准函数/转换和权威内置清单 |
| `instance.` 引脚 completion | **adapt**，只在实例类型能唯一解析时提供 |
| 当前文档 definition | **keep**，局部优先于 GVL；歧义和内置项返回 null |
| 结构化 hover | **keep**，只展示声明/清单能证明的 kind/type/direction |
| workspace symbols/references/rename | **drop**，需要跨文件工程模型 |
| 官方 Node server 示例结构 | **adapt**，保留独立进程/client，server 改为 Python+C++ 权威前端 |

置信度：高。上述边界与 D1 原始“诊断/补全/跳转/悬停”逐项对应。

### 1.5 扩展允许两个固定 npm 依赖，但不扩大产品发布

`vscode-languageclient 10.1.0` 是唯一 runtime npm 依赖；
`@vscode/vsce 3.9.2` 只用于测试/打包。两者 lock、license、audit 与 VSIX
进入 CI；core/default wheel 无新运行依赖。扩展不可信 workspace 不启动
Python，也不自动选 workspace `.venv`。

置信度：中。官方 client 显著降低协议适配错误；如果维护者坚持零 npm
依赖，D1 应改为“只交付通用 Language Server、不交付 VS Code client”，
而不是在扩展里手写一套 language client。

## 2. 假设

| 假设 | 置信度 | 来源 / 什么会推翻 |
|---|---|---|
| D1 的验收面就是诊断、补全、定义、悬停 | 高 | 软件极致计划 D1；新增 rename/format 等会扩规格 |
| 单文本多 POU 足够首批真实试用 | 中 | 当前 `compile(source)`/`IncrementalCompiler`；真实用户工程必须跨文件会推翻 |
| Python 3.10+ 可作为 wheel 与扩展之间的 host | 高 | `pyproject.toml` 既有支持范围；无 Python 的安装目标会推翻 |
| UTF-16 固定协商覆盖 VS Code 与通用 LSP client | 高 | LSP 3.18 必支持基线 |
| 当前编译诊断可在编辑事件后全量运行 | 中 | 前端为加载域且容量有界；Release 观测若出现可见卡顿会触发下一批优化 |
| `.st` 是首批唯一默认扩展名 | 高 | 现有文档/示例；用户提出其他既有工程扩展名时再登记 |

## 3. 偏差政策

保守选择 = 不猜跨文件语义、不复制语言清单、歧义返回空、坏增量整批拒绝、
不执行 workspace 内容、保持 core/VM/字节码不变，并把新行为留在加载/工具
域。实现中的小型可逆偏差即时写入实施记录后继续。

若发现必须改变 ST 编译语义/字节码、引入第三个依赖、执行 workspace 命令、
读取未打开文件、支持网络监听或扩展到跨文件工程，停止并重新批准矩阵；第三
个实质偏差触发重新 kickoff。

## 4. 机械工作（低评审价值）

1. 新增 `core/st/language.h` 与 C++ 定向测试，公开有界 POU cache、符号/查询
   结果和更新报告。
2. 在 `python/pyplcopen.cpp` 加私有桥，并把零依赖 `plcopen_lsp` 纯 Python
   包装入 wheel。
3. 实现 Content-Length/JSON-RPC 生命周期、UTF-16 增量文档、四项 handler
   与原始 transcript 测试。
4. 在 `editors/vscode/` 增加语言登记、基础配置、官方 client、trust/解释器
   设置、Node 测试、lockfile 与 VSIX 打包。
5. 增加 Windows/Linux Language Tools workflow；同步双语指南、导航、CI
   总账、KB-092、CHANGELOG、STATUS/ROADMAP 与实施记录。

## 5. 验证

- 先写 C++ 索引、Python transcript、UTF-16 change、扩展启动/manifest 的失败
  测试，确认失败点分别是缺 API/handler/package；
- 展示一个未保存且语法未闭合的 `.st` 文档：诊断带稳定码，同时 completion、
  definition、hover 仍基于当前可辨识符号工作；
- 展示三 POU 文档只改一处后 `1 reparsed / 2 reused`，且前方插入 emoji 后
  UTF-16 definition range 仍准确；
- 本地 C++ 定向/全量 CTest、Python unit/integration、Node/package、ST fuzz、
  RT scan、replay、MkDocs strict/i18n 全绿；
- PR Windows/Linux/Language Tools/Documentation 及合并后主线终态全绿；
  VSIX 作为 CI artifact 可安装，但不发布商店。

## Handoff

矩阵批准后立即把批准日期与范围写回规格，再按测试先行实施；全程维护
[d1-st-language-server-implementation-notes.md](d1-st-language-server-implementation-notes.md)，
偏差即时记录，不在收尾时补写。
