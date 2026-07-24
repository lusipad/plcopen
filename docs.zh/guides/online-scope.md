# 在线调试示波器

D3 Scope 把参考 executor 的 `PLCT v1` trace 变成可在线跟随的 commissioning
通道：控制线程只把固定大小记录写入 SPSC，非实时 writer 持续落盘，工具侧
负责阈值触发、前后窗口冻结和可视化。示波器落后时不会反压运动执行。

## 构建参考 executor

```bash
cmake -S . -B build-scope -DPLCOPEN_BUILD_CORE=ON -DPLCOPEN_BUILD_DEMOS=ON
cmake --build build-scope --config Debug --target plcopen_core_rt_executor_demo
```

在一个终端中启动 executor。Windows 多配置构建的可执行文件位于
`build-scope/core/Debug/`；Linux 位于 `build-scope/core/`。

```bash
build-scope/core/plcopen_core_rt_executor_demo \
  --cycles 20000 \
  --trace live.bin
```

`live.bin` 会在 executor 运行期间持续增长。最终摘要中的
`trace_records` 应等于 `cycles × axes`，`trace_dropped` 应为零；任何丢样
都会让 executor 健康门失败，但不会阻塞控制。

## 冻结触发窗口

以下命令在 axis 0 的 position 从下向上穿过 `0.1` 时触发，保留触发前
200 拍、触发拍和触发后 400 拍：

```bash
python tools/plcopen_trace.py live.bin \
  --follow \
  --trigger-axis 0 \
  --trigger-field position \
  --trigger-edge rising \
  --trigger-threshold 0.1 \
  --pre-cycles 200 \
  --post-cycles 400 \
  --timeout 5 \
  --window scope.bin \
  --csv scope.csv \
  --html scope.html
```

触发采用穿越而不是电平判断：第一个样本只建立比较基线。`falling` 表示从
阈值上方穿到阈值或下方。窗口包含所有轴的完整记录；缺拍、轴记录不完整、
静态文件尾部截断或超时都显式失败，不输出伪完整窗口。`--timeout` 是从
follow 启动算起的总时限；持续收到未触发的数据不会延长命令。

## Rerun

Rerun 是可选 adapter，不进入 core 或默认 wheel 依赖。在仓库根目录安装：

```bash
python -m pip install ".[scope]"
```

在前面的命令中追加 `--rrd scope.rrd`，工具会把跟随到的每个样本记录为
`axes/<axis>/position|velocity|acceleration`。只有显式追加 `--spawn`
才启动 Viewer；`--spawn` 必须与 `.rrd` 同时使用，确保会话保留证据文件。

## 诚实边界

- `PLCT v1` 记录的是 command position/velocity/acceleration，不是 actual、
  following error 或 ST watch；
- 本工具是 commissioning 观察面，不是认证测量设备、远程服务或安全通道；
- 文件 I/O、触发和 Rerun 全在非实时侧；控制优先于 trace 完整性；
- ST L7 的 `DebugTraceRecord` 保持独立，不属于本批跨工具格式。

规范与拒绝规则见
[D3 在线调试示波器语义矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/online-scope-semantics.md)。
