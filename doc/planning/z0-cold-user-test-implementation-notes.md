# Z0′ 冷用户首轮实施记录

Plan: [软件极致计划 Z0′](software-excellence-plan.md#计划补遗2026-07-12-维护者指令盘点挂号缺失的软件项)

2026-07-22 首轮已完成：从公开 README、文档站与 PyPI `v0.20.0`
出发，Linux CPython 3.13 预编译 wheel 和 Windows CPython 3.14
sdist 回退均在干净环境跑通。Python 指南五组示例全部可执行；
本批只修正安装工具链提示和 SI 示例的误导性零速回读。

## Decisions

- 冷用户证据不使用源码树、`PYTHONPATH` 或本地 build 产物；安装来源
  只是 [PyPI `pyplcopen==0.20.0`](https://pypi.org/project/pyplcopen/0.20.0/)。
- 预编译路径用 `python:3.13-slim` 干净容器并强制
  `--only-binary=:all:`；实际安装 manylinux x86_64 wheel 后运行 README
  stream 示例。
- Windows 路径用新建 venv 与本机唯一可用的 CPython 3.14；
  pip 从 sdist 生成 `cp314-win_amd64` wheel 并运行同一示例。
- Python 指南的 single-axis、SI、stream、pose 与 cam 五段代码在
  隔离工作目录中逐段执行。
- `ci/docs_python_smoke.py` 现从公开指南直接提取这五个 Python
  fence，分别在临时目录执行；`pyplcopen_smoke_docs` 与现有绑定
  smoke 同时进入 Linux PR 门。

## Deviations

- 无。本轮预注册为 Python 公开入口首次实跑，不把 C++ 消费者、
  macOS 或 Windows 3.10～3.13 预编译 wheel 扩张成本批完成条件。

## Surprises

- `requires-python >=3.10` 使 Python 3.14 合法落到 sdist；有编译工具链时
  可成功，但不是文档开头所说的“无 C++ 工具链”路径。
- `AxisSim.move_absolute()` 返回时已经停稳，旧 SI 示例随后读
  `command_velocity()` 固定显示 `0.0 mm/s`，不能证明 200 mm/s 转换。

## Questions for review

- 无阻断问题。后续若把 Python 3.14 加入预编译矩阵，需作为下一发布
  批次单独验证，不倒写本轮 `v0.20.0` 工件事实。
