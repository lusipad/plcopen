#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "st/binding.h"
#include "st/pins.h"
#include "st/type_desc.h"

namespace plcopen::core::st
{

// Stable public identity for an ST binding. Values are grouped by authority
// set and follow that set's canonical order; they do not encode FbType or a
// native C++ block type. In particular MC_CamOut and MC_GearOut remain
// distinct even though the native facade currently aliases their block type.
enum class BindingFbId : std::uint32_t
{
    invalid = 0,
    r_trig = 0x00010001U,
    f_trig = 0x00010002U,
    sr = 0x00010003U,
    rs = 0x00010004U,
    ton = 0x00010005U,
    tof = 0x00010006U,
    tp = 0x00010007U,
    ctu = 0x00010008U,
    ctd = 0x00010009U,
    ctud = 0x0001000aU,
    mc_power = 0x00020001U,
    mc_home = 0x00020002U,
    mc_stop = 0x00020003U,
    mc_halt = 0x00020004U,
    mc_move_absolute = 0x00020005U,
    mc_move_relative = 0x00020006U,
    mc_move_additive = 0x00020007U,
    mc_move_velocity = 0x0002000aU,
    mc_set_override = 0x00020012U,
    mc_reset = 0x00020021U,
    mc_cam_out = 0x00020027U,
    mc_gear_out = 0x00020029U,
    mc_add_axis_to_group = 0x00030001U,
    mc_remove_axis_from_group = 0x00030002U,
    mc_ungroup_all_axes = 0x00030003U,
    mc_group_read_status = 0x0003001eU,
};

enum class BindingSet : std::uint8_t
{
    iec_basic = 0,
    plcopen_part1_part2,
    plcopen_part4,
    plcopen_part5,
};

enum class PinDirection : std::uint8_t
{
    input = 0,
    output,
    in_out,
};

using BindingPinId = std::uint16_t;

struct BindingPinDesc
{
    BindingPinId id = 0;
    std::string_view lower_name;
    PinDirection direction = PinDirection::input;
    TypeId type_id = invalid_type_id;
    bool declared = false;
    bool generated = false;
    bool excluded = false;
    std::string_view scope_reason;
    std::string_view rejection_test;
};

struct BindingFbDesc
{
    BindingFbId id = BindingFbId::invalid;
    BindingSet set = BindingSet::iec_basic;
    std::string_view lower_name;
    std::string_view source;
    bool declared = false;
    bool generated = false;
    bool excluded = false;
    std::string_view scope_reason;
    std::string_view rejection_test;
    const BindingPinDesc *pins = nullptr;
    std::size_t pin_count = 0;
};

struct BindingTargetDesc
{
    BindingTargetKind kind = BindingTargetKind::invalid;
    TypeId type_id = invalid_type_id;
    bool bindable = false;
};

namespace binding_manifest_detail
{

constexpr BindingSet binding_set(generated::StBindingSet set) noexcept
{
    switch(set) {
    case generated::StBindingSet::iec_basic: return BindingSet::iec_basic;
    case generated::StBindingSet::plcopen_part1_part2:
        return BindingSet::plcopen_part1_part2;
    case generated::StBindingSet::plcopen_part4:
        return BindingSet::plcopen_part4;
    case generated::StBindingSet::plcopen_part5:
        return BindingSet::plcopen_part5;
    }
    return BindingSet::iec_basic;
}

constexpr PinDirection pin_direction(
    generated::StBindingPinDirection direction) noexcept
{
    switch(direction) {
    case generated::StBindingPinDirection::input:
        return PinDirection::input;
    case generated::StBindingPinDirection::output:
        return PinDirection::output;
    case generated::StBindingPinDirection::in_out:
        return PinDirection::in_out;
    case generated::StBindingPinDirection::unresolved:
        return PinDirection::input;
    }
    return PinDirection::input;
}

constexpr TypeId pin_type_id(std::string_view name) noexcept
{
    if(name == "BOOL") return builtin::bool_;
    if(name == "SINT") return builtin::sint;
    if(name == "INT") return builtin::int_;
    if(name == "DINT") return builtin::dint;
    if(name == "LINT") return builtin::lint;
    if(name == "USINT") return builtin::usint;
    if(name == "UINT") return builtin::uint_;
    if(name == "REAL") return builtin::real;
    if(name == "LREAL") return builtin::lreal;
    if(name == "TIME") return builtin::time;
    if(name == "UDINT") return builtin::udint;
    if(name == "ULINT") return builtin::ulint;
    if(name == "BYTE") return builtin::byte_;
    if(name == "WORD") return builtin::word;
    if(name == "DWORD") return builtin::dword;
    if(name == "LWORD") return builtin::lword;
    if(name == "DATE") return builtin::date;
    if(name == "TOD") return builtin::tod;
    if(name == "DT") return builtin::dt;
    for(const generated::StBindingTypeMetadata &metadata :
        generated::kStBindingTypes) {
        if(metadata.name == name) return metadata.type;
    }
    return invalid_type_id;
}

inline void append_unsigned(std::string &out, std::uint64_t value)
{
    char digits[20]{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while(value != 0U);
    while(count != 0U) out.push_back(digits[--count]);
}

} // namespace binding_manifest_detail

// Load-domain view generated from the complete checked-in authority catalog.
class BindingManifest
{
  public:
    std::size_t fb_count() const noexcept { return fbs_.size(); }

    const BindingFbDesc &fb(std::size_t index) const noexcept
    {
        return fbs_[index];
    }

    const BindingFbDesc *find(BindingFbId id) const noexcept
    {
        for(const BindingFbDesc &desc : fbs_) {
            if(desc.id == id) return &desc;
        }
        return nullptr;
    }

    const TypeTable &type_table() const noexcept { return types_; }

    std::size_t target_count() const noexcept { return targets_.size(); }

    const BindingTargetDesc &target(std::size_t index) const noexcept
    {
        return targets_[index];
    }

    std::string canonical_dump() const
    {
        std::string out;
        for(const BindingFbDesc &fb_desc : fbs_) {
            out += "fb ";
            binding_manifest_detail::append_unsigned(
                out, static_cast<std::uint32_t>(fb_desc.id));
            out += " set=";
            binding_manifest_detail::append_unsigned(
                out, static_cast<std::uint8_t>(fb_desc.set));
            out += " name=";
            out.append(fb_desc.lower_name.data(), fb_desc.lower_name.size());
            out += " source=";
            out.append(fb_desc.source.data(), fb_desc.source.size());
            out += " declared=";
            out += fb_desc.declared ? '1' : '0';
            out += " generated=";
            out += fb_desc.generated ? '1' : '0';
            out += " excluded=";
            out += fb_desc.excluded ? '1' : '0';
            out += " scope=";
            out.append(fb_desc.scope_reason.data(), fb_desc.scope_reason.size());
            out += " rejection=";
            out.append(fb_desc.rejection_test.data(),
                       fb_desc.rejection_test.size());
            out += '\n';
            for(std::size_t index = 0; index < fb_desc.pin_count; ++index) {
                const BindingPinDesc &pin = fb_desc.pins[index];
                out += "pin ";
                binding_manifest_detail::append_unsigned(out, pin.id);
                out += " name=";
                out.append(pin.lower_name.data(), pin.lower_name.size());
                out += " direction=";
                binding_manifest_detail::append_unsigned(
                    out, static_cast<std::uint8_t>(pin.direction));
                out += " type=";
                binding_manifest_detail::append_unsigned(out, pin.type_id);
                out += " declared=";
                out += pin.declared ? '1' : '0';
                out += " generated=";
                out += pin.generated ? '1' : '0';
                out += " excluded=";
                out += pin.excluded ? '1' : '0';
                out += " scope=";
                out.append(pin.scope_reason.data(), pin.scope_reason.size());
                out += " rejection=";
                out.append(pin.rejection_test.data(),
                           pin.rejection_test.size());
                out += '\n';
            }
        }
        for(const BindingTargetDesc &target_desc : targets_) {
            out += "target ";
            binding_manifest_detail::append_unsigned(
                out, static_cast<std::uint8_t>(target_desc.kind));
            out += " type=";
            binding_manifest_detail::append_unsigned(out, target_desc.type_id);
            out += " bindable=";
            out += target_desc.bindable ? '1' : '0';
            out += '\n';
        }
        return out;
    }

  private:
    friend const BindingManifest &binding_manifest();

    static constexpr std::size_t kMaxPins = 32;

    BindingManifest()
    {
        (void)install_binding_types(types_);
        for(std::size_t fb_index = 0;
            fb_index < generated::kStBindingFbs.size(); ++fb_index) {
            const generated::StBindingFbMetadata &source =
                generated::kStBindingFbs[fb_index];
            for(std::size_t pin_index = 0; pin_index < source.pin_count;
                ++pin_index) {
                const generated::StBindingPinMetadata &pin =
                    generated::kStBindingPins[source.first_pin + pin_index];
                pins_[fb_index][pin_index] = {
                    pin.stable_id, pin.name,
                    binding_manifest_detail::pin_direction(pin.direction),
                    binding_manifest_detail::pin_type_id(pin.st_type),
                    true, true, false, {}, {}};
            }
            fbs_[fb_index] = {
                static_cast<BindingFbId>(source.stable_id),
                binding_manifest_detail::binding_set(source.set),
                source.parser_key, source.source, true, true, false, {}, {},
                pins_[fb_index].data(), source.pin_count};
        }
    }

    TypeTable types_;
    std::array<std::array<BindingPinDesc, kMaxPins>,
               generated::kStBindingFbs.size()> pins_{};
    std::array<BindingFbDesc, generated::kStBindingFbs.size()> fbs_{};
    std::array<BindingTargetDesc, 2> targets_{{
        {BindingTargetKind::axis, binding_type::axis_ref, true},
        {BindingTargetKind::group, binding_type::group_ref, true},
    }};
};

inline const BindingManifest &binding_manifest()
{
    static const BindingManifest manifest;
    return manifest;
}

static_assert(BindingFbId::mc_cam_out != BindingFbId::mc_gear_out,
              "aliased native FBs require distinct binding identities");

} // namespace plcopen::core::st
