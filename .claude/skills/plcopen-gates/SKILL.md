---
name: plcopen-gates
description: plcopen 提交前门禁——全量测试、RT 扫描、回放校验的准确命令与已知误报坑。Use before any commit/push in this repo, when the user says 门禁/gates, or when another skill finishes an implementation step.
---

# 门禁（提交前全套）

按序执行，全绿才允许提交。构建目录惯例：`build-sync`（VS 2026）。

```bash
cmake --build build-sync --config Debug            # 0 error
ctest --test-dir build-sync -C Debug --output-on-failure   # 100% pass
cmake -P cmake/rt_safety_scan.cmake                # RT-safety scan passed
cmake -P cmake/verify_replay_fixtures.cmake        # fixtures verified
git diff --check                                   # 只允许 CRLF warning
```

按需追加：

- 覆盖率：`.\coverage.ps1 -BuildDir build-sync -Configuration Debug`（门槛见 STATUS.md）
- DoD §5.3 对照：`build-legacy-cmp` 目录（`PLCOPEN_BUILD_LEGACY=ON`）构建
  `plcopen_core_legacy_compare` 并运行（exe 旁需 `plcopen.dll`）
- 推送后确认 CI：`gh run list --limit 2`（Windows CI + Linux CI 双绿）

## RT 扫描的已知词法坑（正则级匹配，注释也会命中）

扫描目录：`core/{adapters,rt,otg,geom,exec,kin,stream}` + 任何含 `RT-SAFE`
标记的 core 文件。**注释措辞要避开这些词形**：

- `system (` —— 任何后跟 `(` 的 system 单词（写 "equations (" 之类替代）
- 独立单词 `new` / `delete` / `throw` / `try` / `catch`（"new mode"→"next
  mode"，"catch-up"→"recovery"）
- `double`/`float` 类型后名字含 `dt`/`time`/`elapsed` 的变量（时间一律
  `std::int64_t *_cycles`）
- `<chrono>`/`<thread>`/`<iostream>`/`<fstream>` include

## 测试可执行体的已知坑

- **门禁必须按上面用 Debug 跑 ctest，不要图快换 Release**：Windows CI
  覆盖率步骤会用 Debug 重跑全部测试 exe，Release-only 自检会漏掉
  Debug-only 崩溃（2026-07-05 实例：pose_tests 栈溢出漏到 CI）。
- **≥6 轴的组测试台不要放栈上**：MSVC 默认 1MB 栈，Debug 帧膨胀下
  单函数两个 6 轴 rig（6×AxisModel + AxisGroup）即 `0xC00000FD`
  栈溢出且**无任何输出**（Git Bash 报 exit 127）——测试台局部量一律
  `static`（3 轴 rig 恰好没超线，别以此类推）。

## 回放红了怎么办

先判断是不是声明变更（见 `plcopen-replay-baseline`）；不是 → 你改坏了
周期路径，修代码而不是改基线。

完成判据：上述五条命令全绿 + （如推送）CI 双绿。
