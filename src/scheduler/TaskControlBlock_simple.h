#ifndef TASK_CONTROL_BLOCK_SIMPLE_H
#define TASK_CONTROL_BLOCK_SIMPLE_H

#include <cstdint>
#include <atomic>
#include <string>
#include <memory>

namespace plc_runtime {
namespace scheduler {

using Priority = uint8_t;

enum class TaskState : uint8_t {
    IDLE = 0,
    READY = 1,
    RUNNING = 2
};

enum class TaskType : uint8_t {
    CYCLIC = 0,
    INTERRUPT = 1
};

struct TaskControlBlock {
    uint32_t task_id;
    TaskState state;
    TaskType type;
    Priority priority;
    
    std::atomic<uint64_t> exec_count;
    std::atomic<uint64_t> max_exec_time_ns;
    std::atomic<uint64_t> min_exec_time_ns;
    std::atomic<uint64_t> last_start_time_ns;
    std::atomic<uint64_t> deadline_miss_count;
    
    std::string name;
    
    TaskControlBlock(uint32_t id, const std::string& task_name, 
                    TaskType task_type, Priority prio)
        : task_id(id)
        , state(TaskState::IDLE)
        , type(task_type)
        , priority(prio)
        , exec_count(0)
        , max_exec_time_ns(0)
        , min_exec_time_ns(UINT64_MAX)
        , last_start_time_ns(0)
        , deadline_miss_count(0)
        , name(task_name)
    {
    }
    
    void update_execution_stats(uint64_t start_time_ns, uint64_t end_time_ns) {
        uint64_t exec_time = end_time_ns - start_time_ns;
        exec_count.fetch_add(1);
        last_start_time_ns.store(start_time_ns);
        
        // 简单更新，避免复杂的原子操作
        if (exec_time > max_exec_time_ns.load()) {
            max_exec_time_ns.store(exec_time);
        }
        if (exec_time < min_exec_time_ns.load()) {
            min_exec_time_ns.store(exec_time);
        }
    }
};

using TCBPtr = std::shared_ptr<TaskControlBlock>;

} // namespace scheduler
} // namespace plc_runtime

#endif // TASK_CONTROL_BLOCK_SIMPLE_H