#pragma once

#include "rt/error.h"

namespace plcopen::core::fb
{

enum class ExecuteStep
{
    busy,
    done,
    aborted,
    error,
};

class ExecuteLatch
{
public:
    bool done = false;
    bool busy = false;
    bool active = false;
    bool command_aborted = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void cycle(bool execute, ExecuteStep step, rt::ErrorCode step_error = rt::ErrorCode::ok)
    {
        if(!execute) {
            done = false;
            busy = false;
            active = false;
            command_aborted = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            was_execute_ = false;
            return;
        }

        if(!was_execute_ || busy || active) {
            apply(step, step_error);
        }
        was_execute_ = true;
    }

private:
    void apply(ExecuteStep step, rt::ErrorCode step_error)
    {
        done = false;
        command_aborted = false;
        error = false;
        error_id = rt::ErrorCode::ok;

        if(step == ExecuteStep::busy) {
            busy = true;
            active = true;
        } else if(step == ExecuteStep::done) {
            done = true;
            busy = false;
            active = false;
        } else if(step == ExecuteStep::aborted) {
            command_aborted = true;
            busy = false;
            active = false;
        } else {
            error = true;
            error_id = step_error == rt::ErrorCode::ok ? rt::ErrorCode::invalid_argument : step_error;
            busy = false;
            active = false;
        }
    }

    bool was_execute_ = false;
};

class ReadInfoLatch
{
public:
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void cycle(bool enable, bool source_valid, rt::ErrorCode source_error = rt::ErrorCode::ok)
    {
        if(!enable) {
            valid = false;
            busy = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        busy = false;
        valid = source_valid && source_error == rt::ErrorCode::ok;
        error = source_error != rt::ErrorCode::ok;
        error_id = source_error;
    }
};

class EnableReadFb
{
public:
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

protected:
    bool begin_enable()
    {
        if(!enable) {
            valid = false;
            busy = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            error_latched_ = false;
            latched_error_ = rt::ErrorCode::ok;
            return false;
        }
        if(error_latched_) {
            valid = false;
            busy = false;
            error = true;
            error_id = latched_error_;
            return false;
        }
        valid = false;
        busy = true;
        error = false;
        error_id = rt::ErrorCode::ok;
        return true;
    }

    void complete_enable()
    {
        valid = true;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

    void fail_enable(rt::ErrorCode code)
    {
        valid = false;
        busy = false;
        error = true;
        error_id = code;
        error_latched_ = true;
        latched_error_ = code;
    }

private:
    bool error_latched_ = false;
    rt::ErrorCode latched_error_ = rt::ErrorCode::ok;
};

class StartSyncPulse
{
public:
    bool start_sync = false;

    void complete(bool active_sync)
    {
        start_sync = active_sync && !emitted_;
        emitted_ = emitted_ || start_sync;
    }

    void reset()
    {
        start_sync = false;
        emitted_ = false;
    }

private:
    bool emitted_ = false;
};

} // namespace plcopen::core::fb
