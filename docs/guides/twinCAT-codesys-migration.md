# TwinCAT / CODESYS 概念迁移指南

这不是逐项兼容层。它帮助熟悉 TwinCAT 或 CODESYS 的工程师把概念映射到
plcopen 的 C++/ST 消费面，并指出必须重新验证的边界。

| 既有概念 | plcopen 入口 | 迁移注意 |
|----------|---------------|----------|
| NC/PTP 单轴 | `AxisModel` + `FbMoveAbsolute` | 位置、速度、加速度、jerk 使用每周期单位 |
| Axis reference | `axis::AxisModel*` / ST `AXIS_REF` | 宿主持有生命周期；ST 运行后禁止重绑 |
| Group / coordinated motion | `AxisGroup` + `FbMoveLinear*` | 组是非拥有成员集合，成员必须先于组析构 |
| Circular interpolation | `FbMoveCircular*` | 仅三点 BORDER，CENTER/RADIUS 不支持 |
| BufferMode / blending | `buffer_mode`, `transition_mode` | 未定义组合显式报错，不静默降级 |
| Cam table | `CamTable` / `FbCamIn` | 句柄由调用方持有，spline 是显式 opt-in |
| PLC task scan | `Instance::scan()` 或宿主周期调用 | scan/cycle 预算和周期时间由宿主配置 |
| EtherCAT task / drive | `adapters::Servo` 窄接口 | 本仓库不含 EtherCAT、DC 或总线 IO |

## 迁移步骤

1. 先用 `find_package(plcopen)` 或 `FetchContent` 接入 `plcopen::plcopen`。
2. 把 Scheduler 风格的隐式推进改成显式规划周期，生产系统再接 ADR-0007
   双域执行器。
3. 把旧的百分比 override 换成 `[0,1]` factor；`0` 是受控暂停，不是报错。
4. 把 `mAxis`/`mExecute`/`mDone` 改成 `axis_ref`/`execute`/`outputs.done`。
5. 对每个已迁移场景运行黄金回放，并逐条查看 `known-boundaries.md`。

## 不能直接假设的差异

- Part 1 门面 43/43 不等于 B 级 I/O 或 D 条款合规。
- Part 4 目前 40/68 有同名门面，Part 5 11/11 有 C++ 门面但仍有接口和
  真机语义边界。
- `TIME` 在 ST 中是整数纳秒存储，消费精度由任务周期决定。
- STL 容器、异常、阻塞锁和系统调用不能带进周期路径。
- 任何 EtherCAT、驱动器安全或 SIL/PL 声明都必须由集成商按自己的硬件
  和安全生命周期验证。
