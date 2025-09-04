# PLC运行时核心系统 CI基准测试运行脚本 (Windows PowerShell版本)
# 用于Windows开发环境中的基准测试

param(
    [switch]$SkipBuild,
    [switch]$SkipOptimization,
    [switch]$Help,
    [string]$BuildType = "Release"
)

# 颜色输出函数
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Blue
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

# 显示帮助信息
function Show-Help {
    Write-Host "PLC运行时核心系统 CI基准测试运行脚本"
    Write-Host ""
    Write-Host "用法: .\run-ci-benchmarks.ps1 [参数]"
    Write-Host ""
    Write-Host "参数:"
    Write-Host "  -SkipBuild         跳过项目构建"
    Write-Host "  -SkipOptimization  跳过Windows性能优化"
    Write-Host "  -BuildType         构建类型 (Release/Debug，默认Release)"
    Write-Host "  -Help              显示帮助信息"
    Write-Host ""
    Write-Host "示例:"
    Write-Host "  .\run-ci-benchmarks.ps1                    # 完整运行"
    Write-Host "  .\run-ci-benchmarks.ps1 -SkipBuild        # 跳过构建"
    Write-Host "  .\run-ci-benchmarks.ps1 -BuildType Debug  # Debug构建"
}

# 检查依赖
function Test-Dependencies {
    Write-Info "检查依赖..."
    
    $missingDeps = @()
    
    # 检查CMake
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        $missingDeps += "cmake"
    }
    
    # 检查Python
    if (-not (Get-Command python -ErrorAction SilentlyContinue) -and 
        -not (Get-Command python3 -ErrorAction SilentlyContinue)) {
        $missingDeps += "python"
    }
    
    # 检查编译器
    $hasCompiler = $false
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        $hasCompiler = $true
        Write-Info "检测到 MSVC 编译器"
    } elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
        $hasCompiler = $true
        Write-Info "检测到 GCC 编译器"
    } elseif (Get-Command clang -ErrorAction SilentlyContinue) {
        $hasCompiler = $true
        Write-Info "检测到 Clang 编译器"
    }
    
    if (-not $hasCompiler) {
        $missingDeps += "编译器 (MSVC/GCC/Clang)"
    }
    
    if ($missingDeps.Count -gt 0) {
        Write-Error "缺少依赖: $($missingDeps -join ', ')"
        Write-Info "请安装缺少的依赖后重试"
        Write-Info "建议使用 Visual Studio Installer 或 vcpkg 安装依赖"
        exit 1
    }
    
    Write-Success "依赖检查通过"
}

# 配置Windows性能优化
function Set-WindowsOptimization {
    Write-Info "配置Windows性能优化..."
    
    try {
        # 检查是否有管理员权限
        $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")
        
        if ($isAdmin) {
            Write-Info "检测到管理员权限，应用性能优化..."
            
            # 设置电源计划为高性能
            try {
                powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
                Write-Info "电源计划已设置为高性能模式"
            } catch {
                Write-Warning "无法设置电源计划: $($_.Exception.Message)"
            }
            
            # 设置进程优先级类
            try {
                $currentProcess = Get-Process -Id $PID
                $currentProcess.PriorityClass = "High"
                Write-Info "进程优先级已设置为高"
            } catch {
                Write-Warning "无法设置进程优先级: $($_.Exception.Message)"
            }
            
        } else {
            Write-Warning "无管理员权限，跳过系统级优化"
            Write-Info "建议以管理员身份运行以获得最佳性能"
        }
        
        # 设置.NET垃圾回收为服务器模式
        $env:COMPlus_gcServer = "1"
        $env:COMPlus_gcConcurrent = "1"
        
        Write-Success "Windows性能优化配置完成"
        
    } catch {
        Write-Warning "性能优化配置失败: $($_.Exception.Message)"
    }
}

# 构建项目
function Build-Project {
    Write-Info "构建项目..."
    
    $buildDir = "build"
    
    # 创建构建目录
    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }
    
    Push-Location $buildDir
    
    try {
        # 配置CMake
        $cmakeArgs = @(
            "-DCMAKE_BUILD_TYPE=$BuildType",
            "-DENABLE_TESTING=ON",
            "-DENABLE_BENCHMARKS=ON"
        )
        
        # 检测生成器
        if (Get-Command ninja -ErrorAction SilentlyContinue) {
            $cmakeArgs += "-G", "Ninja"
            $buildCmd = "ninja"
        } elseif (Get-Command cl -ErrorAction SilentlyContinue) {
            # 使用Visual Studio生成器
            $buildCmd = "cmake --build . --config $BuildType --parallel"
        } else {
            # 使用MinGW Makefiles
            $cmakeArgs += "-G", "MinGW Makefiles"
            $buildCmd = "mingw32-make -j$([Environment]::ProcessorCount)"
        }
        
        Write-Info "CMake配置: $($cmakeArgs -join ' ')"
        & cmake $cmakeArgs ".."
        
        if ($LASTEXITCODE -ne 0) {
            throw "CMake配置失败"
        }
        
        # 构建
        Write-Info "开始构建..."
        Write-Info "构建命令: $buildCmd"
        
        if ($buildCmd.StartsWith("cmake")) {
            Invoke-Expression $buildCmd
        } else {
            & $buildCmd.Split()[0] $buildCmd.Split()[1..100]
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "项目构建失败"
        }
        
        Write-Success "项目构建完成"
        
    } finally {
        Pop-Location
    }
}

# 运行基准测试
function Start-BenchmarkTests {
    Write-Info "运行CI基准门禁测试..."
    
    $buildDir = "build"
    
    # 查找基准测试可执行文件
    $benchmarkExe = $null
    $possiblePaths = @(
        "$buildDir\tests\ci\$BuildType\ci-benchmark-gates.exe",
        "$buildDir\tests\ci\ci-benchmark-gates.exe",
        "$buildDir\tests\ci\ci-benchmark-gates"
    )
    
    foreach ($path in $possiblePaths) {
        if (Test-Path $path) {
            $benchmarkExe = $path
            break
        }
    }
    
    if (-not $benchmarkExe) {
        Write-Error "基准测试可执行文件未找到"
        Write-Info "查找路径: $($possiblePaths -join ', ')"
        exit 1
    }
    
    Write-Info "找到基准测试可执行文件: $benchmarkExe"
    
    # 切换到构建目录
    Push-Location $buildDir
    
    try {
        # 设置环境变量
        $env:GTEST_OUTPUT = "xml:ci-benchmark-results.xml"
        
        # 运行基准测试
        Write-Info "执行基准测试..."
        
        # 尝试设置高优先级
        try {
            $process = Start-Process -FilePath $benchmarkExe -Wait -PassThru -NoNewWindow
            
            # 设置进程优先级
            try {
                $process.PriorityClass = "High"
                Write-Info "基准测试进程优先级已设置为高"
            } catch {
                Write-Warning "无法设置基准测试进程优先级"
            }
            
            $exitCode = $process.ExitCode
            
        } catch {
            Write-Warning "无法以高优先级运行，使用普通优先级"
            & $benchmarkExe
            $exitCode = $LASTEXITCODE
        }
        
        if ($exitCode -eq 0) {
            Write-Success "基准测试执行完成"
        } else {
            Write-Error "基准测试执行失败，退出码: $exitCode"
            return $exitCode
        }
        
    } finally {
        Pop-Location
    }
    
    return 0
}

# 生成报告
function New-Reports {
    Write-Info "生成基准测试报告..."
    
    $buildDir = "build"
    $jsonReport = "$buildDir\ci-benchmark-report.json"
    $xmlReport = "$buildDir\ci-benchmark-results.xml"
    
    # 检查JSON报告是否存在
    if (-not (Test-Path $jsonReport)) {
        Write-Error "基准测试JSON报告不存在: $jsonReport"
        return $false
    }
    
    # 显示JSON报告摘要
    Write-Info "基准测试结果摘要:"
    
    try {
        $reportData = Get-Content $jsonReport | ConvertFrom-Json
        
        Write-Host "----------------------------------------"
        foreach ($result in $reportData.results) {
            $status = if ($result.passed) { "✅ PASS" } else { "❌ FAIL" }
            Write-Host "- $($result.testName): $status (平均: $([math]::Round($result.avgTime, 2))μs, 最大: $([math]::Round($result.maxTime, 2))μs, 抖动: $([math]::Round($result.jitter, 2))μs)"
        }
        Write-Host "----------------------------------------"
        
        # 统计信息
        $totalTests = $reportData.results.Count
        $passedTests = ($reportData.results | Where-Object { $_.passed }).Count
        $failedTests = $totalTests - $passedTests
        
        Write-Info "总测试数: $totalTests"
        Write-Info "通过测试: $passedTests"
        Write-Info "失败测试: $failedTests"
        
        if ($failedTests -gt 0) {
            Write-Error "有 $failedTests 个测试失败"
            Write-Info "失败的测试:"
            foreach ($result in $reportData.results | Where-Object { -not $_.passed }) {
                Write-Host "  - $($result.testName): $($result.failReason)"
            }
        } else {
            Write-Success "所有基准测试通过"
        }
        
    } catch {
        Write-Warning "无法解析JSON报告: $($_.Exception.Message)"
        Get-Content $jsonReport
    }
    
    # 生成HTML报告
    $htmlReport = "$buildDir\ci-benchmark-report.html"
    $pythonCmd = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { "python" }
    
    try {
        & $pythonCmd "scripts\generate-benchmark-report.py" $jsonReport -o $htmlReport
        Write-Success "HTML报告已生成: $htmlReport"
    } catch {
        Write-Warning "HTML报告生成失败: $($_.Exception.Message)"
    }
    
    # 复制报告到根目录
    try {
        Copy-Item $jsonReport . -ErrorAction SilentlyContinue
        Copy-Item $htmlReport . -ErrorAction SilentlyContinue
        Copy-Item $xmlReport . -ErrorAction SilentlyContinue
        Write-Info "报告文件已复制到根目录"
    } catch {
        Write-Warning "复制报告文件失败: $($_.Exception.Message)"
    }
    
    return $true
}

# 性能回归检测
function Test-PerformanceRegression {
    Write-Info "检查性能回归..."
    
    $currentReport = "ci-benchmark-report.json"
    $baselineReport = "baseline-benchmark.json"
    
    if (-not (Test-Path $currentReport)) {
        Write-Warning "当前基准报告不存在，跳过回归检测"
        return $true
    }
    
    if (-not (Test-Path $baselineReport)) {
        Write-Warning "基线基准报告不存在，跳过回归检测"
        Write-Info "将当前报告保存为基线"
        Copy-Item $currentReport $baselineReport
        return $true
    }
    
    $pythonCmd = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { "python" }
    
    try {
        & $pythonCmd "scripts\check-performance-regression.py" $baselineReport $currentReport --threshold 10.0
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "性能回归检测通过"
            return $true
        } else {
            Write-Error "检测到性能回归"
            return $false
        }
    } catch {
        Write-Warning "性能回归检测失败: $($_.Exception.Message)"
        return $true
    }
}

# 主函数
function Main {
    if ($Help) {
        Show-Help
        return
    }
    
    Write-Info "开始CI基准门禁测试..."
    Write-Info "时间: $(Get-Date)"
    Write-Info "系统: $($env:OS) $($env:PROCESSOR_ARCHITECTURE)"
    Write-Info "PowerShell版本: $($PSVersionTable.PSVersion)"
    
    try {
        # 执行步骤
        Test-Dependencies
        
        if (-not $SkipOptimization) {
            Set-WindowsOptimization
        }
        
        if (-not $SkipBuild) {
            Build-Project
        }
        
        $benchmarkResult = Start-BenchmarkTests
        
        if ($benchmarkResult -eq 0) {
            $reportResult = New-Reports
            
            if ($reportResult) {
                $regressionResult = Test-PerformanceRegression
                
                if ($regressionResult) {
                    Write-Success "CI基准门禁测试完成 - 全部通过"
                    exit 0
                } else {
                    Write-Error "CI基准门禁测试完成 - 性能回归检测失败"
                    exit 1
                }
            } else {
                Write-Error "CI基准门禁测试完成 - 报告生成失败"
                exit 1
            }
        } else {
            Write-Error "CI基准门禁测试完成 - 基准测试失败"
            exit $benchmarkResult
        }
        
    } catch {
        Write-Error "CI基准门禁测试失败: $($_.Exception.Message)"
        Write-Error $_.ScriptStackTrace
        exit 1
    }
}

# 运行主函数
Main