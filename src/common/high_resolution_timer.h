#ifndef HIGH_RESOLUTION_TIMER_H
#define HIGH_RESOLUTION_TIMER_H

#include <cstdint>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#elif defined(__linux__)
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#endif

namespace plc_runtime {
namespace common {

class HighResolutionTimer {
private:
    static constexpr uint64_t NS_PER_SEC = 1000000000ULL;
    static constexpr uint64_t US_PER_SEC = 1000000ULL;
    static constexpr uint64_t NS_PER_US = 1000ULL;
    
#ifdef _WIN32
    LARGE_INTEGER frequency_;
    bool use_qpc_;
#endif
    
public:
    HighResolutionTimer() {
#ifdef _WIN32
        use_qpc_ = QueryPerformanceFrequency(&frequency_) != 0;
        timeBeginPeriod(1);
#endif
    }
    
    ~HighResolutionTimer() {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
    
    uint64_t get_current_time_ns() const {
#ifdef _WIN32
        if (use_qpc_) {
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            return static_cast<uint64_t>(
                (counter.QuadPart * NS_PER_SEC) / frequency_.QuadPart
            );
        } else {
            return GetTickCount64() * 1000000ULL;
        }
#elif defined(__linux__)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * NS_PER_SEC + 
               static_cast<uint64_t>(ts.tv_nsec);
#else
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
#endif
    }
    
    uint64_t get_current_time_us() const {
        return get_current_time_ns() / NS_PER_US;
    }
    
    uint64_t get_current_time_ms() const {
        return get_current_time_ns() / 1000000ULL;
    }
    
    void precise_sleep_ns(uint64_t sleep_time_ns) const {
        if (sleep_time_ns == 0) {
            return;
        }
        
        uint64_t start_time = get_current_time_ns();
        uint64_t target_time = start_time + sleep_time_ns;
        
        if (sleep_time_ns > 10000) {
            uint64_t system_sleep_time = sleep_time_ns - 5000;
            
#ifdef _WIN32
            DWORD sleep_ms = static_cast<DWORD>(system_sleep_time / 1000000);
            if (sleep_ms > 0) {
                Sleep(sleep_ms);
            }
#elif defined(__linux__)
            struct timespec req;
            req.tv_sec = system_sleep_time / NS_PER_SEC;
            req.tv_nsec = system_sleep_time % NS_PER_SEC;
            nanosleep(&req, nullptr);
#else
            std::this_thread::sleep_for(
                std::chrono::nanoseconds(system_sleep_time)
            );
#endif
        }
        
        while (get_current_time_ns() < target_time) {
#ifdef _WIN32
            YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#else
            std::this_thread::yield();
#endif
        }
    }
    
    void precise_sleep_us(uint64_t sleep_time_us) const {
        precise_sleep_ns(sleep_time_us * NS_PER_US);
    }
    
    void precise_sleep_ms(uint64_t sleep_time_ms) const {
        precise_sleep_ns(sleep_time_ms * 1000000ULL);
    }
};

class ScopedTimer {
private:
    const HighResolutionTimer& timer_ref;
    uint64_t start_time;
    uint64_t* result_ptr;
    
public:
    ScopedTimer(const HighResolutionTimer& timer, uint64_t* result)
        : timer_ref(timer), start_time(timer.get_current_time_ns()), result_ptr(result) {
    }
    
    ~ScopedTimer() {
        if (result_ptr) {
            *result_ptr = timer_ref.get_current_time_ns() - start_time;
        }
    }
};

#define MEASURE_TIME_NS(timer, result) \
    plc_runtime::common::ScopedTimer _scoped_timer(timer, &result)

} // namespace common
} // namespace plc_runtime

#endif // HIGH_RESOLUTION_TIMER_H