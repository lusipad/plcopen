/**
 * @file MemoryManager.h
 * @brief 内存管理器接口定义
 * @version 1.0
 * @date 2025-01-15
 */

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <cstddef>
#include <memory>

namespace plc_runtime {
namespace memory {

/**
 * @brief 内存管理器类
 * 提供统一的内存分配和释放接口
 */
class MemoryManager {
public:
    /**
     * @brief 获取内存管理器单例实例
     * @return 内存管理器实例引用
     */
    static MemoryManager& getInstance();
    
    /**
     * @brief 初始化内存管理器
     */
    void initialize();
    
    /**
     * @brief 分配内存
     * @param size 要分配的字节数
     * @return 分配的内存指针，失败返回nullptr
     */
    void* allocate(size_t size);
    
    /**
     * @brief 释放内存
     * @param ptr 要释放的内存指针
     */
    void deallocate(void* ptr);
    
    /**
     * @brief 获取已分配内存总量
     * @return 已分配的字节数
     */
    size_t getAllocatedSize() const { return allocated_size_; }
    
    /**
     * @brief 获取分配次数
     * @return 分配次数
     */
    size_t getAllocationCount() const { return allocation_count_; }

private:
    MemoryManager() = default;
    ~MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    
    size_t allocated_size_ = 0;
    size_t allocation_count_ = 0;
};

} // namespace memory
} // namespace plc_runtime

#endif // MEMORY_MANAGER_H