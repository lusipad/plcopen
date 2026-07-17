# MC_DigitalCamSwitch 多轨语义矩阵（P1-A4）

> 状态：**已预批准**（2026-07-12，维护者批准合规计划后续全部调整）。
> 本批关闭 D-13 及 E 级软件执行语义；保持四通道数字输出硬件边界与
> 周期路径零分配。PLCopen Appendix B 中 action/track 结构元素不冒充 FB 输出。

## 1. 数据模型

`CamSwitchTable<Capacity>` 为调用方持有的固定容量表；FB 仅保存只读 view。
每项 `CamSwitchAction` 包含 `track_number`（1..4）、`on_position`、
`off_position`、厂商周期扩展 `period`，以及标准结构元素
`axis_direction`、`cam_switch_mode`、`duration_ns`。`CamTrackOption` 按轨道
保存 `on_compensation_ns/off_compensation_ns`。Capacity 为 8；多个 action
可属于同一 track，任一 action 命中则该 track 为 TRUE。

## 2. FB 接口

| 字段 | 语义 |
|------|------|
| Axis | B 输入，位置/速度来源由 ValueSource 选择 |
| Switches | B 输入，固定表 view；空表/悬空由调用方生命周期合同约束 |
| Outputs | E IN_OUT；可选调用方 `bool` span，逐周期镜像四轨计算值 |
| TrackOptions | E IN_OUT；可选调用方 track option span |
| Enable | B 输入，TRUE 时每周期计算所有 action |
| EnableMask | E 输入，bit0..3 分别启用 TrackNumber 1..4 |
| ValueSource | E 输入，选择 command/actual 位置与速度 |
| InOperation | B 输出，表合法且本周期所有轨道已写入时 TRUE |
| Busy | E 输出；软件同周期启停，恒为 FALSE |
| Error/ErrorID | 错误输出；失败时 InOperation=FALSE 并清本 FB 已控制轨道 |

移除旧单窗口公开字段 `output_number/on_position/off_position/period/value/valid`；
迁移为一项 action 的表即可获得同等行为。

## 3. 验证与原子性

- 表加载/每周期调用先完整验证 size、有限值、period、TrackNumber，再写输出；
- 任一非法项导致整表拒绝，所有本 FB 已控制轨道清 FALSE，不出现半写；
- 非周期窗口要求 on<=off；周期窗口允许跨零；period 必须 >=0；
- TrackNumber 使用 PLCopen/Beckhoff 的 1-based 轨道号，超出 1..4 返回 `unsupported`；
- position action 按 AxisDirection 过滤；on/off compensation 以整数纳秒给出，
  用选定位置源的速度预测开/关边沿，负值提前、正值延后；
- time action 在 FirstOnPosition 穿越时触发，`Duration` 转为整数周期倒计时；
  `call(task_period_ns)` 拒绝非正周期，不做浮点时间累加；
- Disable 清全部受控轨道及输出状态；切换表先清旧表不再拥有的轨道；
- Axis/null、空表、越容量、NaN/Inf 返回 `invalid_argument`。

## 4. 验收

1. 单轨单窗口、1-based TrackNumber 与多窗口 OR；
2. 单轨多窗口 OR、多轨独立、周期跨零、负位置全部通过；
3. 表切换/Disable 清旧轨道；非法中间项证明无半写；
4. 8 action 容量边界、重复 track、track 3/4 边界；
5. Outputs/TrackOptions/EnableMask/ValueSource/Busy、方向、时间 cam、补偿；
6. 全构建、CTest、RT-safety、回放、矩阵和 docs 全绿。

## 5. 不做

Hysteresis、brake cam、retrigger 策略、外部硬件 compare unit offload 不在本批；
未定义模式显式拒绝，不伪装为 position cam。

---

*创建并预批准：2026-07-12。*
