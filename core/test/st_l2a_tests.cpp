#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>

#include "axis/state.h"
#include "rt/spsc_queue.h"
#include "st/st.h"

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

bool has_code(const st::CompileResult &result, st::DiagCode code)
{
    for(const st::Diagnostic &diagnostic : result.diagnostics) {
        if(diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

std::string program(const char *vars, const char *body)
{
    std::string source = "PROGRAM p\nVAR\n";
    source += vars;
    source += "\nEND_VAR\n";
    source += body;
    source += "\nEND_PROGRAM\n";
    return source;
}

void axis_ref_contract()
{
    const st::CompileResult declared =
        st::compile(program("AxisX : AXIS_REF;", "").c_str());
    check(declared.ok, "AXIS_REF declaration compiles");

    const st::CompileResult assigned = st::compile(
        program("AxisX : AXIS_REF; AxisY : AXIS_REF;",
                "AxisX := AxisY;")
            .c_str());
    check(!assigned.ok, "AXIS_REF assignment rejected");
    check(has_code(assigned, st::DiagCode::sema_operand_type_invalid),
          "AXIS_REF assignment diagnostic");
}

void host_binding_contract()
{
    const st::CompileResult compiled =
        st::compile(program("AxisX : AXIS_REF;", "").c_str());
    check(compiled.ok, "binding program compiles");
    if(!compiled.ok) {
        return;
    }

    alignas(8) unsigned char storage[256]{};
    st::Instance instance;
    check(instance.load(compiled.program, storage, sizeof(storage), 1000000) ==
              rt::ErrorCode::ok,
          "binding program loads");

    axis::AxisModel first;
    axis::AxisModel second;
    check(instance.bind_axis("axisx", &first) == rt::ErrorCode::ok,
          "case-insensitive initial bind");
    check(instance.bind_axis("AXISX", &second) == rt::ErrorCode::ok,
          "pre-scan rebind");
    check(instance.bind_axis("missing", &first) ==
              rt::ErrorCode::invalid_argument,
          "unknown AXIS_REF rejected");
    check(instance.scan(8) == st::ScanError::ok, "first scan");
    check(instance.bind_axis("AxisX", &first) ==
              rt::ErrorCode::precondition_failed,
          "running rebind rejected");
}

void mc_power_contract()
{
    const char *source =
        "PROGRAM p\n"
        "VAR\n"
        "  AxisX : AXIS_REF;\n"
        "  Power : MC_Power;\n"
        "  Status : BOOL;\n"
        "  Valid : BOOL;\n"
        "  Error : BOOL;\n"
        "  ErrorID : DINT;\n"
        "END_VAR\n"
        "Power(Axis := AxisX, Enable := TRUE);\n"
        "Status := Power.Status;\n"
        "Valid := Power.Valid;\n"
        "Error := Power.Error;\n"
        "ErrorID := Power.ErrorID;\n"
        "END_PROGRAM\n";
    const st::CompileResult compiled = st::compile(source);
    check(compiled.ok, "MC_Power program compiles");
    if(!compiled.ok) {
        return;
    }

    alignas(8) unsigned char unbound_storage[512]{};
    st::Instance unbound;
    check(unbound.load(compiled.program, unbound_storage,
                       sizeof(unbound_storage), 1000000) == rt::ErrorCode::ok,
          "unbound MC_Power loads");
    check(unbound.scan(64) == st::ScanError::ok,
          "unbound MC_Power does not fault scan");
    const int unbound_error = unbound.find("Error");
    const int unbound_error_id = unbound.find("ErrorID");
    check(unbound_error >= 0 &&
              unbound.value_i64(static_cast<std::size_t>(unbound_error)) == 1,
          "unbound MC_Power reports Error");
    check(unbound_error_id >= 0 &&
              unbound.value_i64(static_cast<std::size_t>(unbound_error_id)) ==
                  static_cast<std::int64_t>(rt::ErrorCode::invalid_argument),
          "unbound MC_Power reports invalid_argument");

    alignas(8) unsigned char bound_storage[512]{};
    st::Instance bound;
    axis::AxisModel axis;
    check(bound.load(compiled.program, bound_storage, sizeof(bound_storage),
                     1000000) == rt::ErrorCode::ok,
          "bound MC_Power loads");
    check(bound.bind_axis("AxisX", &axis) == rt::ErrorCode::ok,
          "MC_Power axis binds");
    check(bound.scan(64) == st::ScanError::ok, "bound MC_Power scans");
    check(axis.powered(), "MC_Power enables AxisModel");
    const int status = bound.find("Status");
    const int valid = bound.find("Valid");
    const int error = bound.find("Error");
    check(status >= 0 &&
              bound.value_i64(static_cast<std::size_t>(status)) == 1,
          "MC_Power Status output");
    check(valid >= 0 && bound.value_i64(static_cast<std::size_t>(valid)) == 1,
          "MC_Power Valid output");
    check(error >= 0 && bound.value_i64(static_cast<std::size_t>(error)) == 0,
          "MC_Power Error output clears");
}

void mc_power_rejection_contract()
{
    const st::CompileResult wrong_axis = st::compile(
        program("Power : MC_Power;", "Power(Axis := 1, Enable := TRUE);")
            .c_str());
    check(!wrong_axis.ok, "MC_Power numeric Axis rejected");
    check(has_code(wrong_axis, st::DiagCode::sema_type_mismatch),
          "MC_Power numeric Axis diagnostic");

    const st::CompileResult input_read = st::compile(
        program("Power : MC_Power; Enabled : BOOL;",
                "Enabled := Power.Enable;")
            .c_str());
    check(!input_read.ok, "MC_Power input pin read rejected");
    check(has_code(input_read, st::DiagCode::sema_pin_not_output),
          "MC_Power input read diagnostic");

    const st::CompileResult output_write = st::compile(
        program("Power : MC_Power;", "Power(Status := TRUE);").c_str());
    check(!output_write.ok, "MC_Power output pin write rejected");
    check(has_code(output_write, st::DiagCode::sema_pin_not_input),
          "MC_Power output write diagnostic");
}

void mc_execute_blocks_contract()
{
    const char *home_source =
        "PROGRAM p\nVAR\n"
        "AxisX : AXIS_REF; Home : MC_Home; Busy : BOOL; Active : BOOL; "
        "Error : BOOL;\nEND_VAR\n"
        "Home(Axis := AxisX, Execute := TRUE, Position := 2.0, "
        "BufferMode := 0);\n"
        "Busy := Home.Busy; Active := Home.Active; Error := Home.Error;\n"
        "END_PROGRAM\n";
    const st::CompileResult home_program = st::compile(home_source);
    check(home_program.ok, "MC_Home program compiles");
    if(home_program.ok) {
        alignas(8) unsigned char storage[1024]{};
        st::Instance instance;
        axis::AxisModel axis;
        axis.set_power(true);
        check(instance.load(home_program.program, storage, sizeof(storage),
                            1000000) == rt::ErrorCode::ok,
              "MC_Home loads");
        check(instance.bind_axis("AxisX", &axis) == rt::ErrorCode::ok,
              "MC_Home binds");
        check(instance.scan(128) == st::ScanError::ok, "MC_Home scans");
        check(axis.snapshot().active_command_id != 0,
              "MC_Home submits command");
        const int busy = instance.find("Busy");
        const int active = instance.find("Active");
        const int error = instance.find("Error");
        check(busy >= 0 && instance.value_i64(busy) == 1, "MC_Home Busy");
        check(active >= 0 && instance.value_i64(active) == 1,
              "MC_Home Active");
        check(error >= 0 && instance.value_i64(error) == 0,
              "MC_Home no Error");
    }

    const char *stop_halt_source =
        "PROGRAM p\nVAR\n"
        "AxisX : AXIS_REF; Stop : MC_Stop; Halt : MC_Halt; "
        "StopError : BOOL; HaltError : BOOL;\nEND_VAR\n"
        "Stop(Axis := AxisX, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 2.0);\n"
        "Halt(Axis := AxisX, Execute := TRUE, Deceleration := 1.0, "
        "Jerk := 2.0, BufferMode := 0);\n"
        "StopError := Stop.Error; HaltError := Halt.Error;\n"
        "END_PROGRAM\n";
    const st::CompileResult stop_halt = st::compile(stop_halt_source);
    check(stop_halt.ok, "MC_Stop and MC_Halt compile");
    if(stop_halt.ok) {
        alignas(8) unsigned char storage[2048]{};
        st::Instance instance;
        check(instance.load(stop_halt.program, storage, sizeof(storage),
                            1000000) == rt::ErrorCode::ok,
              "MC_Stop and MC_Halt load");
        check(instance.scan(128) == st::ScanError::ok,
              "unbound Stop and Halt scan");
        const int stop_error = instance.find("StopError");
        const int halt_error = instance.find("HaltError");
        check(stop_error >= 0 && instance.value_i64(stop_error) == 1,
              "unbound MC_Stop Error");
        check(halt_error >= 0 && instance.value_i64(halt_error) == 1,
              "unbound MC_Halt Error");
    }

    const st::CompileResult unsupported_mode = st::compile(
        "PROGRAM p\nVAR AxisX : AXIS_REF; Home : MC_Home; Error : BOOL; "
        "ErrorID : DINT; END_VAR\n"
        "Home(Axis := AxisX, Execute := TRUE, Position := 0.0, "
        "BufferMode := 6); Error := Home.Error; ErrorID := Home.ErrorID;\n"
        "END_PROGRAM\n");
    check(unsupported_mode.ok, "unsupported BufferMode program compiles");
    if(unsupported_mode.ok) {
        alignas(8) unsigned char storage[1024]{};
        st::Instance instance;
        axis::AxisModel axis;
        axis.set_power(true);
        check(instance.load(unsupported_mode.program, storage,
                            sizeof(storage), 1000000) == rt::ErrorCode::ok,
              "unsupported BufferMode loads");
        check(instance.bind_axis("AxisX", &axis) == rt::ErrorCode::ok,
              "unsupported BufferMode binds");
        check(instance.scan(128) == st::ScanError::ok,
              "unsupported BufferMode scans");
        const int error = instance.find("Error");
        const int error_id = instance.find("ErrorID");
        check(error >= 0 && instance.value_i64(error) == 1,
              "unsupported BufferMode Error");
        check(error_id >= 0 && instance.value_i64(error_id) ==
                                   static_cast<std::int64_t>(
                                       rt::ErrorCode::invalid_argument),
              "unsupported BufferMode invalid_argument");
    }
}

void mc_move_absolute_contract()
{
    const char *source =
        "PROGRAM p\nVAR\n"
        "AxisX : AXIS_REF; Move : MC_MoveAbsolute; Done : BOOL; Busy : BOOL; "
        "Aborted : BOOL; Error : BOOL;\nEND_VAR\n"
        "Move(Axis := AxisX, Execute := TRUE, ContinuousUpdate := FALSE, "
        "Position := 0.25, Velocity := 1.0, Acceleration := 2.0, "
        "Deceleration := 2.0, Jerk := 10.0, Direction := 0, "
        "BufferMode := 0);\n"
        "Done := Move.Done; Busy := Move.Busy; "
        "Aborted := Move.CommandAborted; Error := Move.Error;\n"
        "END_PROGRAM\n";
    const st::CompileResult compiled = st::compile(source);
    check(compiled.ok, "MC_MoveAbsolute program compiles");
    if(!compiled.ok) return;

    alignas(8) unsigned char storage[2048]{};
    st::Instance instance;
    axis::AxisModel st_axis;
    axis::AxisModel cpp_axis;
    st_axis.set_power(true);
    cpp_axis.set_power(true);
    check(instance.load(compiled.program, storage, sizeof(storage), 1000000) ==
              rt::ErrorCode::ok,
          "MC_MoveAbsolute loads");
    check(instance.bind_axis("AxisX", &st_axis) == rt::ErrorCode::ok,
          "MC_MoveAbsolute binds");

    fb::FbMoveAbsolute direct;
    direct.axis_ref = &cpp_axis;
    direct.execute = true;
    direct.position = 0.25;
    direct.velocity = 1.0;
    direct.acceleration = 2.0;
    direct.deceleration = 2.0;
    direct.jerk = 10.0;
    direct.call();
    check(instance.scan(256) == st::ScanError::ok,
          "MC_MoveAbsolute first scan");
    check(st_axis.snapshot().active_command_id != 0,
          "MC_MoveAbsolute submits command");

    bool reached = false;
    for(int cycle = 0; cycle < 4000; ++cycle) {
        st_axis.cycle();
        cpp_axis.cycle();
        check(st_axis.snapshot().command_position ==
                  cpp_axis.snapshot().command_position,
              "ST and C++ MoveAbsolute setpoint equivalence");
        direct.call();
        check(instance.scan(256) == st::ScanError::ok,
              "MC_MoveAbsolute lifecycle scan");
        const int done = instance.find("Done");
        if(done >= 0 && instance.value_i64(done) == 1) {
            reached = true;
            break;
        }
    }
    check(reached, "MC_MoveAbsolute reaches Done");
    check(st_axis.snapshot().command_position == 0.25,
          "MC_MoveAbsolute exact endpoint");
    const int busy = instance.find("Busy");
    const int aborted = instance.find("Aborted");
    const int error = instance.find("Error");
    check(busy >= 0 && instance.value_i64(busy) == 0,
          "MC_MoveAbsolute Busy clears");
    check(aborted >= 0 && instance.value_i64(aborted) == 0,
          "MC_MoveAbsolute not aborted");
    check(error >= 0 && instance.value_i64(error) == 0,
          "MC_MoveAbsolute no Error");
}

void remaining_blocks_compile_contract()
{
    const char *source =
        "PROGRAM p\nVAR\n"
        "AxisX : AXIS_REF; Rel : MC_MoveRelative; Add : MC_MoveAdditive; "
        "Vel : MC_MoveVelocity; Override : MC_SetOverride; Reset : MC_Reset;\n"
        "Flag : BOOL; Code : DINT;\nEND_VAR\n"
        "Rel(Axis := AxisX, Execute := FALSE, ContinuousUpdate := FALSE, "
        "Distance := 1.0, Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0, BufferMode := 1);\n"
        "Add(Axis := AxisX, Execute := FALSE, ContinuousUpdate := FALSE, "
        "Distance := 1.0, Velocity := 1.0, Acceleration := 1.0, "
        "Deceleration := 1.0, Jerk := 1.0, BufferMode := 0);\n"
        "Vel(Axis := AxisX, Execute := FALSE, ContinuousUpdate := FALSE, "
        "Velocity := 1.0, Acceleration := 1.0, Deceleration := 1.0, "
        "Jerk := 1.0, Direction := 1, BufferMode := 0);\n"
        "Override(Axis := AxisX, Enable := FALSE, VelFactor := 1.0, "
        "AccFactor := 1.0, JerkFactor := 1.0);\n"
        "Reset(Axis := AxisX, Execute := FALSE);\n"
        "Flag := Rel.Done OR Add.Done OR Vel.InVelocity OR Override.Enabled "
        "OR Reset.Done; Code := Reset.ErrorID;\n"
        "END_PROGRAM\n";
    const st::CompileResult compiled = st::compile(source);
    check(compiled.ok, "remaining first-ten MC blocks compile with all pins");
}

void executor_domain_smoke()
{
    const st::CompileResult compiled = st::compile(
        "PROGRAM p\nVAR AxisX : AXIS_REF; Move : MC_MoveAbsolute; "
        "END_VAR\nMove(Axis := AxisX, Execute := TRUE, "
        "ContinuousUpdate := FALSE, Position := 0.1, Velocity := 1.0, "
        "Acceleration := 2.0, Deceleration := 2.0, Jerk := 10.0, "
        "Direction := 0, BufferMode := 0);\nEND_PROGRAM\n");
    check(compiled.ok, "executor ST program compiles");
    if(!compiled.ok) return;

    struct Frame
    {
        double position = 0.0;
        bool done = false;
    };
    static_assert(std::is_trivially_copyable<Frame>::value,
                  "committed ST frame must be queue-safe");
    rt::SpscQueue<Frame, 64> committed;
    alignas(8) unsigned char storage[2048]{};
    st::Instance instance;
    axis::AxisModel axis;
    axis.set_power(true);
    check(instance.load(compiled.program, storage, sizeof(storage), 1000000) ==
              rt::ErrorCode::ok,
          "executor ST program loads");
    check(instance.bind_axis("AxisX", &axis) == rt::ErrorCode::ok,
          "executor ST axis binds");

    std::thread planner([&]() {
        for(int cycle = 0; cycle < 2000; ++cycle) {
            if(instance.scan(256) != st::ScanError::ok) return;
            axis.cycle();
            Frame frame{};
            frame.position = axis.snapshot().command_position;
            frame.done = axis.snapshot().last_completed_command_id != 0;
            while(!committed.push(frame)) std::this_thread::yield();
            if(frame.done) return;
        }
    });

    Frame received{};
    bool done = false;
    for(int spin = 0; spin < 200000 && !done; ++spin) {
        if(committed.pop(received)) done = received.done;
        else std::this_thread::yield();
    }
    planner.join();
    while(committed.pop(received)) done = done || received.done;
    check(done, "executor ST committed trajectory completes");
    check(received.position == 0.1, "executor ST RT frame endpoint");
}

} // namespace

int main()
{
    axis_ref_contract();
    host_binding_contract();
    mc_power_contract();
    mc_power_rejection_contract();
    mc_execute_blocks_contract();
    mc_move_absolute_contract();
    remaining_blocks_compile_contract();
    executor_domain_smoke();
    if(failures == 0) {
        std::printf("PASS st_l2a_tests\n");
    }
    return failures == 0 ? 0 : 1;
}
