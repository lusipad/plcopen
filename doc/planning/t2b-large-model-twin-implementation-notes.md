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

## Deviations

## Surprises

- 当前 PATH 的基础 Python 没安装 `twin` extra；已有
  `build-twin-venv` 才包含 MuJoCo/Rerun。这是预期的可选依赖隔离，不是
  产品缺陷，spike 未安装任何新依赖。
- T2a 的 `LoadedModel`、蓝图、运行结果和循环都用双元素 tuple 固化；
  单纯替换 MJCF 无法形成 T2b，必须泛化装载层并新增独立 H1 旅程。

## Questions for review
