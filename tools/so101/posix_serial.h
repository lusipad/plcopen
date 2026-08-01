#pragma once

#include <cstddef>
#include <cstdint>

#include "read_only_probe.h"

namespace plcopen::tools::so101
{

class PosixSerial
{
public:
    PosixSerial() = default;
    ~PosixSerial();

    PosixSerial(const PosixSerial &) = delete;
    PosixSerial &operator=(const PosixSerial &) = delete;

    bool open_port(const char *path);
    bool discard_input();
    bool write_all(const std::uint8_t *bytes, std::size_t size);
    ReadResult read_byte(std::uint8_t &byte, int timeout_ms);

    const char *error_text() const;

private:
    int descriptor_ = -1;
    int error_number_ = 0;
};

} // namespace plcopen::tools::so101
