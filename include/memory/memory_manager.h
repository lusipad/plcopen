/**
 * @file memory_manager.h
 * @brief 内存管理器接口
 * @version MVP-1.0
 */

#pragma once

#include <cstddef>
#include <memory>

namespace plc_runtime {
namespace memory {

/**
 * @brief 内存管理器类
 */
class MemoryManager {
public:
    static MemoryManager& getInstance();
    
    void initialize();
    void* allocate(size_t size);
    void deallocate(void* ptr);
    
private:
    MemoryManager() = default;
    ~MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
};

/**
 * @brief 获取全局内存管理器实例
 */
inline MemoryManager* GetMemoryManager() {
    return &MemoryManager::getInstance();
}

} // namespace memory
} // namespace plc_runtime