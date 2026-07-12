# MC_DigitalCamSwitch 多轨语义矩阵（P1-A4）

> 状态：**已预批准**（2026-07-12，维护者批准合规计划后续全部调整）。
> 本批关闭 D-13，补齐 B 级 Switches、TrackNumber、InOperation；保持现有
> 四通道数字输出硬件边界与周期路径零分配。

## 1. 数据模型

`CamSwitchTable<Capacity>` 为调用方持有的固定容量表；FB 仅保存只读 view。
每项 `CamSwitchAction` 至少包含：`track_number`、`on_position`、
`off_position`、`period`。Capacity 首版为 8；TrackNumber 映射数字输出
0..3。多个 action 可属于同一 track，任一窗口命中则该 track 为 TRUE。

## 2. FB 接口

| 字段 | 语义 |
|------|------|
| Axis | B 输入，位置来源为 command position |
| Switches | B 输入，固定表 view；空表/悬空由调用方生命周期合同约束 |
| Enable | B 输入，TRUE 时每周期计算所有 action |
| InOperation | B 输出，表合法且本周期所有轨道已写入时 TRUE |
| Error/ErrorID | 错误输出；失败时 InOperation=FALSE 并清本 FB 已控制轨道 |

移除旧单窗口公开字段 `output_number/on_position/off_position/period/value/valid`；
迁移为一项 action 的表即可获得同等行为。

## 3. 验证与原子性

- 表加载/每周期调用先完整验证 size、有限值、period、TrackNumber，再写输出；
- 任一非法项导致整表拒绝，所有本 FB 已控制轨道清 FALSE，不出现半写；
- 非周期窗口要求 on<=off；周期窗口允许跨零；period 必须 >=0；
- TrackNumber 超出 0..3 返回 `unsupported`；重复轨道按 OR 合并；
- Disable 清全部受控轨道及输出状态；切换表先清旧表不再拥有的轨道；
- Axis/null、空表、越容量、NaN/Inf 返回 `invalid_argument`。

## 4. 验收

1. 单轨单窗口与旧行为逐周期一致；
2. 单轨多窗口 OR、多轨独立、周期跨零、负位置全部通过；
3. 表切换/Disable 清旧轨道；非法中间项证明无半写；
4. 8 action 容量边界、重复 track、track 3/4 边界；
5. 全构建、CTest、RT-safety、回放、矩阵和 docs 全绿。

## 5. 不做

Time-based CamSwitchMode、Duration、AxisDirection、Compensation、Hysteresis、
外部硬件 compare unit offload 均为 E 级或硬件扩展，另批处理。

---

*创建并预批准：2026-07-12。*
