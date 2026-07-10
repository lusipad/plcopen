#pragma once

// B9 multi-joint aggregation entry (approved trajectory-stream matrix,
// decision #10): a fixed-capacity bank of independent per-joint stream
// filters sharing one configuration call. This is a configuration
// convenience — no cross-joint time synchronization semantics are promised;
// whole-body consistency belongs to the producer.
//
// RT-SAFE cycle path: cycle() advances every configured joint filter; all
// storage is fixed, no heap allocation, no locks, no exceptions.

#include <cstddef>
#include <cstdint>

#include "rt/error.h"
#include "stream/filter.h"

namespace plcopen::core::stream
{

class JointStreamGroup
{
public:
    static constexpr std::size_t MaxJoints = 32;

    // Applies one shared configuration to all joints (planning-domain call).
    rt::ErrorCode configure(std::size_t joint_count, const StreamFilterConfig &config)
    {
        if(joint_count < 1 || joint_count > MaxJoints) {
            return rt::ErrorCode::invalid_argument;
        }
        const std::size_t preflight_count =
            joint_count > joint_count_ ? joint_count : joint_count_;
        for(std::size_t i = 0; i < preflight_count; ++i) {
            if(!filters_[i].can_configure(config)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(std::size_t i = 0; i < joint_count; ++i) {
            const rt::ErrorCode configured = filters_[i].configure(config);
            if(configured != rt::ErrorCode::ok) {
                return configured;
            }
        }
        joint_count_ = joint_count;
        return rt::ErrorCode::ok;
    }

    void end_session()
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            filters_[i].end_session();
        }
    }

    rt::ErrorCode reset(std::size_t joint, otg::State1D state)
    {
        if(joint >= joint_count_) {
            return rt::ErrorCode::invalid_argument;
        }
        return filters_[joint].reset(state);
    }

    rt::ErrorCode push_target(std::size_t joint, const StreamTarget &target)
    {
        if(joint >= joint_count_) {
            return rt::ErrorCode::invalid_argument;
        }
        return filters_[joint].push_target(target);
    }

    // RT cycle path: advances every joint one interpolation cycle.
    void cycle()
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            filters_[i].cycle();
        }
    }

    otg::State1D state(std::size_t joint) const
    {
        if(joint >= joint_count_) {
            return otg::State1D{};
        }
        return filters_[joint].state();
    }

    const StreamFilter1D &joint(std::size_t index) const
    {
        return filters_[index];
    }

    std::size_t joint_count() const
    {
        return joint_count_;
    }

private:
    StreamFilter1D filters_[MaxJoints]{};
    std::size_t joint_count_ = 0;
};

} // namespace plcopen::core::stream
