#ifndef PLCOPEN_GROUPLINEARPLANNER_HPP_
#define PLCOPEN_GROUPLINEARPLANNER_HPP_

#include "PLCTypes.h"
#include "ProfilePlanner.h"

#include <cstddef>
#include <cstdint>

namespace plcopen
{

/** Maps one scalar ProfilePlanner sample onto a fixed-capacity linear group path. */
class GroupLinearPlanner
{
public:
    bool setFrequency(uint32_t frequency);
    bool plan(
        const MC_POS_REF &start,
        const MC_POS_REF &target,
        double velocity,
        double acceleration,
        double deceleration,
        double jerk);
    bool plan(
        const MC_POS_REF &start,
        const MC_POS_REF &target,
        double startVelocity,
        double velocity,
        double acceleration,
        double deceleration,
        double jerk);
    bool execute(void);

    std::size_t count(void) const;
    double position(std::size_t index);
    double velocity(std::size_t index);
    double acceleration(std::size_t index);

private:
    ProfilePlanner mPathPlanner;
    std::size_t mCount = 0;
    double mStart[PLCOPEN_AXESGROUP_IDENT_NUM] = {0};
    double mDirection[PLCOPEN_AXESGROUP_IDENT_NUM] = {0};
    bool mComplete = true;
};

} // namespace plcopen

#endif /** PLCOPEN_GROUPLINEARPLANNER_HPP_ **/
