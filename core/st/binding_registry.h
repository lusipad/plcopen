#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "st/bind.h"
#include "st/binding_manifest.h"

namespace plcopen::core::st
{

using BindingInitOp = void (*)(FbType, unsigned char *, std::int64_t);
using BindingStoreOp = bool (*)(FbType, unsigned char *, std::uint16_t,
                                std::uint64_t);
using BindingStoreObjectOp = bool (*)(FbType, unsigned char *, std::uint8_t,
                                      const unsigned char *, TypeId,
                                      std::uint32_t);
using BindingLoadOp = bool (*)(FbType, const unsigned char *, std::uint16_t,
                               std::uint64_t &);
using BindingLoadObjectOp = bool (*)(FbType, const unsigned char *,
                                     std::uint8_t, unsigned char *, TypeId,
                                     std::uint32_t);
using BindingStoreSequenceOp = bool (*)(
    FbType, unsigned char *, std::uint16_t,
    generated::StBindingNativeSequenceValue);
using BindingLoadSequenceOp = bool (*)(
    FbType, const unsigned char *, std::uint16_t,
    generated::StBindingNativeSequenceValue &);
using BindingStoreTaggedReferenceOp = bool (*)(
    FbType, unsigned char *, std::uint16_t, TypeId,
    generated::StBindingNativeTaggedReferenceValue);
using BindingLoadTaggedReferenceOp = bool (*)(
    FbType, const unsigned char *, std::uint16_t, TypeId,
    generated::StBindingNativeTaggedReferenceValue &);
using BindingCycleOp = void (*)(FbType, unsigned char *, std::int64_t);

struct BindingDispatcher
{
    BindingFbId id = BindingFbId::invalid;
    FbType type = FbType::r_trig;
    std::uint64_t store_mask = 0;
    std::uint64_t load_mask = 0;
    std::uint64_t object_store_mask = 0;
    std::uint64_t object_load_mask = 0;
    std::uint64_t sequence_store_mask = 0;
    std::uint64_t sequence_load_mask = 0;
    std::uint64_t tagged_reference_store_mask = 0;
    std::uint64_t tagged_reference_load_mask = 0;
    BindingInitOp init_op = nullptr;
    BindingStoreOp store_op = nullptr;
    BindingStoreObjectOp store_object_op = nullptr;
    BindingLoadOp load_op = nullptr;
    BindingLoadObjectOp load_object_op = nullptr;
    BindingStoreSequenceOp store_sequence_op = nullptr;
    BindingLoadSequenceOp load_sequence_op = nullptr;
    BindingStoreTaggedReferenceOp store_tagged_reference_op = nullptr;
    BindingLoadTaggedReferenceOp load_tagged_reference_op = nullptr;
    BindingCycleOp cycle_op = nullptr;

    std::size_t storage_size() const { return fb_size(type); }

    bool stores(BindingPinId pin) const
    {
        return pin < 64U && (store_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool loads(BindingPinId pin) const
    {
        return pin < 64U && (load_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool stores_object(BindingPinId pin) const
    {
        return pin < 64U &&
               (object_store_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool loads_object(BindingPinId pin) const
    {
        return pin < 64U &&
               (object_load_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool stores_sequence(BindingPinId pin) const
    {
        return pin < 64U &&
               (sequence_store_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool loads_sequence(BindingPinId pin) const
    {
        return pin < 64U &&
               (sequence_load_mask & (std::uint64_t{1} << pin)) != 0U;
    }

    bool stores_tagged_reference(BindingPinId pin) const
    {
        return pin < 64U &&
               (tagged_reference_store_mask &
                (std::uint64_t{1} << pin)) != 0U;
    }

    bool loads_tagged_reference(BindingPinId pin) const
    {
        return pin < 64U &&
               (tagged_reference_load_mask &
                (std::uint64_t{1} << pin)) != 0U;
    }

    void init(unsigned char *storage, std::int64_t task_period_ns) const
    {
        init_op(type, storage, task_period_ns);
    }

    bool store(unsigned char *storage, BindingPinId pin,
               std::uint64_t value) const
    {
        return store_op(type, storage, pin, value);
    }

    bool load(const unsigned char *storage, BindingPinId pin,
              std::uint64_t &value) const
    {
        return load_op(type, storage, pin, value);
    }

    void cycle(unsigned char *storage, std::int64_t task_period_ns) const
    {
        cycle_op(type, storage, task_period_ns);
    }
};

namespace binding_registry_detail
{

inline constexpr BindingInitOp kInit = &fb_init;
inline constexpr BindingStoreOp kStore = &fb_store_scalar;
inline constexpr BindingStoreObjectOp kStoreObject = &fb_store_object;
inline constexpr BindingLoadOp kLoad = &fb_load_scalar;
inline constexpr BindingLoadObjectOp kLoadObject = &fb_load_object;
inline constexpr BindingStoreSequenceOp kStoreSequence = &fb_store_sequence;
inline constexpr BindingLoadSequenceOp kLoadSequence = &fb_load_sequence;
inline constexpr BindingStoreTaggedReferenceOp kStoreTaggedReference =
    &fb_store_tagged_reference;
inline constexpr BindingLoadTaggedReferenceOp kLoadTaggedReference =
    &fb_load_tagged_reference;
inline constexpr BindingCycleOp kCycle = &fb_cycle;

constexpr std::array<BindingDispatcher, generated::kStBindingFbs.size()>
make_dispatchers()
{
    std::array<BindingDispatcher, generated::kStBindingFbs.size()> result{};
    for(std::size_t index = 0; index < result.size(); ++index) {
        const generated::StBindingFbMetadata &metadata =
            generated::kStBindingFbs[index];
        const generated::StBindingNativeCapabilityMasks masks =
            generated::st_binding_native_capabilities(metadata.type);
        result[index] = {
            static_cast<BindingFbId>(metadata.stable_id), metadata.type,
            masks.store_scalar, masks.load_scalar, masks.store_object,
            masks.load_object, masks.store_sequence, masks.load_sequence,
            masks.store_tagged_reference, masks.load_tagged_reference,
            kInit, kStore, kStoreObject, kLoad, kLoadObject, kStoreSequence,
            kLoadSequence, kStoreTaggedReference, kLoadTaggedReference,
            kCycle};
    }
    return result;
}

inline constexpr auto kDispatchers = make_dispatchers();

} // namespace binding_registry_detail

inline const BindingDispatcher *binding_dispatcher(BindingFbId id) noexcept
{
    for(const BindingDispatcher &dispatcher :
        binding_registry_detail::kDispatchers) {
        if(dispatcher.id == id) return &dispatcher;
    }
    return nullptr;
}

inline bool binding_pin_registered(BindingFbId fb_id,
                                   BindingPinId pin_id) noexcept
{
    const BindingDispatcher *dispatcher = binding_dispatcher(fb_id);
    const BindingFbDesc *fb_desc = binding_manifest().find(fb_id);
    if(dispatcher == nullptr || fb_desc == nullptr ||
       pin_id >= fb_desc->pin_count) {
        return false;
    }
    const BindingPinDesc &pin = fb_desc->pins[pin_id];
    const TypeDesc *type = binding_manifest().type_table().get(pin.type_id);
    if(type == nullptr) return false;
    const auto stores = [&] {
        return dispatcher->stores(pin_id) || dispatcher->stores_object(pin_id) ||
               dispatcher->stores_sequence(pin_id) ||
               dispatcher->stores_tagged_reference(pin_id);
    };
    const auto loads = [&] {
        return dispatcher->loads(pin_id) || dispatcher->loads_object(pin_id) ||
               dispatcher->loads_sequence(pin_id) ||
               dispatcher->loads_tagged_reference(pin_id);
    };
    switch(pin.direction) {
    case PinDirection::input: return stores();
    case PinDirection::output: return loads();
    case PinDirection::in_out: return stores() && loads();
    }
    return false;
}

inline bool binding_fb_registered(BindingFbId id) noexcept
{
    const BindingDispatcher *dispatcher = binding_dispatcher(id);
    const BindingFbDesc *fb_desc = binding_manifest().find(id);
    if(dispatcher == nullptr || fb_desc == nullptr ||
       dispatcher->storage_size() == 0U || dispatcher->init_op == nullptr ||
       dispatcher->store_op == nullptr || dispatcher->load_op == nullptr ||
       dispatcher->store_object_op == nullptr ||
       dispatcher->load_object_op == nullptr ||
       dispatcher->store_sequence_op == nullptr ||
       dispatcher->load_sequence_op == nullptr ||
       dispatcher->store_tagged_reference_op == nullptr ||
       dispatcher->load_tagged_reference_op == nullptr ||
       dispatcher->cycle_op == nullptr) {
        return false;
    }
    for(std::size_t index = 0; index < fb_desc->pin_count; ++index) {
        if(!binding_pin_registered(id, fb_desc->pins[index].id)) return false;
    }
    return true;
}

static_assert(binding_registry_detail::kDispatchers.size() == 134U);

} // namespace plcopen::core::st
