# Implementation notes — PLCopen C3 / Part 4 68 门面

Plan: [plcopen-c3-implementation-plan.md](plcopen-c3-implementation-plan.md)

## Decisions

- 采用批准矩阵的三批边界，但在一个实现批次内连续交付；公开类型放回现有
  `axis/group.h`、`fb/group.h`、`fb/path_table.h`、`fb/management.h`，没有
  新增单用途抽象层。
- `MC_COORD_REF` 落为现有 `ToolData` 固定 6D RPY，`MC_KIN_REF` 落为现有
  `Kinematics`/`PoseKinematics` 非拥有引用。
- Halt 复用既有原路径制动规划，但拥有独立 command id 与被接管账本；Wait
  使用独立整数周期状态，不使用墙钟或休眠。

## Deviations

- 原计划写“Buffered Wait 等前序完成”；实现支持活动运动后的 Buffered Wait，
  以及 Wait 后的 Buffered 运动。若提交 Wait 前普通组队列已非空，仍按矩阵
  显式 `unsupported`，避免两个异构队列伪造全序。
- `GroupPower` 关闭已启用组时按成员掉电事实将组锁存 ErrorStop，同时 FB 本身
  完成电平请求；逐轴/组电源双写仍因缺命令源身份无法检测。

## Surprises

- 旧 `plcopen-part4-clause-audit.md` 的 §6 标题仍写 18 个无同名门面，且混入
  已由 P4-B2/P4-B3 关闭的 7 项；本批改为 0 个无同名门面和 11 项 C3 子集证据。
- 新增两个 CTest 后 Windows Debug 总数从 67 增至 69；既有 18 份回放零差异。
- 首轮干净覆盖率显示 production motion stack branch 83.7%；补齐 3D/6D
  正逆变换、奇异/插件错误、Halt/Wait 状态与无效输入证据后恢复至 85.0%。

## Questions for review

- 后续若要从“名称面齐全”推进 E 级对等，优先级应是统一异构组命令队列，
  还是 MC_Power/GroupPower command-source 仲裁；C3 不预设该架构。

## Verification

- Windows Debug、Linux GCC、Linux Clang：69/69 tests passed；分配守卫、
  benchmark 与 18 份 replay regression 包含在全量运行中。
- ARM64/QEMU：按 CI 排除 benchmark/jitter 后 62/62 passed；C3 fuzz 修正后
  又单独经 QEMU 复跑通过。
- clang-tidy：Clang 18 编译数据库 67 项，0 个 error-level
  `warnings-as-errors`；普通 readability 历史警告不作为 E2 失败。
- 覆盖率：full-core line 92.9%（15941/17151）；production motion stack
  line 94.1%（11948/12702）、branch 85.0%（7111/8368），硬门通过。
- RT safety scan 27 files、18 replay fixtures/2409 samples、Part 1/2 49-row
  matrix、Part 1 I/O 43 FB/236 B/302 E、`git diff --check` 全部通过。
