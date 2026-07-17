# Part 4 路径表与变换 FB 语义矩阵（P 系列第二批，已批准）

> 状态：**已批准**（2026-07-07 起草，2026-07-08 维护者批准并实现）。
> Part 4 剩余 FB 第二批：MC_PathSelect / MC_MovePath + 坐标变换 FB
> 形态（编程接口已有，补标准 FB 门面）。实现：`core/fb/path_table.h`，
> 验收 24 测试（含逐周期路径一致性 ≤1e-9 oracle）。

## 决策点（v1 提案）

| # | FB | 语义 |
|---|-----|------|
| 1 | `MC_PathSelect` | `PathDescription` 是调用方持有的**定长源路点数组**（≤32 点：目标 + 每点 dynamics/transition + interpolation_space）；`PathData` 是调用方另行持有的选择结果。FB 先全表校验，再原子复制并写入非零 selection handle、轴数和点数；失败不改写上一个有效结果。两者分别对应 supplier-specific `MC_PATH_REF` 与 `MC_PATH_DATA_REF`，不以一个可变对象冒充两种引用。 |
| 2 | `MC_MovePath` | 消费已选择的 `PathData`。`CoordSystem` 写入每一段并在 submit 期完成 ACS/MCS/PCS 变换；`BufferMode` 直接决定首段对在途命令的 abort/queue/blend 接管；`TransitionMode` / `TransitionParameter` 直接描述前序命令到首段的相邻过渡。后续段仍按 `PathDescription` 生成的逐点 transition 进入既有窗口机器。 |
| 3 | 运行中换表 | 不支持（执行中 PathSelect 新句柄不影响在途 MovePath；重触发 MovePath 走 aborting 语义） |
| 4 | `MC_SetKinTransform` | `KinTransformRef` 用 `none/kinematics/pose` tag 明确插件种类，不从空指针组合猜型；`none` 原子清除插件，另两类原子安装所选插件并清除另一类；immediate/queued 复用组管理队列，非法 tag 原子拒绝 |
| 5 | `MC_ReadCartesianTransform` | CoordSystem=PCS/TCS 选择工件帧/工具帧，固定 6D 原值回显到 TransX/Y/Z 与 RotAngle1/2/3；其他坐标系 `unsupported` |
| 6 | `MC_SetCartesianTransform` | TransX/Y/Z 与 RotAngle1/2/3 组成固定 6D RPY；CoordSystem=PCS/TCS 选择写入帧，immediate/queued 复用组管理队列 |

## 退化与拒绝

空表/单点表、维数/非有限值/非正 dynamics、非法 enum 或
`TransitionMode=None` 携带非零参数均为 `invalid_argument`；当前未实现的
坐标系或 transition mode 为 `unsupported`。PathSelect 失败保持旧 PathData
逐字不变；MovePath 在提交首段前重验调用方可写的 PathData 与四个门面输入，
因此参数错误不会产生部分路径。与手动 blending 提交混用按窗口既有拒绝口径。

## 验收指标

多点表（含混合 blending 参数与降级点）、PCS 变换、Buffered 接管及
MovePath 首段 transition 经窗口执行的几何/节拍与手动逐点提交
**逐周期一致 ≤1e-9**（路径表 = 提交语法糖的证明）；生命周期观察语义
（Busy/Done/Aborted/降级报告）；变换 FB 与编程接口等价；既有回放逐位不变。

## 不做（v1）

路径表文件格式（导入属工具层，同 cam CSV 口径）；>32 点分段表；
运行中表编辑；每点驻留时间（dwell，v2）。
