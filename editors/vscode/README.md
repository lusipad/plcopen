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

## 依赖安全与本地客户端适配

开发验证顺序：`npm ci --ignore-scripts` → `npm test` →
`npm run verify:licenses` → `npm audit --omit=dev` → `npm run package` →
`npm run verify:vsix`。`npm test` 和打包都会显式执行 `prepare:client`；
不启用第三方安装脚本。仅安装依赖而未执行准备步骤的源码目录不能直接运行扩展。

当前 `vscode-languageclient@10.1.0` 的 `minimatch` 被定向锁定到 `11.0.1`，
避免影响开发工具自己的依赖。Minimatch 11 删除 `makeRe()`，因此不能只做
版本覆盖：`client-compatibility.json` 记录两个客户端文件的原始/适配后
SHA-256，准备脚本仅将原有正则编译检查替换为空模式集合检查，实际匹配仍由
Minimatch 执行。不生成正则、不增加全局猴子补丁；未知内容或版本立即拒绝。
空模式和注释模式仍被拒绝。Minimatch 11 自身的路径匹配修复随升级生效。

回归测试载入真实客户端 glob 与文件事件过滤代码，只替换 VS Code 宿主 API；
覆盖相对模式、Windows 路径、大小写、隐藏文件、scheme、空模式和注销。
VSIX 验证检查最终归档中两份适配文件的哈希及完整生产依赖集合（含嵌套覆盖）。
这不是完整的 VS Code UI 端到端测试。上游客户端正式支持安全版本后应删除
本地适配，并重新验证依赖、许可证和归档。
