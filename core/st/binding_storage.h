#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
#include "fb/io.h"
#include "fb/path_table.h"
#include "st/binding.h"
#include "st/types.h"

namespace plcopen::core::st
{

enum class BindingStorageKind : std::uint8_t
{
    none = 0,
    axis_targets,
    group_targets,
    path_tables,
    path_descriptions,
    cam_switch_tables,
    cam_switch_outputs,
    cam_track_options,
    cam_tables,
    position_profiles,
    velocity_profiles,
    acceleration_profiles,
    kin_transforms,
};

struct BindingProfileSlot
{
    std::array<axis::ProfileSegment, kMaxProfileSegments> segments{};
    std::size_t count = 0;
};

using AxisTargetStorage =
    std::array<axis::AxisModel *, kMaxAxisBindings>;
using GroupTargetStorage =
    std::array<axis::AxisGroup *, kMaxGroupBindings>;
using PathTableStorage = std::array<fb::PathTable *, kMaxTableBindings>;
using PathDescriptionStorage =
    std::array<fb::PathDescription, kMaxTableBindings>;
using CamSwitchTableStorage =
    std::array<fb::CamSwitchTableView, kMaxTableBindings>;
using CamSwitchOutputStorage =
    std::array<fb::CamSwitchOutputsView, kMaxTableBindings>;
using CamTrackOptionStorage =
    std::array<fb::CamTrackOptionsView, kMaxTableBindings>;
using CamTableStorage =
    std::array<exec::CamTableView, kMaxTableBindings>;
using PositionProfileStorage =
    std::array<BindingProfileSlot, kMaxTableBindings>;
using VelocityProfileStorage = PositionProfileStorage;
using AccelerationProfileStorage = PositionProfileStorage;
using KinTransformStorage =
    std::array<axis::KinTransformRef, kMaxTableBindings>;

constexpr BindingStorageKind binding_storage_kind(Type type, TypeId type_id)
{
    if(type == Type::axis_ref || type_id == binding_type::axis_ref ||
       type_id == binding_type::mc_input_ref ||
       type_id == binding_type::mc_output_ref) {
        return BindingStorageKind::axis_targets;
    }
    if(type == Type::group_ref || type_id == binding_type::group_ref) {
        return BindingStorageKind::group_targets;
    }
    switch(type_id) {
    case binding_type::mc_path_table:
        return BindingStorageKind::path_tables;
    case binding_type::mc_path_description:
        return BindingStorageKind::path_descriptions;
    case binding_type::mc_cam_switch_table_view:
        return BindingStorageKind::cam_switch_tables;
    case binding_type::mc_cam_switch_outputs_view:
        return BindingStorageKind::cam_switch_outputs;
    case binding_type::mc_cam_track_options_view:
        return BindingStorageKind::cam_track_options;
    case binding_type::mc_cam_table_view:
        return BindingStorageKind::cam_tables;
    case binding_type::mc_time_position:
        return BindingStorageKind::position_profiles;
    case binding_type::mc_time_velocity:
        return BindingStorageKind::velocity_profiles;
    case binding_type::mc_time_acceleration:
        return BindingStorageKind::acceleration_profiles;
    case binding_type::mc_kin_transform_ref:
        return BindingStorageKind::kin_transforms;
    default: return BindingStorageKind::none;
    }
}

constexpr std::size_t binding_storage_bytes(BindingStorageKind kind)
{
    switch(kind) {
    case BindingStorageKind::axis_targets: return sizeof(AxisTargetStorage);
    case BindingStorageKind::group_targets: return sizeof(GroupTargetStorage);
    case BindingStorageKind::path_tables: return sizeof(PathTableStorage);
    case BindingStorageKind::path_descriptions:
        return sizeof(PathDescriptionStorage);
    case BindingStorageKind::cam_switch_tables:
        return sizeof(CamSwitchTableStorage);
    case BindingStorageKind::cam_switch_outputs:
        return sizeof(CamSwitchOutputStorage);
    case BindingStorageKind::cam_track_options:
        return sizeof(CamTrackOptionStorage);
    case BindingStorageKind::cam_tables: return sizeof(CamTableStorage);
    case BindingStorageKind::position_profiles:
        return sizeof(PositionProfileStorage);
    case BindingStorageKind::velocity_profiles:
        return sizeof(VelocityProfileStorage);
    case BindingStorageKind::acceleration_profiles:
        return sizeof(AccelerationProfileStorage);
    case BindingStorageKind::kin_transforms:
        return sizeof(KinTransformStorage);
    case BindingStorageKind::none: return 0;
    }
    return 0;
}

} // namespace plcopen::core::st
