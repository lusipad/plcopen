# H2 SerialChain 实施记录

计划：[`h2-serial-chain-plan.md`](h2-serial-chain-plan.md)

## 摘要

H2 已完成：固定容量 standard/modified DH 正解、确定性数值 DLS 逆解、
三类失败、严格/best-effort 分流、7DOF 偏好、零分配与性能趋势均已落地。
既有解析插件、L5 六轴 seam 与回放语义不变。

## 决策

- 复用 `PoseKinematics` 严格 seam，不改现有 vtable。
- 7DOF 本批止于 `core/kin` 求解面；现有 L5 六轴位姿组守卫保持不变。
- 首批 DH 表只承载转动关节，支持 standard/modified 两种整链约定。
- 新错误码追加到 `ErrorCode` 尾部，避免改变既有枚举数值。
- 7DOF 验收与 benchmark 共用交替扭角的非退化 7R fixture；零空间偏好只
  尝试一次、复验主任务双门，并占用固定 32 次总循环中的一次。
- 数值链不另造 workspace-outside 错误码；预算耗尽、最大阻尼无下降、
  硬限位投影和 seed 步门分别稳定映射到
  `not_converged` / `singular_region` / `limit_infeasible` / `infeasible`。

## 偏差

- v2 文本写“进 `CARTESIAN_METRICS`”。实际采用同一 benchmark 工件中的
  `SERIAL_CHAIN_METRICS` 独立行：首个 PR 的 E5 job 会用 base commit 的严格
  parser 读取 head 输出；若给既有行加字段，旧 parser 会把合法 head 判成
  unknown token。独立前缀让旧 parser 在迁移 PR 忽略新行，合并后由新版 parser
  正式纳入 base/head 趋势，预算语义不变。

## 意外

- 仓库通用 RT 技能仍写着“数值逆解 ≤3 次”，但 H2 已批准的专项合同明确
  固定上限为 32；本批以更具体、更新的 v2.1 合同为准。
- 首轮 4 万例回环暴露了 `acos(trace)` 在极小姿态误差附近的精度损失：
  真值已接近零时会被放大到约 `2.1e-8` rad。SO(3) log 改为
  `atan2(skew_norm, trace_cosine)`，仅在 π 邻域退回轴角提取，随后
  2 万 6DOF + 2 万 7DOF 回环通过 1e-9 双门。
- 退化同轴腕 fixture 初测虽快，但不足以证明真实冗余臂；替换为共享的
  非退化交替扭角 7R fixture，并让 benchmark 启用偏好路径。1e-5 rad
  hot-seed Debug 本机样本为 22.650、23.900 µs/解，独立复审样本为
  23.850 µs/解；规范事实只保留“通过 30 µs 硬门”。Windows CI 新增专项
  Debug CTest，E5 继续承担 Release 同机趋势。

## 验证

- Windows Debug 全量 CTest 96/96 通过（含 11 项 fuzz、2 万 6DOF +
  2 万非退化 7DOF 回环、冻结窗口零分配与 benchmark）。
- 独立 H2 Debug benchmark CTest 通过；最终单跑观测
  `serial_chain_ik_us=22.650`，低于 30 µs 硬门。
- RT 静态扫描 31 文件通过；18 份回放、2409 个样本校验通过。
- benchmark trend 15/15 单元测试、Release `pyplcopen_smoke`、
  `find_package` / `FetchContent` 两个公开消费者和安装头文件检查均通过。
- `mkdocs build --strict` 通过。本机未安装 Doxygen/Graphviz，API 文档构建
  留给 PR Documentation CI 复验，不冒充本地证据。
- 两轮独立架构复审关闭退化 7DOF fixture、循环外偏好、Debug 门与合同漂移
  问题；最终复审未发现 P1/P2。

## 待确认问题

无。
