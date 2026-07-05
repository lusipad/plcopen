#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "axis/state.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace
{
std::string make_error_message(const char *operation, plcopen::core::rt::ErrorCode error)
{
    return std::string(operation) + " failed with error " + std::to_string(static_cast<int>(error));
}

void throw_on_error(const char *operation, plcopen::core::rt::ErrorCode error)
{
    if(error != plcopen::core::rt::ErrorCode::ok) {
        throw std::runtime_error(make_error_message(operation, error));
    }
}

class AxisSim
{
public:
    explicit AxisSim(std::int32_t domain_id = 0)
        : axis_(domain_id)
    {
    }

    void power_on()
    {
        throw_on_error("power_on", axis_.set_power(true));
    }

    void move_absolute(double position, double velocity, double acceleration, double deceleration,
                       double jerk = 1.0, int max_cycles = 5000)
    {
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::move_absolute;
        command.value = position;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit_and_wait(command, "move_absolute", max_cycles);
    }

    void move_relative(double distance, double velocity, double acceleration, double deceleration,
                       double jerk = 1.0, int max_cycles = 5000)
    {
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::move_relative;
        command.value = distance;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit_and_wait(command, "move_relative", max_cycles);
    }

    void move_velocity(double velocity, double acceleration = 1.0, double deceleration = 1.0,
                       double jerk = 1.0, int cycles = 1)
    {
        if(!std::isfinite(velocity) || velocity == 0.0) {
            throw std::runtime_error("move_velocity requires non-zero finite velocity");
        }
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::move_velocity;
        command.value = velocity < 0.0 ? -1.0 : 1.0;
        command.velocity = std::fabs(velocity);
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit(command, "move_velocity");
        for(int i = 0; i < cycles; ++i) {
            axis_.cycle();
        }
    }

    void halt(double deceleration = 1.0, double jerk = 1.0, int max_cycles = 5000)
    {
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::halt;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit_and_wait(command, "halt", max_cycles);
    }

    void stop(double deceleration = 1.0, double jerk = 1.0, int max_cycles = 5000)
    {
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::stop;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit_and_wait(command, "stop", max_cycles);
    }

    void home_direct(double position = 0.0)
    {
        throw_on_error("home_direct", axis_.home_direct(position));
        home_position_ = position;
    }

    double home_position() const
    {
        return home_position_;
    }

    double actual_position() const
    {
        return axis_.snapshot().actual_position;
    }

    double command_position() const
    {
        return axis_.snapshot().command_position;
    }

    double actual_velocity() const
    {
        return axis_.snapshot().actual_velocity;
    }

    double command_velocity() const
    {
        return axis_.snapshot().command_velocity;
    }

    double actual_acceleration() const
    {
        return axis_.snapshot().actual_acceleration;
    }

    double command_acceleration() const
    {
        return axis_.snapshot().command_acceleration;
    }

    plcopen::core::axis::AxisStatus status() const
    {
        return axis_.status();
    }

    // B9 stream session (KB-035): a low-rate joint target stream upsampled
    // to the cycle rate through the online OTG filter. Producers stamp
    // targets in the session cycle domain (stream_now()).
    void stream_engage(double velocity_limit,
                       double acceleration_limit,
                       double jerk_limit,
                       std::int64_t timeout_cycles,
                       std::int64_t extrapolation_cycles)
    {
        if(!axis_.powered()) {
            power_on();
        }
        plcopen::core::stream::StreamFilterConfig config{};
        config.limits = {velocity_limit, acceleration_limit, acceleration_limit, jerk_limit};
        config.timeout_cycles = timeout_cycles;
        config.extrapolation_cycles = extrapolation_cycles;
        const plcopen::core::rt::Result<std::uint32_t> session = axis_.stream_engage(config);
        if(!session) {
            throw std::runtime_error(make_error_message("stream_engage", session.error()));
        }
    }

    void stream_push(double position,
                     std::int64_t timestamp_cycles,
                     std::optional<double> velocity)
    {
        plcopen::core::stream::StreamTarget target{};
        target.position = position;
        target.timestamp_cycles = timestamp_cycles;
        if(velocity.has_value()) {
            target.velocity = *velocity;
            target.has_velocity = true;
        }
        throw_on_error("stream_push", axis_.stream_push(target));
    }

    void stream_disengage()
    {
        throw_on_error("stream_disengage", axis_.stream_disengage());
    }

    std::int64_t stream_now() const
    {
        return axis_.stream_filter().now_cycles();
    }

    std::string stream_mode() const
    {
        switch(axis_.stream_filter().mode()) {
        case plcopen::core::stream::StreamFilter1D::Mode::idle:
            return "idle";
        case plcopen::core::stream::StreamFilter1D::Mode::tracking:
            return "tracking";
        case plcopen::core::stream::StreamFilter1D::Mode::extrapolating:
            return "extrapolating";
        case plcopen::core::stream::StreamFilter1D::Mode::stopping:
            return "stopping";
        case plcopen::core::stream::StreamFilter1D::Mode::stopped:
            return "stopped";
        }
        return "unknown";
    }

    std::uint32_t stream_dropouts() const
    {
        return axis_.stream_filter().dropout_count();
    }

    void cycle(int cycles)
    {
        for(int i = 0; i < cycles; ++i) {
            axis_.cycle();
        }
    }

private:
    void submit(plcopen::core::axis::AxisCommand command, const char *operation)
    {
        if(!axis_.powered()) {
            power_on();
        }
        const plcopen::core::rt::Result<std::uint32_t> accepted = axis_.submit(command);
        if(!accepted) {
            throw std::runtime_error(make_error_message(operation, accepted.error()));
        }
    }

    void submit_and_wait(plcopen::core::axis::AxisCommand command, const char *operation, int max_cycles)
    {
        submit(command, operation);
        for(int cycle = 0; cycle < max_cycles; ++cycle) {
            axis_.cycle();
            if(axis_.status() == plcopen::core::axis::AxisStatus::standstill) {
                return;
            }
        }
        throw std::runtime_error(std::string(operation) + " timed out");
    }

    plcopen::core::axis::AxisModel axis_;
    double home_position_ = 0.0;
};
} // namespace

PYBIND11_MODULE(pyplcopen, module)
{
    module.doc() = "Minimal Python smoke facade for the plcopen rewrite core";

    py::enum_<plcopen::core::axis::AxisStatus>(module, "AxisStatus")
        .value("DISABLED", plcopen::core::axis::AxisStatus::disabled)
        .value("STANDSTILL", plcopen::core::axis::AxisStatus::standstill)
        .value("DISCRETE_MOTION", plcopen::core::axis::AxisStatus::discrete_motion)
        .value("CONTINUOUS_MOTION", plcopen::core::axis::AxisStatus::continuous_motion)
        .value("SYNCHRONIZED_MOTION", plcopen::core::axis::AxisStatus::synchronized_motion)
        .value("STOPPING", plcopen::core::axis::AxisStatus::stopping)
        .value("ERRORSTOP", plcopen::core::axis::AxisStatus::errorstop);

    py::class_<AxisSim>(module, "AxisSim")
        .def(py::init<std::int32_t>(), py::arg("domain_id") = 0)
        .def("power_on", &AxisSim::power_on)
        .def("move_absolute", &AxisSim::move_absolute, py::arg("position"), py::arg("velocity"),
             py::arg("acceleration"), py::arg("deceleration"), py::arg("jerk") = 1.0,
             py::arg("max_cycles") = 5000)
        .def("move_relative", &AxisSim::move_relative, py::arg("distance"), py::arg("velocity"),
             py::arg("acceleration"), py::arg("deceleration"), py::arg("jerk") = 1.0,
             py::arg("max_cycles") = 5000)
        .def("move_velocity", &AxisSim::move_velocity, py::arg("velocity"), py::arg("acceleration") = 1.0,
             py::arg("deceleration") = 1.0, py::arg("jerk") = 1.0, py::arg("cycles") = 1)
        .def("halt", &AxisSim::halt, py::arg("deceleration") = 1.0, py::arg("jerk") = 1.0,
             py::arg("max_cycles") = 5000)
        .def("stop", &AxisSim::stop, py::arg("deceleration") = 1.0, py::arg("jerk") = 1.0,
             py::arg("max_cycles") = 5000)
        .def("home_direct", &AxisSim::home_direct, py::arg("position") = 0.0)
        .def("home_position", &AxisSim::home_position)
        .def("actual_position", &AxisSim::actual_position)
        .def("command_position", &AxisSim::command_position)
        .def("actual_velocity", &AxisSim::actual_velocity)
        .def("command_velocity", &AxisSim::command_velocity)
        .def("actual_acceleration", &AxisSim::actual_acceleration)
        .def("command_acceleration", &AxisSim::command_acceleration)
        .def("status", &AxisSim::status)
        .def("stream_engage", &AxisSim::stream_engage, py::arg("velocity_limit"),
             py::arg("acceleration_limit"), py::arg("jerk_limit"),
             py::arg("timeout_cycles") = 50, py::arg("extrapolation_cycles") = 40)
        .def("stream_push", &AxisSim::stream_push, py::arg("position"),
             py::arg("timestamp_cycles"), py::arg("velocity") = std::nullopt)
        .def("stream_disengage", &AxisSim::stream_disengage)
        .def("stream_now", &AxisSim::stream_now)
        .def("stream_mode", &AxisSim::stream_mode)
        .def("stream_dropouts", &AxisSim::stream_dropouts)
        .def("cycle", &AxisSim::cycle, py::arg("cycles") = 1);
}
