#include "Task.h"

namespace Uranus {
    Task::Task(uint32_t id, TaskType type, uint32_t priority, TaskPriority taskPriority)
        : id_(id), type_(type), priority_(priority), taskPriority_(taskPriority), 
          state_(TaskState::IDLE), cyclePeriodUs_(1000000) {}

    Task::~Task() {}

    void Task::Run() {
        if (state_ == TaskState::READY || state_ == TaskState::IDLE) {
            state_ = TaskState::RUNNING;
            if (entry_) {
                entry_();
            }
            state_ = TaskState::READY;
        }
    }

    void Task::SetFunction(std::function<void()> func) {
        entry_ = func;
    }

    void Task::SetCyclePeriod(uint64_t periodUs) {
        cyclePeriodUs_ = periodUs;
    }

    void Task::Suspend() {
        state_ = TaskState::SUSPENDED;
    }

    void Task::Resume() {
        state_ = TaskState::READY;
    }

    void Task::Terminate() {
        state_ = TaskState::TERMINATED;
    }
}