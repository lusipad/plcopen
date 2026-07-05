# 愿景期归档

本目录保存 plcopen 项目早期（v0.1 之前）对"完整 PLC 生态"的设计和规划文档。

## 本目录的性质

**这些文档不是当前路线图**。

| 找当前信息，请看 | 位置 |
|------------------|------|
| 长期方向 | [../../VISION.md](../../VISION.md) |
| 近期承诺 | [../../ROADMAP.md](../../ROADMAP.md) |
| 当前能力 | [../../README.md](../../README.md) |

## 为什么归档而不是删除

这些文档代表了项目早期对"完整 PLC 生态"的思考，其中大部分内容**按当前路线图不做**。但"不做"不等于"错"，它们只是**在错的时间点被写出来**（在没有足够证据支持启动前）。

保留它们的理由：

- 当 [VISION.md 的解锁条件](../../VISION.md#解锁条件) 满足时，相关文档可以作为重启起点
- 了解项目演进历史有价值
- 体现了 PLCopen / IEC 61131-3 标准的完整视野

## 目录内容

### archived-plan.md（原根目录 `plan.md`）

详细的 6 阶段开发计划（月份 1-24）：
1. 核心运行时系统
2. 编程语言支持（ST / IL）
3. 图形化编程（LD / FBD / SFC）
4. 集成开发环境
5. 通信和网络
6. 高级功能和优化

**归档原因**：按单人项目真实产能算需要 10-20 年，而非原计划的 2 年。

### archived-milestones.md（原根目录 `MILESTONES.md`）

跟踪 6 阶段的里程碑进度文档。

**归档原因**：进度数据不准确（例如"功能块框架已完成 80%"对应的代码实现接近 0 行）。

### archived-specs/plc-runtime-core/（原 `.kiro/specs/plc-runtime-core/`）

18 份设计文档，规划了一套完整 PLC 运行时系统：

| 文档 | 规划内容 | 当前代码状态 |
|------|----------|--------------|
| design.md | 六层分层架构 | 部分对应现有运动控制 |
| design-realtime-scheduler.md | RT-PREEMPT 调度器，<50μs 抖动 | 未实现 |
| design-memory-manager.md | 无锁内存池、NUMA 感知分配器 | 未实现 |
| design-function-block-engine.md | 完整 FB 引擎 | 部分对应（运动控制部分） |
| design-io-system.md | 三阶段 I/O 处理模型 | 未实现 |
| design-st-compiler.md | ST 语言多阶段编译器 | 未实现 |
| design-communication.md | gRPC + 零拷贝 | 未实现 |
| design-motion-algorithms.md | S 曲线 + Look-ahead | 部分对应（梯形曲线） |
| design-security.md | 插件沙箱安全模型 | 未实现 |
| requirements.md | 27 个需求 + 优先级 | 参考价值 |
| architecture-decisions.md | 8 个 ADR | 部分仍有效 |
| current-status-summary.md | "实施就绪"状态报告 | 与实际产能不符 |
| p0-action-plan.md | 2 周内修复 P0 缺口 | 按计划无法达成 |
| technical-debt-analysis.md | 技术债务分析 | 参考价值 |
| traceability-matrix.md | 需求-设计追溯 | 参考价值 |
| tasks.md | 10 周详细任务 | 按计划无法达成 |
| glossary.md | 术语表 | 有参考价值 |
| improvement-summary.md | 改进摘要 | 归档参考 |

---

## 如何使用这些文档

### 不要

- 不要把它们当作路线图
- 不要按这些设计直接开始实施（它们可能已经不符合当前代码和最佳实践）
- 不要新贡献者以为"项目在做这些"

### 可以

- 作为学习材料了解项目早期思路
- 在 VISION.md 解锁条件满足时，引用相关文档作为重启评审的起点
- 作为 PLC / PLCopen / IEC 61131-3 标准学习的补充资料

## 从愿景到代码的正确路径

```
  野心（VISION.md）
        ↓ 解锁条件满足
  规划（ROADMAP.md）
        ↓ 具体任务
  设计（重新评审本目录相关文档 + 结合当前代码）
        ↓ 实施
  代码
```

**顺序不能反**。从本目录旧设计直接跳到实施会跳过两次评审环节，很危险。

## 这些文档的命运

- **不会更新**：保持归档时的快照状态
- **不会删除**：除非某个设计被完全证伪
- **可能被引用**：当相关能力解锁启动时

---

*归档日期：2026-04-17*
*归档原因：Sprint 0 项目重新定位，从"5-8 人团队 24 个月计划"收缩到"单人可执行的运动控制库"*
*详情见 [ADJUSTMENT_PLAN.md](../archive/ADJUSTMENT_PLAN.md)*
