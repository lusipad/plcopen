// L0 front-end fuzz driver (approved st-l0-semantics 2.7, metric 5.2):
// crash-free compilation of arbitrary input. Three deterministic modes per
// iteration -- structured program generation, byte mutation of a valid seed,
// and raw token soup -- driven by a fixed-seed xorshift PRNG so every run is
// reproducible. The smoke tier runs in CTest; the nightly tier runs the
// full budget under ASan/UBSan (workflow core-nightly).
//
// Usage: st_fuzz [--iterations N] [--seed HEX]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "st/compile.h"

namespace
{

struct Rng
{
    std::uint64_t state;

    explicit Rng(std::uint64_t seed)
        : state(seed ? seed : 0x9E3779B97F4A7C15ULL)
    {
    }

    std::uint64_t next()
    {
        std::uint64_t x = state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state = x;
        return x;
    }

    std::uint32_t below(std::uint32_t bound)
    {
        return bound ? static_cast<std::uint32_t>(next() % bound) : 0;
    }
};

const char *const kFragments[] = {
    "PROGRAM", "END_PROGRAM", "VAR", "END_VAR", "IF", "THEN", "ELSIF",
    "ELSE", "END_IF", "CASE", "OF", "END_CASE", "FOR", "TO", "BY", "DO",
    "END_FOR", "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT",
    "EXIT", "RETURN", "AND", "OR", "XOR", "NOT", "MOD", "BOOL", "INT",
    "DINT", "REAL", "LREAL", "TIME", "TON", "CTU", "R_TRIG", "STRUCT",
    "ARRAY", "STRING", "TASK", ":=", ";", ":", ",", "..", ".", "(", ")",
    "+", "-", "*", "/", "<", ">", "<=", ">=", "<>", "=", "&", "16#FF",
    "2#10", "8#7", "T#1s", "T#", "TIME#5ms", "T#1.5s2h", "1.5e3", "1..",
    "SINT", "LINT", "USINT", "ULINT", "BYTE", "WORD", "DWORD", "LWORD",
    "CONSTANT", "CONTINUE", "**", "INT#5", "BYTE#16#FF", "LREAL#1.5",
    "SINT#-129", "ULINT#", "INT_TO_REAL", "LREAL_TO_DINT", "TRUNC_INT",
    "BOOL_TO_BYTE", "WORD_TO_UINT", "TIME_TO_LINT", "XXX_TO_YYY",
    "AXIS_REF", "MC_Power", "MC_Home", "MC_Stop", "MC_Halt",
    "MC_MoveAbsolute", "MC_MoveRelative", "MC_MoveAdditive",
    "MC_MoveVelocity", "MC_SetOverride", "MC_Reset", "Axis", "Execute",
    "BufferMode", "Direction", "ErrorID",
    "1__2", "x", "y", "zz_9", "TRUE", "FALSE", "32768", "-32769",
    "9999999999999999999999", "0.0", "(*", "*)", "//", "\n", "\t", "\"",
    "'", "#", "%", "@", "\x01", "\xFF", "\x80",
};

std::string structured_program(Rng &rng)
{
    std::string source = "PROGRAM f\nVAR\n";
    const int vars = 1 + static_cast<int>(rng.below(4));
    static const char *const kTypes[] = {
        "BOOL", "INT",   "DINT",  "REAL", "LREAL", "TIME",  "TON",
        "CTU",  "SINT",  "LINT",  "USINT", "UINT", "UDINT", "ULINT",
        "BYTE", "WORD",  "DWORD", "LWORD", "AXIS_REF", "MC_Power",
        "MC_MoveAbsolute"};
    for(int i = 0; i < vars; ++i) {
        source += "v";
        source += std::to_string(i);
        source += " : ";
        source += kTypes[rng.below(sizeof(kTypes) / sizeof(kTypes[0]))];
        source += ";\n";
    }
    source += "END_VAR\n";
    const int statements = static_cast<int>(rng.below(12));
    for(int i = 0; i < statements; ++i) {
        switch(rng.below(8)) {
        case 0:
            source += "v0 := v";
            source += std::to_string(rng.below(4));
            source += " + ";
            source += std::to_string(rng.below(70000));
            source += ";\n";
            break;
        case 1:
            source += "IF v0 > 1 THEN v1 := 1; ELSE v1 := 2; END_IF;\n";
            break;
        case 2:
            source += "FOR v0 := 1 TO ";
            source += std::to_string(rng.below(100));
            source += " DO v1 := v1 + 1; END_FOR;\n";
            break;
        case 3:
            source += "CASE v0 OF 1: ; 2..5: ; ELSE ; END_CASE;\n";
            break;
        case 4:
            source += "v0(IN := TRUE, PT := T#1ms);\n";
            break;
        default:
            source += "WHILE v0 < 3 DO v0 := v0 + 1; END_WHILE;\n";
            break;
        }
    }
    source += "END_PROGRAM\n";
    return source;
}

std::string token_soup(Rng &rng)
{
    std::string source;
    const int tokens = static_cast<int>(rng.below(120));
    for(int i = 0; i < tokens; ++i) {
        source += kFragments[rng.below(sizeof(kFragments) /
                                       sizeof(kFragments[0]))];
        if(rng.below(3) == 0) {
            source += ' ';
        }
    }
    return source;
}

std::string mutated_seed(Rng &rng)
{
    std::string source =
        "PROGRAM m\nVAR x : INT; t : TON; END_VAR\n"
        "x := 16#FF + 2 * (x - 1);\n"
        "IF x >= 5 THEN x := x MOD 3; END_IF;\n"
        "FOR x := 1 TO 10 BY 2 DO ; END_FOR;\n"
        "t(IN := x > 2, PT := T#1s500ms);\n"
        "CASE x OF 1, 3..4: x := 0; ELSE ; END_CASE;\n"
        "END_PROGRAM\n";
    const int mutations = 1 + static_cast<int>(rng.below(8));
    for(int i = 0; i < mutations; ++i) {
        const std::size_t at = rng.below(
            static_cast<std::uint32_t>(source.size()));
        source[at] = static_cast<char>(rng.next() & 0xFF);
    }
    return source;
}

} // namespace

int main(int argc, char **argv)
{
    long long iterations = 3000;
    std::uint64_t seed = 0xC0DEF00DULL;
    for(int i = 1; i < argc; ++i) {
        if(i + 1 < argc && std::strcmp(argv[i], "--iterations") == 0) {
            iterations = std::atoll(argv[i + 1]);
            ++i;
        } else if(i + 1 < argc && std::strcmp(argv[i], "--seed") == 0) {
            seed = std::strtoull(argv[i + 1], nullptr, 16);
            ++i;
        }
    }

    Rng rng(seed);
    long long compiled_ok = 0;
    for(long long i = 0; i < iterations; ++i) {
        std::string source;
        switch(rng.below(3)) {
        case 0: source = structured_program(rng); break;
        case 1: source = token_soup(rng); break;
        default: source = mutated_seed(rng); break;
        }
        const plcopen::core::st::CompileResult result =
            plcopen::core::st::compile(source);
        if(result.ok) {
            ++compiled_ok;
        }
        // Crash-free is the contract; diagnostics content is not asserted
        // here. A defensive floor: a compile must always produce either a
        // program or at least one diagnostic.
        if(!result.ok && result.diagnostics.empty()) {
            std::printf("FUZZ_FAIL silent failure at iteration %lld\n", i);
            return 1;
        }
    }
    std::printf("FUZZ_PASS seed=0x%llX iterations=%lld ok=%lld\n",
                static_cast<unsigned long long>(seed), iterations,
                compiled_ok);
    return 0;
}
