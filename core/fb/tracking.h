#pragma once

#include <cstdint>

#include "axis/group.h"
#include "fb/motion.h"

namespace plcopen::core::fb
{

class TrackingExecuteFb
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool execute = false;
    axis::CoordSystem coord_system = axis::CoordSystem::pcs;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            tracked_command_id_ = 0;
        }
        return rising;
    }

    void accept(const rt::Result<std::uint32_t> &accepted)
    {
        clear(outputs);
        if(!accepted) {
            outputs.error = true;
            outputs.error_id = accepted.error();
            return;
        }
        tracked_command_id_ = accepted.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.done = true;
        outputs.busy = true;
    }

    void observe()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr) return;
        const rt::ErrorCode error = group_ref->tracking_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(group_ref->tracking_command_busy(tracked_command_id_)) {
            outputs.busy = true;
            outputs.active = group_ref->tracking_command_active(tracked_command_id_);
            return;
        }
        outputs.command_aborted =
            group_ref->tracking_command_aborted(tracked_command_id_);
        outputs.busy = false;
        outputs.active = false;
        tracked_command_id_ = 0;
    }

private:
    bool last_execute_ = false;
    std::uint32_t tracked_command_id_ = 0;
};

class FbSetDynCoordTransform : public TrackingExecuteFb
{
public:
    axis::AxisGroup *master_group_ref = nullptr;
    axis::ToolData coord_transform{};

    void call()
    {
        if(rising_edge()) {
            accept(group_ref == nullptr || master_group_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(
                             rt::ErrorCode::invalid_argument)
                       : group_ref->set_dynamic_coord_transform(
                             *master_group_ref, coord_transform, coord_system, buffer_mode));
        }
        observe();
    }
};

class FbTrackConveyorBelt : public TrackingExecuteFb
{
public:
    axis::AxisModel *conveyor_belt_ref = nullptr;
    axis::ToolData conveyor_belt_origin{};
    axis::ToolData initial_object_position{};

    void call()
    {
        if(rising_edge()) {
            accept(group_ref == nullptr || conveyor_belt_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(
                             rt::ErrorCode::invalid_argument)
                       : group_ref->track_conveyor(
                             *conveyor_belt_ref, conveyor_belt_origin,
                             initial_object_position, coord_system, buffer_mode));
        }
        observe();
    }
};

class FbTrackRotaryTable : public TrackingExecuteFb
{
public:
    axis::AxisModel *rotary_table_ref = nullptr;
    axis::ToolData rotary_table_origin{};
    axis::ToolData initial_object_position{};

    void call()
    {
        if(rising_edge()) {
            accept(group_ref == nullptr || rotary_table_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(
                             rt::ErrorCode::invalid_argument)
                       : group_ref->track_rotary_table(
                             *rotary_table_ref, rotary_table_origin,
                             initial_object_position, coord_system, buffer_mode));
        }
        observe();
    }
};

} // namespace plcopen::core::fb
