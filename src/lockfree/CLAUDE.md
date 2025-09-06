# 无锁数据结构模块 (Lock-free Data Structures)

**路径**: `src/lockfree/` 和 `include/lockfree/`  
**导航**: [🏠 项目根目录](/root/Repos/plcopen/CLAUDE.md) > 无锁数据结构模块  
**模块类型**: 基础设施核心组件  
**开发状态**: ✅ P0阶段完成

## 📋 模块概述

无锁数据结构模块提供高性能、无锁的并发数据结构，避免优先级反转问题，确保实时系统的确定性。包括SPSC队列、MPMC队列、原子工具和无锁哈希映射。

## 📁 文件结构

### 头文件
- **[spsc_queue.h](/root/Repos/plcopen/include/lockfree/spsc_queue.h)** - 单生产者单消费者队列
- **[mpmc_queue.h](/root/Repos/plcopen/include/lockfree/mpmc_queue.h)** - 多生产者多消费者队列
- **[lockfree_hash_map.h](/root/Repos/plcopen/include/lockfree/lockfree_hash_map.h)** - 无锁哈希映射
- **[atomic_utils.h](/root/Repos/plcopen/include/lockfree/atomic_utils.h)** - 原子操作工具

### 实现文件
- **[spsc_queue.h](/root/Repos/plcopen/src/lockfree/spsc_queue.h)** - SPSC队列调度器版本
- **[spsc_queue.h](/root/Repos/plcopen/src/scheduler/lockfree/spsc_queue.h)** - 调度器专用版本

## 🔧 核心数据结构

### SPSC队列 (Single Producer Single Consumer)
```cpp
template<typename T, size_t Size>
class SPSCQueue {
public:
    bool push(const T& item);
    bool pop(T& item);
    bool empty() const;
    bool full() const;
    size_t size() const;
    
private:
    alignas(64) std::atomic<size_t> head_{0};  // 生产者指针
    alignas(64) std::atomic<size_t> tail_{0};  // 消费者指针
    alignas(64) T buffer_[Size];               // 环形缓冲区
};
```

### MPMC队列 (Multi Producer Multi Consumer)
```cpp
template<typename T, size_t Size>
class MPMCQueue {
public:
    bool enqueue(const T& item);
    bool dequeue(T& item);
    size_t size() const;
    
private:
    struct Cell {
        std::atomic<size_t> sequence_;
        T data_;
    };
    
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
    alignas(64) Cell buffer_[Size];
};
```

### 无锁哈希映射
```cpp
template<typename Key, typename Value, size_t Size>
class LockFreeHashMap {
public:
    bool insert(const Key& key, const Value& value);
    bool find(const Key& key, Value& value);
    bool remove(const Key& key);
    
private:
    struct Node {
        Key key_;
        Value value_;
        std::atomic<Node*> next_;
    };
    
    alignas(64) std::atomic<Node*> buckets_[Size];
};
```

## 📊 性能特征

| 数据结构 | 操作复杂度 | 内存占用 | 无锁特性 |
|----------|------------|----------|----------|
| SPSC队列 | O(1) | 缓存行对齐 | ✅ 完全无锁 |
| MPMC队列 | O(1) | 缓存行对齐 | ✅ 完全无锁 |
| 无锁哈希映射 | O(1) | 动态增长 | ✅ 无锁读写 |

## 🚀 使用示例

### SPSC队列使用
```cpp
#include "lockfree/spsc_queue.h"

// 创建队列
SPSCQueue<int, 1024> queue;

// 生产者线程
std::thread producer([&queue]() {
    for (int i = 0; i < 1000; ++i) {
        while (!queue.push(i)) {
            std::this_thread::yield(); // 队列满时让出CPU
        }
    }
});

// 消费者线程
std::thread consumer([&queue]() {
    int value;
    int count = 0;
    while (count < 1000) {
        if (queue.pop(value)) {
            std::cout << "接收到: " << value << "\n";
            ++count;
        } else {
            std::this_thread::yield(); // 队列空时让出CPU
        }
    }
});
```

### 调度器中的应用
```cpp
// 在调度器中使用SPSC队列进行任务调度
class TaskScheduler {
private:
    SPSCQueue<TaskControlBlock*, 256> ready_queue_;
    
public:
    void enqueue_task(TaskControlBlock* tcb) {
        while (!ready_queue_.push(tcb)) {
            // 处理队列满的情况
            handle_queue_overflow();
        }
    }
    
    TaskControlBlock* dequeue_task() {
        TaskControlBlock* tcb;
        if (ready_queue_.pop(tcb)) {
            return tcb;
        }
        return nullptr;
    }
};
```

---

*本模块提供高性能无锁并发原语，是实时系统的基础组件。*