// ST-L2c RED acceptance tests (approved st-l2c-semantics, 2026-07-17).
//
// The production binding manifest is the only FB/pin schema consumed here.
// This file deliberately does not repeat the 134 FB names or their pins: the
// closure test walks the generated manifest and then probes the real registry.

#include <array>
#include <cstdio>
#include <cstdlib>
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
    "Moving : BOOL; Stopping : BOOL; ErrorStop : BOOL; "
    "Interrupted : BOOL; END_VAR\n"
    "Read(AxesGroup := GroupX, Enable := TRUE);\n"
    "Valid := Read.Valid; Error := Read.Error; ErrorID := Read.ErrorID; "
    "Disabled := Read.Disabled; Standby := Read.Standby; "
    "Moving := Read.Moving; Stopping := Read.Stopping; "
    "ErrorStop := Read.ErrorStop; Interrupted := Read.Interrupted;\n"
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
        const bool fb_registered = st::binding_fb_registered(fb_desc.id);
        check(fb_registered, "L2c-A01 FB has a production registry entry");
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
            const bool pin_registered =
                st::binding_pin_registered(fb_desc.id, pin.id);
            check(pin_registered, "L2c-A02 pin has a production dispatcher entry");
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
    check(declared_fbs == generated_fbs && generated_fbs == registered_fbs &&
              registered_fbs == tested_fbs + excluded_fbs,
          "L2c-A01 declared == generated == registered == tested + excluded FBs");
    check(declared_pins == generated_pins && generated_pins == registered_pins &&
              registered_pins == tested_pins + excluded_pins,
          "L2c-A02 declared == generated == registered == tested + excluded pins");
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
            check(type != nullptr, "L2c-A02 every pin TypeId resolves");
            if(type != nullptr && type->kind == st::TypeKind::ref) {
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

void group_ref_is_opaque_to_st()
{
    const st::CompileResult copy = st::compile(
        "PROGRAM p VAR Left : GROUP_REF; Right : GROUP_REF; END_VAR "
        "Left := Right; END_PROGRAM");
    check(!copy.ok, "L2c-D01 GROUP_REF cannot be copied in ST");
    check(has_code(copy, st::DiagCode::sema_type_mismatch),
          "L2c-D01 GROUP_REF copy has stable sema_type_mismatch");

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
                  rt::ErrorCode::unsupported,
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

} // namespace

int main()
{
    manifest_closure_uses_one_production_schema();
    reference_target_tags_are_inferred_from_manifest();
    host_binding_lifecycle_and_error_mapping();
    group_ref_is_opaque_to_st();
    unbound_references_report_fb_errors_without_vm_faults();
    canonical_enum_pins_replace_legacy_ints();
    axis_st_and_cpp_paths_are_cycle_equivalent();
    group_st_and_cpp_paths_are_cycle_equivalent();
    fixed_capacity_boundaries_are_exact();
    manifest_compile_and_binding_order_are_deterministic();
    full_binding_scan_is_zero_allocation();
    if(failures == 0) std::printf("PASS st_l2c_tests\n");
    return failures == 0 ? 0 : 1;
}
