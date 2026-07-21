#pragma once

#include <cstdint>

namespace plcopen::core::axis
{

class AxisGroup;

// MoveDirect command lifecycle state. Shared queue, path/window execution,
// status and error state deliberately remain owned by AxisGroup.
class GroupDirectPathState
{
    friend class AxisGroup;

    std::uint32_t direct_command_id_ = 0;
    std::uint32_t last_completed_direct_id_ = 0;
    std::uint32_t last_aborted_direct_id_ = 0;
    bool direct_active_ = false;
    bool direct_stopping_ = false;
};

} // namespace plcopen::core::axis
