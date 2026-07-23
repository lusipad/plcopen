# C++ 嵌入式接入 — 30 分钟上手

把 plcopen 当作 header-only 库接入你的控制器项目。不需要运行时依赖，也不需要
动态链接，直接 `#include` 就能开始。

这条上手路径会带你完成：安装已发布的 `v0.20.0` 源码包、构建一个双轴示例
consumer、
执行 `MC_GroupEnable` 和 `MC_MoveLinearAbsolute`，并观察成功/错误边界。下面
这份完整 consumer 已在 Windows 和 Linux CI 中编译并运行。

## 环境要求

- C++17 编译器（GCC 9+、Clang 10+、MSVC 2022+）
- CMake 3.21+

## 0–5 分钟：选择安装路径

`v0.20.0` 已于 2026-07-20 发布。`FetchContent` 是最短的干净起步路径；
install + `find_package` 则更接近生产环境。

=== "FetchContent"

    ```cmake
    include(FetchContent)
    FetchContent_Declare(
      plcopen
      GIT_REPOSITORY https://github.com/lusipad/plcopen.git
      GIT_TAG v0.20.0)
    FetchContent_MakeAvailable(plcopen)
    ```

=== "Install + find_package"

    ```bash
    git clone --branch v0.20.0 --depth 1 https://github.com/lusipad/plcopen.git
    cmake -S plcopen -B plcopen/build -DPLCOPEN_BUILD_TESTS=OFF -DPLCOPEN_BUILD_DEMOS=OFF
    cmake --build plcopen/build --config Release
    cmake --install plcopen/build --config Release --prefix /opt/plcopen
    ```

=== "vcpkg overlay"

    仓库自带一个经过验证的 overlay port。在外部 curated registry 接收它之前，
    请显式把 vcpkg 指向检出的 port：

    ```bash
    git clone https://github.com/lusipad/plcopen.git
    "$VCPKG_ROOT/vcpkg" install --classic \
      --overlay-ports="$PWD/plcopen/ports" plcopen
    ```

=== "Conan 2"

    仓库中的 recipe 与其 consumer `test_package` 是一起验证过的。
    ConanCenter 的接收状态与这个源码 recipe 分开追踪：

    ```bash
    git clone https://github.com/lusipad/plcopen.git
    conan profile detect --force
    conan create plcopen --build=missing \
      -s build_type=Release -s compiler.cppstd=17
    ```

当前源码仓库里的 recipe 和 overlay port 现在就可以使用；单独的
ConanCenter submission asset 锁定的是已发布的 `v0.20.0` 归档。它们目前都还
不是 ConanCenter 或 vcpkg curated registry 里的正式条目，因此不能因为本地
consumer 测试是绿色的，就推断外部 PR 或审核也已通过。

## 5–15 分钟：构建标准 consumer

创建一个目录，并放入下面这个 `CMakeLists.txt`：

```cmake
--8<-- "test_package/find_package/CMakeLists.txt"
```

把这份由 CI 持有的程序复制到 `main.cpp`：

```cpp
--8<-- "test_package/find_package/main.cpp"
```

配置、构建并运行它。把 `/opt/plcopen` 替换成你的安装前缀：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/plcopen
cmake --build build --config Release
./build/plcopen_find_package_smoke
```

在 Windows 上，可执行文件通常位于 `build/Release/`。退出码为 `0` 表示两个轴都
在没有 FB 错误的情况下到达了 `(3, 4)`。

## 15–25 分钟：理解生命周期

- `AxisModel` 对象的生命周期要长于其被 `AxisGroup` 以 non-owning 方式持有
  的成员关系。
- `FbGroupEnable` 和 `FbMoveLinearAbsolute` 使用 `execute` 的 level/edge，
  并暴露 `busy`、`done`、`command_aborted`、`error` 与 `error_id`。
- `group.cycle()` 每次只推进一个确定性周期；示例里没有 wall-clock sleep，
  因为调度由宿主负责。
- 返回值和 `error_id` 都是 `rt::ErrorCode`；在决定是否重试之前，请先使用
  `rt::to_string()` 和诊断 hint API。

底层 API 中的动力学量都是按每周期计的。请在 load boundary 使用
`rt::CycleConfig` 或 SI 轴/组配置对象做一次性转换；不要在 cycle loop 中自己
累加浮点时间。

## 25–30 分钟：选择生产边界

教学示例中的循环同时负责 planning 和 cycle 推进。生产环境请使用下面提到的
committed-frame executor，或者接入你自己的 scheduler 和 `Servo` adapter。
配置和诊断都应放在 RT tick 之外。

## 实时约束

核心库面向硬实时设计：

- **整个 cycle path 都禁止堆分配、加锁和抛异常**。这覆盖完整 RT scan surface
  （rt / otg / geom / exec / kin / stream / adapters，以及 L5 axis 和 L6
  fb 的 cycle 路径）
- **L0-L4 完全不带 PLCopen 语义**，因此通用轨迹内核可以单独复用
- **禁用异常与 RTTI**（`-fno-exceptions -fno-rtti`）
- cycle loop 中**不允许 OS 调用**
- **不允许浮点时间累加**（使用整数周期计数器）

`Servo` 窄接口（ADR-0004）负责与你的硬件驱动对接。

## 生产运行时形态（ADR-0007）

上面示例里的单线程 `while` 循环只是**教学简化**，逻辑是正确的，但并不是生产
形态。

在生产环境（ADR-0007）中，**planning-domain 线程**是 `AxisGroup` /
`AxisModel` 的唯一拥有者：它负责消费命令、桥接反馈、运行 `cycle()`，并向前
填充最多 H 帧的 **committed trajectory ring**。**RT 线程每个 tick 只弹出一帧**
，因此是 O(1)、零分配。如果 planning 变慢，ring 水位会下降，lookahead 深度
会缩小；但 RT 周期时序和运动平滑性永远不会被打扰。跨 domain 共享的只有四个
SPSC 队列（commands、trajectory ring、feedback、state snapshots）；参考
执行器在 TSAN 下为零发现。

可参考 `core/demo/rt_executor_demo.cpp` 中带 `ServoSim` 的双线程参考执行器，
以及首页上的
[运行时图](../index.md#运行时形态adr-0007)。

## 嵌入 ST 运行时

下一步可以看独立的 [ST 直接运动上手](st.md)。其中完整宿主程序由 CTest
编译并运行，覆盖 `compile()` → `load()` → `bind_axis()` → 周期性 `scan()`
与 `AxisModel::cycle()`，没有占位步骤。

## 构建选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `PLCOPEN_BUILD_TESTS` | ON (top-level) | All test targets |
| `PLCOPEN_BUILD_DEMOS` | ON (top-level) | Example executables in `core/demo/` |
| `PLCOPEN_BUILD_PYTHON_BINDINGS` | OFF | pyplcopen pybind11 module |
| `PLCOPEN_BUILD_LEGACY` | OFF | Frozen v0.x line |

## 后续阅读

- [Python 仿真指南](python.md) — 不依赖 C++ 工具链先做原型
- [ST 直接运动指南](st.md) — 从 ST 执行 `MC_Power` 和 `MC_MoveAbsolute`
- [算法内幕](algorithms.md) — 理解运动规划器如何工作
- [BUILD_README.md](https://github.com/lusipad/plcopen/blob/main/BUILD_README.md) — 完整构建参考
