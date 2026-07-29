// ST-L2c acceptance tests (approved st-l2c-semantics, 2026-07-17).
//
// The production binding manifest is the only FB/pin schema consumed here.
// This file deliberately does not repeat the 134 FB names or their pins: the
// closure test walks the generated manifest and then probes the real registry.

#include <algorithm>
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

void binding_registry_helpers_reject_unknown_values()
{
    st::BindingDispatcher dispatcher;
    constexpr st::BindingPinId pin = 5;
    constexpr std::uint64_t mask = std::uint64_t{1} << pin;
    dispatcher.store_mask = mask;
    dispatcher.load_mask = mask;
    dispatcher.object_store_mask = mask;
    dispatcher.object_load_mask = mask;
    dispatcher.sequence_store_mask = mask;
    dispatcher.sequence_load_mask = mask;
    dispatcher.tagged_reference_store_mask = mask;
    dispatcher.tagged_reference_load_mask = mask;

    check(dispatcher.stores(pin) && dispatcher.loads(pin) &&
              dispatcher.stores_object(pin) && dispatcher.loads_object(pin) &&
              dispatcher.stores_sequence(pin) &&
              dispatcher.loads_sequence(pin) &&
              dispatcher.stores_tagged_reference(pin) &&
              dispatcher.loads_tagged_reference(pin),
          "L2c registry masks accept their declared pin");
    check(!dispatcher.stores(pin - 1U) && !dispatcher.loads(pin - 1U) &&
              !dispatcher.stores_object(pin - 1U) &&
              !dispatcher.loads_object(pin - 1U) &&
              !dispatcher.stores_sequence(pin - 1U) &&
              !dispatcher.loads_sequence(pin - 1U) &&
              !dispatcher.stores_tagged_reference(pin - 1U) &&
              !dispatcher.loads_tagged_reference(pin - 1U),
          "L2c registry masks reject an unset pin");
    check(!dispatcher.stores(64U) && !dispatcher.loads(64U) &&
              !dispatcher.stores_object(64U) &&
              !dispatcher.loads_object(64U) &&
              !dispatcher.stores_sequence(64U) &&
              !dispatcher.loads_sequence(64U) &&
              !dispatcher.stores_tagged_reference(64U) &&
              !dispatcher.loads_tagged_reference(64U),
          "L2c registry masks reject shifts outside uint64");

    check(st::binding_dispatcher(st::BindingFbId::invalid) == nullptr &&
              !st::binding_pin_registered(st::BindingFbId::invalid, 0),
          "L2c registry rejects an unknown FB");
    const st::BindingFbDesc &first = st::binding_manifest().fb(0);
    check(!st::binding_pin_registered(
              first.id, static_cast<st::BindingPinId>(first.pin_count)),
          "L2c registry rejects a pin beyond the manifest");

    const st::generated::StBindingSet unknown_set =
        static_cast<st::generated::StBindingSet>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    const st::generated::StBindingPinDirection unknown_direction =
        static_cast<st::generated::StBindingPinDirection>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    check(st::binding_manifest_detail::binding_set(unknown_set) ==
              st::BindingSet::iec_basic &&
              st::binding_manifest_detail::pin_direction(
                  st::generated::StBindingPinDirection::unresolved) ==
                  st::PinDirection::input &&
              st::binding_manifest_detail::pin_direction(unknown_direction) ==
                  st::PinDirection::input,
          "L2c manifest enum fallbacks are conservative");

    struct TypeCase
    {
        std::string_view name;
        st::TypeId id;
    };
    const TypeCase types[] = {
        {"BOOL", st::builtin::bool_},   {"SINT", st::builtin::sint},
        {"INT", st::builtin::int_},     {"DINT", st::builtin::dint},
        {"LINT", st::builtin::lint},    {"USINT", st::builtin::usint},
        {"UINT", st::builtin::uint_},   {"REAL", st::builtin::real},
        {"LREAL", st::builtin::lreal},  {"TIME", st::builtin::time},
        {"UDINT", st::builtin::udint},  {"ULINT", st::builtin::ulint},
        {"BYTE", st::builtin::byte_},   {"WORD", st::builtin::word},
        {"DWORD", st::builtin::dword},  {"LWORD", st::builtin::lword},
        {"DATE", st::builtin::date},    {"TOD", st::builtin::tod},
        {"DT", st::builtin::dt},
    };
    for(const TypeCase &type : types) {
        check(st::binding_manifest_detail::pin_type_id(type.name) == type.id,
              "L2c manifest resolves every builtin pin type");
    }
    check(st::binding_manifest_detail::pin_type_id("AXIS_REF") ==
              st::binding_type::axis_ref &&
              st::binding_manifest_detail::pin_type_id("NOT_A_TYPE") ==
                  st::invalid_type_id,
          "L2c manifest resolves generated and unknown pin types");

    std::string digits;
    st::binding_manifest_detail::append_unsigned(digits, 0);
    digits.push_back(',');
    st::binding_manifest_detail::append_unsigned(
        digits, std::numeric_limits<std::uint64_t>::max());
    check(digits == "0,18446744073709551615",
          "L2c manifest unsigned formatting covers zero and max");
}

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

void generated_pin_codecs_cover_every_registered_pin()
{
    st::TypeTable types;
    check(st::install_binding_types(types) == st::TypeError::ok,
          "L2c-A02 generated pin codec type table installs");

    for(const st::generated::StBindingFbMetadata &metadata :
        st::generated::kStBindingFbs) {
        const std::size_t bytes =
            st::generated::st_binding_native_size(metadata.type);
        std::vector<std::uint64_t> storage((bytes + 7U) / 8U);
        check(st::generated::st_binding_default_construct(metadata.type,
                                                          storage.data()),
              "L2c-A02 generated pin codec FB constructs");

        const st::PinTable pins = st::pin_table(metadata.type);
        check(pins.count == metadata.pin_count,
              "L2c-A02 generated pin codec table matches metadata");
        for(std::uint16_t pin = 0U; pin < pins.count; ++pin) {
            if(st::generated::st_binding_can_store_scalar(metadata.type,
                                                           pin)) {
                check(st::generated::st_binding_store_scalar(
                          metadata.type, pin, storage.data(), 0U),
                      "L2c-A02 generated scalar input codec");
                for(std::uint64_t bits = 1U; bits <= 32U; ++bits) {
                    (void)st::generated::st_binding_store_scalar(
                        metadata.type, pin, storage.data(), bits);
                }
                constexpr std::array<std::uint64_t, 14U> boundary_bits{
                    127U,
                    128U,
                    255U,
                    256U,
                    32767U,
                    32768U,
                    65535U,
                    65536U,
                    0x7FFFFFFFULL,
                    0x80000000ULL,
                    0xFFFFFFFFULL,
                    (std::numeric_limits<std::uint64_t>::max)(),
                    std::uint64_t{1} << 63U,
                    0x7FF8000000000000ULL};
                for(const std::uint64_t bits : boundary_bits) {
                    (void)st::generated::st_binding_store_scalar(
                        metadata.type, pin, storage.data(), bits);
                }
            }
            if(st::generated::st_binding_can_load_scalar(metadata.type,
                                                          pin)) {
                std::uint64_t value = 0U;
                check(st::generated::st_binding_load_scalar(
                          metadata.type, pin, storage.data(), value),
                      "L2c-A02 generated scalar output codec");
            }

            const st::PinDesc &pin_desc = pins.pins[pin];
            const st::TypeDesc *type_desc = types.get(pin_desc.type_id);
            if(st::generated::st_binding_can_store_object(metadata.type,
                                                           pin) ||
               st::generated::st_binding_can_load_object(metadata.type,
                                                          pin)) {
                check(type_desc != nullptr && type_desc->size != 0U,
                      "L2c-A02 generated object codec type has storage");
                if(type_desc != nullptr && type_desc->size != 0U) {
                    std::vector<unsigned char> object_bytes(
                        static_cast<std::size_t>(type_desc->size));
                    if(st::generated::st_binding_can_store_object(
                           metadata.type, pin)) {
                        check(st::generated::st_binding_store_object(
                                  metadata.type, pin, storage.data(),
                                  object_bytes.data(), pin_desc.type_id,
                                  static_cast<std::uint32_t>(
                                      object_bytes.size())),
                              "L2c-A02 generated object input codec");
                        constexpr std::array<unsigned char, 4U>
                            object_patterns{0x01U, 0x7FU, 0x80U, 0xFFU};
                        for(const unsigned char pattern : object_patterns) {
                            std::fill(object_bytes.begin(), object_bytes.end(),
                                      pattern);
                            (void)st::generated::st_binding_store_object(
                                metadata.type, pin, storage.data(),
                                object_bytes.data(), pin_desc.type_id,
                                static_cast<std::uint32_t>(
                                    object_bytes.size()));
                        }
                        check(!st::generated::st_binding_store_object(
                                  metadata.type, pin, storage.data(),
                                  object_bytes.data(), st::invalid_type_id,
                                  static_cast<std::uint32_t>(
                                      object_bytes.size())),
                              "L2c-A02 generated object rejects wrong type");
                        check(!st::generated::st_binding_store_object(
                                  metadata.type, pin, storage.data(),
                                  object_bytes.data(), pin_desc.type_id, 0U),
                              "L2c-A02 generated object rejects wrong size");
                    }
                    if(st::generated::st_binding_can_load_object(
                           metadata.type, pin)) {
                        check(st::generated::st_binding_load_object(
                                  metadata.type, pin, storage.data(),
                                  object_bytes.data(), pin_desc.type_id,
                                  static_cast<std::uint32_t>(
                                      object_bytes.size())),
                              "L2c-A02 generated object output codec");
                    }
                }
            }

            st::generated::StBindingNativeSequenceValue sequence{};
            if(st::generated::st_binding_can_store_sequence(metadata.type,
                                                             pin)) {
                check(st::generated::st_binding_store_sequence(
                          metadata.type, pin, storage.data(), sequence),
                      "L2c-A02 generated sequence input codec");
                const std::uint64_t element = 0U;
                sequence = {&element, 1U, false};
                (void)st::generated::st_binding_store_sequence(
                    metadata.type, pin, storage.data(), sequence);
                sequence = {nullptr, 1U, false};
                check(!st::generated::st_binding_store_sequence(
                          metadata.type, pin, storage.data(), sequence),
                      "L2c-A02 generated sequence rejects null data");
                sequence = {&element,
                            (std::numeric_limits<std::size_t>::max)(), false};
                check(!st::generated::st_binding_store_sequence(
                          metadata.type, pin, storage.data(), sequence),
                      "L2c-A02 generated sequence rejects excessive count");
                sequence = {&element, 1U, true};
                check(!st::generated::st_binding_store_sequence(
                          metadata.type, pin, storage.data(), sequence),
                      "L2c-A02 generated sequence rejects invalid flag");
            }
            if(st::generated::st_binding_can_load_sequence(metadata.type,
                                                            pin)) {
                check(st::generated::st_binding_load_sequence(
                          metadata.type, pin, storage.data(), sequence),
                      "L2c-A02 generated sequence output codec");
            }

            st::generated::StBindingNativeTaggedReferenceValue tagged{};
            if(st::generated::st_binding_can_store_tagged_reference(
                   metadata.type, pin)) {
                check(st::generated::st_binding_store_tagged_reference(
                          metadata.type, pin, storage.data(), pin_desc.type_id,
                          tagged),
                      "L2c-A02 generated tagged-reference input codec");
                check(!st::generated::st_binding_store_tagged_reference(
                          metadata.type, pin, storage.data(),
                          st::invalid_type_id, tagged),
                      "L2c-A02 generated tagged reference rejects wrong type");
                tagged.tag = static_cast<
                    st::generated::StBindingNativeTaggedReferenceTag>(255U);
                check(!st::generated::st_binding_store_tagged_reference(
                          metadata.type, pin, storage.data(), pin_desc.type_id,
                          tagged),
                      "L2c-A02 generated tagged reference rejects wrong tag");
                tagged = {};
            }
            if(st::generated::st_binding_can_load_tagged_reference(
                   metadata.type, pin)) {
                check(st::generated::st_binding_load_tagged_reference(
                          metadata.type, pin, storage.data(), pin_desc.type_id,
                          tagged),
                      "L2c-A02 generated tagged-reference output codec");
            }
        }

        const std::uint16_t invalid_pin = pins.count;
        std::uint64_t scalar = 0U;
        unsigned char object = 0U;
        st::generated::StBindingNativeSequenceValue sequence{};
        st::generated::StBindingNativeTaggedReferenceValue tagged{};
        check(!st::generated::st_binding_store_scalar(
                  metadata.type, invalid_pin, storage.data(), 0U) &&
                  !st::generated::st_binding_load_scalar(
                      metadata.type, invalid_pin, storage.data(), scalar) &&
                  !st::generated::st_binding_store_object(
                      metadata.type, invalid_pin, storage.data(), &object,
                      st::invalid_type_id, 1U) &&
                  !st::generated::st_binding_load_object(
                      metadata.type, invalid_pin, storage.data(), &object,
                      st::invalid_type_id, 1U) &&
                  !st::generated::st_binding_store_sequence(
                      metadata.type, invalid_pin, storage.data(), sequence) &&
                  !st::generated::st_binding_load_sequence(
                      metadata.type, invalid_pin, storage.data(), sequence) &&
                  !st::generated::st_binding_store_tagged_reference(
                      metadata.type, invalid_pin, storage.data(),
                      st::invalid_type_id, tagged) &&
                  !st::generated::st_binding_load_tagged_reference(
                      metadata.type, invalid_pin, storage.data(),
                      st::invalid_type_id, tagged),
              "L2c-A02 generated dispatchers reject an out-of-range pin");

        check(st::generated::st_binding_destruct(metadata.type,
                                                 storage.data()),
              "L2c-A02 generated pin codec FB destructs");

        std::vector<std::uint64_t> invoked_storage((bytes + 7U) / 8U);
        check(st::generated::st_binding_default_construct(
                  metadata.type, invoked_storage.data()) &&
                  st::generated::st_binding_invoke(
                      metadata.type, invoked_storage.data(), 1000000),
              "L2c-A02 generated pin codec FB invokes from defaults");
        for(std::uint16_t pin = 0U; pin < pins.count; ++pin) {
            if(st::generated::st_binding_can_load_scalar(metadata.type, pin)) {
                std::uint64_t value = 0U;
                check(st::generated::st_binding_load_scalar(
                          metadata.type, pin, invoked_storage.data(), value),
                      "L2c-A02 invoked scalar output codec");
            }
        }
        check(st::generated::st_binding_destruct(metadata.type,
                                                 invoked_storage.data()),
              "L2c-A02 invoked pin codec FB destructs");
    }
}

template <st::FbType Type>
void exercise_profile_sequence_bounds()
{
    using Accessor = st::generated::StBindingNativePinAccessor<Type, 1U>;
    using Element = std::remove_cv_t<
        std::remove_pointer_t<typename Accessor::data_type>>;
    typename Accessor::fb_type instance;
    Element element{};
    st::generated::StBindingNativeSequenceValue value{};

    Accessor::data(instance) = nullptr;
    Accessor::count(instance) = 1U;
    check(!st::generated::st_binding_load_sequence(Type, 1U, &instance,
                                                    value),
          "L2c-A02 profile output rejects null data with a count");
    Accessor::data(instance) = &element;
    Accessor::count(instance) = 0U;
    check(!st::generated::st_binding_load_sequence(Type, 1U, &instance,
                                                    value),
          "L2c-A02 profile output rejects an empty non-null view");
    Accessor::count(instance) = 9U;
    check(!st::generated::st_binding_load_sequence(Type, 1U, &instance,
                                                    value),
          "L2c-A02 profile output rejects excess entries");
    Accessor::count(instance) = 1U;
    check(st::generated::st_binding_load_sequence(Type, 1U, &instance,
                                                   value) &&
              value.data == &element && value.count == 1U && !value.flag,
          "L2c-A02 profile output exposes a bounded sequence");
}

template <std::uint16_t Pin, std::size_t Maximum>
void exercise_cam_view_sequence_bounds()
{
    constexpr st::FbType Type = st::FbType::mc_digital_cam_switch;
    using Accessor = st::generated::StBindingNativePinAccessor<Type, Pin>;
    using View = std::remove_reference_t<decltype(
        Accessor::get(std::declval<typename Accessor::fb_type &>()))>;
    using Element = std::remove_cv_t<
        std::remove_pointer_t<decltype(std::declval<View &>().data)>>;
    typename Accessor::fb_type instance;
    Element element{};
    st::generated::StBindingNativeSequenceValue value{};
    View &view = Accessor::get(instance);

    view.data = nullptr;
    view.size = 1U;
    check(!st::generated::st_binding_load_sequence(Type, Pin, &instance,
                                                    value),
          "L2c-A02 cam view rejects null data with a count");
    view.data = &element;
    view.size = 0U;
    check(!st::generated::st_binding_load_sequence(Type, Pin, &instance,
                                                    value),
          "L2c-A02 cam view rejects an empty non-null view");
    view.size = Maximum + 1U;
    check(!st::generated::st_binding_load_sequence(Type, Pin, &instance,
                                                    value),
          "L2c-A02 cam view rejects excess entries");
    view.size = 1U;
    check(st::generated::st_binding_load_sequence(Type, Pin, &instance,
                                                   value) &&
              value.data == &element && value.count == 1U && !value.flag,
          "L2c-A02 cam view exposes a bounded sequence");
}

void generated_sequence_output_boundaries_are_exact()
{
    exercise_profile_sequence_bounds<st::FbType::mc_position_profile>();
    exercise_profile_sequence_bounds<st::FbType::mc_velocity_profile>();
    exercise_profile_sequence_bounds<st::FbType::mc_acceleration_profile>();
    exercise_cam_view_sequence_bounds<1U, 8U>();
    exercise_cam_view_sequence_bounds<2U, 4U>();
    exercise_cam_view_sequence_bounds<3U, 4U>();

    fb::FbCamTableSelect select;
    exec::CamPoint point{};
    st::generated::StBindingNativeSequenceValue value{};
    select.cam_table = {nullptr, 1U, false};
    check(!st::generated::st_binding_load_sequence(
              st::FbType::mc_cam_table_select, 2U, &select, value),
          "L2c-A02 cam table rejects null points with a count");
    select.cam_table = {&point, 1U, false};
    check(!st::generated::st_binding_load_sequence(
              st::FbType::mc_cam_table_select, 2U, &select, value),
          "L2c-A02 cam table rejects fewer than two points");
    select.cam_table = {&point, 65U, false};
    check(!st::generated::st_binding_load_sequence(
              st::FbType::mc_cam_table_select, 2U, &select, value),
          "L2c-A02 cam table rejects excess points");
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
        "Profile : MC_PositionProfile; Profile2 : MC_PositionProfile; "
        "Error : BOOL; END_VAR "
        "Profile(Axis := AxisX, TimePosition := Position, Execute := TRUE, "
        "ContinuousUpdate := FALSE, TimeScale := 1.0, "
        "PositionScale := 1.0, Offset := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting); "
        "Profile2(Axis := Profile.Axis, "
        "TimePosition := Profile.TimePosition, Execute := FALSE); "
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
        "Select2 : MC_PathSelect; Move : MC_MovePath; "
        "Selected : BOOL; Error : BOOL; END_VAR "
        "Select(AxesGroup := G, PathData := Data, "
        "PathDescription := Description, Execute := TRUE); "
        "Select2(AxesGroup := Select.AxesGroup, "
        "PathData := Select.PathData, "
        "PathDescription := Select.PathDescription, Execute := FALSE); "
        "Move(AxesGroup := Select2.AxesGroup, "
        "PathData := Select2.PathData, Execute := FALSE, "
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
        "Cam2 : MC_DigitalCamSwitch; "
        "Tick : DINT; EnableCmd : BOOL; InOperation : BOOL; Error : BOOL; "
        "END_VAR EnableCmd := Tick = 0; "
        "Cam(Axis := AxisX, Switches := Switches, Outputs := Outputs, "
        "TrackOptions := Options, Enable := EnableCmd, EnableMask := 1, "
        "ValueSource := MC_MASTER_VALUE_SOURCE#command); "
        "Cam2(Axis := Cam.Axis, Switches := Cam.Switches, "
        "Outputs := Cam.Outputs, TrackOptions := Cam.TrackOptions, "
        "Enable := FALSE); "
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

void broad_fb_binding_scan_exercises_untested_codecs()
{
    axis::AxisModel axis1;
    axis::AxisModel axis2;
    axis::AxisGroup group;
    axis1.set_power(true);
    axis2.set_power(true);
    group.add_axis(axis1);
    group.add_axis(axis2);
    group.enable();

    const st::CompileResult axis_fbs = st::compile(
        "PROGRAM axis_coverage VAR\n"
        "  A : AXIS_REF;\n"
        "  Power : MC_Power;\n"
        "  Home : MC_Home;\n"
        "  Stop : MC_Stop;\n"
        "  Halt : MC_Halt;\n"
        "  MoveRel : MC_MoveRelative;\n"
        "  MoveAdd : MC_MoveAdditive;\n"
        "  MoveSup : MC_MoveSuperimposed;\n"
        "  HaltSup : MC_HaltSuperimposed;\n"
        "  MoveVel : MC_MoveVelocity;\n"
        "  MoveCA : MC_MoveContinuousAbsolute;\n"
        "  MoveCR : MC_MoveContinuousRelative;\n"
        "  SetPos : MC_SetPosition;\n"
        "  SetOvr : MC_SetOverride;\n"
        "  ReadP : MC_ReadParameter;\n"
        "  ReadBP : MC_ReadBoolParameter;\n"
        "  WriteP : MC_WriteParameter;\n"
        "  WriteBP : MC_WriteBoolParameter;\n"
        "  ReadPos : MC_ReadActualPosition;\n"
        "  ReadVel : MC_ReadActualVelocity;\n"
        "  ReadTrq : MC_ReadActualTorque;\n"
        "  ReadSt : MC_ReadStatus;\n"
        "  ReadMS : MC_ReadMotionState;\n"
        "  ReadAI : MC_ReadAxisInfo;\n"
        "  ReadAE : MC_ReadAxisError;\n"
        "  Reset : MC_Reset;\n"
        "END_VAR\n"
        "Power(Axis := A, Enable := TRUE);\n"
        "Home(Axis := A, Execute := TRUE, Position := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "Stop(Axis := A, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 1.0);\n"
        "Halt(Axis := A, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveRel(Axis := A, Execute := TRUE, Distance := 1.0, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveAdd(Axis := A, Execute := TRUE, Distance := 0.5, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveSup(Axis := A, Execute := TRUE, Distance := 0.1, "
        "VelocityDiff := 1.0);\n"
        "HaltSup(Axis := A, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 1.0);\n"
        "MoveVel(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#positive, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveCA(Axis := A, Execute := TRUE, Position := 2.0, "
        "Velocity := 1.0, EndVelocity := 0.5, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#positive, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveCR(Axis := A, Execute := TRUE, Distance := 1.0, "
        "Velocity := 1.0, EndVelocity := 0.5, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "SetPos(Axis := A, Execute := TRUE, Position := 0.0, "
        "Relative := FALSE, ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "SetOvr(Axis := A, Enable := TRUE, VelFactor := 1.0, "
        "AccFactor := 1.0, JerkFactor := 1.0);\n"
        "ReadP(Axis := A, Enable := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#commanded_position);\n"
        "ReadBP(Axis := A, Enable := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#enable_limit_pos);\n"
        "WriteP(Axis := A, Execute := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#sw_limit_pos, "
        "Value := 100.0);\n"
        "WriteBP(Axis := A, Execute := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#enable_limit_neg, "
        "Value := TRUE);\n"
        "ReadPos(Axis := A, Enable := TRUE);\n"
        "ReadVel(Axis := A, Enable := TRUE);\n"
        "ReadTrq(Axis := A, Enable := TRUE);\n"
        "ReadSt(Axis := A, Enable := TRUE);\n"
        "ReadMS(Axis := A, Enable := TRUE);\n"
        "ReadAI(Axis := A, Enable := TRUE);\n"
        "ReadAE(Axis := A, Enable := TRUE);\n"
        "Reset(Axis := A, Execute := TRUE);\n"
        "END_PROGRAM\n");
    if(!axis_fbs.ok) {
        for(const st::Diagnostic &d : axis_fbs.diagnostics) {
            std::printf("  B01-axis diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
        }
    }
    check(axis_fbs.ok, "L2c-B01 broad axis FB source compiles");
    if(axis_fbs.ok) {
        std::vector<std::uint64_t> storage = instance_storage(axis_fbs.program);
        st::Instance instance;
        check(instance.load(axis_fbs.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_axis("A", &axis1) == st::BindingError::ok,
              "L2c-B01 broad axis FB instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B01 broad axis FB binding scan succeeds");
        axis1.cycle();
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B01 broad axis FB second cycle scan succeeds");
    }

    const st::CompileResult group_fbs = st::compile(
        "PROGRAM group_coverage VAR\n"
        "  G : GROUP_REF;\n"
        "  Enable : MC_GroupEnable;\n"
        "  Disable : MC_GroupDisable;\n"
        "  GHome : MC_GroupHome;\n"
        "  GPower : MC_GroupPower;\n"
        "  ReadPos : MC_GroupReadPosition;\n"
        "  ReadVel : MC_GroupReadVelocity;\n"
        "  ReadAcc : MC_GroupReadAcceleration;\n"
        "  ReadMS : MC_GroupReadMotionState;\n"
        "  ReadCI : MC_GroupReadCommandInfo;\n"
        "  ReadParam : MC_GroupReadParameter;\n"
        "  WriteParam : MC_GroupWriteParameter;\n"
        "  ReadErr : MC_GroupReadError;\n"
        "  GReset : MC_GroupReset;\n"
        "  GStop : MC_GroupStop;\n"
        "  GHalt : MC_GroupHalt;\n"
        "  SetOvr : MC_GroupSetOverride;\n"
        "  ReadRefDyn : MC_GroupReadReferenceDynamics;\n"
        "  WriteRefDyn : MC_GroupWriteReferenceDynamics;\n"
        "  ReadDefDyn : MC_GroupReadDefaultDynamics;\n"
        "  WriteDefDyn : MC_GroupWriteDefaultDynamics;\n"
        "  Position : MC_GROUP_POSITION;\n"
        "END_VAR\n"
        "Enable(AxesGroup := G, Execute := TRUE);\n"
        "Disable(AxesGroup := G, Execute := TRUE);\n"
        "GHome(AxesGroup := G, Execute := TRUE, Position := Position, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "GPower(AxesGroup := G, Enable := TRUE);\n"
        "ReadPos(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "ReadVel(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "ReadAcc(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "ReadMS(AxesGroup := G, Enable := TRUE);\n"
        "ReadCI(AxesGroup := G, Enable := TRUE);\n"
        "ReadParam(AxesGroup := G, Enable := TRUE, ParameterNumber := 1);\n"
        "WriteParam(AxesGroup := G, Execute := TRUE, ParameterNumber := 1, "
        "Value := 0.0);\n"
        "ReadErr(AxesGroup := G, Enable := TRUE);\n"
        "GReset(AxesGroup := G, Execute := TRUE);\n"
        "GStop(AxesGroup := G, Execute := TRUE, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "GHalt(AxesGroup := G, Execute := TRUE, "
        "Deceleration := 1.0, Jerk := 1.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "SetOvr(AxesGroup := G, Enable := TRUE, VelFactor := 1.0, "
        "AccFactor := 1.0, JerkFactor := 1.0);\n"
        "ReadRefDyn(AxesGroup := G, Enable := TRUE);\n"
        "WriteRefDyn(AxesGroup := G, Execute := TRUE, "
        "Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "ReadDefDyn(AxesGroup := G, Enable := TRUE);\n"
        "WriteDefDyn(AxesGroup := G, Execute := TRUE, "
        "Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "END_PROGRAM\n");
    if(!group_fbs.ok) {
        for(const st::Diagnostic &d : group_fbs.diagnostics)
            std::printf("  B01-grp diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(group_fbs.ok, "L2c-B01 broad group FB source compiles");
    if(group_fbs.ok) {
        std::vector<std::uint64_t> storage = instance_storage(group_fbs.program);
        st::Instance instance;
        check(instance.load(group_fbs.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok,
              "L2c-B01 broad group FB instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B01 broad group FB binding scan succeeds");
        group.cycle();
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B01 broad group FB second cycle scan succeeds");
    }

    const st::CompileResult io_fbs = st::compile(
        "PROGRAM io_coverage VAR\n"
        "  A : AXIS_REF;\n"
        "  InRef : MC_INPUT_REF;\n"
        "  OutRef : MC_OUTPUT_REF;\n"
        "  ReadDI : MC_ReadDigitalInput;\n"
        "  ReadDO : MC_ReadDigitalOutput;\n"
        "  WriteDO : MC_WriteDigitalOutput;\n"
        "  Probe : MC_TouchProbe;\n"
        "  AbortT : MC_AbortTrigger;\n"
        "  CamOut : MC_CamOut;\n"
        "  GearOut : MC_GearOut;\n"
        "END_VAR\n"
        "ReadDI(Input := InRef, Enable := TRUE, InputNumber := 1);\n"
        "ReadDO(Output := OutRef, Enable := TRUE, OutputNumber := 1);\n"
        "WriteDO(Output := OutRef, Execute := TRUE, OutputNumber := 1, "
        "Value := TRUE);\n"
        "Probe(Axis := A, Execute := TRUE, TriggerInput := 1);\n"
        "AbortT(Axis := A, Execute := TRUE, TriggerInput := 1);\n"
        "CamOut(Slave := A, Execute := TRUE);\n"
        "GearOut(Slave := A, Execute := TRUE);\n"
        "END_PROGRAM\n");
    if(!io_fbs.ok) {
        for(const st::Diagnostic &d : io_fbs.diagnostics)
            std::printf("  B01-io diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(io_fbs.ok, "L2c-B01 broad IO FB source compiles");
    if(io_fbs.ok) {
        std::vector<std::uint64_t> storage = instance_storage(io_fbs.program);
        st::Instance instance;
        check(instance.load(io_fbs.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_axis("A", &axis1) == st::BindingError::ok,
              "L2c-B01 broad IO FB instance loads and binds");
        check(instance.scan(2048) == st::ScanError::ok,
              "L2c-B01 broad IO FB binding scan succeeds");
    }

    const st::CompileResult iec_fbs = st::compile(
        "PROGRAM iec_coverage VAR\n"
        "  Timer1 : TON; Timer2 : TOF; Pulse : TP;\n"
        "  CountUp : CTU; CountDown : CTD; CountBoth : CTUD;\n"
        "  SetReset : SR; ResetSet : RS;\n"
        "  RisingEdge : R_TRIG; FallingEdge : F_TRIG;\n"
        "  Q1 : BOOL; Q2 : BOOL; Q3 : BOOL;\n"
        "  CV1 : DINT; CV2 : DINT;\n"
        "END_VAR\n"
        "Timer1(IN := TRUE, PT := T#100ms);\n"
        "Timer2(IN := FALSE, PT := T#50ms);\n"
        "Pulse(IN := TRUE, PT := T#200ms);\n"
        "CountUp(CU := TRUE, R := FALSE, PV := 10);\n"
        "CountDown(CD := TRUE, LD := FALSE, PV := 5);\n"
        "CountBoth(CU := TRUE, CD := FALSE, R := FALSE, "
        "LD := FALSE, PV := 10);\n"
        "SetReset(SET1 := TRUE, RESET := FALSE);\n"
        "ResetSet(S := FALSE, R1 := TRUE);\n"
        "RisingEdge(CLK := TRUE);\n"
        "FallingEdge(CLK := FALSE);\n"
        "Q1 := Timer1.Q; Q2 := SetReset.Q1; Q3 := RisingEdge.Q;\n"
        "CV1 := CountUp.CV; CV2 := CountDown.CV;\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK Main(INTERVAL := T#1ms, PHASE := T#0ms, PRIORITY := 0, "
        "BUDGET := 100000);\n"
        "PROGRAM P0 WITH Main : iec_coverage;\n"
        "END_RESOURCE\nEND_CONFIGURATION\n");
    if(!iec_fbs.ok) {
        for(const st::Diagnostic &d : iec_fbs.diagnostics)
            std::printf("  B01-iec diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(iec_fbs.ok, "L2c-B01 broad IEC FB source compiles");
    if(iec_fbs.ok) {
        std::vector<std::uint64_t> storage(32768);
        st::ConfigurationRuntime runtime;
        check(runtime.load(iec_fbs.program, "plant",
                           reinterpret_cast<unsigned char *>(storage.data()),
                           storage.size() * sizeof(std::uint64_t),
                           1000000) == rt::ErrorCode::ok,
              "L2c-B01 IEC FB configuration loads");
        runtime.boundary(1000000, nullptr, 0);
        check(runtime.run(100000) == rt::ErrorCode::ok,
              "L2c-B01 IEC FB first scan succeeds");
        runtime.boundary(2000000, nullptr, 0);
        check(runtime.run(100000) == rt::ErrorCode::ok,
              "L2c-B01 IEC FB second scan succeeds");
        runtime.boundary(3000000, nullptr, 0);
        check(runtime.run(100000) == rt::ErrorCode::ok,
              "L2c-B01 IEC FB third scan succeeds");
    }
}

void broad_group_motion_binding_scan_exercises_part4_codecs()
{
    axis::AxisModel members[2];
    axis::AxisGroup group;
    for(auto &m : members) {
        m.set_power(true);
        group.add_axis(m);
    }
    group.enable();

    const st::CompileResult linear = st::compile(
        "PROGRAM lin_coverage VAR\n"
        "  G : GROUP_REF;\n"
        "  MoveLA : MC_MoveLinearAbsolute;\n"
        "  MoveLR : MC_MoveLinearRelative;\n"
        "  MoveDA : MC_MoveDirectAbsolute;\n"
        "  MoveDR : MC_MoveDirectRelative;\n"
        "  Interrupt : MC_GroupInterrupt;\n"
        "  GCont : MC_GroupContinue;\n"
        "  Target : MC_GROUP_POSITION;\n"
        "END_VAR\n"
        "Target.value[0] := 1.0; Target.value[1] := 1.0;\n"
        "MoveLA(AxesGroup := G, Execute := TRUE, Position := Target, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveLR(AxesGroup := G, Execute := TRUE, Distance := Target, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveDA(AxesGroup := G, Execute := TRUE, Position := Target, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "MoveDR(AxesGroup := G, Execute := TRUE, Distance := Target, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "Interrupt(AxesGroup := G, Execute := TRUE);\n"
        "GCont(AxesGroup := G, Execute := TRUE);\n"
        "END_PROGRAM\n");
    if(!linear.ok) {
        for(const st::Diagnostic &d : linear.diagnostics)
            std::printf("  B02-lin diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(linear.ok, "L2c-B02 broad linear motion FB source compiles");
    if(linear.ok) {
        std::vector<std::uint64_t> storage = instance_storage(linear.program);
        st::Instance instance;
        check(instance.load(linear.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok,
              "L2c-B02 linear motion instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B02 linear motion binding scan succeeds");
        group.cycle();
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B02 linear motion second cycle scan succeeds");
    }

    const st::CompileResult jog_tool = st::compile(
        "PROGRAM jog_tool_coverage VAR\n"
        "  G : GROUP_REF;\n"
        "  Jog : MC_GroupJog;\n"
        "  JogVec : MC_GroupJogVector;\n"
        "  WriteJog : MC_GroupWriteJoggingDynamics;\n"
        "  ReadJog : MC_GroupReadJoggingDynamics;\n"
        "  WriteTool : MC_GroupWriteToolData;\n"
        "  ReadTool : MC_GroupReadToolData;\n"
        "  SelectTool : MC_GroupSelectTool;\n"
        "  ReadSelTool : MC_GroupReadTool;\n"
        "  WritePayload : MC_GroupWritePayloadData;\n"
        "  ReadPayload : MC_GroupReadPayloadData;\n"
        "  SelectPayload : MC_GroupSelectPayload;\n"
        "  ReadSelPayload : MC_GroupReadPayload;\n"
        "  ReadSWLim : MC_GroupReadSWLimits;\n"
        "  WriteSWLim : MC_GroupWriteSWLimits;\n"
        "  TransformPos : MC_GroupTransformPosition;\n"
        "  ReadRBD : MC_GroupReadRigidBodyDynamic;\n"
        "  WriteRBD : MC_GroupWriteRigidBodyDynamic;\n"
        "  Position : MC_GROUP_POSITION;\n"
        "END_VAR\n"
        "Jog(AxesGroup := G, Enable := TRUE, "
        "VelOverride := 0.5, AccOverride := 0.5, "
        "CoordSystem := MC_COORD_SYSTEM#acs);\n"
        "JogVec(AxesGroup := G, Enable := TRUE, Direction := Position, "
        "VelOverride := 0.5, AccOverride := 0.5, "
        "CoordSystem := MC_COORD_SYSTEM#acs);\n"
        "WriteJog(AxesGroup := G, Execute := TRUE, "
        "Velocity := 0.5, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "ReadJog(AxesGroup := G, Enable := TRUE);\n"
        "WriteTool(AxesGroup := G, Execute := TRUE, ToolNumber := 1);\n"
        "ReadTool(AxesGroup := G, Enable := TRUE, ToolNumber := 1);\n"
        "SelectTool(AxesGroup := G, Execute := TRUE, ToolNumber := 1);\n"
        "ReadSelTool(AxesGroup := G, Enable := TRUE);\n"
        "WritePayload(AxesGroup := G, Execute := TRUE, "
        "PayloadNumber := 1);\n"
        "ReadPayload(AxesGroup := G, Enable := TRUE, PayloadNumber := 1);\n"
        "SelectPayload(AxesGroup := G, Execute := TRUE, "
        "PayloadNumber := 1);\n"
        "ReadSelPayload(AxesGroup := G, Enable := TRUE);\n"
        "ReadSWLim(AxesGroup := G, Enable := TRUE);\n"
        "WriteSWLim(AxesGroup := G, Execute := TRUE);\n"
        "TransformPos(AxesGroup := G, Enable := TRUE, "
        "InputPosition := Position, "
        "InputCoordSystem := MC_COORD_SYSTEM#acs, "
        "OutputCoordSystem := MC_COORD_SYSTEM#pcs);\n"
        "ReadRBD(AxesGroup := G, Enable := TRUE);\n"
        "WriteRBD(AxesGroup := G, Execute := TRUE, "
        "RigidBodyCount := 1);\n"
        "END_PROGRAM\n");
    if(!jog_tool.ok) {
        for(const st::Diagnostic &d : jog_tool.diagnostics)
            std::printf("  B02-jog diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(jog_tool.ok, "L2c-B02 broad jog/tool FB source compiles");
    if(jog_tool.ok) {
        std::vector<std::uint64_t> storage = instance_storage(jog_tool.program);
        st::Instance instance;
        check(instance.load(jog_tool.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok,
              "L2c-B02 jog/tool instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B02 jog/tool binding scan succeeds");
    }

    const st::CompileResult homing_fbs = st::compile(
        "PROGRAM homing_coverage VAR\n"
        "  A : AXIS_REF;\n"
        "  HomeDirect : MC_HomeDirect;\n"
        "  HomeAbs : MC_HomeAbsolute;\n"
        "  Finish : MC_FinishHoming;\n"
        "  StepAbs : MC_StepAbsoluteSwitch;\n"
        "  StepLim : MC_StepLimitSwitch;\n"
        "  StepBlk : MC_StepBlock;\n"
        "  StepRef : MC_StepReferencePulse;\n"
        "  StepDist : MC_StepDistanceCoded;\n"
        "  StepFlying : MC_StepReferenceFlyingSwitch;\n"
        "  StepFlyRef : MC_StepReferenceFlyingRefPulse;\n"
        "  AbortHome : MC_AbortPassiveHoming;\n"
        "END_VAR\n"
        "HomeDirect(Axis := A, Execute := TRUE, SetPosition := 0.0);\n"
        "HomeAbs(Axis := A, Execute := TRUE, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "Finish(Axis := A, Execute := TRUE, Distance := 0.0, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "StepAbs(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "SetPosition := 0.0);\n"
        "StepLim(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "SetPosition := 0.0);\n"
        "StepBlk(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "SetPosition := 0.0);\n"
        "StepRef(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "SetPosition := 0.0);\n"
        "StepDist(Axis := A, Execute := TRUE, Velocity := 1.0);\n"
        "StepFlying(Axis := A, Execute := TRUE, "
        "SetPosition := 0.0);\n"
        "StepFlyRef(Axis := A, Execute := TRUE, "
        "SetPosition := 0.0);\n"
        "AbortHome(Axis := A, Execute := TRUE);\n"
        "END_PROGRAM\n");
    if(!homing_fbs.ok) {
        for(const st::Diagnostic &d : homing_fbs.diagnostics)
            std::printf("  B02-home diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(homing_fbs.ok, "L2c-B02 broad homing FB source compiles");
    if(homing_fbs.ok) {
        axis::AxisModel homing_axis;
        homing_axis.set_power(true);
        std::vector<std::uint64_t> storage =
            instance_storage(homing_fbs.program);
        st::Instance instance;
        check(instance.load(homing_fbs.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_axis("A", &homing_axis) == st::BindingError::ok,
              "L2c-B02 homing instance loads and binds");
        check(instance.scan(2048) == st::ScanError::ok,
              "L2c-B02 homing binding scan succeeds");
    }
}

void broad_remaining_fb_scan_exercises_all_codec_paths()
{
    axis::AxisModel axis1;
    axis::AxisModel axis2;
    axis::AxisModel axis3;
    axis::AxisGroup group;
    axis::AxisGroup group2;
    axis1.set_power(true);
    axis2.set_power(true);
    axis3.set_power(true);
    group.add_axis(axis1);
    group.add_axis(axis2);
    group.enable();
    group2.add_axis(axis3);
    group2.enable();

    const st::CompileResult axis_sync = st::compile(
        "PROGRAM axis_sync_cov VAR\n"
        "  Master : AXIS_REF; Slave : AXIS_REF; A : AXIS_REF;\n"
        "  Vel : MC_TIME_VELOCITY;\n"
        "  Acc : MC_TIME_ACCELERATION;\n"
        "  CamT : MC_CAM_TABLE_VIEW;\n"
        "  VelProf : MC_VelocityProfile;\n"
        "  VelProf2 : MC_VelocityProfile;\n"
        "  AccProf : MC_AccelerationProfile;\n"
        "  AccProf2 : MC_AccelerationProfile;\n"
        "  CamSel : MC_CamTableSelect;\n"
        "  CamSel2 : MC_CamTableSelect;\n"
        "  CamI : MC_CamIn;\n"
        "  Combine : MC_CombineAxes;\n"
        "  ReadAGI : MC_ReadAxisGroupInfo;\n"
        "END_VAR\n"
        "VelProf(Axis := A, TimeVelocity := Vel, Execute := TRUE, "
        "TimeScale := 1.0, VelocityScale := 1.0, Offset := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "VelProf2(Axis := VelProf.Axis, "
        "TimeVelocity := VelProf.TimeVelocity, Execute := FALSE);\n"
        "AccProf(Axis := A, TimeAcceleration := Acc, Execute := TRUE, "
        "TimeScale := 1.0, AccelerationScale := 1.0, Offset := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "AccProf2(Axis := AccProf.Axis, "
        "TimeAcceleration := AccProf.TimeAcceleration, Execute := FALSE);\n"
        "CamSel(Master := Master, Slave := Slave, CamTable := CamT, "
        "Execute := TRUE, Periodic := TRUE, MasterAbsolute := TRUE, "
        "SlaveAbsolute := TRUE, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "CamSel2(Master := CamSel.Master, Slave := CamSel.Slave, "
        "CamTable := CamSel.CamTable, Execute := FALSE);\n"
        "CamI(Master := Master, Slave := Slave, Execute := TRUE, "
        "MasterOffset := 0.0, SlaveOffset := 0.0, "
        "MasterScaling := 1.0, SlaveScaling := 1.0, "
        "MasterStartDistance := 10.0, MasterSyncPosition := 0.0, "
        "StartMode := MC_CAM_START_MODE#absolute, "
        "MasterValueSource := MC_MASTER_VALUE_SOURCE#command, "
        "CamTableID := 1, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "Combine(Master1 := Master, Master2 := Slave, Slave := A, "
        "Execute := TRUE, "
        "CombineMode := MC_COMBINE_MODE#add_axes, "
        "GearRationNumeratorM1 := 1.0, GearRatioDenominatorM1 := 1.0, "
        "GearRatioNumeratorM2 := 1.0, GearRatioDenominatorM2 := 1.0, "
        "MasterValueSourceM1 := MC_MASTER_VALUE_SOURCE#command, "
        "MasterValueSourceM2 := MC_MASTER_VALUE_SOURCE#command, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "ReadAGI(Axis := A, Enable := TRUE);\n"
        "END_PROGRAM\n");
    if(!axis_sync.ok) {
        for(const st::Diagnostic &d : axis_sync.diagnostics)
            std::printf("  B04-sync diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(axis_sync.ok, "L2c-B04 axis sync/profile FB source compiles");
    if(axis_sync.ok) {
        std::vector<std::uint64_t> storage = instance_storage(axis_sync.program);
        st::Instance instance;
        check(instance.load(axis_sync.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_axis("Master", &axis1) ==
                      st::BindingError::ok &&
                  instance.bind_axis("Slave", &axis2) ==
                      st::BindingError::ok &&
                  instance.bind_axis("A", &axis3) == st::BindingError::ok,
              "L2c-B04 axis sync instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B04 axis sync FB binding scan succeeds");
    }

    const st::CompileResult group_kin = st::compile(
        "PROGRAM group_kin_cov VAR\n"
        "  G : GROUP_REF; A : AXIS_REF;\n"
        "  Pos : MC_GROUP_POSITION;\n"
        "  Tool : MC_TOOL_DATA;\n"
        "  Ident : MC_IDENT_IN_GROUP;\n"
        "  ReadConf : MC_GroupReadConfiguration;\n"
        "  SetCart : MC_SetCartesianTransform;\n"
        "  SetCoord : MC_SetCoordinateTransform;\n"
        "  ReadCart : MC_ReadCartesianTransform;\n"
        "  ReadCoord : MC_ReadCoordinateTransform;\n"
        "  ReadDH : MC_ReadDHParameters;\n"
        "  ReadJI : MC_ReadJointInfo;\n"
        "  GSetPos : MC_GroupSetPosition;\n"
        "END_VAR\n"
        "ReadConf(AxesGroup := G, Enable := TRUE, "
        "IdentInGroup := Ident, "
        "CoordSystem := MC_COORD_SYSTEM#acs);\n"
        "SetCart(AxesGroup := G, Execute := TRUE, "
        "TransX := 1.0, TransY := 2.0, TransZ := 3.0, "
        "RotAngle1 := 0.1, RotAngle2 := 0.2, RotAngle3 := 0.3, "
        "CoordSystem := MC_COORD_SYSTEM#pcs, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "SetCoord(AxesGroup := G, Execute := TRUE, "
        "CoordTransform := Tool, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "ReadCart(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#pcs);\n"
        "ReadCoord(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs);\n"
        "ReadDH(AxesGroup := G, Enable := TRUE);\n"
        "ReadJI(AxesGroup := G, Enable := TRUE);\n"
        "GSetPos(AxesGroup := G, Execute := TRUE, "
        "Position := Pos, Relative := FALSE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "END_PROGRAM\n");
    if(!group_kin.ok) {
        for(const st::Diagnostic &d : group_kin.diagnostics)
            std::printf("  B04-kin diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(group_kin.ok, "L2c-B04 group kinematics FB source compiles");
    if(group_kin.ok) {
        std::vector<std::uint64_t> storage = instance_storage(group_kin.program);
        st::Instance instance;
        check(instance.load(group_kin.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok &&
                  instance.bind_axis("A", &axis1) == st::BindingError::ok,
              "L2c-B04 group kin instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B04 group kinematics FB binding scan succeeds");
    }

    const st::CompileResult group_motion = st::compile(
        "PROGRAM group_motion_cov VAR\n"
        "  G : GROUP_REF; G2 : GROUP_REF;\n"
        "  A : AXIS_REF; Conv : AXIS_REF;\n"
        "  Pos : MC_GROUP_POSITION;\n"
        "  Aux : MC_GROUP_POSITION;\n"
        "  Tool : MC_TOOL_DATA;\n"
        "  Path : MC_PATH_TABLE;\n"
        "  TuN : MC_TU_C_NUMERATOR;\n"
        "  TuD : MC_TU_C_DENOMINATOR;\n"
        "  CircAbs : MC_MoveCircularAbsolute;\n"
        "  CircRel : MC_MoveCircularRelative;\n"
        "  SyncAG : MC_SyncAxisToGroup;\n"
        "  SyncGA : MC_SyncGroupToAxis;\n"
        "  DynCoord : MC_SetDynCoordTransform;\n"
        "  TrackCB : MC_TrackConveyorBelt;\n"
        "  TrackRT : MC_TrackRotaryTable;\n"
        "END_VAR\n"
        "CircAbs(AxesGroup := G, Execute := TRUE, "
        "CircMode := MC_CIRC_MODE#border, "
        "AuxPoint := Aux, EndPoint := Pos, "
        "PathChoice := MC_CIRC_PATH_CHOICE#clockwise, "
        "Tolerance := 0.01, Velocity := 10.0, "
        "Acceleration := 100.0, Deceleration := 100.0, Jerk := 1000.0, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting, "
        "TransitionMode := MC_TRANSITION_MODE#none, "
        "OrientationMode := MC_ORIENTATION_MODE#joint_space);\n"
        "CircRel(AxesGroup := G, Execute := TRUE, "
        "CircMode := MC_CIRC_MODE#center, "
        "AuxPoint := Aux, EndPoint := Pos, "
        "PathChoice := MC_CIRC_PATH_CHOICE#counter_clockwise, "
        "Tolerance := 0.01, Velocity := 10.0, "
        "Acceleration := 100.0, Deceleration := 100.0, Jerk := 1000.0, "
        "CoordSystem := MC_COORD_SYSTEM#pcs, "
        "BufferMode := MC_BUFFER_MODE#aborting, "
        "TransitionMode := MC_TRANSITION_MODE#none, "
        "OrientationMode := MC_ORIENTATION_MODE#shortest_path);\n"
        "SyncAG(AxesGroup := G, SlaveAxis := A, Execute := TRUE, "
        "RatioNumerator := 1.0, RatioDenominator := 1.0, "
        "Acceleration := 10.0, Deceleration := 10.0, Jerk := 100.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "SyncGA(Master := A, AxesGroup := G, PathData := Path, "
        "Execute := TRUE, Mode := MC_PATH_MODE#non_periodic, "
        "TuCNumerator := TuN, TuCDenominator := TuD, "
        "Acceleration := 10.0, Deceleration := 10.0, Jerk := 100.0, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "DynCoord(AxesGroup := G, MasterAxesGroup := G2, "
        "Execute := TRUE, CoordTransform := Tool, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "TrackCB(AxesGroup := G, ConveyorBelt := Conv, "
        "Execute := TRUE, ConveyorBeltOrigin := Tool, "
        "InitialObjectPosition := Tool, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "TrackRT(AxesGroup := G, RotaryTable := Conv, "
        "Execute := TRUE, RotaryTableOrigin := Tool, "
        "InitialObjectPosition := Tool, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "END_PROGRAM\n");
    if(!group_motion.ok) {
        for(const st::Diagnostic &d : group_motion.diagnostics)
            std::printf("  B04-motion diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(group_motion.ok,
          "L2c-B04 group motion/tracking FB source compiles");
    if(group_motion.ok) {
        std::vector<std::uint64_t> storage =
            instance_storage(group_motion.program);
        st::Instance instance;
        check(instance.load(group_motion.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok &&
                  instance.bind_group("G2", &group2) ==
                      st::BindingError::ok &&
                  instance.bind_axis("A", &axis1) == st::BindingError::ok &&
                  instance.bind_axis("Conv", &axis2) == st::BindingError::ok,
              "L2c-B04 group motion instance loads and binds");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B04 group motion/tracking FB binding scan succeeds");
    }
}

void broad_vm_language_features_exercise_untested_opcodes()
{
    const st::CompileResult rich = st::compile(
        "TYPE LRealArr : ARRAY[0..7] OF LREAL; END_TYPE\n"
        "TYPE DintArr : ARRAY[1..4] OF DINT; END_TYPE\n"
        "PROGRAM vm_rich VAR\n"
        "  arr : LRealArr;\n"
        "  iarr : DintArr;\n"
        "  s1 : STRING;\n"
        "  s2 : STRING;\n"
        "  idx : DINT;\n"
        "  x : LREAL; y : LREAL; z : LREAL;\n"
        "  i : DINT; j : DINT; k : DINT;\n"
        "  b1 : BOOL; b2 : BOOL;\n"
        "  r : REAL;\n"
        "  li : LINT;\n"
        "  ui : UDINT;\n"
        "  si : SINT;\n"
        "END_VAR\n"
        "arr[0] := 1.0; arr[1] := 2.0; arr[2] := 3.0;\n"
        "arr[3] := arr[0] + arr[1] * arr[2];\n"
        "idx := 4;\n"
        "arr[idx] := arr[idx - 1];\n"
        "iarr[1] := 10; iarr[2] := 20; iarr[3] := 30; iarr[4] := 40;\n"
        "i := iarr[1] + iarr[4];\n"
        "x := ABS(-3.14);\n"
        "y := SQRT(x);\n"
        "z := MIN(x, y);\n"
        "x := MAX(x, y);\n"
        "x := SIN(1.0);\n"
        "y := COS(1.0);\n"
        "z := ATAN(y);\n"
        "x := EXP(1.0);\n"
        "y := LN(x);\n"
        "z := EXPT(2.0, 3.0);\n"
        "r := LREAL_TO_REAL(x);\n"
        "li := DINT_TO_LINT(i);\n"
        "ui := DINT_TO_UDINT(i);\n"
        "si := DINT_TO_SINT(i);\n"
        "i := REAL_TO_DINT(r);\n"
        "b1 := i > 0;\n"
        "b2 := x >= y;\n"
        "b1 := b1 AND b2;\n"
        "b2 := b1 OR NOT b2;\n"
        "b1 := b1 XOR b2;\n"
        "i := i MOD 7;\n"
        "j := i * 4;\n"
        "k := j / 2;\n"
        "i := k + j - i;\n"
        "j := i * 3 + k;\n"
        "CASE idx OF\n"
        "  0: x := 0.0;\n"
        "  1, 2: x := 1.0;\n"
        "  3..5: x := 2.0;\n"
        "ELSE x := 3.0;\n"
        "END_CASE;\n"
        "FOR i := 0 TO 7 DO\n"
        "  arr[i] := DINT_TO_LREAL(i) * 0.5;\n"
        "  IF i = 5 THEN EXIT; END_IF;\n"
        "END_FOR;\n"
        "i := 0;\n"
        "WHILE i < 4 DO\n"
        "  iarr[i + 1] := i * i;\n"
        "  i := i + 1;\n"
        "END_WHILE;\n"
        "j := 0;\n"
        "REPEAT\n"
        "  j := j + 1;\n"
        "UNTIL j >= 3 END_REPEAT;\n"
        "s1 := 'Hello';\n"
        "s2 := CONCAT(s1, ' World');\n"
        "i := LEN(s2);\n"
        "b1 := s1 < s2;\n"
        "s1 := LEFT(s2, 5);\n"
        "s2 := MID(s2, 5, 7);\n"
        "END_PROGRAM\n"
        "CONFIGURATION Plant\nRESOURCE R0 ON PLC\n"
        "TASK Main(INTERVAL := T#1ms, PRIORITY := 0, BUDGET := 500000);\n"
        "PROGRAM P0 WITH Main : vm_rich;\n"
        "END_RESOURCE\nEND_CONFIGURATION\n");
    if(!rich.ok) {
        for(const st::Diagnostic &d : rich.diagnostics)
            std::printf("  B03-vm diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(rich.ok, "L2c-B03 rich VM feature source compiles");
    if(rich.ok) {
        std::vector<std::uint64_t> storage(65536);
        st::ConfigurationRuntime runtime;
        check(runtime.load(rich.program, "plant",
                           reinterpret_cast<unsigned char *>(storage.data()),
                           storage.size() * sizeof(std::uint64_t),
                           1000000) == rt::ErrorCode::ok,
              "L2c-B03 rich VM program loads");
        for(int cycle = 0; cycle < 5; ++cycle) {
            runtime.boundary(static_cast<std::uint64_t>(cycle + 1) * 1000000,
                             nullptr, 0);
            check(runtime.run(500000) == rt::ErrorCode::ok,
                  "L2c-B03 rich VM cycle succeeds");
        }
    }
}

void broad_output_pin_loading_exercises_all_load_codecs()
{
    axis::AxisModel axis1;
    axis::AxisModel axis2;
    axis::AxisGroup group;
    axis1.set_power(true);
    axis2.set_power(true);
    group.add_axis(axis1);
    group.add_axis(axis2);
    group.enable();

    const st::CompileResult axis_out = st::compile(
        "PROGRAM axis_out_cov VAR\n"
        "  A : AXIS_REF;\n"
        "  Power : MC_Power;\n"
        "  Home : MC_Home;\n"
        "  Stop : MC_Stop;\n"
        "  Halt : MC_Halt;\n"
        "  MoveAbs : MC_MoveAbsolute;\n"
        "  MoveRel : MC_MoveRelative;\n"
        "  MoveAdd : MC_MoveAdditive;\n"
        "  MoveSup : MC_MoveSuperimposed;\n"
        "  HaltSup : MC_HaltSuperimposed;\n"
        "  MoveVel : MC_MoveVelocity;\n"
        "  MoveCA : MC_MoveContinuousAbsolute;\n"
        "  MoveCR : MC_MoveContinuousRelative;\n"
        "  Torque : MC_TorqueControl;\n"
        "  SetPos : MC_SetPosition;\n"
        "  SetOvr : MC_SetOverride;\n"
        "  ReadP : MC_ReadParameter;\n"
        "  ReadBP : MC_ReadBoolParameter;\n"
        "  WriteP : MC_WriteParameter;\n"
        "  WriteBP : MC_WriteBoolParameter;\n"
        "  ReadPos : MC_ReadActualPosition;\n"
        "  ReadVel : MC_ReadActualVelocity;\n"
        "  ReadTrq : MC_ReadActualTorque;\n"
        "  ReadSt : MC_ReadStatus;\n"
        "  ReadMS : MC_ReadMotionState;\n"
        "  ReadAI : MC_ReadAxisInfo;\n"
        "  ReadAE : MC_ReadAxisError;\n"
        "  Reset : MC_Reset;\n"
        "  b : BOOL; d : DINT; v : LREAL;\n"
        "END_VAR\n"
        "Power(Axis := A, Enable := TRUE);\n"
        "b := Power.Status; b := Power.Valid; b := Power.Error; "
        "d := Power.ErrorID;\n"
        "Home(Axis := A, Execute := TRUE, Position := 0.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := Home.Done; b := Home.Busy; b := Home.Active; "
        "b := Home.CommandAborted; b := Home.Error; d := Home.ErrorID;\n"
        "Stop(Axis := A, Execute := TRUE, Deceleration := 1.0, Jerk := 1.0);\n"
        "b := Stop.Done; b := Stop.Busy; b := Stop.CommandAborted; "
        "b := Stop.Error; d := Stop.ErrorID;\n"
        "Halt(Axis := A, Execute := TRUE, Deceleration := 1.0, Jerk := 1.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := Halt.Done; b := Halt.Busy; b := Halt.Active; "
        "b := Halt.CommandAborted; b := Halt.Error; d := Halt.ErrorID;\n"
        "MoveAbs(Axis := A, Execute := TRUE, Position := 1.0, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveAbs.Done; b := MoveAbs.Busy; b := MoveAbs.Active; "
        "b := MoveAbs.CommandAborted; b := MoveAbs.Error; "
        "d := MoveAbs.ErrorID;\n"
        "MoveRel(Axis := A, Execute := TRUE, Distance := 1.0, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveRel.Done; b := MoveRel.Busy; b := MoveRel.Active; "
        "b := MoveRel.CommandAborted; b := MoveRel.Error; "
        "d := MoveRel.ErrorID;\n"
        "MoveAdd(Axis := A, Execute := TRUE, Distance := 0.5, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveAdd.Done; b := MoveAdd.Busy; b := MoveAdd.Active; "
        "b := MoveAdd.CommandAborted; b := MoveAdd.Error; "
        "d := MoveAdd.ErrorID;\n"
        "MoveSup(Axis := A, Execute := TRUE, Distance := 0.1, "
        "VelocityDiff := 1.0);\n"
        "b := MoveSup.Done; b := MoveSup.Busy; v := MoveSup.CoveredDistance; "
        "b := MoveSup.CommandAborted; b := MoveSup.Error; "
        "d := MoveSup.ErrorID;\n"
        "HaltSup(Axis := A, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 1.0);\n"
        "b := HaltSup.Done; b := HaltSup.Busy; "
        "b := HaltSup.Error; d := HaltSup.ErrorID;\n"
        "MoveVel(Axis := A, Execute := TRUE, Velocity := 1.0, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#positive, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveVel.InVelocity; b := MoveVel.Busy; "
        "b := MoveVel.Active; b := MoveVel.CommandAborted; "
        "b := MoveVel.Error; d := MoveVel.ErrorID;\n"
        "MoveCA(Axis := A, Execute := TRUE, Position := 2.0, "
        "Velocity := 1.0, EndVelocity := 0.5, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#positive, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveCA.InEndVelocity; b := MoveCA.Busy; b := MoveCA.Active; "
        "b := MoveCA.CommandAborted; b := MoveCA.Error; "
        "d := MoveCA.ErrorID;\n"
        "MoveCR(Axis := A, Execute := TRUE, Distance := 1.0, "
        "Velocity := 1.0, EndVelocity := 0.5, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := MoveCR.InEndVelocity; b := MoveCR.Busy; b := MoveCR.Active; "
        "b := MoveCR.CommandAborted; b := MoveCR.Error; "
        "d := MoveCR.ErrorID;\n"
        "Torque(Axis := A, Execute := TRUE, "
        "ContinuousUpdate := FALSE, TorqueRamp := 1.0, "
        "Torque := 0.5, Velocity := 1.0, "
        "Acceleration := 1.0, Deceleration := 1.0, Jerk := 1.0, "
        "Direction := MC_DIRECTION#positive, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := Torque.InTorque; b := Torque.Busy; b := Torque.Active; "
        "b := Torque.CommandAborted; b := Torque.Error; "
        "d := Torque.ErrorID;\n"
        "SetPos(Axis := A, Execute := TRUE, Position := 0.0, "
        "Relative := FALSE, "
        "ExecutionMode := MC_EXECUTION_MODE#immediately);\n"
        "b := SetPos.Done; b := SetPos.Busy; "
        "b := SetPos.Error; d := SetPos.ErrorID;\n"
        "SetOvr(Axis := A, Enable := TRUE, VelFactor := 1.0, "
        "AccFactor := 1.0, JerkFactor := 1.0);\n"
        "b := SetOvr.Enabled; b := SetOvr.Busy; "
        "b := SetOvr.Error; d := SetOvr.ErrorID;\n"
        "ReadP(Axis := A, Enable := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#commanded_position);\n"
        "b := ReadP.Valid; b := ReadP.Busy; b := ReadP.Error; "
        "d := ReadP.ErrorID; v := ReadP.Value;\n"
        "ReadBP(Axis := A, Enable := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#enable_limit_pos);\n"
        "b := ReadBP.Valid; b := ReadBP.Busy; b := ReadBP.Error; "
        "d := ReadBP.ErrorID; b := ReadBP.Value;\n"
        "WriteP(Axis := A, Execute := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#sw_limit_pos, "
        "Value := 100.0);\n"
        "b := WriteP.Done; b := WriteP.Busy; "
        "b := WriteP.Error; d := WriteP.ErrorID;\n"
        "WriteBP(Axis := A, Execute := TRUE, "
        "ParameterNumber := MC_AXIS_PARAMETER#enable_limit_neg, "
        "Value := TRUE);\n"
        "b := WriteBP.Done; b := WriteBP.Busy; "
        "b := WriteBP.Error; d := WriteBP.ErrorID;\n"
        "ReadPos(Axis := A, Enable := TRUE);\n"
        "b := ReadPos.Valid; b := ReadPos.Busy; b := ReadPos.Error; "
        "d := ReadPos.ErrorID; v := ReadPos.Position;\n"
        "ReadVel(Axis := A, Enable := TRUE);\n"
        "b := ReadVel.Valid; b := ReadVel.Busy; b := ReadVel.Error; "
        "d := ReadVel.ErrorID; v := ReadVel.Velocity;\n"
        "ReadTrq(Axis := A, Enable := TRUE);\n"
        "b := ReadTrq.Valid; b := ReadTrq.Busy; b := ReadTrq.Error; "
        "d := ReadTrq.ErrorID; v := ReadTrq.Torque;\n"
        "ReadSt(Axis := A, Enable := TRUE);\n"
        "b := ReadSt.Valid; b := ReadSt.Busy; b := ReadSt.Error; "
        "d := ReadSt.ErrorID; "
        "b := ReadSt.Disabled; b := ReadSt.Errorstop; "
        "b := ReadSt.Standstill; b := ReadSt.DiscreteMotion; "
        "b := ReadSt.ContinuousMotion; b := ReadSt.SynchronizedMotion; "
        "b := ReadSt.Stopping; b := ReadSt.Homing;\n"
        "ReadMS(Axis := A, Enable := TRUE);\n"
        "b := ReadMS.Valid; b := ReadMS.Busy; b := ReadMS.Error; "
        "d := ReadMS.ErrorID; "
        "b := ReadMS.ConstantVelocity; b := ReadMS.Accelerating; "
        "b := ReadMS.Decelerating; b := ReadMS.DirectionPositive; "
        "b := ReadMS.DirectionNegative;\n"
        "ReadAI(Axis := A, Enable := TRUE);\n"
        "b := ReadAI.Valid; b := ReadAI.Busy; b := ReadAI.Error; "
        "d := ReadAI.ErrorID; "
        "b := ReadAI.HomeAbsSwitch; b := ReadAI.LimitSwitchPos; "
        "b := ReadAI.LimitSwitchNeg; b := ReadAI.Simulation; "
        "b := ReadAI.CommunicationReady; b := ReadAI.ReadyForPowerOn; "
        "b := ReadAI.PowerOn; b := ReadAI.IsHomed; "
        "b := ReadAI.AxisWarning;\n"
        "ReadAE(Axis := A, Enable := TRUE);\n"
        "b := ReadAE.Valid; b := ReadAE.Busy; b := ReadAE.Error; "
        "d := ReadAE.ErrorID; d := ReadAE.AxisErrorID;\n"
        "Reset(Axis := A, Execute := TRUE);\n"
        "b := Reset.Done; b := Reset.Busy; "
        "b := Reset.Error; d := Reset.ErrorID;\n"
        "END_PROGRAM\n");
    if(!axis_out.ok) {
        for(const st::Diagnostic &d : axis_out.diagnostics)
            std::printf("  B05-axout diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(axis_out.ok, "L2c-B05 axis output pin source compiles");
    if(axis_out.ok) {
        std::vector<std::uint64_t> storage = instance_storage(axis_out.program);
        st::Instance instance;
        check(instance.load(axis_out.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_axis("A", &axis1) == st::BindingError::ok,
              "L2c-B05 axis output instance loads and binds");
        check(instance.scan(8192) == st::ScanError::ok,
              "L2c-B05 axis output pin scan succeeds");
        axis1.cycle();
        check(instance.scan(8192) == st::ScanError::ok,
              "L2c-B05 axis output pin second scan succeeds");
    }

    const st::CompileResult group_out = st::compile(
        "PROGRAM group_out_cov VAR\n"
        "  G : GROUP_REF;\n"
        "  Enable : MC_GroupEnable;\n"
        "  Disable : MC_GroupDisable;\n"
        "  GHome : MC_GroupHome;\n"
        "  GPower : MC_GroupPower;\n"
        "  GStop : MC_GroupStop;\n"
        "  GHalt : MC_GroupHalt;\n"
        "  GInterrupt : MC_GroupInterrupt;\n"
        "  GContinue : MC_GroupContinue;\n"
        "  SetOvr : MC_GroupSetOverride;\n"
        "  GReset : MC_GroupReset;\n"
        "  ReadStatus : MC_GroupReadStatus;\n"
        "  ReadPos : MC_GroupReadPosition;\n"
        "  ReadVel : MC_GroupReadVelocity;\n"
        "  ReadAcc : MC_GroupReadAcceleration;\n"
        "  ReadMS : MC_GroupReadMotionState;\n"
        "  ReadCI : MC_GroupReadCommandInfo;\n"
        "  ReadErr : MC_GroupReadError;\n"
        "  ReadParam : MC_GroupReadParameter;\n"
        "  WriteParam : MC_GroupWriteParameter;\n"
        "  ReadRefDyn : MC_GroupReadReferenceDynamics;\n"
        "  WriteRefDyn : MC_GroupWriteReferenceDynamics;\n"
        "  ReadDefDyn : MC_GroupReadDefaultDynamics;\n"
        "  WriteDefDyn : MC_GroupWriteDefaultDynamics;\n"
        "  Pos : MC_GROUP_POSITION;\n"
        "  b : BOOL; d : DINT; v : LREAL;\n"
        "END_VAR\n"
        "Enable(AxesGroup := G, Execute := TRUE);\n"
        "b := Enable.Done; b := Enable.Busy; "
        "b := Enable.Error; d := Enable.ErrorID;\n"
        "Disable(AxesGroup := G, Execute := TRUE);\n"
        "b := Disable.Done; b := Disable.Busy; "
        "b := Disable.Error; d := Disable.ErrorID;\n"
        "GHome(AxesGroup := G, Execute := TRUE, "
        "Position := Pos, BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := GHome.Done; b := GHome.Busy; b := GHome.Active; "
        "b := GHome.CommandAborted; b := GHome.Error; d := GHome.ErrorID;\n"
        "GPower(AxesGroup := G, Enable := TRUE);\n"
        "b := GPower.Status; b := GPower.Valid; "
        "b := GPower.Error; d := GPower.ErrorID;\n"
        "GStop(AxesGroup := G, Execute := TRUE, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "b := GStop.Done; b := GStop.Busy; "
        "b := GStop.Error; d := GStop.ErrorID;\n"
        "GHalt(AxesGroup := G, Execute := TRUE, "
        "Deceleration := 1.0, Jerk := 1.0, "
        "BufferMode := MC_BUFFER_MODE#aborting);\n"
        "b := GHalt.Done; b := GHalt.Busy; b := GHalt.Active; "
        "b := GHalt.CommandAborted; b := GHalt.Error; d := GHalt.ErrorID;\n"
        "GInterrupt(AxesGroup := G, Execute := TRUE);\n"
        "b := GInterrupt.Done; b := GInterrupt.Busy; "
        "b := GInterrupt.Error; d := GInterrupt.ErrorID;\n"
        "GContinue(AxesGroup := G, Execute := TRUE);\n"
        "b := GContinue.Done; b := GContinue.Busy; "
        "b := GContinue.Error; d := GContinue.ErrorID;\n"
        "SetOvr(AxesGroup := G, Enable := TRUE, VelFactor := 1.0, "
        "AccFactor := 1.0, JerkFactor := 1.0);\n"
        "b := SetOvr.Enabled; b := SetOvr.Busy; "
        "b := SetOvr.Error; d := SetOvr.ErrorID;\n"
        "GReset(AxesGroup := G, Execute := TRUE);\n"
        "b := GReset.Done; b := GReset.Busy; "
        "b := GReset.Error; d := GReset.ErrorID;\n"
        "ReadStatus(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadStatus.Valid; b := ReadStatus.Busy; "
        "b := ReadStatus.Error; d := ReadStatus.ErrorID; "
        "b := ReadStatus.GroupDisabled; b := ReadStatus.GroupStandby; "
        "b := ReadStatus.GroupMoving; b := ReadStatus.GroupStopping; "
        "b := ReadStatus.GroupErrorStop;\n"
        "ReadPos(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "b := ReadPos.Valid; b := ReadPos.Busy; b := ReadPos.Error; "
        "d := ReadPos.ErrorID; Pos := ReadPos.Position;\n"
        "ReadVel(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "b := ReadVel.Valid; b := ReadVel.Busy; b := ReadVel.Error; "
        "d := ReadVel.ErrorID; Pos := ReadVel.Velocity;\n"
        "ReadAcc(AxesGroup := G, Enable := TRUE, "
        "CoordSystem := MC_COORD_SYSTEM#acs, "
        "Source := MC_GROUP_VALUE_SOURCE#actual);\n"
        "b := ReadAcc.Valid; b := ReadAcc.Busy; b := ReadAcc.Error; "
        "d := ReadAcc.ErrorID; Pos := ReadAcc.Acceleration;\n"
        "ReadMS(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadMS.Valid; b := ReadMS.Busy; b := ReadMS.Error; "
        "d := ReadMS.ErrorID; "
        "b := ReadMS.ConstantVelocity; b := ReadMS.Accelerating; "
        "b := ReadMS.Decelerating;\n"
        "ReadCI(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadCI.Valid; b := ReadCI.Busy; b := ReadCI.Error; "
        "d := ReadCI.ErrorID;\n"
        "ReadErr(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadErr.Valid; b := ReadErr.Busy; b := ReadErr.Error; "
        "d := ReadErr.ErrorID; d := ReadErr.GroupErrorID;\n"
        "ReadParam(AxesGroup := G, Enable := TRUE, ParameterNumber := 1);\n"
        "b := ReadParam.Valid; b := ReadParam.Busy; "
        "b := ReadParam.Error; d := ReadParam.ErrorID; "
        "v := ReadParam.Value;\n"
        "WriteParam(AxesGroup := G, Execute := TRUE, "
        "ParameterNumber := 1, Value := 0.0);\n"
        "b := WriteParam.Done; b := WriteParam.Busy; "
        "b := WriteParam.Error; d := WriteParam.ErrorID;\n"
        "ReadRefDyn(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadRefDyn.Valid; b := ReadRefDyn.Busy; "
        "b := ReadRefDyn.Error; d := ReadRefDyn.ErrorID; "
        "v := ReadRefDyn.Velocity; v := ReadRefDyn.Acceleration; "
        "v := ReadRefDyn.Deceleration; v := ReadRefDyn.Jerk;\n"
        "WriteRefDyn(AxesGroup := G, Execute := TRUE, "
        "Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "b := WriteRefDyn.Done; b := WriteRefDyn.Busy; "
        "b := WriteRefDyn.Error; d := WriteRefDyn.ErrorID;\n"
        "ReadDefDyn(AxesGroup := G, Enable := TRUE);\n"
        "b := ReadDefDyn.Valid; b := ReadDefDyn.Busy; "
        "b := ReadDefDyn.Error; d := ReadDefDyn.ErrorID; "
        "v := ReadDefDyn.Velocity; v := ReadDefDyn.Acceleration; "
        "v := ReadDefDyn.Deceleration; v := ReadDefDyn.Jerk;\n"
        "WriteDefDyn(AxesGroup := G, Execute := TRUE, "
        "Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0);\n"
        "b := WriteDefDyn.Done; b := WriteDefDyn.Busy; "
        "b := WriteDefDyn.Error; d := WriteDefDyn.ErrorID;\n"
        "END_PROGRAM\n");
    if(!group_out.ok) {
        for(const st::Diagnostic &d : group_out.diagnostics)
            std::printf("  B05-grpout diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(group_out.ok, "L2c-B05 group output pin source compiles");
    if(group_out.ok) {
        std::vector<std::uint64_t> storage = instance_storage(group_out.program);
        st::Instance instance;
        check(instance.load(group_out.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok &&
                  instance.bind_group("G", &group) == st::BindingError::ok,
              "L2c-B05 group output instance loads and binds");
        check(instance.scan(8192) == st::ScanError::ok,
              "L2c-B05 group output pin scan succeeds");
        group.cycle();
        axis1.cycle();
        axis2.cycle();
        check(instance.scan(8192) == st::ScanError::ok,
              "L2c-B05 group output pin second scan succeeds");
    }

    const st::CompileResult iec_out = st::compile(
        "PROGRAM iec_out_cov VAR\n"
        "  Timer1 : TON; Timer2 : TOF; Pulse : TP;\n"
        "  CountUp : CTU; CountDown : CTD; CountBoth : CTUD;\n"
        "  SetReset : SR; ResetSet : RS;\n"
        "  Rising : R_TRIG; Falling : F_TRIG;\n"
        "  b : BOOL; d : DINT; t : TIME;\n"
        "END_VAR\n"
        "Timer1(IN := TRUE, PT := T#100ms);\n"
        "b := Timer1.Q; t := Timer1.ET;\n"
        "Timer2(IN := FALSE, PT := T#50ms);\n"
        "b := Timer2.Q; t := Timer2.ET;\n"
        "Pulse(IN := TRUE, PT := T#200ms);\n"
        "b := Pulse.Q; t := Pulse.ET;\n"
        "CountUp(CU := TRUE, R := FALSE, PV := 10);\n"
        "b := CountUp.Q; d := CountUp.CV;\n"
        "CountDown(CD := TRUE, LD := FALSE, PV := 5);\n"
        "b := CountDown.Q; d := CountDown.CV;\n"
        "CountBoth(CU := TRUE, CD := FALSE, R := FALSE, "
        "LD := FALSE, PV := 10);\n"
        "b := CountBoth.QU; b := CountBoth.QD; d := CountBoth.CV;\n"
        "SetReset(SET1 := TRUE, RESET := FALSE);\n"
        "b := SetReset.Q1;\n"
        "ResetSet(S := FALSE, R1 := TRUE);\n"
        "b := ResetSet.Q1;\n"
        "Rising(CLK := TRUE);\n"
        "b := Rising.Q;\n"
        "Falling(CLK := FALSE);\n"
        "b := Falling.Q;\n"
        "END_PROGRAM\n");
    if(!iec_out.ok) {
        for(const st::Diagnostic &d : iec_out.diagnostics)
            std::printf("  B05-iec diag %d:%d %s\n", d.line, d.column,
                        d.message.c_str());
    }
    check(iec_out.ok, "L2c-B05 IEC output pin source compiles");
    if(iec_out.ok) {
        std::vector<std::uint64_t> storage = instance_storage(iec_out.program);
        st::Instance instance;
        check(instance.load(iec_out.program,
                            reinterpret_cast<unsigned char *>(storage.data()),
                            storage.size() * sizeof(storage[0]), 1000000) ==
                  rt::ErrorCode::ok,
              "L2c-B05 IEC output instance loads");
        check(instance.scan(4096) == st::ScanError::ok,
              "L2c-B05 IEC output pin scan succeeds");
    }
}

} // namespace

int main()
{
    binding_registry_helpers_reject_unknown_values();
    manifest_closure_uses_one_production_schema();
    generated_lifecycle_covers_every_declared_fb();
    generated_pin_codecs_cover_every_registered_pin();
    generated_sequence_output_boundaries_are_exact();
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
    broad_fb_binding_scan_exercises_untested_codecs();
    broad_group_motion_binding_scan_exercises_part4_codecs();
    broad_remaining_fb_scan_exercises_all_codec_paths();
    broad_vm_language_features_exercise_untested_opcodes();
    broad_output_pin_loading_exercises_all_load_codecs();
    if(failures == 0) std::printf("PASS st_l2c_tests\n");
    return failures == 0 ? 0 : 1;
}
