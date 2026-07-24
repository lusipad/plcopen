<title>标准与证据</title>

# 标准与证据

plcopen 把“名字相同”“软件语义通过”“真机验证”和“正式批准”视为四种不同
强度的声明。本页说明如何从公开功能块入口走到规范、边界和可复核证据。

## 声明阶梯

| 层级 | 能证明什么 | 不能自动推出什么 |
|---|---|---|
| 接口清单 | 功能块和引脚存在 | 行为符合某一标准条款 |
| 语义矩阵 | 输入、状态、错误和边界行为有明确合同 | 真驱动、时钟与安全设备行为 |
| 自动化门禁 | 指定源码和场景在给定平台通过 | 未执行路径、现场可靠性或认证 |
| 真机证据 | 指定台架和配置下的测量结果 | 其他设备、部署或长期支持承诺 |
| 正式批准 | 对应机构或责任方作出的声明 | 超出声明范围的产品能力 |

因此，功能块数量、覆盖率或一次绿色 CI 都不能单独代表 PLCopen 批准。

## 入口地图

| 你要回答的问题 | 从这里进入 |
|---|---|
| 当前公开合规口径是什么？ | [项目合规导览](../project/compliance.md) |
| 某个功能块有哪些输入输出？ | [功能块参考](fb-reference.md) |
| 与 Beckhoff 的核心能力差异在哪里？ | 下方能力对等矩阵 |
| 已知限制是否登记？ | [已知边界](../project/known-boundaries.md) |
| 某条 CI 实际证明什么？ | 下方 CI 门禁矩阵 |
| ST 语言能力是否闭合？ | [ST 运行时](st-runtime.md) |

## 规范问题的阅读顺序

1. 从功能块参考定位 Part、名称与引脚。
2. 进入对应语义矩阵，确认状态机、边界值、错误和明确排除项。
3. 检查已知边界注册表，避免把有意差异当成遗漏。
4. 查看 CI 门禁，确认哪类测试真实执行了该合同。
5. 如果结论依赖现场总线、驱动、安全链或认证，停在软件证据边界。

## 权威来源

- [PLCopen 全面合规审计](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md)
  是跨 Part 声明与认证边界的主入口。
- [PLCopen / Beckhoff 核心能力对等矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)
  使用 implemented / partial / excluded 逐能力裁定。
- [CI 门禁矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/ci-gates.md)
  记录 workflow 的触发条件、证明对象与发布作用。
- [生成的 I/O 声明](https://github.com/lusipad/plcopen/tree/main/doc/compliance/generated)
  提供可复核的机器接口证据。

规范原文、逐项矩阵和机器声明仍以仓库文件为唯一事实源；本页只提供稳定的
阅读路径。
