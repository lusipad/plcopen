#pragma once

// RT-SAFE: generated cycle-path FB dispatch for the ST VM. Native instances
// are placement-constructed in caller-owned storage; every scalar/object
// transfer is validated by the generated capability and type authorities.

#include <cstdint>

#include "fb/basic.h"
#include "st/pins.h"

namespace plcopen::core::st
{

inline void fb_init(FbType type, unsigned char *storage,
                    std::int64_t task_period_ns)
{
    if(!generated::st_binding_default_construct(type, storage)) return;
    switch(type) {
    case FbType::ton:
        static_cast<fb::TON *>(static_cast<void *>(storage))
            ->set_cycle_time(task_period_ns);
        break;
    case FbType::tof:
        static_cast<fb::TOF *>(static_cast<void *>(storage))
            ->set_cycle_time(task_period_ns);
        break;
    case FbType::tp:
        static_cast<fb::TP *>(static_cast<void *>(storage))
            ->set_cycle_time(task_period_ns);
        break;
    default: break;
    }
}

inline void fb_destroy(FbType type, unsigned char *storage) noexcept
{
    (void)generated::st_binding_destruct(type, storage);
}

inline bool fb_store_scalar(FbType type, unsigned char *storage,
                            std::uint16_t pin, std::uint64_t bits) noexcept
{
    return generated::st_binding_store_scalar(type, pin, storage, bits);
}

inline bool fb_load_scalar(FbType type, const unsigned char *storage,
                           std::uint16_t pin, std::uint64_t &bits) noexcept
{
    return generated::st_binding_load_scalar(type, pin, storage, bits);
}

inline bool fb_store_object(FbType type, unsigned char *storage,
                            std::uint8_t pin, const unsigned char *bytes,
                            TypeId type_id, std::uint32_t size) noexcept
{
    return generated::st_binding_store_object(type, pin, storage, bytes,
                                               type_id, size);
}

inline bool fb_load_object(FbType type, const unsigned char *storage,
                           std::uint8_t pin, unsigned char *bytes,
                           TypeId type_id, std::uint32_t size) noexcept
{
    return generated::st_binding_load_object(type, pin, storage, bytes,
                                              type_id, size);
}

inline bool fb_store_sequence(
    FbType type, unsigned char *storage, std::uint16_t pin,
    generated::StBindingNativeSequenceValue value) noexcept
{
    return generated::st_binding_store_sequence(type, pin, storage, value);
}

inline bool fb_load_sequence(
    FbType type, const unsigned char *storage, std::uint16_t pin,
    generated::StBindingNativeSequenceValue &value) noexcept
{
    return generated::st_binding_load_sequence(type, pin, storage, value);
}

inline bool fb_store_tagged_reference(
    FbType type, unsigned char *storage, std::uint16_t pin, TypeId type_id,
    generated::StBindingNativeTaggedReferenceValue value) noexcept
{
    return generated::st_binding_store_tagged_reference(
        type, pin, storage, type_id, value);
}

inline bool fb_load_tagged_reference(
    FbType type, const unsigned char *storage, std::uint16_t pin,
    TypeId type_id,
    generated::StBindingNativeTaggedReferenceValue &value) noexcept
{
    return generated::st_binding_load_tagged_reference(
        type, pin, storage, type_id, value);
}

inline void fb_cycle(FbType type, unsigned char *storage,
                     std::int64_t task_period_ns = 0)
{
    // Timer periods are installed once in the load domain. The generated
    // standalone invoke helper accepts an explicit period, so VM scans keep
    // the timer's installed value through its direct cycle entry.
    switch(type) {
    case FbType::ton:
        static_cast<fb::TON *>(static_cast<void *>(storage))->cycle();
        return;
    case FbType::tof:
        static_cast<fb::TOF *>(static_cast<void *>(storage))->cycle();
        return;
    case FbType::tp:
        static_cast<fb::TP *>(static_cast<void *>(storage))->cycle();
        return;
    default: break;
    }
    (void)generated::st_binding_invoke(type, storage, task_period_ns);
}

} // namespace plcopen::core::st
