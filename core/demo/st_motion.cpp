#include "axis/state.h"
#include "rt/error_text.h"
#include "st/st.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{

using namespace plcopen::core;

constexpr const char *kSource = R"(PROGRAM motion_demo
VAR
  AxisX : AXIS_REF;
  Power : MC_Power;
  Move : MC_MoveAbsolute;
  Powered : BOOL;
  Busy : BOOL;
  Done : BOOL;
  Error : BOOL;
  ErrorID : DINT;
END_VAR
Power(Axis := AxisX, Enable := TRUE);
Move(
  Axis := AxisX,
  Execute := Power.Status,
  ContinuousUpdate := FALSE,
  Position := 1.25,
  Velocity := 1.0,
  Acceleration := 2.0,
  Deceleration := 2.0,
  Jerk := 10.0,
  Direction := MC_DIRECTION#current,
  BufferMode := MC_BUFFER_MODE#aborting);
Powered := Power.Status;
Busy := Move.Busy;
Done := Move.Done;
Error := Move.Error;
ErrorID := Move.ErrorID;
END_PROGRAM
)";

std::vector<std::uint64_t> make_storage(const st::Program &program)
{
    return std::vector<std::uint64_t>((program.required_bytes() + 7U) / 8U + 1U);
}

int fail_compile(const st::CompileResult &compiled)
{
    std::cerr << "st motion demo: compile failed\n";
    for (const st::Diagnostic &diagnostic : compiled.diagnostics)
    {
        std::cerr << "  line " << diagnostic.line << ", column " << diagnostic.column << ": "
                  << st::to_string(diagnostic.code);
        if (!diagnostic.message.empty())
        {
            std::cerr << " - " << diagnostic.message;
        }
        std::cerr << '\n';
    }
    return 1;
}

int fail_runtime(const char *stage, rt::ErrorCode error)
{
    std::cerr << "st motion demo: " << stage << " failed: " << rt::to_string(error) << '\n';
    return 1;
}

int fail_scan(st::ScanError error, int cycle)
{
    std::cerr << "st motion demo: scan failed at cycle " << cycle << ": " << st::to_string(error)
              << '\n';
    return 1;
}

} // namespace

int main()
{
    const st::CompileResult compiled = st::compile(kSource);
    if (!compiled.ok)
    {
        return fail_compile(compiled);
    }

    std::vector<std::uint64_t> storage = make_storage(compiled.program);
    st::Instance instance;
    axis::AxisModel axis;

    const rt::ErrorCode loaded =
        instance.load(compiled.program, reinterpret_cast<unsigned char *>(storage.data()),
                      storage.size() * sizeof(storage[0]), 1000000);
    if (loaded != rt::ErrorCode::ok)
    {
        return fail_runtime("load", loaded);
    }

    if (instance.bind_axis("AxisX", &axis) != st::BindingError::ok)
    {
        std::cerr << "st motion demo: bind_axis failed\n";
        return 1;
    }

    const int powered_index = instance.find("Powered");
    const int busy_index = instance.find("Busy");
    const int done_index = instance.find("Done");
    const int error_index = instance.find("Error");
    const int error_id_index = instance.find("ErrorID");
    if (powered_index < 0 || busy_index < 0 || done_index < 0 || error_index < 0 ||
        error_id_index < 0)
    {
        std::cerr << "st motion demo: demo symbols not found\n";
        return 1;
    }

    bool done = false;
    bool saw_powered = false;
    int cycle = 0;
    for (; cycle < 4000; ++cycle)
    {
        const st::ScanError scan = instance.scan(512);
        if (scan != st::ScanError::ok)
        {
            return fail_scan(scan, cycle);
        }

        saw_powered = saw_powered || instance.value_bool(static_cast<std::size_t>(powered_index));
        if (instance.value_bool(static_cast<std::size_t>(error_index)))
        {
            const auto code = static_cast<rt::ErrorCode>(
                instance.value_i64(static_cast<std::size_t>(error_id_index)));
            std::cerr << "st motion demo: motion FB error: " << rt::to_string(code) << '\n';
            return 1;
        }
        done = instance.value_bool(static_cast<std::size_t>(done_index));
        if (done)
        {
            break;
        }
        axis.cycle();
    }

    const double final_position = axis.snapshot().command_position;
    std::cout << "st motion demo: powered=" << saw_powered << ", done=" << done
              << ", busy=" << instance.value_bool(static_cast<std::size_t>(busy_index))
              << ", cycles=" << cycle << ", position=" << final_position << '\n';

    return saw_powered && done && std::fabs(final_position - 1.25) < 1e-8 ? 0 : 1;
}
