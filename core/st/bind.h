#pragma once

// RT-SAFE: cycle-path FB dispatch for the ST VM (approved st-l0-semantics
// 3.8-3.10). Instance init copies a value-initialized block image into the
// caller-owned static buffer; store/load/cycle are plain field access on the
// bound basic.h blocks. No allocation, no exceptions, integer cycle domain.

#include <cstdint>
#include <cstring>
#include <type_traits>

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

inline bool buffer_mode(std::int64_t value, axis::BufferMode &mode)
{
    switch(value) {
    case 0: mode = axis::BufferMode::aborting; return true;
    case 1: mode = axis::BufferMode::buffered; return true;
    case 2: mode = axis::BufferMode::blending_low; return true;
    case 5: mode = axis::BufferMode::blending_high; return true;
    default: return false;
    }
}

inline bool direction(std::int64_t value, axis::Direction &direction)
{
    switch(value) {
    case 0: direction = axis::Direction::current; return true;
    case 1: direction = axis::Direction::positive; return true;
    case -1: direction = axis::Direction::negative; return true;
    case 2: direction = axis::Direction::shortest_way; return true;
    default: return false;
    }
}

template <typename Block>
inline void store_move_common(Block &block, std::uint8_t pin,
                              std::int64_t value, std::uint8_t value_pin,
                              bool has_direction)
{
    store_axis_execute(block, pin, value);
    if(pin == 2) {
        return;
    }
    const double decoded = bits_double(static_cast<std::uint64_t>(value));
    if(pin == value_pin) {
        if constexpr(std::is_same_v<Block, fb::FbMoveAbsolute>) {
            block.position = decoded;
        } else {
            block.distance = decoded;
        }
    } else if(pin == value_pin + 1) {
        block.velocity = decoded;
    } else if(pin == value_pin + 2) {
        block.acceleration = decoded;
    } else if(pin == value_pin + 3) {
        block.deceleration = decoded;
    } else if(pin == value_pin + 4) {
        block.jerk = decoded;
    } else if(has_direction && pin == value_pin + 5) {
        if(!direction(value, block.direction)) block.axis_ref = nullptr;
    } else if(pin == value_pin + (has_direction ? 6 : 5)) {
        if(!buffer_mode(value, block.buffer_mode)) block.axis_ref = nullptr;
    }
}

template <typename Block>
inline void store_axis_execute(Block &block, std::uint8_t pin,
                               std::int64_t value)
{
    if(pin == 0) {
        block.axis_ref = reinterpret_cast<axis::AxisModel *>(
            static_cast<std::uintptr_t>(value));
    } else if(pin == 1) {
        block.execute = value != 0;
    }
}

inline std::int64_t load_motion_output(const fb::MotionOutputs &outputs,
                                       std::uint8_t output)
{
    switch(output) {
    case 0: return outputs.done ? 1 : 0;
    case 1: return outputs.busy ? 1 : 0;
    case 2: return outputs.active ? 1 : 0;
    case 3: return outputs.command_aborted ? 1 : 0;
    case 4: return outputs.error ? 1 : 0;
    default: return static_cast<std::int64_t>(outputs.error_id);
    }
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
    case FbType::mc_power: detail::init_block<fb::FbPower>(storage); break;
    case FbType::mc_home: detail::init_block<fb::FbHome>(storage); break;
    case FbType::mc_stop: detail::init_block<fb::FbStop>(storage); break;
    case FbType::mc_halt: detail::init_block<fb::FbHalt>(storage); break;
    case FbType::mc_move_absolute:
        detail::init_block<fb::FbMoveAbsolute>(storage); break;
    case FbType::mc_move_relative:
        detail::init_block<fb::FbMoveRelative>(storage); break;
    case FbType::mc_move_additive:
        detail::init_block<fb::FbMoveAdditive>(storage); break;
    case FbType::mc_move_velocity:
        detail::init_block<fb::FbMoveVelocity>(storage); break;
    case FbType::mc_set_override:
        detail::init_block<fb::FbSetOverride>(storage); break;
    case FbType::mc_reset: detail::init_block<fb::FbReset>(storage); break;
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
    case FbType::mc_power: {
        fb::FbPower *power = detail::block<fb::FbPower>(storage);
        if(pin == 0) {
            power->axis_ref = reinterpret_cast<axis::AxisModel *>(
                static_cast<std::uintptr_t>(value));
        } else if(pin == 1) {
            power->enable = value != 0;
        }
        break;
    }
    case FbType::mc_home: {
        fb::FbHome &home = *detail::block<fb::FbHome>(storage);
        detail::store_axis_execute(home, pin, value);
        if(pin == 2) {
            home.position = detail::bits_double(
                static_cast<std::uint64_t>(value));
        } else if(pin == 3) {
            if(!detail::buffer_mode(value, home.buffer_mode)) {
                home.axis_ref = nullptr;
            }
        }
        break;
    }
    case FbType::mc_stop: {
        fb::FbStop &stop = *detail::block<fb::FbStop>(storage);
        detail::store_axis_execute(stop, pin, value);
        if(pin == 2) {
            stop.deceleration = detail::bits_double(
                static_cast<std::uint64_t>(value));
        } else if(pin == 3) {
            stop.jerk = detail::bits_double(static_cast<std::uint64_t>(value));
        }
        break;
    }
    case FbType::mc_halt: {
        fb::FbHalt &halt = *detail::block<fb::FbHalt>(storage);
        detail::store_axis_execute(halt, pin, value);
        if(pin == 2) {
            halt.deceleration = detail::bits_double(
                static_cast<std::uint64_t>(value));
        } else if(pin == 3) {
            halt.jerk = detail::bits_double(static_cast<std::uint64_t>(value));
        } else if(pin == 4) {
            if(!detail::buffer_mode(value, halt.buffer_mode)) {
                halt.axis_ref = nullptr;
            }
        }
        break;
    }
    case FbType::mc_move_absolute:
        detail::store_move_common(*detail::block<fb::FbMoveAbsolute>(storage),
                                  pin, value, 3, true);
        break;
    case FbType::mc_move_relative:
        detail::store_move_common(*detail::block<fb::FbMoveRelative>(storage),
                                  pin, value, 3, false);
        break;
    case FbType::mc_move_additive:
        detail::store_move_common(*detail::block<fb::FbMoveAdditive>(storage),
                                  pin, value, 3, false);
        break;
    case FbType::mc_move_velocity: {
        fb::FbMoveVelocity &move =
            *detail::block<fb::FbMoveVelocity>(storage);
        detail::store_axis_execute(move, pin, value);
        if(pin == 2) move.continuous_update = value != 0;
        else if(pin == 3) move.velocity = detail::bits_double(value);
        else if(pin == 4) move.acceleration = detail::bits_double(value);
        else if(pin == 5) move.deceleration = detail::bits_double(value);
        else if(pin == 6) move.jerk = detail::bits_double(value);
        else if(pin == 7) {
            axis::Direction direction{};
            if(!detail::direction(value, direction) ||
               direction == axis::Direction::shortest_way) {
                move.axis_ref = nullptr;
            } else {
                move.direction = direction == axis::Direction::negative
                                     ? -1.0 : 1.0;
            }
        } else if(pin == 8 &&
                  !detail::buffer_mode(value, move.buffer_mode)) {
            move.axis_ref = nullptr;
        }
        break;
    }
    case FbType::mc_set_override: {
        fb::FbSetOverride &override =
            *detail::block<fb::FbSetOverride>(storage);
        if(pin == 0) override.axis_ref = reinterpret_cast<axis::AxisModel *>(
            static_cast<std::uintptr_t>(value));
        else if(pin == 1) override.enable = value != 0;
        else if(pin == 2) override.vel_factor = detail::bits_double(value);
        break;
    }
    case FbType::mc_reset:
        detail::store_axis_execute(*detail::block<fb::FbReset>(storage), pin,
                                   value);
        break;
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
    case FbType::mc_power: {
        const fb::FbPower *power = detail::block<fb::FbPower>(storage);
        switch(pin) {
        case 4: return power->status ? 1 : 0;
        case 5: return power->valid ? 1 : 0;
        case 6: return power->error ? 1 : 0;
        default: return static_cast<std::int64_t>(power->error_id);
        }
    }
    case FbType::mc_home:
        return detail::load_motion_output(
            detail::block<fb::FbHome>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 4));
    case FbType::mc_stop: {
        const fb::MotionOutputs &outputs =
            detail::block<fb::FbStop>(storage)->outputs;
        if(pin == 4) return outputs.done ? 1 : 0;
        if(pin == 5) return outputs.busy ? 1 : 0;
        if(pin == 6) return outputs.command_aborted ? 1 : 0;
        if(pin == 7) return outputs.error ? 1 : 0;
        return static_cast<std::int64_t>(outputs.error_id);
    }
    case FbType::mc_halt:
        return detail::load_motion_output(
            detail::block<fb::FbHalt>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 5));
    case FbType::mc_move_absolute:
        return detail::load_motion_output(
            detail::block<fb::FbMoveAbsolute>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 10));
    case FbType::mc_move_relative:
        return detail::load_motion_output(
            detail::block<fb::FbMoveRelative>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 9));
    case FbType::mc_move_additive:
        return detail::load_motion_output(
            detail::block<fb::FbMoveAdditive>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 9));
    case FbType::mc_move_velocity:
        return detail::load_motion_output(
            detail::block<fb::FbMoveVelocity>(storage)->outputs,
            static_cast<std::uint8_t>(pin - 9));
    case FbType::mc_set_override: {
        const fb::FbSetOverride &override =
            *detail::block<fb::FbSetOverride>(storage);
        if(pin == 5) return override.enabled ? 1 : 0;
        if(pin == 6) return 0;
        if(pin == 7) return override.error ? 1 : 0;
        return static_cast<std::int64_t>(override.error_id);
    }
    case FbType::mc_reset: {
        const fb::MotionOutputs &outputs =
            detail::block<fb::FbReset>(storage)->outputs;
        if(pin == 2) return outputs.done ? 1 : 0;
        if(pin == 3) return outputs.busy ? 1 : 0;
        if(pin == 4) return outputs.error ? 1 : 0;
        return static_cast<std::int64_t>(outputs.error_id);
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
    case FbType::mc_power: detail::block<fb::FbPower>(storage)->call(); break;
    case FbType::mc_home: detail::block<fb::FbHome>(storage)->call(); break;
    case FbType::mc_stop: detail::block<fb::FbStop>(storage)->call(); break;
    case FbType::mc_halt: detail::block<fb::FbHalt>(storage)->call(); break;
    case FbType::mc_move_absolute:
        detail::block<fb::FbMoveAbsolute>(storage)->call(); break;
    case FbType::mc_move_relative:
        detail::block<fb::FbMoveRelative>(storage)->call(); break;
    case FbType::mc_move_additive:
        detail::block<fb::FbMoveAdditive>(storage)->call(); break;
    case FbType::mc_move_velocity:
        detail::block<fb::FbMoveVelocity>(storage)->call(); break;
    case FbType::mc_set_override:
        detail::block<fb::FbSetOverride>(storage)->call(); break;
    case FbType::mc_reset: detail::block<fb::FbReset>(storage)->call(); break;
    }
}

} // namespace plcopen::core::st
