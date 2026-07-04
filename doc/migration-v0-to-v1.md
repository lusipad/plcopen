# v0.x → v1.0 迁移指南（旧 `src/` 线 → 新核 `core/`）

状态：随 `v1.0.0-alpha` 草案发布，对应 [rewrite-plan](planning/rewrite-plan.md) §5 DoD 第 5 条。
语义仲裁以 [doc/compliance/](compliance/) 矩阵与黄金回放 diff 为准；本文只描述消费面变化，不新增语义承诺。

## 版本与维护口径

| 线 | 最后功能版本 | 维护政策 |
|----|--------------|----------|
| 旧 `src/`（v0.x） | `v0.11.0` | P0-only 维护窗口（见 [EOL 公告草案](planning/r4-evidence-package.md)），窗口结束后仅保留 tag 作回放基线 |
| 新核 `core/`（v1.x） | `v1.0.0-alpha`（草案） | 活跃开发线，默认消费面 |

## 构建与 CMake 消费

| 项 | v0.x | v1.0 |
|----|------|------|
| 包目标 | `plcopen::plcopen`（编译产物库） | `plcopen::plcopen`（header-only `INTERFACE`，即 `plcopen::core`） |
| 链接产物 | `plcopen.dll` / `libplcopen.so`，需要 `PATH` / `LD_LIBRARY_PATH` | 无产物、无运行时路径注入 |
| 头文件布局 | `include/plcopen/` 平铺（`Axis.h`、`FbSingleAxis.h`…） | `include/plcopen/` 分层（`rt/`、`otg/`、`geom/`、`plan/`、`exec/`、`axis/`、`fb/`） |
| 旧线构建 | 默认 | 显式 `-DPLCOPEN_BUILD_LEGACY=ON`（仅源码树） |
| 旧线安装 | 默认 | 另需 `-DPLCOPEN_INSTALL_LEGACY=ON`，旧头装入 `include/plcopen/legacy/` |
| C++ 标准 | C++17 | C++17；RT 路径兼容 `-fno-exceptions -fno-rtti` |

`find_package(plcopen)` 与 `FetchContent` 的写法不变，直接指向新核；参考
[test_package/](../test_package/) 两个 consumer。

## 运行模型

| 项 | v0.x | v1.0 |
|----|------|------|
| 命名空间 | `plcopen` | `plcopen::core`（子空间 `axis` / `fb` / `rt` 等） |
| 轴生命周期 | `Scheduler::newAxis()` 集中创建，`Scheduler::runCycle()` 推进 | `axis::AxisModel` / `axis::AxisGroup` 值语义对象，调用方按周期显式 `cycle()` |
| 错误码 | `MC_ErrorCode::GOOD` | `rt::ErrorCode::ok` |
| FB 字段命名 | `mAxis`、`mEnable`、`mDone`（成员前缀） | `axis_ref` / `group_ref`、`enable` / `execute`，运动输出集中在 `outputs.done` 等（snake_case） |
| 时间 | 浮点频率（`setFrequency`） | 整型周期计数（如 `TON::set_cycle_time(ticks)`），禁浮点时间累加 |

迁移前后对照的最小可运行示例：`git diff v0.11.0..HEAD -- test_package/find_package/main.cpp`。

## 功能块映射

已在 `v1.0.0-alpha` 提供（头文件 `fb/motion.h`、`fb/basic.h`）：

| v0.x | v1.0 | 备注 |
|------|------|------|
| `FbPower` | `fb::FbPower` | 输出 `status` / `valid` / `error` |
| `FbReset` | `fb::FbReset` | |
| `FbSetOverride` | `fb::FbSetOverride` | override 语义边界见合规矩阵 |
| `FbMoveAbsolute` / `FbMoveRelative` / `FbMoveVelocity` | `fb::FbMoveAbsolute` / `FbMoveRelative` / `FbMoveVelocity` | 输出移入 `outputs` |
| `FbHome` / `FbHalt` / `FbStop` | `fb::FbHome` / `FbHalt` / `FbStop` | |
| `FbTorqueControl` | `fb::FbTorqueControl` | |
| `FbGroupEnable` / `FbGroupDisable` / `FbGroupStop` | `fb::FbGroupEnable` / `FbGroupDisable` / `FbGroupStop` | 组由 `axis::AxisGroup` 承载，无独立 `AxesGroup` 类 |
| `FbMoveLinearAbsolute` / `FbMoveLinearRelative` | `fb::FbMoveLinearAbsolute` / `FbMoveLinearRelative` | 位置用 `GroupPosition{size,value[]}` |
| `FbRTrig` / `FbFTrig` / `FbSr` / `FbRs` | `fb::RTrig` / `FTrig` / `SR` / `RS` | 去掉 `Fb` 前缀 |
| `FbTon` / `FbTof` / `FbTp` / `FbCtu` / `FbCtd` / `FbCtud` / `FbRtc` | `fb::TON` / `TOF` / `TP` / `CTU` / `CTD` / `CTUD` / `RTC` | 周期用 `set_cycle_time(ticks)` |
| `FbGearIn` / `FbGearInPos` / `FbGearOut` | `fb::FbGearIn` / `FbGearInPos` / `FbGearOut`（`fb/sync.h`） | `mInGear` → `in_sync`；`StartSync` → `start_sync`（逼近开始与入同步各脉冲一周期） |
| `FbCamTableSelect` / `FbCamIn` / `FbCamOut` | `fb::FbCamTableSelect` / `FbCamIn` / `FbCamOut` | cam 表句柄换为非拥有的 `exec::CamTableView`；周期表用 `CamTable::set_periodic` |
| `FbPhasingAbsolute` / `FbPhasingRelative` | `fb::FbPhasingAbsolute` / `FbPhasingRelative` | 相位输入上升沿锁存；`Velocity=0` 保持直接设置语义 |
| `FbCombineAxes` | `fb::FbCombineAxes` | add/sub 两主轴合成，`ContinuousUpdate` 支持模式与配比在线更新 |
| `FbMoveAdditive` | `fb::FbMoveAdditive` | aborting 时目标基于被中止命令的原承诺终点解析 |
| `FbMoveContinuousAbsolute` / `FbMoveContinuousRelative` | `fb::FbMoveContinuousAbsolute` / `FbMoveContinuousRelative` | 到达目标后以 `end_velocity` 保持；`Done` 表示"保持终速中"（非锁存完成态），被接管报 `CommandAborted`；`ContinuousUpdate` 支持在线改目标（relative 从原命令起点重解析） |
| `FbMoveSuperimposed` / `FbHaltSuperimposed` | `fb::FbMoveSuperimposed` / `FbHaltSuperimposed` | 独立偏移剖面叠加在基础运动之上；halt 只停偏移且当周期完成（不建模减速段），已累计偏移保留 |
| `FbReadParameter` / `FbReadBoolParameter` / `FbWriteParameter` / `FbWriteBoolParameter` | 同名（`fb/parameter.h`） | 参数号换 `axis::AxisParameter` 枚举；不支持参数报 `rt::ErrorCode::unsupported`；位置滞后监控两参数在新核不建模（显式 unsupported） |
| `FbReadActualPosition` / `FbReadActualVelocity` / `FbReadActualTorque` | 同名 | Enable 型快照读 |
| `FbReadCommandPosition` / `FbReadCommandVelocity`（项目扩展） | 同名 | 同上 |
| `FbReadStatus` / `FbReadAxisError` | 同名 | 状态布尔按 PLCopen 轴状态逐一暴露；`axis_error` 读 `snapshot().error` |
| `FbSetPosition` | `fb::FbSetPosition` | 支持 `relative`；运动中拒绝（含同步与叠加偏移进行中） |
| `FbPositionProfile` / `FbVelocityProfile` / `FbAccelerationProfile` | 同名（`fb/profile.h`） | 表为调用方持有的 `axis::ProfileSegment` 定长数组（≤8 段，替代 `mNext` 链表）；段时长为周期计数，`TimeScale` 乘于时长；速度/加速度剖面 `Done` 表示"保持终段速度中"；`ContinuousUpdate` 仅支持单段剖面；加速度整形不建模（加速度缩放仅作用于限值输入） |
| `FbTouchProbe` / `FbAbortTrigger` | 同名（`fb/probe.h`） | 触发源即数字输入组 `AxisModel::set_digital_input`（固定 4 通道，替代 Servo 数字输入通道）；上升沿捕获、`WindowOnly` 门控、按通道独立、解除空闲通道非错误等边界一致；Servo 锁存位置回读不承接（记录 capture 周期的 actual position） |
| `FbReadDigitalInput` / `FbReadDigitalOutput` / `FbWriteDigitalOutput` | 同名（`fb/io.h`） | 通道即 `AxisModel` 数字 IO 组（输入/输出各固定 4 通道）；不支持通道报 `unsupported` |
| `FbDigitalCamSwitch` | `fb::FbDigitalCamSwitch` | 位置窗驱动一路数字输出；周期窗支持跨界（`on > off`）；换通道/禁用清旧输出；非周期反向窗显式报错 |
| `FbReadAxisInfo` | `fb::FbReadAxisInfo` | 诊断位经 `AxisModel::set_axis_info_inputs` 适配器注入（默认仿真就绪态）；limit switch 输出合并适配器位与软限位越界状态 |
| `FbReadMotionState` | `fb::FbReadMotionState` | 方向/加减速相位由所选源速度与命令加速度导出；源为类型化枚举（旧 `SOURCE_ILLEGAL` 错误不再可表示） |
| `FbEmergencyStop`（项目扩展） | `fb::FbEmergencyStop` | 驱动 errorstop，经 `FbReset` 恢复 |
| `FbAddAxisToGroup` / `FbRemoveAxisFromGroup` / `FbGroupReset` | 同名（`fb/group.h`） | 薄门面包装 `AxisGroup::add_axis/remove_axis/reset`；移除仍要求组 disabled |
| `FbGroupReadStatus` / `FbGroupReadActualPosition` / `FbGroupReadCommandPosition` | 同名 | Enable 型；`moving`/`standby` 合入成员级 gear/cam 同步状态（承接旧线可观察组状态） |

已声明的行为边界（新核为最小语义层，回放仲裁外的变化以此为准；编号锚点见
[README 已知边界](../README.md#已知边界)，测试与 PR 说明应引用这些编号）：

- `KB-019`：同步从轴只接受 aborting 命令接管（非 aborting 显式报 `invalid_argument`）；组级 `stop` 不中止成员级同步；同步接入与 aborting 基础命令清除叠加偏移。
- `KB-020`：`MC_SetOverride` 只作用于新规划的命令，不重规划 active 命令（含连续运动保持段）。
- `KB-021`：同步逼近段按主轴行程线性插值 + 可选每周期速度上限，无加速度/加加速度整形；相位过渡为速度斜坡。
- `KB-022`：TouchProbe 触发源为固定 4 通道数字输入组，记录 capture 周期 actual position，无 Servo 锁存回读。
- `KB-023`：位置滞后监控参数显式 `unsupported`；错误码粗映射（`PARAMETER_NOT_SUPPORT` → `unsupported`，组前置失败 → `precondition_failed`）。
- `KB-024`：轨迹表为定长段数组（≤8 段）、时长按周期计数、`ContinuousUpdate` 仅单段、终段速度无限保持。
- `KB-025`：连续运动与速度/加速度剖面的 `Done` 为持续态非锁存完成态；`MC_HaltSuperimposed` 当周期完成。
- `KB-026`：离散运动经近时间最优 7 段 S 曲线规划，时长显著短于 v0.x/早期新核的保守剖面；加速度形状为梯形/三角相位。

至此 v0.x 公开 FB 面已全量由新核承接。旧线仍可经 `PLCOPEN_BUILD_LEGACY=ON` 构建作回放
基线；真实硬件的 Servo 虚接口（D4 决策中的窄虚边界）仍是 L7 适配层设计项（Phase B5/B7），
当前由 `AxisModel` 的适配器钩子（`set_actual_feedback` / `set_digital_input` /
`set_axis_info_inputs`）暂代。

对应旧线语义边界（`KB-001` 起）见 [README 已知边界](../README.md#已知边界)；未迁移项在新核中调用不存在的符号会在编译期失败，不会静默降级。

## Python 绑定

绑定源码从 `src/python/pyplcopen.cpp`（绑旧 API）移到顶层 `python/pyplcopen.cpp`（绑新核 `AxisModel`），
构建开关仍是 `-DPLCOPEN_BUILD_PYTHON_BINDINGS=ON`（经 FetchContent 拉取 pybind11），模块名与类名不变（`pyplcopen.AxisSim`）。

保留的方法：`power_on`、`move_absolute`、`move_relative`、`move_velocity`、`halt`、`stop`、
`home_direct`、`home_position`、`status`、`command_position/velocity/acceleration`、
`actual_position/velocity/acceleration`。v0.x 公开 Python 面已全量恢复；`home_direct` 现按
MC_Home 直接回零语义置 homed 标志。

## 迁移步骤建议

1. 先把消费方式切到 `find_package(plcopen)` / `FetchContent` 新核目标，确认编译期暴露的缺失符号清单。
2. 缺失符号落在"尚未迁移"分组的，评估：等新核排期，或临时锁定 `v0.11.0` + `PLCOPEN_BUILD_LEGACY=ON`。
3. 逐个替换 FB：字段改 snake_case、输出读 `outputs.*`、错误码换 `rt::ErrorCode`。
4. 把调度改为显式周期：删除 `Scheduler`，按控制周期调用 `AxisGroup::cycle()` / 各 FB `call()`。
5. 用回放/自有测试对比行为；与合规矩阵不符的差异按缺陷上报，引用 KB 编号。

---

*本文档随 R4 证据包维护；发现映射错漏请提 issue 并引用本文行号。*
