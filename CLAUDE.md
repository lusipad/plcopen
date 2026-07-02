# CLAUDE.md — AI 协作操作手册

本项目大部分编码、测试、文档工作由 AI 执行；人负责方向、规格、评审与硬件。完整体系见 [doc/planning/ai-collaboration.md](doc/planning/ai-collaboration.md)。

## 项目速览

plcopen：现代 C++ 的 PLCopen 运动控制库（Apache 2.0）。**当前处于核心重写期**（12 个月冲刺，见 [rewrite-plan.md](doc/planning/rewrite-plan.md) §3.1b）：

- 旧核 `src/`：冻结，仅修 P0 缺陷，不加功能，不做格式化/重构
- 新核 `core/`：按 L0-L7 分层生长（rt → otg → geom → plan → exec → axis → fb → adapters），依赖只向内，L0-L4 不得引用 PLCopen 语义

**读代码前先读**：`doc/planning/README.md`（文档分层索引）→ `doc/compliance/`（验收规格，normative）→ README「已知边界」节 → 对应模块 README。

## 常用命令

```bash
# Windows
.\build.ps1 -Test

# 跨平台
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure

# 覆盖率（Windows） / 文档
.\coverage.ps1
cmake -S . -B build -DPLCOPEN_BUILD_DOCS=ON && cmake --build build --target docs
```

## 风险分级变更政策

| 级别 | 范围 | AI 权限 |
|------|------|---------|
| T0 | 文档、注释、demo、新增测试、格式化、CI 脚本 | 自主推进，门禁绿即可 |
| T1 | 非 RT 路径实现、工具链、pyplcopen、构建配置 | 实现+自检+证据包，人轻评审 |
| T2 | RT 周期路径、OTG/规划器算法、状态机语义、公开 API | 全门禁+24h 冷却，**人逐行评审后合入** |
| T3 | LICENSE/NOTICE/PROVENANCE、发布 tag、安全边界对外声明 | 只起草，不落地 |

## 硬规则

1. **语义矩阵先行**：新特性先改规格（`doc/compliance/` 矩阵 + 边界文档），规格 PR 获人批准后再实现。未定义的语义组合显式报错，不静默猜测。
2. **RT 路径禁令**（新核 cycle-path：`core/` 下 rt/otg/geom/exec 及标注 `// RT-SAFE` 的代码）：禁堆分配、禁阻塞锁、禁异常、禁系统调用、禁浮点时间累加（时间一律整型周期计数）。
3. **测试先行**：实现前先写失败测试；规划器/执行器任何变更必须通过黄金回放 diff（未声明的语义变更 = 零差异）。
4. **文档同 PR**：公开头文件或矩阵变更必须伴随文档更新，否则 docs-sync 门禁拒合。
5. **出处纪律**：禁止参考/复现旧核 i5 残留文件（清单见 rewrite-plan §0：`ProfilePlanner.cpp`、`AxisMove.cpp`、`Axis.cpp` 等 18 个文件）与任何 GPL 实现（IgH/LinuxCNC 源码）。新核语义只来自：合规矩阵、测试、已知边界文档、PLCopen 标准文本。
6. **PR = 证据包**：门禁结果、基准 delta、影响的 KB 边界编号、AI 生成声明。小 PR：软上限 400 行 diff，核心算法 200 行。
7. **裁决查 ADR**：架构/语义问题先检索 `doc/design/decisions/`；没有先例才向人提问，裁决后落一份 ADR。

## 行为准则（压缩版）

- **先想后写**：假设不明确就问；多种解释就列出来，不默默选一个；有更简单的方案就说。
- **最小实现**：不做没被要求的功能、抽象、配置项。写完自问：资深工程师会说这过度设计吗？
- **外科手术式修改**：只动任务要求的行；不顺手改邻近代码/注释/格式；旧线 `src/` 尤其如此。
- **目标驱动**：每个任务先定义可验证的完成标准，循环直到验证通过；弱标准（"能跑就行"）不接受。

---

*本手册与 [ai-collaboration.md](doc/planning/ai-collaboration.md) 同步维护；矛盾时以后者为准并提 PR 修复。*
