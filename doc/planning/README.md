# doc/planning/ 索引

本目录只放**现行**规划文档。与其他文档层的分工：

| 层 | 位置 | 回答的问题 | 复盘节奏 |
|----|------|------------|----------|
| 现状 | `STATUS.md`（根） | 现在能干什么、进行到哪 | 每批收口 |
| 战略 | `VISION.md`（根） | 为什么、去哪里 | 年度 |
| 规划 | 本目录 | 怎么去、多少代价 | 季度 |
| 承诺 | `ROADMAP.md`（根） | 当下在做什么 | 随里程碑 |
| 规格 | `doc/compliance/` | 「做到」的定义（normative） | 随实现 |
| 设计 | `doc/design/` | 具体怎么实现的 | 随码 |
| 归档 | `doc/archive/`、`doc/vision/` | 历史决策与教训 | 只进不出 |

## 现行文档

| 文档 | 内容 | 状态 |
|------|------|------|
| [long-term-plan.md](long-term-plan.md) | 商业化战略：第 0 章 S0-S3 关键路径（现行）、商用级定义、收入模型、风险与 KPI、技术难点 T1-T12、算法权衡 | **第 7 稿（2026-07-06 重基线）**，季度复盘 KPI |
| [phase-b-software-work-breakdown.md](phase-b-software-work-breakdown.md) | Phase B 纯软件拆解（BS1-BS6 已全部完成）+ 硬件延后清单与触发条件 | 已收口，留触发清单 |
| [robot-integration.md](robot-integration.md) | 机器人集成蓝图：形态可行性、参考架构、差距→计划映射 | 现行 |
| [ai-collaboration.md](ai-collaboration.md) | AI-First 工程体系（战略部分；操作流程已迁入 `.claude/skills/`） | 现行 |
| [v0.20.0-release-draft.md](v0.20.0-release-draft.md) | v0.20.0 发布形式、门禁证据与已执行检查单；面向用户的发布记录见 [`docs/releases/v0.20.0.md`](../../docs/releases/v0.20.0.md) | **已发布（2026-07-20）** |
| [v1.0.0-alpha-release-draft.md](v1.0.0-alpha-release-draft.md) | v1.0.0-alpha 实验预览发布记录 | 历史记录 |
| [r1-rt-report-template.md](r1-rt-report-template.md) | 72h PREEMPT_RT 报告模板（B7 硬件阶段使用） | 模板，待真机 |
| [software-excellence-plan.md](software-excellence-plan.md) | 软件极致候选清单（Y 算法/P 标准面/Z 采纳/E 证据四线） | 2026-07-21 重基线；当前起手见文末 |
| [t2-mujoco-rerun-plan.md](t2-mujoco-rerun-plan.md) / [实施记录](t2-mujoco-rerun-implementation-notes.md) | T2a MuJoCo 物理闭环 + Rerun 离线记录首批，保持 H1/H2/L5 边界 | **已完成，Windows/Linux 远端复验通过（2026-07-24）** |
| [t2b-large-model-twin-plan.md](t2b-large-model-twin-plan.md) / [实施记录](t2b-large-model-twin-implementation-notes.md) | T2b 仓库自有 7DOF 模型 + H1 q/dq 原子帧 + 通用 N 关节孪生 | **已批准，待实现（2026-07-25）** |
| [d3-online-scope-plan.md](d3-online-scope-plan.md) / [实施记录](d3-online-scope-implementation-notes.md) | D3 `PLCT v1` 在线 Scope：非 RT 落盘、阈值触发、窗口冻结与可选 Rerun | **已完成并上线，PR #23 与主线四门全绿（2026-07-24，KB-091）** |
| [d1-st-language-server-plan.md](d1-st-language-server-plan.md) / [实施记录](d1-st-language-server-implementation-notes.md) | D1 单文档多 POU ST LSP + trusted-workspace VS Code client | **已完成并合入，PR #26 与合并后五门全绿（2026-07-24，KB-092）** |
| [h1-synchronized-joint-stream-plan.md](h1-synchronized-joint-stream-plan.md) / [实施记录](h1-synchronized-joint-stream-implementation-notes.md) | H1 48 关节原子混合命令帧、direct/upsample 延迟与组级断流实施基线 | **已完成并合入，PR #29 与合并后五门全绿（2026-07-25，KB-035）** |
| [docs-i18n-plan.md](docs-i18n-plan.md) / [实施记录](docs-i18n-implementation-notes.md) | 文档站简体中文默认入口、`/en/` 英文镜像、同页语言切换与原子 Pages 发布 | **已完成并上线，远端 build/deploy 全绿（2026-07-24）** |
| [docs-site-integration-plan.md](docs-site-integration-plan.md) / [实施记录](docs-site-integration-implementation-notes.md) | 把架构、合规、边界、贡献、治理、安全与变更历史整合为双语站内项目文档层 | **已完成并上线，21 对页面与公网深层路由通过（2026-07-24）** |
| [docs-reference-integration-plan.md](docs-reference-integration-plan.md) / [实施记录](docs-reference-integration-implementation-notes.md) | 把 ST、标准证据和 ADR 接入双语参考区，修正五个英文页与深层同页语言切换 | **已完成并上线，24 对页面与深层同页语言切换公网通过（2026-07-24）** |
- [L 系列工作拆解](l-series-work-breakdown.md) —— L0-L7、L∀ 已闭合的范围、依赖、出口判据与完成态证据（2026-07-17 重基线）
- [PLCopen 合规补齐计划](plcopen-conformance-plan.md) —— 原文审计后的 P 系列重构：P1-A 结构缺口 → L2a 引脚表（即合规面）→ 可提交认证声明（2026-07-12）
- [**主计划：从这里到商用级**](master-plan.md) —— 复盘第一入口：剩余工作按 AI 能力边界四栏分类 + 依赖链总图 + 人侧杠杆排序（2026-07-21 重基线）
- [采纳与推广计划](adoption-plan.md) —— 开源本位的 90 天推广序列：人群分层/渠道三选/证据即内容/灯塔口径/P0 账号、签署与发布授权门（2026-07-20）
- [**执行计划清单**](execution-plan-2026-07-12.md) —— ROADMAP「当前承诺」的任务级勾选账：P0/A1/A2/G1/X5 已完成 / E5 软件完成待远端证据 / F 轨条件插队 / 人专属前置（2026-07-21 重基线）
- [X5 executor IPC 实施计划](x5-executor-ipc-plan.md) / [实施记录](x5-executor-ipc-implementation-notes.md) —— `Servo` 边界固定 ABI IPC；软件实现完成，远端 Nightly 首轮待推送（2026-07-21）
| [full-project-review-2026-07-09.md](full-project-review-2026-07-09.md) | 全项目 Review：架构热点、门禁漂移、公共 API 合同与修复队列 | 已执行，P1/P2 修复批输入 |
| [axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md) | `AxisGroup` 按行为簇拆分计划与验收顺序 | **第 1-5 批全部完成（2026-07-21）** |
| [axis-group-batch2-plan.md](axis-group-batch2-plan.md) / [实施记录](axis-group-batch2-implementation-notes.md) | joint look-ahead window 状态 owner 与实现拆分 | **已完成（2026-07-21）**：group.h 7744→6774 行，93/93 + RT + replay 全绿 |
| [axis-group-batch3-plan.md](axis-group-batch3-plan.md) / [实施记录](axis-group-batch3-implementation-notes.md) | Cartesian path/window 状态 owner 与实现拆分 | **已完成（2026-07-21）**：group.h 6774→5426 行，93/93 + RT + replay + 安装态 consumer 全绿 |
| [axis-group-batch4-plan.md](axis-group-batch4-plan.md) / [实施记录](axis-group-batch4-implementation-notes.md) | frame/tool/pose/kinematics 状态 owner 与实现拆分 | **已完成（2026-07-21）**：group.h 5426→4687 行，93/93 + RT + replay + find_package/FetchContent consumer 全绿 |
| [axis-group-batch5-plan.md](axis-group-batch5-plan.md) / [实施记录](axis-group-batch5-implementation-notes.md) | MoveDirect 专属生命周期状态 owner 与 path 实现拆分 | **已完成（2026-07-21）**：group.h 4687→4487 行，93/93 + RT + replay + find_package/FetchContent consumer 全绿 |
| [y7b1-circular-takeover-plan.md](y7b1-circular-takeover-plan.md) / [实施记录](y7b1-circular-takeover-implementation-notes.md) | Y7b1 plain joint-domain circular aborting 接管连续性 | **已完成（2026-07-21）**：当批 22 场景、93/93、ASan/UBSan、RT/replay、消费者与文档门全绿；后续 Y7b2a 已单独解除 Cartesian LINE 来源到 joint LINE/circular |
| [y7b2-cartesian-takeover-plan.md](y7b2-cartesian-takeover-plan.md) / [实施记录](y7b2-cartesian-takeover-implementation-notes.md) | Y7b2a plain Cartesian LINE 来源到 joint linear/circular 的 aborting 接管连续性 | **已完成（2026-07-21）**：复用真实成员输出历史，不改 kinematics ABI；Cartesian 目标另过 feasibility/语义 gate |

已完成的执行文档（R0-R4 拆解、证据包、rewrite-plan、v0.11 草案等）在
[doc/archive/](../archive/)。

---

*本索引最后更新：2026-07-25（H1 主线证据闭合，当前转入 T2b）*
