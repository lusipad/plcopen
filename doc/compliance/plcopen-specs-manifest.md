# PLCopen 规格文档清单（审计依据）

> **为什么仓库里没有 PDF**：文档页脚为 `Copyright © PLCopen. All rights
> reserved.`——**免费下载 ≠ 授权再分发**。本仓库是 Apache 2.0 公开仓，
> 不得再分发第三方版权文档；且出处纪律（CLAUDE.md 硬规则 5）要求
> **合规文档只引条目、不抄文本**。
>
> **审计仍然可复现**：跑 `tools/fetch-plcopen-specs.sh` 取回全部文档，
> SHA256 与本清单逐一校验——任何人都能得到与我们**完全相同**的依据文档。
> 缓存落在 `refs/plcopen-specs/`（已 gitignore）。

## 一键取回

```bash
tools/fetch-plcopen-specs.sh            # 下载 + SHA256 校验 + 抽文本
tools/fetch-plcopen-specs.sh --verify   # 只校验本地已有文件
```

依赖：`curl`、`sha256sum`、`pdftotext`（poppler-utils，抽文本用，可选）。

## 清单（12 份 · 888 页 · 全部公开免费）

来源均为 PLCopen 官方下载页 <https://www.plcopen.org/downloads/>。

### 运动控制规格（TC2）

| 键 | 文档 | 版本 | 页 | SHA256（前 16） | 审计状态 |
|----|------|------|----|----------------|---------|
| `mc_part1` | Function Blocks for Motion Control – Part 1 | 2.0 (2011) | 141 | `adac5d8d5f773624` | ✅ 全文完成（[条款矩阵](plcopen-part1-clause-matrix.md)，D-01~D-20） |
| `mc_part3` | User Guidelines – Part 3 | 2.0 | 94 | `3233f08272ad8b75` | ✅ 全文完成（[25 节审计](plcopen-part3-audit.md)） |
| `mc_part4` | Coordinated Motion – Part 4 | **2.0 (2026-05)** | 217 | `a0b1e688e7c6df17` | ✅ 全文完成（[68 FB 条款审计](plcopen-part4-clause-audit.md)，D4-01~D4-13） |
| `mc_part4_v1` | Coordinated Motion – Part 4 | 1.0（旧版，用于命名溯源） | 119 | `b43683a19b22f67b` | ✅ 全文完成（39 FB 与 v1→v2 迁移，见 Part 4 审计） |
| `mc_part5` | Homing Procedures – Part 5 | 2.0 (2011) | 38 | `d77847d0d355c14f` | ✅ 全文完成（[Part 5/6 审计](plcopen-part5-part6-audit.md)，5 部分/6 缺失） |
| `mc_part6` | Fluid Power Extensions – Part 6 | 2.0 | 27 | `e678ef33533df47c` | ✅ 全文完成（门控未实现，5 FB 均明确不支持） |

### 指南与方法（合规/质量/语言）

| 键 | 文档 | 版本 | 页 | SHA256（前 16） | 审计状态 |
|----|------|------|----|----------------|---------|
| `guide_compliant_fb` | Creating PLCopen compliant Function Block libraries | 1.0 | 4 | `753c241953625525` | ✅ 全文完成（[指南审计](plcopen-guides-audit.md)，G-01） |
| `guide_coding` | PLCopen Coding Guidelines | 1.0 | 127 | `feffbe8e47b78bc2` | ✅ 全文完成（64 条正式 Guideline 规则族，G-02） |
| `guide_quality_metrics` | Guideline Software Quality Metrics | 1.0 | 65 | `2863c2edbe215f5a` | ✅ 全文完成（指标覆盖矩阵，G-03） |
| `guide_quality_automation` | Quality Metrics for Automation Software | — | 5 | `00f1a3990f3e49ac` | ✅ 全文完成（提案性质与成熟度缺口已登记） |
| `guide_oop` | PLCopen OOP Guidelines | 1.0 | 27 | `70d6977342bc3a8a` | ✅ 全文完成（OOP 能力缺失，G-04） |
| `annex_a_e` | Annex A-E: specification method for textual languages | — | 24 | `8b2798805c4b78bc2` | ✅ 全文完成（Annex A-E 覆盖与实现依赖参数，G-05） |

**未纳入**（VISION 显式门控，需要时再取）：Safety Part 1-4 + SafeMotion、
OPC UA（Client FB / 信息模型）、XML-TC6（IEC 61131-10）、OOP 运动库（zip）、
PackML/OMAC 映射、各语言入门与本地化材料。

## 使用纪律

1. **只引条目，不抄文本**——条款矩阵记录"条款号 + 我们自述的要求 + 判定
   + 证据"，**不得复制规格原文**。
2. **不再分发**——PDF 不进 git，不放文档站，不随发布包分发。
3. **版本锁定**——SHA256 变更即上游更新，需重跑审计并在矩阵注明版本。

---

*创建：2026-07-12。审计进度以本表"审计状态"列为准；
详见 [全面合规审计](plcopen-conformance-audit.md) 与
[Part 1 条款矩阵](plcopen-part1-clause-matrix.md)。*
