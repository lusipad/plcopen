# H3 固定基座 RNEA 实现记录

Plan: [h3-rnea-plan.md](h3-rnea-plan.md)

> 状态：实现候选与本地验证已完成（2026-07-26）；PR/main 远端证据待闭合。

## Decisions

- 2026-07-25：实现前在 `.omx/tmp` 做一次性空间向量 RNEA 可行性 spike；
  Windows/MSVC Release、严格浮点、六条 8 关节链顺序求值平均
  `5.511254 µs/cycle`，预先固定的 `≤10 µs` 门通过。该原型只证明预算
  数量级，不复制到 `core/dyn`；正式数据布局、校验和 CI 基准重新实现。
- 2026-07-26：公共面收敛为 `RevoluteBody`、`FixedBaseChainSpec` 与
  `FixedBaseChain` 三个类型；不创建通用空间向量/矩阵框架。成功路径用
  `[angular; linear]` 空间向量和固定数组完成前向/反向递推。
- 2026-07-26：惯量输入要求逐项精确对称，不替调用方平均或修补；正定门用
  Sylvester 判据，主惯量三角关系用坐标不变的 `trace(I)E-2I` 半正定判据。
  容差随矩阵量纲缩放，既不拒绝合法小惯量，也不放宽非物理模型。
- 2026-07-26：运行调用先写局部 torque 数组，完整有限性复验后才提交，
  因而空指针、NaN/Inf 与数值溢出均返回 `invalid_argument` 且输出逐位不变。
- 2026-07-26：独立 oracle 使用测试层 ABA，不从 RNEA 生成质量矩阵；
  另以单摆/2R 闭式公式和世界坐标势能中心差分交叉验证。
- 2026-07-26：六个调用方独立持有的 8 关节实例纳入 Release 硬门；正式实现
  本地实测 `5.050 µs/cycle`，低于预先固定的 `10 µs/cycle` 上限。

## Deviations

- 无。实现未接入 H1、AxisGroup、adapter 或 executor，未扩展浮动基座、
  接触、树/闭链、模型导入和 Python。

## Surprises

- 首版物理可行性容差直接沿用无量纲阈值，会误拒绝量级较小但合法的惯量；
  改为按一阶/二阶/三阶主子式各自量纲缩放后，严格边界与小惯量同时成立。

## Questions for review

- 无开放语义问题；远端评审只需确认合同实现和跨平台门证据。

## Verification

- Windows Debug：91/91 非 fuzz、11/11 fuzz。
- H3 Debug：合约与独立 oracle 2/2。
- H3 Release：合约、oracle、48 关节预算 3/3；`5.050 µs/cycle`。
- RT safety scan：32 个生产头文件通过。
- 安装态 `find_package` 与 FetchContent consumer：构建并运行通过。
- PR 分支 ARM64/QEMU、clang-tidy、Linux/Windows 与文档 strict：待远端
  CI；完成后在证据闭环提交补链接。
