# CLAUDE.md — AI 协作操作手册

本项目大部分编码、测试、文档由 AI 执行；人负责方向、规格批准、评审与
硬件。**操作流程已固化为技能**（`.claude/skills/`，路由见
`plcopen-guide`）；本文件只留红线与权限。完整体系见
[ai-collaboration.md](doc/planning/ai-collaboration.md)。

## 项目速览

plcopen：现代 C++ 的 PLCopen 运动控制内核（Apache 2.0），可嵌入库。
新核 `core/` 按 L0-L7 分层（依赖只向内，L0-L4 不得引用 PLCopen 语义）；
旧线 `src/` 冻结（P0-only，回放基线）。**现状看 [STATUS.md](STATUS.md)，
术语看 [CONTEXT.md](CONTEXT.md)。**

**读代码前先读**：`CONTEXT.md` → `STATUS.md` → `doc/compliance/`
（normative 规格 + [已知边界](doc/compliance/known-boundaries.md)）→
对应模块 README。

## 常用命令

```bash
.\build.ps1 -Test                                   # Windows 一键
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

提交前全套门禁与已知坑：用 `plcopen-gates` 技能。

## 人机对齐点（2026-07-05 起，替代旧 T0-T3 分级）

现实分工：AI 承担实现、测试、文档的全部工作量；人拍板方向与语义。
需要人的时刻只有三类，其余 AI 自主推进、由门禁裁决：

| 对齐点 | 形式 |
|--------|------|
| **语义批准** | 新行为先写语义矩阵，人批准后实现；批准记录写进矩阵状态头 |
| **声明变更** | 改变既有周期路径输出必须声明：KB 登记 + 回放基线重录 + 提交信息注明；未声明变更 = 回放零差异 |
| **人专属动作** | LICENSE/NOTICE/PROVENANCE、发布 tag、对外承诺：AI 只起草，落地仅人 |

合入由门禁裁决；人按需抽查 `git log`（每个提交自带证据与 KB 引用），
错了回滚。门禁是守门员，git 是回退键。

## 硬规则

1. **语义矩阵先行**：新特性先改规格、人批后实现；未定义组合显式报错
   ——全流程见 `plcopen-semantic-feature` 技能。
2. **RT 路径禁令**：周期路径禁堆分配/阻塞锁/异常/系统调用/浮点时间
   累加——细则见 `plcopen-rt-safety` 技能。
3. **测试先行**：实现前先写失败测试；规划器/执行器变更必须过黄金回放
   （未声明变更 = 零差异）——流程见 `plcopen-replay-baseline` 技能。
4. **文档同提交**：公开头文件或矩阵变更必须伴随文档更新（KB 登记见
   `plcopen-kb-boundary` 技能），否则 docs-sync 门禁拒合。
5. **出处纪律**：禁止参考旧核 i5 残留与任何 GPL 实现——清单与 vendoring
   流程见 `plcopen-provenance` 技能。
6. **提交 = 证据包**：目的、门禁结果、KB 编号、AI 生成声明写进提交
   信息；一次提交一件事——格式见 `plcopen-commit-style`。
7. **裁决查 ADR**：架构/语义问题先检索 `doc/design/decisions/`；没有
   先例才向人提问，裁决后落一份 ADR。

## 行为准则（压缩版）

- **先想后写**：假设不明确就问；多种解释就列出来，不默默选一个。
- **最小实现**：不做没被要求的功能、抽象、配置项。
- **外科手术式修改**：只动任务要求的行；旧线 `src/` 尤其如此。
- **目标驱动**：先定义可验证的完成标准；弱标准（"能跑就行"）不接受。

---

*任何 agent（不限 Claude）同样适用；跨工具入口见 [AGENTS.md](AGENTS.md)。
与其他文档矛盾时以本文件与技能为准，发现矛盾当场修复。*
