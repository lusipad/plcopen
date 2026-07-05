# R4 切换收口证据包

## 范围

本证据包对应 [R0-R4 工作拆解](r0-r4-work-breakdown.md) 的 R4：让新核能被用户实际消费，并把旧 `src/` 有秩序地退到维护状态。

## R4 DoD 对照

| ID | 任务 | 当前证据 | 状态 |
|----|------|----------|------|
| R4.1 | CMake install/export 迁移 | 默认 `plcopen::plcopen` 指向 `core/`，`cmake --install build-r4-nmake --prefix build-r4-install-2` 导出 `core` 头文件和 package config | 已验证 |
| R4.2 | FetchContent consumer 迁移 | `test_package/fetchcontent` 使用 `axis/group.h` + `fb/motion.h`；`plcopen_fetchcontent_smoke` 通过 | 已验证 |
| R4.3 | demos 迁移 | 默认 demo 收口到 `core/demo/basic_fb_cycle.cpp` 和 `core/demo/group_linear_move.cpp`；两个 demo smoke 通过 CTest | 已验证 |
| R4.4 | pyplcopen smoke 迁移 | `python/pyplcopen.cpp` 绑定新核 `AxisModel` 最小 facade；`pyplcopen_smoke` 通过 | 已验证 |
| R4.5 | README 重写为新核口径 | README 默认消费面、quick start、架构图和 CMake 消费均指向 `core/`；`git diff --check` 通过 | 已验证 |
| R4.6 | 旧设计文档归档 | `doc/design/README.md` 标注当前/历史入口；旧设计文档加归档提示；docs target fallback 通过 | 已验证 |
| R4.7 | 删除或隔离旧 `src/` | 默认不再 `add_subdirectory(src)`；显式 `PLCOPEN_BUILD_LEGACY=ON` 才构建旧线；默认 full CTest 只有新核/fixture/demo 门禁 | 已验证 |
| R4.8 | v0.x EOL 公告 | 本文提供草案，需人工审签 | 草案 |
| R4.9 | `v1.0.0-alpha` 证据包 | 完整发布草案见 [v1.0.0-alpha-release-draft.md](v1.0.0-alpha-release-draft.md)（含 Release notes 草稿、DoD 对照、人工检查单），需人工发布 | 草案 |

## 本地验证记录

- `git diff --check`
- `cmake -S . -B build-r4-nmake -G "NMake Makefiles" -DPLCOPEN_BUILD_TESTS=ON -DPLCOPEN_BUILD_DEMOS=ON`
- `cmake --build build-r4-nmake`
- `ctest --test-dir build-r4-nmake --output-on-failure`：9/9 通过
- `cmake --install build-r4-nmake --prefix build-r4-install-2`
- installed `find_package` consumer：`plcopen_find_package_smoke` 通过
- `FetchContent` consumer：`plcopen_fetchcontent_smoke` 通过
- `cmake -P cmake/rt_safety_scan.cmake`
- `cmake -P cmake/verify_replay_fixtures.cmake`
- `cmake -P cmake/generate_part1_part2_matrix.cmake`
- `coverage.ps1 -BuildDir build-r4-nmake -OutputDir out/coverage-r4`：core line coverage 83.62%
- `cmake -DPLCOPEN_MUTATION_GENERATOR="NMake Makefiles" -DPLCOPEN_MUTATION_BUILD_DIR=build-r4-mutation -P cmake/mutation_smoke.cmake`
- `ctest --test-dir build-r4-python -R pyplcopen_smoke --output-on-failure`
- `cmake --build build-r4-docs --target docs`：本机无 Doxygen，fallback target 通过

补充（2026-07-05，DoD §5.3 新旧核对照首测）：

- 对照工具 `plcopen_core_legacy_compare`（`core/bench/legacy_compare.cpp`，仅
  `PLCOPEN_BUILD_LEGACY=ON` 构建）：等价单轴负载（1kHz、500 单位往返 move、
  20 万周期、含 ~160 次重规划）单进程双栈计时。
- **首测结果（诚实记录）**：旧线 41ms / 新核 618ms —— 新核慢 **15×**，
  DoD §5.3"新核周期耗时 ≤ 旧核 50%"按此负载口径不通过。
- 根因定位：不在周期路径（新核纯周期成本 ~20ns/轴，见 axis_cycle_pair 基准），
  而在**规划成本**：`plan_time_optimal` 的 quintic 候选按整段逐周期采样验证
  可行性（长剖面 O(N·logN) 求值），叠加 floored 构造在亚周期 jerk 相位参数域
  退化出巨残差修正。单次规划毫秒级——若在 1ms 周期内同步提交命令会爆预算。
- 已验证的缓解方向（因并发会话在 `core/otg` 活跃，改动未入库，进 backlog）：
  ①候选门控——长剖面跳过 quintic/baseline 候选（它们只在 ≤64 周期短剖面胜出），
  实测 15.1× → 10.3×；②floored 构造对亚周期 jerk 相位快速失败（该域应由 exact
  候选独占）。两项合计预期把规划降到微秒级；DoD §5.3 建议把"周期耗时"与
  "规划耗时"分列口径后复测。

补充（2026-07-05，FB 面收齐后）：

- v0.x 公开 FB 面与 pyplcopen 面已全量由新核承接（迁移证据见 [r3-migration-matrix.md](r3-migration-matrix.md)）；全量 CTest 19 项通过。
- 覆盖率门禁修复后重测：`coverage.ps1 -BuildDir build-sync -Configuration Debug` 全套件合并行覆盖 **85.37%**（5245/6144；此前工具只采 `plcopen_core_r3_tests` 单套件且 RelWithDebInfo 内联吸收头内函数，数字失真）。
- 新核黄金回放回归上线：`plcopen_core_replay_regression` 对 3 个 `core-*.jsonl` 黄金文件逐周期比对。

补充（2026-07-05）：

- R4 变更按风险分级拆为 4 个 commit 合入 `main` 并推送（`75b8a50` T2 隔离、`1cfc7a5` 消费面、`7827a46` CI 工具、`6ea90f4` 文档）。
- Linux GCC 11 交叉验证：`-std=c++17 -O2 -fno-exceptions -fno-rtti` 手工编译新核 5 组测试 + 基准 + 2 个 demo + 2 个 consumer main，全部通过、零告警（验证环境无 CMake，构建系统路径以 Windows 记录与 CI 首跑为准）。
- rewrite-plan §5 DoD 第 5 条的迁移指南落地：[doc/migration-v0-to-v1.md](../migration-v0-to-v1.md)；`PROVENANCE.md` 维持 T3 草案；第 6 条 EOL 公告发布仍待人工执行。

## v0.x EOL 公告草案

`v0.11.0` 是旧 `src/` 线的最后一个功能性检查点。R4 切换后，默认 CMake 包目标、FetchContent、demo 和 Python smoke 均指向新核 `core/`。旧 `src/` 线进入 P0-only 维护窗口：只接收构建失败、数据损坏、错误安全边界和已发布行为的高优先级缺陷修复，不再新增功能块、规划器能力或旧 API 扩面。

建议窗口：从 `v1.0.0-alpha` 发布日起保留 90 天 P0-only 维护。窗口结束后，旧线仅通过 `v0.11.0` tag 和 release source tarball 作为 golden replay 与迁移基线保留。

迁移路径：

1. 新项目直接链接 `plcopen::plcopen` 并 include `core/` 头文件。
2. 需要旧线回放或对照时，源码树构建显式启用 `-DPLCOPEN_BUILD_LEGACY=ON`。
3. 旧 API 的长期兼容层不在 R4 默认范围内；真实下游迁移阻塞应拆成独立 issue。

## `v1.0.0-alpha` 发布草案

发布定位：`v1.0.0-alpha` 是新核消费入口检查点，不是商用级 PLC runtime 或完整 PLCopen 认证版本。

Release notes 应包含：

- 默认 `plcopen::plcopen` 目标切换为新核 `core/`。
- 安装后 `find_package` 与源码树 `FetchContent` 均消费新核。
- 默认 demo 和 Python smoke 已迁移到新核最小公开面。
- 旧 `src/` 默认隔离，只在 `PLCOPEN_BUILD_LEGACY=ON` 时构建。
- 当前已知限制：无 kinematics、无工业总线、无完整 PLC runtime、无 PLCopen 认证承诺、无 v1 ABI 稳定承诺。

发布前必须附上：

- `git diff --check`
- full CTest
- replay fixture format
- RT-safety scan
- fuzz smoke
- installed `find_package` consumer
- `FetchContent` consumer
- pyplcopen smoke
- docs target
- Windows/Linux CI 链接
- [迁移指南](../migration-v0-to-v1.md)（rewrite-plan §5 DoD 第 5 条）
