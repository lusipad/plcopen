// A1 floating-point contract: strict compilation, default IEEE environment,
// per-op REAL/LREAL rounding and portable transcendental tolerances.

#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "st/st.h"

#if defined(__FAST_MATH__)
#error "plcopen::core forbids fast-math"
#endif

#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0
#error "plcopen::core requires NaN and infinity semantics"
#endif

static_assert(FLT_RADIX == 2, "plcopen::core requires radix-2 floating point");
static_assert(FLT_EVAL_METHOD == 0,
              "plcopen::core requires source-precision evaluation");
static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::digits == 24,
              "REAL requires IEEE 754 binary32");
static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53,
              "LREAL requires IEEE 754 binary64");

namespace
{

using namespace plcopen::core;

int failures = 0;

void check(bool condition, const char *name)
{
    if(!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
double contraction_probe(double a, double b, double c)
{
    return a * b + c;
}

bool close_lreal(double actual, double expected)
{
    const double scale = std::fmax(1.0, std::fabs(expected));
    return std::fabs(actual - expected) <=
           8.0 * std::numeric_limits<double>::epsilon() * scale;
}

struct Rig
{
    st::CompileResult compiled;
    st::Instance instance;
    alignas(8) unsigned char buffer[65536] = {};

    bool build(const std::string &source)
    {
        compiled = st::compile(source);
        if(!compiled.ok) {
            for(const st::Diagnostic &diagnostic : compiled.diagnostics) {
                std::printf("  diag %d:%d %s\n", diagnostic.line,
                            diagnostic.column, diagnostic.message.c_str());
            }
            return false;
        }
        return instance.load(compiled.program, buffer, sizeof(buffer),
                             1000000) == rt::ErrorCode::ok;
    }

    double value(const char *name) const
    {
        const int index = instance.find(name);
        return index < 0 ? std::numeric_limits<double>::quiet_NaN()
                         : instance.value_f64(static_cast<std::size_t>(index));
    }
};

void host_environment_contract()
{
    check(std::fegetround() == FE_TONEAREST, "round-to-nearest-even environment");

    volatile double denormal = std::numeric_limits<double>::denorm_min();
    volatile double one = 1.0;
    check(denormal * one == denormal, "gradual underflow environment");

    volatile double a = 1.0 + 0x1p-27;
    volatile double b = 1.0 - 0x1p-27;
    volatile double c = -1.0;
    check(contraction_probe(a, b, c) == 0.0,
          "multiply-add contraction disabled");
}

void st_runtime_contract()
{
    const char *source = R"ST(
PROGRAM p
VAR
  ra : REAL := REAL#1.00000011920928955078125;
  rb : REAL := REAL#0.999999940395355224609375;
  rmul : REAL;
  rsum : REAL;
  la : LREAL := LREAL#1.000000007450580596923828125;
  lb : LREAL := LREAL#0.999999992549419403076171875;
  lmul : LREAL;
  lprobe : LREAL;
  zero : LREAL;
  negzero : LREAL;
  pinf : LREAL;
  qnan : LREAL;
  sqrt2 : LREAL;
  ln2 : LREAL;
  exp1 : LREAL;
  sin1 : LREAL;
  cos1 : LREAL;
  atan1 : LREAL;
END_VAR
rmul := ra * rb;
rsum := rmul + rb;
lmul := la * lb;
lprobe := lmul - 1.0;
negzero := -zero;
pinf := DIV(1.0, zero);
qnan := DIV(zero, zero);
sqrt2 := SQRT(2.0);
ln2 := LN(2.0);
exp1 := EXP(1.0);
sin1 := SIN(1.0);
cos1 := COS(1.0);
atan1 := ATAN(1.0);
END_PROGRAM
)ST";

    Rig rig;
    check(rig.build(source), "numeric contract program builds");
    if(!rig.compiled.ok) return;
    check(rig.instance.scan(1000000) == st::ScanError::ok,
          "numeric contract program scans");

    check(rig.value("rmul") == 1.0, "REAL rounds after multiply");
    check(rig.value("rsum") == 2.0,
          "REAL half-ULP tie rounds to even");
    check(rig.value("lmul") == 1.0 && rig.value("lprobe") == 0.0,
          "LREAL multiply-add remains two operations");
    check(rig.value("negzero") == 0.0 && std::signbit(rig.value("negzero")),
          "signed zero preserved");
    check(std::isinf(rig.value("pinf")) && rig.value("pinf") > 0.0,
          "positive infinity preserved");
    check(std::isnan(rig.value("qnan")), "NaN classification preserved");

    check(close_lreal(rig.value("sqrt2"), 1.4142135623730950488),
          "SQRT cross-platform tolerance");
    check(close_lreal(rig.value("ln2"), 0.69314718055994530942),
          "LN cross-platform tolerance");
    check(close_lreal(rig.value("exp1"), 2.7182818284590452354),
          "EXP cross-platform tolerance");
    check(close_lreal(rig.value("sin1"), 0.84147098480789650665),
          "SIN cross-platform tolerance");
    check(close_lreal(rig.value("cos1"), 0.5403023058681397174),
          "COS cross-platform tolerance");
    check(close_lreal(rig.value("atan1"), 0.78539816339744830962),
          "ATAN cross-platform tolerance");
}

} // namespace

int main()
{
    host_environment_contract();
    st_runtime_contract();
    if(failures == 0) {
        std::printf("st_numeric_semantics_tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
