# T2b 七关节大模型孪生实施记录

计划：
[`t2b-large-model-twin-plan.md`](t2b-large-model-twin-plan.md)

## Decisions

- 2026-07-25：kickoff 将“大模型”按仓库既有定义收敛为更多关节的本地
  MuJoCo 模型，不解释为大语言模型。默认选择仓库自有 7DOF primitive
  fixture，避免 Menagerie/厂商资产的下载、版本和许可证漂移。
- 2026-07-25：T2b 只为 Python 暴露 q/dq 原子帧所需的最小 H1 门面；
  `tau_ff/kp/kd` 不进入 Python/MuJoCo 消费路径，继续留给 T18/H3。
- 2026-07-25：内存 MJCF spike 在现有 MuJoCo 3.10.0 环境完成 7 joint /
  7 actuator、2,000 tick、2.0 s，状态全有限，墙钟 0.0218 s；`kp=100`
  的保持段末最大误差为 0.00977 rad，因此规格硬门固定为 0.02 rad。
- 2026-07-25：红测先固定 `JointStreamSim` 的最小公开形状、七关节
  feasibility、200 帧闭环、确定性、失败原子性、组级断流和 Rerun footer/
  实体集合。生产代码未改时，新集成测试因模块不存在失败，feasibility
  因 `seven_link.xml` 不存在失败；T2a 原测试仍通过。
- 2026-07-25：最小实现落在 `python/pyplcopen.cpp`、
  `tools/twin/mujoco_rerun_demo.py`、独立
  `tools/twin/mujoco_joint_stream_demo.py` 与 `seven_link.xml`。公共装载
  校验泛化为 1～48 组映射，但 T2a 闭环入口仍要求恰好两个，避免改变
  KB-090 的 CLI/实体合同。

## Deviations

## Surprises

- 当前 PATH 的基础 Python 没安装 `twin` extra；已有
  `build-twin-venv` 才包含 MuJoCo/Rerun。这是预期的可选依赖隔离，不是
  产品缺陷，spike 未安装任何新依赖。
- T2a 的 `LoadedModel`、蓝图、运行结果和循环都用双元素 tuple 固化；
  单纯替换 MJCF 无法形成 T2b，必须泛化装载层并新增独立 H1 旅程。
- 本机没有可直接调用的 `clang-format`；C++ 格式与全量
  `clang-tidy` 交由 Linux 远端门禁复核，不据此跳过其他本地门。

## Questions for review

## Verification

- MSVC Release `pyplcopen` 构建通过；`ci/smoke_test.py` 与
  `pyplcopen_smoke` 通过。
- Windows Debug 全量 99/99（含 11 fuzz）通过；RT scan 31 文件、
  replay 18 文件/2,409 样本通过。
- T2b 专项 4/4、T2a 回归 7/7、feasibility 2/2 通过；默认 CLI 完成
  2,000 tick / 200 帧、零拒绝、零断流，实测最大末误差
  `0.01133902048 rad`。
- CLI 生成的 `.rrd` 为 2,832,521 bytes，并通过
  `rerun rrd verify --check-footers true`；七关节四类时序实体与七级
  link transform 均由测试锁定。
- Python 3.14 从干净临时目录构建
  `pyplcopen-0.20.0-cp314-cp314-win_amd64.whl`，隔离安装后的
  `ci/smoke_test.py` 与七关节 `JointStreamSim` smoke 均通过；这只是
  当前源码安装态证据，不构成发布动作。
- 中英文 MkDocs strict 与 26 对页面 i18n 通过。规格
  [PR #31](https://github.com/lusipad/plcopen/pull/31) 的 Windows、
  Linux GCC/clang、ARM64、Twin、Language Tools 与 E5 全绿后已合入
  `d9b246a`。
- 实现 [PR #32](https://github.com/lusipad/plcopen/pull/32) 全门通过后合入
  `2c73fa7`；合入后的 Windows、Linux、Language Tools、Twin 与
  Documentation 五条主线 workflow 全绿。中英文 Python 与实时集成四个
  公网页面均返回 200，并包含七关节 H1/`JointStreamSim` 标记。
