# ST Language Server 与 VS Code

D1 为 PLCopen Structured Text 提供诊断、补全、定义跳转与悬停。C++ 前端
继续作为唯一语言权威；零第三方运行依赖的 Python 包通过 stdio 提供 LSP
3.18 server，薄 VS Code 扩展只连接官方 language client。

!!! warning "当前仅提供源码快照"
    D1 尚未进入公开的 `pyplcopen==0.20.0`，VS Code 扩展也未发布到商店。
    请从同一份仓库 checkout 构建 wheel 和 VSIX，或从成功的 Language Tools
    workflow 下载 VSIX artifact。

## 环境要求

- Python 3.10+、C++17 构建工具链与 CMake
- 本地打包 VSIX 需要 Node.js 20+
- VS Code 1.91+

## 构建并安装 server

建议使用隔离解释器，然后在仓库根目录构建 wheel：

```bash
python -m pip install build
python -m build --wheel --outdir dist
python -m pip install --force-reinstall --no-deps dist/pyplcopen-*.whl
```

PowerShell 不依赖 shell glob，可显式解析 wheel：

```powershell
$Wheel = Get-ChildItem dist -Filter 'pyplcopen-*.whl' | Select-Object -First 1
python -m pip install --force-reinstall --no-deps $Wheel.FullName
```

确认同一个解释器同时包含两部分：

```bash
python -c "import pyplcopen, plcopen_lsp; print(pyplcopen.__file__)"
```

server 通常由编辑器启动。直接入口是 `python -m plcopen_lsp`，只通过
stdin/stdout 上带 `Content-Length` 的 LSP frame 通信。

## 构建并安装 VSIX

```bash
cd editors/vscode
npm ci --ignore-scripts
npm run verify:licenses
npm test
npm audit --omit=dev
npm run package
npm run verify:vsix
```

在 VS Code 中运行 **Extensions: Install from VSIX...**，选择
`plcopen-st-language-tools.vsix`。把 machine-scope 设置
`plcopenSt.pythonPath` 指向上一步使用的准确解释器；扩展不会自动发现或
执行工作区 `.venv`。

打开 `.st` 文件，或把 untitled 文档的语言模式设为 **PLCopen ST**。两种
scheme 都连接同一个 language client。

## Trust 与失败行为

- untrusted workspace 仍登记 `.st`、注释和括号，但扩展不创建 Python 进程；
- workspace 获得 trust 后才启动 `<python> -m plcopen_lsp`；
- 模块缺失或解释器无效会写入 PLCopen ST output channel，并弹出带 wheel
  安装命令的错误通知；
- server 把 URI 当作 opaque，只分析客户端发送的文本；不读取未打开文件、
  不访问网络、不执行 ST，也不运行 workspace command。

## v1 范围

一个打开文档可包含类型、全局变量、函数、功能块、程序与 SFC。源码未完成时
仍发布诊断，容错索引继续返回可以证明的符号。

首版不提供跨文件工程、workspace symbol、references、rename、formatting、
semantic token、调试器、在线 PLC 连接或 TextMate grammar；定义跳转不会
离开当前 URI。

准确的协议、UTF-16 变更、容量、歧义和依赖规则见
[D1 语义矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-language-server-semantics.md)。
