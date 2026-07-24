# IEC 61131-3 ST — 30 分钟上手

如果你希望在同一套运动内核之上使用 PLC 风格时序逻辑，就使用内置 ST 运行时。
内存、绑定和 scan 调度仍由宿主负责；VM 只负责执行程序。

这条上手路径会带你完成：编译一段 ST 程序、绑定一个 `AXIS_REF`、驱动
`MC_Power` 和 `MC_MoveAbsolute`，然后通过宿主侧的 symbol 读取观察完成状态。
下面这份完整宿主程序已在 CI 中编译并运行。

## 环境要求

- C++17 编译器（GCC 9+、Clang 10+、MSVC 2022+）
- CMake 3.21+
- 一个已安装的 plcopen 包，或本地构建前缀

## 0–10 分钟：理解所有权模型

ST 运行时不会替你隐藏宿主职责：

- `compile()` 属于 load-domain 工作，允许内存分配
- `scan()` 属于 cycle-domain 工作，并遵守 RT 规则
- 调用方拥有传给 `Instance::load()` 的对齐字节缓冲区
- `AXIS_REF` / `GROUP_REF` 对象保留在宿主中，并通过显式方式绑定

这种分离是有意设计的。它让 RT 语义保持直观且可测试。

## 10–20 分钟：构建标准 ST consumer

创建一个目录，并放入下面这个 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.21)
project(plcopen_st_journey LANGUAGES CXX)

find_package(plcopen CONFIG REQUIRED)
add_executable(st_motion st_motion.cpp)
target_compile_features(st_motion PRIVATE cxx_std_17)
target_link_libraries(st_motion PRIVATE plcopen::plcopen)
```

把下面这份宿主程序复制到 `st_motion.cpp`：

```cpp
--8<-- "core/demo/st_motion.cpp"
```

配置、构建并运行它。把 `/opt/plcopen` 替换成你的安装前缀：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/plcopen
cmake --build build --config Release
./build/st_motion
```

退出码为 `0` 表示：

- ST 源码已成功编译
- 程序已装载到调用方持有的 buffer 中
- `bind_axis("AxisX", &axis)` 成功
- 反复执行 `scan()` + `axis.cycle()` 后，轴到达位置 `1.25`

## 20–25 分钟：读懂关键边界

宿主内嵌的这段 ST 源码使用的是每周期 motion 值：

- `Velocity := 1.0`
- `Acceleration := 2.0`
- `Deceleration := 2.0`
- `Jerk := 10.0`

这些数值会按传给 `Instance::load()` 的 1 kHz task 中的每周期量来解释。如果你的
面向人的配置使用的是 SI 单位，请在宿主里用 `rt::CycleConfig` 先做一次转换，
再据此生成或注入 ST 输入。

## 25–30 分钟：判断 ST 是否是合适界面

在这些情况下选择 ST：

- 机器逻辑需要保持 PLC 形态
- 时序逻辑应该与 IEC 61131-3 功能块放在一起
- 硬件桥接仍然由宿主应用负责

在这些情况下选择直接 C++：

- 你希望所有运动调用点都由原生代码直接控制

你也可以混合使用两者：用 ST 做时序编排，用 C++ 做 adapter。

## 更多细节

- [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md)
- [ST Language Server 与 VS Code](../guides/st-language-server.md)
- [ST 运行时参考](../references/st-runtime.md)
- [Function-block reference](../references/fb-reference.md)
