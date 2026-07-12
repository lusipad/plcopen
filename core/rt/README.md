# L0 rt

`core/rt` 是 L0-L7 阶梯的首层：全核实时基础设施（阶梯全貌与正典架构图见
[core/README.md](../README.md)，审计事实见
[架构综述](../../doc/design/architecture-review-2026-07.md)）。

Responsibilities:

- integer cycle ticks and durations;
- 全核 RT 基础设施所需的定长容器（fixed-capacity containers）;
- single-producer / single-consumer queues;
- small error-code based `Result` values;
- 辅助头：`error_text.h`（错误码到文本，仅供非周期路径/加载域的诊断与日志
  使用）与 `units.h`（`CycleConfig` 周期配置与单位换算，纯 constexpr 值计算，
  周期域可用）。

RT constraints:

- no heap allocation after construction;
- no blocking locks;
- no exceptions or RTTI;
- no OS calls or wall-clock reads;
- no floating-point time accumulation.

依赖与消费者：L0 只依赖对所提供类型不分配的 C++17 标准库设施；L0 不得依赖
PLCopen FB 语义、旧 `src/` 或 adapters。作为阶梯首层（L0-L7 阶梯 + 支撑库
形态见[架构综述](../../doc/design/architecture-review-2026-07.md)），全核所有
上层均可向内依赖本层；直接消费者包括阶梯各层、支撑库 kin（依赖 geom/rt）与
stream（依赖 otg/rt），以及外圈的 st 语言层（消费 `rt/error.h`）与 L7
adapters。
