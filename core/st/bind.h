#pragma once

// RT-SAFE: cycle-path FB dispatch for the ST VM (approved st-l0-semantics
// 3.8-3.10). Instance init copies a value-initialized block image into the
// caller-owned static buffer; store/load/cycle are plain field access on the
// bound basic.h blocks. No allocation, no exceptions, integer cycle domain.

#include <cstdint>
#include <cstring>

#include "fb/basic.h"
#include "st/pins.h"
#include "st/types.h"

namespace plcopen::core::st
{

namespace detail
{

template <typename Block>
inline void init_block(unsigned char *storage)
{
    const Block blank{};
    std::memcpy(storage, &blank, sizeof(Block));
}

template <typename Timer>
inline void init_timer(unsigned char *storage, std::int64_t period_ns)
{
    Timer blank{};
    blank.set_cycle_time(period_ns);
    std::memcpy(storage, &blank, sizeof(Timer));
}

template <typename Block>
inline Block *block(unsigned char *storage)
{
    return reinterpret_cast<Block *>(storage);
}

template <typename Block>
inline const Block *block(const unsigned char *storage)
{
    return reinterpret_cast<const Block *>(storage);
}

inline std::int64_t clamp_dint(std::int64_t value)
{
    // DINT readback wraps to 32 bits (matrix 1.8 wrap policy applied to the
    // binding boundary; declared in the implementation record).
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(value)));
}

} // namespace detail

// Load-domain instance construction (timers receive the task period so
// PT/ET stay in nanoseconds end-to-end; matrix 3.4).
inline void fb_init(FbType type, unsigned char *storage,
                    std::int64_t task_period_ns)
{
    switch(type) {
    case FbType::r_trig: detail::init_block<fb::RTrig>(storage); break;
    case FbType::f_trig: detail::init_block<fb::FTrig>(storage); break;
    case FbType::sr: detail::init_block<fb::SR>(storage); break;
    case FbType::rs: detail::init_block<fb::RS>(storage); break;
    case FbType::ton:
        detail::init_timer<fb::TON>(storage, task_period_ns);
        break;
    case FbType::tof:
        detail::init_timer<fb::TOF>(storage, task_period_ns);
        break;
    case FbType::tp:
        detail::init_timer<fb::TP>(storage, task_period_ns);
        break;
    case FbType::ctu: detail::init_block<fb::CTU>(storage); break;
    case FbType::ctd: detail::init_block<fb::CTD>(storage); break;
    case FbType::ctud: detail::init_block<fb::CTUD>(storage); break;
    }
}

// Cycle path: write one input pin (canonical int64 payload).
inline void fb_store(FbType type, unsigned char *storage, std::uint8_t pin,
                     std::int64_t value)
{
    switch(type) {
    case FbType::r_trig:
        detail::block<fb::RTrig>(storage)->clk = value != 0;
        break;
    case FbType::f_trig:
        detail::block<fb::FTrig>(storage)->clk = value != 0;
        break;
    case FbType::sr: {
        fb::SR *sr = detail::block<fb::SR>(storage);
        if(pin == 0) {
            sr->set = value != 0;
        } else {
            sr->reset = value != 0;
        }
        break;
    }
    case FbType::rs: {
        fb::RS *rs = detail::block<fb::RS>(storage);
        if(pin == 0) {
            rs->set = value != 0;
        } else {
            rs->reset = value != 0;
        }
        break;
    }
    case FbType::ton: {
        fb::TON *timer = detail::block<fb::TON>(storage);
        if(pin == 0) {
            timer->in = value != 0;
        } else {
            timer->pt = value;
        }
        break;
    }
    case FbType::tof: {
        fb::TOF *timer = detail::block<fb::TOF>(storage);
        if(pin == 0) {
            timer->in = value != 0;
        } else {
            timer->pt = value;
        }
        break;
    }
    case FbType::tp: {
        fb::TP *timer = detail::block<fb::TP>(storage);
        if(pin == 0) {
            timer->in = value != 0;
        } else {
            timer->pt = value;
        }
        break;
    }
    case FbType::ctu: {
        fb::CTU *counter = detail::block<fb::CTU>(storage);
        if(pin == 0) {
            counter->cu = value != 0;
        } else if(pin == 1) {
            counter->reset = value != 0;
        } else {
            counter->pv = value;
        }
        break;
    }
    case FbType::ctd: {
        fb::CTD *counter = detail::block<fb::CTD>(storage);
        if(pin == 0) {
            counter->cd = value != 0;
        } else if(pin == 1) {
            counter->load = value != 0;
        } else {
            counter->pv = value;
        }
        break;
    }
    case FbType::ctud: {
        fb::CTUD *counter = detail::block<fb::CTUD>(storage);
        if(pin == 0) {
            counter->cu = value != 0;
        } else if(pin == 1) {
            counter->cd = value != 0;
        } else if(pin == 2) {
            counter->reset = value != 0;
        } else if(pin == 3) {
            counter->load = value != 0;
        } else {
            counter->pv = value;
        }
        break;
    }
    }
}

// Cycle path: read one output pin (canonical int64 payload).
inline std::int64_t fb_load(FbType type, const unsigned char *storage,
                            std::uint8_t pin)
{
    switch(type) {
    case FbType::r_trig:
        return detail::block<fb::RTrig>(storage)->q ? 1 : 0;
    case FbType::f_trig:
        return detail::block<fb::FTrig>(storage)->q ? 1 : 0;
    case FbType::sr:
        return detail::block<fb::SR>(storage)->q ? 1 : 0;
    case FbType::rs:
        return detail::block<fb::RS>(storage)->q ? 1 : 0;
    case FbType::ton: {
        const fb::TON *timer = detail::block<fb::TON>(storage);
        return pin == 2 ? (timer->q ? 1 : 0) : timer->et;
    }
    case FbType::tof: {
        const fb::TOF *timer = detail::block<fb::TOF>(storage);
        return pin == 2 ? (timer->q ? 1 : 0) : timer->et;
    }
    case FbType::tp: {
        const fb::TP *timer = detail::block<fb::TP>(storage);
        return pin == 2 ? (timer->q ? 1 : 0) : timer->et;
    }
    case FbType::ctu: {
        const fb::CTU *counter = detail::block<fb::CTU>(storage);
        return pin == 3 ? (counter->q ? 1 : 0)
                        : detail::clamp_dint(counter->cv);
    }
    case FbType::ctd: {
        const fb::CTD *counter = detail::block<fb::CTD>(storage);
        return pin == 3 ? (counter->q ? 1 : 0)
                        : detail::clamp_dint(counter->cv);
    }
    case FbType::ctud: {
        const fb::CTUD *counter = detail::block<fb::CTUD>(storage);
        if(pin == 5) {
            return counter->qu ? 1 : 0;
        }
        if(pin == 6) {
            return counter->qd ? 1 : 0;
        }
        return detail::clamp_dint(counter->cv);
    }
    }
    return 0;
}

// Cycle path: run one block evaluation.
inline void fb_cycle(FbType type, unsigned char *storage)
{
    switch(type) {
    case FbType::r_trig: detail::block<fb::RTrig>(storage)->cycle(); break;
    case FbType::f_trig: detail::block<fb::FTrig>(storage)->cycle(); break;
    case FbType::sr: detail::block<fb::SR>(storage)->cycle(); break;
    case FbType::rs: detail::block<fb::RS>(storage)->cycle(); break;
    case FbType::ton: detail::block<fb::TON>(storage)->cycle(); break;
    case FbType::tof: detail::block<fb::TOF>(storage)->cycle(); break;
    case FbType::tp: detail::block<fb::TP>(storage)->cycle(); break;
    case FbType::ctu: detail::block<fb::CTU>(storage)->cycle(); break;
    case FbType::ctd: detail::block<fb::CTD>(storage)->cycle(); break;
    case FbType::ctud: detail::block<fb::CTUD>(storage)->cycle(); break;
    }
}

} // namespace plcopen::core::st
