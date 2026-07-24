# H1 同步关节流实施记录

Plan: [h1-synchronized-joint-stream-plan.md](h1-synchronized-joint-stream-plan.md)

## 摘要

2026-07-25 开始实现。批准范围是 48 关节固定容量原子混合帧、
direct/upsample 分层延迟合同、组级断流、混合字段斜坡和 setpoint 快照；
不包含 actual feedback、跨线程/进程、Python、T2b、H3、T18 或真机声明。

## Decisions

- 2026-07-25：维护者以“按照顺序开发吧”批准
  [H1 v2.1 矩阵](../compliance/trajectory-stream-semantics.md#v21-增补h1-同步关节流组已批准2026-07-25)；
  [PR #28](https://github.com/lusipad/plcopen/pull/28) 已合入主线提交
  `036dbf3d2a7a93ec04a21a5dcadf384ff2f59814`。
- 第一实现批按 TDD 拆为：
  1. `StreamFilter1D` 可预算重规划与显式 dropout 入口；
  2. `JointStreamGroup` 原子帧、direct/upsample、组级 watchdog 与快照；
  3. 48 关节预算、消费者与文档证据。
- 逐关节 `timeout_cycles` 不同时，frame 会话采用最小值作为组级
  watchdog 阈值，确保所有关节同拍进入断流且没有成员超过自身上限。
- `upsample` 每拍从轮转起点遍历全部成员：T24 命中不消耗慢解额度，
  慢解最多 10 个，延期成员继续旧安全剖面；48 关节全慢输入的待解队列
  为 `38→28→18→8→0`，累计延期 92。
- Windows MSVC Release 微基准实测：
  direct 稳态 `0.15µs/拍`、direct 对抗帧 `0.75µs/拍`、
  upsample 快路径 `12.90µs/拍`、全慢预算拍 `131.00µs/拍`，
  均低于批准的 `300µs` 软件门。

## Deviations

- 无。

## Surprises

- 初始快路径夹具的位姿并不满足 T24 解析包络，实际走慢解；改为
  `{q=0,dq=0.001}` 的已证明命中输入后，48 关节当拍零延期。
- 初始五拍全慢夹具使用 `timeout_cycles=2`，第 4 拍会按合同进入断流，
  与纯重规划队列观测相互干扰；预算夹具单独提高 watchdog，断流行为由
  独立用例覆盖。
- 首轮 Linux GCC 主门的代码、CTest、benchmark、安装态/FetchContent 与
  Conan 均通过，但固定公开 `v0.20.0` 的 vcpkg port 被共用消费者默认要求
  H1 新 API 阻断。消费者因此新增显式 `PLCOPEN_SMOKE_REQUIRE_H1`：
  当前源码/安装态默认开启，历史发布包预检显式关闭；未改变 port 版本或
  伪装 H1 已进入 `v0.20.0`。

## Verification

- Windows MSVC Debug 全量 CTest：99/99，通过 11 项 fuzz。
- Windows MSVC Release `STREAM_METRICS`：48 关节四项 H1 指标均低于
  300µs；既有 28 关节 legacy 指标也低于原门槛。
- 安装态 `find_package` 与源码态 `FetchContent` 消费者均编译并运行
  `stream/joint_group.h` 的 H1 direct 帧冒烟；关闭 H1/H2 未发布面后，
  同一消费者也已对仅含 legacy `JointStreamGroup::MaxJoints=32` 的历史
  安装树编译并运行通过。
- RT safety scan：31 文件通过；replay fixture：18 文件、2409 样本通过。
- 中英文 MkDocs strict 与 i18n：26 对页面通过。当前 Windows 主机未安装
  Doxygen，CMake `docs` target 只验证了仓库定义的提示型 fallback；真实
  Doxygen/Graphviz、Linux GCC/Clang、ARM64 与远端工作流仍待 PR 门禁。

## Questions for review

- 无。
