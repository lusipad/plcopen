# Y4b 组同步切换实现记录

> 状态：已完成（2026-07-22，KB-088）。批准规格：
> [组同步切换语义矩阵](../compliance/group-synchronized-switch-semantics.md)。

## Decisions

- 只同步 `GroupTakeoverConnector` 的 vector residual 规划；普通组路径、
  单方向 linear connector、MoveDirect 与 GroupStop 同步停稳不在本批。
- 测试接缝为 `AxisGroup::submit_linear/submit_circular`、`cycle()`、成员
  `snapshot()` 与 connector 公共观测量，不暴露私有 profile 供测试读取。
- 先以公开输出的正交残差完成周期证明 RED，再增加最小规划 helper。
- RED 夹具固定为 3 轴 circular source → linear target；通过新命令
  `command_info().progress` 还原共享标量 base，再从成员输出扣出逐轴
  residual，避免测试读取 connector 私有 profile。

## Deviations

- GREEN 后既有 `check_stop_during_linear_vector_connector` 暴露一处兼容回归：
  fixed-time residual 在第 5 拍进入 GroupStop 时，残差坐标的反向加速可按原
  `Acceleration` 上限规划，但与沿路制动合成后仍是成员减速，导致成员输出
  超过请求的 `Deceleration` 包络并被完整复验拒绝。最小修复仅把 residual
  独立停止规划的 acceleration/deceleration 上限均收紧为
  `min(Acceleration, Deceleration)`；GroupStop 仍分别重规划沿路与 residual，
  未纳入 Y4b 同步停稳。

## Surprises

- 现有普通组路径已由共享标量 profile 构造性同拍到达；Y4b 的真实缺口是
  vector connector 内各成员 residual 仍各走 time-optimal、先后归零。
- RED 实测最后非零周期为 `[459, 386, 327]`，失败点仅为同拍汇合断言；
  夹具、connector 激活、公差管与终点合同均成立。
- 首版 GREEN 使上述周期统一，但既有 GroupStop 场景在成员 1 的第 19 停止拍
  观测到合成加速度 `-1.183743619e-3`，超出为承接入口状态放宽后的
  `-1.110178707e-3`，从而返回 `infeasible`。收紧 residual 停止预算后，
  Y7 的 31 个顶层场景全部通过。

## Verification

- RED：`last_ticks=[459,386,327]`，断言“residuals did not finish same cycle”。
- GREEN：linear 3 轴与 circular 高轴场景均经公开 `command_info/snapshot`
  观测到至少两个非零 residual 同拍结束，且全程不越 `R_tube`。
- 定向：`plcopen_core_y7_group_takeover_tests` PASS（31 个顶层场景，含
  connector 活跃期 GroupStop 回归）。
- 全量：Windows Debug 全构建通过；CTest 93/93（含 11 fuzz）通过；
  `rt_safety_scan` 29 文件通过；replay 18 文件 / 2409 样本零差异；
  `mkdocs build --strict` 与 `git diff --check` 通过。

## Questions

- 无开放问题；v1 范围已由维护者批准。
