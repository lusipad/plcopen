#pragma once

namespace plcopen::core::rt
{

enum class ErrorCode
{
    ok = 0,
    invalid_argument,
    out_of_range,
    capacity_exceeded,
    infeasible,
};

template <typename T> class Result
{
public:
    static constexpr Result success(const T &value)
    {
        return Result(true, ErrorCode::ok, value);
    }

    static constexpr Result failure(ErrorCode error)
    {
        return Result(false, error, T{});
    }

    constexpr explicit operator bool() const
    {
        return ok_;
    }

    constexpr bool ok() const
    {
        return ok_;
    }

    constexpr ErrorCode error() const
    {
        return error_;
    }

    constexpr const T &value() const
    {
        return value_;
    }

    constexpr T &value()
    {
        return value_;
    }

private:
    constexpr Result(bool ok, ErrorCode error, const T &value)
        : ok_(ok)
        , error_(error)
        , value_(value)
    {
    }

    bool ok_ = false;
    ErrorCode error_ = ErrorCode::invalid_argument;
    T value_{};
};

} // namespace plcopen::core::rt
