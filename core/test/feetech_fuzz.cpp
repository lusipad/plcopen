#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "adapters/feetech.h"

namespace
{

struct Lcg
{
    unsigned int state = 0xFE37EC42u;
    unsigned int next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
};

int iterations(int argc, char **argv)
{
    int count = 2000;
    for(int i = 1; i + 1 < argc; ++i) {
        if(std::strcmp(argv[i], "--iterations") == 0) count = std::atoi(argv[i + 1]);
    }
    return count > 0 ? count : 1;
}

} // namespace

int main(int argc, char **argv)
{
    plcopen::core::adapters::FeetechParser parser;
    plcopen::core::adapters::FeetechFrame frame{};
    Lcg random{};
    const int count = iterations(argc, argv);
    for(int i = 0; i < count; ++i) {
        const unsigned int bits = random.next();
        const int length = static_cast<int>((bits >> 24) & 0x3Fu);
        for(int j = 0; j < length; ++j) {
            parser.push(static_cast<unsigned char>(random.next() >> 16), frame);
            frame.ready = false;
        }
    }
    std::printf("PASS Feetech parser fuzz (%d inputs, zero crash)\n", count);
    return 0;
}
