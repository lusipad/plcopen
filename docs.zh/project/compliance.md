<title>合规</title>

# 合规

plcopen 将接口覆盖、软件语义、硬件证据和正式批准视为不同声明。功能块名称
相同，本身并不能证明合规。

## 当前声明模型

| 范围 | 跟踪内容 | 不代表什么 |
|---|---|---|
| Part 1 | 43 个标准门面及已审计软件语义 | 供应商声明或 PLCopen 批准 |
| Part 4 | 68 个同名门面及明确的部分边界 | 完整逐条款合规 |
| Part 5 | 11 个标准门面及机器可读 B/E 声明 | 硬件真实性或认证 |

详细且带时效性的裁定维护在
[权威合规审计](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md)。

## 证据地图

- [PLCopen / Beckhoff parity matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)
  给出能力级裁定。
- [已知边界](known-boundaries.md) 记录有意差异与不支持组合。
- [生成的 I/O 声明](https://github.com/lusipad/plcopen/tree/main/doc/compliance/generated)
  提供机器可读的接口证据。
- [CI 门禁矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/ci-gates.md)
  说明每条 workflow 实际证明什么。

## 阅读规则

日常集成使用公开指南即可。当行为触及标准边界时，应沿对应功能块进入
`doc/compliance/`，先读规范矩阵与 KB 条目，再依赖该行为。

plcopen 不声称 PLCopen 批准、功能安全认证、硬件真实性验证，也不声称与
厂商产品具有黑盒性能等价性。
