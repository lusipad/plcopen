#!/usr/bin/env bash
# 取回 PLCopen 规格文档到本地缓存（供合规审计使用）。
#
# 为什么是脚本而不是把 PDF 提交进仓库：
#   1. 版权——PDF 页脚为 "Copyright © PLCopen. All rights reserved."；
#      免费下载 ≠ 授权再分发。本仓库是 Apache 2.0 公开仓，不得再分发。
#   2. 出处纪律（CLAUDE.md 硬规则 5 / plcopen-provenance 技能）：
#      合规文档只引条目、不抄文本。
#   3. 体积——12 份共 ~28 MB，不该永久留在 git 历史。
#
# 本脚本让审计**可复现**：任何人跑一次即可得到与我们完全相同的文档
# （SHA256 校验），而仓库里只有"取回它们的能力"。
#
# 用法：
#   tools/fetch-plcopen-specs.sh          # 下载 + 校验 + 抽文本
#   tools/fetch-plcopen-specs.sh --verify # 只校验已有文件
#
# 依赖：curl、sha256sum、pdftotext（poppler-utils，抽文本用；可选）
# 输出：refs/plcopen-specs/{name}.pdf 与 {name}.txt（已 gitignore）

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/refs/plcopen-specs"
BASE="https://www.plcopen.org/download_file/force"
VERIFY_ONLY=0
[ "${1:-}" = "--verify" ] && VERIFY_ONLY=1

# name|uuid|sha256|页数
DOCS=(
  "mc_part1|9f19d854-2dbf-4e07-a2ff-e5ff1a3e293a|adac5d8d5f77362469480fe393825c3e8433e20e24514544286ce8e209c3c507|141"
  "mc_part3|1f500e89-0c3c-467f-908b-85cd73e51a22|3233f08272ad8b750b8aaf058add1863e1b8683ae253f15490551190acb90841|94"
  "mc_part4|938d04f2-0c6e-44c1-bb24-d36ba0e12e9d|a0b1e688e7c6df17bac1ee907568d6dfa5739b7a9a1ad188f039ea2d709a5947|217"
  "mc_part4_v1|595f84ef-8be9-4b37-8da6-9e0df4d99388|b43683a19b22f67bc4727267c68d6dc5f9ad17f3cdc2876126e18b70489476f1|119"
  "mc_part5|a43abb73-7d50-4037-8da6-41fb9ae6a3c6|d77847d0d355c14f25b9b9488a304b808586b8dfc76e2c058d2e93a5d141357a|38"
  "mc_part6|9d64ccb7-4b78-4af3-b477-14d2df272256|e678ef33533df47ca46d07775b634feb9b05f4d812fe5227827c1a1d42ce8ec8|27"
  "guide_compliant_fb|7ea712b1-1f89-43fc-870c-6006e415956f|753c241953625525e97c26019093b5fb4c69140286b72105d5de3978a4a94815|4"
  "guide_coding|ff60e817-eee5-442d-b718-a357a7279c7e|feffbe8e47b78bc2cb822cb88ec4e9ceff43c159cb0d23a40bcdc20a097dc948|127"
  "guide_quality_metrics|5378acfc-c952-4d40-ad76-0ae1c008c188|2863c2edbe215f5aeed7d93758fb28258eb9f15ead803fcf8140bd06cbb8e5e8|65"
  "guide_quality_automation|153ba450-e28f-415c-9bcf-710251f836dc|00f1a3990f3e49ac0ae7dd6de41f050e1362b73555faf5cabbbad90d7883501c|5"
  "guide_oop|14f8df00-91d6-45d6-a161-76f9b5895210|70d6977342bc3a8a4d3e3c069c7852638b484902727522706bb1da86c5c44456|27"
  "annex_a_e|62803da1-28e1-4ad2-9082-18bf065d981d|8b2798805c4a530bfffab159017e3fd50e82eef7e9256d59efce3d9cf5e45f59|24"
)

mkdir -p "$DEST"
fail=0
have_pdftotext=0
command -v pdftotext >/dev/null 2>&1 && have_pdftotext=1

printf '%-26s %8s  %s\n' "文档" "页数" "状态"
printf '%-26s %8s  %s\n' "---" "---" "---"

for entry in "${DOCS[@]}"; do
    IFS='|' read -r name uuid want pages <<<"$entry"
    pdf="${DEST}/${name}.pdf"

    if [ ! -s "$pdf" ] && [ "$VERIFY_ONLY" -eq 0 ]; then
        curl -sSL --fail -o "$pdf" "${BASE}/${uuid}/342" 2>/dev/null || true
    fi

    if [ ! -s "$pdf" ]; then
        printf '%-26s %8s  ❌ 缺失\n' "$name" "$pages"; fail=1; continue
    fi

    got="$(sha256sum "$pdf" | cut -d' ' -f1)"
    if [ "$got" != "$want" ]; then
        printf '%-26s %8s  ⚠️  SHA256 不符（上游可能更新，请核对清单）\n' "$name" "$pages"
        fail=1; continue
    fi

    if [ "$have_pdftotext" -eq 1 ] && [ ! -s "${DEST}/${name}.txt" ]; then
        pdftotext -layout "$pdf" "${DEST}/${name}.txt" 2>/dev/null
    fi
    printf '%-26s %8s  ✅\n' "$name" "$pages"
done

echo
echo "缓存目录：${DEST}（已 gitignore，不进版本库）"
[ "$have_pdftotext" -eq 0 ] && \
  echo "提示：未找到 pdftotext，仅下载 PDF。安装 poppler-utils 可自动抽取文本。"
echo "清单与官方链接：doc/compliance/plcopen-specs-manifest.md"
exit $fail
