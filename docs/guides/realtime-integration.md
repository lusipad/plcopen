# Real-time integration guide

## 推荐拓扑

生产系统采用 ADR-0007 双域模型：

1. 规划域线程是 `AxisGroup`/`AxisModel` 的唯一写者，排空命令、运行 FB
   和 `cycle()`，填充承诺轨迹环。
2. RT 线程每周期只弹出一帧，调用驱动器窄接口并写回反馈。
3. 两域之间只共享命令、承诺轨迹、反馈、状态快照四条 SPSC 队列。

RT 线程不调用 `submit_*`、look-ahead、kinematics 求解或可能扩容的规划函数。
规划慢时前瞻深度下降，RT 周期不等待规划线程。参考实现：
`core/demo/rt_executor_demo.cpp`。

## 周期骨架

```cpp
// Planning domain owns these objects for their entire lifetime.
axis::AxisModel axis;
adapters::ServoSim servo;

// Planning domain: consume commands, call FBs, run cycle(), publish a frame.
axis.cycle();

// RT domain: consume exactly one committed frame and bridge feedback.
// Do not call planning APIs from this thread.
```

实际 EtherCAT、线程调度、时钟同步和总线 IO 不在本仓库内；宿主执行器在
周期边界调用 `adapters::Servo`，并使用 `set_actual_feedback()`、
`set_digital_input()` 等钩子回写。Feetech STS 当前只有纯软件协议层，
不含串口 IO，也不能替代工业实时总线。

## RT 清单

- 周期路径零堆分配、零阻塞锁、无异常、无系统调用。
- 时间使用整数 cycle counter，禁止浮点时间累加。
- 固定容量容器在初始化/规划域准备，RT 只读已承诺数据。
- 驱动器适配器实现窄接口，不把总线对象或线程所有权塞进核心。
- 反馈、命令和快照使用单写者队列，禁止跨线程共享可变 `AxisModel`。

提交前运行：

```bash
cmake --build build-sync --config Debug
ctest --test-dir build-sync -C Debug --output-on-failure
cmake -P cmake/rt_safety_scan.cmake
```

规划域提交失败时保留当前承诺轨迹，记录 `rt::ErrorCode`，不要在 RT 线程
重试同一提交。承诺环耗尽时由宿主监控 ring level 并进入自己的安全策略，
不让驱动器线程阻塞等待规划结果。
