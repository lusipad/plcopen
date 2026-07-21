#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "rt/error.h"
#include "rt/static_vector.h"

// A5 joint-space look-ahead window state (KB-032 and later Part 4 reuse).
//
// This is the second AxisGroup split cluster. It owns only the fixed-capacity
// preplanned window and its lifecycle state. AxisGroup deliberately keeps the
// shared command queue, group status/error state and direct-command completion
// points because those coordinate the other motion domains as well.

namespace plcopen::core::axis
{

class AxisGroup;

template <std::size_t MaxAxes>
class GroupLookaheadWindow
{
    friend class AxisGroup;

    static constexpr std::size_t BlendTableSize = 33;
    static constexpr std::size_t Capacity = 64;

    struct Node
    {
        bool has_curve = false;
        std::array<std::array<double, MaxAxes>, 6> ctrl{};
        std::array<double, BlendTableSize> cumulative{};
        double curve_length = 0.0;
        double corner_cap = 0.0;
        double curve_velocity = 0.0;
        std::int64_t curve_cycles = 0;
    };

    enum class Kind
    {
        line,
        direct_line,
        arc,
    };

    struct Segment
    {
        Kind kind = Kind::line;
        std::array<double, MaxAxes> entry{};
        std::array<double, MaxAxes> dir{};
        std::array<double, MaxAxes> target{};
        double full_length = 0.0;
        double trim_in = 0.0;
        double trim_out = 0.0;
        geom::ArcSegment arc_geom{};
        otg::Limits1D limits{};
        std::uint32_t command_id = 0;
        double entry_velocity = 0.0;
        double exit_velocity = 0.0;
        otg::Profile1D profile{};
        Node node{};

        double line_length() const
        {
            const double length = full_length - trim_in - trim_out;
            return length > 0.0 ? length : 0.0;
        }
    };

    class Storage
    {
      public:
        Storage()
            : data_(new rt::StaticVector<Segment, Capacity>())
        {
        }

        bool empty() const { return data_->empty(); }
        std::size_t size() const { return data_->size(); }
        Segment &operator[](std::size_t index) { return (*data_)[index]; }
        const Segment &operator[](std::size_t index) const { return (*data_)[index]; }
        rt::ErrorCode push_back(const Segment &value) { return data_->push_back(value); }
        void pop_back() { data_->pop_back(); }
        void clear() { data_->clear(); }

      private:
        std::unique_ptr<rt::StaticVector<Segment, Capacity>> data_;
    };

    Storage segments_{};
    otg::Profile1D stop_profile_{};
    otg::State1D seed_state_{};
    std::size_t depth_ = Capacity;
    std::size_t index_ = 0;
    std::int64_t tick_ = 0;
    double stop_origin_ = 0.0;
    bool active_ = false;
    bool in_curve_ = false;
    bool stopping_ = false;
    bool override_paused_ = false;
};

} // namespace plcopen::core::axis
