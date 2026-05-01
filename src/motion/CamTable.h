#ifndef _URANUS_CAMTABLE_HPP_
#define _URANUS_CAMTABLE_HPP_

#include <utility>
#include <vector>

namespace plcopen
{

    class CamTable
    {
    public:
        void addPoint(double masterPosition, double slavePosition);
        void setPeriodic(bool periodic);
        bool periodic(void) const;
        bool empty(void) const;
        double sample(double masterPosition) const;

    private:
        std::vector<std::pair<double, double>> mPoints;
        bool mPeriodic = false;
    };

} // namespace plcopen

#endif /** _URANUS_CAMTABLE_HPP_ **/
