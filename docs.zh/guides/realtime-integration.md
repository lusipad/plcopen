# 实时集成指南

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

## 同步关节命令帧

`stream::JointStreamGroup` 提供最多 48 关节的 H1 固定容量命令原语。
会话前配置并 reset 每个成员，之后只通过 `push_frame()` 提交完整帧：

```cpp
stream::JointStreamGroupConfig config{};
config.joint_count = 2;
config.mode = stream::JointFrameMode::direct;
// Fill every config.joints[i] filter, torque, gain, and safe-gain limit.

stream::JointStreamGroup joint_stream;
joint_stream.configure_frame(config);
joint_stream.reset(0, {});
joint_stream.reset(1, {});

stream::JointCommandFrame frame{};
frame.joint_count = 2;
frame.timestamp_cycles = 1; // ordering/echo only
frame.joints[0] = {0.2, 0.01, 0.0, 8.0, 2.0};
frame.joints[1] = {-0.2, -0.01, 0.0, 8.0, 2.0};
joint_stream.push_frame(frame);
joint_stream.cycle();
const auto &command = joint_stream.read_setpoint_frame();
```

producer 时间戳只用于排序和回显，不会安排未来拍生效；生效与 watchdog
年龄由持有者的本地 cycle 决定。`direct` 在下一拍共同呈现已接受的 q/dq，
但不声明 jerk-limited 平滑。`upsample` 复用一维 OTG 滤波器：快路径成员
同拍响应，慢解每拍最多 10 个，48 关节最迟五拍全部开始追踪。任一成员
非法都会整帧拒绝。

返回帧是**命令快照**，不是 actual feedback。H1 会携带 `tau_ff`，但不授权
drive adapter 或 executor 使用它；消费前必须另行完成 T18 的扭矩限幅、
速度监督与位置围栏合同。

## 固定基座动力学前馈

`dyn::FixedBaseChain` 为一条调用方持有的 1～8 关节转动串联链计算纯前馈
扭矩。初始化阶段构造并验证模型，之后以 SI 量调用有界 O(n) RNEA：

```cpp
dyn::FixedBaseChain chain(spec);
if (!chain.valid()) {
    // Reject the configuration before starting the cyclic task.
}

double tau_ff[8]{};
const double gravity[3]{0.0, 0.0, -9.80665};
const auto error =
    chain.inverse_dynamics(q_rad, dq_rad_s, ddq_rad_s2, gravity, tau_ff);
```

失败不会修改 `tau_ff`。臂、腿、腰等链各自使用独立实例。H3 只产生数值；
规划域 producer 可以在完成单位与所有权检查后把结果复制到 H1 命令帧，
但在 T18 定义并验证扭矩限幅、速度监督和位置围栏之前，drive/executor
仍必须忽略 `tau_ff`。

当前源码为规划域 Python 实验提供了同一原语的 q/dq 窄门面：

```python
import pyplcopen

stream = pyplcopen.JointStreamSim(
    7, "upsample",
    velocity_limit=0.8,
    acceleration_limit=0.08,
    jerk_limit=0.02,
)
stream.reset([0.0] * 7)
stream.push_frame([0.1] * 7, timestamp_cycles=1, velocities=[0.0] * 7)
stream.cycle(10)
command = stream.setpoint_frame()
```

`JointStreamSim` 会在触碰组状态前校验完整 Python 向量，非法帧整体拒绝。
它不是跨线程 RT API，也不暴露 `tau_ff`、`kp` 或 `kd`；生产集成应使用
C++ 原语，并在 T18 独立合同关闭前保持扭矩消费禁用。

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
