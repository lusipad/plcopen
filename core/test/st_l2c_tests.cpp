// ST-L2c acceptance tests (approved st-l2c-semantics, 2026-07-17).
//
// The production binding manifest is the only FB/pin schema consumed here.
// This file deliberately does not repeat the 134 FB names or their pins: the
// closure test walks the generated manifest and then probes the real registry.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/group.h"
#include "fb/motion.h"
#include "fb/path_table.h"
#include "kin/gantry.h"
#include "rt/error.h"
#include "st/st.h"

namespace
{

bool g_freeze_allocations = false;
unsigned long long g_frozen_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size)
{
    if(g_freeze_allocations) ++g_frozen_allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }

namespace
{

using namespace plcopen::core;

int failures = 0;

void check(bool condition, const char *name)
{
    if(!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) return true;
    }
    return false;
}

std::string lower_ascii(std::string_view text)
{
    std::string result(text);
    for(char &character : result) {
        if(character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

std::vector<std::uint64_t> instance_storage(const st::Program &program)
{
    return std::vector<std::uint64_t>((program.required_bytes() + 7U) / 8U + 1U);
}

const char *kCanonicalAxisProgram =
    "PROGRAM p\n"
    "VAR AxisX : AXIS_REF; Move : MC_MoveAbsolute; Done : BOOL; "
    "Busy : BOOL; Active : BOOL; Aborted : BOOL; Error : BOOL; "
    "ErrorID : DINT; END_VAR\n"
    "Move(Axis := AxisX, Execute := TRUE, ContinuousUpdate := FALSE, "
    "Position := 0.25, Velocity := 1.0, Acceleration := 2.0, "
    "Deceleration := 2.0, Jerk := 10.0, "
    "Direction := MC_DIRECTION#current, "
    "BufferMode := MC_BUFFER_MODE#aborting);\n"
    "Done := Move.Done; Busy := Move.Busy; Active := Move.Active; "
    "Aborted := Move.CommandAborted; Error := Move.Error; "
    "ErrorID := Move.ErrorID;\n"
    "END_PROGRAM\n";

const char *kCanonicalGroupProgram =
    "PROGRAM p\n"
    "VAR GroupX : GROUP_REF; Read : MC_GroupReadStatus; Valid : BOOL; "
    "Error : BOOL; ErrorID : DINT; Disabled : BOOL; Standby : BOOL; "
    "Moving : BOOL; Stopping : BOOL; ErrorStop : BOOL; END_VAR\n"
    "Read(AxesGroup := GroupX, Enable := TRUE);\n"
    "Valid := Read.Valid; Error := Read.Error; ErrorID := Read.ErrorID; "
    "Disabled := Read.GroupDisabled; Standby := Read.GroupStandby; "
    "Moving := Read.GroupMoving; Stopping := Read.GroupStopping; "
    "ErrorStop := Read.GroupErrorStop;\n"
    "END_PROGRAM\n";

void manifest_closure_uses_one_production_schema()
{
    const st::BindingManifest &manifest = st::binding_manifest();
    const std::array<std::size_t, 4> expected = {10, 45, 68, 11};
    std::array<std::size_t, 4> set_counts{};
    std::set<std::string> fb_names;
    std::size_t declared_fbs = 0;
    std::size_t generated_fbs = 0;
    std::size_t registered_fbs = 0;
    std::size_t tested_fbs = 0;
    std::size_t excluded_fbs = 0;
    std::size_t declared_pins = 0;
    std::size_t generated_pins = 0;
    std::size_t registered_pins = 0;
    std::size_t tested_pins = 0;
    std::size_t excluded_pins = 0;

    for(std::size_t index = 0; index < manifest.fb_count(); ++index) {
        const st::BindingFbDesc &fb_desc = manifest.fb(index);
        switch(fb_desc.set) {
        case st::BindingSet::iec_basic: ++set_counts[0]; break;
        case st::BindingSet::plcopen_part1_part2: ++set_counts[1]; break;
        case st::BindingSet::plcopen_part4: ++set_counts[2]; break;
        case st::BindingSet::plcopen_part5: ++set_counts[3]; break;
        }
        check(!fb_desc.lower_name.empty(), "L2c-A01 FB has a name");
        check(fb_names.insert(lower_ascii(fb_desc.lower_name)).second,
              "L2c-A01 FB names are unique case-insensitively");
        check(!fb_desc.source.empty(), "L2c-A01 FB retains authority source");
        check(fb_desc.declared, "L2c-A01 FB is declared by authority source");
        check(fb_desc.generated, "L2c-A01 FB is generated from authority source");
        declared_fbs += fb_desc.declared ? 1U : 0U;
        generated_fbs += fb_desc.generated ? 1U : 0U;
        const st::generated::StBindingFbMetadata &fb_metadata =
            st::generated::kStBindingFbs[index];
        bool expected_fb_registered = true;
        for(std::size_t pin_index = 0; pin_index < fb_metadata.pin_count;
            ++pin_index) {
            const st::generated::StBindingPinMetadata &pin_metadata =
                st::generated::kStBindingPins[fb_metadata.first_pin +
                                              pin_index];
            expected_fb_registered = expected_fb_registered &&
                pin_metadata.adapter !=
                    st::generated::StBindingAdapterKind::unresolved &&
                pin_metadata.st_type != "UNRESOLVED";
        }
        const bool fb_registered = st::binding_fb_registered(fb_desc.id);
        check(fb_registered == expected_fb_registered,
              "L2c-A01 FB registration matches complete native coverage");
        registered_fbs += fb_registered ? 1U : 0U;
        if(fb_desc.excluded) {
            ++excluded_fbs;
            check(!fb_desc.scope_reason.empty(),
                  "L2c-A06 excluded FB has a scope reason");
            check(!fb_desc.rejection_test.empty(),
                  "L2c-A06 excluded FB has a rejection anchor");
        } else {
            ++tested_fbs;
        }

        std::set<std::string> pin_names;
        for(std::size_t pin_index = 0; pin_index < fb_desc.pin_count; ++pin_index) {
            const st::BindingPinDesc &pin = fb_desc.pins[pin_index];
            check(!pin.lower_name.empty(), "L2c-A02 pin has a name");
            check(pin_names.insert(lower_ascii(pin.lower_name)).second,
                  "L2c-A02 pin names are unique within an FB");
            switch(pin.direction) {
            case st::PinDirection::input:
            case st::PinDirection::output:
            case st::PinDirection::in_out: break;
            default:
                check(false, "L2c-A02 pin has a canonical direction");
                break;
            }
            check(pin.declared, "L2c-A02 pin is declared by authority source");
            check(pin.generated, "L2c-A02 pin is generated from authority source");
            declared_pins += pin.declared ? 1U : 0U;
            generated_pins += pin.generated ? 1U : 0U;
            const st::generated::StBindingPinMetadata &pin_metadata =
                st::generated::kStBindingPins[fb_metadata.first_pin +
                                              pin_index];
            const bool expected_pin_registered =
                pin_metadata.adapter !=
                    st::generated::StBindingAdapterKind::unresolved &&
                pin_metadata.st_type != "UNRESOLVED";
            const bool pin_registered =
                st::binding_pin_registered(fb_desc.id, pin.id);
            check(pin_registered == expected_pin_registered,
                  "L2c-A02 pin registration matches native adapter coverage");
            registered_pins += pin_registered ? 1U : 0U;
            if(pin.excluded) {
                ++excluded_pins;
                check(!pin.scope_reason.empty(),
                      "L2c-A06 excluded pin has a scope reason");
                check(!pin.rejection_test.empty(),
                      "L2c-A06 excluded pin has a rejection anchor");
            } else {
                ++tested_pins;
            }
        }
    }

    check(set_counts == expected, "L2c-A01 basic10 + Part1/2-45 + Part4-68 + Part5-11");
    check(declared_fbs == generated_fbs &&
              generated_fbs == tested_fbs + excluded_fbs,
          "L2c-A01 declared == generated == tested + excluded FBs");
    check(declared_pins == generated_pins &&
              generated_pins == tested_pins + excluded_pins,
          "L2c-A02 declared == generated == tested + excluded pins");
    check(registered_fbs <= generated_fbs && registered_pins <= generated_pins,
          "L2c-A01/A02 registration never exceeds generated authority");
}

void generated_lifecycle_covers_every_declared_fb()
{
    for(const st::generated::StBindingFbMetadata &metadata :
        st::generated::kStBindingFbs) {
        const std::size_t bytes =
            st::generated::st_binding_native_size(metadata.type);
        const std::size_t alignment =
            st::generated::st_binding_native_align(metadata.type);
        check(bytes != 0U && alignment != 0U && alignment <= alignof(std::uint64_t),
              "L2c-A01 generated FB has a static native layout");
        if(bytes == 0U || alignment == 0U ||
           alignment > alignof(std::uint64_t)) {
            continue;
        }
        std::vector<std::uint64_t> storage((bytes + 7U) / 8U);
        check(st::generated::st_binding_default_construct(metadata.type,
                                                          storage.data()),
              "L2c-A01 generated FB constructs through its real native type");
        check(st::generated::st_binding_invoke(metadata.type, storage.data(),
                                               1000000),
              "L2c-A01 generated FB invokes its declared lifecycle");
        check(st::generated::st_binding_destruct(metadata.type,
                                                 storage.data()),
              "L2c-A01 generated FB destructs through its real native type");
    }
}

void aggregate_fb_outputs_use_the_object_codec()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR GroupX : GROUP_REF; Read : MC_GroupReadPosition; "
        "Position : MC_GROUP_POSITION; END_VAR "
        "Read(AxesGroup := GroupX, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual); "
        "Position := Read.Position; END_PROGRAM");
    check(compiled.ok, "L2c-A02 aggregate FB output source compiles");
    if(!compiled.ok) return;

    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisGroup group;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok &&
              instance.bind_group("GroupX", &group) == st::BindingError::ok,
          "L2c-A02 aggregate FB output program loads and binds");
    check(instance.scan(256) == st::ScanError::ok,
          "L2c-A02 aggregate FB output uses a registered fieldwise codec");
}

void reference_target_tags_are_inferred_from_manifest()
{
    const st::BindingManifest &manifest = st::binding_manifest();
    std::set<st::TypeId> pin_reference_types;
    std::set<st::TypeId> target_reference_types;

    for(std::size_t fb_index = 0; fb_index < manifest.fb_count(); ++fb_index) {
        const st::BindingFbDesc &fb_desc = manifest.fb(fb_index);
        for(std::size_t pin_index = 0; pin_index < fb_desc.pin_count; ++pin_index) {
            const st::BindingPinDesc &pin = fb_desc.pins[pin_index];
            const st::TypeDesc *type = manifest.type_table().get(pin.type_id);
            const st::generated::StBindingFbMetadata &fb_metadata =
                st::generated::kStBindingFbs[fb_index];
            const st::generated::StBindingPinMetadata &pin_metadata =
                st::generated::kStBindingPins[fb_metadata.first_pin +
                                              pin_index];
            check((pin_metadata.st_type == "UNRESOLVED") == (type == nullptr),
                  "L2c-A02 resolved pin TypeIds match generated type authority");
            if(type != nullptr &&
               (pin.type_id == st::binding_type::axis_ref ||
                pin.type_id == st::binding_type::group_ref)) {
                pin_reference_types.insert(pin.type_id);
            }
        }
    }

    for(std::size_t index = 0; index < manifest.target_count(); ++index) {
        const st::BindingTargetDesc &target = manifest.target(index);
        check(target.kind != st::BindingTargetKind::invalid,
              "L2c-A02 reference target has a stable non-invalid tag");
        check(target.type_id != st::invalid_type_id,
              "L2c-A02 reference target has a stable TypeId");
        check(target.bindable, "L2c-A02 reference target has a host bind path");
        check(target_reference_types.insert(target.type_id).second,
              "L2c-A02 reference target TypeIds are unique");
    }
    check(pin_reference_types == target_reference_types,
          "L2c-A02 all and only manifest reference pins have BindingTarget tags");

    static_assert(std::is_trivially_copyable<st::BindingTarget>::value,
                  "BindingTarget is a non-owning tagged value");
    axis::AxisModel axis;
    axis::AxisGroup group;
    const st::BindingTarget axis_target = st::BindingTarget::axis(&axis);
    const st::BindingTarget group_target = st::BindingTarget::group(&group);
    check(axis_target.kind() == st::BindingTargetKind::axis,
          "L2c-D01 AXIS_REF target tag");
    check(group_target.kind() == st::BindingTargetKind::group,
          "L2c-D01 GROUP_REF target tag");
    check(axis_target.kind() != group_target.kind(),
          "L2c-D01 axis and group tags cannot alias");
}

void host_binding_lifecycle_and_error_mapping()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM refs VAR AxisOne : AXIS_REF; GroupOne : GROUP_REF; "
        "END_VAR END_PROGRAM");
    check(compiled.ok, "L2c-A03 AXIS_REF/GROUP_REF declarations compile");
    if(!compiled.ok) return;

    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel axis;
    axis::AxisGroup group;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-A03 reference program loads");
    check(instance.bind_axis("missing", &axis) == st::BindingError::unknown,
          "L2c-A03 unknown AXIS_REF binding");
    check(instance.bind_group("missing", &group) == st::BindingError::unknown,
          "L2c-A03 unknown GROUP_REF binding");
    check(instance.bind_axis("AxisOne", nullptr) == st::BindingError::null_target,
          "L2c-A03 null AXIS_REF target");
    check(instance.bind_group("GroupOne", nullptr) == st::BindingError::null_target,
          "L2c-A03 null GROUP_REF target");
    check(instance.bind_axis("axisone", &axis) == st::BindingError::ok,
          "L2c-A03 case-insensitive AXIS_REF bind");
    check(instance.bind_axis("AXISONE", &axis) == st::BindingError::duplicate,
          "L2c-A03 duplicate AXIS_REF bind");
    check(instance.bind_group("groupone", &group) == st::BindingError::ok,
          "L2c-A03 case-insensitive GROUP_REF bind");
    check(instance.bind_group("GROUPONE", &group) == st::BindingError::duplicate,
          "L2c-A03 duplicate GROUP_REF bind");
    const int axis_index = instance.find("AxisOne");
    const int group_index = instance.find("GroupOne");
    check(axis_index >= 0 && instance.value_i64(axis_index) == 1,
          "L2c-D01 AXIS_REF variable stores a declaration-order handle");
    check(group_index >= 0 && instance.value_i64(group_index) == 1,
          "L2c-D01 GROUP_REF variable stores a declaration-order handle");
    check(static_cast<std::uintptr_t>(instance.value_i64(axis_index)) !=
              reinterpret_cast<std::uintptr_t>(&axis) &&
              static_cast<std::uintptr_t>(instance.value_i64(group_index)) !=
                  reinterpret_cast<std::uintptr_t>(&group),
          "L2c-D01 ST variables never store host pointer bits");
    check(instance.scan(8) == st::ScanError::ok,
          "L2c-A03 bound reference program scans");
    check(instance.bind_axis("AxisOne", &axis) == st::BindingError::locked,
          "L2c-A03 AXIS_REF bind locks after first scan");
    check(instance.bind_group("GroupOne", &group) == st::BindingError::locked,
          "L2c-A03 GROUP_REF bind locks after first scan");

    std::vector<std::uint64_t> generic_storage = instance_storage(compiled.program);
    st::Instance generic;
    check(generic.load(compiled.program,
                       reinterpret_cast<unsigned char *>(generic_storage.data()),
                       generic_storage.size() * sizeof(generic_storage[0]),
                       1000000) == rt::ErrorCode::ok,
          "L2c-A03 generic reference binder loads");
    check(generic.bind_reference("AxisOne", st::BindingTarget::group(&group)) ==
              st::BindingError::wrong_kind,
          "L2c-A03 GROUP_REF target cannot bind AXIS_REF variable");
    check(generic.bind_reference("GroupOne", st::BindingTarget::axis(&axis)) ==
              st::BindingError::wrong_kind,
          "L2c-A03 AXIS_REF target cannot bind GROUP_REF variable");
    check(generic.bind_reference("AxisOne", st::BindingTarget::axis(&axis)) ==
              st::BindingError::ok,
          "L2c-A03 tagged AXIS_REF dispatch");
    check(generic.bind_reference("GroupOne", st::BindingTarget::group(&group)) ==
              st::BindingError::ok,
          "L2c-A03 tagged GROUP_REF dispatch");

    {
        std::vector<std::uint64_t> scoped_storage =
            instance_storage(compiled.program);
        st::Instance scoped_instance;
        check(scoped_instance.load(
                  compiled.program,
                  reinterpret_cast<unsigned char *>(scoped_storage.data()),
                  scoped_storage.size() * sizeof(scoped_storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  scoped_instance.bind_axis("AxisOne", &axis) ==
                      st::BindingError::ok &&
                  scoped_instance.bind_group("GroupOne", &group) ==
                      st::BindingError::ok,
              "L2c-D01 instance accepts non-owning host references");
    }
    axis.set_power(true);
    check(axis.powered() && group.status() == axis::GroupStatus::disabled,
          "L2c-D01 destroying Instance does not destroy host targets");
}

void host_sequence_binding_lifecycle_and_error_mapping()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM tables VAR AxisX : AXIS_REF; Path : MC_PATH_TABLE; "
        "Switches : MC_CAM_SWITCH_TABLE_VIEW; Cam : MC_CAM_TABLE_VIEW; "
        "Position : MC_TIME_POSITION; Velocity : MC_TIME_VELOCITY; "
        "Acceleration : MC_TIME_ACCELERATION; "
        "Profile : MC_PositionProfile; Error : BOOL; END_VAR "
        "Profile(Axis := AxisX, TimePosition := Position, Execute := TRUE, "
        "ContinuousUpdate := FALSE, TimeScale := 1.0, "
        "PositionScale := 1.0, Offset := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "Error := Profile.Error; END_PROGRAM");
    check(compiled.ok, "L2c-D05 registry-backed sequence declarations compile");
    if(!compiled.ok) {
        for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
            std::printf("L2c-D05 diagnostic %d:%d %s\n", diagnostic.line,
                        diagnostic.column, diagnostic.message.c_str());
        }
        return;
    }

    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel axis;
    fb::PathTable path;
    const std::array<fb::CamSwitchAction, 1> switches{{
        {1U, 1.0, 2.0, 3.0},
    }};
    const std::array<exec::CamPoint, 2> cam{{
        {0.0, 0.0},
        {1.0, 1.0},
    }};
    const std::array<st::BindingPositionProfileEntry, 1> position{{
        {1500001, 1.0, 2.0, 3.0, 3.0, 4.0, false},
    }};
    const std::array<st::BindingVelocityProfileEntry, 1> velocity{{
        {2000000, 2.0, 3.0, 3.0, 4.0},
    }};
    const std::array<st::BindingAccelerationProfileEntry, 1> acceleration{{
        {3000000, 3.0},
    }};

    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-D05 sequence registry program loads");
    check(instance.bind_path_table("missing", &path) ==
              st::BindingError::unknown,
          "L2c-D05 unknown sequence binding");
    check(instance.bind_path_table("Path", nullptr) ==
              st::BindingError::null_target,
          "L2c-D05 null path table target");
    check(instance.bind_cam_switch_table("Switches", switches.data(), 0U) ==
              st::BindingError::invalid_value,
          "L2c-D05 cam switch count lower bound");
    check(instance.bind_cam_table("Cam", cam.data(), 1U, false) ==
              st::BindingError::invalid_value,
          "L2c-D05 cam table count lower bound");
    st::BindingPositionProfileEntry invalid_position = position[0];
    invalid_position.time_ns = 0;
    check(instance.bind_position_profile("Position", &invalid_position, 1U) ==
              st::BindingError::invalid_value,
          "L2c-D05 profile rejects non-positive TIME");
    invalid_position = position[0];
    invalid_position.position =
        std::numeric_limits<double>::quiet_NaN();
    check(instance.bind_position_profile("Position", &invalid_position, 1U) ==
              st::BindingError::invalid_value,
          "L2c-D05 profile rejects non-finite values");

    check(instance.bind_axis("AxisX", &axis) == st::BindingError::ok &&
              instance.bind_path_table("Path", &path) ==
                  st::BindingError::ok &&
              instance.bind_cam_switch_table("Switches", switches.data(),
                                             switches.size()) ==
                  st::BindingError::ok &&
              instance.bind_cam_table("Cam", cam.data(), cam.size(), true) ==
                  st::BindingError::ok &&
              instance.bind_position_profile("Position", position.data(),
                                             position.size()) ==
                  st::BindingError::ok &&
              instance.bind_velocity_profile("Velocity", velocity.data(),
                                             velocity.size()) ==
                  st::BindingError::ok &&
              instance.bind_acceleration_profile(
                  "Acceleration", acceleration.data(), acceleration.size()) ==
                  st::BindingError::ok,
          "L2c-D05 all six registry-backed sequence kinds bind");
    check(instance.bind_path_table("PATH", &path) ==
              st::BindingError::duplicate,
          "L2c-D05 duplicate sequence binding");
    for(const char *name : {"Path", "Switches", "Cam", "Position",
                            "Velocity", "Acceleration"}) {
        const int index = instance.find(name);
        check(index >= 0 && instance.value_i64(index) == 1,
              "L2c-D05 ST sequence variable stores a 1-based handle");
    }
    std::int64_t rounded_cycles = 0;
    check(st::generated::st_binding_time_ns_to_cycles(
              position[0].time_ns, 1000000, rounded_cycles) &&
              rounded_cycles == 2,
          "L2c-D05 profile TIME uses ceiling task-cycle conversion");
    check(instance.scan(128) == st::ScanError::ok,
          "L2c-D05 sequence handles round-trip through an in-out FB pin");
    check(instance.value_i64(instance.find("Position")) == 1,
          "L2c-D05 sequence output resolves back to the existing handle");
    check(instance.bind_path_table("Path", &path) == st::BindingError::locked,
          "L2c-D05 sequence registry locks after first scan");

    std::vector<std::uint64_t> unbound_storage =
        instance_storage(compiled.program);
    st::Instance unbound;
    check(unbound.load(compiled.program,
                       reinterpret_cast<unsigned char *>(unbound_storage.data()),
                       unbound_storage.size() * sizeof(unbound_storage[0]),
                       1000000) == rt::ErrorCode::ok &&
              unbound.scan(128) == st::ScanError::ok,
          "L2c-D05 handle 0 reaches the FB as an empty sequence");
    check(unbound.value_bool(unbound.find("Error")),
          "L2c-D05 unbound sequence reports an FB error without VM fault");
}

void path_description_registry_and_path_fb_adapters_are_closed()
{
    const st::CompileResult opaque_copy = st::compile(
        "PROGRAM p VAR Left : MC_PATH_DESCRIPTION; "
        "Right : MC_PATH_DESCRIPTION; END_VAR Left := Right; END_PROGRAM");
    check(!opaque_copy.ok &&
              has_code(opaque_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D08 path descriptions remain opaque registry handles");

    const st::CompileResult compiled = st::compile(
        "PROGRAM path VAR G : GROUP_REF; Data : MC_PATH_TABLE; "
        "Description : MC_PATH_DESCRIPTION; Select : MC_PathSelect; "
        "Move : MC_MovePath; Selected : BOOL; Error : BOOL; END_VAR "
        "Select(AxesGroup := G, PathData := Data, "
        "PathDescription := Description, Execute := TRUE); "
        "Move(AxesGroup := G, PathData := Data, Execute := FALSE, "
        "CoordSystem := MC_COORD_SYSTEM#pcs, "
        "BufferMode := MC_BUFFER_MODE#blending_high, "
        "TransitionMode := MC_TRANSITION_MODE#max_corner_deviation, "
        "TransitionParameter := 0.25); "
        "Selected := Select.Done; Error := Select.Error OR Move.Error; "
        "END_PROGRAM");
    check(compiled.ok,
          "L2c-D08 PathSelect and MovePath stable surface compiles");
    if(!compiled.ok) {
        for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
            std::printf("L2c-D08 diagnostic %d:%d %s\n", diagnostic.line,
                        diagnostic.column, diagnostic.message.c_str());
        }
        return;
    }

    static axis::AxisModel members[2];
    static axis::AxisGroup group;
    check(group.add_axis(members[0]) == rt::ErrorCode::ok &&
              group.add_axis(members[1]) == rt::ErrorCode::ok,
          "L2c-D08 path group setup");

    std::array<fb::PathWaypoint, 2> waypoints{};
    for(fb::PathWaypoint &waypoint : waypoints) {
        waypoint.target.size = 2U;
    }
    waypoints[1].target.value[0] = 1.0;
    waypoints[1].target.value[1] = 2.0;
    fb::PathTable path_data{};
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-D08 path registry program loads");
    check(instance.bind_path_description("missing", waypoints.data(),
                                         waypoints.size()) ==
              st::BindingError::unknown &&
              instance.bind_path_description("Description", nullptr,
                                             waypoints.size()) ==
                  st::BindingError::null_target &&
              instance.bind_path_description("Description", waypoints.data(),
                                             1U) ==
                  st::BindingError::invalid_value &&
              instance.bind_path_description("Description", waypoints.data(),
                                             33U) ==
                  st::BindingError::invalid_value,
          "L2c-D08 path description binding validates name pointer and count");
    std::array<fb::PathWaypoint, 2> invalid = waypoints;
    invalid[1].target.size = 1U;
    check(instance.bind_path_description("Description", invalid.data(),
                                         invalid.size()) ==
              st::BindingError::invalid_value,
          "L2c-D08 path description requires one stable axis arity");
    invalid = waypoints;
    invalid[0].velocity = std::numeric_limits<double>::quiet_NaN();
    check(instance.bind_path_description("Description", invalid.data(),
                                         invalid.size()) ==
              st::BindingError::invalid_value,
          "L2c-D08 path description rejects invalid waypoint dynamics");

    check(instance.bind_group("G", &group) == st::BindingError::ok &&
              instance.bind_path_table("Data", &path_data) ==
                  st::BindingError::ok &&
              instance.bind_path_description("Description", waypoints.data(),
                                             waypoints.size()) ==
                  st::BindingError::ok,
          "L2c-D08 group path data and owned description bind");
    check(instance.bind_path_description("DESCRIPTION", waypoints.data(),
                                         waypoints.size()) ==
              st::BindingError::duplicate,
          "L2c-D08 path description duplicate binding is rejected");
    check(instance.value_i64(instance.find("Data")) == 1 &&
              instance.value_i64(instance.find("Description")) == 1,
          "L2c-D08 ST path variables contain 1-based handles only");
    waypoints[0].target.value[0] = 99.0;
    check(instance.scan(256) == st::ScanError::ok,
          "L2c-D08 PathSelect handles round-trip without VM fault");
    check(instance.value_i64(instance.find("Data")) == 1 &&
              instance.value_i64(instance.find("Description")) == 1 &&
              instance.value_bool(instance.find("Selected")) &&
              !instance.value_bool(instance.find("Error")) &&
              path_data.count == 2U && path_data.handle != 0U &&
              path_data.waypoints[0].target.value[0] == 0.0,
          "L2c-D08 PathSelect uses the immutable VM-owned description copy");
    check(instance.bind_path_description("Description", waypoints.data(),
                                         waypoints.size()) ==
              st::BindingError::locked,
          "L2c-D08 path description registry locks after first scan");

    std::vector<std::uint64_t> empty_storage =
        instance_storage(compiled.program);
    st::Instance empty;
    check(empty.load(compiled.program,
                     reinterpret_cast<unsigned char *>(empty_storage.data()),
                     empty_storage.size() * sizeof(empty_storage[0]),
                     1000000) == rt::ErrorCode::ok &&
              empty.bind_group("G", &group) == st::BindingError::ok &&
              empty.scan(256) == st::ScanError::ok &&
              empty.value_i64(empty.find("Data")) == 0 &&
              empty.value_i64(empty.find("Description")) == 0 &&
              empty.value_bool(empty.find("Error")),
          "L2c-D08 handle 0 reaches PathSelect as canonical empty refs");

    constexpr st::FbType select_type = st::FbType::mc_path_select;
    constexpr st::FbType move_type = st::FbType::mc_move_path;
    check(st::generated::st_binding_can_store_sequence(select_type, 1U) &&
              st::generated::st_binding_can_load_sequence(select_type, 1U) &&
              st::generated::st_binding_can_store_sequence(select_type, 2U) &&
              st::generated::st_binding_can_load_sequence(select_type, 2U) &&
              st::generated::st_binding_can_store_sequence(move_type, 1U) &&
              st::generated::st_binding_can_load_sequence(move_type, 1U),
          "L2c-D08 path refs use bidirectional registry sequence codecs");
    fb::FbMovePath native_move;
    check(st::generated::st_binding_store_scalar(move_type, 3U, &native_move,
                                                 3U) &&
              st::generated::st_binding_store_scalar(move_type, 4U,
                                                     &native_move, 5U) &&
              st::generated::st_binding_store_scalar(move_type, 5U,
                                                     &native_move, 4U) &&
              st::generated::st_binding_store_scalar(
                  move_type, 6U, &native_move,
                  st::generated::st_binding_double_bits(0.25)) &&
              native_move.coord_system == axis::CoordSystem::pcs &&
              native_move.buffer_mode == axis::BufferMode::blending_high &&
              native_move.transition_mode ==
                  axis::TransitionMode::max_corner_deviation &&
              native_move.transition_parameter == 0.25,
          "L2c-D08 MovePath optional inputs reach exact native fields");
}

void direct_jog_wait_adapters_and_task_period_are_exact()
{
    constexpr st::FbType absolute_type =
        st::FbType::mc_move_direct_absolute;
    constexpr st::FbType relative_type =
        st::FbType::mc_move_direct_relative;
    fb::FbMoveDirectAbsolute absolute;
    fb::FbMoveDirectRelative relative;
    check(st::generated::st_binding_store_scalar(absolute_type, 7U, &absolute,
                                                 3U) &&
              st::generated::st_binding_store_scalar(absolute_type, 8U,
                                                     &absolute, 5U) &&
              st::generated::st_binding_store_scalar(
                  absolute_type, 9U, &absolute,
                  st::generated::st_binding_double_bits(1.25)) &&
              st::generated::st_binding_store_scalar(absolute_type, 10U,
                                                     &absolute, 4U) &&
              st::generated::st_binding_store_scalar(
                  absolute_type, 11U, &absolute,
                  st::generated::st_binding_double_bits(0.5)) &&
              absolute.coord_system == axis::CoordSystem::pcs &&
              absolute.buffer_mode == axis::BufferMode::blending_high &&
              absolute.transition_velocity == 1.25 &&
              absolute.transition_mode ==
                  axis::TransitionMode::max_corner_deviation &&
              absolute.transition_parameter == 0.5,
          "L2c-D09 MoveDirectAbsolute optional inputs reach exact native fields");
    check(st::generated::st_binding_store_scalar(relative_type, 7U, &relative,
                                                 1U) &&
              st::generated::st_binding_store_scalar(relative_type, 8U,
                                                     &relative, 1U) &&
              st::generated::st_binding_store_scalar(
                  relative_type, 9U, &relative,
                  st::generated::st_binding_double_bits(2.5)) &&
              st::generated::st_binding_store_scalar(relative_type, 10U,
                                                     &relative, 3U) &&
              st::generated::st_binding_store_scalar(
                  relative_type, 11U, &relative,
                  st::generated::st_binding_double_bits(0.75)) &&
              relative.coord_system == axis::CoordSystem::mcs &&
              relative.buffer_mode == axis::BufferMode::buffered &&
              relative.transition_velocity == 2.5 &&
              relative.transition_mode == axis::TransitionMode::corner_distance &&
              relative.transition_parameter == 0.75,
          "L2c-D09 MoveDirectRelative optional inputs reach exact native fields");

    fb::FbGroupJog jog;
    constexpr st::FbType jog_type = st::FbType::mc_group_jog;
    check(st::generated::st_binding_store_scalar(
              jog_type, 4U, &jog,
              st::generated::st_binding_double_bits(0.8)) &&
              st::generated::st_binding_store_scalar(
                  jog_type, 5U, &jog,
                  st::generated::st_binding_double_bits(0.6)) &&
              st::generated::st_binding_store_scalar(
                  jog_type, 7U, &jog,
                  st::generated::st_binding_double_bits(12.0)) &&
              st::generated::st_binding_store_scalar(
                  jog_type, 8U, &jog,
                  st::generated::st_binding_double_bits(1.5)) &&
              jog.vel_override == 0.8 && jog.acc_override == 0.6 &&
              jog.max_linear_distance == 12.0 &&
              jog.max_angular_distance == 1.5,
          "L2c-D09 GroupJog limits and overrides reach exact native fields");

    fb::FbGroupJogVector jog_vector;
    constexpr st::FbType vector_type = st::FbType::mc_group_jog_vector;
    check(st::generated::st_binding_store_scalar(
              vector_type, 3U, &jog_vector,
              st::generated::st_binding_double_bits(0.7)) &&
              st::generated::st_binding_store_scalar(
                  vector_type, 4U, &jog_vector,
                  st::generated::st_binding_double_bits(0.4)) &&
              jog_vector.vel_override == 0.7 &&
              jog_vector.acc_override == 0.4,
          "L2c-D09 GroupJogVector overrides reach exact native fields");

    fb::FbGroupWaitTime native_wait;
    constexpr std::int64_t duration_ns = 2500001;
    check(st::generated::st_binding_store_scalar(
              st::FbType::mc_group_wait_time, 2U, &native_wait,
              static_cast<std::uint64_t>(duration_ns)) &&
              native_wait.duration == duration_ns,
          "L2c-D09 GroupWaitTime TIME reaches the native nanosecond field");

    const st::CompileResult compiled = st::compile(
        "PROGRAM wait_period VAR G : GROUP_REF; Delay : MC_GroupWaitTime; "
        "Done : BOOL; Busy : BOOL; Error : BOOL; END_VAR "
        "Delay(AxesGroup := G, Execute := TRUE, Duration := T#2500001ns, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "Done := Delay.Done; Busy := Delay.Busy; Error := Delay.Error; "
        "END_PROGRAM");
    check(compiled.ok,
          "L2c-D09 GroupWaitTime standard TIME surface compiles");
    if(!compiled.ok) return;

    axis::AxisModel member;
    axis::AxisGroup group;
    check(group.add_axis(member) == rt::ErrorCode::ok &&
              member.set_power(true) == rt::ErrorCode::ok,
          "L2c-D09 wait group setup");
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    constexpr std::int64_t task_period_ns = 2000000;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), task_period_ns) ==
              rt::ErrorCode::ok &&
              instance.bind_group("G", &group) == st::BindingError::ok &&
              group.task_cycle_period_ns() == task_period_ns &&
              group.enable() == rt::ErrorCode::ok,
          "L2c-D09 group binding installs the VM task period before enable");
    check(instance.scan(128) == st::ScanError::ok &&
              instance.value_bool(instance.find("Busy")) &&
              !instance.value_bool(instance.find("Done")) &&
              !instance.value_bool(instance.find("Error")),
          "L2c-D09 wait starts with the configured non-default task period");
    group.cycle();
    check(instance.scan(128) == st::ScanError::ok &&
              instance.value_bool(instance.find("Busy")) &&
              !instance.value_bool(instance.find("Done")),
          "L2c-D09 ceil wait remains busy after its first task cycle");
    group.cycle();
    check(instance.scan(128) == st::ScanError::ok &&
              instance.value_bool(instance.find("Done")) &&
              !instance.value_bool(instance.find("Busy")) &&
              !instance.value_bool(instance.find("Error")),
          "L2c-D09 ceil wait completes after exactly two task cycles");

}

void gear_phasing_cam_adapters_and_lifecycle_are_exact()
{
    const auto bits = [](double value) {
        return st::generated::st_binding_double_bits(value);
    };

    fb::FbGearIn gear;
    constexpr st::FbType gear_type = st::FbType::mc_gear_in;
    check(st::generated::st_binding_store_scalar(gear_type, 7U, &gear,
                                                 bits(2.0)) &&
              st::generated::st_binding_store_scalar(gear_type, 8U, &gear,
                                                     bits(3.0)) &&
              st::generated::st_binding_store_scalar(gear_type, 9U, &gear,
                                                     bits(4.0)) &&
              gear.acceleration == 2.0 && gear.deceleration == 3.0 &&
              gear.jerk == 4.0,
          "L2c-D10 GearIn dynamics reach exact native fields");

    fb::FbGearInPos gear_pos;
    constexpr st::FbType gear_pos_type = st::FbType::mc_gear_in_pos;
    check(st::generated::st_binding_store_scalar(gear_pos_type, 8U,
                                                 &gear_pos, 1U) &&
              st::generated::st_binding_store_scalar(gear_pos_type, 11U,
                                                     &gear_pos, bits(5.0)) &&
              st::generated::st_binding_store_scalar(gear_pos_type, 12U,
                                                     &gear_pos, bits(6.0)) &&
              st::generated::st_binding_store_scalar(gear_pos_type, 13U,
                                                     &gear_pos, bits(7.0)) &&
              gear_pos.sync_mode == axis::SyncMode::catch_up &&
              gear_pos.acceleration == 5.0 &&
              gear_pos.deceleration == 6.0 && gear_pos.jerk == 7.0,
          "L2c-D10 GearInPos sync mode and dynamics reach exact native fields");

    fb::FbPhasingAbsolute phase_absolute;
    fb::FbPhasingRelative phase_relative;
    constexpr st::FbType phase_absolute_type =
        st::FbType::mc_phasing_absolute;
    constexpr st::FbType phase_relative_type =
        st::FbType::mc_phasing_relative;
    std::uint64_t phase_bits = 0;
    phase_absolute.absolute_phase_shift = 8.5;
    phase_relative.covered_phase_shift = 9.5;
    check(st::generated::st_binding_store_scalar(
              phase_absolute_type, 5U, &phase_absolute, bits(1.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_absolute_type, 6U, &phase_absolute, bits(2.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_absolute_type, 7U, &phase_absolute, bits(3.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_absolute_type, 8U, &phase_absolute, 5U) &&
              phase_absolute.acceleration == 1.0 &&
              phase_absolute.deceleration == 2.0 &&
              phase_absolute.jerk == 3.0 &&
              phase_absolute.buffer_mode ==
                  axis::BufferMode::blending_high &&
              st::generated::st_binding_load_scalar(
                  phase_absolute_type, 15U, &phase_absolute, phase_bits) &&
              st::generated::st_binding_double_from_bits(phase_bits) == 8.5,
          "L2c-D10 PhasingAbsolute dynamics and phase output are exact");
    check(st::generated::st_binding_store_scalar(
              phase_relative_type, 5U, &phase_relative, bits(4.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_relative_type, 6U, &phase_relative, bits(5.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_relative_type, 7U, &phase_relative, bits(6.0)) &&
              st::generated::st_binding_store_scalar(
                  phase_relative_type, 8U, &phase_relative, 1U) &&
              phase_relative.acceleration == 4.0 &&
              phase_relative.deceleration == 5.0 &&
              phase_relative.jerk == 6.0 &&
              phase_relative.buffer_mode == axis::BufferMode::buffered &&
              st::generated::st_binding_load_scalar(
                  phase_relative_type, 15U, &phase_relative, phase_bits) &&
              st::generated::st_binding_double_from_bits(phase_bits) == 9.5,
          "L2c-D10 PhasingRelative dynamics and phase output are exact");

    const st::CompileResult sync_program = st::compile(
        "PROGRAM sync_lifecycle VAR Master : AXIS_REF; Slave : AXIS_REF; "
        "Gear : MC_GearIn; Pos : MC_GearInPos; "
        "Absolute : MC_PhasingAbsolute; Relative : MC_PhasingRelative; "
        "Fire : BOOL; Error : BOOL; END_VAR Fire := NOT Fire; "
        "Gear(Master := Master, Slave := Slave, Execute := Fire, "
        "Acceleration := 1.0, Deceleration := 2.0, Jerk := 3.0); "
        "Pos(Master := Master, Slave := Slave, Execute := Fire, "
        "SyncMode := MC_SYNC_MODE#catch_up, Acceleration := 4.0, "
        "Deceleration := 5.0, Jerk := 6.0); "
        "Absolute(Master := Master, Slave := Slave, Execute := Fire, "
        "Acceleration := 7.0, Deceleration := 8.0, Jerk := 9.0, "
        "BufferMode := MC_BUFFER_MODE#buffered); "
        "Relative(Master := Master, Slave := Slave, Execute := Fire, "
        "Acceleration := 10.0, Deceleration := 11.0, Jerk := 12.0, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "Error := Gear.Error OR Pos.Error OR Absolute.Error OR Relative.Error; "
        "END_PROGRAM");
    check(sync_program.ok,
          "L2c-D10 Gear and Phasing stable surfaces compile");
    if(sync_program.ok) {
        std::vector<std::uint64_t> sync_storage =
            instance_storage(sync_program.program);
        st::Instance sync_instance;
        check(sync_instance.load(
                  sync_program.program,
                  reinterpret_cast<unsigned char *>(sync_storage.data()),
                  sync_storage.size() * sizeof(sync_storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  sync_instance.scan(512) == st::ScanError::ok &&
                  sync_instance.value_bool(sync_instance.find("Error")),
              "L2c-D10 rising edge reaches native Gear and Phasing validation");
        check(sync_instance.scan(512) == st::ScanError::ok &&
                  !sync_instance.value_bool(sync_instance.find("Error")),
              "L2c-D10 falling edge clears terminal Gear and Phasing errors");
    }

    fb::CamSwitchAction action{};
    action.track_number = 1U;
    action.on_position = -1.0;
    action.off_position = 1.0;
    action.axis_direction = fb::CamSwitchAction::AxisDirection::both;
    action.cam_switch_mode = fb::CamSwitchAction::Mode::position;
    bool output_levels[axis::AxisModel::DigitalOutputCount]{};
    std::array<fb::CamTrackOption,
               axis::AxisModel::DigitalOutputCount> track_options{};
    track_options[0].on_compensation_ns = 100;
    track_options[0].off_compensation_ns = 200;

    fb::FbDigitalCamSwitch digital_cam;
    constexpr st::FbType digital_cam_type =
        st::FbType::mc_digital_cam_switch;
    check(st::generated::st_binding_store_sequence(
              digital_cam_type, 1U, &digital_cam,
              {&action, 1U, false}) &&
              st::generated::st_binding_store_sequence(
                  digital_cam_type, 2U, &digital_cam,
                  {output_levels, axis::AxisModel::DigitalOutputCount,
                   false}) &&
              st::generated::st_binding_store_sequence(
                  digital_cam_type, 3U, &digital_cam,
                  {track_options.data(), track_options.size(), false}) &&
              st::generated::st_binding_store_scalar(
                  digital_cam_type, 5U, &digital_cam, 1U) &&
              st::generated::st_binding_store_scalar(
                  digital_cam_type, 6U, &digital_cam, 1U) &&
              digital_cam.switches.data == &action &&
              digital_cam.outputs.data == output_levels &&
              digital_cam.track_options.data == track_options.data() &&
              digital_cam.enable_mask == 1U &&
              digital_cam.value_source == axis::MasterValueSource::actual,
          "L2c-D11 DigitalCamSwitch sequences and controls are exact");

    fb::FbCamTableSelect table_select;
    constexpr st::FbType table_select_type =
        st::FbType::mc_cam_table_select;
    check(st::generated::st_binding_store_scalar(
              table_select_type, 4U, &table_select, 1U) &&
              st::generated::st_binding_store_scalar(
                  table_select_type, 5U, &table_select, 0U) &&
              st::generated::st_binding_store_scalar(
                  table_select_type, 6U, &table_select, 0U) &&
              st::generated::st_binding_store_scalar(
                  table_select_type, 7U, &table_select, 1U) &&
              table_select.periodic && !table_select.master_absolute &&
              !table_select.slave_absolute &&
              table_select.execution_mode == axis::ExecutionMode::queued,
          "L2c-D11 CamTableSelect controls reach exact native fields");

    fb::FbCamIn cam_in;
    constexpr st::FbType cam_in_type = st::FbType::mc_cam_in;
    cam_in.end_of_profile = true;
    std::uint64_t end_of_profile = 0;
    check(st::generated::st_binding_store_scalar(cam_in_type, 10U, &cam_in,
                                                 1U) &&
              st::generated::st_binding_store_scalar(cam_in_type, 12U,
                                                     &cam_in, 77U) &&
              cam_in.start_mode == axis::CamStartMode::relative &&
              cam_in.cam_table_id == 77U &&
              st::generated::st_binding_load_scalar(
                  cam_in_type, 20U, &cam_in, end_of_profile) &&
              end_of_profile == 1U,
          "L2c-D11 CamIn start mode ID and profile output are exact");

    const st::CompileResult cam_program = st::compile(
        "PROGRAM cam_handles VAR AxisX : AXIS_REF; "
        "Switches : MC_CAM_SWITCH_TABLE_VIEW; "
        "Outputs : MC_CAM_SWITCH_OUTPUTS_VIEW; "
        "Options : MC_CAM_TRACK_OPTIONS_VIEW; Cam : MC_DigitalCamSwitch; "
        "Tick : DINT; EnableCmd : BOOL; InOperation : BOOL; Error : BOOL; "
        "END_VAR EnableCmd := Tick = 0; "
        "Cam(Axis := AxisX, Switches := Switches, Outputs := Outputs, "
        "TrackOptions := Options, Enable := EnableCmd, EnableMask := 1, "
        "ValueSource := MC_MASTER_VALUE_SOURCE#command); "
        "InOperation := Cam.InOperation; Error := Cam.Error; "
        "Tick := Tick + 1; END_PROGRAM");
    check(cam_program.ok,
          "L2c-D11 typed DigitalCamSwitch handle surface compiles");
    if(!cam_program.ok) return;

    axis::AxisModel cam_axis;
    std::vector<std::uint64_t> cam_storage =
        instance_storage(cam_program.program);
    st::Instance cam_instance;
    check(cam_instance.load(
              cam_program.program,
              reinterpret_cast<unsigned char *>(cam_storage.data()),
              cam_storage.size() * sizeof(cam_storage[0]), 2000000) ==
              rt::ErrorCode::ok &&
              cam_instance.bind_axis("AxisX", &cam_axis) ==
                  st::BindingError::ok &&
              cam_instance.bind_cam_switch_table("Switches", &action, 1U) ==
                  st::BindingError::ok &&
              cam_instance.bind_cam_switch_outputs(
                  "Outputs", output_levels,
                  axis::AxisModel::DigitalOutputCount) ==
                  st::BindingError::ok &&
              cam_instance.bind_cam_track_options(
                  "Options", track_options.data(), track_options.size()) ==
                  st::BindingError::ok,
          "L2c-D11 all DigitalCamSwitch registries bind fixed typed handles");
    check(cam_instance.value_i64(cam_instance.find("Switches")) == 1 &&
              cam_instance.value_i64(cam_instance.find("Outputs")) == 1 &&
              cam_instance.value_i64(cam_instance.find("Options")) == 1,
          "L2c-D11 DigitalCamSwitch ST variables contain handles only");
    check(cam_instance.scan(256) == st::ScanError::ok && output_levels[0] &&
              cam_instance.value_bool(cam_instance.find("InOperation")) &&
              !cam_instance.value_bool(cam_instance.find("Error")) &&
              cam_instance.value_i64(cam_instance.find("Outputs")) == 1,
          "L2c-D11 enabled DigitalCamSwitch round-trips views and drives output");
    check(cam_instance.scan(256) == st::ScanError::ok && !output_levels[0] &&
              !cam_instance.value_bool(cam_instance.find("InOperation")) &&
              !cam_instance.value_bool(cam_instance.find("Error")),
          "L2c-D11 disable lifecycle releases registered outputs");
    check(cam_instance.bind_cam_switch_outputs(
              "Outputs", output_levels,
              axis::AxisModel::DigitalOutputCount) == st::BindingError::locked,
          "L2c-D11 DigitalCamSwitch registries lock after first scan");
}

void group_ref_is_opaque_to_st()
{
    const st::CompileResult axis_copy = st::compile(
        "PROGRAM p VAR Left : AXIS_REF; Right : AXIS_REF; END_VAR "
        "Left := Right; END_PROGRAM");
    check(!axis_copy.ok, "L2c-D01 AXIS_REF cannot be copied in ST");
    check(has_code(axis_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 AXIS_REF copy has stable sema_type_mismatch");

    const st::CompileResult copy = st::compile(
        "PROGRAM p VAR Left : GROUP_REF; Right : GROUP_REF; END_VAR "
        "Left := Right; END_PROGRAM");
    check(!copy.ok, "L2c-D01 GROUP_REF cannot be copied in ST");
    check(has_code(copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 GROUP_REF copy has stable sema_type_mismatch");

    const st::CompileResult axis_init = st::compile(
        "PROGRAM p VAR AxisX : AXIS_REF := 0; END_VAR END_PROGRAM");
    check(!axis_init.ok &&
              has_code(axis_init, st::DiagCode::sema_type_mismatch),
          "L2c-D01 AXIS_REF declaration initializer is rejected");
    const st::CompileResult group_init = st::compile(
        "PROGRAM p VAR GroupX : GROUP_REF := 0; END_VAR END_PROGRAM");
    check(!group_init.ok &&
              has_code(group_init, st::DiagCode::sema_type_mismatch),
          "L2c-D01 GROUP_REF declaration initializer is rejected");

    const st::CompileResult sequence_copy = st::compile(
        "PROGRAM p VAR Left : MC_TIME_POSITION; "
        "Right : MC_TIME_POSITION; END_VAR Left := Right; END_PROGRAM");
    check(!sequence_copy.ok &&
              has_code(sequence_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 registry-backed sequence handles cannot be copied in ST");
    const st::CompileResult sequence_arithmetic = st::compile(
        "PROGRAM p VAR Position : MC_TIME_POSITION; Value : ULINT; END_VAR "
        "Value := Position + 1; END_PROGRAM");
    check(!sequence_arithmetic.ok &&
              has_code(sequence_arithmetic, st::DiagCode::sema_type_mismatch),
          "L2c-D01 registry-backed handles cannot enter ST arithmetic");
    const st::CompileResult sequence_init = st::compile(
        "PROGRAM p VAR Position : MC_TIME_POSITION := 0; END_VAR END_PROGRAM");
    check(!sequence_init.ok &&
              has_code(sequence_init, st::DiagCode::sema_type_mismatch),
          "L2c-D01 registry-backed handle initializers are rejected");

    const st::CompileResult nested_copy = st::compile(
        "TYPE Holder : STRUCT Axis : AXIS_REF; Group : GROUP_REF; "
        "END_STRUCT END_TYPE "
        "PROGRAM p VAR Left : Holder; Right : Holder; END_VAR "
        "Left.Axis := Right.Axis; Left.Group := Right.Group; END_PROGRAM");
    check(!nested_copy.ok &&
              has_code(nested_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 aggregate reference fields remain opaque to ST assignment");
    const st::CompileResult aggregate_copy = st::compile(
        "TYPE Holder : STRUCT Axis : AXIS_REF; Group : GROUP_REF; "
        "END_STRUCT END_TYPE "
        "PROGRAM p VAR Left : Holder; Right : Holder; END_VAR "
        "Left := Right; END_PROGRAM");
    check(!aggregate_copy.ok &&
              has_code(aggregate_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 aggregates containing references cannot copy handles");

    // D03 forbids direct language mutation, not the standard Part 4 command
    // path. These three commands remain ordinary members of the generated
    // 68-FB closure and must not be hidden behind excluded.
    const std::array<std::string_view, 3> controlled_commands = {
        "mc_addaxistogroup", "mc_removeaxisfromgroup", "mc_ungroupallaxes"};
    const st::BindingManifest &manifest = st::binding_manifest();
    for(const std::string_view command : controlled_commands) {
        bool found = false;
        for(std::size_t index = 0; index < manifest.fb_count(); ++index) {
            const st::BindingFbDesc &fb_desc = manifest.fb(index);
            if(lower_ascii(fb_desc.lower_name) == command) {
                found = true;
                check(fb_desc.set == st::BindingSet::plcopen_part4 &&
                          !fb_desc.excluded &&
                          st::binding_fb_registered(fb_desc.id),
                      "L2c-D03 group mutation is exposed only by registered Part 4 FBs");
                break;
            }
        }
        check(found, "L2c-D03 controlled Part 4 group command is in the manifest");
    }
}

void tagged_kin_transform_round_trips_only_through_fb_output()
{
    const st::CompileResult copy = st::compile(
        "PROGRAM p VAR Left : MC_KIN_TRANSFORM_REF; "
        "Right : MC_KIN_TRANSFORM_REF; END_VAR Left := Right; END_PROGRAM");
    check(!copy.ok && has_code(copy, st::DiagCode::sema_type_mismatch),
          "L2c-D06 tagged references remain opaque to variable copy");

    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR G : GROUP_REF; K : MC_KIN_TRANSFORM_REF; "
        "Out : MC_KIN_TRANSFORM_REF; Set : MC_SetKinTransform; "
        "Read : MC_ReadKinTransform; Done : BOOL; Valid : BOOL; "
        "Error : BOOL; END_VAR "
        "Set(AxesGroup := G, Execute := TRUE, KinTransform := K, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately); "
        "Read(AxesGroup := G, Enable := TRUE); "
        "Out := Read.KinTransform; Done := Set.Done; "
        "Valid := Read.Valid; Error := Set.Error OR Read.Error; "
        "END_PROGRAM");
    check(compiled.ok,
          "L2c-D06 exact tagged FB output assignment compiles");
    if(!compiled.ok) return;

    static axis::AxisModel members[2];
    static axis::AxisGroup group;
    static const double scales[2] = {2.0, 3.0};
    static const double offsets[2] = {0.5, -0.25};
    static const kin::CartesianGantry gantry(2, scales, offsets);
    check(group.add_axis(members[0]) == rt::ErrorCode::ok &&
              group.add_axis(members[1]) == rt::ErrorCode::ok,
          "L2c-D06 kinematics group setup");
    members[0].set_power(true);
    members[1].set_power(true);
    check(group.enable() == rt::ErrorCode::ok,
          "L2c-D06 kinematics group reaches standby");

    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok &&
              instance.bind_group("G", &group) == st::BindingError::ok,
          "L2c-D06 tagged reference program loads and binds group");
    check(instance.bind_kin_transform("K", {}) ==
              st::BindingError::invalid_value &&
              instance.bind_kin_transform(
                  "K", {axis::KinTransformKind::kinematics, &gantry,
                        reinterpret_cast<const kin::PoseKinematics *>(&gantry)}) ==
                  st::BindingError::invalid_value,
          "L2c-D06 none and mixed host tags are rejected");
    check(instance.bind_kin_transform(
              "K", {axis::KinTransformKind::kinematics, &gantry, nullptr}) ==
              st::BindingError::ok,
          "L2c-D06 valid host kinematics gets a stable handle");
    check(instance.value_i64(instance.find("K")) == 1 &&
              instance.value_i64(instance.find("Out")) == 0,
          "L2c-D06 tagged handle is 1-based and output starts empty");
    check(instance.scan(512) == st::ScanError::ok,
          "L2c-D06 tagged input and output execute without VM fault");
    check(group.kinematics_plugin() == &gantry &&
              instance.value_bool(instance.find("Done")) &&
              instance.value_bool(instance.find("Valid")) &&
              !instance.value_bool(instance.find("Error")) &&
              instance.value_i64(instance.find("Out")) == 1,
          "L2c-D06 KinTransform round-trips through Set and Read FBs");
    check(instance.bind_kin_transform(
              "K", {axis::KinTransformKind::kinematics, &gantry, nullptr}) ==
              st::BindingError::locked,
          "L2c-D06 tagged registry locks after first scan");
}

void ref_aliases_share_the_axis_registry_without_pointer_bits()
{
    const st::CompileResult alias_copy = st::compile(
        "PROGRAM p VAR Left : MC_INPUT_REF; Right : MC_INPUT_REF; "
        "END_VAR Left := Right; END_PROGRAM");
    check(!alias_copy.ok &&
              has_code(alias_copy, st::DiagCode::sema_type_mismatch),
          "L2c-D07 reference aliases remain opaque to ST copy");

    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR A : AXIS_REF; I : MC_INPUT_REF; O : MC_OUTPUT_REF; "
        "ReadI : MC_ReadDigitalInput; ReadO : MC_ReadDigitalOutput; "
        "InputValue : BOOL; OutputValue : BOOL; END_VAR "
        "ReadI(Input := I, Enable := TRUE, InputNumber := 2); "
        "ReadO(Output := O, Enable := TRUE, OutputNumber := 3); "
        "InputValue := ReadI.Value; OutputValue := ReadO.Value; "
        "END_PROGRAM");
    check(compiled.ok, "L2c-D07 axis reference aliases compile");
    if(!compiled.ok) {
        for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
            std::printf("L2c-D07 diagnostic %d:%d %s\n", diagnostic.line,
                        diagnostic.column, diagnostic.message.c_str());
        }
        return;
    }

    static axis::AxisModel target;
    check(target.set_digital_input(2, true) == rt::ErrorCode::ok &&
              target.set_digital_output(3, true) == rt::ErrorCode::ok,
          "L2c-D07 digital alias target setup");
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok &&
              instance.bind_axis("A", &target) == st::BindingError::ok &&
              instance.bind_axis("I", &target) == st::BindingError::ok &&
              instance.bind_axis("O", &target) == st::BindingError::ok,
          "L2c-D07 aliases bind through the unified axis registry");
    check(instance.value_i64(instance.find("A")) == 1 &&
              instance.value_i64(instance.find("I")) == 2 &&
              instance.value_i64(instance.find("O")) == 3,
          "L2c-D07 aliases store deterministic 1-based handles");
    check(instance.scan(256) == st::ScanError::ok &&
              instance.value_bool(instance.find("InputValue")) &&
              instance.value_bool(instance.find("OutputValue")),
          "L2c-D07 alias handles resolve to native AxisModel pointers");
}

void torque_extension_pins_use_the_vm_task_period()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR A : AXIS_REF; T : MC_TorqueControl; "
        "InTorque : BOOL; Error : BOOL; END_VAR "
        "T(Axis := A, Execute := TRUE, ContinuousUpdate := TRUE, "
        "Torque := 2.0, TorqueRamp := 500.0, Velocity := 4.0, "
        "Acceleration := 3.0, Deceleration := 2.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#negative, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "InTorque := T.InTorque; Error := T.Error; END_PROGRAM");
    check(compiled.ok, "L2c-D08 TorqueControl extension pins compile");
    if(!compiled.ok) return;

    static axis::AxisModel target;
    target.set_power(true);
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 2000000) ==
              rt::ErrorCode::ok &&
              instance.bind_axis("A", &target) == st::BindingError::ok &&
              instance.scan(512) == st::ScanError::ok,
          "L2c-D08 TorqueControl loads through generated task-period lifecycle");
    check(!instance.value_bool(instance.find("InTorque")) &&
              !instance.value_bool(instance.find("Error")) &&
              target.command_torque() == 0.0,
          "L2c-D08 positive TorqueRamp starts without a setpoint step");
    target.cycle();
    check(instance.scan(512) == st::ScanError::ok &&
              target.command_torque() == -1.0 &&
              !instance.value_bool(instance.find("InTorque")),
          "L2c-D08 500 units/s at 2ms advances exactly one unit per cycle");
    target.cycle();
    check(instance.scan(512) == st::ScanError::ok &&
              target.command_torque() == -2.0 &&
              instance.value_bool(instance.find("InTorque")) &&
              target.snapshot().torque_velocity_limit == 4.0 &&
              target.snapshot().torque_acceleration_limit == 3.0 &&
              target.snapshot().torque_deceleration_limit == 2.0 &&
              target.snapshot().torque_jerk_limit == 1.0 &&
              target.snapshot().torque_direction == axis::Direction::negative,
          "L2c-D08 ST and C++ CST setpoint contracts are exact");
}

void unbound_references_report_fb_errors_without_vm_faults()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR AxisX : AXIS_REF; GroupX : GROUP_REF; "
        "Power : MC_Power; Read : MC_GroupReadStatus; "
        "AxisError : BOOL; AxisCode : DINT; GroupError : BOOL; "
        "GroupCode : DINT; END_VAR "
        "Power(Axis := AxisX, Enable := TRUE); "
        "Read(AxesGroup := GroupX, Enable := TRUE); "
        "AxisError := Power.Error; AxisCode := Power.ErrorID; "
        "GroupError := Read.Error; GroupCode := Read.ErrorID; "
        "END_PROGRAM");
    check(compiled.ok, "L2c-A03 unbound reference program compiles");
    if(!compiled.ok) return;
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-A03 unbound reference program loads");
    check(instance.scan(256) == st::ScanError::ok,
          "L2c-A03 unbound references do not fault the VM");
    const int axis_error = instance.find("AxisError");
    const int axis_code = instance.find("AxisCode");
    const int group_error = instance.find("GroupError");
    const int group_code = instance.find("GroupCode");
    check(axis_error >= 0 && instance.value_i64(axis_error) == 1,
          "L2c-A03 unbound AXIS_REF sets FB Error");
    check(group_error >= 0 && instance.value_i64(group_error) == 1,
          "L2c-A03 unbound GROUP_REF sets FB Error");
    check(axis_code >= 0 &&
              instance.value_i64(axis_code) ==
                  static_cast<std::int64_t>(rt::ErrorCode::invalid_argument),
          "L2c-A03 unbound AXIS_REF has stable ErrorID");
    check(group_code >= 0 &&
              instance.value_i64(group_code) ==
                  static_cast<std::int64_t>(rt::ErrorCode::invalid_argument),
          "L2c-A03 unbound GROUP_REF has stable ErrorID");
}

void canonical_enum_pins_replace_legacy_ints()
{
    const st::BindingManifest &manifest = st::binding_manifest();
    bool saw_buffer_mode = false;
    bool saw_direction = false;
    for(std::size_t fb_index = 0; fb_index < manifest.fb_count(); ++fb_index) {
        const st::BindingFbDesc &fb_desc = manifest.fb(fb_index);
        for(std::size_t pin_index = 0; pin_index < fb_desc.pin_count; ++pin_index) {
            const st::BindingPinDesc &pin = fb_desc.pins[pin_index];
            const std::string name = lower_ascii(pin.lower_name);
            if(name != "buffermode" && name != "direction" &&
               name != "switchmode") {
                continue;
            }
            const st::generated::StBindingFbMetadata &fb_metadata =
                st::generated::kStBindingFbs[fb_index];
            const st::generated::StBindingPinMetadata &pin_metadata =
                st::generated::kStBindingPins[fb_metadata.first_pin +
                                              pin_index];
            if(pin_metadata.adapter ==
                   st::generated::StBindingAdapterKind::unresolved ||
               (name == "direction" &&
                pin.type_id == st::binding_type::mc_group_position)) {
                continue;
            }
            const st::TypeDesc *type = manifest.type_table().get(pin.type_id);
            check(type != nullptr && type->kind == st::TypeKind::enum_,
                  "L2c-A05 mode/direction pin is a canonical enum");
            if(type == nullptr) continue;
            if(name == "buffermode") {
                saw_buffer_mode = true;
                check(lower_ascii(type->name) == "mc_buffer_mode",
                      "L2c-A05 BufferMode uses MC_BUFFER_MODE");
            } else if(name == "direction") {
                saw_direction = true;
                const std::string type_name = lower_ascii(type->name);
                check(type_name == "mc_direction" ||
                          type_name == "mc_home_direction",
                      "L2c-A05 Direction uses its canonical enum family");
            } else {
                check(lower_ascii(type->name) == "mc_switch_mode",
                      "L2c-A05 SwitchMode uses MC_SWITCH_MODE");
            }
        }
    }
    check(saw_buffer_mode, "L2c-A05 manifest contains BufferMode pins");
    check(saw_direction, "L2c-A05 manifest contains Direction pins");

    const st::CompileResult canonical = st::compile(kCanonicalAxisProgram);
    check(canonical.ok, "L2c-A05 canonical enum source compiles");
    const st::CompileResult legacy = st::compile(
        "PROGRAM p VAR AxisX : AXIS_REF; Move : MC_MoveAbsolute; END_VAR "
        "Move(Axis := AxisX, Execute := TRUE, ContinuousUpdate := FALSE, "
        "Position := 1.0, Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0, Direction := 0, "
        "BufferMode := 0); END_PROGRAM");
    check(!legacy.ok, "L2c-A05 legacy INT Direction/BufferMode source rejected");
    check(has_code(legacy, st::DiagCode::sema_type_mismatch),
          "L2c-A05 legacy INT source has stable sema_type_mismatch");

    if(canonical.ok) {
        st::Program old_bytecode = canonical.program;
        old_bytecode.format_version = canonical.program.format_version - 1U;
        std::vector<std::uint64_t> storage = instance_storage(old_bytecode);
        st::Instance instance;
        check(instance.load(old_bytecode,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::bytecode_version_mismatch,
              "L2c-A05 old bytecode version is rejected without migration");
    }
}

void axis_st_and_cpp_paths_are_cycle_equivalent()
{
    const st::CompileResult compiled = st::compile(kCanonicalAxisProgram);
    check(compiled.ok, "L2c-A04 canonical MC_MoveAbsolute compiles");
    if(!compiled.ok) return;
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel st_axis;
    axis::AxisModel cpp_axis;
    st_axis.set_power(true);
    cpp_axis.set_power(true);
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-A04 MC_MoveAbsolute ST instance loads");
    check(instance.bind_axis("AxisX", &st_axis) == st::BindingError::ok,
          "L2c-A04 MC_MoveAbsolute ST axis binds");

    fb::FbMoveAbsolute direct;
    direct.axis_ref = &cpp_axis;
    direct.execute = true;
    direct.position = 0.25;
    direct.velocity = 1.0;
    direct.acceleration = 2.0;
    direct.deceleration = 2.0;
    direct.jerk = 10.0;
    direct.direction = axis::Direction::current;
    direct.buffer_mode = axis::BufferMode::aborting;
    direct.call();
    check(instance.scan(512) == st::ScanError::ok,
          "L2c-A04 MC_MoveAbsolute first ST scan");

    bool done = false;
    for(int cycle = 0; cycle < 4000; ++cycle) {
        st_axis.cycle();
        cpp_axis.cycle();
        check(st_axis.snapshot().command_position ==
                  cpp_axis.snapshot().command_position,
              "L2c-A04 MC_MoveAbsolute per-cycle setpoint equivalence");
        direct.call();
        check(instance.scan(512) == st::ScanError::ok,
              "L2c-A04 MC_MoveAbsolute lifecycle scan");
        const int done_index = instance.find("Done");
        const int busy_index = instance.find("Busy");
        const int active_index = instance.find("Active");
        const int error_index = instance.find("Error");
        check(done_index >= 0 &&
                  instance.value_bool(done_index) == direct.outputs.done,
              "L2c-A04 MC_MoveAbsolute Done equivalence");
        check(busy_index >= 0 &&
                  instance.value_bool(busy_index) == direct.outputs.busy,
              "L2c-A04 MC_MoveAbsolute Busy equivalence");
        check(active_index >= 0 &&
                  instance.value_bool(active_index) == direct.outputs.active,
              "L2c-A04 MC_MoveAbsolute Active equivalence");
        check(error_index >= 0 &&
                  instance.value_bool(error_index) == direct.outputs.error,
              "L2c-A04 MC_MoveAbsolute Error equivalence");
        if(direct.outputs.done) {
            done = true;
            break;
        }
    }
    check(done, "L2c-A04 MC_MoveAbsolute equivalent trace reaches Done");
}

void group_st_and_cpp_paths_are_cycle_equivalent()
{
    const st::CompileResult compiled = st::compile(kCanonicalGroupProgram);
    check(compiled.ok, "L2c-A04 canonical MC_GroupReadStatus compiles");
    if(!compiled.ok) return;
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel member;
    axis::AxisGroup group;
    check(group.add_axis(member) == rt::ErrorCode::ok,
          "L2c-A04 group member setup");
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-A04 group ST instance loads");
    check(instance.bind_group("GroupX", &group) == st::BindingError::ok,
          "L2c-A04 GROUP_REF binds");

    fb::FbGroupReadStatus direct;
    direct.group_ref = &group;
    direct.enable = true;
    for(int phase = 0; phase < 2; ++phase) {
        if(phase == 1) {
            member.set_power(true);
            check(group.enable() == rt::ErrorCode::ok,
                  "L2c-A04 group enters standby");
        }
        direct.call();
        check(instance.scan(256) == st::ScanError::ok,
              "L2c-A04 MC_GroupReadStatus lifecycle scan");
        const int valid = instance.find("Valid");
        const int error = instance.find("Error");
        const int disabled = instance.find("Disabled");
        const int standby = instance.find("Standby");
        check(valid >= 0 && instance.value_bool(valid) == direct.valid,
              "L2c-A04 group Valid equivalence");
        check(error >= 0 && instance.value_bool(error) == direct.error,
              "L2c-A04 group Error equivalence");
        check(disabled >= 0 && instance.value_bool(disabled) == direct.disabled,
              "L2c-A04 group Disabled equivalence");
        check(standby >= 0 && instance.value_bool(standby) == direct.standby,
              "L2c-A04 group Standby equivalence");
    }
}

std::string reference_capacity_program(std::size_t axes, std::size_t groups)
{
    std::string source = "PROGRAM refs VAR ";
    for(std::size_t index = 0; index < axes; ++index) {
        source += "A" + std::to_string(index) + " : AXIS_REF; ";
    }
    for(std::size_t index = 0; index < groups; ++index) {
        source += "G" + std::to_string(index) + " : GROUP_REF; ";
    }
    source += "END_VAR END_PROGRAM";
    return source;
}

std::string fb_capacity_program(std::size_t count)
{
    std::string source = "PROGRAM blocks VAR ";
    for(std::size_t index = 0; index < count; ++index) {
        source += "B" + std::to_string(index) + " : R_TRIG; ";
    }
    source += "END_VAR END_PROGRAM";
    return source;
}

void fixed_capacity_boundaries_are_exact()
{
    const st::CompileOptions defaults;
    check(defaults.max_axis_refs == 64, "L2c-A07 default AXIS_REF capacity is 64");
    check(defaults.max_group_refs == 16, "L2c-A07 default GROUP_REF capacity is 16");
    check(defaults.max_fb_instances == 1024,
          "L2c-A07 default FB instance capacity is 1024");

    const st::CompileResult exact_refs =
        st::compile(reference_capacity_program(64, 16));
    check(exact_refs.ok, "L2c-A07 64 axes and 16 groups compile");
    const st::CompileResult too_many_axes =
        st::compile(reference_capacity_program(65, 16));
    check(!too_many_axes.ok &&
              has_code(too_many_axes, st::DiagCode::capacity_exceeded),
          "L2c-A07 65th axis is rejected by stable capacity diagnostic");
    const st::CompileResult too_many_groups =
        st::compile(reference_capacity_program(64, 17));
    check(!too_many_groups.ok &&
              has_code(too_many_groups, st::DiagCode::capacity_exceeded),
          "L2c-A07 17th group is rejected by stable capacity diagnostic");

    st::CompileOptions host_limits;
    host_limits.max_axis_refs = 65;
    host_limits.max_group_refs = 17;
    const st::CompileResult host_refs =
        st::compile(reference_capacity_program(65, 17), host_limits);
    check(!host_refs.ok &&
              has_code(host_refs, st::DiagCode::capacity_exceeded),
          "L2c-A07 CompileOptions cannot widen fixed host registry limits");

    const st::CompileResult exact_fbs = st::compile(fb_capacity_program(1024));
    check(exact_fbs.ok, "L2c-A07 1024 FB instances compile");
    const st::CompileResult too_many_fbs = st::compile(fb_capacity_program(1025));
    check(!too_many_fbs.ok &&
              has_code(too_many_fbs, st::DiagCode::capacity_fb_instances),
          "L2c-A07 1025th FB is rejected by stable capacity diagnostic");
}

void manifest_compile_and_binding_order_are_deterministic()
{
    const std::string manifest_dump = st::binding_manifest().canonical_dump();
    for(int iteration = 0; iteration < 100; ++iteration) {
        check(st::binding_manifest().canonical_dump() == manifest_dump,
              "L2c-A07 binding manifest dump is deterministic");
    }

    const st::CompileResult baseline = st::compile(kCanonicalAxisProgram);
    check(baseline.ok, "L2c-A07 deterministic source compiles");
    if(!baseline.ok) return;
    std::string baseline_types;
    check(baseline.program.types.canonical_dump(baseline_types) == st::TypeError::ok,
          "L2c-A07 binding TypeTable dumps");
    for(int iteration = 0; iteration < 25; ++iteration) {
        const st::CompileResult again = st::compile(kCanonicalAxisProgram);
        std::string again_types;
        check(again.ok && again.program.code == baseline.program.code &&
                  again.program.constants == baseline.program.constants &&
                  again.program.initial_data == baseline.program.initial_data &&
                  again.program.types.canonical_dump(again_types) == st::TypeError::ok &&
                  again_types == baseline_types,
              "L2c-A07 compiler and binding metadata are deterministic");
    }

    const st::CompileResult refs = st::compile(
        "PROGRAM refs VAR A : AXIS_REF; G : GROUP_REF; END_VAR END_PROGRAM");
    check(refs.ok, "L2c-A07 binding-order program compiles");
    if(!refs.ok) return;
    std::vector<std::uint64_t> left_storage = instance_storage(refs.program);
    std::vector<std::uint64_t> right_storage = instance_storage(refs.program);
    st::Instance left;
    st::Instance right;
    axis::AxisModel axis;
    axis::AxisGroup group;
    check(left.load(refs.program,
                    reinterpret_cast<unsigned char *>(left_storage.data()),
                    left_storage.size() * sizeof(left_storage[0]), 1000000) ==
              rt::ErrorCode::ok &&
              right.load(refs.program,
                         reinterpret_cast<unsigned char *>(right_storage.data()),
                         right_storage.size() * sizeof(right_storage[0]), 1000000) ==
                  rt::ErrorCode::ok,
          "L2c-A07 binding-order instances load");
    check(left.bind_axis("A", &axis) == st::BindingError::ok &&
              left.bind_group("G", &group) == st::BindingError::ok &&
              right.bind_group("G", &group) == st::BindingError::ok &&
              right.bind_axis("A", &axis) == st::BindingError::ok,
          "L2c-A07 binding order is accepted");
    check(left.scan(8) == st::ScanError::ok &&
              right.scan(8) == st::ScanError::ok,
          "L2c-A07 binding order has deterministic scan result");
}

void full_binding_scan_is_zero_allocation()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM p VAR AxisX : AXIS_REF; GroupX : GROUP_REF; "
        "Move : MC_MoveAbsolute; Read : MC_GroupReadStatus; END_VAR "
        "Move(Axis := AxisX, Execute := TRUE, ContinuousUpdate := FALSE, "
        "Position := 0.25, Velocity := 1.0, Acceleration := 2.0, "
        "Deceleration := 2.0, Jerk := 10.0, "
        "Direction := MC_DIRECTION#current, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "Read(AxesGroup := GroupX, Enable := TRUE); END_PROGRAM");
    check(compiled.ok, "L2c-A07 RT allocation program compiles");
    if(!compiled.ok) return;
    std::vector<std::uint64_t> storage = instance_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel axis;
    axis::AxisModel member;
    axis::AxisGroup group;
    axis.set_power(true);
    member.set_power(true);
    check(group.add_axis(member) == rt::ErrorCode::ok &&
              group.enable() == rt::ErrorCode::ok,
          "L2c-A07 RT allocation group setup");
    check(instance.load(compiled.program,
                        reinterpret_cast<unsigned char *>(storage.data()),
                        storage.size() * sizeof(storage[0]), 1000000) ==
              rt::ErrorCode::ok,
          "L2c-A07 RT allocation instance loads");
    check(instance.bind_axis("AxisX", &axis) == st::BindingError::ok,
          "L2c-A07 RT allocation axis binds");
    check(instance.bind_group("GroupX", &group) == st::BindingError::ok,
          "L2c-A07 RT allocation group binds");
    check(instance.scan(512) == st::ScanError::ok,
          "L2c-A07 RT allocation warm-up scan");
    axis.cycle();

    g_frozen_allocations = 0;
    g_freeze_allocations = true;
    for(int cycle = 0; cycle < 1000; ++cycle) {
        if(instance.scan(512) != st::ScanError::ok) {
            g_freeze_allocations = false;
            check(false, "L2c-A07 frozen binding scan stays healthy");
            return;
        }
        axis.cycle();
    }
    g_freeze_allocations = false;
    check(g_frozen_allocations == 0,
          "L2c-A07 full binding scan performs zero allocations");
}

void controlled_group_commands_use_ident_object_adapter()
{
    axis::AxisModel member;
    axis::AxisGroup group;
    member.set_power(true);

    const st::CompileResult add_program = st::compile(
        "PROGRAM p VAR G : GROUP_REF; A : AXIS_REF; "
        "Ident : MC_IDENT_IN_GROUP; Add : MC_AddAxisToGroup; "
        "Done : BOOL; Error : BOOL; END_VAR "
        "Ident.index := 7; "
        "Add(AxesGroup := G, Axis := A, Execute := TRUE, "
        "IdentInGroup := Ident); Done := Add.Done; Error := Add.Error; "
        "END_PROGRAM");
    check(add_program.ok,
          "L2c-D03 standard MC_AddAxisToGroup source compiles");
    if(!add_program.ok) return;
    std::vector<std::uint64_t> add_storage =
        instance_storage(add_program.program);
    st::Instance add;
    check(add.load(add_program.program,
                   reinterpret_cast<unsigned char *>(add_storage.data()),
                   add_storage.size() * sizeof(add_storage[0]), 1000000) ==
              rt::ErrorCode::ok &&
              add.bind_group("G", &group) == st::BindingError::ok &&
              add.bind_axis("A", &member) == st::BindingError::ok &&
              add.scan(128) == st::ScanError::ok,
          "L2c-D03 MC_AddAxisToGroup executes through object adapter");
    const int add_done = add.find("Done");
    const int add_error = add.find("Error");
    check(group.member(axis::IdentInGroup{7}) == &member &&
              add_done >= 0 && add.value_bool(add_done) &&
              add_error >= 0 && !add.value_bool(add_error),
          "L2c-D03 MC_AddAxisToGroup preserves IdentInGroup");

    const st::CompileResult remove_program = st::compile(
        "PROGRAM p VAR G : GROUP_REF; Ident : MC_IDENT_IN_GROUP; "
        "Remove : MC_RemoveAxisFromGroup; Done : BOOL; Error : BOOL; END_VAR "
        "Ident.index := 7; "
        "Remove(AxesGroup := G, Execute := TRUE, IdentInGroup := Ident); "
        "Done := Remove.Done; Error := Remove.Error; END_PROGRAM");
    check(remove_program.ok,
          "L2c-D03 standard MC_RemoveAxisFromGroup source compiles");
    if(!remove_program.ok) return;
    std::vector<std::uint64_t> remove_storage =
        instance_storage(remove_program.program);
    st::Instance remove;
    check(remove.load(remove_program.program,
                      reinterpret_cast<unsigned char *>(remove_storage.data()),
                      remove_storage.size() * sizeof(remove_storage[0]),
                      1000000) == rt::ErrorCode::ok &&
              remove.bind_group("G", &group) == st::BindingError::ok &&
              remove.scan(128) == st::ScanError::ok,
          "L2c-D03 MC_RemoveAxisFromGroup executes through object adapter");
    check(group.member_count() == 0 &&
              remove.value_bool(remove.find("Done")) &&
              !remove.value_bool(remove.find("Error")),
          "L2c-D03 MC_RemoveAxisFromGroup removes by stable identifier");

    check(group.add_axis(member, axis::IdentInGroup{11}) ==
              rt::ErrorCode::ok,
          "L2c-D03 ungroup setup");
    const st::CompileResult ungroup_program = st::compile(
        "PROGRAM p VAR G : GROUP_REF; Ungroup : MC_UngroupAllAxes; "
        "Done : BOOL; Error : BOOL; END_VAR "
        "Ungroup(AxesGroup := G, Execute := TRUE); "
        "Done := Ungroup.Done; Error := Ungroup.Error; END_PROGRAM");
    check(ungroup_program.ok,
          "L2c-D03 standard MC_UngroupAllAxes source compiles");
    if(!ungroup_program.ok) return;
    std::vector<std::uint64_t> ungroup_storage =
        instance_storage(ungroup_program.program);
    st::Instance ungroup;
    check(ungroup.load(ungroup_program.program,
                       reinterpret_cast<unsigned char *>(ungroup_storage.data()),
                       ungroup_storage.size() * sizeof(ungroup_storage[0]),
                       1000000) == rt::ErrorCode::ok &&
              ungroup.bind_group("G", &group) == st::BindingError::ok &&
              ungroup.scan(128) == st::ScanError::ok,
          "L2c-D03 MC_UngroupAllAxes executes through GROUP_REF");
    check(group.member_count() == 0 &&
              ungroup.value_bool(ungroup.find("Done")) &&
              !ungroup.value_bool(ungroup.find("Error")),
          "L2c-D03 MC_UngroupAllAxes atomically clears the group");
}

} // namespace

int main()
{
    manifest_closure_uses_one_production_schema();
    generated_lifecycle_covers_every_declared_fb();
    aggregate_fb_outputs_use_the_object_codec();
    reference_target_tags_are_inferred_from_manifest();
    host_binding_lifecycle_and_error_mapping();
    host_sequence_binding_lifecycle_and_error_mapping();
    path_description_registry_and_path_fb_adapters_are_closed();
    direct_jog_wait_adapters_and_task_period_are_exact();
    gear_phasing_cam_adapters_and_lifecycle_are_exact();
    group_ref_is_opaque_to_st();
    tagged_kin_transform_round_trips_only_through_fb_output();
    ref_aliases_share_the_axis_registry_without_pointer_bits();
    torque_extension_pins_use_the_vm_task_period();
    unbound_references_report_fb_errors_without_vm_faults();
    canonical_enum_pins_replace_legacy_ints();
    axis_st_and_cpp_paths_are_cycle_equivalent();
    group_st_and_cpp_paths_are_cycle_equivalent();
    fixed_capacity_boundaries_are_exact();
    manifest_compile_and_binding_order_are_deterministic();
    full_binding_scan_is_zero_allocation();
    controlled_group_commands_use_ident_object_adapter();
    if(failures == 0) std::printf("PASS st_l2c_tests\n");
    return failures == 0 ? 0 : 1;
}
