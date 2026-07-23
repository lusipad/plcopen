<title>项目文档</title>

# 项目文档

本节把面向公众的项目文档整合进文档站。每个页面都是权威仓库文档的导读，
不是第二份事实源。

!!! info "权威来源"
    时效性事实、规范合同和政策原文仍以仓库为准。本节每个页面都会回链其
    权威来源。

## 找到正确的文档

| 你要回答的问题 | 从这里开始 | 权威来源 |
|---|---|---|
| 现在能做什么，接下来做什么？ | [状态与方向](status.md) | [STATUS.md](https://github.com/lusipad/plcopen/blob/main/STATUS.md) |
| 内核怎样分层？ | [架构](architecture.md) | [架构原文](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md) |
| 这里的“PLCopen 兼容”具体指什么？ | [合规](compliance.md) | [合规审计](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md) |
| 已声明哪些限制？ | [已知边界](known-boundaries.md) | [KB 注册表](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md) |
| 如何提交变更？ | [参与贡献](contributing.md) | [CONTRIBUTING.md](https://github.com/lusipad/plcopen/blob/main/CONTRIBUTING.md) |
| 谁负责裁决和发布？ | [治理](governance.md) | [GOVERNANCE.md](https://github.com/lusipad/plcopen/blob/main/GOVERNANCE.md) |
| 如何报告漏洞？ | [安全](security.md) | [SECURITY.md](https://github.com/lusipad/plcopen/blob/main/SECURITY.md) |
| 版本之间发生了什么？ | [变更日志](changelog.md) | [CHANGELOG.md](https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md) |

## 哪些内容有意不放进站点导航

`doc/planning/` 保存现行工程计划，`doc/archive/` 保存历史材料，
`doc/compliance/` 保存详细规范矩阵和审计证据。它们仍可在仓库中浏览，但
不会混入面向用户的学习路径。

日常使用从上面的页面开始；需要完整工程记录时，再沿权威来源链接深入。
