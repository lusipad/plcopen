#include <cstdint>
#include <cstdio>

#include "posix_serial.h"

namespace
{

using plcopen::tools::so101::ProbeResult;

int usage(const char *program)
{
    std::fprintf(stderr, "usage: %s /dev/ttyACM0\n", program);
    return 64;
}

} // namespace

int main(int argc, char **argv)
{
    if(argc != 2) return usage(argv[0]);

    plcopen::tools::so101::PosixSerial serial;
    if(!serial.open_port(argv[1])) {
        std::fprintf(stderr, "SO101_DISCOVER ERROR open=%s reason=%s\n",
                     argv[1], serial.error_text());
        return 1;
    }

    std::printf("SO101_DISCOVER mode=read-only instruction=PING "
                "baud=1000000 device=%s\n",
                argv[1]);
    std::size_t found = 0;
    for(std::uint8_t id = 1; id <= 6; ++id) {
        std::uint8_t status = 0;
        const ProbeResult result =
            plcopen::tools::so101::probe_servo(serial, id, 200, status);
        if(result == ProbeResult::found) {
            ++found;
            std::printf("SO101_DISCOVER found id=%u raw_status=0x%02X\n",
                        static_cast<unsigned int>(id),
                        static_cast<unsigned int>(status));
            continue;
        }
        if(result == ProbeResult::timeout) {
            std::printf("SO101_DISCOVER missing id=%u\n",
                        static_cast<unsigned int>(id));
            continue;
        }
        std::fprintf(stderr, "SO101_DISCOVER ERROR id=%u reason=%s\n",
                     static_cast<unsigned int>(id), serial.error_text());
        return 1;
    }

    if(found != 6) {
        std::printf("SO101_DISCOVER INCOMPLETE found=%zu expected=6\n", found);
        return 2;
    }
    std::printf("SO101_DISCOVER PASS found=6 expected=6\n");
    return 0;
}
