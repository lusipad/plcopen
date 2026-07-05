#pragma once

// BS6.3: CSP/CSV/CST mode-management skeleton (bumpless switching). The
// library's setpoints carry the full kinematic state every cycle, so a
// bumpless switch means: at the switch instant the field the next mode hands
// to the drive equals the drive's current state — no position, velocity, or
// torque step. This skeleton latches the last emitted setpoints on a switch
// and asserts the continuity contract; the drive-side handshake (modes of
// operation / display polling over the object dictionary) is B5 scope in the
// real-drive context.

#include <cmath>

#include "adapters/servo.h"
#include "rt/error.h"

namespace plcopen::core::adapters
{

enum class OperationMode : std::uint8_t
{
    csp, // cyclic synchronous position
    csv, // cyclic synchronous velocity
    cst, // cyclic synchronous torque
};

class ModeManager
{
public:
    OperationMode mode() const
    {
        return mode_;
    }

    // Bumpless switch: only defined when the stream is live (a latched
    // last-setpoints state exists) — the next mode continues from it.
    rt::ErrorCode switch_mode(OperationMode next)
    {
        if(!primed_) {
            return rt::ErrorCode::precondition_failed;
        }
        mode_ = next;
        return rt::ErrorCode::ok;
    }

    // Per-cycle emission: passes the full-order setpoints through and
    // latches them for the next switch; the primary field per mode is the
    // continuity-asserted channel.
    ServoSetpoints emit(const ServoSetpoints &setpoints)
    {
        last_ = setpoints;
        primed_ = true;
        return setpoints;
    }

    const ServoSetpoints &latched() const
    {
        return last_;
    }

    // The channel the drive consumes in the current mode.
    double primary_channel(const ServoSetpoints &setpoints) const
    {
        switch(mode_) {
        case OperationMode::csp:
            return setpoints.position;
        case OperationMode::csv:
            return setpoints.velocity;
        case OperationMode::cst:
            return setpoints.torque;
        }
        return setpoints.position;
    }

private:
    OperationMode mode_ = OperationMode::csp;
    ServoSetpoints last_{};
    bool primed_ = false;
};

} // namespace plcopen::core::adapters
