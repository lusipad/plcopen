# 项目调整计划（v0.1 → v0.2）

> 📦 **已归档（2026-07-05）**：历史执行/规划文档，仅供追溯，不再维护；现行文档入口见 [doc/planning/README.md](../planning/README.md) 与根 [STATUS.md](../../STATUS.md)。


**日期**: 2026-04-17
**评审来源**: CEO plan review
**评审模式**: SCOPE REDUCTION（范围削减）
**项目定位**: 期望建立有用户、有社区的开源项目
**当前状态（2026-05-05）**: 历史执行计划；Sprint 0 文档重组已完成，项目正式名称已决策为 `plcopen`，公开面与内部 `PLCOPEN_*` 宏/guard 已统一。

---

## 1. 现状诊断

### 1.1 真实状态与文档叙述的差距

| 维度 | 文档叙述 | 代码实际 |
|------|----------|----------|
| 团队规模 | 5-8 人 + 专家顾问 | 1 人（lusipad） |
| 开发周期 | 18-24 个月，6 阶段 | 近期产出以文档/构建脚本为主 |
| 代码规模 | PLC 运行时 + ST 编译器 + IDE | ~4300 行 C++ 运动控制库 |
| 测试覆盖率 | 目标 >90% | 接近 0% |
| 跨平台 | Windows/Linux/macOS | 构建脚本仅 Windows + VS2022 |
| 设计文档 | 18 份"88% 完成度" | 为不存在的系统预先设计 |

### 1.2 核心问题

**设计过度，执行不足**。单人项目挂着团队级计划在走，每次打开 `plan.md` 都会累积一次挫败感。

### 1.3 纸上富贵的具体表现

`.kiro/specs/plc-runtime-core/` 下 18 份文档规划的能力（RT-PREEMPT 调度器、无锁 SPSC/MPMC、NUMA 感知分配器、ST 编译器、Electron IDE、OPC UA、SIL 安全认证）—— 对应代码 0 行。

---

## 2. 战略决策

### 2.1 模式：SCOPE REDUCTION

3 个月内完成"**小而清晰、可信可用**"的项目重塑。

### 2.2 核心原则

1. **分层管理野心**：北极星愿景不删，但与当下路线图分离
2. **窄切入点**：先做一件事做透，再被用户拉向扩展
3. **量化解锁**：每个宏大能力都有触发条件，避免内心焦虑推动
4. **诚实节奏**：按单人项目真实产能做承诺

### 2.3 本次调整的边界

- **会做**：文档重构、定位澄清、归档、命名统一
- **不会做**：改动 `src/` 下任何 C++ 源码
- **暂不决定**：具体功能实现顺序（由 ROADMAP.md 承接）

---

## 3. 三层文档结构

```
  北极星愿景（VISION.md）        3-5 年，不承诺时间
  ─────────────────              开源 PLC 完整生态
                ↑ 解锁
  
  路线图（ROADMAP.md）           6-12 个月，每季度复盘
  ─────────────────              v0.2 稳定库 → v0.3 S 曲线 → ...
                ↑ 当下
  
  当前焦点（README.md）          2-3 个月，当下承诺
  ─────────────────              CI + 测试 + Linux + 定位
```

| 文档 | 给谁看 | 性质 | 更新频率 |
|------|--------|------|----------|
| `README.md` | 首次访问者 | 现在能给你什么 | 每次 release |
| `ROADMAP.md` | 潜在用户/贡献者 | 近期可信承诺 | 每季度 |
| `VISION.md` | 合作者/自己 | 终极方向 | 年度 |

---

## 4. 项目重新定位

### 4.1 一句话定位（候选）

> **现代 C++ 的 PLCopen 运动控制库 —— 嵌入到你的控制器里，不替代你的控制器。**
>
> *A modern C++ motion-control library implementing the PLCopen Part 1&2 standard. Embed it in your controller, not replace your controller.*

### 4.2 差异化对比（README 首屏）

| 项目 | 定位 | 目标用户 |
|------|------|----------|
| **plcopen（本项目）** | **C++ PLCopen 运动控制库** | **做自定义控制器的 C++ 工程师** |
| CODESYS | 商业完整 PLC 平台 | 工业客户 |
| Beremiz | IDE + MatIEC 全功能 | 做完整 PLC 的团队 |
| OpenPLC | Arduino/RPi 入门 PLC | 教学/DIY |
| LinuxCNC | CNC 机床控制器 | 机床工厂 |
| MatIEC | IEC 61131-3 编译器 | 需要语言转换 |

### 4.3 目标用户画像

**PLC-Embedder**：在做工业机器人、自动化设备、定制控制器的 C++ 工程师。他们需要：

- 实现了 PLCopen 状态机的现成库
- 实现了梯形/Jerk 曲线规划的算法
- 不要 IDE，不要完整 PLC 运行时
- 能被 `find_package` / `FetchContent` 引用

---

## 5. NOT in scope（明确不做）

以下能力从**当前路线图**移除。这些不是"以后做"，是"**当前不做**"。它们保留在 `VISION.md` 和 §6 解锁条件表里。

```
  ✗ ST / IL / LD / FBD / SFC 编译器与编辑器
  ✗ Electron 跨平台 IDE
  ✗ 工业通信协议（Modbus / EtherNet/IP / PROFINET / OPC UA）
  ✗ Web HMI 编辑器
  ✗ 系统冗余 / 热备份 / SIL 安全认证
  ✗ Linux RT-PREEMPT 实时调度器
  ✗ 无锁 SPSC/MPMC / NUMA 感知分配器
  ✗ 云原生 / Kubernetes / 分布式 PLC
  ✗ 完整 IEC 61131-3 标准功能块库
  ✗ 集成开发环境
```

### 每一项的一句话理由

| 砍掉的项 | 为什么不做 |
|----------|-----------|
| ST/IL/LD/FBD/SFC 编译器 | MatIEC 和 Beremiz 做得好，无必要重做 |
| Electron IDE | 与"库"的定位冲突；独立项目 |
| 工业通信协议 | 有 libmodbus / open62541，用户自己接 |
| RT-PREEMPT 调度 | 单人项目不可能可靠测试，不如不承诺 |
| Web HMI | 与运动控制库无关 |
| 冗余 / SIL | 需要工程师团队 + 认证预算 |

---

## 6. 解锁条件表

每个 NOT in scope 的能力都有量化触发条件。**条件不到，不启动**。

| 能力 | 解锁条件（任一满足即可启动） |
|------|----------------------------|
| S 曲线（Jerk 受限） | v0.2 稳定后 OR 1 个用户明确提 issue |
| MC_Home 完整实现 | v0.2 稳定后第一优先级（现 75%） |
| 多轴凸轮/齿轮 | 1 个用户 issue + 有测试硬件 |
| Python 绑定 | ≥3 个"Python 能调吗"询问 |
| ST 编译器 MVP | ≥5 个用户要求 + 有合作者 |
| Modbus 适配器 | 有正在使用库的项目提集成需求 |
| OPC UA | 1 个工业客户深度集成承诺 |
| IDE | 库被 ≥10 个项目引用 |
| SIL 认证 | 有客户愿意承担认证成本 |

---

## 7. 执行步骤

### Sprint 0（第 1-2 周）—— 解除心理负担

| # | 动作 | 产出 | 状态 |
|---|------|------|------|
| 1 | 归档 `.kiro/specs/` → `doc/vision/archived-specs/` | 保留历史，脱离当前 | ✅ 已 stage |
| 2 | 归档 `plan.md` → `doc/vision/archived-plan.md` | 移出根目录 | ✅ 已 stage |
| 3 | 归档 `MILESTONES.md` → `doc/vision/archived-milestones.md` | 移出根目录 | ✅ 已 stage |
| 4 | 写 `doc/vision/README.md`（归档前言） | 说明这些文档的性质 | ⏳ 待开始 |
| 5 | 写 `VISION.md`（北极星） | 保留野心 | ⏳ 待开始 |
| 6 | 写 `ROADMAP.md`（6-12 个月路线） | 近期承诺 | ⏳ 待开始 |
| 7 | 重写 `README.md`（定位 + 对比表） | 首屏清晰 | ⏳ 待开始 |
| 8 | 命名决策：`plcopen` | 公开面与内部 `PLCOPEN_*` 宏/guard 统一 | ✅ 完成 |

### Sprint 1（第 3-6 周）—— 基础设施

| # | 动作 | 产出 |
|---|------|------|
| 9 | GitHub Actions CI（Windows + Linux） | 自动构建 + 测试徽章 |
| 10 | 接入 Catch2 或 GoogleTest | 替换 `test_basic` 手写框架 |
| 11 | 写 `AxisBase` 状态机测试套 | 覆盖 PLCopen 8 状态转换 |
| 12 | 写 `ProfilePlanner` 边界测试 | 覆盖 0 速度/极短距离/超限 |
| 13 | Linux 构建脚本 + 文档 | 跨平台可信 |

### Sprint 2（第 7-12 周）—— 对外可用

| # | 动作 | 产出 |
|---|------|------|
| 14 | CMake `install(EXPORT)` 配置 | `find_package(plcopen)` 可用 |
| 15 | `FetchContent` 兼容 | 外部项目可一键引用 |
| 16 | 独立 `example` 仓库 | 展示如何嵌入到自己项目 |
| 17 | v0.2.0 tag + GitHub Release | 对外"项目活着"的信号 |
| 18 | 在社区发布（r/PLC / HN / 论坛） | 收集第一批用户反馈 |

---

## 8. 预期交付物

### Sprint 0 完成后
- 三份新文档：`VISION.md` / `ROADMAP.md` / `README.md`
- 一个清晰归档：`doc/vision/` 包含所有愿景期设计
- 一个项目定位：首屏 30 秒能说清"是什么 / 不是什么"

### Sprint 1 完成后
- CI 徽章可放 README
- 测试覆盖率 >50%（诚实数字，不再是"目标 90%"的空话）
- 可在 Linux 构建

### Sprint 2 完成后
- v0.2.0 可被外部项目引用
- 一个"嵌入到自己项目"的可运行例子
- 开源社区第一次对外发声

---

## 9. 已执行动作（截至目前）

以下 git 变更已 stage，尚未 commit：

```
  renamed:  .kiro/specs/plc-runtime-core/*  →  doc/vision/archived-specs/plc-runtime-core/*
  renamed:  plan.md                         →  doc/vision/archived-plan.md
  renamed:  MILESTONES.md                   →  doc/vision/archived-milestones.md
  deleted:  .kiro/（空目录）
  added:    doc/vision/（新目录）
```

**性质**：纯文件搬家，零代码改动，完全可回滚。

**回滚方式**（若决定放弃）：

```bash
git restore --staged .
git checkout -- .
# 手动恢复被删的 .kiro 目录：git 会追踪被重命名的文件路径
```

---

## 10. 风险与回退

### 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| 社区反应低于预期 | 发布后可能没人 star/issue | 不影响库本身价值，继续做核心 |
| 自己又想做更多 | 某天想重启 ST 编译器/IDE | §6 解锁条件表 gatekeep |
| 用户定位失准 | 真实用户可能是另一类人 | 每季度 ROADMAP 复盘时调整 |
| 归档文档被误用 | 新贡献者把愿景当计划 | `doc/vision/README.md` 前言明确标注 |

### 回退机制

- 所有搬家都用 `git mv`，历史可追溯
- 文档重写的每一份都可单独回滚（`git checkout HEAD~1 -- README.md`）
- 最坏情况：放弃本计划并 `git reset --hard origin/main`，零损失

---

## 11. 待用户决策点

本计划通过审批后，请决定：

1. **搬家是否保留？**
   - A) 保留（作为 Sprint 0 步骤 1-3 的完成状态）
   - B) 回滚（重新讨论归档策略）

2. **项目正式名称？**
   - 已决策：保留 `plcopen` 作为仓库名、CMake project/package target、public namespace，并将内部 legacy macro / include guard 统一为 `PLCOPEN_*`。

3. **Sprint 0 剩余步骤（4-7）的节奏？**
   - A) 一气呵成：VISION → ROADMAP → README 一次性写完
   - B) 分批审阅：写一份给你看一份，逐份调整

4. **VISION.md 的野心边界？**
   - A) 保留整个 6 阶段计划作为愿景（完整版）
   - B) 收紧到"运动控制生态"主线（聚焦版）
   - C) 由我先起草，你在草稿上调整

---

## 附录 A：本计划的产出结构

Sprint 0 完成后的目录结构：

```
plcopen/
├── README.md                  ← 重写：一句话定位 + 对比表 + 快速开始
├── VISION.md                  ← 新：北极星愿景（3-5 年）
├── ROADMAP.md                 ← 新：6-12 个月路线（每季度更新）
├── ADJUSTMENT_PLAN.md         ← 本文档（Sprint 0 完成后可归档）
├── BUILD_README.md            ← 保留：构建指南
├── CLAUDE.md                  ← 保留：开发规范
├── CMakeLists.txt             ← 不改
├── .gitignore                 ← 不改
├── build.ps1                  ← 不改
├── src/                       ← 不改
└── doc/
    ├── design/
    │   └── design_doc.md      ← 保留：当前代码的真实设计
    ├── reference/
    │   └── *.pdf              ← 保留：PLCopen 标准文档
    └── vision/                ← 新：愿景期归档
        ├── README.md          ← 新：归档说明（不是路线图）
        ├── archived-plan.md   ← 原 plan.md
        ├── archived-milestones.md  ← 原 MILESTONES.md
        └── archived-specs/
            └── plc-runtime-core/
                └── *.md       ← 原 .kiro/specs/* 共 18 份
```

---

*本计划是 Sprint 0 的指挥棒。Sprint 0 完成后，可归档到 `doc/vision/archived-adjustment-plan.md`，由 `ROADMAP.md` 承接后续节奏。*
