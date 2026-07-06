# 信号通道细案（当前里程碑并行项 4b，2026-07-07）

> 拷问结论落地："学习不等建造"——最大未知数（有没有人用）的首个
> 实验与 Y7/P 系列并行。两件套，全部 T1 级（无核心语义，免矩阵）。

## 1. pip 包（T1/Z1 细案）

- **包名 `pyplcopen`**（与模块名一致；内核命名议题已收回，不再造名）；
- 构建：`pyproject.toml`（scikit-build-core 驱动既有 CMake）+
  cibuildwheel GitHub Actions 工作流（win/linux/mac × py3.10-3.13）；
- 版本：跟随内核 SemVer（当前 1.0.0a1 —— PEP 440 的 alpha 表记）；
- 内容物：现有 AxisSim/PoseArmSim/流接口/cam 工具全量 + README 快速
  开始（中文为主 + 英文摘要节）；
- **发布动作属人**（PyPI token，人专属清单已有）；CI 先出 artifact
  供本地验证，token 到位即发；
- 验收：三平台 wheel 构建绿 + 干净环境 `pip install` 后跑通 smoke
  片段（Z0 冷用户测试首次执行）。

## 2. 最小文档站（Z3 起步形态）

- 工具：**mkdocs + material 主题**（Python 生态、零 JS 工程、GitHub
  Pages 直出）；
- v1 结构（只做门廊，不搬运全部文档）：
  - 首页 = README 定位 + "一个内核，多种控制方式"表；
  - 三条 30 分钟旅程：Python 数字孪生（pip 起步）/ C++ 嵌入
    （find_package 起步）/ 算法白盒（矩阵与 oracle 导读）；
  - 现有 doc/compliance、doc/design 以链接形式挂出（不复制，防两处
    维护）；
- 托管：GitHub Pages（gh-pages 分支，CI 自动部署）；域名后置；
- 验收：站点可访问 + 三旅程可从零走通（Z0 口径）。

## 3. 度量（进 v7 §0.7 KPI）

pip 周下载、站点 UV、GitHub star/issue 增量——S0 出口条件"外部信号
非零基线"的数据源。
