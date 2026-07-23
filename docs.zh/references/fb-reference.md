# 功能块参考

这页是 `core/fb` 的消费面索引，不是 PLCopen 合规声明。标准条款和已知
边界以 [合规矩阵](https://github.com/lusipad/plcopen/tree/main/doc/compliance)
为准。

## 生命周期

| 类型 | 触发 | 典型输出 | 使用规则 |
|------|------|----------|----------|
| `FbPower` | 每个 scan/cycle 调用 | `status`, `valid`, `error`, `error_id` | `enable` 是电平；轴引用必须有效 |
| `FbReset` | `execute` 上升沿 | `outputs.done` 或 `outputs.error` | 下降沿清除输出 |
| 单轴运动 | `execute` 上升沿提交 | `busy`, `active`, `done`, `command_aborted`, `error` | 用 `outputs.command_id` 与快照完成账本关联 |
| 参数/回读 | `enable` 或 `execute` | `valid`/`done`/`error_id` | 不支持的参数返回 `unsupported` |
| 组管理 | `execute` 上升沿 | `done`, `error_id` | 组引用和所有权先满足前置条件 |

调用顺序通常是：更新输入 → 调用 FB → 调用所属轴/组的规划周期 → 读取
输出。生产双域中，规划线程拥有 FB、`AxisModel` 和 `AxisGroup`；RT 线程
只消费已承诺轨迹帧，不能在 RT 线程直接调用规划型 FB。

## 单轴运动

头文件：`fb/motion.h`。`FbMoveAbsolute`、`FbMoveRelative`、
`FbMoveAdditive`、`FbMoveVelocity`、`FbMoveContinuousAbsolute/Relative`、
`FbHalt`、`FbStop`、`FbHome` 和 `FbSetOverride` 覆盖常用 PTP、连续运动、
受控停止和速度倍率入口。运动输入使用每周期单位；动力学输入必须有限且
为正值。`buffer_mode` 的当前范围和退化规则见 `KB-001`、`KB-009`、`KB-029`。

```cpp
fb::FbMoveAbsolute move;
move.axis_ref = &axis;
move.position = 100.0;
move.velocity = 0.5;
move.acceleration = 0.05;
move.deceleration = 0.05;
move.jerk = 0.01;
move.execute = true;       // 上升沿提交一次
move.call();
axis.cycle();
if(move.outputs.error) {
    // move.outputs.error_id is an rt::ErrorCode
}
```

`done` 表示当前命令 ID 已完成；队列中的后继保持 `busy`，被明确接管才
报告 `command_aborted`。连续运动的 `done` 是持续保持态，不是锁存的停止事件。

## 组路径与同步

头文件：`fb/group.h`、`fb/management.h`、`fb/path_table.h`、`fb/sync.h`。

- `FbMoveLinearAbsolute/Relative` 使用调用方持有的 `GroupPosition`，支持
  线性路径、坐标帧和已批准的公差带 blending。
- `FbMoveCircularAbsolute/Relative` 当前只支持三点 `BORDER` 圆弧；
  `CENTER`/`RADIUS` 明确返回 `unsupported`（`KB-030`）。
- `FbPathSelect` + `FbMovePath` 消费定长路点表；表由调用方持有，最多 32 个路点。
- `FbGearIn`、`FbGearInPos`、`FbCamIn`、`FbCombineAxes` 和 phasing FB 复用
  轴同步快照；gear/cam 的主从轴必须位于同一启用组。
- `FbGroupInterrupt`/`FbGroupContinue` 保留窗口与暂停位置；Interrupted
  状态拒绝 buffered 提交，避免恢复顺序歧义。

## 回零、IO 与诊断

头文件：`fb/homing.h`、`fb/probe.h`、`fb/io.h`。

回零 Step FB 通过 `AxisModel::set_digital_input()` 消费固定数字输入组。
成功的 Step 会清除 `homed`，只有 `FbFinishHoming` 置回 `homed`。软件
StepBlock 不证明真实机械堵转安全，厂商编码器和安全回路仍由集成商负责。

TouchProbe 捕获的是触发周期的 `actual_position`；数字 IO 固定为输入 4
通道、输出 4 通道。所有错误都通过 `error_id` 返回，不在周期路径抛异常。

## 重要边界

- `core/fb` 当前包含 126 个 `Fb*` 类型，其中标准消费面按 Part 1/2 43、
  Part 4 68、Part 5 11 记账；类型或同名门面存在不等于 PLCopen 完整合规，
  正式声明、部分 E/O 字段、真机与认证边界见逐项审计。
- `BufferMode`、TransitionMode、坐标系和非 ACS 组合按矩阵显式拒绝。
- `core/fb` 是语义门面，规划计算仍发生在 `AxisModel`/`AxisGroup` 的规划域。
