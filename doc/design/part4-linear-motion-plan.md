# v0.11.0 Part 4 Linear Motion Foundation Plan

> 状态：当前执行计划。PLCopen Motion Control Part 4 Version 2.0 原始规范已于 2026-06-21 从 PLCopen 官方下载页核对；公开合同以 [Part 4 linear matrix](../compliance/plcopen-motion-part4-linear-matrix.md) 为准。

## 决策

`v0.11.0` 继续深化项目最有辨识度的运动控制能力，交付 ACS 下的 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative` 最小可靠闭环。

不选择“完整 IEC 通用函数库”作为下一版本，是因为这类函数在 C++ 中差异化价值较低；不直接做 kinematics、圆弧或坐标变换，是因为现有 runtime 尚无共享组路径执行内核，跨层扩展会让范围失控。

## 假设与边界

- 项目仍是可嵌入的单线程 C++17 库，不变成 CNC、机器人平台或 IDE。
- 组内所有轴必须来自同一个 `Scheduler`，共享频率并由同一个 `runCycle()` 推进。
- 当前版本支持 2-8 个成员轴和 ACS。`MC_POS_REF` 采用规范允许的 vendor-specific fixed-capacity representation，位置按稳定的 group member slot 映射。
- 线性运动必须由一个共享标量路径进度驱动；同时启动多个独立 `ProfilePlanner` 不算完成。
- 命令必须原子提交：任一成员、参数或限制校验失败时，所有成员都保持原状态。
- `ABORTING` 和 `BUFFERED` 是本版本唯一承诺的时间顺序模式；`TransitionVelocity=mcTVZero`、`TransitionMode=mcTMNone` 和 `OrientationMode=mcLinear` 是唯一支持的过渡/姿态组合。其他值显式返回错误。
- 硬件相关行为只能通过已有 `Servo` 抽象表达；不声称具备现场总线时钟同步或驱动级插补能力。
- PLCopen Part 4 Version 2.0 是公开 API 和生命周期语义的最终依据。仓库内 overview 只用于历史背景，不能替代原始规范。

## 当前基线

- Phase 0 门禁已收紧：`build.ps1 -Test` 明确启用测试并拒绝 0 tests；Linux docs 安装 Graphviz 并检查图产物。
- PLCopen Part 4 v2.0 来源、哈希和公开合同矩阵已固定。
- `Scheduler` 已推进 group runtime；`AxesGroup` 已拥有组级队列、共享路径命令与安全的 release/destructor 生命周期。
- `GroupLinearPlanner` 已复用一维 `ProfilePlanner`，并通过 2/3/8 轴共线、混合方向和零距离测试。
- `FbMoveLinearAbsolute` / `FbMoveLinearRelative`、CommandAccepted/CommandID、Aborting/Buffered、按原路径受控减速并保持 GroupStopping 的 GroupStop，以及成员错误传播已形成首条纵向闭环。
- 当前实现与本地消费闭环已经完成；剩余缺口是 GitHub 上的 Windows/Linux/coverage/docs 全门禁确认和版本发布面收口。

## 当前进度（2026-06-21）

| 阶段 | 状态 | 证据或剩余项 |
|---|---|---|
| Phase 0 发布门禁 | 完成 | clean NMake 构建；当前全量 CTest 通过；CI 配置已补图生成门禁 |
| Phase 1 标准合同 | 完成 | 官方 Part 4 v2.0 + SHA-256 + live matrix |
| Phase 2 组命令运行时 | 完成 | scheduler/group 生命周期、原子队列、互斥、状态和故障停止已覆盖 |
| Phase 3 共享规划器 | 完成 | 2/3/8 轴 Absolute/Relative 逐周期共线，成员共同限制与软限位已覆盖 |
| Phase 4 公开功能块 | 完成 | Absolute/Relative、Buffered/Aborting、GroupStop 的保持/释放/中止时序、reset/error 已覆盖 |
| Phase 5 消费与发布 | 收口中 | demo、find_package/FetchContent smoke、README/CHANGELOG 和源码版本面已完成；推送后的 GitHub CI 是最终发布证据 |

## Definition of Done

`v0.11.0` 只有同时满足以下条件才完成：

1. Part 4 Version 2.0 中的 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative` 输入、输出、生命周期、错误和坐标系要求已形成可审计矩阵。
2. `Scheduler` 每周期恰好推进一次 group command，成员轴在同一周期接收同一共享路径样本。
3. 2、3、8 轴绝对和相对运动均覆盖正负方向、静止分量、短距离及零距离边界。
4. 每个采样点满足参数化直线关系，终点误差在项目既有数值容差内，成员开始/结束偏差不超过一个周期。
5. 参数、成员状态或限制失败时不产生部分入队或部分运动；运行中任一成员错误会使整个 group 进入可解释的停止/错误状态。
6. `ABORTING`、`BUFFERED`、`MC_GroupStop`、disable/reset 与功能块 Done/Busy/Active/CommandAborted/Error 生命周期有自动化覆盖。
7. Windows、Linux GCC/Clang、覆盖率、安装后 `find_package`、`FetchContent`、Python/docs smoke 全绿；CTest 不能以 0 tests 成功。
8. README、ROADMAP、CHANGELOG、公开头文件注释和 Part 4 范围矩阵描述一致，且不声称支持本计划非目标。

## 执行阶段

### Phase 0：发布门禁预检（2-3 晚）

目标：从可信的绿色基线开始，而不是把既有假成功带进新 runtime。

任务：

- 让 `build.ps1 -Test` 显式启用测试，并在 CTest 未发现测试时失败。
- 为 Linux docs 通道补齐 Doxygen 图依赖或关闭不可用图功能；生成日志中的 map error 必须使门禁失败或被根因消除。
- 保留 Windows/Linux 全量测试、覆盖率、consumer、Python 和 docs 证据。

验证：

- 从全新构建目录运行正式入口，核心测试数量大于 0。
- CI 日志不再包含 Doxygen `problems opening map file`。
- `main` 全部工作流成功。

### Phase 1：标准合同与矩阵（3-4 晚）

目标：先固定支持声明，再设计接口。

任务：

- 固定 PLCopen Part 4 Version 2.0 的官方来源、版本和 SHA-256。
- 维护 `doc/compliance/plcopen-motion-part4-linear-matrix.md`。
- 固定 position reference、成员 slot、ACS、BufferMode、状态输出和错误语义。
- 将 MCS/PCS、transform、blending 等未支持行为列为明确边界。

验证：

- 矩阵中的每个支持项都有计划中的代码入口与测试证据位置。
- 不存在“暂按猜测实现”的公开字段。

### Phase 2：组命令运行时（1-2 周）

目标：让 coordinated command 成为一个真正的运行时所有者。

任务：

- 在 `Scheduler` 与 `AxesGroup` 之间建立最小注册和生命周期关系。
- 为 group 增加 active/queued command 所有权和每周期推进入口。
- 校验所有成员属于同一 scheduler、已上电、group enabled 且可接受新命令。
- 实现原子 validate-then-commit；失败时不得留下成员轴队列残片。
- 明确 group command 与现有单轴、gear/cam 命令的互斥规则。

验证：

- group 每个 scheduler tick 只推进一次。
- 无成员、跨 scheduler、未上电、disabled、成员 busy/error 均有回归测试。
- 提交中任一校验失败后，所有成员位置、状态和队列保持不变。

### Phase 3：共享线性路径规划器（1-2 周）

目标：以一个标量进度生成整组轴的指令位置、速度和加速度。

任务：

- 新增窄范围 group linear planner；复用已验证的一维速度曲线能力，不复制一套无关算法。
- 从起点向量和目标向量构造共享路径参数，并将每周期样本映射到全部成员轴。
- 根据请求参数与成员运动限制，在提交前计算整条路径可接受的共同约束。
- 处理零位移成员、全零路径、非有限值、软限位和运行时成员错误。

验证：

- 2/3/8 轴采样点共线，正负与混合方向成立。
- 静止分量全程不漂移；全零路径按标准合同结束。
- 任一成员限制更严格时，整组降速或整条命令拒绝，不允许单轴脱离路径。
- 任一成员运行中出错时，不允许其他成员继续完成原路径。

### Phase 4：公开功能块与缓冲语义（约 1 周）

目标：通过项目既有功能块生命周期暴露协调线性运动。

任务：

- 增加 `FbMoveLinearAbsolute` 和 `FbMoveLinearRelative`，公开名称按项目现有 `Fb*` 约定映射 PLCopen `MC_*` 名称。
- 实现 Execute 边沿、Busy/Active/Done、CommandAborted/Error 及下降沿清理。
- 实现 `ABORTING` 和 `BUFFERED`；其他 BufferMode/TransitionMode 返回明确错误。
- 让 `MC_GroupStop` 沿 active group path 按 Deceleration/Jerk 受控停止全部成员；速度为零后置 Done，Execute 保持为真时维持 GroupStopping，Disable/掉电通过 CommandAborted 中止。

验证：

- 绝对/相对、连续两段 buffered、active 中 abort、stop、reset 和非法模式测试。
- 功能块输入在执行期间按标准合同锁存，不因调用方改值产生未声明更新。

### Phase 5：消费闭环与发布（约 1 周）

目标：确保能力能被下游使用，而不只在仓库测试里存在。

任务：

- 增加最小 `group_linear_move` demo。
- 更新安装导出、README quick start、限制说明、CHANGELOG、ROADMAP 和 Part 4 矩阵。
- 扩展 `find_package` / `FetchContent` consumer，至少编译并运行一个两轴线性运动 smoke。
- 运行完整发布门禁并发布 `v0.11.0`。

验证：

- 全量 CTest、覆盖率、consumer、Python/docs smoke 全绿。
- clean checkout 可重复构建；`git diff --check` 通过。
- tag、`.version`、CMake、README、CHANGELOG、ROADMAP 与 GitHub Release 版本一致。

## 明确非目标

- `MC_MoveCircularAbsolute` / `MC_MoveCircularRelative`
- `MC_MoveDirectAbsolute` / `MC_MoveDirectRelative`、`MC_MovePath`
- `MC_GroupHome`、`MC_GroupHalt`、`MC_GroupInterrupt` / `MC_GroupContinue`
- MCS / PCS、kinematics、Cartesian / coordinate / dynamic transforms
- 几何 blending、look-ahead、路径表导入
- 多 scheduler 协调、硬件时钟同步、现场总线或真实驱动认证

## 预计变更面

计划主要涉及：

- `src/motion/Scheduler.*`
- `src/motion/AxesGroup.*`
- 新的窄范围 group linear planner 文件
- `src/fb/FbMultiAxis.*`
- `src/fb/PLCTypes.h`、`src/motion/Global.h`
- `src/test/test_axes_group.cpp`、`src/test/test_fb_multi_axis.cpp` 和新的 planner focused test
- `src/CMakeLists.txt`、demo、consumer 与公开文档

不在实施前预先拆出通用“运动图”“任意维轨迹框架”或插件接口。只有两个线性功能块需要的共享能力才进入本版本。

## 停止条件

- 若实现与 Part 4 Version 2.0 矩阵发生冲突，停止公开 API 扩展并先修正合同，不以旧版 overview 覆盖新规范。
- 若共享路径必须破坏现有单轴命令语义才能工作，先记录架构冲突并重做 Phase 2 设计，不用兼容分支掩盖问题。
- 若 8 轴路径无法在一个 scheduler tick 内原子生成和提交，版本延期，不降级成独立单轴规划后宣称 coordinated motion。
