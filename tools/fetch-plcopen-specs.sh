#!/usr/bin/env bash
# 取回 PLCopen 规格文档到本地缓存（供合规审计使用）。
#
# 为什么是脚本而不是把 PDF 提交进仓库：
#   1. 版权——PDF 页脚为 "Copyright © PLCopen. All rights reserved."；
#      免费下载 ≠ 授权再分发。本仓库是 Apache 2.0 公开仓，不得再分发。
#   2. 出处纪律（CLAUDE.md 硬规则 5 / plcopen-provenance 技能）：
#      合规文档只引条目、不抄文本。
#   3. 体积——35 个文件共数十 MB，不该永久留在 git 历史。
#
# 本脚本让审计**可复现**：任何人跑一次即可得到与我们完全相同的文档
# （SHA256 校验），而仓库里只有"取回它们的能力"。
#
# 用法：
#   tools/fetch-plcopen-specs.sh          # 下载 + 校验 + 抽文本
#   tools/fetch-plcopen-specs.sh --verify # 只校验已有文件
#
# 依赖：curl、sha256sum、pdftotext（poppler-utils，抽文本用；可选）
# 输出：refs/plcopen-specs/{name}.{pdf,zip,csv}；PDF 同时抽取 {name}.txt

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/refs/plcopen-specs"
BASE="https://www.plcopen.org/download_file/force"
VERIFY_ONLY=0
[ "${1:-}" = "--verify" ] && VERIFY_ONLY=1

# name|uuid|扩展名|sha256|页数（非 PDF 为 -）
DOCS=(
  "mc_part1|9f19d854-2dbf-4e07-a2ff-e5ff1a3e293a|pdf|adac5d8d5f77362469480fe393825c3e8433e20e24514544286ce8e209c3c507|141"
  "mc_part3|1f500e89-0c3c-467f-908b-85cd73e51a22|pdf|3233f08272ad8b750b8aaf058add1863e1b8683ae253f15490551190acb90841|94"
  "mc_part4|938d04f2-0c6e-44c1-bb24-d36ba0e12e9d|pdf|a0b1e688e7c6df17bac1ee907568d6dfa5739b7a9a1ad188f039ea2d709a5947|217"
  "mc_part4_v1|595f84ef-8be9-4b37-8da6-9e0df4d99388|pdf|b43683a19b22f67bc4727267c68d6dc5f9ad17f3cdc2876126e18b70489476f1|119"
  "mc_part5|a43abb73-7d50-4037-8da6-41fb9ae6a3c6|pdf|d77847d0d355c14f25b9b9488a304b808586b8dfc76e2c058d2e93a5d141357a|38"
  "mc_part6|9d64ccb7-4b78-4af3-b477-14d2df272256|pdf|e678ef33533df47ca46d07775b634feb9b05f4d812fe5227827c1a1d42ce8ec8|27"
  "guide_compliant_fb|7ea712b1-1f89-43fc-870c-6006e415956f|pdf|753c241953625525e97c26019093b5fb4c69140286b72105d5de3978a4a94815|4"
  "guide_coding|ff60e817-eee5-442d-b718-a357a7279c7e|pdf|feffbe8e47b78bc2cb822cb88ec4e9ceff43c159cb0d23a40bcdc20a097dc948|127"
  "guide_quality_metrics|5378acfc-c952-4d40-ad76-0ae1c008c188|pdf|2863c2edbe215f5aeed7d93758fb28258eb9f15ead803fcf8140bd06cbb8e5e8|65"
  "guide_quality_automation|153ba450-e28f-415c-9bcf-710251f836dc|pdf|00f1a3990f3e49ac0ae7dd6de41f050e1362b73555faf5cabbbad90d7883501c|5"
  "guide_oop|14f8df00-91d6-45d6-a161-76f9b5895210|pdf|70d6977342bc3a8a4d3e3c069c7852638b484902727522706bb1da86c5c44456|27"
  "annex_a_e|62803da1-28e1-4ad2-9082-18bf065d981d|pdf|8b2798805c4a530bfffab159017e3fd50e82eef7e9256d59efce3d9cf5e45f59|24"
  "annex_f|199f5334-01d0-4cce-a514-d8ba275ff615|pdf|94b115dfb889bfbf986c5bba35c7852006409d547e108bf2f9599064ac4e7122|46"
  "mc_examples|9e5feea0-99f7-4e1f-872a-1b2f7c9f3256|pdf|948b4e59449dd397d583ff382c0136ea2e12b51c4440f2dddfedd696bd0084a4|4"
  "mc_oop_examples|0bb2fe53-3a50-48ad-87bb-101472b1f0d7|pdf|8683cfadfc3a61f0a9202769d2aba756bfde73f6f3b8323e72bb63b0d54af037|46"
  "mc_oop_library|c718c870-0c02-4047-899d-5508bb18dc38|zip|1f3e2d2fef6208e3d423223d7352ac7bca35fa3c4ffb0527b1d7709a307fd362|-"
  "safety_part1|6a32064b-14cc-49bb-99a6-1b00e418ee94|pdf|f011cd83dccc6d3903128f9155b2ce05654a63ac754e1784a2772215ac3a1c7c|194"
  "safety_part2|7ac434db-4581-435f-bc1d-74222365be7e|pdf|1b771cfd7ec0657f79ddc238a31635e2bc9b62929a649434c2a948bbeb6b0430|42"
  "safety_part3|56d0eb46-6ace-4430-8b2a-f9456ba0ed69|pdf|64684a550ce783d253c3cf587f110cbaa428091ce636b6a395ca13f1c4b8ea83|61"
  "safety_part4|d85a9629-cbe5-47e8-bd3b-774246709d3e|pdf|b156cc8ace490ecdd04971417e9ec7b11dfa2409d014f6fdd4293a6568049be8|136"
  "safemotion|2b6d634e-8dde-46c4-949f-bc8bc6eb7589|pdf|44f8f677549d77ff228249d43d037e25ee89250073c97d8e20f61d233e5733b6|45"
  "safety_logic_motion|bc285d15-8014-425d-8f2a-1dbd6a5bc197|pdf|bd87c957949667d804c1a800e9ccb5fcd9cf2b2318485204286a145b7c1ab6b7|15"
  "opcua_client_fb|65a31dd9-fc32-413c-98c6-609a3cb468df|pdf|096350bbaf511b7bb2f344d3c97e01ef234d698ab3a962af8b5bec3de523a492|71"
  "opcua_info_model|16bedd4c-ff89-4abd-917a-18f419a7a29e|pdf|db1eef5d5aac535bf7851be266936fcc267cbbbf67f4e8952d0abcd1a5fcd84e|64"
  "opcua_info_model_nodeset|ef89d6cd-caee-41c0-b156-758e20dcaec5|zip|20e3104fc5830c96c6b62274cf199e8a8b9fc05bcb2961b57e90dc748d1267d4|-"
  "opcua_fb_nodeids|2c0a9e15-6b90-4787-b6c5-a39e686b8eae|csv|5e72a4f293855c52ca46d5c8e31e50b60cb8391c258876baec9d5c25b68d68da|-"
  "opcua_client_arch|6983c9c7-197b-4e67-88ab-f444b5acc247|pdf|907622d50e84f109646862ede5759755ed04fc6855c610e79b166c23fd80ffaf|4"
  "opcua_use_cases|686733f5-b0cf-4bda-afb2-4f9d82d89add|pdf|3e96b88a29eca1f0872c67f6c0c32aa2a7f8b643c5e597de7797165a3c3fc481|1"
  "xml_v201_technical|df0925d6-8953-4d87-a12d-f603ad25ddc6|pdf|446dfb76bfb5e864908f9594820fdfd2a436073783a8b8cf3465100d17610209|80"
  "xml_v201_xsd_doc|5087ac70-cfa8-4ff6-ae33-ae67a71fd0b2|pdf|bb7384b179005e147a60b05f3693452365182abc8890fb03a4ba14f3451f0d1e|340"
  "xml_v201_xsd|d397ab98-6a21-47d5-9fb1-37da9e94ef34|zip|2e412f478a21fdedaf758cb7da19e333bcbf95e30b56ee24293f09dc32244b99|-"
  "iec61131_10_components|0df46fd9-5dc4-40a9-9bcf-bee135b1c253|zip|154efd2f9159a96c1c36688541256060361f60e5d6dc3932ba749f09e502f992|-"
  "guide_packml_mapping|91a361c1-b61b-4e8f-a321-ead1d8e92136|pdf|196a32f325b75e1004e8e8201cd0fef11d1676bdf8d63fecde647143067797e3|4"
  "guide_structuring|36c7aa6d-dd3c-4966-b4b5-a4c278632090|pdf|2f5f9bd48d854c594e9df4e1f8c624fc02b266b89607205a1574e2d779a13e9d|4"
  "guide_software_evaluation|00d57736-e0f6-4db4-8fd2-009785834f35|pdf|5d86fef9538a39327193f824d5fb4f667d6acf120f4630d0696656f163bfec5f|2"
)

mkdir -p "$DEST"
fail=0
have_pdftotext=0
command -v pdftotext >/dev/null 2>&1 && have_pdftotext=1

printf '%-26s %8s  %s\n' "文档" "页数" "状态"
printf '%-26s %8s  %s\n' "---" "---" "---"

for entry in "${DOCS[@]}"; do
    IFS='|' read -r name uuid ext want pages <<<"$entry"
    file="${DEST}/${name}.${ext}"

    if [ ! -s "$file" ] && [ "$VERIFY_ONLY" -eq 0 ]; then
        curl -sSL --fail -o "$file" "${BASE}/${uuid}/342" 2>/dev/null || true
    fi

    if [ ! -s "$file" ]; then
        printf '%-26s %8s  ❌ 缺失\n' "$name" "$pages"; fail=1; continue
    fi

    got="$(sha256sum "$file" | cut -d' ' -f1)"
    if [ "$got" != "$want" ]; then
        printf '%-26s %8s  ⚠️  SHA256 不符（上游可能更新，请核对清单）\n' "$name" "$pages"
        fail=1; continue
    fi

    if [ "$ext" = "pdf" ] && [ "$have_pdftotext" -eq 1 ] && [ ! -s "${DEST}/${name}.txt" ]; then
        pdftotext -layout "$file" "${DEST}/${name}.txt" 2>/dev/null
    fi
    printf '%-26s %8s  ✅\n' "$name" "$pages"
done

echo
echo "缓存目录：${DEST}（已 gitignore，不进版本库）"
[ "$have_pdftotext" -eq 0 ] && \
  echo "提示：未找到 pdftotext，仅下载 PDF。安装 poppler-utils 可自动抽取文本。"
echo "清单与官方链接：doc/compliance/plcopen-specs-manifest.md"
exit $fail
