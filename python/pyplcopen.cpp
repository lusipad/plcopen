#include <pybind11/pybind11.h>

#include "axis/state.h"

#include <cmath>
#include <cstdint>
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

    void halt()
    {
        plcopen::core::axis::AxisCommand command{};
        command.kind = plcopen::core::axis::CommandKind::halt;
        submit(command, "halt");
        axis_.cycle();
    }

    void home_direct(double position = 0.0)
    {
        throw_on_error("home_direct", axis_.set_position(position));
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

    plcopen::core::axis::AxisStatus status() const
    {
        return axis_.status();
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
        .def("halt", &AxisSim::halt)
        .def("home_direct", &AxisSim::home_direct, py::arg("position") = 0.0)
        .def("actual_position", &AxisSim::actual_position)
        .def("command_position", &AxisSim::command_position)
        .def("actual_velocity", &AxisSim::actual_velocity)
        .def("command_velocity", &AxisSim::command_velocity)
        .def("status", &AxisSim::status);
}
