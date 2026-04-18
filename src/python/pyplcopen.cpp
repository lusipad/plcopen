#include <pybind11/pybind11.h>

#include "Axis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace py = pybind11;
using namespace plcopen;

namespace
{
std::string makeErrorMessage(const char *operation, MC_ErrorCode errorCode)
{
    return std::string(operation) + " failed with error " + std::to_string(static_cast<int>(errorCode));
}

class AxisSim
{
  public:
    explicit AxisSim(double frequency = 100.0, int32_t axisId = 1)
    {
        const MC_ErrorCode frequencyError = mScheduler.setFrequency(frequency);
        if (frequencyError != MC_ErrorCode::GOOD)
            throw std::runtime_error(makeErrorMessage("set_frequency", frequencyError));

        mAxis = mScheduler.newAxis(axisId, new Servo());
        if (!mAxis)
            throw std::runtime_error("failed to create axis");

        mPower.mAxis = mAxis;
        mPower.mEnablePositive = true;
        mPower.mEnableNegative = true;
    }

    ~AxisSim()
    {
        mScheduler.release();
    }

    void power_on(int maxCycles = 20)
    {
        mPower.mEnable = true;

        for (int cycle = 0; cycle < maxCycles; ++cycle)
        {
            tick();
            if (mPower.mStatus && mPower.mValid)
                return;
            ensurePowerHealthy("power_on");
        }

        throw std::runtime_error("power_on timed out");
    }

    void move_absolute(double position, double velocity, double acceleration, double deceleration, double jerk = 0.0,
                       int maxCycles = 5000)
    {
        ensurePowered();

        FbMoveAbsolute move;
        move.mAxis = mAxis;
        move.mPosition = position;
        move.mVelocity = velocity;
        move.mAcceleration = acceleration;
        move.mDeceleration = deceleration;
        move.mJerk = jerk;
        move.mExecute = true;

        runUntilDone(move, maxCycles, "move_absolute");
        mActiveVelocityMove.reset();
    }

    void move_relative(double distance, double velocity, double acceleration, double deceleration, double jerk = 0.0,
                       int maxCycles = 5000)
    {
        ensurePowered();

        FbMoveRelative move;
        move.mAxis = mAxis;
        move.mDistance = distance;
        move.mVelocity = velocity;
        move.mAcceleration = acceleration;
        move.mDeceleration = deceleration;
        move.mJerk = jerk;
        move.mExecute = true;

        runUntilDone(move, maxCycles, "move_relative");
        mActiveVelocityMove.reset();
    }

    void move_velocity(double velocity, double acceleration, double deceleration, double jerk = 0.0, int maxCycles = 5000)
    {
        ensurePowered();

        if (mActiveVelocityMove)
            throw std::runtime_error("move_velocity requires halt() or stop() before starting another velocity move");

        mActiveVelocityMove = std::make_unique<FbMoveVelocity>();
        mActiveVelocityMove->mAxis = mAxis;
        mActiveVelocityMove->mVelocity = velocity;
        mActiveVelocityMove->mAcceleration = acceleration;
        mActiveVelocityMove->mDeceleration = deceleration;
        mActiveVelocityMove->mJerk = jerk;
        mActiveVelocityMove->mExecute = true;

        runUntil(*mActiveVelocityMove, maxCycles, "move_velocity", [](const FbMoveVelocity &block) { return block.mInVelocity; }, false);
    }

    void halt(double deceleration, double jerk = 0.0, int maxCycles = 5000)
    {
        ensurePowered();

        FbHalt halt;
        halt.mAxis = mAxis;
        halt.mDeceleration = deceleration;
        halt.mJerk = jerk;
        halt.mExecute = true;

        runUntilDone(halt, maxCycles, "halt");
        mActiveVelocityMove.reset();
    }

    void stop(double deceleration, double jerk = 0.0, int maxCycles = 5000)
    {
        ensurePowered();

        FbStop stop;
        stop.mAxis = mAxis;
        stop.mDeceleration = deceleration;
        stop.mJerk = jerk;
        stop.mExecute = true;

        runUntilDone(stop, maxCycles, "stop");
        mActiveVelocityMove.reset();
    }

    void home_direct(double position = 0.0, int maxCycles = 500)
    {
        ensurePowered();

        AxisConfig config;
        config.mHomingInfo.mHomingMode = MC_HomingMode::DIRECT;

        const MC_ErrorCode configError = mScheduler.setAxisConfig(mAxis, config);
        if (configError != MC_ErrorCode::GOOD)
            throw std::runtime_error(makeErrorMessage("set_axis_config", configError));

        FbHome home;
        home.mAxis = mAxis;
        home.mPosition = position;
        home.mExecute = true;

        runUntilDone(home, maxCycles, "home_direct");
        mActiveVelocityMove.reset();
    }

    double actual_position() const
    {
        return mAxis->actPosition();
    }

    double command_position() const
    {
        return mAxis->cmdPosition();
    }

    double actual_velocity() const
    {
        return mAxis->actVelocity();
    }

    double actual_acceleration() const
    {
        return mAxis->actAcceleration();
    }

    double command_velocity() const
    {
        return mAxis->cmdVelocity();
    }

    double command_acceleration() const
    {
        return mAxis->cmdAcceleration();
    }

    double home_position() const
    {
        return mAxis->homePosition();
    }

    MC_AxisStatus status() const
    {
        return mAxis->status();
    }

  private:
    void tick()
    {
        mScheduler.runCycle();
        mPower.call();
        if (mActiveVelocityMove)
            mActiveVelocityMove->call();
    }

    template <typename Block>
    void tick(Block &block)
    {
        mScheduler.runCycle();
        mPower.call();
        if (mActiveVelocityMove && static_cast<const void *>(mActiveVelocityMove.get()) != static_cast<const void *>(&block))
            mActiveVelocityMove->call();
        block.call();
    }

    template <typename Block>
    void runUntilDone(Block &block, int maxCycles, const char *operation)
    {
        runUntil(block, maxCycles, operation, [](const Block &value) { return value.mDone; }, true);
    }

    template <typename Block, typename Predicate>
    void runUntil(Block &block, int maxCycles, const char *operation, Predicate predicate, bool clearExecuteOnSuccess)
    {
        for (int cycle = 0; cycle < maxCycles; ++cycle)
        {
            tick(block);
            ensurePowerHealthy(operation);
            ensureActiveVelocityMoveHealthy(operation, &block);

            if (block.mError)
                throw std::runtime_error(makeErrorMessage(operation, block.mErrorID));

            if (predicate(block))
            {
                if (clearExecuteOnSuccess)
                {
                    block.mExecute = false;
                    tick(block);
                    ensurePowerHealthy(operation);
                    ensureActiveVelocityMoveHealthy(operation, &block);
                    if (block.mError)
                        throw std::runtime_error(makeErrorMessage(operation, block.mErrorID));
                }
                return;
            }
        }

        throw std::runtime_error(std::string(operation) + " timed out");
    }

    void ensurePowerHealthy(const char *operation)
    {
        if (mPower.mError)
            throw std::runtime_error(makeErrorMessage(operation, mPower.mErrorID));
    }

    template <typename Block>
    void ensureActiveVelocityMoveHealthy(const char *operation, const Block *currentBlock)
    {
        if (!mActiveVelocityMove || static_cast<const void *>(mActiveVelocityMove.get()) == static_cast<const void *>(currentBlock))
            return;

        if (mActiveVelocityMove->mError)
            throw std::runtime_error(makeErrorMessage(operation, mActiveVelocityMove->mErrorID));
    }

    void ensurePowered()
    {
        if (!(mPower.mStatus && mPower.mValid))
            power_on();
    }

    Scheduler mScheduler;
    Axis *mAxis = nullptr;
    FbPower mPower;
    std::unique_ptr<FbMoveVelocity> mActiveVelocityMove;
};
} // namespace

PYBIND11_MODULE(pyplcopen, module)
{
    module.doc() = "Minimal Python bindings for plcopen single-axis simulation";

    py::enum_<MC_AxisStatus>(module, "AxisStatus")
        .value("DISABLED", MC_AxisStatus::DISABLED)
        .value("STANDSTILL", MC_AxisStatus::STANDSTILL)
        .value("HOMING", MC_AxisStatus::HOMING)
        .value("DISCRETE_MOTION", MC_AxisStatus::DISCRETE_MOTION)
        .value("CONTINUOUS_MOTION", MC_AxisStatus::CONTINUOUS_MOTION)
        .value("SYNCHRONIZED_MOTION", MC_AxisStatus::SYNCHRONIZED_MOTION)
        .value("STOPPING", MC_AxisStatus::STOPPING)
        .value("ERRORSTOP", MC_AxisStatus::ERRORSTOP);

    py::class_<AxisSim>(module, "AxisSim")
        .def(py::init<double, int32_t>(), py::arg("frequency") = 100.0, py::arg("axis_id") = 1)
        .def("power_on", &AxisSim::power_on, py::arg("max_cycles") = 20)
        .def("move_absolute", &AxisSim::move_absolute, py::arg("position"), py::arg("velocity"), py::arg("acceleration"),
             py::arg("deceleration"), py::arg("jerk") = 0.0, py::arg("max_cycles") = 5000)
        .def("move_relative", &AxisSim::move_relative, py::arg("distance"), py::arg("velocity"),
             py::arg("acceleration"), py::arg("deceleration"), py::arg("jerk") = 0.0, py::arg("max_cycles") = 5000)
        .def("move_velocity", &AxisSim::move_velocity, py::arg("velocity"), py::arg("acceleration"),
             py::arg("deceleration"), py::arg("jerk") = 0.0, py::arg("max_cycles") = 5000)
        .def("halt", &AxisSim::halt, py::arg("deceleration"), py::arg("jerk") = 0.0, py::arg("max_cycles") = 5000)
        .def("stop", &AxisSim::stop, py::arg("deceleration"), py::arg("jerk") = 0.0, py::arg("max_cycles") = 5000)
        .def("home_direct", &AxisSim::home_direct, py::arg("position") = 0.0, py::arg("max_cycles") = 500)
        .def("actual_position", &AxisSim::actual_position)
        .def("command_position", &AxisSim::command_position)
        .def("actual_velocity", &AxisSim::actual_velocity)
        .def("actual_acceleration", &AxisSim::actual_acceleration)
        .def("command_velocity", &AxisSim::command_velocity)
        .def("command_acceleration", &AxisSim::command_acceleration)
        .def("home_position", &AxisSim::home_position)
        .def("status", &AxisSim::status);
}
