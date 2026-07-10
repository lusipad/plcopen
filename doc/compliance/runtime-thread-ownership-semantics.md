# 参考 executor 线程隔离规格 v1（草案）

> 状态：**草案，待维护者批准**。本文只约束参考 executor 的对象生命周期、
> 单写者和跨线程交接，不新增 `AxisModel` / `AxisGroup` 的公共线程安全承诺，
> 不改变 Gear/Cam/Combine、组路径、stream 或 PLCopen FB 的既有命令语义。
>
> 当前 `submit*` 同时包含规划与运行时状态修改，尚无独立的
> `planning -> committed trajectory -> RT` 接口。因此 v1 只能先关闭参考示例
> 中的确定性数据竞争，不能宣称 canonical 规划域与 RT 域已经完成拆分。

## 范围与不变量

| 合同 | v1 要求 |
|------|---------|
| 对象生命周期 | 轴、组及其非 owning 依赖在线程启动前完成绑定；停止请求、producer join、executor drain/join 后才允许销毁或转移所有权 |
| 单写者 | 运行期间仅一个 executor context 可调用会修改 `AxisModel` / `AxisGroup` 的方法；producer 不直接读写这些对象 |
| 公共语义 | 既有合法单线程调用的 Done/Busy/Error/CommandAborted、command id、回放输出和命令仲裁保持不变 |
| RT 边界 | `cycle()` 仍须满足零分配、零阻塞锁、无异常、无墙钟读取；v1 不把包含 `submit*` 规划开销的整个 demo 循环声明为硬 RT |
| 分层 | 线程、等待和 wall-clock 调度仅存在于 demo/executor；核心对象不创建线程 |

## v1 决策

| # | 决策点 | v1 约束 |
|---|--------|---------|
| 1 | 控制命令去程 | 单 producer 通过固定容量 `SpscQueue<GroupCommand>` 向唯一 executor 交接自包含命令；载荷不得含需要异步保活的裸指针、view 或引用 |
| 2 | 命令执行 | executor 在不与 `cycle()` 并发的边界调用现有核心 API。该调用可含规划开销，属于当前 API 的已知限制，不等同于最终 RT 提交通道 |
| 3 | 队列满载 | `push` 失败必须计数并显式报告；不覆盖尚未消费的控制命令，不静默丢弃 |
| 4 | 状态回程 | executor 将完整、不可变的 `GroupSnapshot` 推入固定容量 SPSC；单 reader 只读取已发布对象，不以版本复验包装普通 payload 并发读写 |
| 5 | 快照满载 | 参考 demo 可丢弃待发布快照并计数，但不得覆盖正在读取的槽位；reader 后续读取下一份完整快照 |
| 6 | 消费预算 | 若每个边界最多消费 `B` 条控制命令，入队前深度为 `D`，最坏接纳延迟上界为 `ceil((D + 1) / B)` 个边界；不得笼统承诺所有命令下一拍执行 |
| 7 | 关闭顺序 | producer 停止入队并 join；executor 消费已接纳命令、发布最终快照后退出；随后才销毁队列载荷及核心对象 |

## 明确排除

- 不新增 `standalone/group_path/sync/stream` owner enum 或拒绝矩阵。
- 不改变组命令遇成员同步时的既有接管/清除规则，也不改变 KB-019。
- 不给 CombineAxes 新增“必须同组”前置条件。
- 不把 trajectory stream 的高频 target/frame 放入“不丢命令”控制队列；其
  keep-latest 语义继续以 `trajectory-stream-semantics.md` 为准。
- 不在 v1 异步化 PLCopen FB 门面；command ack/result、request correlation、
  queued/sync phase 快照属于后续独立设计。
- 不声称参考 demo 已实现规划域产出 committed trajectory、RT 域仅 O(1)
  消费的最终架构。

## 验收

| 指标 | 门槛 |
|------|------|
| 直接对象访问 | producer 线程中无 `AxisModel` / `AxisGroup` 读写；所有写操作只在 executor context |
| 命令交接 | 正常 smoke 中 `commands_consumed == commands_enqueued > 0`，队列满载为 0；独立容量测试能观察到失败且原队列内容不变 |
| 状态交接 | 读取数大于 0；每帧字段来自同一 executor 边界；无普通 payload 跨线程读写 |
| 生命周期 | 所有线程 join 后才读取最终核心状态和销毁对象；关闭时无悬空载荷 |
| ThreadSanitizer | 支持 TSAN 的 Linux 工具链下压力场景 0 data race；本地工具链不支持时必须明确记录缺口 |
| 原有门禁 | focused smoke、分配守卫、黄金回放、RT 静态扫描与全量 CTest 全绿 |
| 声明边界 | demo 文档与注释明确其为单写者交接参考，不把 `submit*` 所在循环描述为 canonical 硬 RT 管线 |

## 后续独立设计项

下列工作会影响架构或公共语义，必须另立 ADR/语义矩阵，不并入 v1：

1. 规划域生成 committed trajectory，RT 域只消费有界预计算结果；
2. command ack/result 与 request/command id 的异步合同；
3. trajectory stream 专用 keep-latest mailbox；
4. 完整 setpoint producer taxonomy 与组/成员 takeover 语义。

---

*草案创建：2026-07-10；本版按现有 Gear/Cam/Combine、Group 与 stream 语义复核后收窄。*
