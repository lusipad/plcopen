/**
 * @file FbPLCOpenBase.cpp
 * @brief PLCOpen 标准功能块实现
 */

#include "function_block/FbPLCOpenBase.h"
#include <chrono>

namespace Uranus {

// ============================================================================
// FB_Timer 实现
// ============================================================================

void FB_Timer::call() {
    // 检测输入上升沿
    if (IN && !prevIN) {
        // 开始计时
        timerRunning = true;
        startTime = getCurrentTimeNs();
        ET = 0;
        Q = false;
    }
    // 检测输入下降沿
    else if (!IN && prevIN) {
        // 停止计时
        timerRunning = false;
        ET = 0;
        Q = false;
    }
    
    // 如果正在计时
    if (timerRunning && IN) {
        uint64_t currentTime = getCurrentTimeNs();
        ET = currentTime - startTime;
        
        // 检查是否到达预设时间
        if (ET >= PT) {
            Q = true;
            ET = PT;  // 限制在预设时间
        }
    }
    
    prevIN = IN;
}

void FB_Timer::reset() {
    FbPLCOpenBase::reset();
    IN = false;
    PT = 0;
    Q = false;
    ET = 0;
    prevIN = false;
    startTime = 0;
    timerRunning = false;
}

uint64_t FB_Timer::getCurrentTimeNs() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

// ============================================================================
// FB_Counter 实现
// ============================================================================

void FB_Counter::call() {
    // 检测复位上升沿
    if (RESET && !prevRESET) {
        CV = 0;
        Q = false;
    }
    // 检测计数上升沿
    else if (CU && !prevCU && !RESET) {
        if (CV < UINT32_MAX) {
            CV++;
        }
        
        // 检查是否达到预设值
        if (CV >= PV) {
            Q = true;
        }
    }
    
    prevCU = CU;
    prevRESET = RESET;
}

void FB_Counter::reset() {
    FbPLCOpenBase::reset();
    CU = false;
    RESET = false;
    PV = 0;
    Q = false;
    CV = 0;
    prevCU = false;
    prevRESET = false;
}

// ============================================================================
// FB_RisingEdge 实现
// ============================================================================

void FB_RisingEdge::call() {
    // 检测上升沿
    Q = CLK && !prevCLK;
    prevCLK = CLK;
}

void FB_RisingEdge::reset() {
    FbPLCOpenBase::reset();
    CLK = false;
    Q = false;
    prevCLK = false;
}

// ============================================================================
// FB_FallingEdge 实现
// ============================================================================

void FB_FallingEdge::call() {
    // 检测下降沿
    Q = !CLK && prevCLK;
    prevCLK = CLK;
}

void FB_FallingEdge::reset() {
    FbPLCOpenBase::reset();
    CLK = false;
    Q = false;
    prevCLK = false;
}

// ============================================================================
// FB_SetReset 实现
// ============================================================================

void FB_SetReset::call() {
    // 复位优先
    if (RESET) {
        Q1 = false;
    } else if (SET1) {
        Q1 = true;
    }
    // 如果都为 false，保持当前状态
}

void FB_SetReset::reset() {
    FbPLCOpenBase::reset();
    SET1 = false;
    RESET = false;
    Q1 = false;
}

// ============================================================================
// FB_ResetSet 实现
// ============================================================================

void FB_ResetSet::call() {
    // 置位优先
    if (SET) {
        Q1 = true;
    } else if (RESET1) {
        Q1 = false;
    }
    // 如果都为 false，保持当前状态
}

void FB_ResetSet::reset() {
    FbPLCOpenBase::reset();
    SET = false;
    RESET1 = false;
    Q1 = false;
}

// ============================================================================
// FB_Semaphore 实现
// ============================================================================

void FB_Semaphore::call() {
    // 检测申请上升沿
    if (CLAIM && !prevCLAIM && !acquired) {
        acquired = true;
        BUSY = true;
    }
    // 检测释放上升沿
    else if (RELEASE && !prevRELEASE && acquired) {
        acquired = false;
        BUSY = false;
    }
    
    prevCLAIM = CLAIM;
    prevRELEASE = RELEASE;
}

void FB_Semaphore::reset() {
    FbPLCOpenBase::reset();
    CLAIM = false;
    RELEASE = false;
    BUSY = false;
    prevCLAIM = false;
    prevRELEASE = false;
    acquired = false;
}

// ============================================================================
// FB_TimerOff 实现
// ============================================================================

void FB_TimerOff::call() {
    // 检测输入下降沿
    if (!IN && prevIN) {
        // 开始计时
        timerRunning = true;
        startTime = getCurrentTimeNs();
        ET = 0;
        // 输出立即变为 false
        Q = false;
    }
    // 检测输入上升沿
    else if (IN && !prevIN) {
        // 停止计时
        timerRunning = false;
        ET = 0;
        // 输出立即变为 true
        Q = true;
    }
    
    // 如果正在计时（输入为 false）
    if (timerRunning && !IN) {
        uint64_t currentTime = getCurrentTimeNs();
        ET = currentTime - startTime;
        
        // 检查是否到达预设时间
        if (ET >= PT) {
            Q = false;
            ET = PT;  // 限制在预设时间
            timerRunning = false;
        } else {
            Q = true;  // 延时期间输出为 true
        }
    }
    // 如果输入为 true，输出立即为 true
    else if (IN) {
        Q = true;
        ET = 0;
    }
    
    prevIN = IN;
}

void FB_TimerOff::reset() {
    FbPLCOpenBase::reset();
    IN = false;
    PT = 0;
    Q = false;
    ET = 0;
    prevIN = false;
    startTime = 0;
    timerRunning = false;
}

uint64_t FB_TimerOff::getCurrentTimeNs() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

// ============================================================================
// FB_TimerPulse 实现
// ============================================================================

void FB_TimerPulse::call() {
    // 检测输入上升沿
    if (IN && !prevIN) {
        // 开始脉冲计时
        timerRunning = true;
        startTime = getCurrentTimeNs();
        ET = 0;
        Q = true;  // 立即输出脉冲
    }
    
    // 如果正在计时
    if (timerRunning) {
        uint64_t currentTime = getCurrentTimeNs();
        ET = currentTime - startTime;
        
        // 检查是否到达预设时间
        if (ET >= PT) {
            Q = false;
            ET = PT;  // 限制在预设时间
            timerRunning = false;
        }
    }
    
    prevIN = IN;
}

void FB_TimerPulse::reset() {
    FbPLCOpenBase::reset();
    IN = false;
    PT = 0;
    Q = false;
    ET = 0;
    prevIN = false;
    startTime = 0;
    timerRunning = false;
}

uint64_t FB_TimerPulse::getCurrentTimeNs() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

// ============================================================================
// FB_CounterDown 实现
// ============================================================================

void FB_CounterDown::call() {
    // 检测加载上升沿
    if (LOAD && !prevLOAD) {
        CV = PV;
        Q = (CV == 0);
    }
    // 检测递减计数上升沿
    else if (CD && !prevCD && !LOAD) {
        if (CV > 0) {
            CV--;
        }
        
        // 检查是否达到零
        Q = (CV == 0);
    }
    
    prevCD = CD;
    prevLOAD = LOAD;
}

void FB_CounterDown::reset() {
    FbPLCOpenBase::reset();
    CD = false;
    LOAD = false;
    PV = 0;
    Q = false;
    CV = 0;
    prevCD = false;
    prevLOAD = false;
}

// ============================================================================
// FB_CounterUpDown 实现
// ============================================================================

void FB_CounterUpDown::call() {
    // 检测复位上升沿
    if (RESET && !prevRESET) {
        CV = 0;
        QU = false;
        QD = true;  // CV = 0 时 QD 为 true
    }
    // 检测加载上升沿
    else if (LOAD && !prevLOAD) {
        CV = PV;
        QU = (CV >= PV);
        QD = (CV == 0);
    }
    // 检测递增计数上升沿
    else if (CU && !prevCU && !RESET && !LOAD) {
        if (CV < UINT32_MAX) {
            CV++;
        }
        
        // 更新输出
        QU = (CV >= PV);
        QD = (CV == 0);
    }
    // 检测递减计数上升沿
    else if (CD && !prevCD && !RESET && !LOAD) {
        if (CV > 0) {
            CV--;
        }
        
        // 更新输出
        QU = (CV >= PV);
        QD = (CV == 0);
    }
    
    prevCU = CU;
    prevCD = CD;
    prevRESET = RESET;
    prevLOAD = LOAD;
}

void FB_CounterUpDown::reset() {
    FbPLCOpenBase::reset();
    CU = false;
    CD = false;
    RESET = false;
    LOAD = false;
    PV = 0;
    QU = false;
    QD = false;
    CV = 0;
    prevCU = false;
    prevCD = false;
    prevRESET = false;
    prevLOAD = false;
}

} // namespace Uranus