# G1 治理文档实施记录

计划：[G1 治理文档实施计划](g1-governance-plan.md)

> 进度摘要：已完成（2026-07-21）。三份根治理文档、公开入口、issue
> 对齐点和计划总账均已同步；没有修改账号、权限、许可证或远端安全设置，
> SLA、LTS 与继任任命仍保持人侧边界。

## Decisions

- 三份文档引用 `CLAUDE.md`、CI 和合规合同，不复制一套会漂移的平行规则。
- 安全版本政策只写 best-effort；最新发布之外不保证回移植。
- ST 资源表明确是 `CompileOptions` 默认值，不冒充不可调硬上限。

## Deviations

- 无。issue 模板修正和公开入口同步均在计划允许的 G1 一致性范围内。

## Surprises

- `.github/ISSUE_TEMPLATE/task.yml` 仍要求使用已被 `CLAUDE.md` 明确替代的
  T0-T3 变更政策；它属于 G1 入口一致性问题，本批最小修正。
- GitHub Private Vulnerability Reporting 于 2026-07-21 实测为关闭；本批
  不擅自改变远端设置，先提供不泄露细节的联系请求流程。

## Questions for review

- 人侧后续：是否启用 GitHub Private Vulnerability Reporting。启用后应把
  `SECURITY.md` 的临时联系请求流程替换为实测的私密报告入口。

## Verification

- `.github/ISSUE_TEMPLATE/task.yml` 与 `mkdocs.yml` 均通过 YAML 解析；当前
  issue 入口不再把 T0-T3 写成现行政策。
- 13 份相关 Markdown 的本地链接逐条解析通过；`mkdocs build --strict`
  通过。
- Windows Debug 构建通过；全量 CTest 92/92 通过，包含 11 项 fuzz。
- RT-safety scan 27 文件通过；18 份回放、2409 samples 通过；
  `git diff --check` 仅有 Windows LF/CRLF 提示。
- 独立 ST 安全复核确认默认资源表、load/scan/debug/force 边界与代码一致；
  独立治理复核确认无虚假 SLA/LTS、继任、权限或安全渠道承诺。两项均
  `APPROVE`。
- GitHub repository API 于 2026-07-21 返回 private vulnerability reporting
  `enabled=false`；本批未改变该远端状态。
