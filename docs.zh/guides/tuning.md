# 运动调参指南

调参顺序固定为：周期 → 位置/速度单位 → 动力学限值 → 路径几何 →
override/前瞻。先建立能回放的基线，再改一个变量。

## 周期和单位

核心单位是“每周期”。Python 使用 `CycleConfig.at_1khz()` 等辅助把 SI
单位换算为每周期值；C++ 调用方应在边界一次完成秒制到周期制换算，核心
内部不保存浮点时间。

## 动力学参数

`velocity` 决定路径速度上限，`acceleration`/`deceleration` 决定速度包络，
`jerk` 决定加速度变化率。建议先让 `jerk` 足够高验证几何，再逐步降低
以减少冲击。所有值必须有限且大于零，非有限值或不满足软件限位会被拒绝。

每次修改后记录总周期数、峰值速度、峰值加速度、峰值 jerk 和终点误差。
使用黄金回放验证没有意外 setpoint 变化。

## 路径与 blending

- 线性/圆弧先单独验证端点和半径，再启用 blending。
- `MaxCornerDeviation` 的公差是路径偏差上限，不是速度倍率。公差越小，
  过渡段越短，越可能退化为 buffered 停止。
- 连续 blending 进入前瞻窗口；过迟提交、反折、容量不足或不优于完全
  停车基线时会显式降级，并可通过 `last_blend_degraded_command()` 查询。
- 窗口默认容量为 64 段；用 `set_window_depth()` 缩小它前先确认最大连续段数。

## 同步和流

cam 表优先使用 C2 `spline` 插值；周期表首尾 slave 必须一致。换表时必须
在当前 master 相位让新旧从轴位置落在 tolerance 内，否则 `cam_switch`
拒绝并继续旧表。轨迹流设置 `timeout_cycles` 和 `extrapolation_cycles` 时，
应覆盖正常生产抖动但短于危险失联时间；断流后由滤波器受控停，不要自行
把目标跳到零。

## 验证门

```bash
ctest --test-dir build-sync -C Debug -R \
  "commercial_precision|cam_tests|a3_circular|a4_blending|a5_lookahead" \
  --output-on-failure
```

当前软件精度证据：Bezier 稳速波动 `0.0775%`、圆弧约 `7.6e-12%`、cam
相位误差 `0` 周期、blending 偏差不超过用户公差。它们是 setpoint 证据，
不能替代真实驱动器跟随误差和 72h RT 抖动报告。
