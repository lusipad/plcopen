#pragma once

#include <cstdint>
#include <functional>

namespace Uranus {
    enum class TaskType {
        CYCLIC,
        INTERRUPT,
        FREE_RUNNING
    };

    enum class TaskPriority {
        LOW = 1,
        NORMAL = 5,
        HIGH = 10
    };

    enum class TaskState {
        IDLE,
        READY,
        RUNNING,
        SUSPENDED,
        TERMINATED
    };

    class Task {
    public:
        Task(uint32_t id, TaskType type, uint32_t priority, TaskPriority taskPriority);
        ~Task();

        void Run();
        void Suspend();
        void Resume();
        void Terminate();
        void SetFunction(std::function<void()> func);
        void SetCyclePeriod(uint64_t periodUs);

        uint32_t GetId() const { return id_; }
        TaskType GetType() const { return type_; }
        uint32_t GetPriority() const { return priority_; }
        TaskPriority GetTaskPriority() const { return taskPriority_; }
        TaskState GetState() const { return state_; }
        uint64_t GetCyclePeriod() const { return cyclePeriodUs_; }
        std::function<void()> GetFunction() const { return entry_; }

    private:
        uint32_t id_;
        TaskType type_;
        uint32_t priority_;
        TaskPriority taskPriority_;
        TaskState state_;
        uint64_t cyclePeriodUs_;
        std::function<void()> entry_;
    };
}