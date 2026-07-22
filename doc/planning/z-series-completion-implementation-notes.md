# Z 系列收口实施记录

## Decisions

- Python、C++、ST 是三条主旅程；算法白盒归入参考。
- `ErrorCode` 继续以现有枚举值和 `to_string()` 为兼容合同，提示为附加只读元数据。
- trace 可视化以已有 `PLCT v1` 为输入，输出单文件 HTML；ST `DebugTraceRecord`
  仍只属于 ST 调试宿主 API。
- Z4 区分本仓配方完成、外部 registry 提交、外部 registry 收录三个状态。
- Z0 是持续流程而不是一次性功能；本批以登记模板和本次 Z 收口冷启动记录验收。

## Deviations

- 原计划把 Z4 写成一个“发行生态”结果；实施时拆成仓库资产、上游提交和中央
  收录三层。本批只关闭可由本仓自动验证的第一层，不伪造外部账号、签署或审批。
- 文档 API 由 MkDocs 主站携带 Doxygen 子目录，而不是额外维护第二套发布站点；
  这样保留单一 Pages 入口，也避免重复导航和发布状态。

## Surprises

- Z5 的短错误文本、ST 固定容量 trace 环和 `PLCT v1` 解析器均已存在，缺口主要
  是排障提示、Python 可发现性和可视化输出，而不是新的实时采集机制。
- 根 `vcpkg.json` 是清单草稿而非可用 port；现有 Conan recipe 也没有真正的
  `test_package/conanfile.py`，因此此前“Conan recipe 已有”不等于消费者闭环。
- 原计划中的 Z1 还明确要求 notebook，Z2 还要求轴/组配置结构；发布 wheel 和
  `CycleConfig` 只能证明各自完成了一半，不能按标题推断整批已完成。
- vcpkg 从带根清单的仓库运行 overlay port 时会自动进入 manifest mode；严格门
  必须显式使用 `--classic`。真实安装还暴露了 header-only port 的空 `lib/`
  告警，portfile 删除空目录后才通过 `--enforce-port-checks`。
- Windows 上一次超时的 MSBuild 仍在后台占用同一 PDB，导致重叠构建互相冲突；
  终止本批启动的残留进程并单路重跑后通过，未发现源码编译缺陷。

## Verification

- Windows Visual Studio Debug：CTest 98/98（含 11 fuzz）通过，日常非 fuzz
  子集 87/87；新增 Python、notebook、ST 旅程和 trace 定向门 7/7。
- RT 静态扫描通过 30 个周期域文件；18 份 replay、2409 个样本零差异。
- Conan 2.30 根 recipe 以 C++17 Release 创建成功，C++ 与 ST 两个 installed
  package 消费者均通过；ConanCenter 目录 recipe 的同类预检通过。
- vcpkg overlay port 的 `x64-windows-static` 全新安装在
  `--enforce-port-checks` 下通过；installed `find_package` 消费者通过。
- `mkdocs build --strict`、Doxygen 1.17 + Graphviz 15.1、actionlint 1.7.12、
  Python bytecode 编译、所有 workflow YAML 解析与 `git diff --check` 通过。
- [PR #12](https://github.com/lusipad/plcopen/pull/12) 的 Windows、Linux GCC/
  Clang、ARM64、Documentation 与 E5 门禁全绿；Pages strict deploy 修复由
  [PR #13](https://github.com/lusipad/plcopen/pull/13) 独立复验并合入。
- 主干 [Documentation run 29920396387](https://github.com/lusipad/plcopen/actions/runs/29920396387)
  的 build/deploy 均通过；公开 Python/C++/ST、notebook 与 Doxygen API 返回
  HTTP 200。随后 [Cold User run 29920696958](https://github.com/lusipad/plcopen/actions/runs/29920696958)
  从公开 notebook、PyPI wheel、公开 `main` 和文档站完成 29 秒端到端复验。
- 外部 registry 收录仍未发生；本记录不以本仓绿灯替代 ConanCenter/vcpkg
  上游 PR、CI 与维护者审核。

## Questions

- ConanCenter 与 vcpkg curated registry 的最终收录时间由外部维护者决定；本批
  只承诺生成、验证并如实登记提交所需资产。
