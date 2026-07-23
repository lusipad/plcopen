#pragma once

#include "rt/error.h"

namespace plcopen::core::rt
{

struct ErrorDiagnostic
{
    ErrorCode code = ErrorCode::invalid_argument;
    const char *name = "unknown_error";
    const char *summary = "unknown error code";
    const char *hint =
        "upgrade the producer/consumer pair or inspect the raw error value before retrying";
};

constexpr ErrorDiagnostic diagnose(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::ok:
        return {code, "ok", "operation completed successfully",
                "continue with the next command; no recovery action is required"};
    case ErrorCode::invalid_argument:
        return {code, "invalid_argument", "a parameter value is out of its valid domain",
                "check references, NaN/Inf inputs, and dynamics or buffer-mode combinations"};
    case ErrorCode::out_of_range:
        return {code, "out_of_range", "a value exceeded a fixed bound or configured motion window",
                "inspect indices, limits, target positions, and table sizes before retrying"};
    case ErrorCode::capacity_exceeded:
        return {
            code, "capacity_exceeded", "a fixed-size container or queue is full",
            "wait for the queue to drain or reduce window depth instead of increasing RT storage"};
    case ErrorCode::infeasible:
        return {code, "infeasible", "the requested motion or solve has no feasible solution",
                "relax legal dynamics, extend the allowed time, or change the target path"};
    case ErrorCode::not_converged:
        return {code, "not_converged",
                "the bounded numerical solve did not meet its convergence gates",
                "start from a seed closer to the target or use the explicit best-effort result "
                "for diagnostics"};
    case ErrorCode::singular_region:
        return {code, "singular_region",
                "the numerical solve remains ill-conditioned at maximum damping",
                "change the target or seed to move away from the singular configuration"};
    case ErrorCode::limit_infeasible:
        return {code, "limit_infeasible",
                "hard limits prevent the numerical solve from reaching its target",
                "inspect the joint limits or choose a target reachable inside them"};
    case ErrorCode::precondition_failed:
        return {code, "precondition_failed", "the object is not in the required lifecycle state",
                "read the latest axis/group status and satisfy ownership, power, or standby "
                "preconditions first"};
    case ErrorCode::unsupported:
        return {code, "unsupported", "the requested operation is not implemented",
                "switch to a declared-supported path instead of retrying the same request"};
    case ErrorCode::bytecode_version_mismatch:
        return {code, "bytecode_version_mismatch",
                "the compiled ST bytecode version does not match the runtime",
                "recompile the ST program with the current toolchain or upgrade the runtime "
                "together with the artifact"};
    }
    return {code, "unknown_error", "unknown error code",
            "upgrade the producer/consumer pair or inspect the raw error value before retrying"};
}

} // namespace plcopen::core::rt
