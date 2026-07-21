#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "geom/frame.h"
#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::axis
{

class AxisGroup;

// Internal: precomputed Cartesian segment geometry, resolved by the
// submit_linear pre-validation. Not a user input.
struct CartesianSegment
{
    bool pose = false;
    bool angle_driven = false;
    bool arc_path = false;
    geom::ArcSegment arc{};
    bool chain = false;
    double line1 = 0.0;
    double line2 = 0.0;
    geom::Vec3 dir1{};
    geom::Vec3 dir2{};
    geom::Vec3 exit_point{};
    geom::QuinticBlendSegment corner{};
    geom::Vec3 start{};
    geom::Vec3 delta{};
    double rotation_start[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    double axis[3] = {0.0, 0.0, 1.0};
    double angle = 0.0;
    double length = 0.0;
};

// Cartesian path/window runtime state. Frame, tool, kinematics and the
// Cartesian/Jog overlay scratch deliberately remain owned by AxisGroup.
template <std::size_t MaxAxes>
class GroupCartesianState
{
    friend class AxisGroup;

    static constexpr std::size_t WindowCapacity = 32; // <= 16 segments

    struct Piece
    {
        bool corner = false;
        geom::Vec3 start{};
        geom::Vec3 dir{};
        double length = 0.0;
        geom::QuinticBlendSegment blend{};
        double v_in = 0.0;
        double v_out = 0.0;
        double cap = 0.0;
        otg::Profile1D profile{};
        std::int64_t duration = 0;
    };

    class Storage
    {
      public:
        Storage()
            : data_(new rt::StaticVector<Piece, WindowCapacity>())
        {
        }

        bool empty() const { return data_->empty(); }
        std::size_t size() const { return data_->size(); }
        Piece &operator[](std::size_t index) { return (*data_)[index]; }
        const Piece &operator[](std::size_t index) const { return (*data_)[index]; }
        rt::ErrorCode push_back(const Piece &value) { return data_->push_back(value); }
        void pop_back() { data_->pop_back(); }
        void clear() { data_->clear(); }

      private:
        std::unique_ptr<rt::StaticVector<Piece, WindowCapacity>> data_;
    };

    double joints_[MaxAxes] = {};
    double window_joints_[MaxAxes] = {};
    CartesianSegment active_segment_{};
    otg::Profile1D halt_profile_{};
    Storage window_{};
    rt::ErrorCode last_error_ = rt::ErrorCode::ok;
    std::uint32_t window_last_id_ = 0;
    std::size_t piece_index_ = 0;
    std::int64_t piece_tick_ = 0;
    std::int64_t halt_tick_ = 0;
    std::int64_t halt_duration_ = 0;
    double halt_origin_ = 0.0;
    double halt_piece_offset_ = 0.0;
    double window_entry_velocity_ = 0.0;
    double window_entry_acceleration_ = 0.0;
    double window_acceleration_ = 0.0;
    double window_deceleration_ = 0.0;
    double window_jerk_ = 0.0;
    bool window_active_ = false;
    bool window_stopping_ = false;
};

} // namespace plcopen::core::axis
