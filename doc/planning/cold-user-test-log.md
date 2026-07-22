# 冷用户测试登记

Z0 是每次 T/Z 批收口都要重复执行的流程，不是一次性功能。测试必须从公开
README、文档站、PyPI 或 GitHub 公开源码/Release 出发；不得把当前工作区、
本地 build 产物或 `PYTHONPATH` 当作被测产品。

## 收口清单

1. PR 标明 T/Z 批次；不属于 T/Z 时填 `n/a`。
2. 合入并等待文档/工件公开后，手动运行 `Cold User` workflow，填写公开版本和
   source ref。
3. workflow 必须验证 PyPI notebook、C++ 安装消费者、ST 旅程和公开文档入口。
4. 成功链接回填到本表或下一次状态 PR；失败必须开 issue，并在修复前保持批次
   未关闭。

## 记录

| 批次 | 提交/公开版本 | 环境与入口 | 结果 | 失败 issue |
|---|---|---|---|---|
| Z0′ | `v0.20.0` | Linux CPython 3.13 wheel；Windows CPython 3.14 sdist；公开 Python 指南 | 通过，见 [首轮实施记录](z0-cold-user-test-implementation-notes.md) | — |
| Z1～Z5 收口 | `a1673d7`；`pyplcopen==0.20.0` + 公开 `main` | `Cold User` workflow：公开 notebook / C++ / ST / 文档站 | [通过（run 29920696958）](https://github.com/lusipad/plcopen/actions/runs/29920696958)，29 秒 | — |

## 本地预检（不替代公开证据）

PR 阶段可以用本地构建提前发现问题，但必须明确标为预检。公开证据只有合入后
从网络入口重新取得的工件和文档。
