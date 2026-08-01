#pragma once

#include <chrono>
#include <cstdint>

#include "adapters/feetech.h"

namespace plcopen::tools::so101
{

enum class ReadResult
{
    byte,
    timeout,
    error,
};

enum class ProbeResult
{
    found,
    timeout,
    io_error,
    invalid_argument,
};

template<typename Transport>
ProbeResult probe_servo(Transport &transport, std::uint8_t id,
                        int timeout_ms, std::uint8_t &status)
{
    if(id == 0 || id >= 0xFE || timeout_ms <= 0) {
        return ProbeResult::invalid_argument;
    }
    if(!transport.discard_input()) return ProbeResult::io_error;

    core::adapters::FeetechPacket ping{};
    if(core::adapters::encode_instruction(
           id, core::adapters::FeetechInstruction::ping, nullptr, 0, ping) !=
       core::rt::ErrorCode::ok) {
        return ProbeResult::invalid_argument;
    }
    if(!transport.write_all(ping.bytes, ping.size)) {
        return ProbeResult::io_error;
    }

    core::adapters::FeetechParser parser;
    core::adapters::FeetechFrame frame{};
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while(true) {
        const auto now = std::chrono::steady_clock::now();
        if(now >= deadline) return ProbeResult::timeout;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        if(remaining < 1) remaining = 1;

        std::uint8_t byte = 0;
        const ReadResult read =
            transport.read_byte(byte, static_cast<int>(remaining));
        if(read == ReadResult::timeout) return ProbeResult::timeout;
        if(read == ReadResult::error) return ProbeResult::io_error;

        parser.push(byte, frame);
        if(frame.ready && frame.id == id && frame.parameter_count == 0) {
            status = frame.error;
            return ProbeResult::found;
        }
    }
}

} // namespace plcopen::tools::so101
