#ifndef TASK_CONTROL_BLOCK_MINIMAL_H
#define TASK_CONTROL_BLOCK_MINIMAL_H

#include <cstdint>
#include <string>

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
    std::string name;
    
    TaskControlBlock(uint32_t id, const std::string& task_name, 
                    TaskType task_type, Priority prio)
        : task_id(id)
        , state(TaskState::IDLE)
        , type(task_type)
        , priority(prio)
        , name(task_name)
    {
    }
    
    void update_execution_stats(uint64_t start_time_ns, uint64_t end_time_ns) {
        // Simple implementation for testing
    }
};

} // namespace scheduler
} // namespace plc_runtime

#endif // TASK_CONTROL_BLOCK_MINIMAL_H