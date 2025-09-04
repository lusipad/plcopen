/**
 * @file demo_main.cpp
 * @brief PLC运行时核心系统演示程序
 * 
 * 展示如何使用PLC运行时核心系统的基本功能
 */

#include "plc_runtime_core.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

using namespace plc_runtime;

// 全局变量用于信号处理
static PLCRuntimeCore* g_runtime = nullptr;
static volatile bool g_running = true;

/**
 * @brief 信号处理函数
 */
void signalHandler(int signal) {
    std::cout << "\n[INFO] 收到信号 " << signal << "，正在关闭系统..." << std::endl;
    g_running = false;
    
    if (g_runtime) {
        g_runtime->emergencyStop();
    }
}

/**
 * @brief 打印系统信息
 */
void printSystemInfo() {
    std::cout << "========================================" << std::endl;
    std::cout << "PLC运行时核心系统演示程序" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "版本: " << getVersionString() << std::endl;
    std::cout << "构建信息: " << getBuildInfo() << std::endl;
    std::cout << "实时环境: " << (isRealtimeEnvironment() ? "是" : "否") << std::endl;
    std::cout << "========================================" << std::endl;
}

/**
 * @brief 打印性能统计
 */
void printPerformanceStats(const PerformanceStats& stats) {
    std::cout << "\n[性能统计]" << std::endl;
    std::cout << "调度延迟 - 平均: " << stats.averageSchedulingLatencyUs 
              << "μs, 最大: " << stats.maxSchedulingLatencyUs << "μs" << std::endl;
    std::cout << "截止时间错过次数: " << stats.deadlineMissCount << std::endl;
    
    std::cout << "I/O扫描时间 - 平均: " << stats.averageIOScanTimeUs 
              << "μs, 最大: " << stats.maxIOScanTimeUs << "μs" << std::endl;
    std::cout << "I/O错误次数: " << stats.ioErrorCount << std::endl;
    
    std::cout << "内存使用 - 当前: " << stats.totalMemoryUsed / 1024 / 1024 
              << "MB, 峰值: " << stats.peakMemoryUsed / 1024 / 1024 << "MB" << std::endl;
    std::cout << "内存泄漏次数: " << stats.memoryLeakCount << std::endl;
    
    std::cout << "通信统计 - 发送: " << stats.totalPacketsSent 
              << ", 接收: " << stats.totalPacketsReceived 
              << ", 错误: " << stats.communicationErrors << std::endl;
    
    auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(stats.uptime).count();
    std::cout << "系统运行时间: " << uptimeMs / 1000.0 << " 秒" << std::endl;
}

/**
 * @brief 运行基本功能演示
 */
void runBasicDemo(PLCRuntimeCore& runtime) {
    std::cout << "\n[INFO] 开始基本功能演示..." << std::endl;
    
    // 获取各个子系统
    auto scheduler = runtime.getScheduler();
    auto memoryManager = runtime.getMemoryManager();
    auto ioManager = runtime.getIOManager();
    auto commManager = runtime.getCommunicationManager();
    auto fbEngine = runtime.getFunctionBlockEngine();
    auto compiler = runtime.getSTCompiler();
    auto motionController = runtime.getMotionController();
    
    std::cout << "[INFO] 子系统状态:" << std::endl;
    std::cout << "  调度器: " << (scheduler ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  内存管理器: " << (memoryManager ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  I/O管理器: " << (ioManager ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  通信管理器: " << (commManager ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  功能块引擎: " << (fbEngine ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  ST编译器: " << (compiler ? "已初始化" : "未初始化") << std::endl;
    std::cout << "  运动控制器: " << (motionController ? "已初始化" : "未初始化") << std::endl;
}

/**
 * @brief 运行性能测试演示
 */
void runPerformanceDemo(PLCRuntimeCore& runtime) {
    std::cout << "\n[INFO] 开始性能测试演示..." << std::endl;
    
    const int TEST_CYCLES = 1000;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 运行多个扫描周期
    for (int i = 0; i < TEST_CYCLES && g_running; ++i) {
        auto result = runtime.runScanCycle();
        if (result != ErrorCode::SUCCESS) {
            std::cerr << "[ERROR] 扫描周期执行失败: " 
                      << errorCodeToString(result) << std::endl;
            break;
        }
        
        // 每100个周期打印一次进度
        if ((i + 1) % 100 == 0) {
            std::cout << "[INFO] 已完成 " << (i + 1) << "/" << TEST_CYCLES 
                      << " 个扫描周期" << std::endl;
        }
        
        // 模拟1ms周期
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                   (endTime - startTime).count();
    
    std::cout << "[INFO] 性能测试完成，耗时: " << duration << "ms" << std::endl;
    std::cout << "[INFO] 平均周期时间: " << static_cast<double>(duration) / TEST_CYCLES 
              << "ms" << std::endl;
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 打印系统信息
    printSystemInfo();
    
    // 创建系统配置
    SystemConfig config;
    
    // 根据命令行参数调整配置
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--high-performance") {
            config.schedulerCyclePeriodUs = 100;  // 100μs高性能模式
            config.ioScanPeriodUs = 50;           // 50μs I/O扫描
            std::cout << "[INFO] 启用高性能模式" << std::endl;
        } else if (arg == "--low-latency") {
            config.schedulerCyclePeriodUs = 500;  // 500μs低延迟模式
            config.ioScanPeriodUs = 100;          // 100μs I/O扫描
            std::cout << "[INFO] 启用低延迟模式" << std::endl;
        }
    }
    
    try {
        // 创建PLC运行时系统
        std::cout << "\n[INFO] 创建PLC运行时系统..." << std::endl;
        PLCRuntimeCore runtime(config);
        g_runtime = &runtime;
        
        // 初始化系统
        std::cout << "[INFO] 初始化系统..." << std::endl;
        auto result = runtime.initialize();
        if (result != ErrorCode::SUCCESS) {
            std::cerr << "[ERROR] 系统初始化失败: " 
                      << errorCodeToString(result) << std::endl;
            return 1;
        }
        
        // 启动系统
        std::cout << "[INFO] 启动系统..." << std::endl;
        result = runtime.start();
        if (result != ErrorCode::SUCCESS) {
            std::cerr << "[ERROR] 系统启动失败: " 
                      << errorCodeToString(result) << std::endl;
            return 1;
        }
        
        std::cout << "[SUCCESS] 系统启动成功！" << std::endl;
        
        // 运行演示
        runBasicDemo(runtime);
        
        if (g_running) {
            runPerformanceDemo(runtime);
        }
        
        // 主循环 - 监控系统状态
        std::cout << "\n[INFO] 进入主监控循环 (按Ctrl+C退出)..." << std::endl;
        
        auto lastStatsTime = std::chrono::steady_clock::now();
        const auto STATS_INTERVAL = std::chrono::seconds(10);
        
        while (g_running) {
            // 检查系统状态
            auto state = runtime.getState();
            if (state == SystemState::ERROR) {
                std::cerr << "[ERROR] 系统进入错误状态" << std::endl;
                break;
            }
            
            // 定期打印性能统计
            auto now = std::chrono::steady_clock::now();
            if (now - lastStatsTime >= STATS_INTERVAL) {
                auto stats = runtime.getPerformanceStats();
                printPerformanceStats(stats);
                lastStatsTime = now;
            }
            
            // 短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 停止系统
        std::cout << "\n[INFO] 停止系统..." << std::endl;
        result = runtime.stop();
        if (result != ErrorCode::SUCCESS) {
            std::cerr << "[WARNING] 系统停止时出现问题: " 
                      << errorCodeToString(result) << std::endl;
        }
        
        // 关闭系统
        std::cout << "[INFO] 关闭系统..." << std::endl;
        result = runtime.shutdown();
        if (result != ErrorCode::SUCCESS) {
            std::cerr << "[WARNING] 系统关闭时出现问题: " 
                      << errorCodeToString(result) << std::endl;
        }
        
        // 最终性能统计
        auto finalStats = runtime.getPerformanceStats();
        std::cout << "\n[最终性能统计]" << std::endl;
        printPerformanceStats(finalStats);
        
        std::cout << "\n[SUCCESS] 演示程序完成！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] 程序异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[EXCEPTION] 未知异常" << std::endl;
        return 1;
    }
    
    g_runtime = nullptr;
    return 0;
}
"