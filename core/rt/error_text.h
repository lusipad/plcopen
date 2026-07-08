#pragma once

#include "rt/error.h"

namespace plcopen::core::rt
{

constexpr const char *to_string(ErrorCode code)
{
    switch(code) {
    case ErrorCode::ok:
        return "ok";
    case ErrorCode::invalid_argument:
        return "invalid_argument: a parameter value is out of its valid domain";
    case ErrorCode::out_of_range:
        return "out_of_range: a value exceeds an array or container bound";
    case ErrorCode::capacity_exceeded:
        return "capacity_exceeded: a fixed-size container is full";
    case ErrorCode::infeasible:
        return "infeasible: no solution exists for the given constraints";
    case ErrorCode::precondition_failed:
        return "precondition_failed: the object is not in the required state";
    case ErrorCode::unsupported:
        return "unsupported: this operation is not implemented";
    }
    return "unknown error code";
}

} // namespace plcopen::core::rt
