# 贡献指南

感谢你为 plcopen 提交问题、测试、文档或代码。本项目把规格、测试和门禁
视为交付的一部分；“能编译”不是完成标准。

## 先确认范围

开始前按顺序阅读：

1. [CONTEXT.md](CONTEXT.md)：术语；
2. [STATUS.md](STATUS.md)：当前已经支持什么；
3. [ROADMAP.md](ROADMAP.md)：当前承诺与顺序；
4. [doc/compliance/](doc/compliance/) 与对应模块 README：规范、已知边界和
   模块不变量；
5. [CLAUDE.md](CLAUDE.md)：项目硬规则。规则适用于任何人或 AI 工具。

Bug 修复、测试和文档可以直接提 PR。路线内功能建议先开
[issue](https://github.com/lusipad/plcopen/issues/new/choose)；路线外能力先在
[Discussions](https://github.com/lusipad/plcopen/discussions) 对齐价值与范围。
冻结的旧线 `src/` 只接受 P0 级回归修复；新工作默认进入 `core/`。

以下改动必须先完成对应对齐，不能用“CI 已绿”替代：

| 改动 | 前置条件 |
|------|----------|
| 新行为、公共语义或未定义组合 | 先提交语义矩阵，由维护者批准后再实现 |
| 改变既有周期路径输出 | 登记 KB、声明变更、重录并量化回放基线 |
| 架构裁决 | 先查 `doc/design/decisions/`；无先例时新增 ADR |
| tag、GitHub Release | 维护者明确指定版本和目标提交 |
| LICENSE/NOTICE/PROVENANCE、账号、签署、外部承诺 | 人专属；AI 只能起草 |

## 开发环境

最低要求与完整选项见 [BUILD_README.md](BUILD_README.md)：CMake 3.21+、
C++17 编译器；Windows 使用 Visual Studio 2022+ 与 PowerShell，Linux 使用
受支持的 GCC/Clang。

Windows 快速入口：

```powershell
.\build.ps1 -Configuration Debug -Test
```

跨平台入口：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

普通 PR 的 CI 排除 `fuzz` 标签；解析器、ST、状态机或容量边界改动应额外
运行对应 fuzz target。完整 fuzz 与 sanitizer/TSAN 深测由 Core Nightly 执行。

## 工作方式

1. **把问题写成可验收目标。** Issue 至少写清目标、影响层/KB、完成条件、
   验证命令和文档影响；仓库模板已经包含这些字段。
2. **测试先行。** Bug 先提交可复现的失败测试；新语义先由已批准矩阵生成
   验收测试，再实现最小修复。
3. **保持外科手术式差异。** 不顺手重构相邻代码，不引入未要求的抽象或
   依赖；删除仅限本次改动产生的孤儿代码。
4. **守住 RT 边界。** 周期路径禁止堆分配、阻塞锁、异常、系统调用和浮点
   时间累加。细节以 `.claude/skills/plcopen-rt-safety/` 为准。
5. **文档同 PR。** 公共头文件、合规矩阵、行为边界或用户命令变化时，
   同步相应 README、合规文档和 CHANGELOG。
6. **记录出处。** 禁止复制 GPL 实现、冻结旧核实现或 PLCopen/IEC 标准原文；
   外部代码与文档必须遵守 [PROVENANCE.md](PROVENANCE.md) 和
   `.claude/skills/plcopen-provenance/`。

### 测试安全红线

单元测试必须隔离、确定且资源有界：只操作进程内存或由测试创建并验证
归属的临时目录。单元测试不得扫描、修改或删除用户/系统目录、注册表、
真实服务或设备，不得访问网络、持久化、自复制或规避检测。需要外部进程、
网络、设备或系统副作用的验证必须进入显式集成测试，默认门禁不得执行。

## 提交前验证

维护者的 Windows 完成态目录惯例为 `build-sync`；其他环境可替换成自己的
已配置构建目录，但必须保留 Debug 与退出码语义：

```bash
cmake --build build-sync --config Debug
ctest --test-dir build-sync -C Debug --output-on-failure
cmake -P cmake/rt_safety_scan.cmake
cmake -P cmake/verify_replay_fixtures.cmake
git diff --check
```

不要把 `ctest` 接到会吞掉退出码的管道。`git diff --check` 在 Windows 可有
LF/CRLF 提示，但不得有空白错误。

按改动追加：

| 改动面 | 验证 |
|--------|------|
| 合规矩阵 | 运行对应 `cmake/generate_*_matrix.cmake`，生成后工作树无意外差异 |
| 文档站 | `python -m pip install mkdocs mkdocs-material`，然后依次运行 `mkdocs build --strict -f mkdocs.yml` 与 `mkdocs build --strict -f mkdocs.en.yml` |
| 覆盖率敏感代码 | `.\coverage.ps1 -BuildDir build-sync -Configuration Debug` |
| 性能/周期路径 | 对应 benchmark、回放与零分配测试 |
| Linux 静态分析 | `cmake/clang_tidy_gate.cmake`；本地缺工具时以 Linux CI 为准 |

完整 CI 责任矩阵见 [doc/compliance/ci-gates.md](doc/compliance/ci-gates.md)。

## 提交与 PR

提交主题使用 Angular/Conventional 形式：

```text
<type>(<scope>)<!?>: <一句话摘要>（<KB 号或批次>）
```

`type` 使用 `feat/fix/docs/chore/bench/revert`。正文用中文说明目的、设计取舍、
修改内容、影响范围和实际门禁数字；声明变更必须写出回放场景及量化差异。
一次提交只做一件事。AI 辅助的提交与 PR 必须如实填写 AI 生成声明；完整
格式以 `.claude/skills/plcopen-commit-style/` 为准。

PR 使用仓库模板，并完成：

- affected layers / KB ids；
- semantic matrix 是否批准或未触及；
- 是否为声明变更、回放是否重录；
- build、tests、replay、RT-safety、benchmark 的实际证据；
- AI-assisted yes/no。

门禁通过是必要条件，不会自动解决未批准语义、许可证或安全边界。合并与
裁决权限见 [GOVERNANCE.md](GOVERNANCE.md)。安全问题不要公开披露细节，按
[SECURITY.md](SECURITY.md) 报告。
