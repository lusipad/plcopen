# Codex 任务书：PLCopen Part 1 §3/§4 逐 FB 条款对照

> 把本文件**整份粘给 Codex**。它是一份可独立完成、可复核的工程任务。

---

## 0. 你是谁、在哪、干什么

你在 `plcopen` 仓库（现代 C++17 header-only 的 PLCopen 运动控制内核，
Apache 2.0）。仓库根有 `CLAUDE.md`（AI 协作手册），**先读它**，尤其
硬规则 1（语义矩阵先行）、5（出处纪律）、6（提交 = 证据包）。

**你的任务**：把 PLCopen Motion Control **Part 1 v2.0 规格正文的 §3
（单轴 FB，3.1–3.36）与 §4（多轴 FB）** 逐 FB、逐条与我们的实现对照，
把结果**增量追加**进已有的条款矩阵。

**你不写代码。** 本任务只产出对照矩阵与缺陷登记。发现的问题记录下来，
**不修**——修复走语义矩阵 + 人批流程（硬规则 1）。

---

## 1. 准备（先做这一步，否则无从下手）

```bash
tools/fetch-plcopen-specs.sh          # 取回 12 份规格（SHA256 校验）
ls refs/plcopen-specs/mc_part1.txt    # 你的主要依据（141 页已抽成文本）
```

- 依赖 `curl`/`sha256sum`/`pdftotext`（poppler-utils）。缺 pdftotext 就
  `sudo apt-get install -y poppler-utils`。
- **`refs/plcopen-specs/` 已 gitignore，不要提交任何 PDF/txt**（版权文档，
  见 `doc/compliance/plcopen-specs-manifest.md`）。

**必读的三份仓库文档**（决定你的判定口径）：
1. `doc/compliance/plcopen-part1-clause-matrix.md` — **你要续写的文件**，
   已完成 §2（Model），体例照抄。
2. `doc/compliance/plcopen-conformance-audit.md` — 前序审计结论（B 级 I/O
   核对、认证程序真相）。
3. `doc/compliance/known-boundaries.md` — 我们已声明的边界（KB-001~070）。
   **某项"不符合"若已在 KB 中显式声明，判定为 `⚠️偏差(已声明)` 而非违规。**

---

## 2. 实现在哪

| 规格章节 | 我们的实现 |
|---------|-----------|
| §3 单轴 FB | `core/fb/motion.h`、`profile.h`、`parameter.h`、`io.h`、`probe.h` |
| §4 多轴 FB | `core/fb/sync.h` |
| FB 基类与输出语义 | `core/fb/motion.h` 的 `MotionOutputs` / `AxisExecuteFb` |
| 轴状态机与命令语义 | `core/axis/state.h`（`AxisModel`） |
| 测试（行为证据） | `core/test/r3_*.cpp` |

命名映射：规格 `MC_MoveAbsolute` → 我们 `FbMoveAbsolute`（去 `MC_` 加 `Fb`）。
少数别名见 `core/fb/sync.h` 的 `using FbCamOut = FbGearOut;`。

---

## 3. 逐 FB 要对照什么（每个 FB 五问）

规格里每个 FB 章节（如 `3.1. MC_Power`）都有固定结构：
**FB 名 → 一句话功能 → VAR_IN_OUT/VAR_INPUT/VAR_OUTPUT 表（每行带 `B`/`E`
标记）→ Notes（行为规则）→ 时序图**。

对每个 FB 回答：

1. **B 级 I/O 齐备吗**？规格标 `B` 的输入输出，我们是否全部具备
   （名字可不同，但**语义必须对应**；名字不符记为"命名偏差"）。
2. **Notes 里的行为规则我们都满足吗**？——**这是重点**。Notes 常含硬约束，
   例如 MC_Power 的 "Only 1 FB MC_Power should be issued per axis"、
   "If power fails it will generate a transition to ErrorStop"。
3. **状态机交互对吗**？该 FB 在哪些轴状态可调用、把轴转到什么状态、
   非法状态下调用是否报错（对照 §2.1 状态图与我们的 `AxisModel`）。
4. **输出时序对吗**？`Done`/`Busy`/`Active`/`CommandAborted`/`Error` 的
   置位复位时机（对照 §2.4.1 通用规则——**注意已确认的 D-01/D-02 两条
   基类级违规，见矩阵，不要重复登记，但要标注哪些 FB 受其影响**）。
5. **有没有我们特有的扩展**？（如 `jerk`、`buffer_mode`、`command_id`）
   ——这些是 `V`（厂商扩展），**合法但必须登记**（认证时要列出）。

---

## 4. 产出物（唯一交付）

**增量追加**到 `doc/compliance/plcopen-part1-clause-matrix.md`，格式照抄
文件里 §2 的体例：

```markdown
## §3.1 MC_Power — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.1-io | B 级 I/O：Axis / Enable / Status / Error | ✅ | `FbPower`：axis_ref, enable, status, error |
| 3.1-n1 | `Enable` 使能的是驱动器功率级，不是 FB 本身 | ✅ | — |
| 3.1-n2 | `Enable=TRUE` 且轴在 `Disabled` → 转 `Standstill` | ✅ | `state.h:NNN` |
| 3.1-n3 | 掉电（含运行中）→ 转 `ErrorStop` | 🔴 | 我们不监测掉电；`ServoFeedback` 无功率级状态 |
| 3.1-n4 | 每轴只应有一个 MC_Power 实例 | ⚠️ | 我们不阻止多实例——需声明 |
| 3.1-v | 厂商扩展：无 | ➖ | — |
```

**判定口径**（与 §2 一致，不要发明新符号）：

| 符号 | 含义 |
|------|------|
| ✅ | 符合 |
| 🔴 | **违规**：规格强制（`B` 级 / Notes 中的 shall/must/will）而我们不符 |
| ⚠️ | **偏差**：规格允许实现自定，但我们**未声明**；或已声明但需登记 KB |
| ❌ | **缺失**：整个 FB 或 B 级 I/O 不存在 |
| ➖ | 不适用 |

**每条都要给证据**：文件:行号、KB 号、或测试名。**没有证据的判定不算数**
——不确定就标 `🔴待验` 并说明需要验什么，**不要猜**。

---

## 5. 硬性纪律（违反即返工）

1. **出处纪律（硬规则 5）**：**只引条款号 + 你自己的话复述要求，
   绝不复制规格原文**。矩阵里出现规格的整句英文 = 违规。
2. **不改代码**。发现缺陷只登记，不修。
3. **不要重复登记 D-01/D-02**（已确认的两条基类违规）——但要在受影响的
   FB 行里标注"受 D-01 影响"。
4. **已声明的边界不算违规**：先查 `known-boundaries.md`，若该行为已被
   KB-NNN 显式声明，判 `⚠️偏差(KB-NNN 已声明)`。
5. **提交信息按 `plcopen-commit-style`**：angular 类型 + 中文四段正文
   （目的/设计思路/修改内容/影响范围）+ AI 生成声明。scope 用 `compliance`。
6. **一次提交一件事**：建议按批提交（§3.1–3.10 一批、3.11–3.20 一批……），
   而不是憋一个大提交。

---

## 6. 完成判据

- [ ] §3 的 **36 个单轴 FB 章节**全部有对照小节
- [ ] §4 的多轴 FB（CamTableSelect/CamIn/CamOut/GearIn/GearOut/GearInPos/
      Phasing*/CombineAxes）全部有对照小节
- [ ] 每条判定都有**证据**（文件:行号 / KB 号 / 测试名）
- [ ] 所有 🔴 违规在矩阵末尾的"已确认违规详情"节有条目
      （要求 / 我们的行为 / 影响面 / 修复方向）
- [ ] 矩阵头部的"进度"行已更新
- [ ] `git diff --check` 干净；已提交并推送

---

## 7. 先跑通一个再批量

**建议流程**：先完整做 `3.1 MC_Power` 一个 FB，把它的对照小节写出来、
提交、自检格式对不对；**确认体例无误后再批量推进**。这样返工成本最低。

---

## 8. 已知的坑

- 规格 PDF 抽出的文本有页眉页脚噪声（`TC2 Task Force...`、`page N/141`），
  过滤掉再读。
- 规格里的 `B`/`E` 标记在 I/O 表的**行首**，抽文本后可能对不齐——
  权威来源是 **附录 B3**（每个 FB 的合规表），必要时交叉核对。
- 我们的输出统一在 `MotionOutputs` 结构里（`done`/`busy`/`active`/
  `error`/`error_id`/`command_aborted`/`command_accepted`/`command_id`）
  ——判断"有没有某输出"时要看这个结构，别只看 FB 类自己的字段。
- `MC_ReadParameter`/`MC_WriteParameter` 在规格里与 `*BoolParameter`
  合并成一节（3.19/3.20），我们拆成了四个类。

---

## 9. 你可能会发现的东西（不是提示，是心理准备）

前序审计已经证明：**FB 名单齐 ≠ I/O 齐 ≠ 行为对**。已抓到的两条基类级
违规（Execute 脉冲后 Done 永不出现；Inxxx 输出缺失且语义被误解）说明
Notes 里的行为规则是重灾区。**认真读每个 FB 的 Notes**——那里藏着真问题。

如果你发现某条我们"看起来符合"但你不确定，**标 🔴待验并说明**，
不要为了让表格好看而判 ✅。**这份矩阵的价值在于诚实。**
