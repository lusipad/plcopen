#pragma once

// BS6.2: CiA 402 (IEC 61800-7-201) drive state machine — pure software, no
// bus I/O. plcopen-fieldbus (B5) reuses this against real slaves; here a
// mock slave exercises every transition. Command and state names follow the
// standard's power state machine; the fault reaction path is modeled as an
// explicit state so the full ladder is test-coverable.
//
// Contract: no allocation, no exceptions; transition() is a pure function of
// (state, command/event).

#include <cstdint>

namespace plcopen::core::adapters
{

enum class Cia402State : std::uint8_t
{
    not_ready_to_switch_on,
    switch_on_disabled,
    ready_to_switch_on,
    switched_on,
    operation_enabled,
    quick_stop_active,
    fault_reaction_active,
    fault,
};

// Controlword commands (bit patterns abstracted to their standard names).
enum class Cia402Command : std::uint8_t
{
    shutdown,         // -> ready to switch on
    switch_on,        // -> switched on
    enable_operation, // -> operation enabled
    disable_voltage,  // -> switch on disabled
    quick_stop,       // -> quick stop active
    disable_operation, // -> switched on
    fault_reset,      // fault -> switch on disabled
};

class Cia402Machine
{
public:
    Cia402State state() const
    {
        return state_;
    }

    // Boot-up: the drive reports readiness after initialization.
    void initialize()
    {
        if(state_ == Cia402State::not_ready_to_switch_on) {
            state_ = Cia402State::switch_on_disabled;
        }
    }

    // Drive-side fault event (any state except the fault ladder itself).
    void fault_event()
    {
        if(state_ != Cia402State::fault && state_ != Cia402State::fault_reaction_active) {
            state_ = Cia402State::fault_reaction_active;
        }
    }

    // Fault reaction completed (controlled stop done inside the drive).
    void fault_reaction_complete()
    {
        if(state_ == Cia402State::fault_reaction_active) {
            state_ = Cia402State::fault;
        }
    }

    // Quick stop ramp completed: per 605Ah option code 1-4 the drive lands
    // in switch on disabled (the modeled default).
    void quick_stop_complete()
    {
        if(state_ == Cia402State::quick_stop_active) {
            state_ = Cia402State::switch_on_disabled;
        }
    }

    // Host command; returns false when the transition is not defined by the
    // standard's table (the state does not change).
    bool command(Cia402Command command)
    {
        switch(command) {
        case Cia402Command::shutdown:
            if(state_ == Cia402State::switch_on_disabled ||
               state_ == Cia402State::switched_on ||
               state_ == Cia402State::operation_enabled) {
                state_ = Cia402State::ready_to_switch_on;
                return true;
            }
            return false;
        case Cia402Command::switch_on:
            if(state_ == Cia402State::ready_to_switch_on) {
                state_ = Cia402State::switched_on;
                return true;
            }
            return false;
        case Cia402Command::enable_operation:
            if(state_ == Cia402State::switched_on ||
               state_ == Cia402State::quick_stop_active) {
                state_ = Cia402State::operation_enabled;
                return true;
            }
            return false;
        case Cia402Command::disable_voltage:
            if(state_ == Cia402State::ready_to_switch_on ||
               state_ == Cia402State::switched_on ||
               state_ == Cia402State::operation_enabled ||
               state_ == Cia402State::quick_stop_active) {
                state_ = Cia402State::switch_on_disabled;
                return true;
            }
            return false;
        case Cia402Command::quick_stop:
            if(state_ == Cia402State::operation_enabled) {
                state_ = Cia402State::quick_stop_active;
                return true;
            }
            if(state_ == Cia402State::ready_to_switch_on ||
               state_ == Cia402State::switched_on) {
                // Below operation enabled, quick stop acts as disable voltage.
                state_ = Cia402State::switch_on_disabled;
                return true;
            }
            return false;
        case Cia402Command::disable_operation:
            if(state_ == Cia402State::operation_enabled) {
                state_ = Cia402State::switched_on;
                return true;
            }
            return false;
        case Cia402Command::fault_reset:
            if(state_ == Cia402State::fault) {
                state_ = Cia402State::switch_on_disabled;
                return true;
            }
            return false;
        }
        return false;
    }

    bool operational() const
    {
        return state_ == Cia402State::operation_enabled;
    }

private:
    Cia402State state_ = Cia402State::not_ready_to_switch_on;
};

} // namespace plcopen::core::adapters
