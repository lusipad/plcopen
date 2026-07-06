#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
#include "geom/frame.h"
#include "kin/wrist6r.h"

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

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

// Pose-surface facade (KB-042/043/044): a 6R arm group driven by TCP
// poses — command, Cartesian interpolation, and readback through one
// object. Simulation-only convenience mirroring the AxisSim style.
class PoseArmSim
{
public:
    PoseArmSim(double base_height = 0.3,
               double upper_arm = 0.4,
               double forearm = 0.35,
               double tool = 0.08,
               double min_margin = 0.0,
               double max_joint_step = 3.0)
        : arm_(base_height, upper_arm, forearm, tool)
    {
        for(auto &axis : axes_) {
            throw_on_error("power_on", axis.set_power(true));
            throw_on_error("add_axis", group_.add_axis(axis));
        }
        throw_on_error("enable", group_.enable());
        throw_on_error("set_pose_kinematics",
                       group_.set_pose_kinematics(&arm_, min_margin, max_joint_step));
    }

    void set_workpiece_frame_rpy(double x, double y, double z, double roll,
                                 double pitch, double yaw)
    {
        throw_on_error("set_workpiece_frame_rpy",
                       group_.set_workpiece_frame_rpy(x, y, z, roll, pitch, yaw));
    }

    void set_tool_transform_rpy(double x, double y, double z, double roll,
                                double pitch, double yaw)
    {
        throw_on_error("set_tool_transform_rpy",
                       group_.set_tool_transform_rpy(x, y, z, roll, pitch, yaw));
    }

    void move_joints(const std::vector<double> &joints, double velocity,
                     double acceleration, double deceleration, double jerk,
                     int max_cycles = 60000)
    {
        if(joints.size() != 6) {
            throw std::runtime_error("move_joints expects 6 joint targets");
        }
        plcopen::core::axis::GroupCommand command{};
        command.target.size = 6;
        for(std::size_t i = 0; i < 6; ++i) {
            command.target.value[i] = joints[i];
        }
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        submit_and_wait(command, "move_joints", max_cycles);
    }

    void move_pose(double x, double y, double z, double roll, double pitch,
                   double yaw, double velocity, double acceleration,
                   double deceleration, double jerk, bool cartesian = false,
                   bool pcs = false, int max_cycles = 120000)
    {
        plcopen::core::axis::GroupCommand command{};
        command.target.size = 6;
        command.target.value[0] = x;
        command.target.value[1] = y;
        command.target.value[2] = z;
        command.target.value[3] = roll;
        command.target.value[4] = pitch;
        command.target.value[5] = yaw;
        command.velocity = velocity;
        command.acceleration = acceleration;
        command.deceleration = deceleration;
        command.jerk = jerk;
        command.coord_system = pcs ? plcopen::core::axis::CoordSystem::pcs
                                   : plcopen::core::axis::CoordSystem::mcs;
        if(cartesian) {
            command.interpolation_space =
                plcopen::core::axis::InterpolationSpace::cartesian;
        }
        submit_and_wait(command, "move_pose", max_cycles);
    }

    py::tuple read_pose(bool actual = false, bool pcs = false)
    {
        plcopen::core::axis::GroupPosition out{};
        bool gimbal = false;
        throw_on_error(
            "read_pose",
            group_.read_cartesian(pcs ? plcopen::core::axis::CoordSystem::pcs
                                      : plcopen::core::axis::CoordSystem::mcs,
                                  actual
                                      ? plcopen::core::axis::PositionSource::actual
                                      : plcopen::core::axis::PositionSource::command,
                                  out, &gimbal));
        std::vector<double> pose(out.value.begin(), out.value.begin() + 6);
        return py::make_tuple(pose, gimbal);
    }

    std::vector<double> joint_positions() const
    {
        std::vector<double> joints(6, 0.0);
        for(std::size_t i = 0; i < 6; ++i) {
            joints[i] = axes_[i].snapshot().command_position;
        }
        return joints;
    }

    std::string status() const
    {
        switch(group_.status()) {
        case plcopen::core::axis::GroupStatus::disabled:
            return "disabled";
        case plcopen::core::axis::GroupStatus::standby:
            return "standby";
        case plcopen::core::axis::GroupStatus::moving:
            return "moving";
        case plcopen::core::axis::GroupStatus::stopping:
            return "stopping";
        default:
            return "errorstop";
        }
    }

    void cycle(int cycles = 1)
    {
        for(int i = 0; i < cycles; ++i) {
            group_.cycle();
        }
    }

private:
    void submit_and_wait(plcopen::core::axis::GroupCommand command,
                         const char *operation, int max_cycles)
    {
        const auto result = group_.submit_linear(command);
        if(!result) {
            throw_on_error(operation, result.error());
        }
        for(int i = 0; i < max_cycles; ++i) {
            group_.cycle();
            if(group_.status() == plcopen::core::axis::GroupStatus::errorstop) {
                throw_on_error(operation, group_.last_cartesian_error());
                throw std::runtime_error(std::string(operation) + " errorstop");
            }
            if(group_.status() == plcopen::core::axis::GroupStatus::standby) {
                return;
            }
        }
        throw std::runtime_error(std::string(operation) + " did not settle");
    }

    plcopen::core::kin::SphericalWrist6R arm_;
    plcopen::core::axis::AxisModel axes_[6];
    plcopen::core::axis::AxisGroup group_;
};

std::vector<std::pair<double, double>> generate_cam_law_py(const std::string &law,
                                                           double master_span,
                                                           double rise,
                                                           std::size_t points)
{
    plcopen::core::exec::CamLaw kind = plcopen::core::exec::CamLaw::cycloidal;
    if(law == "cycloidal") {
        kind = plcopen::core::exec::CamLaw::cycloidal;
    } else if(law == "modified_sine") {
        kind = plcopen::core::exec::CamLaw::modified_sine;
    } else if(law == "poly345") {
        kind = plcopen::core::exec::CamLaw::poly345;
    } else {
        throw std::runtime_error("unknown cam law: " + law);
    }
    plcopen::core::exec::CamPoint table[64];
    throw_on_error("generate_cam_law",
                   plcopen::core::exec::generate_cam_law(kind, master_span, rise,
                                                         table, points));
    std::vector<std::pair<double, double>> result;
    result.reserve(points);
    for(std::size_t i = 0; i < points; ++i) {
        result.emplace_back(table[i].master, table[i].slave);
    }
    return result;
}

// KB-046 CSV convention: two columns master,slave; # comments; strictly
// increasing master (validated here — the core never does file IO).
std::vector<std::pair<double, double>> load_cam_table_csv(const std::string &path)
{
    std::ifstream file(path);
    if(!file.is_open()) {
        throw std::runtime_error("cannot open cam table: " + path);
    }
    std::vector<std::pair<double, double>> table;
    std::string line;
    while(std::getline(file, line)) {
        if(line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream row(line);
        std::string master_text;
        std::string slave_text;
        if(!std::getline(row, master_text, ',') || !std::getline(row, slave_text)) {
            throw std::runtime_error("malformed cam table row: " + line);
        }
        const double master = std::stod(master_text);
        const double slave = std::stod(slave_text);
        if(!table.empty() && master <= table.back().first) {
            throw std::runtime_error("cam table master must strictly increase");
        }
        table.emplace_back(master, slave);
    }
    if(table.size() < 2) {
        throw std::runtime_error("cam table needs at least two rows");
    }
    return table;
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

    py::class_<PoseArmSim>(module, "PoseArmSim")
        .def(py::init<double, double, double, double, double, double>(),
             py::arg("base_height") = 0.3, py::arg("upper_arm") = 0.4,
             py::arg("forearm") = 0.35, py::arg("tool") = 0.08,
             py::arg("min_margin") = 0.0, py::arg("max_joint_step") = 3.0)
        .def("set_workpiece_frame_rpy", &PoseArmSim::set_workpiece_frame_rpy)
        .def("set_tool_transform_rpy", &PoseArmSim::set_tool_transform_rpy)
        .def("move_joints", &PoseArmSim::move_joints, py::arg("joints"),
             py::arg("velocity"), py::arg("acceleration"), py::arg("deceleration"),
             py::arg("jerk"), py::arg("max_cycles") = 60000)
        .def("move_pose", &PoseArmSim::move_pose, py::arg("x"), py::arg("y"),
             py::arg("z"), py::arg("roll"), py::arg("pitch"), py::arg("yaw"),
             py::arg("velocity"), py::arg("acceleration"), py::arg("deceleration"),
             py::arg("jerk"), py::arg("cartesian") = false, py::arg("pcs") = false,
             py::arg("max_cycles") = 120000)
        .def("read_pose", &PoseArmSim::read_pose, py::arg("actual") = false,
             py::arg("pcs") = false)
        .def("joint_positions", &PoseArmSim::joint_positions)
        .def("status", &PoseArmSim::status)
        .def("cycle", &PoseArmSim::cycle, py::arg("cycles") = 1);

    module.def("generate_cam_law", &generate_cam_law_py, py::arg("law"),
               py::arg("master_span"), py::arg("rise"), py::arg("points"));
    module.def("load_cam_table_csv", &load_cam_table_csv, py::arg("path"));
}
