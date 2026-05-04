#ifndef _URANUS_AXISSYNC_HPP_
#define _URANUS_AXISSYNC_HPP_

#include "AxisMotionBase.h"
#include "PLCTypes.h"

namespace plcopen
{

    class Axis;

    class AxisSync : virtual public AxisMotionBase
    {
    public:
        AxisSync();
        virtual ~AxisSync();

        MC_ErrorCode addGearIn(
            FunctionBlock *fb,
            Axis *master,
            double ratioNumerator,
            double ratioDenominator,
            MC_Source masterValueSource = MC_Source::SETVALUE,
            MC_BufferMode bufferMode = MC_BufferMode::ABORTING,
            int32_t customId = 0);

        MC_ErrorCode addGearInPos(
            FunctionBlock *fb,
            Axis *master,
            double ratioNumerator,
            double ratioDenominator,
            double masterSyncPosition,
            double slaveSyncPosition,
            double masterStartDistance = 0.0,
            double velocity = 0.0,
            double acceleration = 0.0,
            double deceleration = 0.0,
            double jerk = 0.0,
            MC_Source masterValueSource = MC_Source::SETVALUE,
            MC_BufferMode bufferMode = MC_BufferMode::ABORTING,
            int32_t customId = 0);

        MC_ErrorCode addGearOut(
            FunctionBlock *fb,
            int32_t customId = 0);

        MC_ErrorCode setGearPhaseOffset(double phaseOffset);
        MC_ErrorCode addGearPhaseOffset(double phaseShift);
        MC_ErrorCode moveGearPhaseOffset(
            double phaseOffset,
            double velocity,
            double acceleration,
            double deceleration,
            double jerk,
            bool &isDone);
        double gearPhaseOffset(void) const;

        MC_ErrorCode addCamIn(
            FunctionBlock *fb,
            Axis *master,
            MC_CAM_REF camTable,
            double masterSyncPosition = 0.0,
            double masterStartDistance = 0.0,
            double masterOffset = 0.0,
            double slaveOffset = 0.0,
            double masterScaling = 1.0,
            double slaveScaling = 1.0,
            MC_Source masterValueSource = MC_Source::SETVALUE,
            MC_BufferMode bufferMode = MC_BufferMode::ABORTING,
            int32_t customId = 0);

        MC_ErrorCode addCamOut(
            FunctionBlock *fb,
            int32_t customId = 0);

        MC_ErrorCode addCombineAxes(
            FunctionBlock *fb,
            Axis *master1,
            Axis *master2,
            double gearRatioNumeratorM1,
            double gearRatioDenominatorM1,
            double gearRatioNumeratorM2,
            double gearRatioDenominatorM2,
            MC_CombineMode combineMode,
            MC_Source masterValueSourceM1 = MC_Source::SETVALUE,
            MC_Source masterValueSourceM2 = MC_Source::SETVALUE,
            MC_BufferMode bufferMode = MC_BufferMode::ABORTING,
            int32_t customId = 0);

    private:
        class AxisSyncImpl;
        AxisSyncImpl *mImpl_;
    };

} // namespace plcopen

#endif /** _URANUS_AXISSYNC_HPP_ **/
