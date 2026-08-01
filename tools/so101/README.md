# SO-ARM101 host tools

`plcopen_so101_discover` 是 Linux/WSL2 下的首轮只读探测工具。它固定使用
1 Mbps、8N1，依次向 SO-ARM101 的舵机 ID 1～6 发送 `PING`，不发送寄存器写、
`SYNC_WRITE`、校准或运动命令。返回的 Status 只按原始字节打印，不解释尚未
锁定的位定义。

当前状态：Windows/WSL2 软件测试已通过，尚未使用真实 SO-ARM101 验证，不
构成真机通信、实时性能或 S5 里程碑声明。

`plcopen_so101_compare` 是不接触串口的离线 A/B 证据生成器。它让 naive
直通与 plcopen `JointStreamGroup` 使用同一组六轴目标、同一 50 Hz 周期和
同一场景，输出 command-only CSV；文件刻意不含 `actual`，避免把模拟命令
冒充真机反馈。

## 安全边界

- 先确认控制板、follower/leader 和电源电压标签；错误电压可能损坏舵机。
- 不要运行 `lerobot-setup-motors` 或其他 EEPROM 配置命令。
- USB 枚举可以在舵机电源关闭时完成；执行本工具前，须确认电源标签正确并
  清空机械臂运动范围。`PING` 不会使能运动。

## WSL2 构建

将构建目录放在 WSL 原生文件系统：

```bash
cmake -S /mnt/d/Repos/plcopen -B ~/build/plcopen-so101 \
  -DPLCOPEN_BUILD_SO101_TOOLS=ON \
  -DPLCOPEN_BUILD_DEMOS=OFF
cmake --build ~/build/plcopen-so101 --target plcopen_so101_discover
```

USB 控制板经 `usbipd-win` 附加到 WSL 后运行：

```bash
~/build/plcopen-so101/tools/so101/plcopen_so101_discover /dev/ttyACM0
```

退出码：`0` 表示 ID 1～6 全部响应；`2` 表示部分 ID 未响应；`1` 表示串口
I/O 错误；`64` 表示命令行参数错误。

## 离线 A/B 证据

分别生成两种执行模式的相同 dropout 场景：

```bash
~/build/plcopen-so101/tools/so101/plcopen_so101_compare \
  --mode naive --scenario dropout --output naive.csv
~/build/plcopen-so101/tools/so101/plcopen_so101_compare \
  --mode plcopen --scenario dropout --output plcopen.csv
```

两个 CSV 均记录 `target`、`command_position`、`command_velocity`、
`command_acceleration`、整帧序号与 dropout 计数。场景在 3～6 秒停止发布
上层目标；plcopen 模式使用有界 upsample，并在组级 watchdog 到期后直接
进入 jerk 受限停止，naive 模式保持最后一次目标并在恢复时直接接收新目标。

当前确定性软件基线（GCC 13.3，未接硬件）：

| command-only 指标 | naive | plcopen |
|---|---:|---:|
| 最大速度 | 15.0 rad/s | 0.8 rad/s |
| 最大加速度 | 750.0 rad/s² | 5.0 rad/s² |
| 最大 jerk | 75,000.0 rad/s³ | 50.0 rad/s³ |
| dropout 识别 | 无 | 1 次，3.06 s |
| 六轴全部停稳 | 无 watchdog 状态 | 3.76 s |
| filter fault | 不适用 | 0 |

因此这组证据能直接展示三件事：同一输入下输出动力学有明确上限；任一帧流
断流会触发全组协同停车；序号、dropout 与 filter fault 都能由 CSV 审计。
它不能证明跟踪误差、舵机温升、负载能力或真机实时性，这些必须等实际反馈
接入后再补 `actual` 侧证据。
