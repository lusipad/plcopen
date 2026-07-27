// L1a conversion-matrix acceptance (approved st-l1a-semantics 4.x, metric
// 7.1): every supported <SRC>_TO_<DST> cell is exercised with boundary
// seeds against an independently written reference; unsupported cells must
// fail to resolve; NaN/Inf into integer domains must fault. --dump-matrix
// emits the full cell enumeration for the YAML three-way check
// (cmake/verify_st_l1a_conversions.cmake).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "st/st.h"

namespace
{

using namespace plcopen::core;

int failures = 0;

void fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    ++failures;
}

// --- independent reference semantics (oracle discipline) --------------------

std::uint64_t ref_mask(st::Type type)
{
    switch(st::width_bits(type)) {
    case 8: return 0xFFULL;
    case 16: return 0xFFFFULL;
    case 32: return 0xFFFFFFFFULL;
    default: return ~0ULL;
    }
}

std::uint64_t ref_canon(st::Type type, std::uint64_t raw)
{
    if(type == st::Type::bool_) {
        return raw ? 1 : 0;
    }
    const std::uint64_t masked = raw & ref_mask(type);
    if(st::is_signed_int(type) && st::width_bits(type) < 64) {
        const std::uint64_t sign = 1ULL << (st::width_bits(type) - 1);
        if(masked & sign) {
            return masked | ~ref_mask(type); // sign extend
        }
    }
    return masked;
}

double ref_double(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t ref_bits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Returns false when the conversion faults (NaN/Inf into integers).
bool ref_convert(st::Type from, st::Type to, std::uint64_t src,
                 std::uint64_t &dst)
{
    const bool from_intlike =
        st::is_integer(from) || st::is_bitstring(from) ||
        from == st::Type::bool_;
    const bool to_intlike =
        st::is_integer(to) || st::is_bitstring(to) || to == st::Type::bool_;
    if(from_intlike && to_intlike) {
        if(to == st::Type::bool_) {
            dst = src != 0 ? 1 : 0;
        } else {
            dst = ref_canon(to, src);
        }
        return true;
    }
    if(from_intlike && st::is_real_family(to)) {
        const double value = st::is_signed_int(from)
                                 ? static_cast<double>(
                                       static_cast<std::int64_t>(src))
                                 : static_cast<double>(src);
        dst = to == st::Type::real
                  ? ref_bits(static_cast<double>(static_cast<float>(value)))
                  : ref_bits(value);
        return true;
    }
    if(st::is_real_family(from) && to_intlike) {
        const double value = ref_double(src);
        if(to == st::Type::bool_) {
            dst = value != 0.0 ? 1 : 0;
            return true;
        }
        if(!std::isfinite(value)) {
            return false;
        }
        const double rounded = std::nearbyint(value);
        // modular reduction mirrors matrix 4.2 wrap; negation happens in
        // the integer domain (double cannot represent 2^64 - small_value)
        double m = std::fmod(rounded, 18446744073709551616.0);
        const bool negative = m < 0.0;
        if(negative) {
            m = -m;
        }
        std::uint64_t raw = 0;
        if(m >= 9223372036854775808.0) {
            raw = static_cast<std::uint64_t>(m - 9223372036854775808.0) +
                  0x8000000000000000ULL;
        } else {
            raw = static_cast<std::uint64_t>(m);
        }
        if(negative) {
            raw = 0ULL - raw;
        }
        dst = ref_canon(to, raw);
        return true;
    }
    if(from == st::Type::real && to == st::Type::lreal) {
        dst = src;
        return true;
    }
    if(from == st::Type::lreal && to == st::Type::real) {
        dst = ref_bits(static_cast<double>(
            static_cast<float>(ref_double(src))));
        return true;
    }
    dst = src; // TIME<->LINT identity
    return true;
}

// --- seed construction -------------------------------------------------------

struct Seed
{
    const char *text;    // ST source expression producing the value
    std::uint64_t bits;  // canonical bits it produces in the source type
};

int seeds_for(st::Type type, Seed out[8])
{
    switch(type) {
    case st::Type::bool_:
        out[0] = {"TRUE", 1};
        out[1] = {"FALSE", 0};
        return 2;
    case st::Type::sint:
        out[0] = {"127", 127};
        out[1] = {"-128", static_cast<std::uint64_t>(-128LL)};
        out[2] = {"-1", static_cast<std::uint64_t>(-1LL)};
        return 3;
    case st::Type::int_:
        out[0] = {"32767", 32767};
        out[1] = {"-32768", static_cast<std::uint64_t>(-32768LL)};
        return 2;
    case st::Type::dint:
        out[0] = {"2147483647", 2147483647};
        out[1] = {"-2147483648", static_cast<std::uint64_t>(-2147483648LL)};
        return 2;
    case st::Type::lint:
        out[0] = {"9223372036854775807", 9223372036854775807ULL};
        out[1] = {"-9223372036854775808", 0x8000000000000000ULL};
        return 2;
    case st::Type::usint:
        out[0] = {"255", 255};
        out[1] = {"1", 1};
        return 2;
    case st::Type::uint_:
        out[0] = {"65535", 65535};
        return 1;
    case st::Type::udint:
        out[0] = {"4294967295", 4294967295ULL};
        return 1;
    case st::Type::ulint:
        out[0] = {"18446744073709551615", ~0ULL};
        out[1] = {"9223372036854775808", 0x8000000000000000ULL};
        return 2;
    case st::Type::real:
        out[0] = {"1.5", ref_bits(1.5)};
        out[1] = {"-2.5", ref_bits(-2.5)};
        out[2] = {"3.5", ref_bits(3.5)};   // half-even tie -> 4
        out[3] = {"2.5", ref_bits(2.5)};   // half-even tie -> 2
        out[4] = {"300.0", ref_bits(300.0)};
        return 5;
    case st::Type::lreal:
        out[0] = {"1.5", ref_bits(1.5)};
        out[1] = {"-2.5", ref_bits(-2.5)};
        out[2] = {"1.0E18", ref_bits(1.0e18)};
        out[3] = {"-0.5", ref_bits(-0.5)};
        return 4;
    case st::Type::byte_:
        out[0] = {"16#AB", 0xAB};
        return 1;
    case st::Type::word:
        out[0] = {"16#BEEF", 0xBEEF};
        return 1;
    case st::Type::dword:
        out[0] = {"16#DEADBEEF", 0xDEADBEEFULL};
        return 1;
    case st::Type::lword:
        out[0] = {"16#0123456789ABCDEF", 0x0123456789ABCDEFULL};
        return 1;
    default:
        return 0;
    }
}

// --- harness -------------------------------------------------------------

alignas(8) unsigned char g_buffer[65536];
st::Instance g_instance;
st::CompileResult g_compiled;

bool run_conversion(const char *from_name, const char *to_name,
                    const char *seed_text, std::uint64_t &result_bits,
                    st::ScanError &error)
{
    std::string source = "PROGRAM c\nVAR src : ";
    source += from_name;
    source += "; dst : ";
    source += to_name;
    source += ";\nEND_VAR\nsrc := ";
    source += seed_text;
    source += ";\ndst := ";
    source += from_name;
    source += "_TO_";
    source += to_name;
    source += "(src);\nEND_PROGRAM\n";
    g_compiled = st::compile(source);
    if(!g_compiled.ok) {
        return false;
    }
    if(g_instance.load(g_compiled.program, g_buffer, sizeof(g_buffer),
                       1000000) != rt::ErrorCode::ok) {
        return false;
    }
    error = g_instance.scan(1000000);
    const int index = g_instance.find("dst");
    if(index < 0) {
        return false;
    }
    result_bits = static_cast<std::uint64_t>(
        g_instance.value_i64(static_cast<std::size_t>(index)));
    return true;
}

void full_matrix()
{
    int supported = 0;
    int unsupported = 0;
    for(int i = 0; i < st::kConvTypeCount; ++i) {
        for(int j = 0; j < st::kConvTypeCount; ++j) {
            const st::Type from = st::kConvTypes[i];
            const st::Type to = st::kConvTypes[j];
            if(from == to) {
                continue;
            }
            const st::ConvKind kind = st::conv_kind(from, to);
            std::string label = st::to_string(from);
            label += "_TO_";
            label += st::to_string(to);
            if(kind == st::ConvKind::unsupported) {
                ++unsupported;
                // the function must not exist (matrix: 未列即不存在)
                const std::string source =
                    std::string("PROGRAM c\nVAR a : ") + st::to_string(from) +
                    "; b : " + st::to_string(to) + ";\nEND_VAR\nb := " +
                    label + "(a);\nEND_PROGRAM\n";
                if(st::compile(source).ok) {
                    fail((label + " should not resolve").c_str());
                }
                continue;
            }
            ++supported;
            Seed seeds[8];
            const int count = seeds_for(from, seeds);
            for(int k = 0; k < count; ++k) {
                std::uint64_t got = 0;
                st::ScanError error = st::ScanError::ok;
                if(!run_conversion(st::to_string(from), st::to_string(to),
                                   seeds[k].text, got, error) ||
                   error != st::ScanError::ok) {
                    std::printf("  %s seed %s failed to run\n", label.c_str(),
                                seeds[k].text);
                    fail(label.c_str());
                    continue;
                }
                std::uint64_t expected = 0;
                if(!ref_convert(from, to, seeds[k].bits, expected)) {
                    fail((label + " unexpected fault expectation").c_str());
                    continue;
                }
                if(got != expected) {
                    std::printf("  %s(%s) got %llx want %llx\n", label.c_str(),
                                seeds[k].text,
                                static_cast<unsigned long long>(got),
                                static_cast<unsigned long long>(expected));
                    fail(label.c_str());
                }
            }
        }
    }
    // 15 types pairwise = 210 directed cells split between the two buckets.
    if(supported + unsupported != 210) {
        std::printf("  cell count %d\n", supported + unsupported);
        fail("cell enumeration must cover 210 pairs");
    }
}

void special_cells()
{
    // TIME <-> LINT identity (anchor L1a-4.6-time)
    {
        std::uint64_t got = 0;
        st::ScanError error = st::ScanError::ok;
        st::CompileResult r = st::compile(
            "PROGRAM c\nVAR t : TIME; n : LINT; u : TIME;\nEND_VAR\n"
            "t := T#1s500ms;\nn := TIME_TO_LINT(t);\nu := LINT_TO_TIME(n);\n"
            "END_PROGRAM\n");
        if(!r.ok) {
            fail("TIME cells compile");
        } else {
            st::Instance vm;
            vm.load(r.program, g_buffer, sizeof(g_buffer), 1000000);
            if(vm.scan(1000000) != st::ScanError::ok) {
                fail("TIME cells scan");
            } else {
                const int n = vm.find("n");
                const int u = vm.find("u");
                if(vm.value_i64(static_cast<std::size_t>(n)) !=
                       1500000000LL ||
                   vm.value_i64(static_cast<std::size_t>(u)) != 1500000000LL) {
                    fail("TIME ns identity");
                }
            }
        }
        (void)got;
        (void)error;
    }
    // TIME_TO_REAL must not exist
    if(st::compile("PROGRAM c\nVAR t : TIME; r : REAL;\nEND_VAR\n"
                   "r := TIME_TO_REAL(t);\nEND_PROGRAM\n")
           .ok) {
        fail("TIME_TO_REAL must not resolve");
    }

    // round-half-even ties (anchor L1a-4.3-round)
    struct Tie
    {
        const char *value;
        long long expect;
    };
    const Tie ties[] = {{"2.5", 2}, {"3.5", 4}, {"-2.5", -2}, {"-0.5", 0},
                        {"0.5", 0}, {"1.5", 2}};
    for(const Tie &tie : ties) {
        std::uint64_t got = 0;
        st::ScanError error = st::ScanError::ok;
        if(!run_conversion("LREAL", "INT", tie.value, got, error) ||
           error != st::ScanError::ok ||
           static_cast<long long>(got) != tie.expect) {
            std::printf("  tie %s got %lld\n", tie.value,
                        static_cast<long long>(got));
            fail("round-half-even tie");
        }
    }

    // TRUNC family (anchor L1a-4.8-trunc)
    {
        std::uint64_t got = 0;
        st::ScanError error = st::ScanError::ok;
        st::CompileResult r = st::compile(
            "PROGRAM c\nVAR a : DINT; b : DINT;\nEND_VAR\n"
            "a := TRUNC_DINT(2.9);\nb := TRUNC_DINT(-2.9);\nEND_PROGRAM\n");
        if(!r.ok) {
            fail("TRUNC compiles");
        } else {
            st::Instance vm;
            vm.load(r.program, g_buffer, sizeof(g_buffer), 1000000);
            if(vm.scan(1000000) != st::ScanError::ok ||
               vm.value_i64(static_cast<std::size_t>(vm.find("a"))) != 2 ||
               vm.value_i64(static_cast<std::size_t>(vm.find("b"))) != -2) {
                fail("TRUNC toward zero");
            }
        }
        (void)got;
        (void)error;
    }

    // NaN / Inf into integers fault (anchor L1a-4.3-fault); runtime path
    // via a variable so the fold cannot pre-detect it.
    {
        st::CompileResult r = st::compile(
            "PROGRAM c\nVAR z : LREAL; n : DINT;\nEND_VAR\n"
            "n := LREAL_TO_DINT(1.0 / z);\nEND_PROGRAM\n");
        if(!r.ok) {
            fail("Inf fault program compiles");
        } else {
            st::Instance vm;
            vm.load(r.program, g_buffer, sizeof(g_buffer), 1000000);
            if(vm.scan(1000000) != st::ScanError::conversion_invalid) {
                fail("Inf into DINT faults");
            }
            vm.reset();
            if(vm.fault() != st::ScanError::ok) {
                fail("conversion fault resets");
            }
        }
    }
    // NaN into BOOL is TRUE (nonzero), never a fault
    {
        std::uint64_t got = 0;
        st::ScanError error = st::ScanError::ok;
        if(!run_conversion("LREAL", "BOOL", "0.0 / 0.0", got, error) ||
           error != st::ScanError::ok || got != 1) {
            fail("NaN to BOOL is TRUE");
        }
    }
    // minus zero to BOOL is FALSE
    {
        std::uint64_t got = 0;
        st::ScanError error = st::ScanError::ok;
        if(!run_conversion("LREAL", "BOOL", "-0.0", got, error) ||
           error != st::ScanError::ok || got != 0) {
            fail("minus zero to BOOL is FALSE");
        }
    }
}

void public_conversion_resolution_boundaries()
{
    if(st::conv_kind(st::Type::dint, st::Type::dint) !=
           st::ConvKind::unsupported ||
       st::conv_kind(st::Type::time, st::Type::lint) !=
           st::ConvKind::identity ||
       st::conv_kind(st::Type::lint, st::Type::time) !=
           st::ConvKind::identity ||
       st::conv_kind(st::Type::time, st::Type::dint) !=
           st::ConvKind::unsupported ||
       st::conv_kind(st::Type::bool_, st::Type::real) !=
           st::ConvKind::int_to_float ||
       st::conv_kind(st::Type::udint, st::Type::lreal) !=
           st::ConvKind::uint_to_float ||
       st::conv_kind(st::Type::lreal, st::Type::bool_) !=
           st::ConvKind::to_bool_float ||
       std::strcmp(st::to_string(static_cast<st::ConvKind>(255)), "?") != 0) {
        fail("public conversion kind boundaries");
    }
    for(int value = static_cast<int>(st::ConvKind::unsupported);
        value <= static_cast<int>(st::ConvKind::to_bool_float); ++value) {
        if(std::strcmp(st::to_string(static_cast<st::ConvKind>(value)), "?") ==
           0) {
            fail("public conversion kind names");
            break;
        }
    }

    st::ConvDesc desc;
    if(!st::resolve_conversion("trunc_int", desc) ||
       desc.to != st::Type::int_ || !desc.trunc ||
       !st::resolve_conversion("trunc_dint", desc) ||
       desc.to != st::Type::dint || !desc.trunc ||
       !st::resolve_conversion("trunc_lint", desc) ||
       desc.to != st::Type::lint || !desc.trunc ||
       st::resolve_conversion("dint", desc) ||
       st::resolve_conversion("unknown_to_dint", desc) ||
       st::resolve_conversion("dint_to_unknown", desc) ||
       st::resolve_conversion("dint_to_dint", desc) ||
       !st::resolve_conversion("dint_to_lreal", desc) ||
       desc.from != st::Type::dint || desc.to != st::Type::lreal ||
       desc.kind != st::ConvKind::int_to_float || desc.trunc) {
        fail("public conversion name resolution boundaries");
    }

    if(st::type_id(static_cast<st::Type>(255)) != st::invalid_type_id ||
       st::width_bits(st::Type::bool_) != 1 ||
       st::widens_to(st::Type::dint, st::Type::dint) ||
       st::detail::wrap_double_to_u64(9223372036854775808.0) !=
           0x8000000000000000ULL ||
       std::strcmp(st::to_string(static_cast<st::Type>(255)), "?") != 0) {
        fail("public type helper boundaries");
    }
}

void dump_matrix()
{
    for(int i = 0; i < st::kConvTypeCount; ++i) {
        for(int j = 0; j < st::kConvTypeCount; ++j) {
            const st::Type from = st::kConvTypes[i];
            const st::Type to = st::kConvTypes[j];
            if(from == to) {
                continue;
            }
            std::printf("%s %s %s\n", st::to_string(from), st::to_string(to),
                        st::to_string(st::conv_kind(from, to)));
        }
    }
    std::printf("TIME LINT identity\n");
    std::printf("LINT TIME identity\n");
    std::printf("TRUNC INT float_round\n");
    std::printf("TRUNC DINT float_round\n");
    std::printf("TRUNC LINT float_round\n");
}

} // namespace

int main(int argc, char **argv)
{
    if(argc > 1 && std::strcmp(argv[1], "--dump-matrix") == 0) {
        dump_matrix();
        return 0;
    }
    full_matrix();
    special_cells();
    public_conversion_resolution_boundaries();
    if(failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("st l1a conversion tests passed (210 cells)\n");
    return 0;
}
