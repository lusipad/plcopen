#ifndef _URANUS_AXESGROUP_HPP_
#define _URANUS_AXESGROUP_HPP_

#include "Global.h"

#include <cstddef>

namespace plcopen
{

    class Axis;

    class AxesGroup
    {
    public:
        AxesGroup();
        ~AxesGroup();

        MC_GroupStatus status(void) const;
        std::size_t memberCount(void) const;
        Axis *member(std::size_t index) const;
        bool containsAxis(Axis *axis) const;

        MC_ErrorCode canAddAxis(Axis *axis) const;
        MC_ErrorCode addAxis(Axis *axis);
        MC_ErrorCode removeAxis(Axis *axis);
        MC_ErrorCode enable(void);
        MC_ErrorCode disable(void);
        MC_ErrorCode stop(void);
        MC_ErrorCode reset(bool &isDone);

    private:
        Axis *mMembers[URANUS_AXESGROUP_IDENT_NUM] = {nullptr};
        std::size_t mMemberCount = 0;
        bool mEnabled = false;
    };

} // namespace plcopen

#endif /** _URANUS_AXESGROUP_HPP_ **/
