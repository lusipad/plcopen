#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "read_only_probe.h"

namespace
{

using plcopen::tools::so101::ProbeResult;
using plcopen::tools::so101::ReadResult;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

class FakeTransport
{
public:
    bool discard_input()
    {
        read_index_ = 0;
        return discard_ok;
    }

    bool write_all(const std::uint8_t *bytes, std::size_t size)
    {
        write_size = size;
        for(std::size_t i = 0; i < size; ++i) written[i] = bytes[i];
        return write_ok;
    }

    ReadResult read_byte(std::uint8_t &byte, int)
    {
        if(read_error) return ReadResult::error;
        if(read_index_ >= response_size) return ReadResult::timeout;
        byte = response[read_index_++];
        return ReadResult::byte;
    }

    bool discard_ok = true;
    bool write_ok = true;
    bool read_error = false;
    std::uint8_t written[32] = {};
    std::size_t write_size = 0;
    std::uint8_t response[32] = {};
    std::size_t response_size = 0;

private:
    std::size_t read_index_ = 0;
};

void set_status(FakeTransport &transport, std::uint8_t id, std::uint8_t status)
{
    plcopen::core::adapters::FeetechPacket packet{};
    plcopen::core::adapters::encode_status(id, status, nullptr, 0, packet);
    transport.response_size = packet.size;
    for(std::size_t i = 0; i < packet.size; ++i) {
        transport.response[i] = packet.bytes[i];
    }
}

int check_ping_only()
{
    FakeTransport transport;
    set_status(transport, 3, 0x12);
    std::uint8_t status = 0;
    const ProbeResult result =
        plcopen::tools::so101::probe_servo(transport, 3, 20, status);
    const std::uint8_t expected[] = {0xFF, 0xFF, 0x03, 0x02, 0x01, 0xF9};
    if(result != ProbeResult::found || status != 0x12 ||
       transport.write_size != sizeof(expected)) {
        return fail("matching response");
    }
    for(std::size_t i = 0; i < sizeof(expected); ++i) {
        if(transport.written[i] != expected[i]) return fail("ping packet only");
    }
    return 0;
}

int check_wrong_id_and_timeout()
{
    FakeTransport transport;
    set_status(transport, 4, 0);
    std::uint8_t status = 0xA5;
    if(plcopen::tools::so101::probe_servo(transport, 3, 20, status) !=
           ProbeResult::timeout ||
       status != 0xA5) {
        return fail("wrong id ignored");
    }
    transport.response_size = 0;
    if(plcopen::tools::so101::probe_servo(transport, 3, 20, status) !=
        ProbeResult::timeout) {
        return fail("timeout");
    }
    set_status(transport, 3, 0);
    transport.response[transport.response_size - 1] ^= 1u;
    if(plcopen::tools::so101::probe_servo(transport, 3, 20, status) !=
        ProbeResult::timeout) {
        return fail("bad checksum ignored");
    }
    return 0;
}

int check_errors_and_arguments()
{
    FakeTransport transport;
    std::uint8_t status = 0;
    if(plcopen::tools::so101::probe_servo(transport, 0, 20, status) !=
           ProbeResult::invalid_argument ||
       plcopen::tools::so101::probe_servo(transport, 0xFE, 20, status) !=
           ProbeResult::invalid_argument ||
       plcopen::tools::so101::probe_servo(transport, 1, 0, status) !=
           ProbeResult::invalid_argument) {
        return fail("invalid arguments");
    }
    transport.discard_ok = false;
    if(plcopen::tools::so101::probe_servo(transport, 1, 20, status) !=
        ProbeResult::io_error) {
        return fail("discard error");
    }
    transport.discard_ok = true;
    transport.write_ok = false;
    if(plcopen::tools::so101::probe_servo(transport, 1, 20, status) !=
        ProbeResult::io_error) {
        return fail("write error");
    }
    transport.write_ok = true;
    transport.read_error = true;
    if(plcopen::tools::so101::probe_servo(transport, 1, 20, status) !=
        ProbeResult::io_error) {
        return fail("read error");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_ping_only() != 0 || check_wrong_id_and_timeout() != 0 ||
       check_errors_and_arguments() != 0) {
        return 1;
    }
    std::printf("PASS SO-ARM101 read-only probe tests\n");
    return 0;
}
