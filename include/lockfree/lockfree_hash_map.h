/**
 * @file lockfree_hash_map.h
 * @brief 无锁哈希表实现
 * 
 * 基于开放寻址和CAS操作的无锁哈希表
 * 适用于实时系统中的快速查找场景
 * 
 * 特性:
 * - 无锁并发访问
 * - 开放寻址避免内存分配
 * - 支持插入、查找、删除操作
 * - 自动扩容机制
 */

#ifndef LOCKFREE_LOCKFREE_HASH_MAP_H
#define LOCKFREE_LOCKFREE_HASH_MAP_H

#include "atomic_utils.h"
#include <atomic>
#include <memory>
#include <functional>
#include <type_traits>

namespace plc_runtime {
namespace lockfree {

/**
 * @brief 无锁哈希表
 * 
 * 使用开放寻址和线性探测的无锁哈希表实现
 * 
 * @tparam Key 键类型
 * @tparam Value 值类型
 * @tparam Hash 哈希函数类型
 */
template<typename Key, typename Value, typename Hash = std::hash<Key>>
class LockFreeHashMap {
private:
    static_assert(std::is_trivially_copyable_v<Key>, 
                  "Key must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<Value>, 
                  "Value must be trivially copyable");
    
    /**
     * @brief 哈希表条目状态
     */
    enum class EntryState : uint32_t {
        EMPTY = 0,      // 空条目
        OCCUPIED = 1,   // 已占用
        DELETED = 2     // 已删除 (墓碑)
    };
    
    /**
     * @brief 哈希表条目
     */
    struct Entry {
        std::atomic<EntryState> state{EntryState::EMPTY};
        Key key{};
        Value value{};
        
        Entry() = default;
    };
    
    // 哈希表参数
    size_t capacity_;
    size_t mask_;
    double maxLoadFactor_;
    Hash hasher_;
    
    // 条目数组
    std::unique_ptr<Entry[]> entries_;
    
    // 统计信息
    CacheAlignedAtomic<size_t> size_{0};
    CacheAlignedAtomic<size_t> deletedCount_{0};
    
public:
    /**
     * @brief 构造函数
     * @param initialCapacity 初始容量
     * @param maxLoadFactor 最大负载因子
     */
    explicit LockFreeHashMap(size_t initialCapacity = 1024, 
                            double maxLoadFactor = 0.75)
        : maxLoadFactor_(maxLoadFactor), hasher_() {
        
        capacity_ = nextPowerOfTwo(initialCapacity);
        mask_ = capacity_ - 1;
        entries_ = std::make_unique<Entry[]>(capacity_);
    }
    
    /**
     * @brief 析构函数
     */
    ~LockFreeHashMap() = default;
    
    // 禁用拷贝和移动
    LockFreeHashMap(const LockFreeHashMap&) = delete;
    LockFreeHashMap& operator=(const LockFreeHashMap&) = delete;
    LockFreeHashMap(LockFreeHashMap&&) = delete;
    LockFreeHashMap& operator=(LockFreeHashMap&&) = delete;
    
    /**
     * @brief 插入键值对
     * @param key 键
     * @param value 值
     * @return true 如果插入成功，false 如果键已存在
     */
    bool insert(const Key& key, const Value& value) noexcept {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        
        for (size_t i = 0; i < capacity_; ++i) {
            Entry& entry = entries_[index];
            EntryState expected = EntryState::EMPTY;
            
            // 尝试占用空条目
            if (entry.state.compare_exchange_weak(expected, EntryState::OCCUPIED,
                                                 std::memory_order_acq_rel)) {
                entry.key = key;
                entry.value = value;
                size_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // 检查是否是已删除的条目
            expected = EntryState::DELETED;
            if (entry.state.compare_exchange_weak(expected, EntryState::OCCUPIED,
                                                 std::memory_order_acq_rel)) {
                entry.key = key;
                entry.value = value;
                size_.fetch_add(1, std::memory_order_relaxed);
                deletedCount_.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
            
            // 检查是否是相同的键
            if (entry.state.load(std::memory_order_acquire) == EntryState::OCCUPIED &&
                entry.key == key) {
                return false; // 键已存在
            }
            
            // 线性探测下一个位置
            index = (index + 1) & mask_;
        }
        
        return false; // 哈希表已满
    }
    
    /**
     * @brief 查找键对应的值
     * @param key 键
     * @param value 输出参数，存储找到的值
     * @return true 如果找到，false 如果未找到
     */
    bool find(const Key& key, Value& value) const noexcept {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        
        for (size_t i = 0; i < capacity_; ++i) {
            const Entry& entry = entries_[index];
            EntryState state = entry.state.load(std::memory_order_acquire);
            
            if (state == EntryState::EMPTY) {
                return false; // 未找到
            }
            
            if (state == EntryState::OCCUPIED && entry.key == key) {
                value = entry.value;
                return true; // 找到
            }
            
            // 继续探测
            index = (index + 1) & mask_;
        }
        
        return false; // 未找到
    }
    
    /**
     * @brief 删除键值对
     * @param key 键
     * @return true 如果删除成功，false 如果键不存在
     */
    bool remove(const Key& key) noexcept {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        
        for (size_t i = 0; i < capacity_; ++i) {
            Entry& entry = entries_[index];
            EntryState expected = EntryState::OCCUPIED;
            
            if (entry.state.load(std::memory_order_acquire) == EntryState::OCCUPIED &&
                entry.key == key) {
                
                // 尝试标记为已删除
                if (entry.state.compare_exchange_weak(expected, EntryState::DELETED,
                                                     std::memory_order_acq_rel)) {
                    size_.fetch_sub(1, std::memory_order_relaxed);
                    deletedCount_.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
            
            if (entry.state.load(std::memory_order_acquire) == EntryState::EMPTY) {
                return false; // 未找到
            }
            
            // 继续探测
            index = (index + 1) & mask_;
        }
        
        return false; // 未找到
    }
    
    /**
     * @brief 更新键对应的值
     * @param key 键
     * @param value 新值
     * @return true 如果更新成功，false 如果键不存在
     */
    bool update(const Key& key, const Value& value) noexcept {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        
        for (size_t i = 0; i < capacity_; ++i) {
            Entry& entry = entries_[index];
            
            if (entry.state.load(std::memory_order_acquire) == EntryState::OCCUPIED &&
                entry.key == key) {
                entry.value = value;
                return true;
            }
            
            if (entry.state.load(std::memory_order_acquire) == EntryState::EMPTY) {
                return false; // 未找到
            }
            
            // 继续探测
            index = (index + 1) & mask_;
        }
        
        return false; // 未找到
    }
    
    /**
     * @brief 获取哈希表大小
     * @return 当前元素数量
     */
    size_t size() const noexcept {
        return size_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 检查哈希表是否为空
     * @return true 如果为空
     */
    bool empty() const noexcept {
        return size() == 0;
    }
    
    /**
     * @brief 获取哈希表容量
     * @return 哈希表容量
     */
    size_t capacity() const noexcept {
        return capacity_;
    }
    
    /**
     * @brief 获取负载因子
     * @return 当前负载因子
     */
    double loadFactor() const noexcept {
        return static_cast<double>(size()) / capacity_;
    }
    
    /**
     * @brief 遍历哈希表
     * @param func 遍历函数
     */
    template<typename Func>
    void forEach(Func&& func) const {
        for (size_t i = 0; i < capacity_; ++i) {
            const Entry& entry = entries_[i];
            if (entry.state.load(std::memory_order_acquire) == EntryState::OCCUPIED) {
                func(entry.key, entry.value);
            }
        }
    }
    
    /**
     * @brief 清空哈希表
     * 
     * 注意: 此操作不是线程安全的
     */
    void clear() noexcept {
        for (size_t i = 0; i < capacity_; ++i) {
            entries_[i].state.store(EntryState::EMPTY, std::memory_order_relaxed);
        }
        size_.store(0, std::memory_order_relaxed);
        deletedCount_.store(0, std::memory_order_relaxed);
    }

private:
    /**
     * @brief 计算下一个2的幂
     */
    static size_t nextPowerOfTwo(size_t n) noexcept {
        if (n <= 1) return 2;
        
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_t) > 4) {
            n |= n >> 32;
        }
        n++;
        
        return n;
    }
};

/**
 * @brief 专门用于字符串键的哈希表
 */
template<typename Value>
using StringHashMap = LockFreeHashMap<std::string, Value>;

/**
 * @brief 专门用于整数键的哈希表
 */
template<typename Value>
using IntHashMap = LockFreeHashMap<uint64_t, Value>;

} // namespace lockfree
} // namespace plc_runtime

#endif // LOCKFREE_LOCKFREE_HASH_MAP_H
"