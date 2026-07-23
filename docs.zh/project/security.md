<title>安全</title>

# 安全

plcopen 是可嵌入的运动控制内核与 IEC 61131-3 ST 运行时，不是安全沙箱、
安全 PLC、网络服务或经过功能安全认证的产品。

## 支持范围

- `main` 与最新发布版按 best-effort 接受安全修复。
- 更早版本不保证回移植。
- 项目没有 LTS、响应 SLA 或付费安全支持。

## 报告漏洞

不要在公开 issue、Discussion 或 Pull Request 中发布 exploit、PoC 细节、
凭据、真实设备数据或敏感日志。

在仓库启用 GitHub Private Vulnerability Reporting 之前，新建标题为
`[Security contact request]` 的公开 issue，只填写受影响组件与粗粒度影响
类别；维护者提供渠道后再私下继续。

准确的当前流程和信任边界维护在
[权威安全政策](https://github.com/lusipad/plcopen/blob/main/SECURITY.md)。

## Safety 边界

软件安全、实时预算和机械/电气安全属于不同保证域。通过 parser 测试、WCET
门禁或运动仿真，并不能证明功能安全认证或真实机器集成安全。
