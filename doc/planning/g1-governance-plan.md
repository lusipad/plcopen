# G1 治理文档实施计划

> 状态：**已批准执行（2026-07-21）**。维护者在 A2 收口后以“继续”授权
> 进入 G1。本批只把仓库已经执行的贡献、治理与安全边界公开化，不新增
> 组织账号、权限、SLA、LTS 或外部合规承诺。

## 1. 最可能调整的决策

| ID | 决策 | 置信度 | 什么证据会推翻 |
|----|------|--------|----------------|
| G1-D01 | 根目录新增 `CONTRIBUTING.md`、`GOVERNANCE.md`、`SECURITY.md`，现有细节仍以 `CLAUDE.md`、项目技能和 CI 为准 | 高 | 仓库已有同名 authority；当前检索确认不存在 |
| G1-D02 | 治理如实采用当前单维护者模型；AI 是执行工具，不是维护者、投票者或继任者 | 高 | 仓库权限或公开治理角色发生变化 |
| G1-D03 | 明示 bus factor=1、当前无指定继任者；不制造自动继承或“官方 fork”承诺 | 高 | 维护者公开任命第二维护者并授予权限 |
| G1-D04 | 私密报告功能关闭期间，公开 issue 只允许请求私密联系，不得包含漏洞细节 | 高 | GitHub Private Vulnerability Reporting 启用并完成实测 |
| G1-D05 | 当前支持为 `main` 与最新发布版的 best-effort；无 SLA、LTS 或旧版回移植保证 | 高 | 维护者批准正式支持周期 |
| G1-D06 | ST 威胁模型覆盖源码编译、`Program` load、预算 scan、tasking 与 debug/force 宿主权限；不把 VM 描述成安全沙箱 | 高 | 新增 raw artifact 导入、网络调试协议或库内认证授权 |

## 2. 假设

| 假设 | 置信度 | 来源 |
|------|--------|------|
| `STATUS.md` 是当前状态入口，`ROADMAP.md` 是当前承诺源 | 高 | `CLAUDE.md` 与既有计划约定 |
| 当前仓库 owner/唯一维护者为 `@lusipad`，默认分支为 `main` | 高 | Git remote 与 GitHub 仓库元数据（2026-07-21） |
| GitHub Private Vulnerability Reporting 当前关闭 | 高 | GitHub repository API（2026-07-21） |
| ST 没有公开 raw bytecode 反序列化入口，debug/force 不提供库内认证 | 高 | `core/st` 公共入口与 L7 合同审计 |
| 现有 A2 工作树属于同一连续计划，G1 不回退或重排这些改动 | 高 | 当前 `git status` 与上一批实施记录 |

## 3. 偏差策略

- 遇到文档与实现不一致时，以代码、CI 和 `CLAUDE.md` 为准，选择最小、
  可逆、无新增承诺的写法，并记录在实施说明。
- 可以同步与 G1 直接冲突的 README、计划状态和 issue/PR 模板；不借机清理
  其他历史文档。
- 一旦需要启用外部仓库设置、公开个人联系信息、改变 LICENSE/NOTICE/
  PROVENANCE、承诺响应时间或指定继任者，停止该项并列为人侧动作。

## 4. 机械工作（低评审价值）

1. 新增三份根文档并互相链接。
2. 把 issue 模板的废弃 T0-T3 下拉框改为当前对齐点说明。
3. 在 README、文档站入口、CHANGELOG、STATUS/ROADMAP/主计划中同步 G1。
4. 建立实施记录，保留发现、偏差和待人处理项。

## 5. 验证

- 三份根文档和所有新增相对链接可解析，MkDocs 严格构建通过。
- issue form YAML 可解析，且不再出现“当前政策为 T0-T3”的错误引用。
- SECURITY 的默认容量逐项可回指 `CompileOptions`，不声称 sandbox、SLA、
  LTS、认证或已启用的私密报告功能。
- 运行仓库五项完成态门禁：Debug build、全量 CTest、RT scan、回放、
  `git diff --check`。
- 独立复核贡献命令、治理权限和 ST 威胁口径，无未证实承诺。
