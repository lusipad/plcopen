#!/bin/bash

# PLC运行时核心系统 CI基准测试运行脚本
# 用于本地运行和CI环境中的基准测试

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."
    
    # 检查必要的工具
    local missing_deps=()
    
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi
    
    if ! command -v ninja &> /dev/null && ! command -v make &> /dev/null; then
        missing_deps+=("ninja 或 make")
    fi
    
    if ! command -v python3 &> /dev/null; then
        missing_deps+=("python3")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "缺少依赖: ${missing_deps[*]}"
        log_info "请安装缺少的依赖后重试"
        exit 1
    fi
    
    log_success "依赖检查通过"
}

# 配置实时环境
setup_realtime_env() {
    log_info "配置实时环境..."
    
    # 检查是否为Linux系统
    if [[ "$OSTYPE" != "linux-gnu"* ]]; then
        log_warning "非Linux系统，跳过实时环境配置"
        return 0
    fi
    
    # 检查是否有root权限
    if [ "$EUID" -eq 0 ]; then
        log_info "检测到root权限，配置实时环境..."
        
        # 设置CPU频率为性能模式
        if command -v cpupower &> /dev/null; then
            cpupower frequency-set --governor performance 2>/dev/null || true
            log_info "CPU频率已设置为性能模式"
        fi
        
        # 设置实时调度参数
        sysctl -w kernel.sched_rt_runtime_us=950000 2>/dev/null || true
        sysctl -w kernel.sched_rt_period_us=1000000 2>/dev/null || true
        
        # 禁用CPU节能功能
        echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null || true
        
        log_success "实时环境配置完成"
    else
        log_warning "无root权限，跳过实时环境配置"
        log_info "建议使用 sudo 运行以获得最佳性能"
    fi
}

# 构建项目
build_project() {
    log_info "构建项目..."
    
    local build_dir="build"
    local build_type="${BUILD_TYPE:-Release}"
    
    # 创建构建目录
    mkdir -p "$build_dir"
    cd "$build_dir"
    
    # 配置CMake
    local cmake_args=(
        -DCMAKE_BUILD_TYPE="$build_type"
        -DENABLE_TESTING=ON
        -DENABLE_BENCHMARKS=ON
    )
    
    # 选择构建系统
    if command -v ninja &> /dev/null; then
        cmake_args+=(-G Ninja)
        local build_cmd="ninja"
    else
        local build_cmd="make -j$(nproc)"
    fi
    
    log_info "CMake配置: ${cmake_args[*]}"
    cmake "${cmake_args[@]}" ..
    
    # 构建
    log_info "开始构建..."
    $build_cmd
    
    cd ..
    log_success "项目构建完成"
}

# 运行基准测试
run_benchmarks() {
    log_info "运行CI基准门禁测试..."
    
    local build_dir="build"
    local benchmark_exe="$build_dir/tests/ci/ci-benchmark-gates"
    
    if [ ! -f "$benchmark_exe" ]; then
        log_error "基准测试可执行文件不存在: $benchmark_exe"
        exit 1
    fi
    
    # 切换到构建目录
    cd "$build_dir"
    
    # 设置环境变量
    export GTEST_OUTPUT="xml:ci-benchmark-results.xml"
    
    # 运行基准测试
    local run_cmd="./tests/ci/ci-benchmark-gates"
    
    # 如果有root权限，使用实时优先级运行
    if [ "$EUID" -eq 0 ]; then
        log_info "使用实时优先级运行基准测试..."
        run_cmd="chrt -f 99 nice -n -20 $run_cmd"
    elif command -v sudo &> /dev/null && sudo -n true 2>/dev/null; then
        log_info "使用sudo实时优先级运行基准测试..."
        run_cmd="sudo chrt -f 99 nice -n -20 $run_cmd"
    else
        log_warning "无实时权限，使用普通优先级运行"
    fi
    
    log_info "执行命令: $run_cmd"
    
    # 运行测试并捕获输出
    if $run_cmd; then
        log_success "基准测试执行完成"
    else
        local exit_code=$?
        log_error "基准测试执行失败，退出码: $exit_code"
        cd ..
        return $exit_code
    fi
    
    cd ..
}

# 生成报告
generate_reports() {
    log_info "生成基准测试报告..."
    
    local build_dir="build"
    local json_report="$build_dir/ci-benchmark-report.json"
    local xml_report="$build_dir/ci-benchmark-results.xml"
    
    # 检查JSON报告是否存在
    if [ ! -f "$json_report" ]; then
        log_error "基准测试JSON报告不存在: $json_report"
        return 1
    fi
    
    # 显示JSON报告摘要
    log_info "基准测试结果摘要:"
    if command -v jq &> /dev/null; then
        echo "----------------------------------------"
        jq -r '.results[] | "- \(.testName): \(if .passed then "✅ PASS" else "❌ FAIL" end) (平均: \(.avgTime)μs, 最大: \(.maxTime)μs, 抖动: \(.jitter)μs)"' "$json_report"
        echo "----------------------------------------"
        
        # 统计信息
        local total_tests=$(jq '.results | length' "$json_report")
        local passed_tests=$(jq '[.results[] | select(.passed == true)] | length' "$json_report")
        local failed_tests=$(jq '[.results[] | select(.passed == false)] | length' "$json_report")
        
        log_info "总测试数: $total_tests"
        log_info "通过测试: $passed_tests"
        log_info "失败测试: $failed_tests"
        
        if [ "$failed_tests" -gt 0 ]; then
            log_error "有 $failed_tests 个测试失败"
            log_info "失败的测试:"
            jq -r '.results[] | select(.passed == false) | "  - \(.testName): \(.failReason)"' "$json_report"
        else
            log_success "所有基准测试通过"
        fi
    else
        log_warning "未安装jq，无法解析JSON报告"
        cat "$json_report"
    fi
    
    # 生成HTML报告
    local html_report="$build_dir/ci-benchmark-report.html"
    if python3 scripts/generate-benchmark-report.py "$json_report" -o "$html_report"; then
        log_success "HTML报告已生成: $html_report"
    else
        log_warning "HTML报告生成失败"
    fi
    
    # 复制报告到根目录
    cp "$json_report" . 2>/dev/null || true
    cp "$html_report" . 2>/dev/null || true
    cp "$xml_report" . 2>/dev/null || true
    
    log_info "报告文件已复制到根目录"
}

# 性能回归检测
check_regression() {
    log_info "检查性能回归..."
    
    local current_report="ci-benchmark-report.json"
    local baseline_report="baseline-benchmark.json"
    
    if [ ! -f "$current_report" ]; then
        log_warning "当前基准报告不存在，跳过回归检测"
        return 0
    fi
    
    if [ ! -f "$baseline_report" ]; then
        log_warning "基线基准报告不存在，跳过回归检测"
        log_info "将当前报告保存为基线"
        cp "$current_report" "$baseline_report"
        return 0
    fi
    
    if python3 scripts/check-performance-regression.py "$baseline_report" "$current_report" --threshold 10.0; then
        log_success "性能回归检测通过"
        return 0
    else
        log_error "检测到性能回归"
        return 1
    fi
}

# 清理函数
cleanup() {
    log_info "清理临时文件..."
    # 这里可以添加清理逻辑
}

# 主函数
main() {
    log_info "开始CI基准门禁测试..."
    log_info "时间: $(date)"
    log_info "系统: $(uname -a)"
    
    # 设置错误处理
    trap cleanup EXIT
    
    # 解析命令行参数
    local skip_build=false
    local skip_realtime=false
    local help=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --skip-build)
                skip_build=true
                shift
                ;;
            --skip-realtime)
                skip_realtime=true
                shift
                ;;
            --help|-h)
                help=true
                shift
                ;;
            *)
                log_error "未知参数: $1"
                help=true
                shift
                ;;
        esac
    done
    
    if [ "$help" = true ]; then
        echo "用法: $0 [选项]"
        echo "选项:"
        echo "  --skip-build     跳过项目构建"
        echo "  --skip-realtime  跳过实时环境配置"
        echo "  --help, -h       显示帮助信息"
        exit 0
    fi
    
    # 执行步骤
    check_dependencies
    
    if [ "$skip_realtime" != true ]; then
        setup_realtime_env
    fi
    
    if [ "$skip_build" != true ]; then
        build_project
    fi
    
    run_benchmarks
    local benchmark_result=$?
    
    generate_reports
    
    if [ $benchmark_result -eq 0 ]; then
        check_regression
        local regression_result=$?
        
        if [ $regression_result -eq 0 ]; then
            log_success "CI基准门禁测试完成 - 全部通过"
            exit 0
        else
            log_error "CI基准门禁测试完成 - 性能回归检测失败"
            exit 1
        fi
    else
        log_error "CI基准门禁测试完成 - 基准测试失败"
        exit 1
    fi
}

# 运行主函数
main "$@"