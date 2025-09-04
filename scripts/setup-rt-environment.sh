#!/bin/bash

# PLC运行时系统 - Linux RT-PREEMPT环境配置脚本
# 基于ADR-001决策：Linux RT-PREEMPT作为硬实时基线

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

# 检查是否为root用户
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_error "请不要以root用户运行此脚本"
        exit 1
    fi
}

# 检查系统要求
check_system_requirements() {
    log_info "检查系统要求..."
    
    # 检查操作系统
    if [[ ! -f /etc/os-release ]]; then
        log_error "无法检测操作系统版本"
        exit 1
    fi
    
    source /etc/os-release
    log_info "检测到操作系统: $PRETTY_NAME"
    
    # 检查内核版本
    KERNEL_VERSION=$(uname -r)
    log_info "当前内核版本: $KERNEL_VERSION"
    
    # 检查是否已安装RT内核
    if [[ $KERNEL_VERSION == *"rt"* ]]; then
        log_success "检测到RT-PREEMPT内核已安装"
        return 0
    else
        log_warning "未检测到RT-PREEMPT内核，需要安装"
        return 1
    fi
}

# 安装RT-PREEMPT内核 (Ubuntu/Debian)
install_rt_kernel_ubuntu() {
    log_info "安装RT-PREEMPT内核 (Ubuntu/Debian)..."
    
    # 更新包列表
    sudo apt update
    
    # 安装RT内核
    sudo apt install -y linux-image-rt-amd64 linux-headers-rt-amd64
    
    # 更新GRUB
    sudo update-grub
    
    log_success "RT-PREEMPT内核安装完成"
    log_warning "请重启系统并选择RT内核启动"
}

# 安装RT-PREEMPT内核 (CentOS/RHEL)
install_rt_kernel_centos() {
    log_info "安装RT-PREEMPT内核 (CentOS/RHEL)..."
    
    # 启用RT仓库
    sudo yum install -y kernel-rt kernel-rt-devel
    
    # 更新GRUB
    sudo grub2-mkconfig -o /boot/grub2/grub.cfg
    
    log_success "RT-PREEMPT内核安装完成"
    log_warning "请重启系统并选择RT内核启动"
}

# 配置实时性能参数
configure_rt_parameters() {
    log_info "配置实时性能参数..."
    
    # 创建实时配置文件
    sudo tee /etc/security/limits.d/99-realtime.conf > /dev/null <<EOF
# 实时系统用户限制配置
@realtime soft rtprio 99
@realtime soft priority 99
@realtime soft memlock unlimited
@realtime hard rtprio 99
@realtime hard priority 99
@realtime hard memlock unlimited
EOF
    
    # 将当前用户添加到realtime组
    sudo groupadd -f realtime
    sudo usermod -a -G realtime $USER
    
    # 配置内核参数
    sudo tee /etc/sysctl.d/99-realtime.conf > /dev/null <<EOF
# 实时系统内核参数
kernel.sched_rt_runtime_us = -1
kernel.sched_rt_period_us = 1000000
vm.swappiness = 1
kernel.hung_task_timeout_secs = 0
EOF
    
    # 应用内核参数
    sudo sysctl -p /etc/sysctl.d/99-realtime.conf
    
    log_success "实时性能参数配置完成"
}

# 安装开发工具链
install_development_tools() {
    log_info "安装开发工具链..."
    
    if command -v apt &> /dev/null; then
        # Ubuntu/Debian
        sudo apt install -y \
            build-essential \
            cmake \
            ninja-build \
            clang \
            clang-tools \
            gdb \
            valgrind \
            git \
            pkg-config \
            libc6-dev \
            linux-headers-$(uname -r)
    elif command -v yum &> /dev/null; then
        # CentOS/RHEL
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y \
            cmake \
            ninja-build \
            clang \
            clang-tools-extra \
            gdb \
            valgrind \
            git \
            pkgconfig \
            kernel-headers
    else
        log_error "不支持的包管理器"
        exit 1
    fi
    
    log_success "开发工具链安装完成"
}

# 安装实时测试工具
install_rt_test_tools() {
    log_info "安装实时测试工具..."
    
    # 安装rt-tests包
    if command -v apt &> /dev/null; then
        sudo apt install -y rt-tests
    elif command -v yum &> /dev/null; then
        sudo yum install -y rt-tests
    fi
    
    # 编译安装cyclictest (如果包不可用)
    if ! command -v cyclictest &> /dev/null; then
        log_info "从源码编译rt-tests..."
        
        # 创建临时目录
        TEMP_DIR=$(mktemp -d)
        cd $TEMP_DIR
        
        # 下载并编译rt-tests
        git clone https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
        cd rt-tests
        make all
        sudo make install
        
        # 清理临时目录
        cd /
        rm -rf $TEMP_DIR
    fi
    
    log_success "实时测试工具安装完成"
}

# 创建性能测试脚本
create_performance_test_scripts() {
    log_info "创建性能测试脚本..."
    
    # 创建测试脚本目录
    mkdir -p ~/plc-runtime/tests/performance
    
    # 创建调度延迟测试脚本
    cat > ~/plc-runtime/tests/performance/test-scheduling-latency.sh <<'EOF'
#!/bin/bash

# 调度延迟测试脚本
# 目标: 调度延迟 < 50μs (99%情况)

echo "开始调度延迟测试..."
echo "目标: 99%的调度延迟 < 50μs"

# 运行cyclictest
cyclictest -t1 -p 99 -i 1000 -l 100000 -q

echo "测试完成"
EOF

    # 创建中断响应时间测试脚本
    cat > ~/plc-runtime/tests/performance/test-interrupt-latency.sh <<'EOF'
#!/bin/bash

# 中断响应时间测试脚本
# 目标: 中断响应时间 < 20μs

echo "开始中断响应时间测试..."
echo "目标: 中断响应时间 < 20μs"

# 运行hwlatdetect
if command -v hwlatdetect &> /dev/null; then
    hwlatdetect --duration=60
else
    echo "hwlatdetect未安装，跳过硬件延迟检测"
fi

echo "测试完成"
EOF

    # 创建系统负载测试脚本
    cat > ~/plc-runtime/tests/performance/test-system-load.sh <<'EOF'
#!/bin/bash

# 系统负载测试脚本
# 在系统负载下测试实时性能

echo "开始系统负载测试..."

# 启动负载生成器
stress-ng --cpu 4 --io 2 --vm 1 --vm-bytes 1G --timeout 60s &
STRESS_PID=$!

# 在负载下运行实时测试
cyclictest -t1 -p 99 -i 1000 -l 10000 -q

# 停止负载生成器
kill $STRESS_PID 2>/dev/null || true

echo "负载测试完成"
EOF

    # 设置执行权限
    chmod +x ~/plc-runtime/tests/performance/*.sh
    
    log_success "性能测试脚本创建完成"
}

# 创建Docker开发环境
create_docker_environment() {
    log_info "创建Docker开发环境..."
    
    # 创建Dockerfile
    cat > ~/plc-runtime/Dockerfile.rt-dev <<'EOF'
FROM ubuntu:22.04

# 安装基础包
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    clang \
    clang-tools \
    gdb \
    valgrind \
    git \
    pkg-config \
    rt-tests \
    stress-ng \
    && rm -rf /var/lib/apt/lists/*

# 创建开发用户
RUN useradd -m -s /bin/bash developer && \
    usermod -a -G realtime developer

# 配置实时权限
RUN echo "@realtime soft rtprio 99" >> /etc/security/limits.conf && \
    echo "@realtime soft priority 99" >> /etc/security/limits.conf && \
    echo "@realtime soft memlock unlimited" >> /etc/security/limits.conf && \
    echo "@realtime hard rtprio 99" >> /etc/security/limits.conf && \
    echo "@realtime hard priority 99" >> /etc/security/limits.conf && \
    echo "@realtime hard memlock unlimited" >> /etc/security/limits.conf

# 设置工作目录
WORKDIR /workspace

# 切换到开发用户
USER developer

CMD ["/bin/bash"]
EOF

    # 创建docker-compose.yml
    cat > ~/plc-runtime/docker-compose.yml <<'EOF'
version: '3.8'

services:
  rt-dev:
    build:
      context: .
      dockerfile: Dockerfile.rt-dev
    volumes:
      - .:/workspace
      - /dev:/dev
    privileged: true
    network_mode: host
    environment:
      - DISPLAY=${DISPLAY}
    stdin_open: true
    tty: true
EOF

    log_success "Docker开发环境配置完成"
}

# 验证安装
verify_installation() {
    log_info "验证安装..."
    
    # 检查RT内核
    if [[ $(uname -r) == *"rt"* ]]; then
        log_success "RT-PREEMPT内核运行中"
    else
        log_warning "当前未运行RT内核，请重启并选择RT内核"
    fi
    
    # 检查实时优先级权限
    if chrt -f 99 /bin/true 2>/dev/null; then
        log_success "实时优先级权限配置正确"
    else
        log_warning "实时优先级权限配置可能有问题，请重新登录"
    fi
    
    # 检查开发工具
    local tools=("gcc" "g++" "cmake" "ninja" "clang" "gdb" "cyclictest")
    for tool in "${tools[@]}"; do
        if command -v $tool &> /dev/null; then
            log_success "$tool 已安装"
        else
            log_warning "$tool 未找到"
        fi
    done
    
    # 运行快速性能测试
    log_info "运行快速性能测试..."
    if command -v cyclictest &> /dev/null; then
        echo "运行5秒调度延迟测试..."
        cyclictest -t1 -p 99 -i 1000 -l 5000 -q
    fi
}

# 主函数
main() {
    log_info "开始配置Linux RT-PREEMPT环境..."
    
    check_root
    
    # 检查系统要求
    if ! check_system_requirements; then
        # 根据系统类型安装RT内核
        source /etc/os-release
        case $ID in
            ubuntu|debian)
                install_rt_kernel_ubuntu
                ;;
            centos|rhel|fedora)
                install_rt_kernel_centos
                ;;
            *)
                log_error "不支持的操作系统: $ID"
                exit 1
                ;;
        esac
        
        log_warning "RT内核安装完成，请重启系统后重新运行此脚本"
        exit 0
    fi
    
    # 配置实时参数
    configure_rt_parameters
    
    # 安装开发工具
    install_development_tools
    
    # 安装测试工具
    install_rt_test_tools
    
    # 创建测试脚本
    create_performance_test_scripts
    
    # 创建Docker环境
    create_docker_environment
    
    # 验证安装
    verify_installation
    
    log_success "Linux RT-PREEMPT环境配置完成！"
    log_info "下一步："
    log_info "1. 如果刚安装RT内核，请重启系统"
    log_info "2. 重新登录以获得实时权限"
    log_info "3. 运行性能测试验证环境"
    log_info "4. 开始PLC运行时系统开发"
}

# 执行主函数
main "$@"
"