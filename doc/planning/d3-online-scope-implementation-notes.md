# 实施记录 — D3 在线调试示波器

计划：[d3-online-scope-plan.md](d3-online-scope-plan.md)

## 摘要

实现与交付已闭合。`PLCT v1` 格式不变；参考 executor 改为 RT SPSC 发布、
非 RT writer 持续落盘，工具侧完成严格读取、在线跟随、阈值触发、精确窗口、
PLCT/CSV/HTML 与可选 Rerun 输出。

## Decisions

- 2026-07-24：维护者授权按 D3→D1→H1→T2b→H3 顺序开发；D3 v1
  使用既有 `PLCT v1`/CLI seam，ST 调试 trace 保持独立。
- 2026-07-24：writer 保留在 `rt_executor_demo.cpp`，没有因单一生产者新增
  core transport 抽象；RT 路径只执行 SPSC push 与 lock-free 丢样计数。
- 2026-07-24：trigger 留在 Python 工具侧；首批只有单轴单字段阈值穿越，
  Rerun 保持 `scope` 可选依赖，默认 wheel/core 依赖不变。

## Deviations

- 无。

## Surprises

- `rerun-sdk 0.34.1` 的 `RecordingStream.flush()` 不接受 `blocking` 参数；
  adapter 改为 `flush()` 后显式 `disconnect()`，并以官方 `rrd verify
  --check-footers true` 验证 footer。
- CLI 配置复查发现负窗口参数可能在失败前先建立 Rerun sink；验证已前移，
  新增回归确保无效配置不创建输出。
- 双轴代码审查发现 follow 原先把 timeout 当作空闲时限，持续无触发数据可
  无限续期；现改为命令总时限，并确保静态坏输入在建立 Rerun sink 前失败。

## Questions for review

- 无。

## Verification

- TDD：缺失 `TraceTrigger` / follow / strict error API 的定向测试先红，完成
  实现后转绿。
- 定向 Python/executor/Rerun：14/14 通过（含在线可见、总时限、严格截断、
  失败原子性与 Rerun footer）。
- 本地 Debug 全量 CTest 97/97、RT scan 31 文件、18 份/2409 样本 replay
  零差异、双语 MkDocs strict 与 25 对 i18n 校验通过。
- [PR #23](https://github.com/lusipad/plcopen/pull/23) 的
  [Windows](https://github.com/lusipad/plcopen/actions/runs/30068134503)、
  [Linux](https://github.com/lusipad/plcopen/actions/runs/30068134506)、
  [Twin/Scope](https://github.com/lusipad/plcopen/actions/runs/30068134505) 与
  [Documentation](https://github.com/lusipad/plcopen/actions/runs/30068134539)
  全绿后合并。
- 合并后主线
  [Windows](https://github.com/lusipad/plcopen/actions/runs/30071051259)、
  [Linux](https://github.com/lusipad/plcopen/actions/runs/30071051258)、
  [Twin/Scope](https://github.com/lusipad/plcopen/actions/runs/30071051277) 与
  [Documentation](https://github.com/lusipad/plcopen/actions/runs/30071051246)
  全绿；[中文 Scope](https://lusipad.com/plcopen/guides/online-scope/)、
  [英文 Scope](https://lusipad.com/plcopen/en/guides/online-scope/)及两份
  sitemap 公网复验均为 200。
