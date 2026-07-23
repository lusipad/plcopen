# E5 基准趋势管线

## 1. 目标与边界

E5 只回答一个问题：一次提交是否让本仓库已经声明的算法质量或软件执行
成本发生了可重复的结构性退化。CI 观测只构建并运行 `plcopen` 自身代码，
不下载竞品、历史 artifact 或外部基准数据，也不把开发机观测升级为真机
WCET/实时认证结论。

墙钟数字不跨 runner 比较。PR、`main` push 的 base/head 在同一台
GitHub-hosted Linux runner、同一 GCC Release 环境中重新构建并交替采样；
artifact 只承担历史证据留存，不作为后续比较输入。

## 2. 指标合同

| 指标 | 机读来源 | 趋势判定 | 既有绝对门 |
|------|----------|----------|------------|
| OTG `excess_cycles` | `plcopen_core_otg_optimality_oracle` 的每域 `OTG_ORACLE_METRICS` | 域、attempted、compared、hard-gate 身份必须一致；planner fail、oracle miss、negative fail、最大/平均 excess 均不得增加 | hard-gate 域负 excess 仍由 oracle 自身直接失败 |
| ST 每指令成本 | `ST_WCET_METRICS.mixed_observed_ns_per_instruction`；实际执行指令数同时记录 | base/head 成对墙钟比 | ST L0 的约 10⁶ 混合指令 ≤100 ms Release 门继续独立生效 |
| 解析笛卡尔 IK | 既有 `cartesian_ik_cycle_us`（软件计划中的 `cartesian_ik_us` 即此逐周期指标） | base/head 成对墙钟比 | ≤50 µs 硬门继续独立生效 |
| H2 数值 IK | `SERIAL_CHAIN_METRICS` 的非退化 7DOF、preference-enabled、1e-5 rad hot-seed `serial_chain_ik_us` | base/head 成对墙钟比 | Windows CI 专项 Debug CTest ≤30 µs/解 |
| 窗口重规划 | 固定 64 段路径、完整消费 entry/exit 结果的 `window_replan_us` | base/head 成对墙钟比 | 无跨机器绝对门；它是规划域观测，不是周期路径成本 |

`scalar_*` 与 `native_*` ST 校准值继续进入 JSON，供 A2 诊断使用，但主机
噪声较大，不参与 E5 回归判定。OTG 行同时报告 `attempted`、
`planner_fail`、`oracle_miss`、`negative_fail` 和 `compared`，禁止通过静默
丢弃困难样本制造“改善”。

E5 的 base/head 趋势仍使用 Linux/GCC Release 同机对拍；H2 的绝对预算是
已批准的 Debug 口径，因此 Windows 主门另建 Debug
`plcopen_core_benchmark` 并直接运行同一个 30 µs 失败门。两者复用同一份
交替扭角 7DOF fixture，避免测试臂与预算臂漂移。

## 3. 成对采样与阈值

比较器位于 `tools/benchmark_trend.py`，策略位于
`ci/benchmark-trend-policy.json`，仅使用 Python 标准库。

1. base/head 各预热一次；OTG 再各采一次确定性结果。
2. 墙钟指标采 9 对，顺序按 AB/BA 交替，记录每个原始样本和 head/base
   ratio。
3. median ratio >1.10 记 warning。
4. median ratio >1.20 且至少 7/9 对 >1.10 时，再采一组 9 对；第二组仍
   满足相同条件才失败。
5. 输出缺行、重复字段、未知字段、非有限值、非法环境身份或超长行均
   fail closed。base/head 的平台、架构、编译器及版本、构建类型，以及
   指令数、工作单位、窗口深度和绝对预算等 workload 身份必须一致。

确定性 OTG 退化不使用墙钟容差，直接失败。需要有意改变 oracle 样本或
质量取舍时，应单独提交声明变更，而不是在算法提交里放宽门槛。

## 4. CI、信任与历史

`.github/workflows/linux-ci.yml` 的 `benchmark-trend` job 使用
`contents: read`，checkout 不保留凭据，设置 20 分钟 job 上限；不使用
`pull_request_target`、secrets、PR 写权限或 shell 求值 benchmark 输出。
比较器用 `subprocess` 参数数组直接启动已知可执行文件，每次运行上限
120 秒，并严格解析固定字段。

可比较时，比较器和策略从 base worktree 读取，因此同一个 PR 不能靠修改
自身阈值让自己通过。输出/schema 演进采用两步：先让 base 解析器兼容新旧
格式并合入，再改 producer；最后才能移除旧格式。

每次运行上传 schema v1 JSON，保留 90 天。首次部署或手动触发没有可用
base 工具时只执行 `record` bootstrap；该次不伪装成回归比较。bootstrap
合入后的下一次可比较提交开始执行 report-only 比较：比较器仍以非零退出码
标记退化，workflow 记录该信号但不让 PR 或 `main` 变红。历史 artifact 不会
被高权限流程下载或执行。

## 5. 本地验证

```text
python -m unittest tools.test_benchmark_trend -v

python tools/benchmark_trend.py record \
  --policy ci/benchmark-trend-policy.json \
  --benchmark <release-build>/core/plcopen_core_benchmark \
  --oracle <release-build>/core/plcopen_core_otg_optimality_oracle \
  --output <build-dir>/benchmark-trend.json
```

`compare` 模式再传入明确的 base/head 两组可执行文件和 SHA；不得把不同
平台、编译器或 build type 的结果拼在一起。
