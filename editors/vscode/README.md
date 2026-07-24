# PLCopen ST Language Tools

此扩展为 `.st` 文件提供诊断、补全、定义跳转与悬停。扩展本身不实现第二套
ST 语义，而是通过 stdio 启动随 `pyplcopen` wheel 提供的 Language Server。

1. 为目标 Python 安装当前项目构建的 `pyplcopen` wheel。
2. 如 `python` 不是目标解释器，在 VS Code 机器级设置
   `plcopenSt.pythonPath`。
3. 打开文件或 untitled 的 PLCopen ST 文档。

不可信工作区只启用 `.st` 语言登记、括号和注释配置，不启动 Python 进程。
首版按单文档多 POU 分析，不提供跨文件工程、rename、formatting、调试或在线
PLC 连接。
