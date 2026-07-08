# Part 4 路径表与变换 FB 语义矩阵（P 系列第二批，已批准）

> 状态：**已批准**（2026-07-07 起草，2026-07-08 维护者批准并实现）。
> Part 4 剩余 FB 第二批：MC_PathSelect / MC_MovePath + 坐标变换 FB
> 形态（编程接口已有，补标准 FB 门面）。实现：`core/fb/path_table.h`，
> 验收 24 测试（含逐周期路径一致性 ≤1e-9 oracle）。

## 决策点（v1 提案）

| # | FB | 语义 |
|---|-----|------|
| 1 | `MC_PathSelect` | 路径数据 = 调用方持有的**定长路点数组**（≤32 点：目标 + 每点 velocity/blending 参数 + 可选 interpolation_space），FB 做校验（有限性/维数/容量）并发放句柄——KB-016 CamTableSelect 同构（校验 + 句柄传递，核不持有数据） |
| 2 | `MC_MovePath` | 消费句柄：首点 aborting/buffered 进入，后续点按每点 blending 参数以**既有窗口机器**逐点提交（joint 或 cartesian 窗口按 interpolation_space）；整表执行为一条命令生命周期（Busy 到末点 Done，任一点降级按窗口口径报告，CommandAborted 全表终止） |
| 3 | 运行中换表 | 不支持（执行中 PathSelect 新句柄不影响在途 MovePath；重触发 MovePath 走 aborting 语义） |
| 4 | `MC_SetKinTransform` | FB 门面包装既有 `set_pose_kinematics`/`set_kinematics`（standby 守卫沿用）；Execute 边沿语义 + ErrorID 映射 |
| 5 | `MC_ReadCartesianTransform` | FB 门面包装既有帧配置回读（`workpiece_frame_rpy`/`tool_transform_rpy` 原值回显，KB-043 决策 #7）——Enable 电平语义（读类 FB 口径） |

## 退化与拒绝

空表/单点表 `invalid_argument`；点校验失败在 Select 期全表拒绝（零
部分接受）；MovePath 无有效句柄 `invalid_argument`；路径表与手动
blending 提交混用按窗口既有拒绝口径。

## 验收指标

多点表（含混合 blending 参数与降级点）经窗口执行的几何/节拍与手动
逐点提交**逐周期一致 ≤1e-9**（路径表 = 提交语法糖的证明）；生命周期
观察语义（Busy/Done/Aborted/降级报告）；变换 FB 与编程接口等价；
既有回放逐位不变。

## 不做（v1）

路径表文件格式（导入属工具层，同 cam CSV 口径）；>32 点分段表；
运行中表编辑；每点驻留时间（dwell，v2）。
