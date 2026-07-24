<title>架构决策</title>

# 架构决策

架构总图说明系统“现在怎样组织”；Architecture Decision Record（ADR）说明
为什么选择这条边界、拒绝了什么，以及未来修改时必须重新验证什么。

先读 [架构导览](../project/architecture.md) 建立层级与数据流，再按问题进入
对应 ADR。

## 当前决策账

| ADR | 决策 | 记录状态 | 对集成者的影响 |
|---|---|---|---|
| 0001 | v0.x 许可证策略 | 提案；人类许可证复核待办 | 当前 Apache-2.0 不变，不把未来双许可当成承诺 |
| 0002 | 新核保持 C++17 | 现行 | 下游不需要 C++20，所有公共消费面共享同一工具链合同 |
| 0003 | Ruckig 不进入默认构建 | 现行边界 | 默认构建零该依赖；外部对拍须单独核验许可证与集成形态 |
| 0004 | `Servo` 窄适配器 | Accepted | L5 不持有硬件对象；executor 在周期边界桥接反馈与 setpoint |
| 0005 | 人形多链模型 | Accepted | 全身走同步关节流，`AxisGroup` 保持单链职责 |
| 0006 | fieldbus 进程模型 | Accepted；许可证附录待人裁决 | 同进程直连为性能默认，IPC 为另一部署形态 |
| 0007 | executor 承诺轨迹 | Accepted | 规划域产帧，RT 域只消费承诺帧和执行 Servo I/O |

## 按任务阅读

- 嵌入 C++ 控制器：先读 0002、0004、0007。
- 接机器人学习或全身控制：读 0005，再回到
  [实时集成](../guides/realtime-integration.md)。
- 接 EtherCAT：读 0006；许可证结论和台架选择仍属于维护者前置。
- 引入外部算法或改变许可证：读 0001、0003，不能从导读页推导法律结论。

## 权威 ADR

- [ADR-0001：v0.x 许可证策略](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0001-v0x-license-strategy.md)
- [ADR-0002：新核保持 C++17](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0002-core-cpp17-standard.md)
- [ADR-0003：Ruckig oracle 边界](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0003-ruckig-oracle-boundary.md)
- [ADR-0004：Servo 适配器接口](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0004-servo-adapter-interface.md)
- [ADR-0005：人形多链执行模型](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0005-humanoid-multi-chain-model.md)
- [ADR-0006：fieldbus 进程模型与许可证边界](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0006-fieldbus-process-model.md)
- [ADR-0007：executor 承诺轨迹](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0007-executor-committed-trajectory.md)

ADR 原文是决策状态、后果和拒绝方案的唯一事实源。本页不替代许可证、认证
或采购裁决。
